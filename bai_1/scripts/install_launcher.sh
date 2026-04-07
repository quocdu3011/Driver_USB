#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
TARGET_DIR="${HOME}/.local/share/applications"
TARGET_FILE="${TARGET_DIR}/secure-file-manager.desktop"
GUI_PATH="${ROOT_DIR}/app/secure_file_gui"

if [[ ! -x "${GUI_PATH}" ]]; then
    echo "Build the GUI first with scripts/build_all.sh"
    exit 1
fi

mkdir -p "${TARGET_DIR}"
cat > "${TARGET_FILE}" <<EOF
[Desktop Entry]
Type=Application
Version=1.0
Name=Secure File Manager
Comment=GTK frontend for the secure AES kernel driver
Exec=${GUI_PATH}
Icon=system-file-manager
Terminal=false
Categories=Utility;Security;
StartupNotify=true
EOF

chmod +x "${TARGET_FILE}"
echo "Launcher installed at ${TARGET_FILE}"