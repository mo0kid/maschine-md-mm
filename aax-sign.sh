#!/bin/bash

set -euo pipefail

# Compatibility entry point. The installer builder now follows the same
# interface and release layout as ../shine-reverb/Installer/build_installer.sh.
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
exec "$SCRIPT_DIR/scripts/macos/build_installer.sh" "$@"
