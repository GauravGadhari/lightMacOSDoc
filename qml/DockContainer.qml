import QtQuick
import QtQuick.Controls

Item {
    id: root

    property var dockMouseX: null // null when mouse is outside the dock
    property bool isMouseInside: false
    property bool isReady: false  // becomes true after initial items populate (no morph-in on startup)
    property alias itemsRowRef: itemsRow

    readonly property real baseWidth: dockManager.baseIconWidth           // 57.6
    readonly property real maxMagnification: dockManager.maxMagnification // 2.0
    readonly property real distanceLimit: baseWidth * 6.0                 // 345.6
    readonly property real beyondLimit: distanceLimit + 1.0               // 346.6

    // Allow magnified icons to overflow without clipping
    clip: false

    function requestMaskUpdate() {
        updateMaskTimer.restart();
    }

    // ────────────────────────────────────────────────────────────────
    // Svelte 5 tick_spring ODE solver (spring.js)
    // ────────────────────────────────────────────────────────────────
    function tickSpring(dt, last_val, cur_val, target_val, stiffness, damping, precision) {
        var delta = target_val - cur_val;
        var velocity = (cur_val - last_val) / dt;
        var d = (velocity + (stiffness * delta - damping * velocity)) * dt;
        if (Math.abs(d) < precision && Math.abs(delta) < precision) {
            return target_val;
        }
        return cur_val + d;
    }

    // ────────────────────────────────────────────────────────────────
    // Popmotion interpolate() magnification curve from macos-web
    //
    // distanceInput:  [-limit, -limit/1.25, -limit/2, 0, limit/2, limit/1.25, limit]
    // widthOutput:    [base,   base*1.1,    base*1.414, base*2, base*1.414, base*1.1, base]
    //
    // We use absolute distance so only the right half matters:
    //   [0            → base*2.0  ]
    //   [limit/2      → base*1.414]
    //   [limit/1.25   → base*1.1  ]
    //   [limit        → base*1.0  ]
    // ────────────────────────────────────────────────────────────────
    function calcTargetWidth(absDist) {
        var L = root.distanceLimit;
        if (absDist >= L) return root.baseWidth;

        var b = root.baseWidth;
        // Keypoints (distance → scale factor)
        var k0 = 0;              var s0 = 2.0;
        var k1 = L / 2;          var s1 = 1.414;
        var k2 = L / 1.25;       var s2 = 1.1;
        var k3 = L;              var s3 = 1.0;

        var scale;
        if (absDist <= k1) {
            var t = absDist / k1;
            scale = s0 + t * (s1 - s0);    // 2.0 → 1.414
        } else if (absDist <= k2) {
            var t = (absDist - k1) / (k2 - k1);
            scale = s1 + t * (s2 - s1);    // 1.414 → 1.1
        } else {
            var t = (absDist - k2) / (k3 - k2);
            scale = s2 + t * (s3 - s2);    // 1.1 → 1.0
        }
        return b * scale;
    }

    // ────────────────────────────────────────────────────────────────
    // 120Hz/60Hz Physics Ticker
    // ────────────────────────────────────────────────────────────────
    FrameAnimation {
        id: physicsTicker
        running: true

        onTriggered: {
            var dtSec = (frameTime > 0.001 && frameTime < 0.05) ? frameTime : 0.016;
            var delta_time = Math.min(dtSec * 1000.0, 42.0) * 0.06;
            if (delta_time <= 0.01) delta_time = 1.0;

            var count = itemsRepeater.count;
            if (count === 0) return;

            var needsLayout = false;

            for (var i = 0; i < count; ++i) {
                var delegate = itemsRepeater.itemAt(i);
                if (!delegate) continue;
                var dockItem = delegate.dockItemInstance;
                if (!dockItem) continue;

                // ── LIVE distance, exactly like Svelte's getBoundingClientRect() ──
                var targetW = root.baseWidth;
                if (root.dockMouseX !== null && !dockItem.appData.isRemoving) {
                    // Use the IMAGE center (live), same as Svelte's image_el.getBoundingClientRect()
                    var imgCenter = dockItem.mapToItem(null, dockItem.width / 2, 0);
                    var dist = Math.abs(root.dockMouseX - imgCenter.x);
                    targetW = root.calcTargetWidth(dist);
                }

                var nextW = root.tickSpring(delta_time, dockItem.lastWidth, dockItem.currentWidth, targetW, 0.12, 0.47, 0.01);
                if (Math.abs(dockItem.currentWidth - nextW) > 0.005) {
                    dockItem.lastWidth = dockItem.currentWidth;
                    dockItem.currentWidth = nextW;
                    needsLayout = true;
                }
            }

            // Force Row to reposition siblings when widths change
            if (needsLayout) {
                itemsRow.forceLayout();
            }
        }
    }

    // ────────────────────────────────────────────────────────────────
    // Mask Timer
    // ────────────────────────────────────────────────────────────────
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
    Component.onCompleted: {
        updateMaskTimer.restart();
        // Delay isReady so initial dock items don't morph-in on startup
        readyDelayTimer.start();
    }

    Timer {
        id: readyDelayTimer
        interval: 200
        repeat: false
        onTriggered: root.isReady = true
    }

    // Extra padding on sides so magnified edge icons aren't clipped
    width: dockPill.width + 200
    height: 220

    // ────────────────────────────────────────────────────────────────
    // LAYER 1: Full-Width Magnification Tracker (entire 220px height)
    //
    // This ONLY tracks dockMouseX for the magnification spring.
    // It does NOT control isMouseInside (auto-hide).
    // ────────────────────────────────────────────────────────────────
    MouseArea {
        id: magnificationTracker
        anchors.fill: parent
        hoverEnabled: true
        acceptedButtons: Qt.NoButton
        propagateComposedEvents: true
        z: 0

        onPositionChanged: (mouse) => {
            var pt = mapToItem(null, mouse.x, mouse.y);
            root.dockMouseX = pt.x;
        }

        onExited: {
            root.dockMouseX = null;
        }
    }

    // ────────────────────────────────────────────────────────────────
    // LAYER 2: Dock Pill Hover Zone (pill + magnified icon overflow)
    //
    // This controls isMouseInside for auto-hide.
    // Height = pill(68) + max magnified overflow(~60) + padding(22) ≈ 150px
    // Anchored to the bottom, so it covers the pill and magnified icons
    // but NOT the empty space above — just like Svelte's dock-el.
    // ────────────────────────────────────────────────────────────────
    MouseArea {
        id: pillHoverZone
        anchors.bottom: parent.bottom
        anchors.left: dockPill.left
        anchors.right: dockPill.right
        anchors.leftMargin: -20
        anchors.rightMargin: -20
        height: 150
        hoverEnabled: true
        acceptedButtons: Qt.RightButton
        propagateComposedEvents: true
        z: 1

        onEntered: {
            root.isMouseInside = true;
        }

        onExited: {
            root.isMouseInside = false;
        }

        onPositionChanged: (mouse) => {
            var pt = mapToItem(null, mouse.x, mouse.y);
            root.dockMouseX = pt.x;
            root.isMouseInside = true;
        }

        onClicked: (mouse) => {
            if (mouse.button === Qt.RightButton) {
                dockManager.dismissAllMenus();
                var pt = mapToItem(root, mouse.x, mouse.y);
                pillContextMenu.popup(pt.x, pt.y);
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
        clip: false

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

        // Drag & Drop Area
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

        // Row of Dock Items
        Row {
            id: itemsRow
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 6
            anchors.horizontalCenter: parent.horizontalCenter
            spacing: 5
            z: 10
            clip: false
            onWidthChanged: updateMaskTimer.restart()

            Repeater {
                id: itemsRepeater
                model: dockManager.appsModel

                delegate: Row {
                    id: delegateRow
                    spacing: 5
                    anchors.bottom: parent ? parent.bottom : undefined
                    clip: false

                    property alias dockItemInstance: dockItem

                    width: (dockDivider.visible ? (dockDivider.width + spacing) * dockItem.morphProgress : 0) + dockItem.width

                    DockDivider {
                        id: dockDivider
                        visible: modelData.dockBreaksBefore && dockItem.morphProgress > 0.01
                        anchors.bottom: parent.bottom
                        opacity: dockItem.morphProgress
                    }

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

    Connections {
        target: dockManager
        function onDismissPopupsRequested() {
            if (pillContextMenu.visible) {
                pillContextMenu.close();
            }
        }
    }

    // ── Apple-grade Native Context Menu Components ──
    component MacMenuItem: MenuItem {
        id: itemControl
        implicitWidth: 220
        implicitHeight: visible ? 26 : 0

        contentItem: Item {
            anchors.fill: parent
            visible: itemControl.visible

            Text {
                anchors.left: parent.left
                anchors.leftMargin: 10
                anchors.right: (itemControl.subMenu !== null) ? arrowText.left : parent.right
                anchors.rightMargin: (itemControl.subMenu !== null) ? 4 : 10
                anchors.verticalCenter: parent.verticalCenter
                text: itemControl.text
                font.pixelSize: 13
                font.family: "system-ui, -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif"
                color: !itemControl.enabled 
                    ? (dockManager.isDarkTheme ? Qt.rgba(1, 1, 1, 0.35) : Qt.rgba(0, 0, 0, 0.32))
                    : (itemControl.highlighted ? "#FFFFFF" : (dockManager.isDarkTheme ? "#ECECED" : "#1D1D1F"))
                verticalAlignment: Text.AlignVCenter
                elide: Text.ElideRight
            }

            Text {
                id: arrowText
                visible: itemControl.subMenu !== null
                anchors.right: parent.right
                anchors.rightMargin: 10
                anchors.verticalCenter: parent.verticalCenter
                text: "›"
                font.pixelSize: 16
                font.bold: true
                color: itemControl.highlighted ? "#FFFFFF" : (dockManager.isDarkTheme ? Qt.rgba(1, 1, 1, 0.5) : Qt.rgba(0, 0, 0, 0.4))
            }
        }

        background: Rectangle {
            visible: itemControl.visible && itemControl.highlighted
            radius: 5
            color: dockManager.isDarkTheme ? "#0A84FF" : "#007AFF"
            anchors.fill: parent
            anchors.leftMargin: 4
            anchors.rightMargin: 4
        }
    }

    component MacMenuSeparator: MenuSeparator {
        id: sepControl
        implicitWidth: 220
        implicitHeight: visible ? 7 : 0
        topPadding: visible ? 3 : 0
        bottomPadding: visible ? 3 : 0
        leftPadding: 10
        rightPadding: 10

        contentItem: Rectangle {
            visible: sepControl.visible
            implicitHeight: visible ? 1 : 0
            height: visible ? 1 : 0
            color: dockManager.isDarkTheme ? Qt.rgba(1, 1, 1, 0.15) : Qt.rgba(0, 0, 0, 0.12)
        }

        background: Item {
            visible: false
        }
    }

    component MacMenu: Menu {
        id: menuControl
        popupType: Popup.Window
        delegate: MacMenuItem {}

        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside | Popup.CloseOnPressOutsideParent

        topPadding: 5
        bottomPadding: 5
        leftPadding: 5
        rightPadding: 5

        background: Rectangle {
            implicitWidth: 220
            radius: 8
            color: dockManager.isDarkTheme 
                ? Qt.rgba(0.14, 0.14, 0.16, 0.96) 
                : Qt.rgba(0.96, 0.96, 0.98, 0.96)
            border.color: dockManager.isDarkTheme 
                ? Qt.rgba(1, 1, 1, 0.18) 
                : Qt.rgba(0, 0, 0, 0.15)
            border.width: 1
        }

        contentItem: ListView {
            implicitHeight: contentHeight
            model: menuControl.contentModel
            currentIndex: menuControl.currentIndex
            boundsBehavior: Flickable.StopAtBounds
            clip: true
            ScrollBar.vertical: ScrollBar {
                policy: ScrollBar.AlwaysOff
                visible: false
            }
        }
    }

    // Context Menu for Pill Background
    MacMenu {
        id: pillContextMenu

        onOpened: dockManager.setIsMenuOpen(true)
        onClosed: dockManager.setIsMenuOpen(false)

        MacMenuItem {
            text: dockManager.isDarkTheme ? "Switch to Light Theme" : "Switch to Dark Theme"
            onTriggered: dockManager.isDarkTheme = !dockManager.isDarkTheme
        }

        MacMenuItem {
            text: "Reset All Apps to Default"
            onTriggered: dockManager.resetToDefaultApps()
        }

        MacMenuItem {
            text: (dockManager.isAutostartEnabled ? "✓ " : "   ") + "Open at Login"
            onTriggered: dockManager.toggleAutostart()
        }

        MacMenuSeparator {}

        MacMenuItem {
            text: "Quit macOS Dock"
            onTriggered: dockManager.quitDock()
        }
    }
}
