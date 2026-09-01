import QtQuick
import QtQuick.Controls

Item {
    id: root

    required property var appData
    property int itemIndex: 0
    property var dockContainerRef: null
    
    // Exact Svelte spring values
    property real lastWidth: 57.6
    property real currentWidth: 57.6
    property bool isHovered: mouseArea.containsMouse
    property bool isDragging: false
    property bool isMarkedForRemoval: false

    width: currentWidth
    height: currentWidth
    clip: false  // Never clip magnified icons

    // ── macOS Dock Bounce Animation ──
    SequentialAnimation {
        id: bounceAnim
        running: false
        loops: 3

        NumberAnimation {
            target: iconVisual
            property: "y"
            from: 0
            to: -35
            duration: 200
            easing.type: Easing.OutQuad
        }
        NumberAnimation {
            target: iconVisual
            property: "y"
            from: -35
            to: 0
            duration: 250
            easing.type: Easing.OutBounce
        }
        PauseAnimation { duration: 80 }
    }

    function triggerBounce() {
        bounceAnim.restart();
    }

    // App Tooltip (Frosted glass pill hovering directly above icon)
    Tooltip {
        id: tooltip
        anchors.horizontalCenter: parent.horizontalCenter
        text: root.isMarkedForRemoval ? "Remove from Dock" : (appData ? appData.title : "")
        visibleTooltip: root.isHovered || root.isDragging
        z: 300
    }

    // Icon Container
    Item {
        id: iconVisual
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: parent.height
        clip: false
        opacity: root.isMarkedForRemoval ? 0.45 : (root.isDragging ? 0.8 : 1.0)
        scale: root.isMarkedForRemoval ? 0.8 : 1.0

        Behavior on opacity { NumberAnimation { duration: 150 } }
        Behavior on scale { NumberAnimation { duration: 150 } }

        Image {
            id: iconImage
            anchors.fill: parent
            source: appData ? appData.icon : ""
            fillMode: Image.PreserveAspectFit
            smooth: true
            mipmap: true
            asynchronous: true
        }

        // Notification Badge (Red circular pill)
        Rectangle {
            id: badge
            visible: appData ? (appData.badgeCount > 0) : false
            width: Math.max(18, badgeText.implicitWidth + 8)
            height: 18
            radius: 9
            color: "#FF3B30"
            anchors.top: parent.top
            anchors.right: parent.right
            anchors.topMargin: -2
            anchors.rightMargin: -2
            border.color: "#FFFFFF"
            border.width: 1

            Text {
                id: badgeText
                anchors.centerIn: parent
                text: appData ? appData.badgeCount.toString() : ""
                color: "#FFFFFF"
                font.pixelSize: 11
                font.weight: Font.Bold
            }
        }
    }

    // Running Indicator Dot (4px circular dot placed below the icon)
    Rectangle {
        id: runningDot
        width: 4
        height: 4
        radius: 2
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.top: parent.bottom
        anchors.topMargin: 2
        color: dockManager.isDarkTheme ? Qt.rgba(1, 1, 1, 0.85) : Qt.rgba(0.1, 0.1, 0.1, 0.85)
        opacity: (appData && appData.isRunning && !root.isDragging) ? 1.0 : 0.0

        Behavior on opacity {
            NumberAnimation { duration: 250; easing.type: Easing.OutQuad }
        }
    }

    // Mouse Interaction & Drag-to-Rearrange / Remove
    MouseArea {
        id: mouseArea
        anchors.fill: parent
        hoverEnabled: true
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        cursorShape: root.isDragging ? Qt.ClosedHandCursor : Qt.PointingHandCursor

        property real pressX: 0
        property real pressY: 0
        property bool dragTriggered: false

        onPressed: (mouse) => {
            if (mouse.button === Qt.LeftButton) {
                pressX = mouse.x;
                pressY = mouse.y;
                dragTriggered = false;
            }
        }

        onPositionChanged: (mouse) => {
            if (dockContainerRef) {
                var pt = mapToItem(null, mouse.x, mouse.y);
                dockContainerRef.dockMouseX = pt.x;
                dockContainerRef.isMouseInside = true;
            }

            if (pressedButtons & Qt.LeftButton) {
                var dx = mouse.x - pressX;
                var dy = mouse.y - pressY;

                if (!dragTriggered && (Math.abs(dx) > 10 || Math.abs(dy) > 10)) {
                    dragTriggered = true;
                    root.isDragging = true;
                }

                if (dragTriggered) {
                    root.isMarkedForRemoval = (mouse.y < -70);

                    if (!root.isMarkedForRemoval) {
                        if (dx > root.width * 0.75) {
                            dockManager.moveApp(root.itemIndex, root.itemIndex + 1);
                            pressX = mouse.x;
                        } else if (dx < -root.width * 0.75) {
                            dockManager.moveApp(root.itemIndex, root.itemIndex - 1);
                            pressX = mouse.x;
                        }
                    }
                }
            }
        }

        onReleased: (mouse) => {
            if (mouse.button === Qt.LeftButton) {
                if (dragTriggered) {
                    if (root.isMarkedForRemoval) {
                        if (appData) dockManager.removeAppById(appData.id);
                    }
                    root.isDragging = false;
                    root.isMarkedForRemoval = false;
                    dragTriggered = false;
                } else {
                    triggerBounce();
                    if (appData) {
                        dockManager.launchOrToggleApp(appData.id);
                    }
                }
            }
        }

        onCanceled: {
            root.isDragging = false;
            root.isMarkedForRemoval = false;
            dragTriggered = false;
        }

        onClicked: (mouse) => {
            if (mouse.button === Qt.RightButton) {
                contextMenu.popup();
            }
        }
    }

    // macOS Context Menu
    Menu {
        id: contextMenu

        onOpened: dockManager.setIsMenuOpen(true)
        onClosed: dockManager.setIsMenuOpen(false)

        MenuItem {
            text: (appData && appData.isRunning) ? ("Show " + appData.title) : ("Open " + (appData ? appData.title : ""))
            onTriggered: {
                triggerBounce();
                if (appData) dockManager.launchOrToggleApp(appData.id);
            }
        }

        MenuItem {
            visible: appData ? appData.isRunning : false
            text: "New Window"
            onTriggered: {
                triggerBounce();
                if (appData) dockManager.launchNewInstance(appData.id);
            }
        }

        // Multi-Window Submenu when app has multiple windows open
        Menu {
            title: "Open Windows (" + (appData ? appData.windowCount : 0) + ")"
            visible: appData ? (appData.windowCount > 1) : false

            Instantiator {
                model: (appData && appData.windows) ? appData.windows : []
                delegate: MenuItem {
                    text: (modelData.active ? "● " : "  ") + (modelData.title ? (modelData.title.length > 35 ? modelData.title.substring(0, 32) + "..." : modelData.title) : "Window")
                    onTriggered: {
                        dockManager.activateWindow(modelData.id);
                    }
                }
                onObjectAdded: (index, object) => parent.insertItem(index, object)
                onObjectRemoved: (index, object) => parent.removeItem(object)
            }
        }

        MenuItem {
            visible: appData ? appData.isRunning : false
            text: "Minimize"
            onTriggered: {
                if (appData) dockManager.minimizeApp(appData.id);
            }
        }

        MenuItem {
            visible: appData ? appData.isRunning : false
            text: "Quit " + (appData ? appData.title : "")
            onTriggered: {
                if (appData) dockManager.closeApp(appData.id);
            }
        }

        MenuSeparator {}

        Menu {
            title: "Options"

            // Keep in Dock (for dynamic unpinned running apps)
            MenuItem {
                visible: appData ? !appData.isPinned : false
                text: "Keep in Dock"
                onTriggered: {
                    if (appData) dockManager.pinApp(appData.id);
                }
            }

            // Remove from Dock (for pinned apps)
            MenuItem {
                visible: appData ? appData.isPinned : true
                text: "Remove from Dock"
                onTriggered: {
                    if (appData) dockManager.removeAppById(appData.id);
                }
            }

            MenuItem {
                text: "Move Left"
                enabled: root.itemIndex > 0
                onTriggered: {
                    dockManager.moveApp(root.itemIndex, root.itemIndex - 1);
                }
            }

            MenuItem {
                text: "Move Right"
                enabled: root.itemIndex < (dockManager.apps.length - 1)
                onTriggered: {
                    dockManager.moveApp(root.itemIndex, root.itemIndex + 1);
                }
            }

            MenuItem {
                text: (appData && appData.dockBreaksBefore) ? "Remove Divider Before" : "Add Divider Before"
                onTriggered: {
                    if (appData) dockManager.toggleDividerBefore(appData.id);
                }
            }
        }

        MenuSeparator {}

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
