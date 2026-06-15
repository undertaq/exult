@echo off
setlocal enabledelayedexpansion

REM  download_font.bat -- Download CJK TrueType font for Exult TC support
REM
REM  Downloads Noto Sans TC (Traditional Chinese) from Google Fonts and
REM  places it at data/fonts/notosanscjk.ttf so that FreeType can render
REM  Traditional Chinese characters in Exult.
REM
REM  Usage: double-click or run from command prompt:
REM    download_font.bat

set "FONT_DIR=%~dp0data\fonts"
set "FONT_PATH=%FONT_DIR%\notosanscjk.ttf"
set "TEMP_PATH=%TEMP%\notosanscjk.ttf"

REM --- Check if font already exists and is valid (>50KB = full font) ------
if exist "%FONT_PATH%" (
    for %%F in ("%FONT_PATH%") do set "SIZE=%%~zF"
    if !SIZE! GTR 50000 (
        echo [OK] Font already exists at "%FONT_PATH%" (!SIZE! bytes)
        goto :done
    )
    echo [WARN] Existing font is too small (!SIZE! bytes), re-downloading...
    del "%FONT_PATH%"
)

REM --- Ensure target directory exists -------------------------------------
if not exist "%FONT_DIR%" mkdir "%FONT_DIR%"

REM --- Download URL (primary source) --------------------------------------
REM Google Fonts GitHub - Noto Sans TC Variable (replaces old static Regular)
set "URL=https://github.com/google/fonts/raw/main/ofl/notosanstc/NotoSansTC%5Bwght%5D.ttf"

set "DOWNLOADED=0"

echo.
echo [..] Downloading Noto Sans TC font for Exult TC support...
echo [..] Source: %URL%
echo.

REM --- Attempt 1: PowerShell (built-in to all modern Windows) -------------
echo [..] Attempt 1: PowerShell Invoke-WebRequest ...
powershell -NoProfile -Command "try { [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12; Invoke-WebRequest -Uri '%URL%' -OutFile '%TEMP_PATH%' -UserAgent 'Mozilla/5.0 (Windows NT 10.0; Win64; x64)' -ErrorAction Stop; Write-Host 'OK' } catch { Write-Host 'FAILED'; exit 1 }" 2>nul
if %ERRORLEVEL% EQU 0 (
    set "DOWNLOADED=1"
    goto :check
)

REM --- Attempt 2: BITSAdmin (built-in to all modern Windows) --------------
echo [..] Attempt 2: BITSAdmin ...
bitsadmin /transfer "ExultFontDownload" /download /priority high "%URL%" "%TEMP_PATH%" >nul 2>&1
if %ERRORLEVEL% EQU 0 (
    for %%F in ("%TEMP_PATH%") do if %%~zF GTR 50000 set "DOWNLOADED=1"
    if "!DOWNLOADED!"=="1" goto :check
)

REM --- Attempt 3: curl (built-in to Windows 10/11) -----------------------
echo [..] Attempt 3: curl ...
curl -L -s -o "%TEMP_PATH%" "%URL%" 2>nul
if %ERRORLEVEL% EQU 0 (
    for %%F in ("%TEMP_PATH%") do if %%~zF GTR 50000 set "DOWNLOADED=1"
    if "!DOWNLOADED!"=="1" goto :check
)

REM --- Attempt 4: CDN fallback (jsDelivr mirror) -------------------------
set "URL2=https://cdn.jsdelivr.net/gh/google/fonts@main/ofl/notosanstc/NotoSansTC%5Bwght%5D.ttf"
echo [..] Attempt 4: jsDelivr CDN mirror ...
powershell -NoProfile -Command "try { Invoke-WebRequest -Uri '%URL2%' -OutFile '%TEMP_PATH%' -UserAgent 'Mozilla/5.0 (Windows NT 10.0; Win64; x64)' -ErrorAction Stop; Write-Host 'OK' } catch { Write-Host 'FAILED'; exit 1 }" 2>nul
if %ERRORLEVEL% EQU 0 (
    set "DOWNLOADED=1"
    goto :check
)

:check
if "%DOWNLOADED%"=="0" (
    echo.
    echo [FAIL] Could not download the font automatically.
    echo.
    echo Please manually download Noto Sans TC from:
    echo   https://fonts.google.com/noto/specimen/Noto+Sans+TC
    echo.
    echo Place the TTF file at:
    echo   %FONT_PATH%
    echo.
    pause
    exit /b 1
)

REM --- Verify file size ---------------------------------------------------
for %%F in ("%TEMP_PATH%") do set "DL_SIZE=%%~zF"
if !DL_SIZE! LSS 50000 (
    echo [FAIL] Downloaded file is too small (!DL_SIZE! bytes), may be invalid.
    del "%TEMP_PATH%" >nul 2>&1
    pause
    exit /b 1
)

REM --- Verify TTF magic bytes ---------------------------------------------
powershell -NoProfile -Command "$b = [System.IO.File]::ReadAllBytes('%TEMP_PATH%'); if ($b[0] -eq 0x00 -and $b[1] -eq 0x01 -and $b[2] -eq 0x00 -and $b[3] -eq 0x00) { exit 0 } else { exit 1 }" 2>nul
if %ERRORLEVEL% NEQ 0 (
    echo [FAIL] Downloaded file is not a valid TrueType font (bad header).
    del "%TEMP_PATH%" >nul 2>&1
    pause
    exit /b 1
)

REM --- Install -----------------------------------------------------------
move /y "%TEMP_PATH%" "%FONT_PATH%" >nul 2>&1
if %ERRORLEVEL% NEQ 0 (
    echo [FAIL] Could not move font to target directory.
    pause
    exit /b 1
)

for %%F in ("%FONT_PATH%") do set "FINAL_SIZE=%%~zF"
echo [OK] Font downloaded and installed:
echo      %FONT_PATH% (!FINAL_SIZE! bytes)

:done
echo.
echo Font setup complete. Exult will use it when configured with:
echo   ^<font_tc_enabled^>yes^</font_tc_enabled^>
echo   ^<font_tc_file^>data/fonts/notosanscjk.ttf^</font_tc_file^>
echo.
echo (These settings should already be in your exult.cfg)
pause
exit /b 0
