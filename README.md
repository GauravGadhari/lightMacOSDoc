# lightMacOSDoc

A high-performance native macOS Dock for Linux (Wayland / KDE Plasma / X11) built with C++20, Qt 6.9, and QML. Features the exact **Popmotion xzoom magnification physics**, Svelte 5 ODE spring dynamics, bounce animations, dynamic auto-hide with instant pass-through, drag-and-drop customization, and frosted glass aesthetics.

---

## Features

- **Exact macOS xzoom Physics**: Replicates Popmotion's piecewise interpolation and second-order spring dynamics (`stiffness: 0.12`, `damping: 0.47`, 120Hz/60Hz frame-synchronized).
- **Auto-Hide & Reveal with Click-Through**: Window moves gracefully and exposes a 6px bottom trigger strip with full click-through to underlying application windows.
- **Drag & Drop Customization**:
  - Drag icons left/right to rearrange in real-time.
  - Drop `.desktop` applications, binaries, or URLs directly onto the dock.
  - Drag upwards to remove an app from the dock.
  - Right-click Options menu for moving, removing, and toggling dividers.
- **Persistent Configuration**: Saved to `~/.config/macos-dock/apps.json`.
- **Authentic macOS Big Sur / Sonoma Icons**: High-res 256px assets.
- **Linux Desktop Integration**:
  - Finder $\to$ Dolphin / Nautilus
  - Safari $\to$ Default Web Browser
  - Messages $\to$ WhatsApp Web
  - Terminal $\to$ Konsole / GNOME Terminal / Alacritty / Kitty
  - VS Code $\to$ `code`
  - Antigravity $\to$ Antigravity AI IDE
  - System Preferences $\to$ KDE `systemsettings`
  - Calculator $\to$ `kcalc` / `gnome-calculator`
  - GitHub $\to$ Gaurav Gadhari GitHub Profile
- **Dynamic Running Indicators**: Process scanning to render active dot underneath open applications.
- **Click Bounce Animation**: 3-loop vertical impulse bouncing while applications launch.
- **Frosted Glass Pill**: Translucent blur backdrop, specular top highlight, and soft ambient drop shadows.

---

## Build & Run

### Prerequisites
- Qt 6.5+ (Core, Gui, Quick, Qml, QuickControls2, DBus)
- CMake 3.16+
- C++20 Compiler (GCC / Clang)

### Build
```bash
cmake -B build
cmake --build build
```

### Run
```bash
./run_dock.sh
```
