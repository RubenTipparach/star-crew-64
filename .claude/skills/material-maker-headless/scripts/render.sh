#!/usr/bin/env bash
# Render Material Maker graphs to PNG maps from the command line, headless (run setup.sh first).
#
#   bash .claude/skills/material-maker-headless/scripts/render.sh -o OUT [--size PX] \
#     [-t "Godot/Godot 4 Standard"] graph.ptex [graph.ptex ...]
#
# Writes OUT/<graph>_albedo.png, _normal.png, _orm.png, and _emission.png / _heightmap.png when
# the graph wires those ports, plus the target's own files (a .tres for the Godot targets).
# Then checks that the renderer really ran: a flat normal map means it did not.
# Uses Xvfb unless a DISPLAY is already set. MM_DIR and GODOT as setup.sh prints them.
set -euo pipefail
MM_DIR="${MM_DIR:-/tmp/material-maker}"
GODOT="${GODOT:-/tmp/godot47/Godot_v4.7-stable_linux.x86_64}"
out=""; size=""; target="Godot/Godot 4 Standard"; graphs=()
while [ $# -gt 0 ]; do
  case "$1" in
    -o|--output-dir) out="$2"; shift 2 ;;
    --size) size="$2"; shift 2 ;;
    -t|--target) target="$2"; shift 2 ;;
    *) graphs+=("$(realpath "$1")"); shift ;;
  esac
done
[ -n "$out" ] && [ ${#graphs[@]} -gt 0 ] || { sed -n 2,10p "$0" >&2; exit 2; }
grep -q "headless patch" "$MM_DIR/parse_args.gd" 2>/dev/null || { echo "render: run setup.sh first" >&2; exit 1; }
mkdir -p "$out"; out="$(realpath "$out")"
args=(--path "$MM_DIR" --rendering-driver vulkan --export-material -t "$target" -o "$out")
[ -n "$size" ] && args+=(--size "$size")
run=("$GODOT" "${args[@]}" "${graphs[@]}")
[ -z "${DISPLAY:-}" ] && run=(xvfb-run -a -s "-screen 0 1280x800x24" "${run[@]}")
log="$out/material-maker.log"
# Material Maker resolves some paths against the working directory.
(cd "$MM_DIR" && timeout "${MM_TIMEOUT:-1800}" "${run[@]}") >"$log" 2>&1 || true
python3 - "$out" "$log" "${graphs[@]}" <<'PY'
import os, sys
out, log, graphs = sys.argv[1], sys.argv[2], sys.argv[3:]
try:
    import numpy as np
    from PIL import Image
except ImportError:
    np = None

def spread(path):
    """Standard deviation of a normal map's red and green: about 0 when nothing was rendered."""
    a = np.asarray(Image.open(path).convert("RGB"), dtype=np.float32)
    return float(a[..., :2].std())

bad = []
for g in graphs:
    name = os.path.basename(g)[:-5]
    normal = os.path.join(out, name + "_normal.png")
    if not os.path.exists(os.path.join(out, name + "_albedo.png")):
        bad.append("%s: no albedo written" % name)
        continue
    if not os.path.exists(normal):
        note = "no normal port"
    elif np is None:
        note = "normal unchecked (no numpy or Pillow)"
    else:
        s = spread(normal)
        note = "normal spread %.1f" % s
        if s < 1.0:
            bad.append("%s: flat normal map (the renderer did not run)" % name)
    print("render: %-24s %s" % (name, note))
if bad:
    print("\n".join("render: FAIL " + b for b in bad) + "\nrender: see " + log, file=sys.stderr)
    sys.exit(1)
print("render: ok, %d graphs in %s" % (len(graphs), out))
PY
