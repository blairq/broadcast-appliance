// Pildora de estado con punto (AL AIRE, REC, gateway...).
import QtQuick
import BA

Rectangle {
    id: pill
    property string text: ""
    property color dot: Theme.ok
    property bool on: true
    property bool pulse: false

    implicitHeight: 30
    implicitWidth: label.implicitWidth + 36
    radius: height / 2
    color: on ? Qt.rgba(dot.r, dot.g, dot.b, 0.18) : Qt.rgba(1, 1, 1, 0.06)
    border.color: on ? Qt.rgba(dot.r, dot.g, dot.b, 0.55) : Qt.rgba(1, 1, 1, 0.12)

    Rectangle {
        id: d
        width: 8; height: 8; radius: 4
        anchors { left: parent.left; leftMargin: 12; verticalCenter: parent.verticalCenter }
        color: pill.on ? pill.dot : Theme.textFaint
        SequentialAnimation on opacity {
            running: pill.on && pill.pulse
            loops: Animation.Infinite
            NumberAnimation { to: 0.25; duration: 650; easing.type: Easing.InOutSine }
            NumberAnimation { to: 1.0; duration: 650; easing.type: Easing.InOutSine }
        }
    }
    Text {
        id: label
        anchors { left: d.right; leftMargin: 8; verticalCenter: parent.verticalCenter }
        text: pill.text
        color: pill.on ? Theme.text : Theme.textDim
        font { family: Theme.font; pixelSize: 12; weight: Font.DemiBold; letterSpacing: 0.6 }
    }
}
