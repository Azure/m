<#
.SYNOPSIS
    MW17-5 egress-proof: build the Rust shim, emit its alias COFF object, then
    build and run a genuine-WinHTTP-call C++ EXE that links the alias object,
    proving the outbound HTTP calls are diverted into the shim's egress engine
    and steered by the .pilcfg egress mode (redirect / buffer / replay), while a
    non-aliased control reaches the real target.

.DESCRIPTION
    The network-seam analogue of run-linkproof.ps1. It exercises the full
    production link recipe end to end and then four scenarios:

      1. cargo build -p windows-win32-shim   -> windows_win32_shim.dll(+.dll.lib)
      2. gen-alias-obj                        -> windows_win32_shim_alias.obj
      3. cl /c egressproof_main.cpp           -> egressproof_main.obj
      4a. link obj + alias.obj + shim.dll.lib -> egressproof_aliased.exe
      4b. link obj + winhttp.lib              -> egressproof_control.exe
      5. stage the shim DLL next to the aliased exe

    Scenarios (all target a CLOSED loopback port so a genuine send fails fast):

      redirect  aliased  GET  -> .pilcfg redirects the dead port to a live
                                 loopback echo; expect 200 + the echo marker
                                 (the dead target is never contacted).
      buffer    aliased  POST -> .pilcfg buffer mode journals the mutation and
                                 acks 202 without any network (no listener).
      replay    aliased  GET  -> .pilcfg replay mode serves a fixture (203 +
                                 marker) with no listener at all.
      control   control  GET  -> genuine WinHTTP hits the real (dead) target and
                                 fails; proves the alias is what diverts.

    Exit code is the discriminator: 0 = PASS (all scenarios), 1 = FAIL, 2 = SKIP
    (prerequisite missing: no MSVC toolchain, or a port could not be reserved).

.PARAMETER Configuration
    Cargo profile to build/link against: 'debug' (default) or 'release'.

.PARAMETER SkipCargo
    Skip the `cargo build` step and use whatever is already in target/<cfg>/.

.NOTES
    Requires the MSVC toolchain. If `cl.exe` is not already on PATH the script
    locates it via vswhere and imports the x64 developer environment; if neither
    is available the script SKIPs (exit 2) rather than failing.
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
$outDir    = Join-Path $repoRoot ".scratch\egressproof"
$replayDir = Join-Path $outDir 'replay'

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

# Reserve a free loopback TCP port, then release it. Used both for the dead
# (left closed) port and to choose a port for the echo server to bind.
function Get-FreeLoopbackPort {
    $l = [System.Net.Sockets.TcpListener]::new([System.Net.IPAddress]::Loopback, 0)
    $l.Start()
    $port = $l.LocalEndpoint.Port
    $l.Stop()
    return $port
}

# --- 1. Build the Rust shim cdylib (produces the DLL + its import library) -----
$cargoArgs = @('build', '-p', 'windows-win32-shim')
if ($Configuration -eq 'release') { $cargoArgs += '--release' }
if (-not $SkipCargo) {
    Invoke-Checked 'cargo' $cargoArgs 'cargo build (cdylib + gen-alias-obj)'
}

$dll       = Join-Path $targetDir 'windows_win32_shim.dll'
$importLib = Join-Path $targetDir 'windows_win32_shim.dll.lib'
$genTool   = Join-Path $targetDir 'gen-alias-obj.exe'
foreach ($p in @($dll, $importLib, $genTool)) {
    if (-not (Test-Path $p)) { throw "expected build artifact not found: $p (run without -SkipCargo)" }
}

# --- 2. Emit the alias COFF object from the checked-in NDJSON manifest ---------
$aliasObj = Join-Path $outDir 'windows_win32_shim_alias.obj'
Invoke-Checked $genTool @('--out', $aliasObj) 'gen-alias-obj (emit alias COFF)'

