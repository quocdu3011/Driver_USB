#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"

"${ROOT_DIR}/scripts/build_all.sh"
"${ROOT_DIR}/scripts/load_driver.sh"
"${ROOT_DIR}/tests/verify.sh"

echo "CLI verification completed."
echo "Launch the GUI with: bash ${ROOT_DIR}/scripts/run_gui.sh"
echo "Inspect kernel logs with: sudo dmesg | tail -n 50"