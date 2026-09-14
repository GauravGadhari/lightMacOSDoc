import QtQuick
import QtQuick.Controls

Item {
    id: root

    required property var appData
    property int itemIndex: 0
    property var dockContainerRef: null
    
    // Spring state (driven by DockContainer's physicsTicker)
    property real lastWidth: 57.6
    property real currentWidth: 57.6

    // Hovered = dockMouseX is within this item's horizontal span
    property bool isHovered: {
        if (!dockContainerRef || dockContainerRef.dockMouseX === null) return false;
        var center = mapToItem(null, width / 2, 0).x;
        return Math.abs(dockContainerRef.dockMouseX - center) < (width / 2);
    }
    property bool isDragging: false
    property bool isMarkedForRemoval: false

    width: currentWidth
    height: currentWidth
    clip: false

    property bool isLaunching: false

    // ── Single jump (loops: 1) for switching / focusing an already running app ──
    SequentialAnimation {
        id: singleJumpAnim
        running: false
        loops: 1

        NumberAnimation {
            target: bounceTranslate
            property: "y"
            from: 0; to: -26; duration: 150
            easing.type: Easing.OutQuad
        }
        NumberAnimation {
            target: bounceTranslate
            property: "y"
            from: -26; to: 0; duration: 160
            easing.type: Easing.InQuad
        }
    }

    // ── Continuous jump (loops: Animation.Infinite) while opening an app until window appears ──
    SequentialAnimation {
        id: continuousJumpAnim
        running: false
        loops: Animation.Infinite

        NumberAnimation {
            target: bounceTranslate
            property: "y"
            from: 0; to: -34; duration: 180
            easing.type: Easing.OutQuad
        }
        NumberAnimation {
            target: bounceTranslate
            property: "y"
            from: -34; to: 0; duration: 180
            easing.type: Easing.InQuad
        }
        PauseAnimation { duration: 40 }
    }

    // Smooth return to ground when stopping continuous jump
    NumberAnimation {
        id: returnToGroundAnim
        target: bounceTranslate
        property: "y"
        to: 0
        duration: 120
        easing.type: Easing.OutQuad
    }

    function jumpOnce() {
        if (!isLaunching) {
            singleJumpAnim.restart();
        }
    }

    function startContinuousJump() {
        singleJumpAnim.stop();
        isLaunching = true;
        launchTimeoutTimer.restart();
        continuousJumpAnim.restart();
    }

    function stopContinuousJump() {
        launchTimeoutTimer.stop();
        if (isLaunching || continuousJumpAnim.running) {
            isLaunching = false;
            continuousJumpAnim.stop();
            returnToGroundAnim.restart();
        }
    }

    Timer {
        id: launchTimeoutTimer
        interval: 15000 // 15 seconds safety timeout
        repeat: false
        onTriggered: stopContinuousJump()
    }

    Connections {
        target: appData ? appData : null
        function onWindowCountChanged() {
            if (appData && appData.windowCount > 0 && isLaunching) {
                stopContinuousJump();
            }
        }
        function onIsRunningChanged() {
            if (appData && appData.isRunning && isLaunching) {
                stopContinuousJump();
            }
        }
    }

    Connections {
        target: dockManager
        function onAppLaunchStarted(id) {
            if (appData && appData.id === id) {
                startContinuousJump();
            }
        }
        function onAppSwitched(id) {
            if (appData && appData.id === id) {
                jumpOnce();
            }
        }
        function onAppLaunchFinished(id) {
            if (appData && appData.id === id) {
                stopContinuousJump();
            }
        }
    }

    // Tooltip
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

        transform: Translate {
            id: bounceTranslate
            y: 0
        }

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

        // Notification Badge
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

    // Running / Active Indicator Dot
    Rectangle {
        id: runningDot
        property bool isActiveApp: appData ? appData.isActive : false
        width: isActiveApp ? 5 : 4
        height: isActiveApp ? 5 : 4
        radius: width / 2
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.top: parent.bottom
        anchors.topMargin: 2

        color: {
            if (isActiveApp) {
                // Vibrant Apple Blue for the active focused app window
                return dockManager.isDarkTheme ? "#0A84FF" : "#007AFF";
            }
            // Neutral translucent dot for inactive background running apps
            return dockManager.isDarkTheme ? Qt.rgba(1, 1, 1, 0.65) : Qt.rgba(0.2, 0.2, 0.2, 0.65);
        }

        opacity: (appData && appData.isRunning && !root.isDragging) ? 1.0 : 0.0

        Behavior on opacity { NumberAnimation { duration: 250; easing.type: Easing.OutQuad } }
        Behavior on color { ColorAnimation { duration: 200 } }
        Behavior on width { NumberAnimation { duration: 200; easing.type: Easing.OutQuad } }
        Behavior on height { NumberAnimation { duration: 200; easing.type: Easing.OutQuad } }
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
            // Forward mouse X to container for magnification
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
                    if (appData) {
                        if (appData.isRunning && appData.windowCount > 0) {
                            jumpOnce();
                        } else {
                            startContinuousJump();
                        }
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
                dockManager.dismissAllMenus();
                contextMenu.popup();
            }
        }
    }

    function closeAllMenus() {
        if (contextMenu.visible) {
            contextMenu.close();
        }
    }

    Connections {
        target: dockManager
        function onDismissPopupsRequested() {
            closeAllMenus();
        }
    }

    Connections {
        target: root.Window.window ? root.Window.window : null
        function onDockSlideOffsetChanged() {
            if (root.Window.window && root.Window.window.dockSlideOffset > 0) {
                closeAllMenus();
            }
        }
        function onDockVisibleChanged() {
            closeAllMenus();
        }
    }

    // macOS Context Menu
    Menu {
        id: contextMenu

        onOpened: dockManager.isMenuOpen = true
        onClosed: dockManager.isMenuOpen = false

        // 1. Open Windows List
        Repeater {
            model: (appData && appData.windows) ? appData.windows : []
            MenuItem {
                required property var modelData
                text: (modelData.active ? "✓ " : "   ") + (modelData.title ? (modelData.title.length > 35 ? modelData.title.substring(0, 32) + "..." : modelData.title) : "Window")
                onTriggered: {
                    dockManager.dismissAllMenus();
                    dockManager.activateWindow(modelData.id);
                }
            }
        }

        MenuSeparator {
            visible: appData ? (appData.windowCount > 0) : false
        }

        // 2. Open / Show
        MenuItem {
            visible: appData ? (!appData.isRunning || appData.windowCount === 0) : true
            text: (appData && appData.isRunning) ? ("Show " + appData.title) : ("Open " + (appData ? appData.title : ""))
            onTriggered: {
                dockManager.dismissAllMenus();
                if (appData) {
                    if (appData.isRunning && appData.windowCount > 0) {
                        jumpOnce();
                    } else {
                        startContinuousJump();
                    }
                    dockManager.launchOrToggleApp(appData.id);
                }
            }
        }

        // 3. New Window
        MenuItem {
            text: "New Window"
            onTriggered: {
                dockManager.dismissAllMenus();
                if (appData) {
                    startContinuousJump();
                    dockManager.launchNewInstance(appData.id);
                }
            }
        }

        // 4. Close Window (Single window open)
        MenuItem {
            visible: appData ? (appData.windowCount === 1) : false
            text: "Close Window"
            onTriggered: {
                dockManager.dismissAllMenus();
                if (appData) dockManager.closeApp(appData.id);
            }
        }

        // 5. Close All Windows (Multiple windows open)
        MenuItem {
            visible: appData ? (appData.windowCount > 1) : false
            text: "Close All Windows"
            onTriggered: {
                dockManager.dismissAllMenus();
                if (appData) dockManager.closeApp(appData.id);
            }
        }

        // 6. Show All Windows (App Exposé)
        MenuItem {
            visible: appData ? (appData.windowCount > 0) : false
            text: "Show All Windows"
            onTriggered: {
                dockManager.dismissAllMenus();
                if (appData) dockManager.showAllWindows(appData.id);
            }
        }

        MenuSeparator {
            visible: appData ? appData.isRunning : false
        }

        // 7. Options Submenu
        Menu {
            id: optionsMenu
            title: "Options"

            MenuItem {
                text: (appData && appData.isPinned ? "✓ " : "   ") + "Keep in Dock"
                onTriggered: {
                    if (appData) {
                        if (appData.isPinned) {
                            dockManager.unpinApp(appData.id);
                        } else {
                            dockManager.pinApp(appData.id);
                        }
                    }
                }
            }

            MenuItem {
                visible: appData ? appData.isPinned : false
                text: "Remove from Dock"
                onTriggered: {
                    if (appData) dockManager.unpinApp(appData.id);
                }
            }

            MenuItem {
                text: (dockManager.isAutostartEnabled ? "✓ " : "   ") + "Open at Login"
                onTriggered: dockManager.toggleAutostart()
            }

            MenuSeparator {}

            MenuItem {
                text: "Move Left"
                enabled: root.itemIndex > 0
                onTriggered: dockManager.moveApp(root.itemIndex, root.itemIndex - 1)
            }

            MenuItem {
                text: "Move Right"
                enabled: root.itemIndex < (dockManager.apps.length - 1)
                onTriggered: dockManager.moveApp(root.itemIndex, root.itemIndex + 1)
            }

            MenuItem {
                text: (appData && appData.dockBreaksBefore) ? "Remove Divider Before" : "Add Divider Before"
                onTriggered: { if (appData) dockManager.toggleDividerBefore(appData.id); }
            }
        }

        MenuSeparator {}

        // 8. Hide
        MenuItem {
            visible: appData ? appData.isRunning : false
            text: "Hide"
            onTriggered: {
                dockManager.dismissAllMenus();
                if (appData) dockManager.minimizeApp(appData.id);
            }
        }

        // 9. Quit App
        MenuItem {
            visible: appData ? appData.isRunning : false
            text: "Quit " + (appData ? appData.title : "")
            onTriggered: {
                dockManager.dismissAllMenus();
                if (appData) dockManager.closeApp(appData.id);
            }
        }

        MenuSeparator {}

        // 10. Dock Settings Submenu
        Menu {
            id: dockSettingsMenu
            title: "Dock Settings"

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
}
