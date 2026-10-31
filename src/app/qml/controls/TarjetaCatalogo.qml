// La tarjeta de un catálogo (CatalogoBuscable): el marco que se ilumina con el color de
// su categoría al pasar el mouse; lo de adentro lo pone quien la usa.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import BA

Item {
    id: tarjeta
    /// El color de la categoría (el que trae el dato; el de la marca si no trae).
    property color col: Theme.accent
    property real margen: 7
    property real radio: 14
    readonly property bool encima: hov.containsMouse
    default property alias contenido: interior.data
    signal clic()
    signal dobleClic()

    Rectangle {
        anchors { fill: parent; margins: tarjeta.margen }
        radius: tarjeta.radio
        color: hov.containsMouse ? Qt.rgba(1, 1, 1, 0.07) : Qt.rgba(1, 1, 1, 0.035)
        border.color: hov.containsMouse ? tarjeta.col : Qt.rgba(1, 1, 1, 0.08)
        Behavior on border.color { ColorAnimation { duration: 120 } }
        clip: true
        Item { id: interior; anchors.fill: parent }
        MouseArea {
            id: hov
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: tarjeta.clic()
            onDoubleClicked: tarjeta.dobleClic()
        }
    }
}