# --- Locate the MSVC toolchain (import the x64 dev environment if needed) ------
if (-not (Get-Command cl.exe -ErrorAction SilentlyContinue)) {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (-not (Test-Path $vswhere)) {
        Write-Host "SKIP: cl.exe not on PATH and vswhere.exe not found." -ForegroundColor Yellow
        exit $EXIT_SKIP
    }
    $vsRoot = & $vswhere -latest -prerelease -products * -property installationPath
    if (-not $vsRoot) {
        Write-Host "SKIP: cl.exe not on PATH and no Visual Studio installation found." -ForegroundColor Yellow
        exit $EXIT_SKIP
    }
    $vcvars = Join-Path $vsRoot 'VC\Auxiliary\Build\vcvars64.bat'
    if (-not (Test-Path $vcvars)) {
        Write-Host "SKIP: vcvars64.bat not found under $vsRoot." -ForegroundColor Yellow
        exit $EXIT_SKIP
    }
    Write-Host ">> importing MSVC x64 environment from $vcvars" -ForegroundColor Cyan
    cmd /c "`"$vcvars`" >nul && set" | ForEach-Object {
        if ($_ -match '^([^=]+)=(.*)$') { Set-Item -Path "Env:$($matches[1])" -Value $matches[2] }
    }
}

# --- 3. Compile the genuine-WinHTTP-call egress-proof TU -----------------------
$cppSrc = Join-Path $scriptDir 'egressproof_main.cpp'
$cppObj = Join-Path $outDir 'egressproof_main.obj'
Invoke-Checked 'cl.exe' @(
    '/nologo', '/c', '/EHsc', '/std:c++17', '/W4',
    "/Fo$cppObj", $cppSrc
) 'cl (compile egress-proof TU)'

# --- 4a. Aliased EXE: TU + alias object + the shim's import library -------------
$aliasedExe = Join-Path $outDir 'egressproof_aliased.exe'
Invoke-Checked 'link.exe' @(
    '/nologo', "/OUT:$aliasedExe", $cppObj, $aliasObj, $importLib
) 'link (aliased egress-proof EXE)'

# --- 4b. Control EXE: TU + genuine winhttp import library -----------------------
$controlExe = Join-Path $outDir 'egressproof_control.exe'
Invoke-Checked 'link.exe' @(
    '/nologo', "/OUT:$controlExe", $cppObj, 'winhttp.lib'
) 'link (control egress-proof EXE)'

# --- 5. Stage the shim DLL next to the aliased EXE -----------------------------
Copy-Item -Path $dll -Destination $outDir -Force

# --- Ports + a loopback echo server for the redirect scenario ------------------
$deadPort = Get-FreeLoopbackPort   # reserved then released -> guaranteed closed
$echoPort = Get-FreeLoopbackPort   # the echo job re-binds this immediately
if (-not $deadPort -or -not $echoPort) {
    Write-Host "SKIP: could not reserve loopback ports." -ForegroundColor Yellow
    exit $EXIT_SKIP
}

$echoScript = {
    param($port)
    $listener = [System.Net.Sockets.TcpListener]::new([System.Net.IPAddress]::Loopback, [int]$port)
    $listener.Start()
    try {
        while ($true) {
            $client = $listener.AcceptTcpClient()
            $stream = $client.GetStream()
            $stream.ReadTimeout = 1000
            $buf = New-Object byte[] 4096
            try { [void]$stream.Read($buf, 0, $buf.Length) } catch {}
            $body = 'EGRESSPROOF-ECHO'
            $resp = "HTTP/1.1 200 OK`r`nContent-Type: text/plain`r`nContent-Length: $($body.Length)`r`nConnection: close`r`n`r`n$body"
            $bytes = [System.Text.Encoding]::ASCII.GetBytes($resp)
            $stream.Write($bytes, 0, $bytes.Length)
            $stream.Flush()
            $client.Close()
        }
    } finally { $listener.Stop() }
}
Write-Host ">> starting loopback echo server on 127.0.0.1:$echoPort" -ForegroundColor Cyan
$echoJob = Start-Job -ScriptBlock $echoScript -ArgumentList $echoPort

# --- Replay fixture (served with no listener) ----------------------------------
$fixture = @"
<Egress>
  <Fixture verb="GET" path="/probe" status="203">
    <Header name="Content-Type" value="text/plain"/>
    <Body>EGRESSPROOF-REPLAY</Body>
  </Fixture>
</Egress>
"@
Set-Content -Path (Join-Path $replayDir 'fixture.xml') -Value $fixture -Encoding ascii

# --- Scenario runner -----------------------------------------------------------
# The aliased EXE's stdout is swallowed (the CRT's WriteFile(stdout) is itself
# aliased into the shim's mWriteFile), so the verdict travels only in the EXIT
# CODE: the EXE asserts against <expect> internally and returns 0 on a match.
function Invoke-Scenario {
    param([string]$Exe, [string]$Pilcfg, [string]$Verb, [int]$Port, [string]$Expect)
    if ($Pilcfg) {
        Set-Content -Path "$Exe.pilcfg" -Value $Pilcfg -Encoding ascii -NoNewline
    } elseif (Test-Path "$Exe.pilcfg") {
        Remove-Item "$Exe.pilcfg" -Force
    }
    $out = & $Exe '127.0.0.1' "$Port" $Verb '/probe' $Expect 2>&1
    $rc  = $LASTEXITCODE
    [pscustomobject]@{ Exit = $rc; Text = (($out | Out-String).Trim()) }
}

$results = @()
function Add-Check {
    param([string]$Name, [bool]$Pass, [string]$Detail)
    $tag = if ($Pass) { '[ ok ]' } else { '[FAIL]' }
    $color = if ($Pass) { 'Green' } else { 'Red' }
    Write-Host "  $tag $Name -- $Detail" -ForegroundColor $color
    $script:results += $Pass
}

try {
    $redirectCfg = '{ "egress": { "mode": "redirect", "redirections": [ { "from": "127.0.0.1:' + $deadPort + '", "to": "127.0.0.1:' + $echoPort + '" } ] } }'
    $bufferCfg   = '{ "egress": { "mode": "buffer" } }'
    $replayCfg   = '{ "egress": { "mode": "replay", "replay_dir": "' + ($replayDir -replace '\\', '/') + '" } }'

    Write-Host "`n== redirect ==" -ForegroundColor Magenta
    $r = Invoke-Scenario -Exe $aliasedExe -Pilcfg $redirectCfg -Verb 'GET' -Port $deadPort -Expect 'status=200,body=EGRESSPROOF-ECHO'
    Add-Check 'redirect diverts the dead target to the loopback echo' `
        ($r.Exit -eq 0) "exit=$($r.Exit) :: $($r.Text -replace '\r?\n', ' | ')"

    Write-Host "`n== buffer ==" -ForegroundColor Magenta
    $r = Invoke-Scenario -Exe $aliasedExe -Pilcfg $bufferCfg -Verb 'POST' -Port $deadPort -Expect 'status=202'
    Add-Check 'buffer journals the mutation and acks 202 with no network' `
        ($r.Exit -eq 0) "exit=$($r.Exit) :: $($r.Text -replace '\r?\n', ' | ')"

    Write-Host "`n== replay ==" -ForegroundColor Magenta
    $r = Invoke-Scenario -Exe $aliasedExe -Pilcfg $replayCfg -Verb 'GET' -Port $deadPort -Expect 'status=203,body=EGRESSPROOF-REPLAY'
    Add-Check 'replay serves a fixture with no listener' `
        ($r.Exit -eq 0) "exit=$($r.Exit) :: $($r.Text -replace '\r?\n', ' | ')"

    Write-Host "`n== control (non-aliased) ==" -ForegroundColor Magenta
    $r = Invoke-Scenario -Exe $controlExe -Pilcfg $null -Verb 'GET' -Port $deadPort -Expect 'fail'
    Add-Check 'non-aliased control reaches the real (dead) target and fails' `
        ($r.Exit -eq 0) "exit=$($r.Exit) :: $($r.Text -replace '\r?\n', ' | ')"
}
finally {
    if ($echoJob) { Remove-Job -Job $echoJob -Force -ErrorAction SilentlyContinue }
}

$passed = @($results | Where-Object { $_ }).Count
$total  = @($results).Count
Write-Host ""
if ($passed -eq $total) {
    Write-Host "egress-proof PASSED: $passed/$total scenarios." -ForegroundColor Green
    exit $EXIT_PASS
} else {
    Write-Host "egress-proof FAILED: $passed/$total scenarios." -ForegroundColor Red
    exit $EXIT_FAIL
}
