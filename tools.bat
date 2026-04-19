@echo off
REM Launch the star-crew-64 in-browser dev tools.
REM
REM Opens tools\index.html — a single page with tabs for the level editor
REM and model editor. Both editors fetch() from assets\, which browsers
REM block on file:// URLs, so this serves the repo root over HTTP.
REM
REM Usage:
REM   tools.bat          serve on port 8000
REM   tools.bat 9000     custom port

setlocal

set "ROOT=%~dp0"
set "PORT=%~1"
if "%PORT%"=="" set "PORT=8000"

set "URL=http://localhost:%PORT%/tools/"
echo Serving %ROOT% at http://localhost:%PORT%
echo Opening: %URL%

start "" "%URL%"

cd /d "%ROOT%"
where python >nul 2>&1
if %errorlevel%==0 (
    python -m http.server %PORT%
) else (
    where py >nul 2>&1
    if %errorlevel%==0 (
        py -3 -m http.server %PORT%
    ) else (
        echo ERROR: Python not found on PATH. Install Python 3 from https://www.python.org/
        exit /b 1
    )
)

endlocal
