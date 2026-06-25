<#
.SYNOPSIS
    MW15-4 / MW15-5 HWC isolation proof: genuinely activate Hostable Web Core with
    the unmodified `wordy` REST module and prove that the module's custom-store
    filesystem mutations are buffered into the shim overlay (isolated build) or
    reach the live filesystem (native control), driven entirely by whether
    `wordy.dll` was linked against the alias object.

.DESCRIPTION
    This is the end-to-end counterpart of `build-aliased-wordy.ps1`: where that
    script proves *link-time* redirection statically (via `dumpbin`), this script
    proves the *runtime* effect under a real IIS pipeline.

    The host (`wordy-host.exe`) is an ordinary native binary: it writes its
    generated `applicationHost.config`/`web.config` to real disk and binds the
    site. Only the loadable module (`wordy.dll`) is isolated, mirroring a native
    HWC worker hosting an isolated third-party module. With a buffered host
    sidecar (`wordy-host.exe.pilcfg = {"buffer_updates": true}`), the shim that
    `wordy.dll`'s filesystem calls bind buffers every namespace mutation into an
    in-memory overlay, so the live custom-store root is never created on disk.

    Variants:
      - isolated (default): `wordy.dll` is linked with the alias object + shim
        import library, so its FS calls bind the shim. EXPECT the live custom
        root to be ABSENT after a successful add/get/delete round-trip (the
        words lived only in the overlay; the HTTP round-trip still saw them, so
        read-your-writes held).
      - native: `wordy.dll` is built ordinarily; its FS calls bind kernel32.
        EXPECT the live custom root to be PRESENT, the negative control proving
        the overlay is real and the assertion is meaningful. The *same* buffered
        sidecar is staged, so the only difference is the alias link.

    Steps:
      1. Verify HWC is installed (else SKIP).
      2. Build the selected wordy variant; verify its shim binding with dumpbin
         (isolated: must bind the shim; native: must NOT).
      3. Stage the buffered host sidecar; point WORDY_CUSTOM_ROOT at a fresh,
         non-existent live directory.
      4. Run `wordy-host.exe` with WORDY_HOST_HTTP=1 (genuine WebCoreActivate +
         POST/GET/DELETE /custom/widget over real HTTP + WebCoreShutdown). If the
         URL cannot be reserved, SKIP (run elevated or add a urlacl).
      5. Assert the live custom root's presence matches the variant's expectation.

    Exit codes: 0 = PASS, 1 = FAIL, 2 = SKIP (HWC absent or URL unbindable).

.PARAMETER Configuration
    Cargo profile: 'debug' (default) or 'release'.

.PARAMETER Variant
    'isolated' (default) or 'native'.

.PARAMETER SkipShimBuild
    Skip rebuilding the shim cdylib (reuse target/<cfg>/).

.NOTES
    Requires the MSVC toolchain (for dumpbin) and, for a non-SKIP run, the HWC
    feature plus permission to reserve http://localhost:8080/ (elevation or a
    `netsh http add urlacl` grant).
