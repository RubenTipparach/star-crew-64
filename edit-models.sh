#!/usr/bin/env bash
# Launch the star-crew-64 model editor.
#
# Starts a local HTTP server at the repo root so the editor can fetch
# assets/models/*.json (browsers block fetch() from file:// URLs).
#
# Usage:
#   ./edit-models.sh          # serves on port 8000
#   ./edit-models.sh 9000     # custom port

set -e
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PORT="${1:-8000}"

URL="http://localhost:$PORT/tools/model-editor/"
echo "Serving $ROOT at http://localhost:$PORT"
echo "Editor: $URL"

if command -v open >/dev/null 2>&1; then
  (sleep 1 && open "$URL") &
fi

cd "$ROOT"
exec python3 -m http.server "$PORT"
