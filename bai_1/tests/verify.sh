#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
APP_PATH="${ROOT_DIR}/app/secure_file_app"
SAMPLE_PATH="${ROOT_DIR}/tests/sample.txt"
TMP_DIR="$(mktemp -d)"
STORAGE_DIR="${TMP_DIR}/secure_store"
KEY_HEX="00112233445566778899aabbccddeeff"
RESTORED_PATH="${TMP_DIR}/sample.restored.txt"
UPDATED_SOURCE="${TMP_DIR}/updated.txt"
STORED_FILE="${STORAGE_DIR}/sample.txt.saes"

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

echo "Creating secure file from sample.txt..."
"${APP_PATH}" create sample.txt \
    --key "${KEY_HEX}" \
    --from-file "${SAMPLE_PATH}" \
    --storage "${STORAGE_DIR}"

if [[ ! -f "${STORED_FILE}" ]]; then
    echo "Verification failed: encrypted file was not created in the secure storage."
    exit 1
fi

if cmp -s "${SAMPLE_PATH}" "${STORED_FILE}"; then
    echo "Verification failed: stored file is still plaintext."
    exit 1
fi

echo "Listing secure files..."
LIST_OUTPUT="$("${APP_PATH}" list --storage "${STORAGE_DIR}")"
printf '%s\n' "${LIST_OUTPUT}"

if ! printf '%s\n' "${LIST_OUTPUT}" | grep -q '^sample.txt'; then
    echo "Verification failed: secure file does not appear in the list."
    exit 1
fi

echo "Reading secure file back..."
"${APP_PATH}" read sample.txt \
    --key "${KEY_HEX}" \
    --output "${RESTORED_PATH}" \
    --storage "${STORAGE_DIR}"

if ! cmp -s "${SAMPLE_PATH}" "${RESTORED_PATH}"; then
    echo "Verification failed: decrypted content does not match the original file."
    exit 1
fi

printf 'Updated secure file content.\n' > "${UPDATED_SOURCE}"

echo "Updating secure file..."
"${APP_PATH}" update sample.txt \
    --key "${KEY_HEX}" \
    --from-file "${UPDATED_SOURCE}" \
    --storage "${STORAGE_DIR}"

echo "Reading updated secure file back..."
"${APP_PATH}" read sample.txt \
    --key "${KEY_HEX}" \
    --output "${RESTORED_PATH}" \
    --storage "${STORAGE_DIR}"

if ! cmp -s "${UPDATED_SOURCE}" "${RESTORED_PATH}"; then
    echo "Verification failed: updated content was not stored correctly."
    exit 1
fi

echo "Deleting secure file..."
"${APP_PATH}" delete sample.txt --storage "${STORAGE_DIR}"

LIST_OUTPUT="$("${APP_PATH}" list --storage "${STORAGE_DIR}")"
if printf '%s\n' "${LIST_OUTPUT}" | grep -q '^sample.txt'; then
    echo "Verification failed: secure file still appears after deletion."
    exit 1
fi

echo "Verification passed: create/list/read/update/delete all work with encrypted storage."
