#!/usr/bin/env bash
set -euo pipefail

MODULE_NAME="secure_aes"

if lsmod | grep -q "^${MODULE_NAME}\\b"; then
    echo "Unloading ${MODULE_NAME}..."
    sudo rmmod "${MODULE_NAME}"
else
    echo "Module ${MODULE_NAME} is not loaded."
fi