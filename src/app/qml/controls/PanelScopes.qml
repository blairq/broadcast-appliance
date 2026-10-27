// Los instrumentos de video (T39): forma de onda, RGB parade, vectorscopio e
// histograma del programa o de una entrada, con cuánto está fuera de rango y el
// legalizador para TV. Se refresca ~5 veces por segundo mientras está abierto.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import BA

Popup {
    id: pop
    property string entrada: ""          // la entrada elegida en la página (opcional)
    property string fuente: ""           // "" = el programa
    property string tipo: "onda"
    property var dato: ({})
    anchors.centerIn: parent
    width: Math.min(parent.width - 80, 760)
    height: Math.min(parent.height - 80, 600)
    modal: false
    padding: 16
    background: Rectangle { color: Theme.panel; radius: Theme.radiusSmall; border.color: Theme.glassBorder }

    function refrescar() { pop.dato = Studio.scopeUi(pop.fuente, pop.tipo, Math.round(vista.width), Math.round(vista.height)) }
    Timer { interval: 200; repeat: true; running: pop.visible; onTriggered: pop.refrescar() }
    onOpened: refrescar()

    // El contenido del Popup (no un hijo anclado a él): con anchors.fill, su alto natural
    // (textos que se parten según el ancho) volvía al del Popup y daba binding loop.
    contentItem: ColumnLayout {
        spacing: 8
        RowLayout {
            Layout.fillWidth: true
            Text { text: qsTr("Scopes"); color: Theme.text; font.pixelSize: 18; font.weight: Font.DemiBold; Layout.fillWidth: true }
            GlassButton { compact: true; text: qsTr("Cerrar"); onClicked: pop.close() }
        }
        RowLayout {
            spacing: 6
            Repeater {
                model: [{ id: "", t: qsTr("Programa") }].concat(pop.entrada !== "" ? [{ id: pop.entrada, t: pop.entrada }] : [])
                delegate: GlassButton { required property var modelData; compact: true; text: modelData.t; active: pop.fuente === modelData.id
                                        onClicked: { pop.fuente = modelData.id; pop.refrescar() } }
            }
            Item { Layout.fillWidth: true }
            Repeater {
                model: [{ id: "onda", t: qsTr("Onda") }, { id: "parade", t: qsTr("Parade") }, { id: "vector", t: qsTr("Vector") }, { id: "histograma", t: qsTr("Histograma") }]
                delegate: GlassButton { required property var modelData; compact: true; text: modelData.t; active: pop.tipo === modelData.id
                                        onClicked: { pop.tipo = modelData.id; pop.refrescar() } }
            }
        }
        Rectangle {
            id: vista
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: "#0a0c0e"
            radius: 6
            Image {
                anchors.fill: parent
                anchors.margins: 2
                fillMode: Image.PreserveAspectFit
                source: pop.dato.imagen || ""
                cache: false
                smooth: true
            }
            Text { anchors.centerIn: parent; visible: !pop.dato.imagen; text: pop.dato.error || qsTr("Sin imagen"); color: Theme.textDim }
        }
        Text {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            readonly property var l: pop.dato.legalidad || {}
            text: qsTr("Negros aplastados %1 % · blancos quemados %2 % · fuera de gama %3 %")
                  .arg(l.negros_aplastados_pct || 0).arg(l.blancos_quemados_pct || 0).arg(l.fuera_de_gama_pct || 0)
            color: (l.fuera_de_gama_pct || 0) > 1 || (l.blancos_quemados_pct || 0) > 5 ? Theme.amber : Theme.textFaint
            font.pixelSize: 11
        }
        RowLayout {
            spacing: 8
            Text { text: qsTr("Legalizar para TV"); color: Theme.textDim; font.pixelSize: 12 }
            ComboBox {
                id: leg
                Layout.preferredWidth: 260
                textRole: "t"
                model: [{ id: "no", t: qsTr("No") }, { id: "pal", t: qsTr("Sí, PAL (+133 %)") }, { id: "ntsc", t: qsTr("Sí, estricto (+120 %)") }, { id: "rango", t: qsTr("Solo el rango") }]
                currentIndex: Math.max(0, model.findIndex(m => m.id === (pop.dato.legalizador || "no")))
                onActivated: (i) => { const e = Studio.legalizarUi(model[i].id); if (e !== "") Studio.notify(e, "error") }
            }
            Text { Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.textFaint; font.pixelSize: 10
                   text: qsTr("Corre en la cadena de GStreamer de cada entrada (balegal): lo que se agrega después en la escena (zócalos, gráficos) no pasa por él.") }
        }
    }
}
