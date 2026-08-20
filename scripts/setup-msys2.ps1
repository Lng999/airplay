<#
.SYNOPSIS
    Installs MSYS2 (UCRT64) and every package UxPlay needs, idempotently.

.DESCRIPTION
    Phase 0 step 1 of docs/SPEC.md. Safe to re-run: every step is skipped when it is
    already satisfied, and pacman is called with --needed.

    Elevation: NOT required when -Root points at a user-writable location such as
    C:\msys64 (the default). Only an install into C:\Program Files would need an
    elevated shell - avoid that, MSYS2 dislikes paths with spaces anyway.

.PARAMETER Root
    MSYS2 install root. Default C:\msys64 (matches the winget manifest default,
    docs/research/gstreamer-msys2-windows.md section 1.2).

.PARAMETER SkipInstall
    Do not install MSYS2; only run the pacman steps against an existing -Root.

.PARAMETER DryRun
    Print every command that would run, change nothing.

.EXAMPLE
    pwsh -File scripts/setup-msys2.ps1 -DryRun
.EXAMPLE
    pwsh -File scripts/setup-msys2.ps1
#>

param(
    [string] $Root = 'C:\msys64',
    [switch] $SkipInstall,
    [switch] $DryRun
)

$ErrorActionPreference = 'Stop'
# Invoke-WebRequest is far slower on PS 5.1 with the progress bar enabled.
$ProgressPreference = 'SilentlyContinue'

# Official installer, always the newest build (docs/research/gstreamer-msys2-windows.md 1.1).
$InstallerUrl = 'https://github.com/msys2/msys2-installer/releases/latest/download/msys2-x86_64-latest.exe'

# UCRT64 package set, expanded: pacman needs explicit names, brace expansion is a shell
# feature and would not survive being handed to pacman as argv entries.
# Source: docs/research/gstreamer-msys2-windows.md section 2 + SPEC.md 2c.
$Packages = @(
    'mingw-w64-ucrt-x86_64-gcc',
    'mingw-w64-ucrt-x86_64-cmake',
    'mingw-w64-ucrt-x86_64-ninja',
    'mingw-w64-ucrt-x86_64-pkgconf',
    'mingw-w64-ucrt-x86_64-openssl',
    'mingw-w64-ucrt-x86_64-libplist',
    'mingw-w64-ucrt-x86_64-gstreamer',
    'mingw-w64-ucrt-x86_64-gst-plugins-base',
    'mingw-w64-ucrt-x86_64-gst-plugins-good',
    'mingw-w64-ucrt-x86_64-gst-plugins-bad',   # h264parse, d3d11videosink, wasapi2sink
    'mingw-w64-ucrt-x86_64-gst-libav',         # avdec_aac / avdec_alac live nowhere else
    'mingw-w64-ucrt-x86_64-ntldd',             # DLL closure for the self-contained bundle
    'git'                                      # MSYS2-native git, no prefix (UxPlay README:1044)
)

