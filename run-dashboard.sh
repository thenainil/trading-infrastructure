#!/usr/bin/env bash

set -euo pipefail

PROJECT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ENV_FILE="$HOME/.secrets/.env"
cd "$PROJECT_DIR/dashboard"

if [[ -f "$ENV_FILE" ]]; then
    echo "Loading Environment Variables from $ENV_FILE"
    set -a
    source "$ENV_FILE"
    set +a
fi

echo "Installing Dependencies..."
npm ci

echo "Building Metrics Dashboard..."
npm run build

echo "Starting Metrics Dashboard..."
exec npm start