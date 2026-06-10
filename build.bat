@echo off
setlocal enabledelayedexpansion

REM Exult VS2026 Build Script
REM Usage: build.bat [-c Release|Debug] [-p x64|Win32] [-t target] [-?]

SET CONFIG=Release
SET PLATFORM=x64
SET TARGET=Exult
SET SOLVEREL=msvcstuff\vs2026\Exult.sln

REM Parse arguments
:parse_args
if "%~1"=="" goto :build
if /i "%~1"=="-c" (
    SET CONFIG=%~2
    shift
    shift
    goto :parse_args
)
if /i "%~1"=="-p" (
    SET PLATFORM=%~2
    shift
    shift
    goto :parse_args
)
if /i "%~1"=="-t" (
    SET TARGET=%~2
    shift
    shift
    goto :parse_args
)
if /i "%~1"=="-?" goto :help
if /i "%~1"=="--help" goto :help
echo Unknown option: %~1
goto :help

:help
echo Exult VS2026 Build Script
echo.
echo Usage: build.bat [options]
echo.
echo Options:
echo   -c ^<config^>    Build configuration: Release or Debug (default: Release)
echo   -p ^<platform^>  Platform: x64 or Win32 (default: x64)
echo   -t ^<target^>    Target: Exult, expack, exconfig, data, exult_studio, or all (default: Exult)
echo   -?              Show this help
echo.
echo Examples:
echo   build.bat                          # Build Exult, Release^|x64
echo   build.bat -c Debug                 # Build Exult, Debug^|x64
echo   build.bat -t all                   # Build all targets, Release^|x64
echo   build.bat -c Debug -p Win32 -t Exult  # Build Exult, Debug^|Win32
exit /b 0

:build
echo ============================================
echo Exult VS2026 Build
echo ============================================
echo Configuration: %CONFIG%
echo Platform: %PLATFORM%
echo Target: %TARGET%
echo Solution: %SOLVEREL%
echo ============================================

if /i "%TARGET%"=="all" (
    echo Building all targets...
    MSBuild "%SOLVEREL%" /p:Configuration=%CONFIG% /p:Platform=%PLATFORM% /m
) else (
    echo Building %TARGET%...
    MSBuild "%SOLVEREL%" /p:Configuration=%CONFIG% /p:Platform=%PLATFORM% /t:%TARGET% /m
)

if %ERRORLEVEL% neq 0 (
    echo.
    echo ============================================
    echo BUILD FAILED
    echo ============================================
    exit /b 1
)

echo.
echo ============================================
echo BUILD SUCCEEDED
echo ============================================
exit /b 0
