<#
.SYNOPSIS
    MW15-3 FS link-proof (build + import verification): build the shim, emit its
    alias COFF object, then build the shim-unaware `wordy` cdylib with the alias
    object + the shim's import library injected, and prove via `dumpbin /imports`
    that `wordy.dll`'s filesystem (and loader) calls bind the Rust shim instead
    of `kernel32`.

.DESCRIPTION
    `wordy` carries no isolation awareness (SHIM-D19); the act of isolating it is
    performed entirely from the outside, at build time, by the generic
    `WORDY_EXTRA_LINK_*` env vars its build script honors. This script wires the
    alias object + import library through those vars and then verifies the
    redirection happened, mirroring `linkproof/run-linkproof.ps1` but for the
    realistic third-party application instead of a synthetic C++ TU:

      1. cargo build -p windows-win32-shim   -> windows_win32_shim.dll(+.dll.lib)
      2. gen-alias-obj                        -> windows_win32_shim_alias.obj
      3. cargo build -p wordy with WORDY_EXTRA_LINK_{SEARCH,OBJ,LIB} set
                                              -> wordy.dll (FS calls redirected)
      4. dumpbin /imports wordy.dll           -> assert the FS imports bind the
                                                 shim (m<Name>) and that NO aliased
                                                 FS name is still imported from
                                                 kernel32

    The exit code is the proof result (0 = redirected). With -ReportOnly the
    script prints the shim import list and the surviving kernel32 FS imports
    without asserting, to discover exactly which std FS entry points `wordy`
    references on this toolchain.

.PARAMETER Configuration
    Cargo profile to build/link against: 'debug' (default) or 'release'.

.PARAMETER SkipCargo
    Skip building the shim; use whatever is already in target/<cfg>/.

.PARAMETER ReportOnly
    Print the import findings without asserting (discovery mode).

.NOTES
    Requires the MSVC toolchain. If `dumpbin.exe` is not on PATH the script
    imports the x64 developer environment via vswhere/vcvars64.
#>
[CmdletBinding()]
param(
    [ValidateSet('debug', 'release')]
    [string]$Configuration = 'debug',
    [switch]$SkipCargo,
    [switch]$ReportOnly
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$crateDir  = Split-Path -Parent $scriptDir
$repoRoot  = (Resolve-Path (Join-Path $crateDir '..\..')).Path
$targetDir = Join-Path $repoRoot "target\$Configuration"
$outDir    = Join-Path $repoRoot ".scratch\hwcproof"

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
    Invoke-Checked 'cargo' $cargoArgs 'cargo build (shim cdylib + gen-alias-obj)'
}

$shimDll   = Join-Path $targetDir 'windows_win32_shim.dll'
$importLib = Join-Path $targetDir 'windows_win32_shim.dll.lib'
$genTool   = Join-Path $targetDir 'gen-alias-obj.exe'
foreach ($p in @($shimDll, $importLib, $genTool)) {
    if (-not (Test-Path $p)) { throw "expected build artifact not found: $p (run without -SkipCargo)" }
}

# --- 2. Emit the alias COFF object from the checked-in NDJSON manifest ---------
$aliasObj = Join-Path $outDir 'windows_win32_shim_alias.obj'
Invoke-Checked $genTool @('--out', $aliasObj) 'gen-alias-obj (emit alias COFF)'

# --- 3. Build wordy.dll with the alias object + import library injected --------
# These generic vars are the *only* isolation hook wordy honors (SHIM-D19): the
# crate is built ordinarily and the alias object rewrites its FS IAT slots. The
# alias object and the shim import library are both passed as raw linker inputs
# (WORDY_EXTRA_LINK_OBJ), exactly as `linkproof` feeds them to `link.exe`; the
# import library supplies every `m<Name>` export the alias object references.
$env:WORDY_EXTRA_LINK_SEARCH = $targetDir
$env:WORDY_EXTRA_LINK_OBJ    = ($aliasObj, $importLib) -join [System.IO.Path]::PathSeparator
Remove-Item Env:WORDY_EXTRA_LINK_LIB -ErrorAction SilentlyContinue
try {
    $wordyArgs = @('build', '-p', 'wordy')
    if ($Configuration -eq 'release') { $wordyArgs += '--release' }
    Invoke-Checked 'cargo' $wordyArgs 'cargo build (aliased wordy cdylib)'
}
finally {
    Remove-Item Env:WORDY_EXTRA_LINK_SEARCH, Env:WORDY_EXTRA_LINK_OBJ -ErrorAction SilentlyContinue
}

$wordyDll = Join-Path $targetDir 'wordy.dll'
if (-not (Test-Path $wordyDll)) { throw "expected build artifact not found: $wordyDll" }

