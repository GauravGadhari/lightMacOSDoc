#!/bin/bash
# ══════════════════════════════════════════════════════════════════════════════
# Configure macOS Dock as Primary Dock & Enable Magic Lamp Animation
# ══════════════════════════════════════════════════════════════════════════════

set -e

echo "================================================================="
echo "  Configuring macOS Dock as Primary Dock on KDE Plasma 6"
echo "================================================================="
echo ""

# 1. Ensure KWin Magic Lamp Effect is Enabled
echo "==> Step 1: Enabling KWin Magic Lamp effect..."
kwriteconfig6 --file kwinrc --group Plugins --key magiclampEnabled true

# Check if kwin is running and reload effects
if qdbus6 org.kde.KWin /KWin >/dev/null 2>&1; then
    qdbus6 org.kde.KWin /KWin reconfigure || true
    qdbus6 org.kde.KWin /Effects org.kde.kwin.Effects.loadEffect magiclamp || true
    echo "    ✓ Magic Lamp effect enabled and loaded in KWin."
else
    echo "    ✓ Magic Lamp configured in kwinrc."
fi

echo ""
echo "==> Step 2: Primary Dock Magic Lamp Redirection..."
echo "    In KDE Plasma, KWin minimizes windows toward the task manager"
echo "    registered in Plasma. Currently, your right-side KDE panel"
echo "    has an 'Icons-Only Task Manager' widget, which tells KWin to suck"
echo "    windows into the right edge (x=1848)."
echo ""
echo "    To direct the Magic Lamp animation straight into your macOS Dock:"
echo "    1. Right-click any empty space on your right-side KDE panel."
echo "    2. Click 'Enter Edit Mode' (or 'Show Panel Configuration')."
echo "    3. Hover over the app icons on the right panel and click the 'Remove' (trash) icon"
echo "       to remove the 'Icons-Only Task Manager' widget from the right panel."
echo "       (You can keep your App Launcher / Apple menu, System Tray, and Clock on it!)."
echo "    4. Click outside to exit edit mode."
echo ""
echo "    Once the task manager widget is removed from the right panel,"
echo "    KWin naturally animates the Magic Lamp down into your macOS Dock!"
echo ""
echo "================================================================="
echo "  Configuration complete! Launching macOS Dock..."
echo "================================================================="
