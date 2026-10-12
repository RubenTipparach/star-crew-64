---
name: material-maker-headless
description: Run Material Maker (RodZill4's node-based texture tool) with no GPU and no screen, as in a Claude Code cloud session, and render .ptex graphs to PNG maps from the command line. Sets up Godot 4.7, a pinned Material Maker source checkout with two exporter patches, Mesa's software Vulkan (lavapipe) and Xvfb, renders albedo, normal, ORM, emission and height maps, and checks the render really ran. Use whenever a Material Maker graph must be rendered or re-exported, a new or changed .ptex needs its maps, a texture pipeline says "needs Material Maker", or someone asks whether Material Maker can run here ("render the ptex", "export the materials", "Material Maker in the cloud", "run Material Maker headless", "re-export the textures", "new material graph").
metadata:
  author: Claude Code, for the owner's projects
  version: "1.0"
---

# Material Maker, headless

The owner, 2026-10-09: "Damn, that's cool. Ok document this skill on all repos here on texture
processes". The skill is the same in every repository that has it (Star Crew Bridge Simulator,
Pale-Blue-Dot, fps-game-demo, star-crew-64, godot-sandbox). The part before "In this repository"
is shared and only the last section differs, so a fix to the shared part goes to every copy.

Material Maker is a Godot application, and it renders every graph on Godot's GPU
`RenderingDevice`. A machine with no GPU still runs it, with two things in place: Mesa's
**lavapipe** (a software Vulkan driver) and an **Xvfb** virtual screen. Godot's `--headless`
display server creates no rendering device, so it cannot render a material. Material Maker's
command-line exporter also needs two small patches. Set those up once and `--export-material`
writes the same maps it writes on a desktop.

## What is proven

| Date | What was rendered | Result |
| --- | --- | --- |
| 2026-10 (godot-sandbox) | The terrain kit graphs and the alien normal styles | The first headless recipe, in `godot-sandbox/docs/mockups/materials/README.md` |
| 2026-10-09 | Star Crew's eight graphs (`tools/materials/ptex`) at 2048 px | 77 s on lavapipe. `tech_panel` and `light_panel` Lanczos-downsampled to 1024 match fps-game-demo's committed desktop exports exactly (mean difference 0.0 of 255) |
| 2026-10-09 | godot-sandbox's `lava_crust` and `ice` at `--size 512` | 12 s. Downsampled to 256 they match the committed `export/` maps exactly |
| 2026-10-09 | fps-game-demo's whole `export_materials.sh` (twelve graphs, then its post-process), at fps-game-demo `f6cd25c` and again at `9579fca` | About 3 min 10 s. Every texture and material it writes into `game/` came out byte-identical to the committed files |

So a cloud render is not an approximation: it is the desktop render, pixel for pixel. It is
slow (a 2048 px graph takes about 10 s on lavapipe), which matters only for big batches.

## Set up (once per session, about 40 s)

```sh
bash .claude/skills/material-maker-headless/scripts/setup.sh
# prints: export MM_DIR=/tmp/material-maker   export GODOT=/tmp/godot47/Godot_v4.7-stable_linux.x86_64
```

It is idempotent, and does five things:

1. installs `xvfb` and `mesa-vulkan-drivers` if missing;
2. fetches Godot 4.7-stable from the Godot GitHub release (or uses `GODOT`);
3. fetches Material Maker's source at the pinned commit (`MM_REV`, default `174f15e`, master on
   2026-10-07, whose `project.godot` targets Godot 4.7) into `MM_DIR`;
4. applies the two patches below to `parse_args.gd`;
5. runs Godot's one-time `--import` of Material Maker's resources.

The checkout lives in `/tmp`, never in a repository. Nothing here is committed but the graphs
and what a repository's own pipeline makes from the maps.

## Render

```sh
bash .claude/skills/material-maker-headless/scripts/render.sh -o /tmp/mm-out path/to/*.ptex
bash .claude/skills/material-maker-headless/scripts/render.sh -o /tmp/mm-out --size 512 one.ptex
```

| Option | Default | Does |
| --- | --- | --- |
| `-o DIR` | required | Where the maps go (created) |
| `--size PX` | 2048 | Render size. Only works with the patch |
| `-t TARGET` | `Godot/Godot 4 Standard` | Material Maker's export profile |
| `MM_TIMEOUT` | 1800 | Seconds before the render is stopped |

The Godot 4 Standard profile writes `<graph>_albedo.png`, `_normal.png`, `_orm.png` (red
ambient occlusion, green roughness, blue metallic), `_emission.png` and `_heightmap.png` when the
graph wires those ports, and a `<graph>.tres` StandardMaterial3D. Each repository's own export
script and post-process take it from there (the table below).

The script wraps Godot in `xvfb-run` unless a `DISPLAY` is set. It ends by checking every graph:
an albedo was written, and the normal map is not flat. It prints each normal's spread and exits
non-zero on a failure, with the log in `<out>/material-maker.log`.

## The two patches

Both go in `parse_args.gd`, just before the call to `export_files`. Neither is upstream.
`setup.sh` applies them, and it refuses to guess if upstream has moved the line.

