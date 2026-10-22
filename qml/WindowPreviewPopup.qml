import QtQuick
import QtQuick.Controls

// Minimalist Frosted Glass Container Popup for Window Preview Cards
Item {
    id: root

    property var appData: null
    property bool showPreview: false
    property string appIcon: ""

    readonly property int winCount: (appData && appData.windows) ? appData.windows.length : 0

    // Card dimensions
    readonly property real cardWidth: 196
    readonly property real cardHeight: 132
    readonly property real cardSpacing: 8
    readonly property real popupPadding: 8
    readonly property real caretSize: 5

    // Total popup dimensions
    readonly property real popupContentWidth: winCount > 0
        ? (winCount * cardWidth + (winCount - 1) * cardSpacing)
        : 0
    readonly property real totalWidth: popupContentWidth + popupPadding * 2
    readonly property real totalHeight: cardHeight + popupPadding * 2 + caretSize

    width: totalWidth
    height: totalHeight
    anchors.horizontalCenter: parent ? parent.horizontalCenter : undefined

    // Position cleanly above the icon
    y: showPreview ? (-totalHeight - 6) : (-totalHeight + 4)
    opacity: showPreview ? 1.0 : 0.0
    scale: showPreview ? 1.0 : 0.92
    visible: opacity > 0.01 && winCount > 0

    transformOrigin: Item.Bottom

    Behavior on opacity {
        NumberAnimation { duration: 150; easing.type: Easing.OutQuad }
    }
    Behavior on scale {
        NumberAnimation { duration: 150; easing.type: Easing.OutCubic }
    }
    Behavior on y {
        NumberAnimation { duration: 150; easing.type: Easing.OutQuad }
    }

    // ── Minimal Frosted Glass Surface ──
    Rectangle {
        id: bgPill
        anchors.top: parent.top
        anchors.horizontalCenter: parent.horizontalCenter
        width: root.totalWidth
        height: root.cardHeight + root.popupPadding * 2
        radius: 13

        color: dockManager.isDarkTheme
            ? Qt.rgba(0.11, 0.11, 0.13, 0.94)
            : Qt.rgba(0.95, 0.95, 0.97, 0.95)
        border.color: dockManager.isDarkTheme
            ? Qt.rgba(1, 1, 1, 0.14)
            : Qt.rgba(0, 0, 0, 0.10)
        border.width: 0.8

        // Hairline top highlight
        Rectangle {
            anchors.top: parent.top
            anchors.topMargin: 1
            anchors.horizontalCenter: parent.horizontalCenter
            width: parent.width - 22
            height: 1
            radius: 0.5
            color: dockManager.isDarkTheme ? Qt.rgba(1, 1, 1, 0.12) : Qt.rgba(1, 1, 1, 0.5)
        }
    }

    // ── Subtle Downward Caret ──
    Canvas {
        id: caret
        anchors.top: bgPill.bottom
        anchors.topMargin: -1
        anchors.horizontalCenter: parent.horizontalCenter
        width: 12
        height: root.caretSize

        onPaint: {
            var ctx = getContext("2d");
            ctx.reset();
            ctx.beginPath();
            ctx.moveTo(0, 0);
            ctx.lineTo(width / 2, height);
            ctx.lineTo(width, 0);
            ctx.closePath();

            ctx.fillStyle = dockManager.isDarkTheme
                ? Qt.rgba(0.11, 0.11, 0.13, 0.94)
                : Qt.rgba(0.95, 0.95, 0.97, 0.95);
            ctx.fill();

            ctx.beginPath();
            ctx.moveTo(0, 0);
            ctx.lineTo(width / 2, height);
            ctx.lineTo(width, 0);
            ctx.strokeStyle = dockManager.isDarkTheme
                ? Qt.rgba(1, 1, 1, 0.14)
                : Qt.rgba(0, 0, 0, 0.10);
            ctx.lineWidth = 0.8;
            ctx.stroke();
        }

        Connections {
            target: dockManager
            function onIsDarkThemeChanged() {
                caret.requestPaint();
            }
        }
    }

    // ── Preview Cards Row ──
    Row {
        id: cardsRow
        anchors.top: bgPill.top
        anchors.topMargin: root.popupPadding
        anchors.horizontalCenter: bgPill.horizontalCenter
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
                height: root.cardHeight
            }
        }
    }

    // ── Keep dock visible while interacting with preview cards ──
    MouseArea {
        anchors.fill: bgPill
        hoverEnabled: true
        acceptedButtons: Qt.NoButton
        propagateComposedEvents: true
    }
}
