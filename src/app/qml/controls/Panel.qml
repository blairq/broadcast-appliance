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
    // El contenido, nunca con ancho o alto negativos: un panel que todavía no tiene lugar
    // (una pestaña oculta que el layout no midió: ancho 0) le daba -2×padding, y un
    // ColumnLayout con texto que se parte en líneas entraba en un bucle de polish.
    Item {
        id: body
        x: panel.padding
        y: panel.title !== "" ? 46 : panel.padding
        width: Math.max(0, panel.width - 2 * panel.padding)
        height: Math.max(0, panel.height - y - panel.padding)
    }
}
