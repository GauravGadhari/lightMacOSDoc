import QtQuick

Item {
    id: root
    width: 14
    height: 57.6  // Match base dock icon height
    anchors.bottom: parent ? parent.bottom : undefined

    Rectangle {
        anchors.centerIn: parent
        width: 1
        height: 38
        color: dockManager.isDarkTheme ? Qt.rgba(1, 1, 1, 0.22) : Qt.rgba(0, 0, 0, 0.2)
        radius: 0.5
    }
}
