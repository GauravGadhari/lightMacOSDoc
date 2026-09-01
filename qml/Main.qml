import QtQuick
import QtQuick.Window

Window {
    id: root
    visible: true
    title: "macOS Dock"

    flags: Qt.FramelessWindowHint | Qt.BypassWindowManagerHint
    color: "transparent"

    // ── NEVER RESIZE ──
    // Window is ALWAYS 220px × fullWidth. We MOVE it up/down instead.
    // Moving = no GPU buffer realloc = no black square flicker.
    width: realScreenWidth
    height: 220
    x: 0
    y: realScreenHeight - 220  // starts fully visible

    // ── Auto-Hide State ──
    property bool dockVisible: true
    property real dockSlideOffset: 0       // 0 = visible, slideDistance = hidden
    readonly property int slideDistance: 180

    // ═══════════════════════════════════════════════════════════════
    // HIDE: Slide content below window, then move window down so
    // only the top 6px remains on-screen as the trigger zone.
    // ═══════════════════════════════════════════════════════════════
    NumberAnimation {
        id: slideHideAnim
        target: root
        property: "dockSlideOffset"
        to: root.slideDistance
        duration: 200
        easing.type: Easing.InCubic

        onFinished: {
            if (root.dockVisible) return;
            dockManager.setAutoHidden(true);
            // MOVE (not resize!) window down. Top 6px stays on-screen = trigger.
            // Bottom 214px is below screen edge = invisible & unreachable.
            root.y = realScreenHeight - 6;
            dockManager.resetMask();
        }
    }

    // ═══════════════════════════════════════════════════════════════
    // SHOW: Smoothly slide content up into view.
    // ═══════════════════════════════════════════════════════════════
    NumberAnimation {
        id: slideShowAnim
        target: root
        property: "dockSlideOffset"
        to: 0
        duration: 300
        easing.type: Easing.OutCubic

        onFinished: {
            dockContainer.requestMaskUpdate();
            if (!dockContainer.isMouseInside) {
                autoHideTimer.restart();
            }
        }
    }

    // ── Auto-Hide Timer ──
    Timer {
        id: autoHideTimer
        interval: 700
        repeat: false
        onTriggered: {
            // Auto-hide when mouse is outside the dock and no context menu is open
            if (!dockContainer.isMouseInside && !dockManager.isMenuOpen) {
                root.dockVisible = false;
            }
        }
    }

    // ── Reveal Settle Timer ──
    // Wait ~2 frames for compositor to process the window move
    // before making content visible and starting the slide animation.
    Timer {
        id: revealSettleTimer
        interval: 34
        repeat: false
        onTriggered: {
            dockContainer.opacity = 1;
            dockContainer.forceMaskUpdate();
            slideShowAnim.restart();
        }
    }

    // ── State Transitions ──
    onDockVisibleChanged: {
        if (dockVisible) {
            // ── REVEAL ──
            slideHideAnim.stop();
            revealSettleTimer.stop();
            dockManager.setAutoHidden(false);

            // Content invisible during the window move
            dockContainer.opacity = 0;

            // MOVE window to full-visible position (no resize!)
            root.y = realScreenHeight - 220;

            // Wait for compositor to settle, then show + animate
            revealSettleTimer.restart();
        } else {
            // ── HIDE ──
            slideShowAnim.stop();
            revealSettleTimer.stop();
            slideHideAnim.restart();
        }
    }

    Component.onCompleted: {
        dockManager.setWindow(root);
        dockContainer.requestMaskUpdate();
    }

    // ═══════════════════════════════════════════════════════════════
    // EDGE TRIGGER at TOP of window (y=0).
    // When hidden: window at y=screenH-6, so this is the bottom
    //   6px of the physical screen → cursor hits it to reveal.
    // When visible: this is above the dock content. z:-1 puts it
    //   behind DockContainer, and the guard prevents any action.
    // ═══════════════════════════════════════════════════════════════
    MouseArea {
        id: edgeTrigger
        anchors.left: parent.left
        anchors.right: parent.right
        y: 0
        height: 6
        hoverEnabled: true
        z: -1

        onEntered: {
            if (!root.dockVisible) {
                root.dockVisible = true;
            }
        }
    }

    // ── Dock Content ──
    DockContainer {
        id: dockContainer
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: -root.dockSlideOffset

        onIsMouseInsideChanged: {
            if (isMouseInside) {
                autoHideTimer.stop();
                if (!root.dockVisible) {
                    root.dockVisible = true;
                }
            } else {
                if (root.dockVisible) {
                    autoHideTimer.restart();
                }
            }
        }
    }

    // ── App Launch Auto-Dismiss ──
    Connections {
        target: dockManager
        function onAppLaunched(id) {
            autoHideTimer.interval = 300;
            autoHideTimer.restart();
            Qt.callLater(function() { autoHideTimer.interval = 700; });
        }
    }
}
