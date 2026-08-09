#!/bin/bash
#
# serve_hls.sh - Start an HTTP server to serve HLS segments and web player.
#
# Usage:
#   ./scripts/serve_hls.sh
#
# This script starts a Python HTTP server on port 8080, serving from the
# repository root. This allows:
#   - Player UI: http://localhost:8080/web/player.html
#   - Live stream: http://localhost:8080/segments/live.m3u8
#   - Segments: http://localhost:8080/segments/segment_*.ts
#
# Press Ctrl+C to stop the server.

set -e

# Change to repository root
REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$REPO_ROOT"

# Check if Python 3 is available
if ! command -v python3 &> /dev/null; then
    echo "Error: Python 3 is required but not installed."
    exit 1
fi

# Ensure segments directory exists
if [ ! -d "segments" ]; then
    echo "Creating segments directory..."
    mkdir -p segments
fi

echo "=========================================="
echo "Genea HLS Streaming Server"
echo "=========================================="
echo ""
echo "Starting HTTP server on http://localhost:8080"
echo ""
echo "Access points:"
echo "  - Player: http://localhost:8080/web/player.html"
echo "  - Live:   http://localhost:8080/segments/live.m3u8"
echo "  - Archive: http://localhost:8080/segments/archive.m3u8"
echo ""
echo "Serving from: $REPO_ROOT"
echo ""
echo "Press Ctrl+C to stop the server."
echo "=========================================="
echo ""

# Start HTTP server
python3 -m http.server 8080
