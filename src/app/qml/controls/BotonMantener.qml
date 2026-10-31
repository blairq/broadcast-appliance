// Un botón que actúa mientras se lo tiene apretado (el zoom y el foco del PTZ):
// `presionado` al apretar y `soltado` al soltar o si se pierde el toque.
import QtQuick
import BA

Rectangle {
    id: b
    property string text: ""
    signal presionado()
    signal soltado()
    implicitWidth: 96; implicitHeight: 30
    radius: 10
    color: area.pressed ? Theme.accent : Qt.rgba(1, 1, 1, 0.08)
    border.color: Qt.rgba(1, 1, 1, 0.16)
    Text { anchors.centerIn: parent; text: b.text; color: Theme.text; font.pixelSize: 12 }
    MouseArea {
        id: area
        anchors.fill: parent
        onPressed: b.presionado()
        onReleased: b.soltado()
        onCanceled: b.soltado()
    }
}
