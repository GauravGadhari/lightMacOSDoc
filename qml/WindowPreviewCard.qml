import QtQuick
import QtQuick.Controls
import QtQuick.Effects

// Frameless Window Preview with sharp corners (no corner radius) and a smooth drop shadow
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

    // ── Drop Shadow for Sharp Window ──
    Item {
        id: shadowContainer
        anchors.fill: previewImage
        z: -1
        visible: previewImage.visible

        Rectangle {
            id: shadowSource
            anchors.fill: parent
            color: "#000000"
            visible: false
        }

        MultiEffect {
            anchors.fill: shadowSource
            source: shadowSource
            shadowEnabled: true
            autoPaddingEnabled: true
            shadowColor: Qt.rgba(0, 0, 0, 0.55)
            shadowBlur: 0.65
            shadowVerticalOffset: 6
            shadowHorizontalOffset: 0
        }
    }

    // ── Pure Window Screenshot (sharp corners, no corner radius) ──
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
