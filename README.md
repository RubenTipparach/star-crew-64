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

See [install.bat](install.bat) and [build-run.bat](build-run.bat). Produces `star-crew-64.z64` and launches it in [ares](https://ares-emu.net/).

## Browser build (CI)

The `Build Game + Emulator and Deploy to GitHub Pages` workflow runs on every push:

1. Builds `star-crew-64.z64` inside the official libdragon Docker image.
2. Builds the `rubens-ares` emulator to WASM from the `emulator/` submodule.
3. Stages both into `_site/` (the ROM is published as `rom.z64` so the
   emulator's auto-load picks it up on page load) and deploys to GitHub Pages.

## Scripts reference

All scripts come in matched `.bat` (Windows, `cmd.exe`) and `.sh` (macOS / Linux)
pairs unless noted. Run from the repo root.

### Game — setup & build

| Script | What it does |
| --- | --- |
| [install.bat](install.bat) / [install.sh](install.sh) | One-time dev environment setup. Installs the MIPS cross toolchain to `./n64-toolchain`, clones + builds libdragon (preview branch) into `./.libdragon-src`, and clones + builds tiny3d into `./tiny3d`. Windows grabs the prebuilt toolchain via MSYS2 (`C:\msys64` required); macOS builds the toolchain from source via Homebrew (slow: ~30–60 min). |
| [build-run.bat](build-run.bat) / [build-run.sh](build-run.sh) | Build `star-crew-64.z64` and launch it in an emulator. Flags: `--build` (build only), `--run` (run only), `--clean` (wipe `build/`, `filesystem/`, ROM). Windows auto-detects ares; override with `ARES_EXE`. macOS tries ares / mupen64plus / simple64 from PATH or `/Applications/`. |
| [build-run.local.bat.example](build-run.local.bat.example) | Template for a machine-local, gitignored `build-run.local.bat` that `build-run.bat` sources on each run. Use it to pin `ARES_EXE` to a specific `ares.exe` path. |
| [clean.bat](clean.bat) | Hard-cleans the libdragon and tiny3d checkouts (`make clean` + `git checkout -- .` + `git clean -fd`). Windows-only; use if a library build got into a bad state. |

### Dev tools

| Script | What it does |
| --- | --- |
| [tools.bat](tools.bat) / [tools.sh](tools.sh) | Launcher for the in-browser editors. Serves the repo root over HTTP (so editors can `fetch()` assets) and opens [tools/index.html](tools/index.html), a single page with tabs for the level editor and model editor. Default port 8000, override with an arg: `tools.bat 9000`. |
| [edit-models.sh](edit-models.sh) | Older single-purpose launcher for just the model editor (macOS / Linux). Superseded by `tools.sh`. |

### Browser emulator (`emulator/` submodule — [rubens-ares](https://github.com/RubenTipparach/rubens-ares))

These live inside the submodule and only need to be run when hacking on the
emulator itself — the GitHub Pages workflow builds the WASM bundle automatically.

| Script | What it does |
| --- | --- |
| [emulator/install.bat](emulator/install.bat) | Sets up the emulator's build environment: initializes the ares submodule, installs npm deps under `emulator/web/`, and clones + activates Emscripten SDK into `emulator/emsdk/`. |
| [emulator/build.bat](emulator/build.bat) | Fast rebuild — applies `wasm-patches/`, runs `emmake make wasm32=true` in `build/web-ui/`, copies `ares.js` / `ares.wasm` / `ares.data` into `emulator/web/`. Assumes emsdk is already on PATH. |
| [emulator/build-run.bat](emulator/build-run.bat) | Full build-and-serve. Same as `build.bat` but also auto-activates emsdk, stages the ares source tree into `build/`, and starts `http-server` on port 3000. |
| [emulator/run-experiment.sh](emulator/run-experiment.sh) | Profiling helper. Touches known hot files to force a partial rebuild, runs `build.bat`, redeploys, kills any stale server on :3000, and starts `profile-server.js` for a fixed duration. Takes `<label> [duration_sec]`. |
