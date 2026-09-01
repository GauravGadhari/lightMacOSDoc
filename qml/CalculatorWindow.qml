import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Window {
    id: calcWin
    title: "Calculator"
    width: 240
    height: 330
    minimumWidth: 240
    minimumHeight: 330
    maximumWidth: 240
    maximumHeight: 330
    flags: Qt.Window | Qt.FramelessWindowHint
    color: "transparent"

    onClosing: {
        dockManager.setAppRunningState("calculator", false);
    }

    // Calculation Engine State
    property string displayVal: "0"
    property real storedVal: 0
    property string pendingOp: ""
    property bool waitingForOperand: false

    function inputDigit(digit) {
        if (waitingForOperand) {
            displayVal = digit;
            waitingForOperand = false;
        } else {
            if (displayVal === "0" && digit !== ".") {
                displayVal = digit;
            } else {
                if (digit === "." && displayVal.indexOf(".") !== -1) return;
                displayVal += digit;
            }
        }
    }

    function applyOp(op) {
        var cur = parseFloat(displayVal);
        if (pendingOp !== "" && !waitingForOperand) {
            calculate();
            cur = parseFloat(displayVal);
        }
        storedVal = cur;
        pendingOp = op;
        waitingForOperand = true;
    }

    function calculate() {
        if (pendingOp === "") return;
        var cur = parseFloat(displayVal);
        var res = 0;
        if (pendingOp === "+") res = storedVal + cur;
        else if (pendingOp === "-") res = storedVal - cur;
        else if (pendingOp === "×") res = storedVal * cur;
        else if (pendingOp === "÷") res = (cur !== 0) ? (storedVal / cur) : 0;

        // Clean trailing zeros
        var str = res.toString();
        if (str.length > 10) str = res.toPrecision(8);
        displayVal = str;
        pendingOp = "";
        waitingForOperand = true;
    }

    function clearAll() {
        displayVal = "0";
        storedVal = 0;
        pendingOp = "";
        waitingForOperand = false;
    }

    function toggleSign() {
        if (displayVal === "0") return;
        if (displayVal.startsWith("-")) {
            displayVal = displayVal.substring(1);
        } else {
            displayVal = "-" + displayVal;
        }
    }

    function percent() {
        var cur = parseFloat(displayVal);
        displayVal = (cur / 100.0).toString();
    }

    // Dragging helper
    property point dragPosition: Qt.point(0, 0)

    // Outer Background Box
    Rectangle {
        id: bgBox
        anchors.fill: parent
        radius: 12
        color: Qt.rgba(0.12, 0.12, 0.14, 0.94)
        border.color: Qt.rgba(1, 1, 1, 0.15)
        border.width: 0.8

        // Header / Window Drag Area
        Item {
            id: header
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.right: parent.right
            height: 32

            MouseArea {
                anchors.fill: parent
                onPressed: (mouse) => {
                    calcWin.dragPosition = Qt.point(mouse.x, mouse.y);
                }
                onPositionChanged: (mouse) => {
                    var delta = Qt.point(mouse.x - calcWin.dragPosition.x, mouse.y - calcWin.dragPosition.y);
                    calcWin.x += delta.x;
                    calcWin.y += delta.y;
                }
            }

            // macOS Traffic Lights (🔴 🟡 🟢)
            Row {
                anchors.left: parent.left
                anchors.leftMargin: 12
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
                        onClicked: {
                            calcWin.close();
                            dockManager.setAppRunningState("calculator", false);
                        }
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
                        onClicked: calcWin.showMinimized()
                    }
                }

                // Green Expand Button
                Rectangle {
                    width: 12
                    height: 12
                    radius: 6
                    color: expandHover.containsMouse ? "#30D158" : "#27C93F"
                    border.color: "#1AAB29"
                    border.width: 0.5

                    MouseArea {
                        id: expandHover
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                    }
                }
            }
        }

        // Calculator Result Display
        Text {
            id: displayText
            anchors.top: header.bottom
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.margins: 14
            height: 48
            text: calcWin.displayVal
            color: "#FFFFFF"
            font.pixelSize: (text.length > 8) ? 26 : 38
            font.weight: Font.Light
            font.family: "SF Pro Display, Inter, Helvetica, sans-serif"
            horizontalAlignment: Text.AlignRight
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideLeft
        }

        // Keypad Grid
        Grid {
            id: keyGrid
            anchors.top: displayText.bottom
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.margins: 2
            columns: 4
            rowSpacing: 1
            columnSpacing: 1

            readonly property real btnW: (width - 3) / 4.0
            readonly property real btnH: (height - 4) / 5.0

            // Row 1: AC, +/-, %, ÷
            Rectangle {
                width: keyGrid.btnW; height: keyGrid.btnH
                color: acHover.pressed ? "#6C6C75" : (acHover.containsMouse ? "#5A5A62" : "#4A4A52")
                Text { anchors.centerIn: parent; text: (calcWin.displayVal === "0" && calcWin.storedVal === 0) ? "AC" : "C"; color: "white"; font.pixelSize: 17; font.weight: Font.Medium }
                MouseArea { id: acHover; anchors.fill: parent; hoverEnabled: true; onClicked: calcWin.clearAll() }
            }
            Rectangle {
                width: keyGrid.btnW; height: keyGrid.btnH
                color: signHover.pressed ? "#6C6C75" : (signHover.containsMouse ? "#5A5A62" : "#4A4A52")
                Text { anchors.centerIn: parent; text: "+/-"; color: "white"; font.pixelSize: 17; font.weight: Font.Medium }
                MouseArea { id: signHover; anchors.fill: parent; hoverEnabled: true; onClicked: calcWin.toggleSign() }
            }
            Rectangle {
                width: keyGrid.btnW; height: keyGrid.btnH
                color: pctHover.pressed ? "#6C6C75" : (pctHover.containsMouse ? "#5A5A62" : "#4A4A52")
                Text { anchors.centerIn: parent; text: "%"; color: "white"; font.pixelSize: 17; font.weight: Font.Medium }
                MouseArea { id: pctHover; anchors.fill: parent; hoverEnabled: true; onClicked: calcWin.percent() }
            }
            Rectangle {
                width: keyGrid.btnW; height: keyGrid.btnH
                color: divHover.pressed ? "#FFB340" : (divHover.containsMouse ? "#FFAC26" : "#FF9F0A")
                Text { anchors.centerIn: parent; text: "÷"; color: "white"; font.pixelSize: 22; font.weight: Font.DemiBold }
                MouseArea { id: divHover; anchors.fill: parent; hoverEnabled: true; onClicked: calcWin.applyOp("÷") }
            }

            // Row 2: 7, 8, 9, ×
            Rectangle {
                width: keyGrid.btnW; height: keyGrid.btnH
                color: d7.pressed ? "#7C7C85" : (d7.containsMouse ? "#6C6C75" : "#5E5E65")
                Text { anchors.centerIn: parent; text: "7"; color: "white"; font.pixelSize: 18; font.weight: Font.Medium }
                MouseArea { id: d7; anchors.fill: parent; hoverEnabled: true; onClicked: calcWin.inputDigit("7") }
            }
            Rectangle {
                width: keyGrid.btnW; height: keyGrid.btnH
                color: d8.pressed ? "#7C7C85" : (d8.containsMouse ? "#6C6C75" : "#5E5E65")
                Text { anchors.centerIn: parent; text: "8"; color: "white"; font.pixelSize: 18; font.weight: Font.Medium }
                MouseArea { id: d8; anchors.fill: parent; hoverEnabled: true; onClicked: calcWin.inputDigit("8") }
            }
            Rectangle {
                width: keyGrid.btnW; height: keyGrid.btnH
                color: d9.pressed ? "#7C7C85" : (d9.containsMouse ? "#6C6C75" : "#5E5E65")
                Text { anchors.centerIn: parent; text: "9"; color: "white"; font.pixelSize: 18; font.weight: Font.Medium }
                MouseArea { id: d9; anchors.fill: parent; hoverEnabled: true; onClicked: calcWin.inputDigit("9") }
            }
            Rectangle {
                width: keyGrid.btnW; height: keyGrid.btnH
                color: mulHover.pressed ? "#FFB340" : (mulHover.containsMouse ? "#FFAC26" : "#FF9F0A")
                Text { anchors.centerIn: parent; text: "×"; color: "white"; font.pixelSize: 22; font.weight: Font.DemiBold }
                MouseArea { id: mulHover; anchors.fill: parent; hoverEnabled: true; onClicked: calcWin.applyOp("×") }
            }

            // Row 3: 4, 5, 6, -
            Rectangle {
                width: keyGrid.btnW; height: keyGrid.btnH
                color: d4.pressed ? "#7C7C85" : (d4.containsMouse ? "#6C6C75" : "#5E5E65")
                Text { anchors.centerIn: parent; text: "4"; color: "white"; font.pixelSize: 18; font.weight: Font.Medium }
                MouseArea { id: d4; anchors.fill: parent; hoverEnabled: true; onClicked: calcWin.inputDigit("4") }
            }
            Rectangle {
                width: keyGrid.btnW; height: keyGrid.btnH
                color: d5.pressed ? "#7C7C85" : (d5.containsMouse ? "#6C6C75" : "#5E5E65")
                Text { anchors.centerIn: parent; text: "5"; color: "white"; font.pixelSize: 18; font.weight: Font.Medium }
                MouseArea { id: d5; anchors.fill: parent; hoverEnabled: true; onClicked: calcWin.inputDigit("5") }
            }
            Rectangle {
                width: keyGrid.btnW; height: keyGrid.btnH
                color: d6.pressed ? "#7C7C85" : (d6.containsMouse ? "#6C6C75" : "#5E5E65")
                Text { anchors.centerIn: parent; text: "6"; color: "white"; font.pixelSize: 18; font.weight: Font.Medium }
                MouseArea { id: d6; anchors.fill: parent; hoverEnabled: true; onClicked: calcWin.inputDigit("6") }
            }
            Rectangle {
                width: keyGrid.btnW; height: keyGrid.btnH
                color: subHover.pressed ? "#FFB340" : (subHover.containsMouse ? "#FFAC26" : "#FF9F0A")
                Text { anchors.centerIn: parent; text: "-"; color: "white"; font.pixelSize: 24; font.weight: Font.DemiBold }
                MouseArea { id: subHover; anchors.fill: parent; hoverEnabled: true; onClicked: calcWin.applyOp("-") }
            }

            // Row 4: 1, 2, 3, +
            Rectangle {
                width: keyGrid.btnW; height: keyGrid.btnH
                color: d1.pressed ? "#7C7C85" : (d1.containsMouse ? "#6C6C75" : "#5E5E65")
                Text { anchors.centerIn: parent; text: "1"; color: "white"; font.pixelSize: 18; font.weight: Font.Medium }
                MouseArea { id: d1; anchors.fill: parent; hoverEnabled: true; onClicked: calcWin.inputDigit("1") }
            }
            Rectangle {
                width: keyGrid.btnW; height: keyGrid.btnH
                color: d2.pressed ? "#7C7C85" : (d2.containsMouse ? "#6C6C75" : "#5E5E65")
                Text { anchors.centerIn: parent; text: "2"; color: "white"; font.pixelSize: 18; font.weight: Font.Medium }
                MouseArea { id: d2; anchors.fill: parent; hoverEnabled: true; onClicked: calcWin.inputDigit("2") }
            }
            Rectangle {
                width: keyGrid.btnW; height: keyGrid.btnH
                color: d3.pressed ? "#7C7C85" : (d3.containsMouse ? "#6C6C75" : "#5E5E65")
                Text { anchors.centerIn: parent; text: "3"; color: "white"; font.pixelSize: 18; font.weight: Font.Medium }
                MouseArea { id: d3; anchors.fill: parent; hoverEnabled: true; onClicked: calcWin.inputDigit("3") }
            }
            Rectangle {
                width: keyGrid.btnW; height: keyGrid.btnH
                color: addHover.pressed ? "#FFB340" : (addHover.containsMouse ? "#FFAC26" : "#FF9F0A")
                Text { anchors.centerIn: parent; text: "+"; color: "white"; font.pixelSize: 22; font.weight: Font.DemiBold }
                MouseArea { id: addHover; anchors.fill: parent; hoverEnabled: true; onClicked: calcWin.applyOp("+") }
            }

            // Row 5: 0 (spans 2), ., =
            Rectangle {
                width: keyGrid.btnW * 2 + 1; height: keyGrid.btnH
                radius: 10
                color: d0.pressed ? "#7C7C85" : (d0.containsMouse ? "#6C6C75" : "#5E5E65")
                Text { anchors.left: parent.left; anchors.leftMargin: keyGrid.btnW * 0.45; anchors.verticalCenter: parent.verticalCenter; text: "0"; color: "white"; font.pixelSize: 18; font.weight: Font.Medium }
                MouseArea { id: d0; anchors.fill: parent; hoverEnabled: true; onClicked: calcWin.inputDigit("0") }
            }
            Rectangle {
                width: keyGrid.btnW; height: keyGrid.btnH
                color: dotHover.pressed ? "#7C7C85" : (dotHover.containsMouse ? "#6C6C75" : "#5E5E65")
                Text { anchors.centerIn: parent; text: "."; color: "white"; font.pixelSize: 20; font.weight: Font.Bold }
                MouseArea { id: dotHover; anchors.fill: parent; hoverEnabled: true; onClicked: calcWin.inputDigit(".") }
            }
            Rectangle {
                width: keyGrid.btnW; height: keyGrid.btnH
                radius: 10
                color: eqHover.pressed ? "#FFB340" : (eqHover.containsMouse ? "#FFAC26" : "#FF9F0A")
                Text { anchors.centerIn: parent; text: "="; color: "white"; font.pixelSize: 22; font.weight: Font.DemiBold }
                MouseArea { id: eqHover; anchors.fill: parent; hoverEnabled: true; onClicked: calcWin.calculate() }
            }
        }
    }
}
