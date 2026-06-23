<#
.SYNOPSIS
    MW5-6 link-proof: build the Rust shim, emit its alias COFF object, then build
    and run a genuine-Win32-call C++ EXE that links the alias object + the shim's
    import library, proving the calls are redirected into the Rust shim.

.DESCRIPTION
    This is the cross-toolchain verification that cannot run as a `cargo test`
    (it requires a C++ compiler and the client's linker). It exercises the full
    production link recipe end to end:

      1. cargo build -p windows-win32-shim   -> windows_win32_shim.dll(+.dll.lib)
      2. gen-alias-obj                        -> windows_win32_shim_alias.obj
      3. cl /c linkproof_main.cpp             -> linkproof_main.obj
      4. link linkproof_main.obj + alias.obj + windows_win32_shim.dll.lib -> exe
      5. write <exe>.pilcfg = {"buffer_updates": true}; copy the DLL next to it
      6. run the exe; its exit code is this script's exit code (0 = redirected)

    The C++ source (linkproof_main.cpp) includes no shim headers; it calls the
    genuine <windows.h> registry APIs. Defining the __imp_<Name> IAT slots in the
    alias object is what reroutes those calls into the shim's m<Name> exports.

.PARAMETER Configuration
    Cargo profile to build/link against: 'debug' (default) or 'release'.

.PARAMETER SkipCargo
    Skip the `cargo build` step and use whatever is already in target/<cfg>/.

.NOTES
    Requires the MSVC toolchain. If `cl.exe` is not already on PATH the script
    locates it via vswhere and imports the x64 developer environment.
#>
[CmdletBinding()]
param(
    [ValidateSet('debug', 'release')]
    [string]$Configuration = 'debug',
    [switch]$SkipCargo
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$crateDir  = Split-Path -Parent $scriptDir
$repoRoot  = (Resolve-Path (Join-Path $crateDir '..\..')).Path
$targetDir = Join-Path $repoRoot "target\$Configuration"
$outDir    = Join-Path $repoRoot ".scratch\linkproof"

New-Item -ItemType Directory -Force -Path $outDir | Out-Null

function Invoke-Checked {
    param([string]$Exe, [string[]]$Arguments, [string]$What)
    Write-Host ">> $What" -ForegroundColor Cyan
    & $Exe @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "$What failed (exit $LASTEXITCODE): $Exe $($Arguments -join ' ')"
    }
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
    if (-not (Test-Path $vswhere)) { throw "cl.exe not on PATH and vswhere.exe not found; run from an x64 Native Tools prompt." }
    $vsRoot = & $vswhere -latest -prerelease -products * -property installationPath
    if (-not $vsRoot) { throw "cl.exe not on PATH and no Visual Studio installation found by vswhere." }
    $vcvars = Join-Path $vsRoot 'VC\Auxiliary\Build\vcvars64.bat'
    if (-not (Test-Path $vcvars)) { throw "vcvars64.bat not found under $vsRoot." }
    Write-Host ">> importing MSVC x64 environment from $vcvars" -ForegroundColor Cyan
    cmd /c "`"$vcvars`" >nul && set" | ForEach-Object {
        if ($_ -match '^([^=]+)=(.*)$') { Set-Item -Path "Env:$($matches[1])" -Value $matches[2] }
    }
}

# --- 3. Compile the genuine-Win32-call link-proof TU ---------------------------
$cppSrc = Join-Path $scriptDir 'linkproof_main.cpp'
$cppObj = Join-Path $outDir 'linkproof_main.obj'
Invoke-Checked 'cl.exe' @(
    '/nologo', '/c', '/EHsc', '/std:c++17', '/W4',
    "/Fo$cppObj", $cppSrc
) 'cl (compile link-proof TU)'

# --- 4. Link: link-proof TU + alias object + the shim's import library ---------
$exe = Join-Path $outDir 'windows_win32_shim_linkproof.exe'
Invoke-Checked 'link.exe' @(
    '/nologo', "/OUT:$exe", $cppObj, $aliasObj, $importLib
) 'link (link-proof EXE)'

# --- 5. Buffered .pilcfg so writes stay in the in-memory overlay; stage the DLL -
Set-Content -Path "$exe.pilcfg" -Value '{"buffer_updates": true}' -Encoding ascii -NoNewline
Copy-Item -Path $dll -Destination $outDir -Force

# --- 6. Run; the EXE's exit code is the proof result ---------------------------
Write-Host ">> running $exe" -ForegroundColor Cyan
& $exe
$rc = $LASTEXITCODE
if ($rc -eq 0) {
    Write-Host "link-proof PASSED: genuine Win32 calls were redirected into the Rust shim." -ForegroundColor Green
} else {
    Write-Host "link-proof FAILED (exit $rc)." -ForegroundColor Red
}
exit $rc
