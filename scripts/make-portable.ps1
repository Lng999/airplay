<#
.SYNOPSIS
    Packs a self-contained AirPlay receiver that runs on a machine with no MSYS2.

.DESCRIPTION
    build\uxplay.exe is a MinGW binary: it links against ~15 DLLs from C:\msys64\ucrt64\bin and
    loads GStreamer plugins from C:\msys64\ucrt64\lib\gstreamer-1.0 at runtime. Copying the repo
    to another PC therefore copies neither the DLLs nor the plugins, and the GUI reports
    "uxplay.exe bulunamadi" (the exe is not next to it and the fallback build\ path is gone) or,
    worse, finds it and cannot start it.

    This script produces a folder that carries the whole runtime with it:

        <out>\airplay-gui.exe                        the GUI (found side by side ...)
        <out>\uxplay.exe                             ... with the receiver, so no path config
        <out>\ucrt64\bin\*.dll                       the import closure of uxplay + all plugins
        <out>\ucrt64\lib\gstreamer-1.0\*.dll         every GStreamer plugin
        <out>\ucrt64\libexec\gstreamer-1.0\...       gst-plugin-scanner.exe
        <out>\Baslat.bat                             convenience launcher
        <out>\OKU-BENI.txt                           what to do on the target machine

    The GUI detects that layout on its own: config_store.cpp defaultMsysRoot() uses
    <exe dir> as the runtime root whenever <exe dir>\ucrt64\bin exists.

    The DLL list is not hardcoded - it is the transitive import closure computed with objdump,
    seeded from uxplay.exe, gst-plugin-scanner.exe and every plugin DLL. Only DLLs that live in
    ucrt64\bin are copied; Windows' own (KERNEL32, d3d11, ...) are left to the target system.

.PARAMETER MsysRoot
    MSYS2 installation to take the runtime from. Default C:\msys64.

.PARAMETER OutDir
    Destination folder. Default <repo>\dist\airplay-portable. Wiped if it exists.

.PARAMETER Archive
    Also produce <OutDir>.zip next to the folder.

.EXAMPLE
    pwsh -File scripts\make-portable.ps1
    pwsh -File scripts\make-portable.ps1 -Archive
#>
[CmdletBinding()]
param(
    [string]$MsysRoot = 'C:\msys64',
    [string]$OutDir,
    [switch]$Archive
)

$ErrorActionPreference = 'Stop'

$repo = Split-Path -Parent $PSScriptRoot
if (-not $OutDir) { $OutDir = Join-Path $repo 'dist\airplay-portable' }

$guiExe    = Join-Path $repo 'build-app\airplay-gui.exe'
$uxplayExe = Join-Path $repo 'build\uxplay.exe'
$srcBin    = Join-Path $MsysRoot 'ucrt64\bin'
$srcPlug   = Join-Path $MsysRoot 'ucrt64\lib\gstreamer-1.0'
$srcScan   = Join-Path $MsysRoot 'ucrt64\libexec\gstreamer-1.0\gst-plugin-scanner.exe'
$objdump   = Join-Path $srcBin 'objdump.exe'

foreach ($p in @($guiExe, $uxplayExe, $objdump)) {
    if (-not (Test-Path -LiteralPath $p)) {
        throw "not found: $p  (build first: scripts/build.sh and scripts/build-app.sh)"
    }
}
if (-not (Test-Path -LiteralPath $srcPlug)) { throw "GStreamer plugin directory not found: $srcPlug" }

# --- 1. import closure ---------------------------------------------------------------------------

Write-Host '[1/4] reading imports (objdump over the whole runtime, ~1 min)...' -ForegroundColor Cyan

$binDlls    = Get-ChildItem -LiteralPath $srcBin  -Filter *.dll -File
$pluginDlls = Get-ChildItem -LiteralPath $srcPlug -Filter *.dll -File
$binNames   = [System.Collections.Generic.HashSet[string]]::new(
                  [string[]]($binDlls | ForEach-Object { $_.Name.ToLowerInvariant() }),
                  [System.StringComparer]::OrdinalIgnoreCase)

# file (lowercased base name) -> list of imported DLL names
$imports = @{}

$toScan = @()
$toScan += $binDlls.FullName
$toScan += $pluginDlls.FullName
$toScan += $uxplayExe
if (Test-Path -LiteralPath $srcScan) { $toScan += $srcScan }

# One objdump call per chunk: a single call with ~600 paths would blow the command line limit.
$chunk = 40
for ($i = 0; $i -lt $toScan.Count; $i += $chunk) {
    $batch = $toScan[$i..([Math]::Min($i + $chunk - 1, $toScan.Count - 1))]
    $current = $null
    & $objdump -p @batch 2>$null | ForEach-Object {
        if ($_ -match '^(.+):\s+file format ') {
            $current = [System.IO.Path]::GetFileName($Matches[1]).ToLowerInvariant()
            if (-not $imports.ContainsKey($current)) { $imports[$current] = New-Object System.Collections.ArrayList }
        } elseif ($current -and $_ -match '^\s*DLL Name:\s*(\S+)') {
            [void]$imports[$current].Add($Matches[1].ToLowerInvariant())
        }
    }
    Write-Host ("      {0}/{1}" -f [Math]::Min($i + $chunk, $toScan.Count), $toScan.Count)
}

