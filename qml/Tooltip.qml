import QtQuick
import QtQuick.Controls

Item {
    id: root
    property string text: ""
    property bool visibleTooltip: false

    width: pill.width
    height: pill.height

    opacity: visibleTooltip ? 1.0 : 0.0
    scale: visibleTooltip ? 1.0 : 0.85
    y: visibleTooltip ? -pill.height - 12 : -pill.height - 4

    Behavior on opacity {
        NumberAnimation { duration: 150; easing.type: Easing.OutQuad }
    }
    Behavior on scale {
        NumberAnimation { duration: 150; easing.type: Easing.OutBack; easing.overshoot: 1.1 }
    }
    Behavior on y {
        NumberAnimation { duration: 150; easing.type: Easing.OutQuad }
    }

    Rectangle {
        id: pill
        width: label.implicitWidth + 24
        height: label.implicitHeight + 10
        radius: 6
        color: dockManager.isDarkTheme ? Qt.rgba(0.12, 0.12, 0.14, 0.88) : Qt.rgba(0.96, 0.96, 0.98, 0.92)
        border.color: dockManager.isDarkTheme ? Qt.rgba(1, 1, 1, 0.2) : Qt.rgba(0, 0, 0, 0.15)
        border.width: 0.6

        Text {
            id: label
            anchors.centerIn: parent
            text: root.text
            color: dockManager.isDarkTheme ? "#FFFFFF" : "#111111"
            font.pixelSize: 12
            font.weight: Font.DemiBold
            font.family: "SF Pro Text, Inter, Cantarell, Ubuntu, sans-serif"
        }
    }
}
