@echo off
setlocal
rem ---------------------------------------------------------------------------
rem AirPlay.bat - start the AirPlay receiver with one double-click.
rem Wraps scripts\run-uxplay.ps1 (sets PATH/HOME/GST_REGISTRY, then runs
rem build\uxplay.exe). Any argument is passed through, e.g.:
rem     AirPlay.bat -Name "Salon-PC" -Port 7100 -Debug
rem     AirPlay.bat -VideoSink autovideosink
rem ---------------------------------------------------------------------------
cd /d "%~dp0"
title AirPlay alicisi

if not exist "build\uxplay.exe" (
    echo [HATA] build\uxplay.exe bulunamadi.
    echo         Once derle ^(MSYS2 UCRT64 gerekir: scripts\setup-msys2.ps1^):
    echo             cd /d "%CD%"
    echo             git submodule update --init
    echo             C:\msys64\usr\bin\env.exe MSYSTEM=UCRT64 CHERE_INVOKING=1 /usr/bin/bash -lc "bash scripts/build.sh"
    echo.
    pause
    exit /b 1
)

rem PowerShell 7 varsa onu kullan, yoksa Windows PowerShell 5.1'e dus.
where pwsh >nul 2>&1 && (set "PS=pwsh") || (set "PS=powershell")

echo Alici baslatiliyor... iPhone: Denetim Merkezi - Ekran Yansitma - AirPlay-PC
echo Kapatmak icin bu pencerede Ctrl+C.
echo.

%PS% -NoProfile -ExecutionPolicy Bypass -File "scripts\run-uxplay.ps1" %*
set "RC=%ERRORLEVEL%"

echo.
if not "%RC%"=="0" echo [!] uxplay cikis kodu: %RC%
echo Pencereyi kapatmak icin bir tusa basin.
pause >nul
exit /b %RC%
