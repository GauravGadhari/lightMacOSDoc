import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Window {
    id: settingsWindow
    title: "System Settings"
    width: 480
    height: 380
    minimumWidth: 420
    minimumHeight: 340
    flags: Qt.Window | Qt.FramelessWindowHint
    color: "transparent"

    property point dragPosition: Qt.point(0, 0)

    // Keyboard Shortcuts to dismiss instantly
    Shortcut {
        sequence: "Escape"
        onActivated: settingsWindow.close()
    }
    Shortcut {
        sequence: "Ctrl+W"
        onActivated: settingsWindow.close()
    }

    // Outer Frosted Glass Window Frame
    Rectangle {
        id: bgBox
        anchors.fill: parent
        radius: 12
        color: dockManager.isDarkTheme ? Qt.rgba(0.13, 0.13, 0.15, 0.96) : Qt.rgba(0.96, 0.96, 0.98, 0.96)
        border.color: dockManager.isDarkTheme ? Qt.rgba(1, 1, 1, 0.15) : Qt.rgba(0, 0, 0, 0.15)
        border.width: 0.8

        // Draggable Title Bar / Header
        Item {
            id: header
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.right: parent.right
            height: 42

            MouseArea {
                anchors.fill: parent
                onPressed: (mouse) => {
                    settingsWindow.dragPosition = Qt.point(mouse.x, mouse.y);
                }
                onPositionChanged: (mouse) => {
                    var delta = Qt.point(mouse.x - settingsWindow.dragPosition.x, mouse.y - settingsWindow.dragPosition.y);
                    settingsWindow.x += delta.x;
                    settingsWindow.y += delta.y;
                }
            }

            // macOS Traffic Lights (🔴 🟡 🟢)
            Row {
                anchors.left: parent.left
                anchors.leftMargin: 14
                anchors.verticalCenter: parent.verticalCenter
                spacing: 7

                // Red Close Button
                Rectangle {
                    width: 12
                    height: 12
                    radius: 6
                    color: closeHover.containsMouse ? "#FF453A" : "#FF5F56"
                    border.color: "#E0443E"
                    border.width: 0.5

                    MouseArea {
                        id: closeHover
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: settingsWindow.close()
                    }
                }

                // Yellow Minimize Button
                Rectangle {
                    width: 12
                    height: 12
                    radius: 6
                    color: minHover.containsMouse ? "#FFD60A" : "#FFBD2E"
                    border.color: "#DEA123"
                    border.width: 0.5

                    MouseArea {
                        id: minHover
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: settingsWindow.showMinimized()
                    }
                }

                // Green Zoom Button
                Rectangle {
                    width: 12
                    height: 12
                    radius: 6
                    color: zoomHover.containsMouse ? "#30D158" : "#27C93F"
                    border.color: "#1AAB29"
                    border.width: 0.5

                    MouseArea {
                        id: zoomHover
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            if (settingsWindow.visibility === Window.Maximized) {
                                settingsWindow.showNormal();
                            } else {
                                settingsWindow.showMaximized();
                            }
                        }
                    }
                }
            }

            // Window Title
            Text {
                anchors.centerIn: parent
                text: "System Settings"
                color: dockManager.isDarkTheme ? "#FFFFFF" : "#1A1A1A"
                font.pixelSize: 13
                font.weight: Font.DemiBold
                font.family: "SF Pro Display, Inter, Helvetica, sans-serif"
            }

            // Separator line under header
            Rectangle {
                anchors.bottom: parent.bottom
                anchors.left: parent.left
                anchors.right: parent.right
                height: 0.8
                color: dockManager.isDarkTheme ? Qt.rgba(1, 1, 1, 0.1) : Qt.rgba(0, 0, 0, 0.1)
            }
        }

        // Settings Body Container
        Item {
            anchors.top: header.bottom
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.margins: 20

            ColumnLayout {
                anchors.fill: parent
                spacing: 16

                // Category: Dock & Magnification
                ColumnLayout {
                    spacing: 10
                    Layout.fillWidth: true

                    Text {
                        text: "Dock Appearance & Physics"
                        font.pixelSize: 14
                        font.weight: Font.Bold
                        color: dockManager.isDarkTheme ? "#FFFFFF" : "#1A1A1A"
                    }

                    // Base Icon Size Slider
                    ColumnLayout {
                        spacing: 3
                        Layout.fillWidth: true

                        RowLayout {
                            Layout.fillWidth: true
                            Text {
                                text: "Base Icon Size:"
                                font.pixelSize: 12
                                color: dockManager.isDarkTheme ? "#C0C0C5" : "#444444"
                            }
                            Item { Layout.fillWidth: true }
                            Text {
                                text: Math.round(dockManager.baseIconWidth) + " px"
                                font.pixelSize: 12
                                font.weight: Font.DemiBold
                                color: "#0A84FF"
                            }
                        }

                        Slider {
                            Layout.fillWidth: true
                            from: 36.0
                            to: 84.0
                            value: dockManager.baseIconWidth
                            stepSize: 1.0
                            onMoved: dockManager.baseIconWidth = value
                        }
                    }

                    // Magnification Multiplier Slider
                    ColumnLayout {
                        spacing: 3
                        Layout.fillWidth: true

                        RowLayout {
                            Layout.fillWidth: true
                            Text {
                                text: "Magnification Wave Scale (xzoom):"
                                font.pixelSize: 12
                                color: dockManager.isDarkTheme ? "#C0C0C5" : "#444444"
                            }
                            Item { Layout.fillWidth: true }
                            Text {
                                text: dockManager.maxMagnification.toFixed(1) + "x"
                                font.pixelSize: 12
                                font.weight: Font.DemiBold
                                color: "#0A84FF"
                            }
                        }

                        Slider {
                            Layout.fillWidth: true
                            from: 1.0
                            to: 2.5
                            value: dockManager.maxMagnification
                            stepSize: 0.1
                            onMoved: dockManager.maxMagnification = value
                        }
                    }

                    // Theme Toggle
                    RowLayout {
                        Layout.fillWidth: true
                        Text {
                            text: "Frosted Glass Mode:"
                            font.pixelSize: 12
                            color: dockManager.isDarkTheme ? "#C0C0C5" : "#444444"
                        }
                        Item { Layout.fillWidth: true }
                        Button {
                            text: dockManager.isDarkTheme ? "🌙 Dark Mode" : "☀️ Light Mode"
                            onClicked: dockManager.isDarkTheme = !dockManager.isDarkTheme
                        }
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    height: 1
                    color: dockManager.isDarkTheme ? Qt.rgba(1, 1, 1, 0.08) : Qt.rgba(0, 0, 0, 0.08)
                }

                // Category: Linux System Shortcuts
                ColumnLayout {
                    spacing: 8
                    Layout.fillWidth: true

                    Text {
                        text: "System Controls"
                        font.pixelSize: 14
                        font.weight: Font.Bold
                        color: dockManager.isDarkTheme ? "#FFFFFF" : "#1A1A1A"
                    }

                    RowLayout {
                        spacing: 10
                        Button {
                            text: "Open KDE System Settings"
                            onClicked: {
                                dockManager.launchCommand("systemsettings");
                                settingsWindow.close();
                            }
                        }
                        Button {
                            text: "Open Wallpaper Settings"
                            onClicked: {
                                dockManager.launchCommand("systemsettings kcm_desktoptheme || xdg-open /usr/share/wallpapers");
                                settingsWindow.close();
                            }
                        }
                    }
                }

                Item { Layout.fillHeight: true }

                // Dismiss / Close Button
                RowLayout {
                    Layout.fillWidth: true
                    Item { Layout.fillWidth: true }
                    Button {
                        text: "Done"
                        highlighted: true
                        onClicked: settingsWindow.close()
                    }
                }
            }
        }
    }
}
