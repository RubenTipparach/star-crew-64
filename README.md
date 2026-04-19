# star-crew-64

An N64 game built with [libdragon](https://libdragon.dev/) + [tiny3d](https://github.com/HailToDodongo/tiny3d), playable in the browser via the
[`rubens-ares`](https://github.com/RubenTipparach/rubens-ares) WASM+WebGPU emulator.

## Layout

```
src/                 game source (C, libdragon + tiny3d)
assets/              art/audio converted into filesystem/ at build time
emulator/            rubens-ares — included as a git submodule
Makefile             libdragon build for the N64 ROM
.github/workflows/   builds ROM + emulator, deploys to GitHub Pages on every push
```

## Cloning

```bash
git clone --recursive https://github.com/rubentipparach/star-crew-64.git
# or after a plain clone:
git submodule update --init --recursive
```

## Local build (Windows / MSYS2)

See `install.bat` and `build-run.bat`. Produces `star-crew-64.z64` and launches it in [ares](https://ares-emu.net/).

## Browser build (CI)

The `Build Game + Emulator and Deploy to GitHub Pages` workflow runs on every push:

1. Builds `star-crew-64.z64` inside the official libdragon Docker image.
2. Builds the `rubens-ares` emulator to WASM from the `emulator/` submodule.
3. Stages both into `_site/` (the ROM is published as `rom.z64` so the
   emulator's auto-load picks it up on page load) and deploys to GitHub Pages.
