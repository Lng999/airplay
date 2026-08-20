<#
    run-uxplay.ps1 - launcher for the UxPlay build produced by scripts/build.sh.

    It exists because uxplay.exe needs a prepared environment that the MSYS2 shell
    normally provides: the UCRT64 bin directory on PATH, a HOME to persist state in,
    and a private GStreamer registry.

    NOTE: deliberately NOT an advanced function ([CmdletBinding()] is absent) so that
    a plain -Debug switch can be declared; -Debug is a reserved common parameter for
    advanced functions.

    Usage:
        pwsh -File scripts/run-uxplay.ps1
        pwsh -File scripts/run-uxplay.ps1 -Name "Salon PC" -Port 0 -Debug
        pwsh -File scripts/run-uxplay.ps1 -Help
#>

param(
    # AirPlay service name shown on the iPhone (uxplay -n, uxplay.cpp:915/1233).
    [string] $Name = 'AirPlay-PC',

    # uxplay -p semantics, verified in source:
    #   bare "-p"  -> legacy set TCP 7100,7000,7001 / UDP 7011,6001,6000  (uxplay.cpp:1336-1337)
    #   "-p N"     -> TCP N,N+1,N+2 AND UDP N,N+1,N+2                     (uxplay.cpp:1347-1352
    #                 assigns udp[j]=tcp[j]; get_ports() fills the missing two consecutively
    #                 upward, uxplay.cpp:1085-1090)
    # So -Port 7100 opens TCP+UDP 7100-7102, NOT 7100/7099/7098.
    # -Port 0 emits a bare "-p" and uses the legacy set instead.
    # Without any -p the ports are random, which no firewall rule can cover
    # (UxPlay README.md:1388-1397).
    [int] $Port = 7100,

    # GStreamer's own docs call d3d11videosink "the recommended element on Windows";
    # it also honours GstVideoOverlay render-rectangle, which d3d12videosink may ignore.
    # (SPEC.md 2c / research doc 3.3)
    [string] $VideoSink = 'd3d11videosink',

    # wasapi2sink is the documented default audio sink for Windows 8+ (research doc 3.3).
    [string] $AudioSink = 'wasapi2sink',

    [switch] $Debug,
    [switch] $Fullscreen,

    # Defaults to <repo>\build\uxplay.exe, then <Root>\ucrt64\bin\uxplay.exe.
    [string] $ExePath = '',

    [string] $Root = 'C:\msys64',

    [switch] $Help
)

$ErrorActionPreference = 'Stop'

if ($Help) {
    Write-Host @'
run-uxplay.ps1 - launch the built uxplay.exe with a prepared environment.

Parameters:
  -Name <string>       AirPlay name advertised over mDNS   (default AirPlay-PC)
  -Port <int>          uxplay -p N  => TCP+UDP N,N+1,N+2   (default 7100; 0 = legacy set)
  -VideoSink <string>  uxplay -vs                          (default d3d11videosink)
  -AudioSink <string>  uxplay -as                          (default wasapi2sink)
  -Debug               uxplay -d   (verbose protocol log)
  -Fullscreen          uxplay -fs
  -ExePath <path>      override the uxplay.exe location
  -Root <path>         MSYS2 root                          (default C:\msys64)
  -Help                this text
'@
    exit 0
}