$Root = $Root.TrimEnd('\')
$BashExe = Join-Path $Root 'usr\bin\bash.exe'

function Write-Step {
    param([string] $Message)
    Write-Host ''
    Write-Host "==> $Message" -ForegroundColor Cyan
}

function Write-Note {
    param([string] $Message)
    Write-Host "    $Message" -ForegroundColor DarkGray
}

# Runs a command line inside the MSYS2 login shell with the UCRT64 environment selected.
# MSYSTEM must be set BEFORE the login shell starts so that /ucrt64/bin lands on PATH;
# CHERE_INVOKING keeps the current directory instead of jumping to $HOME.
# (docs/research/gstreamer-msys2-windows.md section 2, "Full install one-liner")
function Invoke-Msys2 {
    param(
        [Parameter(Mandatory = $true)][string] $CommandLine,
        [switch] $IgnoreExitCode
    )

    if ($DryRun) {
        Write-Host "DRYRUN  MSYSTEM=UCRT64 CHERE_INVOKING=1 `"$BashExe`" -lc `"$CommandLine`""
        return 0
    }

    $oldMsystem = $env:MSYSTEM
    $oldChere = $env:CHERE_INVOKING
    $code = 0
    try {
        $env:MSYSTEM = 'UCRT64'
        $env:CHERE_INVOKING = '1'
        & $BashExe -lc $CommandLine
        $code = $LASTEXITCODE
    }
    finally {
        $env:MSYSTEM = $oldMsystem
        $env:CHERE_INVOKING = $oldChere
    }

    if ($code -ne 0 -and -not $IgnoreExitCode) {
        throw "MSYS2 command failed (exit $code): $CommandLine"
    }
    return $code
}

function Test-Msys2Installed {
    return (Test-Path -LiteralPath $BashExe)
}

# ---------------------------------------------------------------------------
# (a) Install MSYS2
# ---------------------------------------------------------------------------
Write-Step "MSYS2 root: $Root"

if (Test-Msys2Installed) {
    Write-Note "already installed (found $BashExe) - skipping install"
}
elseif ($SkipInstall) {
    throw "MSYS2 not found at $Root and -SkipInstall was given. Nothing to do."
}
else {
    $installer = Join-Path $env:TEMP 'msys2-x86_64-latest.exe'
    # Qt Installer Framework 'in' verb; --root uses FORWARD slashes in the documented
    # example (https://www.msys2.org/docs/installer/, research doc 1.1).
    $rootFwd = $Root -replace '\\', '/'
    $installArgs = @('in', '--confirm-command', '--accept-messages', '--root', $rootFwd)
    $downloaded = $false

    Write-Step 'Downloading the MSYS2 installer'
    if ($DryRun) {
        Write-Host "DRYRUN  Invoke-WebRequest -Uri $InstallerUrl -OutFile $installer"
        Write-Host "DRYRUN  & `"$installer`" $($installArgs -join ' ')"
        $downloaded = $true
    }
    else {
        try {
            Invoke-WebRequest -Uri $InstallerUrl -OutFile $installer -UseBasicParsing
            $downloaded = $true
        }
        catch {
            Write-Warning "Download failed: $($_.Exception.Message)"
        }
    }

    if ($downloaded -and -not $DryRun) {
        Write-Step 'Running the installer (silent)'
        & $installer @installArgs
        if ($LASTEXITCODE -ne 0) {
            throw "MSYS2 installer exited with $LASTEXITCODE"
        }
    }
    elseif (-not $downloaded) {
        # Fallback: winget. The manifest installs to C:\msys64 by default and sets
        # UpgradeBehavior: deny, so updates still go through pacman -Syu (research doc 1.2).
        Write-Step 'Falling back to winget'
        $wingetArgs = @('install', '--id', 'MSYS2.MSYS2', '-e',
                        '--accept-package-agreements', '--accept-source-agreements')
        if ($DryRun) {
            Write-Host "DRYRUN  winget $($wingetArgs -join ' ')"
        }
        else {
            & winget @wingetArgs
            if ($LASTEXITCODE -ne 0) {
                throw "winget install MSYS2.MSYS2 exited with $LASTEXITCODE"
            }
        }
    }

    if (-not $DryRun -and -not (Test-Msys2Installed)) {
        throw "Install finished but $BashExe is still missing. Check the installer output."
    }
}

# ---------------------------------------------------------------------------
# (b) First-run core update. Run -Syuu TWICE: the first pass may update the MSYS2
#     runtime itself and terminate the shell ("close the terminal and run again"),
#     which surfaces as a non-zero exit. https://www.msys2.org/docs/updating/
# ---------------------------------------------------------------------------
Write-Step 'Core update pass 1/2 (pacman -Syuu)'
$null = Invoke-Msys2 -CommandLine 'pacman -Syuu --noconfirm' -IgnoreExitCode

Write-Step 'Core update pass 2/2 (pacman -Syuu)'
$null = Invoke-Msys2 -CommandLine 'pacman -Syuu --noconfirm'

# ---------------------------------------------------------------------------
# (c) Toolchain + GStreamer
# ---------------------------------------------------------------------------
Write-Step "Installing $($Packages.Count) packages"
$pkgLine = 'pacman -S --needed --noconfirm ' + ($Packages -join ' ')
$null = Invoke-Msys2 -CommandLine $pkgLine

# ---------------------------------------------------------------------------
# (d) Verify
# ---------------------------------------------------------------------------
Write-Step 'Verifying the toolchain'
$verify = @(
    'echo "--- gcc ---"; gcc --version | head -1',
    'echo "--- cmake ---"; cmake --version | head -1',
    'echo "--- ninja ---"; echo "ninja $(ninja --version)"',
    'echo "--- pkgconf ---"; echo "pkg-config $(pkg-config --version)"',
    'echo "--- gstreamer ---"; gst-inspect-1.0 --version | head -2',
    'echo "--- plugin count ---"; gst-inspect-1.0 2>/dev/null | tail -1'
) -join '; '
$null = Invoke-Msys2 -CommandLine $verify -IgnoreExitCode

Write-Step 'Done'
Write-Note "Next: $BashExe -lc 'cd /path/to/repo && ./scripts/build.sh'"
Write-Note 'See scripts/README.md for the full order of operations.'
