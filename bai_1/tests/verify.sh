#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
APP_PATH="${ROOT_DIR}/app/secure_file_app"
SAMPLE_PATH="${ROOT_DIR}/tests/sample.txt"
TMP_DIR="$(mktemp -d)"
KEY_HEX="00112233445566778899aabbccddeeff"
IV_HEX="0102030405060708090a0b0c0d0e0f10"
ENCRYPTED_PATH="${TMP_DIR}/sample.enc"
RESTORED_PATH="${TMP_DIR}/sample.restored.txt"

cleanup() {
    rm -rf "${TMP_DIR}"
}
trap cleanup EXIT

if [[ ! -x "${APP_PATH}" ]]; then
    echo "Application ${APP_PATH} is missing. Run scripts/build_all.sh first."
    exit 1
fi

if [[ ! -e /dev/secure_aes ]]; then
    echo "Device /dev/secure_aes not found. Run scripts/load_driver.sh first."
    exit 1
fi

echo "Encrypting sample file..."
"${APP_PATH}" encrypt "${SAMPLE_PATH}" "${ENCRYPTED_PATH}" --key "${KEY_HEX}" --iv "${IV_HEX}"

echo "Decrypting sample file..."
"${APP_PATH}" decrypt "${ENCRYPTED_PATH}" "${RESTORED_PATH}" --key "${KEY_HEX}" --iv "${IV_HEX}"

if cmp -s "${SAMPLE_PATH}" "${RESTORED_PATH}"; then
    echo "Verification passed: decrypted output matches the original file."
else
    echo "Verification failed: decrypted output differs from the original file."
    exit 1
fi