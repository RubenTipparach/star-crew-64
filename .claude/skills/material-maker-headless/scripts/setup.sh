#!/usr/bin/env bash
# Make Material Maker runnable headless on a Linux box with no GPU (a Claude Code cloud
# session, a CI runner): Godot 4.7, a Material Maker source checkout at a pinned revision with
# the two command-line patches applied, a software Vulkan driver, Xvfb, and the project's
# one-time resource import. Idempotent: a step already done is skipped.
#
#   bash .claude/skills/material-maker-headless/scripts/setup.sh
#
# Environment (defaults in brackets):
#   MM_DIR   where Material Maker's source goes   [/tmp/material-maker]
#   GODOT    a Godot 4.7 binary to use; fetched to /tmp/godot47 when unset and missing
#   MM_REV   the Material Maker commit            [the pinned one below]
#
# Prints the two variables render.sh reads, ready to export.
set -euo pipefail

# Material Maker master on 2026-10-07, which targets Godot 4.7 (project.godot config/features).
# Graphs saved by Material Maker 1.7 load and render on it. Move the pin on purpose, and re-run
# a graph you know before trusting the new one (SKILL.md, "Proving a render").
MM_REV="${MM_REV:-174f15edece663bf10f78588840873fa2fd17bc7}"
MM_DIR="${MM_DIR:-/tmp/material-maker}"
GODOT_URL="https://github.com/godotengine/godot/releases/download/4.7-stable/Godot_v4.7-stable_linux.x86_64.zip"

# 1. Xvfb and a software Vulkan driver (Mesa's lavapipe). Material Maker renders on a
#    RenderingDevice, which Godot's --headless display server does not create.
need=()
command -v xvfb-run >/dev/null || need+=(xvfb xauth)
ls /usr/share/vulkan/icd.d/lvp_icd*.json >/dev/null 2>&1 || need+=(mesa-vulkan-drivers)
if [ ${#need[@]} -gt 0 ]; then
  echo "setup: installing ${need[*]}"
  apt-get update -qq >/dev/null && DEBIAN_FRONTEND=noninteractive apt-get install -y -qq "${need[@]}" >/dev/null
fi

# 2. Godot 4.7.
if [ -z "${GODOT:-}" ]; then
  GODOT=/tmp/godot47/Godot_v4.7-stable_linux.x86_64
  if [ ! -x "$GODOT" ]; then
    echo "setup: fetching Godot 4.7-stable"
    mkdir -p /tmp/godot47
    curl -sSfL -o /tmp/godot47/godot.zip "$GODOT_URL"
    (cd /tmp/godot47 && unzip -oq godot.zip && rm godot.zip)
  fi
fi
"$GODOT" --version >/dev/null

# 3. Material Maker's source at the pin.
if [ "$(git -C "$MM_DIR" rev-parse HEAD 2>/dev/null)" != "$MM_REV" ]; then
  echo "setup: fetching Material Maker $MM_REV"
  rm -rf "$MM_DIR"
  git init -q "$MM_DIR"
  git -C "$MM_DIR" fetch -q --depth 1 https://github.com/RodZill4/material-maker "$MM_REV"
  git -C "$MM_DIR" checkout -q FETCH_HEAD
fi

# 4. The two patches to parse_args.gd (SKILL.md, "The two patches"). Neither is upstream.
python3 - "$MM_DIR/parse_args.gd" <<'PY'
import sys
path = sys.argv[1]
src = open(path).read()
mark = "\t\tawait export_files(expanded_files, output_dir, target, output_file, image_size)"
patch = ("\t\t# headless patch: honour --size, and wait for the RenderingDevice\n"
         "\t\tif texture_size > 0:\n\t\t\timage_size = texture_size\n"
         "\t\twhile mm_renderer.rendering_device == null:\n\t\t\tawait get_tree().process_frame\n")
if "headless patch" in src:
    sys.exit(0)
if src.count(mark) != 1:
    sys.exit("setup: parse_args.gd has changed upstream; apply the two patches by hand (SKILL.md)")
open(path, "w").write(src.replace(mark, patch + mark))
print("setup: patched parse_args.gd")
PY

# 5. The one-time import, so the exporter does not start against unimported resources.
if [ ! -f "$MM_DIR/.godot/.mm_headless_imported" ]; then
  echo "setup: importing Material Maker's resources (about 40 s)"
  timeout 900 "$GODOT" --headless --path "$MM_DIR" --import >"$MM_DIR/import.log" 2>&1 || true
  [ -d "$MM_DIR/.godot/imported" ] || { echo "setup: import failed, see $MM_DIR/import.log" >&2; exit 1; }
  touch "$MM_DIR/.godot/.mm_headless_imported"
fi

echo "export MM_DIR=$MM_DIR"
echo "export GODOT=$GODOT"
