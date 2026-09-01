import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Window {
    id: wallWin
    title: "Wallpapers"
    width: 620
    height: 440
    minimumWidth: 480
    minimumHeight: 360
    flags: Qt.Window | Qt.FramelessWindowHint
    color: "transparent"

    onClosing: {
        dockManager.setAppRunningState("wallpapers", false);
    }

    property point dragPosition: Qt.point(0, 0)

    ListModel {
        id: wallModel
        ListElement { name: "Big Sur Graphic"; file: "big-sur-graphic-1.jpg" }
        ListElement { name: "Big Sur Light"; file: "big-sur-1.jpg" }
        ListElement { name: "Big Sur Dark"; file: "big-sur-2.jpg" }
        ListElement { name: "Catalina Day"; file: "catalina-1.jpg" }
        ListElement { name: "Catalina Night"; file: "catalina-2.jpg" }
        ListElement { name: "Mojave Desert"; file: "desert-1.jpg" }
        ListElement { name: "Mojave Night"; file: "desert-3.jpg" }
        ListElement { name: "Monterey"; file: "monterey-1.jpg" }
        ListElement { name: "Sonoma"; file: "sonoma-1.jpg" }
        ListElement { name: "Ventura"; file: "ventura-1.jpg" }
        ListElement { name: "Solar Grad"; file: "solar-grad-1.jpg" }
        ListElement { name: "The Beach"; file: "the-beach-1.jpg" }
    }

    function applyWallpaper(fileName) {
        var fullPath = "/mnt/code/_Antigravity/General/macos-dock-qt6/assets/wallpapers/" + fileName;
        // Apply using KDE Plasma wallpaper tool or gsettings
        dockManager.launchCommand("plasma-apply-wallpaperimage '" + fullPath + "' || feh --bg-fill '" + fullPath + "' || gsettings set org.gnome.desktop.background picture-uri 'file://" + fullPath + "'");
    }

    Rectangle {
        id: bgBox
        anchors.fill: parent
        radius: 12
        color: Qt.rgba(0.12, 0.12, 0.15, 0.94)
        border.color: Qt.rgba(1, 1, 1, 0.15)
        border.width: 0.8

        // Header with Traffic Lights
        Item {
            id: header
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.right: parent.right
            height: 38

            MouseArea {
                anchors.fill: parent
                onPressed: (mouse) => { wallWin.dragPosition = Qt.point(mouse.x, mouse.y); }
                onPositionChanged: (mouse) => {
                    var delta = Qt.point(mouse.x - wallWin.dragPosition.x, mouse.y - wallWin.dragPosition.y);
                    wallWin.x += delta.x;
                    wallWin.y += delta.y;
                }
            }

            Row {
                anchors.left: parent.left
                anchors.leftMargin: 12
                anchors.verticalCenter: parent.verticalCenter
                spacing: 7

                Rectangle {
                    width: 12; height: 12; radius: 6
                    color: closeHover.containsMouse ? "#FF453A" : "#FF5F56"
                    border.color: "#E0443E"; border.width: 0.5
                    MouseArea {
                        id: closeHover; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                        onClicked: { wallWin.close(); dockManager.setAppRunningState("wallpapers", false); }
                    }
                }
                Rectangle {
                    width: 12; height: 12; radius: 6
                    color: minHover.containsMouse ? "#FFD60A" : "#FFBD2E"
                    border.color: "#DEA123"; border.width: 0.5
                    MouseArea { id: minHover; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: wallWin.showMinimized() }
                }
                Rectangle {
                    width: 12; height: 12; radius: 6
                    color: "#27C93F"; border.color: "#1AAB29"; border.width: 0.5
                }
            }

            Text {
                anchors.centerIn: parent
                text: "Wallpapers"
                color: "#FFFFFF"
                font.pixelSize: 13
                font.weight: Font.DemiBold
            }
        }

        // Wallpaper Grid
        GridView {
            id: grid
            anchors.top: header.bottom
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.margins: 16
            cellWidth: 190
            cellHeight: 130
            clip: true
            model: wallModel

            delegate: Item {
                width: 180
                height: 120

                Rectangle {
                    anchors.fill: parent
                    radius: 8
                    color: itemHover.containsMouse ? Qt.rgba(1, 1, 1, 0.12) : Qt.rgba(1, 1, 1, 0.05)
                    border.color: itemHover.containsMouse ? "#0A84FF" : Qt.rgba(1, 1, 1, 0.15)
                    border.width: itemHover.containsMouse ? 2 : 0.6

                    Image {
                        id: thumb
                        anchors.top: parent.top
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.margins: 4
                        height: 84
                        source: "file:///mnt/code/_Antigravity/General/macos-dock-qt6/assets/wallpapers/" + model.file
                        fillMode: Image.PreserveAspectCrop
                        smooth: true
                        mipmap: true
                    }

                    Text {
                        anchors.bottom: parent.bottom
                        anchors.bottomMargin: 6
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: model.name
                        color: "white"
                        font.pixelSize: 11
                        font.weight: Font.Medium
                    }

                    MouseArea {
                        id: itemHover
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            wallWin.applyWallpaper(model.file);
                        }
                    }
                }
            }
        }
    }
}
