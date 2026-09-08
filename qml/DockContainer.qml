import QtQuick
import QtQuick.Controls

Item {
    id: root

    property var dockMouseX: null // null when mouse is outside the dock
    property bool isMouseInside: false

    readonly property real baseWidth: dockManager.baseIconWidth           // 57.6
    readonly property real maxMagnification: dockManager.maxMagnification // 2.0
    readonly property real distanceLimit: baseWidth * 6.0                 // 345.6
    readonly property real beyondLimit: distanceLimit + 1.0               // 346.6

    // Allow magnified icons to overflow without clipping
    clip: false

    function requestMaskUpdate() {
        updateMaskTimer.restart();
    }

    // Exact Svelte 5 tick_spring ODE solver from svelte/src/motion/spring.js (normalized to delta_time ~ 1.0)
    function tickSpring(delta_time, last_val, cur_val, target_val, stiffness, damping, precision) {
        var delta = target_val - cur_val;
        var velocity = (cur_val - last_val) / delta_time;
        var springForce = stiffness * delta;
        var damperForce = damping * velocity;
        var acceleration = springForce - damperForce; // inv_mass = 1
        var d = (velocity + acceleration) * delta_time;
        if (Math.abs(d) < precision && Math.abs(delta) < precision) {
            return target_val; // Settled cleanly
        } else {
            return cur_val + d;
        }
    }

    // Popmotion piecewise magnification curve from macos-web
    function calcTargetWidth(distance) {
        if (distance > root.distanceLimit) {
            return root.baseWidth;
        }
        var norm = distance / root.distanceLimit;
        var scale;
        if (norm < 0.1667) {
            scale = root.maxMagnification;
        } else if (norm < 0.3333) {
            var t = (norm - 0.1667) / 0.1666;
            scale = root.maxMagnification - t * (root.maxMagnification - 1.833);
        } else if (norm < 0.5) {
            var t = (norm - 0.3333) / 0.1667;
            scale = 1.833 - t * (1.833 - 1.5);
        } else if (norm < 0.6667) {
            var t = (norm - 0.5) / 0.1667;
            scale = 1.5 - t * (1.5 - 1.167);
        } else if (norm < 0.8333) {
            var t = (norm - 0.6667) / 0.1666;
            scale = 1.167 - t * (1.167 - 1.033);
        } else {
            var t = (norm - 0.8333) / 0.1667;
            scale = 1.033 - t * (1.033 - 1.0);
        }
        return root.baseWidth * scale;
    }

    // Calculate static resting reference centers for all icons.
    // In genuine macOS dock physics, distance is measured relative to the resting shelf,
    // so icon width expansion NEVER creates feedback jitter or spatial oscillation.
    function getBaseReferenceCenters() {
        var count = itemsRepeater.count;
        var centers = [];
        var totalBaseW = 0;
        var widths = [];

        for (var i = 0; i < count; ++i) {
            var delegate = itemsRepeater.itemAt(i);
            var hasDivider = (delegate && delegate.modelData && delegate.modelData.dockBreaksBefore);
            var itemW = root.baseWidth + (hasDivider ? 19 : 0);
            widths.push({ w: itemW, dividerOffset: hasDivider ? 19 : 0 });
            totalBaseW += itemW;
            if (i < count - 1) totalBaseW += 5; // spacing
        }

        // Horizontal position of itemsRow inside root (DockContainer)
        var startX = (root.width - totalBaseW) / 2;
        var currentX = startX;

        for (var j = 0; j < count; ++j) {
            var itemInfo = widths[j];
            var center = currentX + itemInfo.dividerOffset + (root.baseWidth / 2);
            centers.push(center);
            currentX += itemInfo.w + 5;
        }

        return centers;
    }

    // Update stable magnification targets
    function updateTargets() {
        var count = itemsRepeater.count;
        if (count === 0) return;

        if (root.dockMouseX === null) {
            for (var i = 0; i < count; ++i) {
                var del = itemsRepeater.itemAt(i);
                if (del && del.dockItemInstance) {
                    del.dockItemInstance.targetWidth = root.baseWidth;
                }
            }
            return;
        }

        // Map mouseX relative to root coordinate space
        var mouseInRoot = root.dockMouseX - (root.mapToItem(null, 0, 0).x);
        var centers = getBaseReferenceCenters();

        for (var k = 0; k < count; ++k) {
            var delegate = itemsRepeater.itemAt(k);
            if (!delegate) continue;
            var dockItem = delegate.dockItemInstance;
            if (!dockItem) continue;

            var refX = centers[k];
            var dist = Math.abs(mouseInRoot - refX);
            dockItem.targetWidth = root.calcTargetWidth(dist);
        }
    }

    onDockMouseXChanged: updateTargets()

    // 120Hz/60Hz Animation Frame Timer for Spring Physics
    FrameAnimation {
        id: physicsTicker
        running: true

        onTriggered: {
            var dtSec = (frameTime > 0.001 && frameTime < 0.05) ? frameTime : 0.016;
            var delta_time = Math.min(dtSec * 1000.0, 42.0) * 0.06;
            if (delta_time <= 0.01) delta_time = 1.0;

            var count = itemsRepeater.count;
            if (count === 0) return;

            for (var i = 0; i < count; ++i) {
                var delegate = itemsRepeater.itemAt(i);
                if (!delegate) continue;
                var dockItem = delegate.dockItemInstance;
                if (!dockItem) continue;

                var targetW = dockItem.targetWidth;

                // Tick ODE Spring smoothly towards invariant static target
                var nextW = root.tickSpring(delta_time, dockItem.lastWidth, dockItem.currentWidth, targetW, 0.12, 0.47, 0.01);

                dockItem.lastWidth = dockItem.currentWidth;
                dockItem.currentWidth = nextW;
                dockItem.width = nextW;
            }
        }
    }

    // Dynamic Input Mask Updating (Restricts pointer capture to ONLY the dock pill!)
    Timer {
        id: updateMaskTimer
        interval: 100
        running: false
        repeat: false
        onTriggered: forceMaskUpdate()
    }

    function forceMaskUpdate() {
        updateMaskTimer.stop();
        var pt = dockPill.mapToItem(null, 0, 0);
        dockManager.updateMask(pt.x, pt.y, dockPill.width, dockPill.height);
    }

    onWidthChanged: {
        updateMaskTimer.restart();
        updateTargets();
    }
    Component.onCompleted: {
        updateMaskTimer.restart();
        updateTargets();
    }

    // Extra padding on sides so magnified edge icons aren't clipped
    width: dockPill.width + 200
    height: 220

    // Master Full-Width Hover Zone: Constant 220px height, immune to icon resizing
    MouseArea {
        id: containerMouseArea
        anchors.fill: parent
        hoverEnabled: true
        acceptedButtons: Qt.RightButton
        propagateComposedEvents: true
        z: 0

        onPositionChanged: (mouse) => {
            var pt = mapToItem(null, mouse.x, mouse.y);
            root.dockMouseX = pt.x;
            root.isMouseInside = true;
            root.updateTargets();
        }

        onExited: {
            root.dockMouseX = null;
            root.isMouseInside = false;
            root.updateTargets();
        }

        onClicked: (mouse) => {
            if (mouse.button === Qt.RightButton) {
                pillContextMenu.popup();
            }
        }
    }

    // Outer Ambient Shadow
    Rectangle {
        id: shadowGlow
        anchors.centerIn: dockPill
        anchors.verticalCenterOffset: 3
        width: dockPill.width + 10
        height: dockPill.height + 6
        radius: 20
        color: Qt.rgba(0, 0, 0, 0.32)
        z: 1
    }

    // Frosted Glass Dock Container Pill
    Rectangle {
        id: dockPill
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 6
        anchors.horizontalCenter: parent.horizontalCenter
        width: itemsRow.width + 18
        height: 68
        radius: 18
        z: 2
        clip: false  // Don't clip magnified icons!

        color: dockManager.isDarkTheme 
            ? Qt.rgba(0.12, 0.12, 0.15, 0.60) 
            : Qt.rgba(0.93, 0.93, 0.96, 0.68)

        border.color: dockManager.isDarkTheme 
            ? Qt.rgba(1, 1, 1, 0.22) 
            : Qt.rgba(0, 0, 0, 0.14)
        border.width: 0.8

        // Top Specular Highlight Line
        Rectangle {
            anchors.top: parent.top
            anchors.topMargin: 1
            anchors.horizontalCenter: parent.horizontalCenter
            width: parent.width - 24
            height: 1
            radius: 0.5
            color: dockManager.isDarkTheme ? Qt.rgba(1, 1, 1, 0.25) : Qt.rgba(1, 1, 1, 0.65)
        }

        // Drag & Drop Area: Drop files, .desktop apps, or URLs onto the dock to add them!
        DropArea {
            anchors.fill: parent
            z: 3
            onEntered: (drag) => {
                root.isMouseInside = true;
            }
            onDropped: (drop) => {
                if (drop.hasUrls) {
                    dockManager.addAppsFromUrls(drop.urls);
                    drop.acceptProposedAction();
                } else if (drop.hasText) {
                    dockManager.addAppFromText(drop.text);
                    drop.acceptProposedAction();
                }
            }
        }

        // Row of Dock Items (Bottom-anchored to dock shelf)
        Row {
            id: itemsRow
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 6
            anchors.horizontalCenter: parent.horizontalCenter
            spacing: 5
            z: 10
            clip: false  // Don't clip magnified icons!
            onWidthChanged: updateMaskTimer.restart()

            Repeater {
                id: itemsRepeater
                model: dockManager.apps

                delegate: Row {
                    id: delegateRow
                    spacing: 5
                    anchors.bottom: parent ? parent.bottom : undefined
                    clip: false  // Don't clip magnified icons!

                    property alias dockItemInstance: dockItem

                    // Divider before item if configured
                    DockDivider {
                        visible: modelData.dockBreaksBefore
                        anchors.bottom: parent.bottom
                    }

                    // Dock Item
                    DockItem {
                        id: dockItem
                        appData: modelData
                        itemIndex: index
                        dockContainerRef: root
                        anchors.bottom: parent.bottom
                    }
                }
            }
        }
    }

    // Context Menu for Pill Background
    Menu {
        id: pillContextMenu

        onOpened: dockManager.setIsMenuOpen(true)
        onClosed: dockManager.setIsMenuOpen(false)

        MenuItem {
            text: dockManager.isDarkTheme ? "Switch to Light Theme" : "Switch to Dark Theme"
            onTriggered: dockManager.isDarkTheme = !dockManager.isDarkTheme
        }

        MenuItem {
            text: "Reset All Apps to Default"
            onTriggered: dockManager.resetToDefaultApps()
        }

        MenuItem {
            text: (dockManager.isAutostartEnabled ? "✓ " : "   ") + "Open at Login"
            onTriggered: {
                dockManager.toggleAutostart();
            }
        }

        MenuSeparator {}

        MenuItem {
            text: "Quit macOS Dock"
            onTriggered: dockManager.quitDock()
        }
    }
}
