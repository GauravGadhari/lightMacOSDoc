import QtQuick
import QtQuick.Controls
import QtQuick.Effects

// Frameless Live Window Preview with Apple-grade rounded corners & shadow
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

    // ── Raw Window Screenshot ──
    Image {
        id: previewImage
        anchors.fill: parent
        source: root.windowId !== "" ? ("image://windowpreview/" + root.windowId) : ""
        asynchronous: true
        cache: false
        fillMode: Image.PreserveAspectFit
        smooth: true
        mipmap: true
        visible: false // rendered through MultiEffect with rounded mask
    }

    // ── Rounded Corner Mask ──
    Rectangle {
        id: maskRect
        width: root.width
        height: root.height
        radius: 16
        visible: false
    }

    // ── Rounded Window Thumbnail with Ambient Drop Shadow ──
    MultiEffect {
        id: effect
        source: previewImage
        anchors.fill: parent
        visible: previewImage.status === Image.Ready || previewImage.implicitWidth > 0

        maskEnabled: true
        maskSource: maskRect
        maskThresholdMin: 0.5
        maskSpreadAtMin: 1.0

        shadowEnabled: true
        shadowBlur: 0.8
        shadowOpacity: 0.45
        shadowVerticalOffset: 6
        shadowScale: 1.0
    }

    // ── Hairline Border Highlight (Matches macOS squircle icons) ──
    Rectangle {
        anchors.fill: parent
        radius: 16
        color: "transparent"
        border.width: 1
        border.color: dockManager.isDarkTheme
            ? Qt.rgba(1, 1, 1, 0.20)
            : Qt.rgba(0, 0, 0, 0.15)
        visible: effect.visible
    }

    // ── Minimal Loading Fallback ──
    Rectangle {
        anchors.fill: parent
        radius: 16
        color: dockManager.isDarkTheme ? Qt.rgba(0.12, 0.12, 0.14, 0.9) : Qt.rgba(0.9, 0.9, 0.92, 0.9)
        border.width: 1
        border.color: dockManager.isDarkTheme ? Qt.rgba(1, 1, 1, 0.15) : Qt.rgba(0, 0, 0, 0.12)
        visible: !effect.visible

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
