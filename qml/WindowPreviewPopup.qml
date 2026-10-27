import QtQuick
import QtQuick.Controls

// Floating Frameless Container for Live Window Previews
Item {
    id: root

    property var appData: null
    property bool showPreview: false
    property string appIcon: ""

    // Hover state to inform DockItem and DockContainer to stay visible
    property bool isHovered: popupHoverArea.containsMouse

    readonly property int winCount: (appData && appData.windows) ? appData.windows.length : 0

    // Scale dynamically based on window count so multi-window apps fit gracefully
    readonly property real cardWidth: winCount > 2 ? 260 : (winCount === 2 ? 310 : 360)
    readonly property real cardSpacing: 12

    readonly property real totalWidth: winCount > 0
        ? (winCount * cardWidth + (winCount - 1) * cardSpacing)
        : 0
    readonly property real totalHeight: cardsRow.height

    width: totalWidth
    height: totalHeight

    // ── Apple-Grade Spring Appear / Dissolve Animation ──
    property real animProgress: showPreview ? 1.0 : 0.0

    Behavior on animProgress {
        NumberAnimation {
            duration: root.showPreview ? 240 : 130
            easing.type: root.showPreview ? Easing.OutBack : Easing.OutQuad
            easing.overshoot: 1.25
        }
    }

    opacity: Math.max(0.0, Math.min(1.0, showPreview ? (animProgress * 1.5) : animProgress))
    scale: 0.88 + 0.12 * animProgress
    visible: opacity > 0.005 && winCount > 0

    transformOrigin: Item.Bottom
    transform: Translate {
        y: (1.0 - animProgress) * 16
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

    // Anchored cleanly 10px above the top of the dock icon
    anchors.bottom: parent ? parent.bottom : undefined
    anchors.bottomMargin: 10

    // ── Seamless Bridge Hover Area ──
    // Extends 24px downward to cover the gap to the icon so the mouse transition is gapless
    MouseArea {
        id: popupHoverArea
        anchors.fill: parent
        anchors.bottomMargin: -24
        anchors.topMargin: -12
        anchors.leftMargin: -12
        anchors.rightMargin: -12
        hoverEnabled: true
        acceptedButtons: Qt.NoButton
        propagateComposedEvents: true
        z: -1
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
            }
        }
    }
}
