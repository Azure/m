<#
.SYNOPSIS
    Launch the two validation-tier services in tandem for manual exploration:
    the `merriam` dictionary-store service and `wordy`'s aliased relay, under a
    chosen `.pilcfg` egress mode.

.DESCRIPTION
    This is the interactive companion to `run-egressrelayproof.ps1` (which runs
    the same wiring as an automated proof). It builds the shim + alias object, the
    aliased `wordy-relay-probe`, and `merriam-host`; starts a real `merriam` on a
    free loopback port; stages the aliased probe's `.pilcfg` with the requested
    egress mode; and prints ready-to-paste commands so you can drive `wordy`'s
    relay through the shim's egress engine against the live `merriam` and observe
    the effect. `merriam` keeps running until you press ENTER.

    The "two services": `merriam` (the running storage service) and the aliased
    `wordy` relay (the probe you invoke). With the egress mode set to `buffer` or
    `replay`, `merriam` is never contacted; with `redirect`, a dead port is
    diverted into `merriam`; with `passthrough`, the relay reaches `merriam`
    directly.

.PARAMETER Mode
    The egress mode to stage: passthrough (default) / redirect / buffer / replay.

.PARAMETER Configuration
    Cargo profile: 'debug' (default) or 'release'.
#>
[CmdletBinding()]
param(
    [ValidateSet('passthrough', 'redirect', 'buffer', 'replay')]
    [string]$Mode = 'passthrough',
    [ValidateSet('debug', 'release')]
    [string]$Configuration = 'debug'
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

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
    if ($LASTEXITCODE -ne 0) { throw "$What failed (exit $LASTEXITCODE)" }
}

function Get-FreeLoopbackPort {
    $l = [System.Net.Sockets.TcpListener]::new([System.Net.IPAddress]::Loopback, 0)
    $l.Start(); $port = $l.LocalEndpoint.Port; $l.Stop(); return $port
}

$cfgFlag = @(); if ($Configuration -eq 'release') { $cfgFlag = @('--release') }

Invoke-Checked 'cargo' (@('build', '-p', 'windows-win32-shim') + $cfgFlag) 'cargo build (shim)'
Invoke-Checked 'cargo' (@('build', '-p', 'merriam', '--bin', 'merriam-host') + $cfgFlag) 'cargo build (merriam-host)'

$dll       = Join-Path $targetDir 'windows_win32_shim.dll'
$importLib = Join-Path $targetDir 'windows_win32_shim.dll.lib'
$genTool   = Join-Path $targetDir 'gen-alias-obj.exe'
$merriamHost = Join-Path $targetDir 'merriam-host.exe'

$aliasObj = Join-Path $outDir 'windows_win32_shim_alias.obj'
Invoke-Checked $genTool @('--out', $aliasObj) 'gen-alias-obj'

$env:WORDY_EXTRA_LINK_SEARCH = $targetDir
$env:WORDY_EXTRA_LINK_OBJ = "$aliasObj;$importLib"
Invoke-Checked 'cargo' (@('build', '-p', 'wordy', '--bin', 'wordy-relay-probe') + $cfgFlag) 'cargo build (aliased probe)'
Remove-Item Env:WORDY_EXTRA_LINK_SEARCH -ErrorAction SilentlyContinue
Remove-Item Env:WORDY_EXTRA_LINK_OBJ -ErrorAction SilentlyContinue

$probe = Join-Path $outDir 'wordy-relay-probe-aliased.exe'
Copy-Item -Path (Join-Path $targetDir 'wordy-relay-probe.exe') -Destination $probe -Force
Copy-Item -Path $dll -Destination $outDir -Force

$livePort = Get-FreeLoopbackPort
$deadPort = Get-FreeLoopbackPort
$liveUrl = "http://127.0.0.1:$livePort/"
if (Test-Path $storeDir) { Remove-Item $storeDir -Recurse -Force }

