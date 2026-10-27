import QtQuick
import QtQuick.Controls

// Frameless Window Preview with smooth rounded corners, floating shadow, and interactive hover
Item {
    id: root

    property string windowId: ""
    property string windowTitle: ""
    property bool windowMinimized: false
    property bool windowActive: false
    property string appIcon: ""

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
        NumberAnimation { duration: 150; easing.type: Easing.OutBack; easing.overshoot: 1.15 }
    }

    // ── Subtle Floating Shadow Behind the Rounded Preview ──
    Rectangle {
        anchors.fill: parent
        radius: 12
        color: Qt.rgba(0, 0, 0, 0.35)
        anchors.verticalCenterOffset: 4
        z: -1
        visible: previewImage.visible
    }

    // ── Pure Window Screenshot (captured cleanly upon hover with anti-aliased rounded corners) ──
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

    // ── Minimal Loading / Uncached Fallback ──
    Item {
        anchors.fill: parent
        visible: !previewImage.visible

        Image {
            anchors.centerIn: parent
            width: 48
            height: 48
            source: root.appIcon
            fillMode: Image.PreserveAspectFit
            smooth: true
            opacity: 0.45
        }
    }

    // ── Click to Focus & Activate Window ──
    MouseArea {
        id: cardMouse
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        z: 10

        onClicked: {
            dockManager.activateWindow(root.windowId);
            dockManager.dismissAllMenus();
        }
    }
}
