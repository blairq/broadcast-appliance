// El esquema de una distribución de pantallas: sus ventanas como rectángulos con lo
// que muestra cada una, en la proporción del rectángulo de los monitores que usa. Es
// la vista previa de una distribución en el selector (SelectorCosas) y en la lista de
// distribuciones. No dibuja video: es un dibujo, liviano aunque haya cientos.
//
// `dibujo` es el de la cosa del catálogo (pantallas/Catalogo.hpp, modo
// «distribucion»): {proporcion, piezas:[{titulo, x, y, ancho, alto}]} (0..1).
import QtQuick
import BA

Item {
    id: e
    property var dibujo: null
    /// Con el texto de cada ventana (en una miniatura muy chica, no).
    property bool conTitulos: true

    readonly property real prop: Math.max(0.2, e.dibujo && e.dibujo.proporcion ? e.dibujo.proporcion : 16 / 9)
    readonly property real aw: Math.min(e.width, e.height * e.prop)
    readonly property real ah: e.aw / e.prop
    readonly property real ax: (e.width - e.aw) / 2
    readonly property real ay: (e.height - e.ah) / 2

    Rectangle {
        x: e.ax; y: e.ay; width: e.aw; height: e.ah
        color: "black"
        border.color: Qt.rgba(1, 1, 1, 0.22)
        radius: 3
    }
    Repeater {
        model: e.dibujo && e.dibujo.piezas ? e.dibujo.piezas : []
        delegate: Rectangle {
            id: pieza
            required property var modelData
            required property int index
            x: e.ax + modelData.x * e.aw + 1
            y: e.ay + modelData.y * e.ah + 1
            width: Math.max(4, modelData.ancho * e.aw - 2)
            height: Math.max(4, modelData.alto * e.ah - 2)
            radius: 2
            color: Qt.rgba(1, 0.42, 0.17, 0.22)
            border.color: Theme.accent
            border.width: 1
            Text {
                visible: e.conTitulos && pieza.height > 14
                anchors { fill: parent; margins: 3 }
                text: (pieza.index + 1) + ". " + (pieza.modelData.titulo || "")
                color: Theme.text; font.pixelSize: 10
                elide: Text.ElideRight; wrapMode: Text.WordWrap
            }
        }
    }
    Text {
        anchors.centerIn: parent
        visible: !e.dibujo || !e.dibujo.piezas || e.dibujo.piezas.length === 0
        text: qsTr("Sin ventanas")
        color: Theme.textFaint; font.pixelSize: 11
    }
}
