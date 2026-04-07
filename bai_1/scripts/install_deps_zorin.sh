#!/usr/bin/env bash
set -euo pipefail

sudo apt update
sudo apt install -y \
    build-essential \
    gcc \
    make \
    pkg-config \
    libgtk-3-dev \
    linux-headers-$(uname -r) \
    mokutil \
    openssl

echo "Dependencies installed for Zorin OS / Ubuntu-based systems."