$Root = $Root.TrimEnd('\')
$repoRoot = Split-Path -Parent $PSScriptRoot

# --- locate the executable --------------------------------------------------
if (-not $ExePath) {
    $candidates = @(
        (Join-Path $repoRoot 'build\uxplay.exe'),
        (Join-Path $Root 'ucrt64\bin\uxplay.exe')
    )
    foreach ($c in $candidates) {
        if (Test-Path -LiteralPath $c) { $ExePath = $c; break }
    }
}
if (-not $ExePath -or -not (Test-Path -LiteralPath $ExePath)) {
    throw "uxplay.exe not found. Build it first (scripts/build.sh) or pass -ExePath."
}
$ExePath = (Resolve-Path -LiteralPath $ExePath).Path

# --- environment ------------------------------------------------------------
# All runtime DLLs (glib, gstreamer, openssl, libplist, libstdc++, ...) sit flat in
# <Root>\ucrt64\bin. Without it on PATH the process dies with
# "libglib-2.0-0.dll was not found" (UxPlay README.md:1157-1162, PR #543).
$ucrtBin = Join-Path $Root 'ucrt64\bin'
if (-not (Test-Path -LiteralPath $ucrtBin)) {
    Write-Warning "MSYS2 UCRT64 bin not found at $ucrtBin - the exe may fail to start."
}
else {
    $env:PATH = "$ucrtBin;$env:PATH"
}

# uxplay's get_homedir() reads XDG_CONFIG_HOMEDIR then HOME and, on Windows, has no
# getpwuid() fallback (uxplay.cpp:768-779). If both are unset it cannot save
# $HOME/.uxplay.pem (uxplay.cpp:3153-3162) and pairing state is lost every launch.
$configHome = Join-Path $env:APPDATA 'airplay'
if (-not (Test-Path -LiteralPath $configHome)) {
    New-Item -ItemType Directory -Path $configHome -Force | Out-Null
}
$env:HOME = $configHome
$env:XDG_CONFIG_HOMEDIR = $configHome

# GStreamer's default registry lives in a shared %LOCALAPPDATA% cache; a stale entry
# left by another GStreamer install (OBS, the MSVC runtime, an older prefix) makes
# elements disappear. Keep our own file. (research doc 4.2)
$cacheHome = Join-Path $env:LOCALAPPDATA 'airplay'
if (-not (Test-Path -LiteralPath $cacheHome)) {
    New-Item -ItemType Directory -Path $cacheHome -Force | Out-Null
}
$env:GST_REGISTRY = Join-Path $cacheHome 'gst-registry.bin'
$env:GST_PLUGIN_SYSTEM_PATH = Join-Path $Root 'ucrt64\lib\gstreamer-1.0'

# --- pre-flight warnings ----------------------------------------------------
# Apple's Bonjour Service competes for UDP 5353 with UxPlay's bundled responder
# (UxPlay issue #297; research doc 7.4). Not installed on this machine, checked anyway.
$bonjour = Get-Service -Name 'Bonjour Service' -ErrorAction SilentlyContinue
if ($bonjour -and $bonjour.Status -eq 'Running') {
    Write-Warning "Apple 'Bonjour Service' is running and may hold UDP 5353. If the iPhone does not see this PC, stop it and retry."
}

$profileInfo = Get-NetConnectionProfile -ErrorAction SilentlyContinue
if ($profileInfo -and -not ($profileInfo | Where-Object { $_.NetworkCategory -eq 'Private' })) {
    Write-Warning "No network adapter is on the Private profile. Windows blocks discovery traffic more aggressively on Public (UxPlay README.md:1112-1122)."
}

# --- argument list ----------------------------------------------------------
$uxArgs = New-Object System.Collections.Generic.List[string]
$uxArgs.Add('-n');  $uxArgs.Add($Name)
$uxArgs.Add('-p')
if ($Port -gt 0) { $uxArgs.Add([string]$Port) }   # bare -p = legacy port set
$uxArgs.Add('-vs'); $uxArgs.Add($VideoSink)
$uxArgs.Add('-as'); $uxArgs.Add($AudioSink)
if ($Debug)      { $uxArgs.Add('-d') }            # uxplay.cpp:1368-1381
if ($Fullscreen) { $uxArgs.Add('-fs') }           # uxplay.cpp:1448

Write-Host "exe          : $ExePath"
Write-Host "args         : $($uxArgs -join ' ')"
Write-Host "HOME         : $env:HOME"
Write-Host "GST_REGISTRY : $env:GST_REGISTRY"
Write-Host ''

$proc = Start-Process -FilePath $ExePath -ArgumentList $uxArgs.ToArray() `
    -NoNewWindow -Wait -PassThru
exit $proc.ExitCode
