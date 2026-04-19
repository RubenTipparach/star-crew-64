#!/usr/bin/env bash
# Launch the star-crew-64 in-browser dev tools.
#
# Opens tools/index.html — a single page with tabs for the level editor and
# model editor. Both editors fetch() from assets/, which browsers block on
# file:// URLs, so this serves the repo root over HTTP.
#
# Usage:
#   ./tools.sh          # serve on port 8000
#   ./tools.sh 9000     # custom port

set -e
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PORT="${1:-8000}"
URL="http://localhost:$PORT/tools/"

echo "Serving $ROOT at http://localhost:$PORT"
echo "Opening: $URL"

if command -v open >/dev/null 2>&1; then
  (sleep 1 && open "$URL") &
elif command -v xdg-open >/dev/null 2>&1; then
  (sleep 1 && xdg-open "$URL") &
fi

cd "$ROOT"
exec python3 -m http.server "$PORT"