1. **Wait for the RenderingDevice.** The render thread creates the device asynchronously, and in
   the GUI the splash screen hides that. Exporting at once compiles the buffer nodes against a null
   device: every normal map comes out flat and every AO channel constant, with no error.

   ```gdscript
   while mm_renderer.rendering_device == null:
       await get_tree().process_frame
   ```

2. **Honour `--size`.** The flag is parsed into `texture_size` and never used, so exports are
   always 2048 px without this:

   ```gdscript
   if texture_size > 0:
       image_size = texture_size
   ```

## Proving a render

A render that "worked" can still be wrong. Before trusting maps, in this order:

- **The check passed.** `render: ok`, and each normal spread is well over 1 (flat is about 0;
  real graphs read 18 to 75).
- **A known graph still renders the same.** When the pin, Godot or the patches change, render a
  graph that has committed output and compare after the same downsample (mean difference of 0 is
  the expectation; anything visible is a regression in the setup, not in the graph).
- **Look at it.** Read the albedo and the repository's contact sheet. A graph can render
  "correctly" into the wrong colours.

## Noise in the log, and real failures

| Log line | Meaning |
| --- | --- |
| `ALSA lib ...`, `init_output_device` | No sound card. Harmless |
| `Steam may not be initialized` | Material Maker's Steam hooks. Harmless |
| `Cannot open user://export_targets` | No user export profiles. Harmless |
| `A Thread object is being destroyed`, `Semaphore object is being destroyed`, `ObjectDB instances were leaked`, `resources still in use at exit` | On quit. Harmless |
| `render: FAIL <graph>: flat normal map` | The device was not ready: the wait patch is missing |
| Every export is 2048 whatever `--size` says | The size patch is missing |
| `Using target X (and not Y)` | The graph has no profile named `-t`; Material Maker picked the closest. Check it is the one you meant |
| No albedo for a graph | It failed to load. Search the log for its name. A graph saved by a newer Material Maker than the pin may need the pin moved |

## Rules

- **The graph is the one implementation of how a material looks.** Never write a Python or shader
  image generator that imitates a graph because Material Maker "cannot run here". It can.
- **Change the graph, re-render, run the repository's own post-process.** Never hand-edit an
  exported map or a layer built from one.
- **Commit the `.ptex` and what the repository's pipeline makes**, never the Material Maker
  checkout or the raw renders (each pipeline keeps them in a gitignored `raw/` or in `/tmp`).
- **Record where maps came from.** A pipeline that writes a `SOURCE.txt` or a manifest says
  "Material Maker <commit>, headless on lavapipe" for a cloud render.
- **A re-export can move pixels.** Rendering at a different size than last time (2048 straight
  from Material Maker against a committed 1024 export, say) shifts small details after
  downsampling. Re-exporting everything is its own commit that says so and shows the contact
  sheet, never a side effect of another change.
- **Lavapipe says nothing about speed.** It renders the same pixels slowly; no frame time or GPU
  cost is measured here.

## Material Maker in each repository

| Repository | Graphs | Export | What the maps become |
| --- | --- | --- | --- |
| Star Crew Bridge Simulator | `tools/materials/ptex/` (written by `build_ptex.py`) | `tools/materials/export_materials.sh` | 128 px layers of one texture array, relief baked into the colour, emission in alpha (`material-maker` skill) |
| fps-game-demo (Undercity) | `tools/material_maker/ptex/` (written by `build_ptex.py`) | `tools/material_maker/export_materials.sh` | 1024 px PBR maps and Godot materials in `game/` |
| godot-sandbox (Mining Mike) | `docs/mockups/materials/` and `docs/mockups/alien_normals/` | the command in its README, `alien_normals/render.py` | The diorama page's maps; the alien normal styles |
| Pale-Blue-Dot | none yet | | 32 x 32 texel pixel-art tiles; see that repository's section |
| star-crew-64 | none | | 32 x 32 N64 textures from a stdlib generator; see that repository's section |

## In this repository: star-crew-64

star-crew-64 has no Material Maker graphs, and its asset rules keep it that way for now
(CLAUDE.md, "Static game asset guidelines"):

- textures are 32 x 32 or smaller 8-bit RGBA PNGs in `assets/textures/`, converted to `.sprite`
  by `mksprite` at build time;
- they are written by generators under `tools/` (`tools/gen-textures.py`) that use only the
  standard library and zlib.

Material Maker needs Godot, a Vulkan driver and a numpy or Pillow downsample, so it cannot be a
step in this repository's build without changing rule 4. Don't add one without the owner asking.

What it can do here today, with nothing committed but the PNG a generator writes:

- **Design reference.** Render a graph at `--size 256` to work out a look (a deck plate, a hazard
  border), then write the generator so its 32 x 32 output reads the same, and compare the two side
  by side at 8 x nearest. The committed generator stays the one implementation.
- **The main game is Star Crew Bridge Simulator**, whose materials do run through Material Maker.
  This repository is the N64 prototype and reference only.
