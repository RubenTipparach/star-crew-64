@echo off
REM Install star-crew-64's recommended VirtualPad1 input bindings into ares.
REM
REM What it does:
REM   - Backs up %%APPDATA%%\ares\settings.bml to settings.bml.bak
REM   - Replaces the VirtualPad1 block with the curated mapping from
REM     tools\ares-settings.bml (preserves all other ares settings).
REM
REM After running, launch the game once with build-run.bat. If keyboard inputs
REM don't register, ares' SDL keyboard ID may be different on your build - open
REM ares - Settings - Input - Nintendo 64 - Port 1 - Gamepad and click each row
REM to re-record it. Then run tools\save-ares-settings.bat (companion script)
REM to capture those working bindings into the repo.

setlocal

set "ROOT=%~dp0"
set "TEMPLATE=%ROOT%ares-settings.bml"
set "SETTINGS=%APPDATA%\ares\settings.bml"

if not exist "%SETTINGS%" (
    echo ERROR: "%SETTINGS%" not found.
    echo Launch ares once first so it generates a default settings.bml.
    exit /b 1
)
if not exist "%TEMPLATE%" (
    echo ERROR: "%TEMPLATE%" missing - pull tools\ares-settings.bml from the repo.
    exit /b 1
)

copy /Y "%SETTINGS%" "%SETTINGS%.bak" >nul
if errorlevel 1 (
    echo ERROR: failed to back up "%SETTINGS%".
    exit /b 1
)
echo Backed up: %SETTINGS%.bak

REM Splice: drop everything between "VirtualPad1" and the next top-level
REM section (lines that don't start with whitespace), then inject the
REM template's block. ares' BML is strictly indentation-based, so a single
REM pass over the file with a "skip-while-indented" flag handles it.
powershell -NoProfile -ExecutionPolicy Bypass -Command "$ErrorActionPreference='Stop'; $tplRaw = Get-Content -LiteralPath '%TEMPLATE%'; $block = ($tplRaw | Where-Object { $_ -notmatch '^\s*//' }) -join [Environment]::NewLine; $src = Get-Content -LiteralPath '%SETTINGS%.bak'; $out = New-Object System.Collections.Generic.List[string]; $skip = $false; foreach ($line in $src) { if ($line -eq 'VirtualPad1') { $out.Add($block); $skip = $true; continue }; if ($skip) { if ($line -match '^\s') { continue } else { $skip = $false } }; $out.Add($line) }; Set-Content -LiteralPath '%SETTINGS%' -Value $out -Encoding ASCII"

if errorlevel 1 (
    echo ERROR: splice failed; restoring backup.
    copy /Y "%SETTINGS%.bak" "%SETTINGS%" >nul
    exit /b 1
)

echo Updated: %SETTINGS%
echo Run build-run.bat and try the keyboard / controller bindings.

endlocal
