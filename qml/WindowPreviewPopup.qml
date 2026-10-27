import QtQuick
import QtQuick.Controls

// Floating Frameless Container for Live Window Previews
Item {
    id: root

    property var appData: null
    property bool showPreview: false
    property string appIcon: ""
    property var dockContainerRef: null
    property var dockItemRef: null

    property bool isHovered: false

    readonly property int winCount: (appData && appData.windows) ? appData.windows.length : 0

    // Scale dynamically based on window count so multi-window apps fit gracefully
    readonly property real cardWidth: winCount > 2 ? 260 : (winCount === 2 ? 310 : 360)
    readonly property real cardSpacing: 14

    readonly property real totalWidth: winCount > 0
        ? (winCount * cardWidth + (winCount - 1) * cardSpacing)
        : 0
    readonly property real totalHeight: cardsRow.height

    width: totalWidth
    height: totalHeight

    // ── Signature macOS Fluid Spring Pop Animation ──
    opacity: showPreview ? 1.0 : 0.0
    scale: showPreview ? 1.0 : 0.88
    visible: opacity > 0.01 && winCount > 0

    transformOrigin: Item.Bottom

    Behavior on opacity {
        NumberAnimation { duration: 150; easing.type: Easing.OutQuad }
    }
    Behavior on scale {
        NumberAnimation {
            duration: root.showPreview ? 220 : 120
            easing.type: root.showPreview ? Easing.OutBack : Easing.InQuad
            easing.overshoot: 1.15
        }
    }

    // Clamp horizontal position so previews near screen edges never clip off-screen
    x: {
        if (!parent) return 0;
        var iconGlobalCenterX = parent.mapToItem(null, parent.width / 2, 0).x;
        var idealGlobalLeft = iconGlobalCenterX - width / 2;
        var minGlobalLeft = 12;
        var maxGlobalLeft = (typeof realScreenWidth !== "undefined" ? realScreenWidth : 1920) - width - 12;
        var clampedGlobalLeft = Math.max(minGlobalLeft, Math.min(maxGlobalLeft, idealGlobalLeft));
        return parent.mapFromItem(null, clampedGlobalLeft, 0).x;
    }

    // Anchored cleanly 14px above the top of the dock icon with smooth rise
    anchors.bottom: parent ? parent.bottom : undefined
    anchors.bottomMargin: showPreview ? 14 : 2

    Behavior on anchors.bottomMargin {
        NumberAnimation {
            duration: root.showPreview ? 200 : 120
            easing.type: Easing.OutCubic
        }
    }

    // ── Preview Cards Row ──
    Row {
        id: cardsRow
        anchors.bottom: parent.bottom
        anchors.horizontalCenter: parent.horizontalCenter
        spacing: root.cardSpacing

        Repeater {
            id: cardsRepeater
            model: (root.appData && root.appData.windows && root.showPreview) ? root.appData.windows : []

            WindowPreviewCard {
                required property var modelData
                required property int index

                windowId: modelData.id || ""
                windowTitle: modelData.title || "Window"
                windowMinimized: modelData.minimized || false
                windowActive: modelData.active || false
                appIcon: root.appIcon
                width: root.cardWidth
                popupRef: root
            }
        }
    }

    // ── Keep dock & preview visible while interacting with preview cards ──
    MouseArea {
        anchors.fill: parent
        hoverEnabled: true
        acceptedButtons: Qt.NoButton
        propagateComposedEvents: true

        onEntered: {
            root.isHovered = true;
            if (root.dockContainerRef) {
                root.dockContainerRef.isMouseInside = true;
            }
        }

        onPositionChanged: {
            root.isHovered = true;
            if (root.dockContainerRef) {
                root.dockContainerRef.isMouseInside = true;
            }
        }

        onExited: {
            root.isHovered = false;
        }
    }

    onIsHoveredChanged: {
        if (isHovered && root.dockContainerRef) {
            root.dockContainerRef.isMouseInside = true;
        }
    }
}
