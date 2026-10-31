// Un bloque con título y una línea que dice para qué es (las pestañas de
// Asamblea): agrupa lo que va junto en vez de una pila de filas sueltas.
//
//   Seccion { titulo: "Conexión"; subtitulo: "Por dónde habla la app con la central"
//             RowLayout { … } }
import QtQuick
import QtQuick.Layouts
import BA

Rectangle {
    id: sec
    property string titulo: ""
    property string subtitulo: ""
    /// Lo que va a la derecha del título (un botón, un estado).
    property alias extra: extraFila.data
    /// Lo de adentro va directo en la columna (sin otra columna en el medio): cada
    /// layout anidado le suma al de arriba una vuelta más de acomodarse cuando un
    /// texto que corta cambia de alto, y Qt corta en la tercera («polish loop»).
    default property alias contenido: col.data
    /// Resaltada (algo abierto, al aire): el borde de ese color.
    property color resaltado: "transparent"

    Layout.fillWidth: true
    implicitHeight: col.implicitHeight + 24
    radius: Theme.radiusSmall
    color: Qt.rgba(1, 1, 1, 0.03)
    border.color: sec.resaltado.a > 0 ? sec.resaltado : Qt.rgba(1, 1, 1, 0.08)
    border.width: sec.resaltado.a > 0 ? 2 : 1

    ColumnLayout {
        id: col
        anchors { left: parent.left; right: parent.right; top: parent.top; margins: 12; leftMargin: 14; rightMargin: 14 }
        spacing: 8
        RowLayout {
            Layout.fillWidth: true
            visible: sec.titulo !== "" || extraFila.children.length > 0
            spacing: 8
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 1
                Text { visible: sec.titulo !== ""; text: sec.titulo; color: Theme.text; font { pixelSize: 13; weight: Font.DemiBold } }
                Text { Layout.fillWidth: true; visible: sec.subtitulo !== ""; wrapMode: Text.WordWrap
                       text: sec.subtitulo; color: Theme.textFaint; font.pixelSize: 11 }
            }
            RowLayout { id: extraFila; spacing: 8 }
        }
    }
}
