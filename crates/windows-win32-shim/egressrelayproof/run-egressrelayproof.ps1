<#
.SYNOPSIS
    MW18-4 end-to-end egress-isolation proof: aliased `wordy` relay + a real
    `merriam` service, exercising the `.pilcfg` egress modes (redirect / buffer /
    replay) plus a non-aliased control.

.DESCRIPTION
    The validation-tier capstone (windows-win32-shim SHIM-D23). It links the
    `wordy-relay-probe` bin against the shim's alias object so the probe's WinHTTP
    calls (made through `wordy`'s real `MerriamClient` relay) are rerouted into the
    shim's egress engine, then drives one merriam instance:

      1. cargo build -p windows-win32-shim     -> shim DLL + import lib + gen-alias-obj
      2. gen-alias-obj                          -> windows_win32_shim_alias.obj
      3. cargo build -p wordy --bin wordy-relay-probe  (WORDY_EXTRA_LINK_* set)
                                                -> aliased probe
      4. cargo build -p wordy --bin wordy-relay-probe  (env unset)
                                                -> native (control) probe
      5. cargo build -p merriam --bin merriam-host
      6. start merriam-host on a free loopback port (the live target)

    Scenarios (the aliased probe drives `wordy`'s relay; the harness asserts
    merriam's state with a *non-aliased* direct query):

      redirect  aliased  add  -> .pilcfg redirects a DEAD port to the live merriam;
                                 the word lands in merriam (proves diversion).
      buffer    aliased  add  -> .pilcfg buffer captures the POST; merriam is
                                 NEVER contacted (the word is absent; probe sees a
                                 synthetic 202 -> exit 2).
      replay    aliased  cont -> .pilcfg replay serves a fixture for a GET with no
                                 network (probe sees exists:true -> exit 0).
      control   native   add  -> genuine WinHTTP hits the DEAD port and fails
                                 (exit 3); proves the alias is what diverts.

    Exit code: 0 PASS (all scenarios) / 1 FAIL / 2 SKIP (merriam URL unbindable —
    needs a urlacl reservation or elevation).

.PARAMETER Configuration
    Cargo profile: 'debug' (default) or 'release'.

.PARAMETER SkipCargo
    Reuse existing target/<cfg> artifacts (still rebuilds the two probe variants).
#>
[CmdletBinding()]
param(
    [ValidateSet('debug', 'release')]
    [string]$Configuration = 'debug',
    [switch]$SkipCargo
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$EXIT_PASS = 0
$EXIT_FAIL = 1
$EXIT_SKIP = 2

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$crateDir  = Split-Path -Parent $scriptDir
$repoRoot  = (Resolve-Path (Join-Path $crateDir '..\..')).Path
$targetDir = Join-Path $repoRoot "target\$Configuration"
$outDir    = Join-Path $repoRoot ".scratch\egressrelayproof"
$replayDir = Join-Path $outDir 'replay'
$storeDir  = Join-Path $outDir 'merriam-store'

New-Item -ItemType Directory -Force -Path $outDir | Out-Null
New-Item -ItemType Directory -Force -Path $replayDir | Out-Null

function Invoke-Checked {
    param([string]$Exe, [string[]]$Arguments, [string]$What)
    Write-Host ">> $What" -ForegroundColor Cyan
    & $Exe @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "$What failed (exit $LASTEXITCODE): $Exe $($Arguments -join ' ')"
    }
}

function Get-FreeLoopbackPort {
    $l = [System.Net.Sockets.TcpListener]::new([System.Net.IPAddress]::Loopback, 0)
    $l.Start()
    $port = $l.LocalEndpoint.Port
    $l.Stop()
    return $port
}

# A non-aliased direct HTTP/1.1 request (used to query merriam's state).
function Invoke-Http {
    param([int]$Port, [string]$Method, [string]$Path, [string]$User = 'probe')
    try {
        $client = [System.Net.Sockets.TcpClient]::new()
        $client.Connect('127.0.0.1', $Port)
        $stream = $client.GetStream()
        $stream.ReadTimeout = 3000
        $req = "$Method $Path HTTP/1.1`r`nHost: 127.0.0.1`r`nX-Wordy-User: $User`r`nConnection: close`r`nContent-Length: 0`r`n`r`n"
        $bytes = [System.Text.Encoding]::ASCII.GetBytes($req)
        $stream.Write($bytes, 0, $bytes.Length)
        $reader = [System.IO.StreamReader]::new($stream)
        $text = $reader.ReadToEnd()
        $client.Close()
        $body = ''
        $idx = $text.IndexOf("`r`n`r`n")
        if ($idx -ge 0) { $body = $text.Substring($idx + 4) }
        return $body
    } catch {
        return ''
    }
}

$cfgFlag = @()
if ($Configuration -eq 'release') { $cfgFlag = @('--release') }

# --- 1. Build the shim + 2. emit the alias object -----------------------------
if (-not $SkipCargo) {
    Invoke-Checked 'cargo' (@('build', '-p', 'windows-win32-shim') + $cfgFlag) 'cargo build (shim)'
    Invoke-Checked 'cargo' (@('build', '-p', 'merriam', '--bin', 'merriam-host') + $cfgFlag) 'cargo build (merriam-host)'
}

$dll       = Join-Path $targetDir 'windows_win32_shim.dll'
$importLib = Join-Path $targetDir 'windows_win32_shim.dll.lib'
$genTool   = Join-Path $targetDir 'gen-alias-obj.exe'
$merriamHost = Join-Path $targetDir 'merriam-host.exe'
foreach ($p in @($dll, $importLib, $genTool, $merriamHost)) {
    if (-not (Test-Path $p)) { throw "expected build artifact not found: $p (run without -SkipCargo)" }
}

$aliasObj = Join-Path $outDir 'windows_win32_shim_alias.obj'
Invoke-Checked $genTool @('--out', $aliasObj) 'gen-alias-obj (emit alias COFF)'

# --- 3. Build the ALIASED probe (WinHTTP imports rerouted into the shim) -------
$env:WORDY_EXTRA_LINK_SEARCH = $targetDir
$env:WORDY_EXTRA_LINK_OBJ = "$aliasObj;$importLib"
Invoke-Checked 'cargo' (@('build', '-p', 'wordy', '--bin', 'wordy-relay-probe') + $cfgFlag) 'cargo build (aliased probe)'
$aliasedProbe = Join-Path $outDir 'wordy-relay-probe-aliased.exe'
Copy-Item -Path (Join-Path $targetDir 'wordy-relay-probe.exe') -Destination $aliasedProbe -Force

# --- 4. Build the NATIVE probe (genuine WinHTTP) for the control ---------------
Remove-Item Env:WORDY_EXTRA_LINK_SEARCH -ErrorAction SilentlyContinue
Remove-Item Env:WORDY_EXTRA_LINK_OBJ -ErrorAction SilentlyContinue
Invoke-Checked 'cargo' (@('build', '-p', 'wordy', '--bin', 'wordy-relay-probe') + $cfgFlag) 'cargo build (native probe)'
$nativeProbe = Join-Path $outDir 'wordy-relay-probe-native.exe'
Copy-Item -Path (Join-Path $targetDir 'wordy-relay-probe.exe') -Destination $nativeProbe -Force

# Stage the shim DLL next to the aliased probe.
Copy-Item -Path $dll -Destination $outDir -Force

# --- 5/6. Ports + start a real merriam on the live port -----------------------
$livePort = Get-FreeLoopbackPort
$deadPort = Get-FreeLoopbackPort   # reserved then released -> guaranteed closed
$liveUrl = "http://127.0.0.1:$livePort/"
if (Test-Path $storeDir) { Remove-Item $storeDir -Recurse -Force }

Write-Host ">> starting merriam-host on $liveUrl" -ForegroundColor Cyan
$merriamOut = Join-Path $outDir 'merriam-out.txt'
$merriamErr = Join-Path $outDir 'merriam-err.txt'
# The child inherits this process's environment; set the merriam config here.
$env:MERRIAM_URL = $liveUrl
$env:MERRIAM_ROOT = $storeDir
$merriamProc = Start-Process -FilePath $merriamHost -PassThru -NoNewWindow `
    -RedirectStandardOutput $merriamOut -RedirectStandardError $merriamErr
Remove-Item Env:MERRIAM_URL -ErrorAction SilentlyContinue
Remove-Item Env:MERRIAM_ROOT -ErrorAction SilentlyContinue

# Wait (bounded) for the readiness banner or a bind failure.
$ready = $false
for ($i = 0; $i -lt 50; $i++) {
    if (Test-Path $merriamOut) {
        $out = Get-Content $merriamOut -Raw -ErrorAction SilentlyContinue
        if ($out -and $out.Contains('merriam listening')) { $ready = $true; break }
    }
    if (Test-Path $merriamErr) {
        $err = Get-Content $merriamErr -Raw -ErrorAction SilentlyContinue
        if ($err -and $err.Contains('failed to bind')) { break }
    }
    if ($merriamProc.HasExited) { break }
    Start-Sleep -Milliseconds 100
}

if (-not $ready) {
    Write-Host "SKIP: merriam could not bind $liveUrl (needs a urlacl reservation or elevation)." -ForegroundColor Yellow
    if (-not $merriamProc.HasExited) { Stop-Process -Id $merriamProc.Id -Force -ErrorAction SilentlyContinue }
    exit $EXIT_SKIP
}

# --- Replay fixture (served for the replay scenario with no network) ----------
$fixture = @"
<Egress>
  <Fixture verb="GET" path="/custom/widget3" status="200">
    <Header name="Content-Type" value="application/json"/>
    <Body>{"word":"widget3","exists":true}</Body>
  </Fixture>
</Egress>
"@
Set-Content -Path (Join-Path $replayDir 'fixture.xml') -Value $fixture -Encoding ascii

$results = @()
function Add-Check {
    param([string]$Name, [bool]$Pass, [string]$Detail)
    $tag = if ($Pass) { '[ ok ]' } else { '[FAIL]' }
    $color = if ($Pass) { 'Green' } else { 'Red' }
    Write-Host "  $tag $Name -- $Detail" -ForegroundColor $color
    $script:results += $Pass
}

function Set-Pilcfg {
    param([string]$Json)
    Set-Content -Path "$aliasedProbe.pilcfg" -Value $Json -Encoding ascii -NoNewline
}

try {
    # redirect: dead port diverted to the live merriam; the word lands in merriam.
    Write-Host "`n== redirect ==" -ForegroundColor Magenta
    Set-Pilcfg ('{ "egress": { "mode": "redirect", "redirections": [ { "from": "127.0.0.1:' + $deadPort + '", "to": "127.0.0.1:' + $livePort + '" } ] } }')
    & $aliasedProbe 'add' '127.0.0.1' "$deadPort" 'widget1' 'probe' | Out-Null
    $rc = $LASTEXITCODE
    $listed = Invoke-Http -Port $livePort -Method 'GET' -Path '/custom'
    Add-Check 'redirect diverts the dead target into the live merriam' `
        ($rc -eq 0 -and $listed -match 'widget1') "probe-exit=$rc merriam-list='$listed'"

    # buffer: the POST is captured; merriam is never contacted.
    Write-Host "`n== buffer ==" -ForegroundColor Magenta
    Set-Pilcfg '{ "egress": { "mode": "buffer" } }'
    & $aliasedProbe 'add' '127.0.0.1' "$livePort" 'widget2' 'probe' | Out-Null
    $rc = $LASTEXITCODE
    $listed = Invoke-Http -Port $livePort -Method 'GET' -Path '/custom'
    Add-Check 'buffer captures the POST; merriam is untouched (no widget2)' `
        ($rc -eq 2 -and $listed -notmatch 'widget2') "probe-exit=$rc merriam-list='$listed'"

    # replay: a fixture answers the GET with no network at all.
    Write-Host "`n== replay ==" -ForegroundColor Magenta
    Set-Pilcfg ('{ "egress": { "mode": "replay", "replay_dir": "' + ($replayDir -replace '\\', '/') + '" } }')
    & $aliasedProbe 'contains' '127.0.0.1' "$deadPort" 'widget3' 'probe' | Out-Null
    $rc = $LASTEXITCODE
    Add-Check 'replay serves a fixture (exists:true) with no network' `
        ($rc -eq 0) "probe-exit=$rc"

    # control: the non-aliased probe hits the real (dead) target and fails.
    Write-Host "`n== control (non-aliased) ==" -ForegroundColor Magenta
    if (Test-Path "$nativeProbe.pilcfg") { Remove-Item "$nativeProbe.pilcfg" -Force }
    & $nativeProbe 'add' '127.0.0.1' "$deadPort" 'widget4' 'probe' | Out-Null
    $rc = $LASTEXITCODE
    Add-Check 'non-aliased control reaches the real (dead) target and fails' `
        ($rc -eq 3) "probe-exit=$rc"
}
finally {
    if (-not $merriamProc.HasExited) { Stop-Process -Id $merriamProc.Id -Force -ErrorAction SilentlyContinue }
}

$passed = @($results | Where-Object { $_ }).Count
$total  = @($results).Count
Write-Host ""
if ($passed -eq $total) {
    Write-Host "egress-relay-proof PASSED: $passed/$total scenarios." -ForegroundColor Green
    exit $EXIT_PASS
} else {
    Write-Host "egress-relay-proof FAILED: $passed/$total scenarios." -ForegroundColor Red
    exit $EXIT_FAIL
}
