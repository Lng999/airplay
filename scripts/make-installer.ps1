<#
.SYNOPSIS
    Builds the AirPlay setup .exe out of the portable folder.

.DESCRIPTION
    Two steps, in this order:

      1. scripts/make-portable.ps1  -> dist\airplay-portable\  (both exes + the ucrt64 runtime)
      2. Inno Setup over installer\airplay.iss -> dist\AirPlay-Setup-<version>.exe

    The version is not typed here: it is read back out of the one place it is written down,
    project(airplay_gui VERSION x.y.z) in app/CMakeLists.txt, which is the same value the GUI
    was compiled with (src/ui/version.h.in) and the same value the update check compares
    against a GitHub release tag. Bump that line, rebuild, run this - nothing else to keep in
    step.

.PARAMETER SkipPortable
    Reuse the existing dist\airplay-portable\ instead of rebuilding it. Only correct when the
    exes in it are already current.

.PARAMETER Iscc
    Path to ISCC.exe. Found automatically in the usual per-user and per-machine locations.

.EXAMPLE
    pwsh -File scripts\make-installer.ps1
#>
[CmdletBinding()]
param(
    [switch]$SkipPortable,
    [string]$Iscc
)

$ErrorActionPreference = 'Stop'

$repo    = Split-Path -Parent $PSScriptRoot
$distDir = Join-Path $repo 'dist'
$portable = Join-Path $distDir 'airplay-portable'

# --- version --------------------------------------------------------------------------------

$cmakeLists = Join-Path $repo 'app\CMakeLists.txt'
$m = [regex]::Match((Get-Content -LiteralPath $cmakeLists -Raw),
                    'project\s*\(\s*airplay_gui\s+VERSION\s+([0-9]+\.[0-9]+\.[0-9]+)')
if (-not $m.Success) { throw "could not read VERSION out of $cmakeLists" }
$version = $m.Groups[1].Value
Write-Host "[version] $version" -ForegroundColor Cyan

# The built exe must agree, or the update check would compare against the wrong number.
$guiExe = Join-Path $repo 'build-app\airplay-gui.exe'
if (-not (Test-Path -LiteralPath $guiExe)) { throw "not built: $guiExe (scripts/build-app.sh)" }
$vi = (Get-Item -LiteralPath $guiExe).VersionInfo
$built = '{0}.{1}.{2}' -f $vi.FileMajorPart, $vi.FileMinorPart, $vi.FileBuildPart
if ($built -ne $version) {
    throw "airplay-gui.exe is $built but app/CMakeLists.txt says $version - rebuild first"
}

# --- ISCC -----------------------------------------------------------------------------------

if (-not $Iscc) {
    $candidates = @(
        (Join-Path $env:LOCALAPPDATA 'Programs\Inno Setup 6\ISCC.exe'),
        'C:\Program Files (x86)\Inno Setup 6\ISCC.exe',
        'C:\Program Files\Inno Setup 6\ISCC.exe'
    )
    $Iscc = $candidates | Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
}
if (-not $Iscc) {
    throw "ISCC.exe not found. Install it: winget install --id JRSoftware.InnoSetup"
}
Write-Host "[iscc] $Iscc" -ForegroundColor Cyan

# --- portable payload -----------------------------------------------------------------------

if ($SkipPortable) {
    if (-not (Test-Path -LiteralPath $portable)) { throw "-SkipPortable but $portable does not exist" }
    Write-Host '[portable] reusing the existing folder' -ForegroundColor Yellow
} else {
    & (Join-Path $PSScriptRoot 'make-portable.ps1')
    if ($LASTEXITCODE) { throw "make-portable.ps1 failed ($LASTEXITCODE)" }
}

# --- compile --------------------------------------------------------------------------------

Write-Host '[iscc] compiling (a few minutes: ~232 MB at lzma2/max)...' -ForegroundColor Cyan
$iss = Join-Path $repo 'installer\airplay.iss'
& $Iscc "/DMyAppVersion=$version" "/DSourceDir=$portable" "/DOutputDir=$distDir" $iss |
    Select-Object -Last 6
if ($LASTEXITCODE) { throw "ISCC failed ($LASTEXITCODE)" }

$setup = Join-Path $distDir "AirPlay-Setup-$version.exe"
if (-not (Test-Path -LiteralPath $setup)) { throw "ISCC reported success but $setup is missing" }
Write-Host ("[done] {0}  ({1:N1} MB)" -f $setup, ((Get-Item -LiteralPath $setup).Length / 1MB)) -ForegroundColor Green
