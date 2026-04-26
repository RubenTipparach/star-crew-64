#!/usr/bin/env bash
# Snapshot the live VirtualPad1 block from ares' settings.bml back into
# tools/ares-settings.bml so the repo's bindings track whatever you've
# configured via the ares GUI.
#
# Run this AFTER you've manually re-bound keys / buttons in ares' Input
# settings and confirmed they work in the running game.

set -euo pipefail

SETTINGS="$HOME/Library/Application Support/ares/settings.bml"
TARGET="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/ares-settings.bml"

if [ ! -f "$SETTINGS" ]; then
  echo "ERROR: $SETTINGS not found." >&2
  exit 1
fi

# Pull the VirtualPad1 block (the section header + subsequent indented lines).
extracted="$(awk '
  /^VirtualPad1$/ { in_block = 1; print; next }
  in_block {
    if ($0 ~ /^[[:space:]]/) { print; next }
    exit
  }
' "$SETTINGS")"

if [ -z "$extracted" ]; then
  echo "ERROR: VirtualPad1 not found in $SETTINGS" >&2
  exit 1
fi

# Preserve the leading comment block in tools/ares-settings.bml; replace only
# from the first occurrence of VirtualPad1 onward.
header="$(awk 'BEGIN{p=1} /^VirtualPad1$/{p=0} p{print}' "$TARGET")"
{
  printf '%s\n' "$header"
  printf '%s\n' "$extracted"
} > "$TARGET.tmp"
mv "$TARGET.tmp" "$TARGET"

echo "Saved live bindings → $TARGET"
echo "Commit it: git add tools/ares-settings.bml && git commit -m 'update ares bindings'"
