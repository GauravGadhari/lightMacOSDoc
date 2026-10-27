import QtQuick
import QtQuick.Controls

// Floating Frameless Container for Live Window Previews
Item {
    id: root

    property var appData: null
    property bool showPreview: false
    property string appIcon: ""

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

    // Smooth fade & scale appearance
    opacity: showPreview ? 1.0 : 0.0
    scale: showPreview ? 1.0 : 0.94
    visible: opacity > 0.01 && winCount > 0

    transformOrigin: Item.Bottom

    Behavior on opacity {
        NumberAnimation { duration: 140; easing.type: Easing.OutQuad }
    }
    Behavior on scale {
        NumberAnimation { duration: 140; easing.type: Easing.OutCubic }
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
    anchors.bottomMargin: showPreview ? 10 : 4

    Behavior on anchors.bottomMargin {
        NumberAnimation { duration: 140; easing.type: Easing.OutQuad }
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

    // ── Keep dock visible while interacting with preview cards ──
    MouseArea {
        anchors.fill: cardsRow
        hoverEnabled: true
        acceptedButtons: Qt.NoButton
        propagateComposedEvents: true
    }
}
