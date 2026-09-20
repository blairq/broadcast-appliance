// Interruptor propio (el Switch del estilo Basic se ve apagado en tema oscuro).
import QtQuick
import BA

Item {
    id: t
    // Se ENLAZA al estado real del modelo: el control no lo escribe (romperia
    // el binding). Un clic solo pide el cambio con toggled(wanted); el
    // interruptor se mueve cuando el modelo lo confirma.
    property bool checked: false
    signal toggled(bool wanted)

    implicitWidth: 42
    implicitHeight: 24
    opacity: enabled ? 1 : 0.4

    Rectangle {
        anchors.fill: parent
        radius: height / 2
        color: t.checked ? Theme.accent : Qt.rgba(1, 1, 1, 0.12)
        border.color: t.checked ? Theme.accentSoft : Qt.rgba(1, 1, 1, 0.18)
        Behavior on color { ColorAnimation { duration: 150 } }
        Rectangle {
            width: parent.height - 6
            height: width
            radius: width / 2
            y: 3
            x: t.checked ? parent.width - width - 3 : 3
            color: "white"
            Behavior on x { SpringAnimation { spring: 6; damping: 0.45 } }
        }
    }
    MouseArea {
        anchors.fill: parent
        cursorShape: Qt.PointingHandCursor
        onClicked: t.toggled(!t.checked)
    }
}
