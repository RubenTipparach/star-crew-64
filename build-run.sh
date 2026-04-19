#!/usr/bin/env bash
# Build star-crew-64.z64 using the local N64 toolchain (see install.sh),
# then launch the ROM in an emulator if one is installed.
#
# Usage:
#   ./build-run.sh            # build + run
#   ./build-run.sh --build    # build only
#   ./build-run.sh --run      # run only
#   ./build-run.sh --clean    # wipe build artifacts

set -euo pipefail

PROJECT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TOOLCHAIN_DIR="$PROJECT_DIR/n64-toolchain"
ROM="$PROJECT_DIR/star-crew-64.z64"

do_build=1
do_run=1
case "${1:-}" in
  --build) do_run=0 ;;
  --run)   do_build=0 ;;
  --clean)
    (cd "$PROJECT_DIR" && rm -rf build filesystem star-crew-64.z64)
    echo "Cleaned."
    exit 0
    ;;
  "") ;;
  *) echo "Unknown option: $1" >&2; exit 2 ;;
esac

if [ "$do_build" -eq 1 ]; then
  if [ ! -x "$TOOLCHAIN_DIR/bin/mips64-elf-gcc" ]; then
    echo "ERROR: N64 toolchain not found at $TOOLCHAIN_DIR" >&2
    echo "Run ./install.sh first." >&2
    exit 1
  fi
  if [ ! -d "$PROJECT_DIR/tiny3d" ]; then
    echo "ERROR: tiny3d/ missing. Run ./install.sh first." >&2
    exit 1
  fi

  export N64_INST="$TOOLCHAIN_DIR"
  export PATH="$TOOLCHAIN_DIR/bin:$PATH"

  echo "=== Building star-crew-64 ==="
  make -C "$PROJECT_DIR" -j"$(sysctl -n hw.ncpu 2>/dev/null || echo 4)"
  [ -f "$ROM" ] || { echo "ERROR: ROM not produced at $ROM" >&2; exit 1; }
  echo "=== Built: $ROM ==="
fi

if [ "$do_run" -eq 1 ]; then
  [ -f "$ROM" ] || { echo "ERROR: no ROM at $ROM — build first." >&2; exit 1; }
  for cmd in ares mupen64plus simple64 simple64-gui; do
    if command -v "$cmd" >/dev/null 2>&1; then
      echo "=== Launching $cmd ==="
      exec "$cmd" "$ROM"
    fi
  done
  for app in "/Applications/ares.app" "/Applications/simple64.app" "/Applications/mupen64plus.app"; do
    if [ -d "$app" ]; then
      echo "=== Launching $app ==="
      exec open -a "$app" "$ROM"
    fi
  done
  echo "No N64 emulator found. Try: brew install --cask ares"
  echo "ROM path: $ROM"
fi
