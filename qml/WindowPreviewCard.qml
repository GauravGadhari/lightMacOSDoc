import QtQuick
import QtQuick.Controls

// Minimalist Apple-grade Window Preview Card with real KWin window thumbnail
Item {
    id: root

    property string windowId: ""
    property string windowTitle: ""
    property bool windowMinimized: false
    property bool windowActive: false
    property string appIcon: ""
    property int refreshTick: 0

    width: 196
    height: 132

    // Hover state
    property bool isHovered: cardMouse.containsMouse || closeBtnMouse.containsMouse

    // Subtle hover background pill
    Rectangle {
        anchors.fill: parent
        anchors.margins: -3
        radius: 10
        color: {
            if (!root.isHovered) return "transparent";
            return dockManager.isDarkTheme
                ? Qt.rgba(1, 1, 1, 0.06)
                : Qt.rgba(0, 0, 0, 0.05);
        }
        Behavior on color { ColorAnimation { duration: 120 } }
    }

    Column {
        anchors.fill: parent
        spacing: 5

        // ── 1. Minimal Header: Clean Title + Hover-only Close Button ──
        Item {
            width: parent.width
            height: 20

            Text {
                anchors.left: parent.left
                anchors.leftMargin: 4
                anchors.right: closeBtn.visible ? closeBtn.left : parent.right
                anchors.rightMargin: 4
                anchors.verticalCenter: parent.verticalCenter
                text: root.windowTitle || "Window"
                font.pixelSize: 11
                font.weight: root.windowActive ? Font.Medium : Font.Normal
                font.family: "SF Pro Text, Inter, Cantarell, Ubuntu, sans-serif"
                color: dockManager.isDarkTheme ? "#EBEBF5" : "#1C1C1E"
                opacity: (root.isHovered || root.windowActive) ? 0.95 : 0.65
                elide: Text.ElideRight
                maximumLineCount: 1

                Behavior on opacity { NumberAnimation { duration: 120 } }
            }

            Rectangle {
                id: closeBtn
                anchors.right: parent.right
                anchors.rightMargin: 2
                anchors.verticalCenter: parent.verticalCenter
                width: 16
                height: 16
                radius: 8
                color: closeBtnMouse.containsMouse
                    ? "#FF453A"
                    : (dockManager.isDarkTheme ? Qt.rgba(1, 1, 1, 0.14) : Qt.rgba(0, 0, 0, 0.09))
                opacity: root.isHovered ? 1.0 : 0.0
                visible: opacity > 0.01

                Behavior on opacity { NumberAnimation { duration: 120 } }
                Behavior on color { ColorAnimation { duration: 80 } }

                Text {
                    anchors.centerIn: parent
                    text: "✕"
                    font.pixelSize: 8
                    font.weight: Font.Bold
                    color: closeBtnMouse.containsMouse
                        ? "#FFFFFF"
                        : (dockManager.isDarkTheme ? Qt.rgba(1, 1, 1, 0.75) : Qt.rgba(0, 0, 0, 0.6))
                }

                MouseArea {
                    id: closeBtnMouse
                    anchors.fill: parent
                    anchors.margins: -4
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: {
                        dockManager.closeWindowById(root.windowId);
                    }
                }
            }
        }

        // ── 2. Clean Edge-to-Edge Thumbnail Viewport ──
        Rectangle {
            id: thumbContainer
            width: parent.width
            height: parent.height - 25
            radius: 8
            color: dockManager.isDarkTheme
                ? Qt.rgba(0.06, 0.06, 0.08, 0.9)
                : Qt.rgba(0.90, 0.90, 0.93, 0.9)
            border.color: {
                if (root.isHovered) {
                    return dockManager.isDarkTheme ? Qt.rgba(1, 1, 1, 0.30) : Qt.rgba(0, 0, 0, 0.22);
                }
                if (root.windowActive) {
                    return dockManager.isDarkTheme ? Qt.rgba(1, 1, 1, 0.18) : Qt.rgba(0, 0, 0, 0.14);
                }
                return dockManager.isDarkTheme ? Qt.rgba(1, 1, 1, 0.07) : Qt.rgba(0, 0, 0, 0.06);
            }
            border.width: 0.8
            clip: true

            Behavior on border.color { ColorAnimation { duration: 120 } }

            // Real KWin Window Screenshot Thumbnail
            Image {
                id: previewImage
                anchors.fill: parent
                anchors.margins: 2
                source: root.windowId !== "" ? ("image://windowpreview/" + root.windowId) : ""
                asynchronous: true
                cache: false
                sourceSize: Qt.size(240, 150)
                fillMode: Image.PreserveAspectFit
                smooth: true
                mipmap: true
                visible: status === Image.Ready && implicitWidth > 0
            }

            // Minimal Fallback (shown only while loading or for uncached minimized windows)
            Item {
                anchors.fill: parent
                visible: !previewImage.visible

                Image {
                    anchors.centerIn: parent
                    anchors.verticalCenterOffset: root.windowMinimized ? -5 : 0
                    width: 32
                    height: 32
                    source: root.appIcon
                    fillMode: Image.PreserveAspectFit
                    smooth: true
                    mipmap: true
                    opacity: 0.45
                }

                Text {
                    visible: root.windowMinimized
                    anchors.bottom: parent.bottom
                    anchors.bottomMargin: 6
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: "Minimized"
                    font.pixelSize: 9
                    font.weight: Font.Normal
                    color: dockManager.isDarkTheme ? Qt.rgba(1, 1, 1, 0.38) : Qt.rgba(0, 0, 0, 0.38)
                }
            }
        }
    }

    // ── Click to Activate Window ──
    MouseArea {
        id: cardMouse
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        z: -1

        onClicked: {
            dockManager.activateWindow(root.windowId);
        }
    }
}