# Replay fixture (used by the replay mode).
$fixture = @"
<Egress>
  <Fixture verb="GET" path="/custom/widget" status="200">
    <Header name="Content-Type" value="application/json"/>
    <Body>{"word":"widget","exists":true}</Body>
  </Fixture>
</Egress>
"@
Set-Content -Path (Join-Path $replayDir 'fixture.xml') -Value $fixture -Encoding ascii

# Stage the egress .pilcfg for the chosen mode.
switch ($Mode) {
    'redirect'   { $cfg = '{ "egress": { "mode": "redirect", "redirections": [ { "from": "127.0.0.1:' + $deadPort + '", "to": "127.0.0.1:' + $livePort + '" } ] } }' }
    'buffer'     { $cfg = '{ "egress": { "mode": "buffer" } }' }
    'replay'     { $cfg = '{ "egress": { "mode": "replay", "replay_dir": "' + ($replayDir -replace '\\', '/') + '" } }' }
    default      { $cfg = '{ "egress": { "mode": "passthrough" } }' }
}
Set-Content -Path "$probe.pilcfg" -Value $cfg -Encoding ascii -NoNewline

Write-Host ">> starting merriam-host on $liveUrl (store=$storeDir)" -ForegroundColor Cyan
$merriamOut = Join-Path $outDir 'merriam-out.txt'
$merriamErr = Join-Path $outDir 'merriam-err.txt'
$env:MERRIAM_URL = $liveUrl
$env:MERRIAM_ROOT = $storeDir
$merriamProc = Start-Process -FilePath $merriamHost -PassThru -NoNewWindow `
    -RedirectStandardOutput $merriamOut -RedirectStandardError $merriamErr
Remove-Item Env:MERRIAM_URL -ErrorAction SilentlyContinue
Remove-Item Env:MERRIAM_ROOT -ErrorAction SilentlyContinue

$ready = $false
for ($i = 0; $i -lt 50; $i++) {
    if ((Test-Path $merriamOut) -and ((Get-Content $merriamOut -Raw -ErrorAction SilentlyContinue) -match 'merriam listening')) { $ready = $true; break }
    if ($merriamProc.HasExited) { break }
    Start-Sleep -Milliseconds 100
}
if (-not $ready) {
    Write-Host "merriam could not bind $liveUrl (needs a urlacl reservation or elevation)." -ForegroundColor Yellow
    if (-not $merriamProc.HasExited) { Stop-Process -Id $merriamProc.Id -Force -ErrorAction SilentlyContinue }
    exit 2
}

# For redirect mode the relay should target the DEAD port (which gets diverted);
# for the other modes target the live port directly.
$relayPort = if ($Mode -eq 'redirect') { $deadPort } else { $livePort }

Write-Host ""
Write-Host "=== Tandem up: merriam + aliased wordy relay (egress mode: $Mode) ===" -ForegroundColor Green
Write-Host "  merriam  : $liveUrl   (store: $storeDir)" -ForegroundColor Gray
Write-Host "  aliased probe (wordy relay through the shim egress engine):" -ForegroundColor Gray
Write-Host "    & '$probe' add 127.0.0.1 $relayPort widget probe" -ForegroundColor White
Write-Host "    & '$probe' contains 127.0.0.1 $relayPort widget probe" -ForegroundColor White
Write-Host "  query merriam directly (non-aliased):" -ForegroundColor Gray
Write-Host "    (curl) http://127.0.0.1:$livePort/custom   with header X-Wordy-User: probe" -ForegroundColor White
Write-Host ""
Write-Host "Press ENTER to stop merriam and exit." -ForegroundColor Yellow
[void][System.Console]::ReadLine()

if (-not $merriamProc.HasExited) { Stop-Process -Id $merriamProc.Id -Force -ErrorAction SilentlyContinue }
Write-Host "merriam stopped." -ForegroundColor Cyan