#>
[CmdletBinding()]
param(
    [ValidateSet('debug', 'release')]
    [string]$Configuration = 'debug',
    [ValidateSet('isolated', 'native')]
    [string]$Variant = 'isolated',
    [switch]$SkipShimBuild
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$crateDir  = Split-Path -Parent $scriptDir
$repoRoot  = (Resolve-Path (Join-Path $crateDir '..\..')).Path
$targetDir = Join-Path $repoRoot "target\$Configuration"
$outDir    = Join-Path $repoRoot ".scratch\hwcproof"

New-Item -ItemType Directory -Force -Path $outDir | Out-Null

function Write-Result {
    param([string]$Status, [string]$Message)
    $color = switch ($Status) { 'PASS' { 'Green' } 'FAIL' { 'Red' } default { 'Yellow' } }
    Write-Host ""
    Write-Host "RESULT: $Status - $Message" -ForegroundColor $color
}

# --- 1. HWC presence ----------------------------------------------------------
$hwebcore = Join-Path $env:windir 'System32\inetsrv\hwebcore.dll'
if (-not (Test-Path $hwebcore)) {
    Write-Result 'SKIP' "HWC engine not installed ($hwebcore). Install IIS-HostableWebCore to run the genuine proof."
    exit 2
}
Write-Host ">> HWC engine present: $hwebcore" -ForegroundColor Cyan

# --- 2. Build the selected wordy variant + verify its shim binding ------------
if ($Variant -eq 'isolated') {
    $buildScript = Join-Path $scriptDir 'build-aliased-wordy.ps1'
    $buildArgs = @('-File', $buildScript, '-Configuration', $Configuration)
    if ($SkipShimBuild) { $buildArgs += '-SkipCargo' }
    Write-Host ">> building + verifying aliased wordy.dll" -ForegroundColor Cyan
    & powershell.exe -NoProfile -ExecutionPolicy Bypass @buildArgs
    if ($LASTEXITCODE -ne 0) { Write-Result 'FAIL' "aliased wordy build/verify failed (exit $LASTEXITCODE)."; exit 1 }
}
else {
    # Native control: build wordy with NO alias injection (clear the vars so the
    # build script reruns and emits an ordinary cdylib).
    if (-not $SkipShimBuild) {
        Write-Host ">> cargo build (shim cdylib, for co-located DLL)" -ForegroundColor Cyan
        $shimArgs = @('build', '-p', 'windows-win32-shim')
        if ($Configuration -eq 'release') { $shimArgs += '--release' }
        & cargo @shimArgs
        if ($LASTEXITCODE -ne 0) { Write-Result 'FAIL' "shim build failed."; exit 1 }
    }
    Remove-Item Env:WORDY_EXTRA_LINK_SEARCH, Env:WORDY_EXTRA_LINK_OBJ, Env:WORDY_EXTRA_LINK_LIB -ErrorAction SilentlyContinue
    Write-Host ">> cargo build (native wordy.dll, no alias)" -ForegroundColor Cyan
    $wordyArgs = @('build', '-p', 'wordy')
    if ($Configuration -eq 'release') { $wordyArgs += '--release' }
    & cargo @wordyArgs
    if ($LASTEXITCODE -ne 0) { Write-Result 'FAIL' "native wordy build failed."; exit 1 }
}

$wordyDll = Join-Path $targetDir 'wordy.dll'
$hostExe  = Join-Path $targetDir 'wordy-host.exe'
foreach ($p in @($wordyDll, $hostExe)) {
    if (-not (Test-Path $p)) { Write-Result 'FAIL' "expected artifact missing: $p"; exit 1 }
}

# Confirm the variant's shim binding via dumpbin (locate it through vcvars if needed).
if (-not (Get-Command dumpbin.exe -ErrorAction SilentlyContinue)) {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (Test-Path $vswhere) {
        $vsRoot = & $vswhere -latest -prerelease -products * -property installationPath
        $vcvars = Join-Path $vsRoot 'VC\Auxiliary\Build\vcvars64.bat'
        if (Test-Path $vcvars) {
            cmd /c "`"$vcvars`" >nul && set" | ForEach-Object {
                if ($_ -match '^([^=]+)=(.*)$') { Set-Item -Path "Env:$($matches[1])" -Value $matches[2] }
            }
        }
    }
}
if (Get-Command dumpbin.exe -ErrorAction SilentlyContinue) {
    $imps = & dumpbin.exe /nologo /imports $wordyDll
    $bindsShim = [bool]($imps | Select-String -SimpleMatch 'windows_win32_shim.dll')
    if ($Variant -eq 'isolated' -and -not $bindsShim) {
        Write-Result 'FAIL' "isolated wordy.dll does NOT import the shim (alias did not bind)."; exit 1
    }
    if ($Variant -eq 'native' -and $bindsShim) {
        Write-Result 'FAIL' "native wordy.dll imports the shim (expected an ordinary build)."; exit 1
    }
    Write-Host ">> wordy.dll binds shim = $bindsShim (expected for variant '$Variant')" -ForegroundColor Cyan
}

# --- 3. Stage the buffered host sidecar + a fresh live custom root -------------
# The sidecar is keyed to the *host process* executable (load_pilcfg reads
# current_exe.pilcfg). The same buffered sidecar is staged for BOTH variants so
# the only independent variable is whether wordy.dll was aliased.
$pilcfg = "$hostExe.pilcfg"
Set-Content -Path $pilcfg -Value '{"buffer_updates": true}' -Encoding ascii -NoNewline

$customRoot = Join-Path $outDir ("custom-{0}" -f ([guid]::NewGuid().ToString('N')))
if (Test-Path $customRoot) { Remove-Item -Recurse -Force $customRoot }

# --- 4. Genuinely activate + drive HTTP --------------------------------------
$prevHttp = $env:WORDY_HOST_HTTP
$prevRoot = $env:WORDY_CUSTOM_ROOT
$env:WORDY_HOST_HTTP   = '1'
$env:WORDY_CUSTOM_ROOT = $customRoot
$logFile = Join-Path $outDir "hwcproof-$Variant.log"
try {
    Write-Host ">> running genuine HWC: $hostExe (WORDY_HOST_HTTP=1, WORDY_CUSTOM_ROOT=$customRoot)" -ForegroundColor Cyan
    & $hostExe *>&1 | Tee-Object -FilePath $logFile
    $hostExit = $LASTEXITCODE
}
finally {
    if ($null -ne $prevHttp) { $env:WORDY_HOST_HTTP = $prevHttp } else { Remove-Item Env:WORDY_HOST_HTTP -ErrorAction SilentlyContinue }
    if ($null -ne $prevRoot) { $env:WORDY_CUSTOM_ROOT = $prevRoot } else { Remove-Item Env:WORDY_CUSTOM_ROOT -ErrorAction SilentlyContinue }
    Remove-Item $pilcfg -ErrorAction SilentlyContinue
}

$log = Get-Content $logFile -Raw
$activated = $log -match 'host activated \(HRESULT 0x00000000\)'
$cannotReserve = $log -match 'cannot reserve .* with HTTP\.sys'

# --- 5. Evaluate --------------------------------------------------------------
$rootOnDisk = Test-Path $customRoot
if (Test-Path $customRoot) { Remove-Item -Recurse -Force $customRoot -ErrorAction SilentlyContinue }

if (-not $activated) {
    if ($cannotReserve) {
        Write-Result 'SKIP' "could not reserve http://localhost:8080/ (run elevated, or: netsh http add urlacl url=http://localhost:8080/ user=DOMAIN\you)."
        exit 2
    }
    Write-Result 'FAIL' "WebCoreActivate did not report success (host exit $hostExit). See $logFile."
    exit 1
}

# Activation succeeded: the add/get/delete round-trip ran. The smoke driver
# asserts 200 + body markers itself, so a zero host exit means read-your-writes
# held end to end.
if ($hostExit -ne 0) {
    Write-Result 'FAIL' "HTTP smoke round-trip failed under genuine HWC (host exit $hostExit). See $logFile."
    exit 1
}

if ($Variant -eq 'isolated') {
    if ($rootOnDisk) {
        Write-Result 'FAIL' "ISOLATION VIOLATED: the live custom root was created on disk ($customRoot) despite the buffered sidecar."
        exit 1
    }
    Write-Result 'PASS' "isolated: the add/get/delete round-trip succeeded over real HTTP, yet the live custom root was never created on disk - the module's FS mutations stayed in the shim overlay."
    exit 0
}
else {
    if (-not $rootOnDisk) {
        Write-Result 'FAIL' "native control inconclusive: the live custom root was NOT created on disk, so the isolated result would not be a meaningful contrast."
        exit 1
    }
    Write-Result 'PASS' "native: the same round-trip created the live custom root on disk (the negative control), confirming the isolated run's absent root is a real overlay effect."
    exit 0
}