# --- Locate the MSVC toolchain (import the x64 dev environment if needed) ------
if (-not (Get-Command dumpbin.exe -ErrorAction SilentlyContinue)) {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (-not (Test-Path $vswhere)) { throw "dumpbin.exe not on PATH and vswhere.exe not found; run from an x64 Native Tools prompt." }
    $vsRoot = & $vswhere -latest -prerelease -products * -property installationPath
    if (-not $vsRoot) { throw "dumpbin.exe not on PATH and no Visual Studio installation found by vswhere." }
    $vcvars = Join-Path $vsRoot 'VC\Auxiliary\Build\vcvars64.bat'
    if (-not (Test-Path $vcvars)) { throw "vcvars64.bat not found under $vsRoot." }
    Write-Host ">> importing MSVC x64 environment from $vcvars" -ForegroundColor Cyan
    cmd /c "`"$vcvars`" >nul && set" | ForEach-Object {
        if ($_ -match '^([^=]+)=(.*)$') { Set-Item -Path "Env:$($matches[1])" -Value $matches[2] }
    }
}

# --- 4. dumpbin /imports and parse the per-module import sections --------------
Write-Host ">> dumpbin /imports $wordyDll" -ForegroundColor Cyan
$dump = & dumpbin.exe /nologo /imports $wordyDll
if ($LASTEXITCODE -ne 0) { throw "dumpbin /imports failed (exit $LASTEXITCODE)" }

# Walk the textual import dump, tracking the current imported-DLL header and the
# function names listed under it. A module header is a non-indented line ending
# in `.dll`; function lines are deeply indented `   hint name` rows.
$importsByDll = @{}
$currentDll = $null
foreach ($line in $dump) {
    if ($line -match '^\s{4}(\S.*\.dll)\s*$') {
        $currentDll = $matches[1].Trim().ToLowerInvariant()
        if (-not $importsByDll.ContainsKey($currentDll)) { $importsByDll[$currentDll] = New-Object System.Collections.Generic.List[string] }
        continue
    }
    if ($null -ne $currentDll -and $line -match '^\s{10,}[0-9A-Fa-f]+\s+(\S+)\s*$') {
        $importsByDll[$currentDll].Add($matches[1])
    }
}

$shimKey = 'windows_win32_shim.dll'
$shimImports = @()
if ($importsByDll.ContainsKey($shimKey)) { $shimImports = @($importsByDll[$shimKey] | Sort-Object -Unique) }

# The aliased filesystem entry points (the set MW15 redirects). Their genuine
# names must NOT survive as kernel32 imports once redirected.
$aliasedFsWin32 = @(
    'CreateFileW', 'CreateDirectoryW', 'DeleteFileW', 'RemoveDirectoryW',
    'GetFileAttributesW', 'GetFileAttributesExW',
    'FindFirstFileW', 'FindFirstFileExW', 'FindNextFileW', 'FindClose'
)
$kernel32 = @()
if ($importsByDll.ContainsKey('kernel32.dll')) { $kernel32 = @($importsByDll['kernel32.dll']) }
$leakedFs = @($kernel32 | Where-Object { $aliasedFsWin32 -contains $_ } | Sort-Object -Unique)

Write-Host ""
Write-Host "Imports from $shimKey ($($shimImports.Count)):" -ForegroundColor Yellow
$shimImports | ForEach-Object { Write-Host "    $_" }
Write-Host ""
Write-Host "Aliased FS names still imported from kernel32.dll ($($leakedFs.Count)):" -ForegroundColor Yellow
$leakedFs | ForEach-Object { Write-Host "    $_" }
Write-Host ""

if ($ReportOnly) {
    Write-Host "report-only: no assertions evaluated." -ForegroundColor DarkGray
    exit 0
}

# --- Assertions ---------------------------------------------------------------
$failures = New-Object System.Collections.Generic.List[string]

if ($shimImports.Count -eq 0) {
    $failures.Add("wordy.dll imports nothing from $shimKey (the alias object did not bind).")
}
$nonShimPrefixed = @($shimImports | Where-Object { $_ -notmatch '^m[A-Z]' })
if ($nonShimPrefixed.Count -gt 0) {
    $failures.Add("non-m-prefixed names imported from the shim: $($nonShimPrefixed -join ', ')")
}
# Every aliased FS entry point wordy actually references must come from the shim,
# never from kernel32.
if ($leakedFs.Count -gt 0) {
    $failures.Add("aliased FS names still bound to kernel32 (redirection incomplete): $($leakedFs -join ', ')")
}
# wordy's custom store creates directories and enumerates them; require at least
# the directory-create + directory-enumeration markers to have been redirected.
$requiredShim = @('mCreateDirectoryW', 'mFindFirstFileExW', 'mFindNextFileW', 'mFindClose')
$missing = @($requiredShim | Where-Object { $shimImports -notcontains $_ })
if ($missing.Count -gt 0) {
    $failures.Add("expected shim FS imports missing from wordy.dll: $($missing -join ', ')")
}

if ($failures.Count -gt 0) {
    Write-Host "FS link-proof FAILED:" -ForegroundColor Red
    $failures | ForEach-Object { Write-Host "  - $_" -ForegroundColor Red }
    exit 1
}

Write-Host "FS link-proof PASSED: wordy.dll's filesystem calls bind the Rust shim." -ForegroundColor Green
exit 0
