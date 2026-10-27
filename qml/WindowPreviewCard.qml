import QtQuick
import QtQuick.Controls

// Frameless Live Window Preview with Apple-grade squircle corners & ambient shadow
Item {
    id: root

    property string windowId: ""
    property string windowTitle: ""
    property bool windowMinimized: false
    property bool windowActive: false
    property string appIcon: ""
    property var popupRef: null

    // Width provided by parent; height dynamically derived from window aspect ratio
    width: 360
    height: {
        if (previewImage.implicitWidth > 0 && previewImage.implicitHeight > 0) {
            var calcH = width * (previewImage.implicitHeight / previewImage.implicitWidth);
            return Math.min(240, Math.max(140, calcH));
        }
        return width * (10.0 / 16.0); // 225
    }

    // Hover state for subtle interactive feel
    property bool isHovered: cardMouse.containsMouse

    scale: isHovered ? 1.03 : 1.0
    Behavior on scale {
        NumberAnimation { duration: 140; easing.type: Easing.OutQuad }
    }

    // ── Soft Ambient Drop Shadow ──
    Rectangle {
        anchors.fill: parent
        anchors.topMargin: -1
        anchors.bottomMargin: -6
        anchors.leftMargin: -4
        anchors.rightMargin: -4
        radius: 18
        color: Qt.rgba(0, 0, 0, 0.38)
        z: -1
        visible: previewImage.visible
    }

    // ── Pure Window Screenshot (Rounded squircle corners baked in via C++ QPainter) ──
    Image {
        id: previewImage
        anchors.fill: parent
        source: root.windowId !== "" ? ("image://windowpreview/" + root.windowId) : ""
        asynchronous: true
        cache: false
        fillMode: Image.PreserveAspectFit
        smooth: true
        mipmap: true
        visible: status === Image.Ready || implicitWidth > 0
    }

    // ── Hairline Border Highlight (Matches macOS squircle icons) ──
    Rectangle {
        anchors.fill: parent
        radius: 14
        color: "transparent"
        border.width: 1
        border.color: dockManager.isDarkTheme
            ? Qt.rgba(1, 1, 1, 0.22)
            : Qt.rgba(0, 0, 0, 0.16)
        visible: previewImage.visible
        z: 1
    }

    // ── Minimal Loading Fallback ──
    Rectangle {
        anchors.fill: parent
        radius: 14
        color: dockManager.isDarkTheme ? Qt.rgba(0.12, 0.12, 0.14, 0.9) : Qt.rgba(0.9, 0.9, 0.92, 0.9)
        border.width: 1
        border.color: dockManager.isDarkTheme ? Qt.rgba(1, 1, 1, 0.15) : Qt.rgba(0, 0, 0, 0.12)
        visible: !previewImage.visible

        Image {
            anchors.centerIn: parent
            width: 48
            height: 48
            source: root.appIcon
            fillMode: Image.PreserveAspectFit
            smooth: true
            opacity: 0.5
        }
    }

    // ── Click to Focus & Activate Window ──
    MouseArea {
        id: cardMouse
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        z: 10

        onEntered: {
            if (root.popupRef) {
                root.popupRef.isHovered = true;
            }
        }

        onPositionChanged: {
            if (root.popupRef) {
                root.popupRef.isHovered = true;
            }
        }

        onClicked: {
            dockManager.activateWindow(root.windowId);
            dockManager.dismissAllMenus();
        }
    }
}
