#!/bin/sh
# One command to build the C code and start the web app.
cd "$(dirname "$0")" || exit 1
command -v gcc  >/dev/null || { echo "gcc is required (macOS: xcode-select --install)"; exit 1; }
command -v node >/dev/null || { echo "Node.js 18+ is required (https://nodejs.org)"; exit 1; }
make || exit 1
echo "Open http://localhost:${PORT:-3000}"
exec node server.js
