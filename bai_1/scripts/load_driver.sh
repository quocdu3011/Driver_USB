#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
MODULE_PATH="${ROOT_DIR}/driver/secure_aes.ko"
MODULE_NAME="secure_aes"
DEVICE_NODE="/dev/secure_aes"

if [[ ! -f "${MODULE_PATH}" ]]; then
    echo "Kernel module not found at ${MODULE_PATH}. Run scripts/build_all.sh first."
    exit 1
fi

if lsmod | grep -q "^${MODULE_NAME}\\b"; then
    echo "Module ${MODULE_NAME} is already loaded."
else
    echo "Loading ${MODULE_NAME} from ${MODULE_PATH}..."
    if ! sudo insmod "${MODULE_PATH}"; then
        echo "Failed to load ${MODULE_NAME}."
        if command -v mokutil >/dev/null 2>&1; then
            if mokutil --sb-state 2>/dev/null | grep -qi "enabled"; then
                echo "Secure Boot appears to be enabled. Unsigned kernel modules are blocked."
                echo "Disable Secure Boot or sign driver/secure_aes.ko before loading the module."
            fi
        fi
        exit 1
    fi
fi

if [[ ! -e "${DEVICE_NODE}" ]]; then
    echo "Device node ${DEVICE_NODE} was not created automatically. Creating it manually..."
    MAJOR_NUMBER="$(awk '$2 == "secure_aes" { print $1 }' /proc/devices | tail -n 1)"
    if [[ -z "${MAJOR_NUMBER}" ]]; then
        echo "Could not determine major number for secure_aes."
        exit 1
    fi

    sudo mknod "${DEVICE_NODE}" c "${MAJOR_NUMBER}" 0
    sudo chmod 666 "${DEVICE_NODE}"
fi

echo "Driver is ready:"
ls -l "${DEVICE_NODE}"