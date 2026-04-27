#!/usr/bin/env bash
# Install star-crew-64's recommended VirtualPad1 input bindings into ares.
#
# What it does:
#   - Backs up ~/Library/Application Support/ares/settings.bml to settings.bml.bak
#   - Replaces the VirtualPad1 block with the curated mapping from
#     tools/ares-settings.bml (preserves all other ares settings).
#
# After running, launch the game once with ./build-run.sh. If keyboard inputs
# don't register, ares' SDL keyboard ID may be different on your build — open
# ares → Settings → Input → Nintendo 64 → Port 1 → Gamepad and click each row
# to re-record it. Then run tools/save-ares-settings.sh to capture those
# working bindings into the repo.

set -euo pipefail

SETTINGS="$HOME/Library/Application Support/ares/settings.bml"
TEMPLATE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/ares-settings.bml"

if [ ! -f "$SETTINGS" ]; then
  echo "ERROR: $SETTINGS not found." >&2
  echo "Launch ares once first so it generates a default settings.bml." >&2
  exit 1
fi
if [ ! -f "$TEMPLATE" ]; then
  echo "ERROR: $TEMPLATE missing — pull tools/ares-settings.bml from the repo." >&2
  exit 1
fi

cp "$SETTINGS" "$SETTINGS.bak"
echo "Backed up: $SETTINGS.bak"

# Splice: drop everything between "VirtualPad1" and the next top-level section
# (lines that don't start with whitespace), then inject the template's block.
# Awk handles this cleanly because ares' BML is strictly indentation-based.
awk -v block_path="$TEMPLATE" '
  BEGIN {
    # Read the replacement block (skip comment lines that start with //).
    while ((getline line < block_path) > 0) {
      if (line !~ /^[[:space:]]*\/\//) {
        block = block (block ? "\n" : "") line
      }
    }
    close(block_path)
    skipping = 0
  }
  {
    if ($0 == "VirtualPad1") {
      print block
      skipping = 1
      next
    }
    if (skipping) {
      # Stay in skip-mode while the line is indented (still inside VirtualPad1).
      if ($0 ~ /^[[:space:]]/) next
      skipping = 0
    }
    print
  }
' "$SETTINGS.bak" > "$SETTINGS"

echo "Updated: $SETTINGS"
echo "Run ./build-run.sh and try the keyboard / controller bindings."
