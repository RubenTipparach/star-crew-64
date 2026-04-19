#!/usr/bin/env bash
# N64 Development Environment Setup (macOS)
#
# Mac equivalent of install.bat. libdragon doesn't ship a prebuilt macOS
# toolchain, so this builds the MIPS cross-compiler from source (slow: ~30-60
# min first time). Everything lands inside the repo at ./n64-toolchain and
# ./tiny3d so it doesn't pollute the host.

set -euo pipefail

PROJECT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TOOLCHAIN_DIR="$PROJECT_DIR/n64-toolchain"
LIBDRAGON_SRC="$PROJECT_DIR/.libdragon-src"
TINY3D_DIR="$PROJECT_DIR/tiny3d"
JOBS="$(sysctl -n hw.ncpu 2>/dev/null || echo 4)"

echo "=== N64 Development Environment Setup (macOS) ==="

# 1. Homebrew + build deps required by libdragon's build-toolchain.sh
if ! command -v brew >/dev/null 2>&1; then
  echo "ERROR: Homebrew not found. Install from https://brew.sh/ first." >&2
  exit 1
fi

echo "--- Installing Homebrew build dependencies ---"
brew install --quiet gmp mpfr libmpc gsed texinfo libpng wget git make

echo "--- Installing ares emulator ---"
brew install --quiet --cask ares

# 2. Clone libdragon (preview branch — required by tiny3d)
if [ ! -d "$LIBDRAGON_SRC" ]; then
  echo "--- Cloning libdragon (preview) ---"
  git clone --depth 1 --branch preview https://github.com/DragonMinded/libdragon.git "$LIBDRAGON_SRC"
fi

# 3. Build the MIPS cross toolchain into $TOOLCHAIN_DIR
if [ ! -x "$TOOLCHAIN_DIR/bin/mips64-elf-gcc" ]; then
  echo "--- Building MIPS cross toolchain (this takes a while) ---"
  mkdir -p "$TOOLCHAIN_DIR"
  (
    cd "$LIBDRAGON_SRC/tools"
    export N64_INST="$TOOLCHAIN_DIR"
    ./build-toolchain.sh
  )
else
  echo "--- Toolchain already present at $TOOLCHAIN_DIR ---"
fi

export N64_INST="$TOOLCHAIN_DIR"
export PATH="$TOOLCHAIN_DIR/bin:$PATH"

# 4. Build + install libdragon library and tools (mksprite, audioconv64, etc.)
echo "--- Building libdragon + tools ---"
make -C "$LIBDRAGON_SRC" -j"$JOBS"
make -C "$LIBDRAGON_SRC" install
make -C "$LIBDRAGON_SRC" tools -j"$JOBS"
make -C "$LIBDRAGON_SRC" tools-install

# 5. Clone + build tiny3d (libt3d.a is linked directly from this tree)
if [ ! -d "$TINY3D_DIR" ]; then
  echo "--- Cloning tiny3d ---"
  git clone --depth 1 https://github.com/HailToDodongo/tiny3d.git "$TINY3D_DIR"
fi
echo "--- Building tiny3d ---"
make -C "$TINY3D_DIR" -j"$JOBS"

echo
echo "=== Setup Complete ==="
echo "Toolchain: $TOOLCHAIN_DIR"
echo "tiny3d:    $TINY3D_DIR"
echo
echo "To build the ROM:"
echo "  export N64_INST=\"$TOOLCHAIN_DIR\""
echo "  make"
echo
echo "Or use ./build-run.sh"