$needed = [System.Collections.Generic.HashSet[string]]::new([System.StringComparer]::OrdinalIgnoreCase)
$queue  = New-Object System.Collections.Queue
foreach ($r in @('uxplay.exe', 'gst-plugin-scanner.exe') + ($pluginDlls | ForEach-Object { $_.Name.ToLowerInvariant() })) {
    $queue.Enqueue($r)
}
while ($queue.Count -gt 0) {
    $cur = $queue.Dequeue()
    if (-not $imports.ContainsKey($cur)) { continue }
    foreach ($dep in $imports[$cur]) {
        if ($binNames.Contains($dep) -and $needed.Add($dep)) { $queue.Enqueue($dep) }
    }
}
Write-Host ("      {0} runtime DLLs required" -f $needed.Count) -ForegroundColor Green

# --- 2. lay the tree out -------------------------------------------------------------------------

Write-Host '[2/4] copying...' -ForegroundColor Cyan

if (Test-Path -LiteralPath $OutDir) { Remove-Item -LiteralPath $OutDir -Recurse -Force }
$dstBin  = Join-Path $OutDir 'ucrt64\bin'
$dstPlug = Join-Path $OutDir 'ucrt64\lib\gstreamer-1.0'
$dstExec = Join-Path $OutDir 'ucrt64\libexec\gstreamer-1.0'
foreach ($d in @($OutDir, $dstBin, $dstPlug, $dstExec)) { New-Item -ItemType Directory -Force -Path $d | Out-Null }

Copy-Item -LiteralPath $guiExe    -Destination (Join-Path $OutDir 'airplay-gui.exe')
Copy-Item -LiteralPath $uxplayExe -Destination (Join-Path $OutDir 'uxplay.exe')
foreach ($d in $needed) { Copy-Item -LiteralPath (Join-Path $srcBin $d) -Destination $dstBin }
foreach ($p in $pluginDlls) { Copy-Item -LiteralPath $p.FullName -Destination $dstPlug }
if (Test-Path -LiteralPath $srcScan) { Copy-Item -LiteralPath $srcScan -Destination $dstExec }

# --- 3. launcher + readme ------------------------------------------------------------------------

Write-Host '[3/4] writing Baslat.bat and OKU-BENI.txt...' -ForegroundColor Cyan

$bat = @'
@echo off
cd /d "%~dp0"
start "" "airplay-gui.exe"
'@
[System.IO.File]::WriteAllText((Join-Path $OutDir 'Baslat.bat'), $bat, [System.Text.Encoding]::ASCII)

$readme = @'
AirPlay alicisi - tasinabilir surum
===================================

Kurulum gerekmez. Bu klasoru oldugu gibi kopyalayin ve Baslat.bat dosyasina
(ya da dogrudan airplay-gui.exe uzerine) cift tiklayin.

ONEMLI: klasorun tamamini kopyalayin. ucrt64\ alt klasoru uygulamanin calisma
zamanidir (GStreamer + MinGW DLL'leri); sadece exe'leri kopyalarsaniz uygulama
"uxplay.exe bulunamadi" hatasi verir.

Ilk calistirmada:
  1. Windows Guvenlik Duvari izin soracak - "Ozel aglar" kutusunu isaretleyip
     "Erisime izin ver" deyin. AirPlay yerel agda calisir, bu izin zorunludur.
  2. Bilgisayarin ag profili "Ozel/Private" olmali (Genel/Public agda mDNS bulunamaz).
  3. iPhone ile bilgisayar ayni Wi-Fi agi uzerinde olmali.

Kullanim: iPhone > Denetim Merkezi > Ekran Yansitma > AirPlay-PC

Ayarlar %APPDATA%\airplay\config.ini dosyasinda tutulur; uygulamayi tasidiginizda
icindeki eski yollar yok sayilir, calisma zamani otomatik bulunur.

Lisans: UxPlay (GPLv3) tabanlidir, bkz. LICENSE.
'@
[System.IO.File]::WriteAllText((Join-Path $OutDir 'OKU-BENI.txt'), $readme, [System.Text.Encoding]::UTF8)

$lic = Join-Path $repo 'LICENSE'
if (Test-Path -LiteralPath $lic) { Copy-Item -LiteralPath $lic -Destination $OutDir }

# --- 4. report -----------------------------------------------------------------------------------

$size = (Get-ChildItem -LiteralPath $OutDir -Recurse -File | Measure-Object -Property Length -Sum).Sum
$files = (Get-ChildItem -LiteralPath $OutDir -Recurse -File).Count
Write-Host ("[4/4] {0}" -f $OutDir) -ForegroundColor Green
Write-Host ("      {0} files, {1:N0} MB" -f $files, ($size / 1MB))

if ($Archive) {
    $zip = "$OutDir.zip"
    Write-Host "      compressing to $zip ..." -ForegroundColor Cyan
    if (Test-Path -LiteralPath $zip) { Remove-Item -LiteralPath $zip -Force }
    Compress-Archive -Path (Join-Path $OutDir '*') -DestinationPath $zip -CompressionLevel Optimal
    $zs = (Get-Item -LiteralPath $zip).Length
    Write-Host ("      {0} ({1:N0} MB)" -f $zip, ($zs / 1MB)) -ForegroundColor Green
}
