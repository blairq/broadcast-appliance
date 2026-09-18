// Vumetro en dBFS (-60..0) con zonas verde / ambar / rojo.
import QtQuick
import BA

Item {
    id: m
    property real db: -100
    property bool vertical: false
    readonly property real frac: Math.max(0, Math.min(1, (db + 60) / 60))

    implicitWidth: vertical ? 8 : 120
    implicitHeight: vertical ? 120 : 8

    Rectangle {
        anchors.fill: parent
        radius: Math.min(width, height) / 2
        color: Qt.rgba(1, 1, 1, 0.08)
    }
    Rectangle {
        radius: Math.min(width, height) / 2
        width: m.vertical ? m.width : m.width * m.frac
        height: m.vertical ? m.height * m.frac : m.height
        anchors { left: parent.left; bottom: parent.bottom }
        gradient: Gradient {
            orientation: m.vertical ? Gradient.Vertical : Gradient.Horizontal
            GradientStop { position: 0.0; color: m.vertical ? "#ff3b30" : "#35c46b" }
            GradientStop { position: 0.25; color: m.vertical ? "#f5b83d" : "#35c46b" }
            GradientStop { position: 0.7; color: m.vertical ? "#35c46b" : "#f5b83d" }
            GradientStop { position: 1.0; color: m.vertical ? "#35c46b" : "#ff3b30" }
        }
        Behavior on width { NumberAnimation { duration: 90 } }
        Behavior on height { NumberAnimation { duration: 90 } }
    }
}
