#!/bin/bash
set -e

SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"

echo "=========================================="
echo "   Integration Test Suite"
echo "=========================================="
echo

# Check if build exists
if [[ ! -f "$PROJECT_ROOT/build/test_reconnect_scenario" ]]; then
    echo "❌ Error: test_reconnect_scenario not built"
    echo "Run: cmake --build build -j\$(nproc)"
    exit 1
fi

echo "Running integration tests..."
echo

# Run integration test scenarios
if "$PROJECT_ROOT/build/test_reconnect_scenario"; then
    echo
    echo "=========================================="
    echo "✅ ALL INTEGRATION TESTS PASSED"
    echo "=========================================="
    exit 0
else
    echo
    echo "=========================================="
    echo "❌ INTEGRATION TESTS FAILED"
    echo "=========================================="
    exit 1
fi
