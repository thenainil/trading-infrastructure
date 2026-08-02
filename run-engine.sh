#!/usr/bin/env bash
set -euo pipefail

PROJECT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ENV_FILE="$HOME/.secrets/.env"
BUILD_DIR="$HOME/.cmake-build/trading-infrastructure/release"
BINARY="$BUILD_DIR/trading_infrastructure"
cd "$PROJECT_DIR"

if [[ -f "$ENV_FILE" ]]; then
    echo "Loading Environment Variables from $ENV_FILE"
    set -a
    source "$ENV_FILE"
    set +a
else
    echo "Warning: Environment File Not Found: $ENV_FILE"
fi

echo "Configuring CMake and Compiling..."
cmake --preset release
cmake --build "$BUILD_DIR" --parallel "$(nproc)"
if [[ ! -x "$BINARY" ]]; then
    echo "Error: Compiled Binary Not Found At:"
    echo "$BINARY"
    exit 1
fi

echo "Starting Trading Infrastructure"
exec "$BINARY"