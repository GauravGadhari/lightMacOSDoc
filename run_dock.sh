#!/usr/bin/env bash
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# Platform is set in main.cpp (forced xcb for reliable setMask click-through).
# Do NOT override QT_QPA_PLATFORM here.
exec "$SCRIPT_DIR/build/macos-dock-qt6" "$@"
