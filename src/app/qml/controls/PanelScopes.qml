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

    ColumnLayout {
        anchors.fill: parent
        spacing: 8
        RowLayout {
            Layout.fillWidth: true
            Text { text: "Scopes"; color: Theme.text; font.pixelSize: 18; font.weight: Font.DemiBold; Layout.fillWidth: true }
            GlassButton { compact: true; text: "Cerrar"; onClicked: pop.close() }
        }
        RowLayout {
            spacing: 6
            Repeater {
                model: [{ id: "", t: "Programa" }].concat(pop.entrada !== "" ? [{ id: pop.entrada, t: pop.entrada }] : [])
                delegate: GlassButton { required property var modelData; compact: true; text: modelData.t; active: pop.fuente === modelData.id
                                        onClicked: { pop.fuente = modelData.id; pop.refrescar() } }
            }
            Item { Layout.fillWidth: true }
            Repeater {
                model: [{ id: "onda", t: "Onda" }, { id: "parade", t: "Parade" }, { id: "vector", t: "Vector" }, { id: "histograma", t: "Histograma" }]
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
            Text { anchors.centerIn: parent; visible: !pop.dato.imagen; text: pop.dato.error || "Sin imagen"; color: Theme.textDim }
        }
        Text {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            readonly property var l: pop.dato.legalidad || {}
            text: "Negros aplastados " + (l.negros_aplastados_pct || 0) + " % · blancos quemados " + (l.blancos_quemados_pct || 0) +
                  " % · fuera de gama " + (l.fuera_de_gama_pct || 0) + " %"
            color: (l.fuera_de_gama_pct || 0) > 1 || (l.blancos_quemados_pct || 0) > 5 ? Theme.amber : Theme.textFaint
            font.pixelSize: 11
        }
        RowLayout {
            spacing: 8
            Text { text: "Legalizar para TV"; color: Theme.textDim; font.pixelSize: 12 }
            ComboBox {
                id: leg
                Layout.preferredWidth: 260
                textRole: "t"
                model: [{ id: "no", t: "No" }, { id: "pal", t: "Sí, PAL (+133 %)" }, { id: "ntsc", t: "Sí, estricto (+120 %)" }, { id: "rango", t: "Solo el rango" }]
                currentIndex: Math.max(0, model.findIndex(m => m.id === (pop.dato.legalizador || "no")))
                onActivated: (i) => { const e = Studio.legalizarUi(model[i].id); if (e !== "") Studio.notify(e, "error") }
            }
            Text { Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.textFaint; font.pixelSize: 10
                   text: "Corre en la cadena de GStreamer de cada entrada (balegal): lo que se agrega después en la escena (zócalos, gráficos) no pasa por él." }
        }
    }
}
