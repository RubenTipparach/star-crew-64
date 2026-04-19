@echo off
REM N64 Development Environment Setup (Windows)
REM
REM Installs into the repo:
REM   - N64 prebuilt MIPS cross toolchain -> ./n64-toolchain
REM   - libdragon (preview) built + installed into ./n64-toolchain (gives n64.mk)
REM   - libdragon host tools (mksprite, audioconv64, ...) into ./n64-toolchain/bin
REM   - tiny3d cloned + built -> ./tiny3d (provides libt3d.a and t3d.mk)
REM
REM Requires MSYS2 at C:\msys64 (https://www.msys2.org/).

setlocal EnableDelayedExpansion

set "PROJECT_DIR=%~dp0"
set "TOOLCHAIN_DIR=%PROJECT_DIR%n64-toolchain"
set "LIBDRAGON_SRC=%PROJECT_DIR%.libdragon-src"
set "TINY3D_DIR=%PROJECT_DIR%tiny3d"
set "TOOLCHAIN_URL=https://github.com/DragonMinded/libdragon/releases/download/toolchain-continuous-prerelease/gcc-toolchain-mips64-win64.zip"
set "MSYS_BASH=C:\msys64\usr\bin\bash.exe"

echo === N64 Development Environment Setup ===
echo.

REM -- 1. Prebuilt MIPS toolchain --------------------------------------------
if exist "%TOOLCHAIN_DIR%\bin\mips64-elf-gcc.exe" (
    echo [1/5] Toolchain already installed at %TOOLCHAIN_DIR%
) else (
    if not exist "%TOOLCHAIN_DIR%" mkdir "%TOOLCHAIN_DIR%"
    echo [1/5] Downloading N64 toolchain...
    curl -L "%TOOLCHAIN_URL%" -o "%TOOLCHAIN_DIR%\toolchain.zip"
    if errorlevel 1 (
        echo ERROR: Failed to download toolchain
        exit /b 1
    )
    echo       Extracting...
    pushd "%TOOLCHAIN_DIR%"
    tar -xf toolchain.zip 2>nul
    if errorlevel 1 powershell -command "Expand-Archive -Path toolchain.zip -DestinationPath . -Force"
    del toolchain.zip
    popd
)

REM -- 2. MSYS2 + build deps -------------------------------------------------
if not exist "%MSYS_BASH%" (
    echo.
    echo ERROR: MSYS2 not found at C:\msys64
    echo Install from https://www.msys2.org/ then rerun this script.
    exit /b 1
)

echo.
echo [2/5] Installing MSYS2 packages (make, git, gcc, libpng)...
%MSYS_BASH% -lc "pacman -S --noconfirm --needed make git mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-make mingw-w64-ucrt-x86_64-libpng"
if errorlevel 1 (
    echo ERROR: pacman failed
    exit /b 1
)

REM Convert Windows paths -> MSYS2 (/c/Users/...) form
set "MSYS_TOOLCHAIN=%TOOLCHAIN_DIR:\=/%"
set "MSYS_TOOLCHAIN=%MSYS_TOOLCHAIN:C:=/c%"
set "MSYS_LIBDRAGON=%LIBDRAGON_SRC:\=/%"
set "MSYS_LIBDRAGON=%MSYS_LIBDRAGON:C:=/c%"
set "MSYS_TINY3D=%TINY3D_DIR:\=/%"
set "MSYS_TINY3D=%MSYS_TINY3D:C:=/c%"

REM -- 3. Clone libdragon (preview branch - tiny3d needs it) -----------------
echo.
echo [3/5] Fetching libdragon (preview)...
if exist "%LIBDRAGON_SRC%\Makefile" (
    echo       Already cloned, skipping.
) else (
    %MSYS_BASH% -lc "git clone --depth 1 --branch preview https://github.com/DragonMinded/libdragon.git '%MSYS_LIBDRAGON%'"
    if errorlevel 1 (
        echo ERROR: Failed to clone libdragon
        exit /b 1
    )
)

REM -- 4. Build + install libdragon & its host tools -------------------------
echo.
echo [4/5] Building libdragon + tools (this is the slow step)...
%MSYS_BASH% -lc "export PATH='%MSYS_TOOLCHAIN%/bin':/ucrt64/bin:/usr/bin:$PATH && export N64_INST='%MSYS_TOOLCHAIN%' && cd '%MSYS_LIBDRAGON%' && make -j$(nproc) && make install && make tools -j$(nproc) && make tools-install"
if errorlevel 1 (
    echo ERROR: libdragon build failed
    exit /b 1
)

REM -- 5. Clone + build tiny3d -----------------------------------------------
echo.
echo [5/5] Fetching + building tiny3d...
if not exist "%TINY3D_DIR%\t3d.mk" (
    %MSYS_BASH% -lc "git clone --depth 1 https://github.com/HailToDodongo/tiny3d.git '%MSYS_TINY3D%'"
    if errorlevel 1 (
        echo ERROR: Failed to clone tiny3d
        exit /b 1
    )
)
%MSYS_BASH% -lc "export PATH='%MSYS_TOOLCHAIN%/bin':/ucrt64/bin:/usr/bin:$PATH && export N64_INST='%MSYS_TOOLCHAIN%' && cd '%MSYS_TINY3D%' && make -j$(nproc)"
if errorlevel 1 (
    echo ERROR: tiny3d build failed
    exit /b 1
)

echo.
echo === Setup Complete ===
echo.
echo Toolchain: %TOOLCHAIN_DIR%
echo tiny3d:    %TINY3D_DIR%
echo.
echo Run build-run.bat to build and launch the game.
echo.
