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

    // 120Hz/60Hz Animation Frame Timer for Spring Physics
    FrameAnimation {
        id: physicsTicker
        running: true

        onTriggered: {
            var dtSec = (frameTime > 0.001 && frameTime < 0.05) ? frameTime : 0.016;
            // Normalize delta_time to ~1.0 at 60fps (exact macos-web ODE solver time scale)
            var delta_time = Math.min(dtSec * 1000.0, 42.0) * 0.06;
            if (delta_time <= 0.01) delta_time = 1.0;

            var count = itemsRepeater.count;
            if (count === 0) return;

            for (var i = 0; i < count; ++i) {
                var delegate = itemsRepeater.itemAt(i);
                if (!delegate) continue;
                var dockItem = delegate.dockItemInstance;
                if (!dockItem) continue;

                var targetW = root.baseWidth;

                if (root.dockMouseX !== null) {
                    var itemCenterPt = dockItem.mapToItem(null, dockItem.width / 2, 0);
                    var dist = Math.abs(root.dockMouseX - itemCenterPt.x);
                    targetW = root.calcTargetWidth(dist);
                }

                // Tick ODE Spring with exact Svelte 5 values
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

    onWidthChanged: updateMaskTimer.restart()
    Component.onCompleted: updateMaskTimer.restart()

    // Extra padding on sides so magnified edge icons aren't clipped
    width: dockPill.width + 200
    height: 220

    // Outer Ambient Shadow
    Rectangle {
        id: shadowGlow
        anchors.centerIn: dockPill
        anchors.verticalCenterOffset: 3
        width: dockPill.width + 10
        height: dockPill.height + 6
        radius: 20
        color: Qt.rgba(0, 0, 0, 0.32)
        z: 0
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
        z: 1
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

        // Full Interactive Mouse Hover Zone
        MouseArea {
            id: containerMouseArea
            anchors.fill: parent
            anchors.topMargin: -140
            hoverEnabled: true
            acceptedButtons: Qt.LeftButton | Qt.RightButton
            propagateComposedEvents: true

            onPositionChanged: (mouse) => {
                var pt = mapToItem(null, mouse.x, mouse.y);
                root.dockMouseX = pt.x;
                root.isMouseInside = true;
            }

            onExited: {
                root.dockMouseX = null;
                root.isMouseInside = false;
            }

            onClicked: (mouse) => {
                if (mouse.button === Qt.RightButton) {
                    pillContextMenu.popup();
                }
            }
        }

        // Drag & Drop Area: Drop files, .desktop apps, or URLs onto the dock to add them!
        DropArea {
            anchors.fill: parent
            z: 0
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

        MenuSeparator {}

        MenuItem {
            text: "Quit macOS Dock"
            onTriggered: dockManager.quitDock()
        }
    }
}
