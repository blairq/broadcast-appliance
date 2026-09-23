// Panel translucido fijo (paginas de edicion y configuracion).
import QtQuick
import BA

Rectangle {
    id: panel
    property string title: ""
    /// Cuánto se mete el contenido desde el borde. Es propiedad porque hay
    /// paneles que no son una tarjeta de configuración: un zócalo de una sola
    /// fila con 16 px de cada lado queda con la fila apretada contra el borde
    /// de abajo. Quien lo usa tiene que poder decirlo, y sobre todo tiene que
    /// poder CONTARLO para calcular su alto.
    property real padding: 16
    default property alias content: body.data
    radius: Theme.radius
    color: Qt.rgba(1, 1, 1, 0.04)
    border.color: Theme.glassBorder

    Text {
        id: head
        visible: panel.title !== ""
        anchors { left: parent.left; top: parent.top; leftMargin: 18; topMargin: 14 }
        text: panel.title
        color: Theme.text
        font { family: Theme.font; pixelSize: 15; weight: Font.DemiBold }
    }
    Item {
        id: body
        anchors { fill: parent; margins: panel.padding; topMargin: panel.title !== "" ? 46 : panel.padding }
    }
}
