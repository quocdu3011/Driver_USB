#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
GUI_PATH="${ROOT_DIR}/app/secure_file_gui"

if [[ ! -x "${GUI_PATH}" ]]; then
    echo "GUI binary not found at ${GUI_PATH}. Run scripts/build_all.sh first."
    exit 1
fi

if [[ ! -e /dev/secure_aes ]]; then
    echo "Warning: /dev/secure_aes is missing. Load the driver first with scripts/load_driver.sh."
fi

exec "${GUI_PATH}"