@echo off
REM Build star-crew-64 N64 Game and launch it in ares.
REM
REM Usage:
REM   build-run.bat           build + run
REM   build-run.bat --build   build only
REM   build-run.bat --run     run only
REM   build-run.bat --clean   wipe build artifacts

setlocal EnableDelayedExpansion

set "PROJECT_DIR=%~dp0"
set "ROM=%PROJECT_DIR%star-crew-64.z64"
set "MSYS_BASH=C:\msys64\usr\bin\bash.exe"

REM Local, machine-specific overrides (e.g. ARES_EXE). Not committed.
if exist "%PROJECT_DIR%build-run.local.bat" call "%PROJECT_DIR%build-run.local.bat"

set "DO_BUILD=1"
set "DO_RUN=1"
if /i "%~1"=="--build" set "DO_RUN=0"
if /i "%~1"=="--run"   set "DO_BUILD=0"
if /i "%~1"=="--clean" goto :clean
if "%~1"=="" goto :args_ok
if /i "%~1"=="--build" goto :args_ok
if /i "%~1"=="--run"   goto :args_ok
echo Unknown option: %~1
exit /b 2

:clean
if exist "%PROJECT_DIR%build"      rmdir /s /q "%PROJECT_DIR%build"
if exist "%PROJECT_DIR%filesystem" rmdir /s /q "%PROJECT_DIR%filesystem"
if exist "%ROM%"                   del /q "%ROM%"
echo Cleaned.
exit /b 0

:args_ok

if "%DO_BUILD%"=="1" (
    if not exist "%MSYS_BASH%" (
        echo ERROR: MSYS2 not found. Run install.bat first.
        exit /b 1
    )

    set "MSYS_PROJECT_DIR=%PROJECT_DIR:\=/%"
    set "MSYS_PROJECT_DIR=!MSYS_PROJECT_DIR:C:=/c!"

    echo === Building star-crew-64 ===
    echo.

    %MSYS_BASH% -lc "export PATH='/ucrt64/bin:/usr/bin:$PATH' && export N64_INST='!MSYS_PROJECT_DIR!/n64-toolchain' && cd '!MSYS_PROJECT_DIR!' && make"

    if errorlevel 1 (
        echo.
        echo Build FAILED!
        exit /b 1
    )

    if not exist "%ROM%" (
        echo ERROR: ROM not produced at %ROM%
        exit /b 1
    )

    echo.
    echo === Built: %ROM% ===
    echo.
)

if "%DO_RUN%"=="1" (
    if not exist "%ROM%" (
        echo ERROR: no ROM at %ROM% - build first.
        exit /b 1
    )

    REM Override auto-detect by setting ARES_EXE to the full path of ares.exe.
    if defined ARES_EXE if not exist "!ARES_EXE!" (
        echo ARES_EXE is set but does not exist: !ARES_EXE!
        exit /b 1
    )
    if not defined ARES_EXE for %%P in (ares.exe) do if not defined ARES_EXE set "ARES_EXE=%%~$PATH:P"
    if not defined ARES_EXE if exist "%ProgramFiles%\ares\ares.exe"       set "ARES_EXE=%ProgramFiles%\ares\ares.exe"
    if not defined ARES_EXE if exist "%ProgramFiles(x86)%\ares\ares.exe"  set "ARES_EXE=%ProgramFiles(x86)%\ares\ares.exe"
    if not defined ARES_EXE if exist "%LOCALAPPDATA%\Programs\ares\ares.exe" set "ARES_EXE=%LOCALAPPDATA%\Programs\ares\ares.exe"
    if not defined ARES_EXE if exist "%USERPROFILE%\scoop\apps\ares\current\ares.exe" set "ARES_EXE=%USERPROFILE%\scoop\apps\ares\current\ares.exe"

    if not defined ARES_EXE (
        echo No ares install found. Set ARES_EXE=C:\path\to\ares.exe, or add ares.exe to PATH.
        echo ROM path: %ROM%
        exit /b 1
    )

    echo === Launching ares ===
    start "" "!ARES_EXE!" "%ROM%"
)

endlocal
