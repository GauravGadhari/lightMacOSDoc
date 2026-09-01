#!/usr/bin/env bash
set -e

echo "Configuring centered 100% transparent macOS Dock in KDE Plasma 6..."

python3 - << 'PYEOF'
import subprocess

script = """
var p = panels();
var bottomPanel = null;

for (var i = 0; i < p.length; i++) {
    if (p[i].location === "bottom") {
        bottomPanel = p[i];
        break;
    }
}

if (!bottomPanel) {
    bottomPanel = new Panel();
    bottomPanel.location = "bottom";
}

bottomPanel.height = 140;
bottomPanel.floating = true;
bottomPanel.hiding = "dodgewindows";

// Clean existing widgets
var ws = bottomPanel.widgets();
for (var j = 0; j < ws.length; j++) {
    ws[j].remove();
}

// Add Left Spacer -> macOS Dock -> Right Spacer for 100% Dead-Center Alignment
bottomPanel.addWidget("org.kde.plasma.panelspacer");
bottomPanel.addWidget("org.kde.plasma.macosdock");
bottomPanel.addWidget("org.kde.plasma.panelspacer");

print("Bottom panel updated: height=140, centered!");
"""

cmd = [
    "dbus-send", "--session", "--dest=org.kde.plasmashell", "--type=method_call",
    "--print-reply", "/PlasmaShell", "org.kde.PlasmaShell.evaluateScript",
    f"string:{script}"
]

res = subprocess.run(cmd, capture_output=True, text=True)
print(res.stdout.strip())
PYEOF

# Find bottom containment ID and set 100% transparent background
python3 - << 'PYEOF'
import re, subprocess

with open('/home/gaurav/.config/plasma-org.kde.plasma.desktop-appletsrc', 'r') as f:
    content = f.read()

# Find containments that are panels
containments = re.findall(r'\[Containments\]\[(\d+)\]', content)
for cid in containments:
    # check if this is the bottom panel
    res = subprocess.run(['kreadconfig6', '--file', 'plasma-org.kde.plasma.desktop-appletsrc', '--group', 'Containments', '--group', cid, '--key', 'location'], capture_output=True, text=True)
    if res.stdout.strip() == '4' or res.stdout.strip() == 'bottom': # 4 = bottom in Plasma
        subprocess.run(['kwriteconfig6', '--file', 'plasma-org.kde.plasma.desktop-appletsrc', '--group', 'Containments', '--group', cid, '--key', 'panelOpacity', '2'])
        subprocess.run(['kwriteconfig6', '--file', 'plasma-org.kde.plasma.desktop-appletsrc', '--group', 'Containments', '--group', cid, '--key', 'opacity', '2'])
        subprocess.run(['kwriteconfig6', '--file', 'plasma-org.kde.plasma.desktop-appletsrc', '--group', 'Containments', '--group', cid, '--key', 'backgroundHints', '0'])
        subprocess.run(['kwriteconfig6', '--file', 'plasma-org.kde.plasma.desktop-appletsrc', '--group', 'Containments', '--group', cid, '--group', 'General', '--key', 'panelOpacity', '2'])
        print(f"Applied 100% transparency to bottom panel containment {cid}")

PYEOF

systemctl --user restart plasma-plasmashell.service 2>/dev/null || (kquitapp6 plasmashell 2>/dev/null && kstart plasmashell 2>/dev/null)

echo "Done! The macOS Dock panel is 100% transparent and centered."
