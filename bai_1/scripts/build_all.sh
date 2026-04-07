#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"

if [[ "${ROOT_DIR}" =~ [[:space:]] ]]; then
    echo "Error: Linux kernel external module builds do not work reliably from paths containing spaces."
    echo "Move the project to a path without spaces, for example: ~/bai_1"
    exit 1
fi

echo "[1/2] Building kernel module..."
make -C "${ROOT_DIR}/driver"

echo "[2/2] Building user-space tools (CLI + GTK GUI)..."
make -C "${ROOT_DIR}/app"

echo "Build completed successfully."