// Sincronizar un canal de audio con su video (sincro labios): el retardo del canal,
// a mano o medido con aplausos. El mismo en la tarjeta del Mezclador, en su ventana
// y en «Sincronía y red» (el modelo es Studio.mezcla.setRetardoUi, como audio_sincro del
// asistente).
//
// abrir(fuente, ms) lo abre para ese canal con el retardo que tiene (el rol `retardoMs` del
// Mezclador, que muestra quien lo abre: la vista no le pregunta al modelo con una función,
// que en el tablero web contesta tarde); `aplicado` avisa para refrescar la vista.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import BA

Popup {
    id: pop
    property string fuente: ""
    property int ms: 0
    property string resultado: ""
    signal aplicado(string fuente, int ms)
    // La medición es del modelo (Studio.mezcla.medicionSincro, una propiedad: también le llega
    // al tablero web). `pedido`: la pidió este diálogo, que muestra lo que dio.
    property bool pedido: false
    readonly property var med: Studio.mezcla.medicionSincro || ({})
    readonly property bool midiendo: med.midiendo === true
    onMedChanged: {
        if (!pop.pedido || pop.med.midiendo || pop.med.vez === undefined) return
        pop.pedido = false
        const r = pop.med
        if (r.ok) {
            retMs.value = pop.ms + r.retardo_ms
            pop.resultado = (r.confiable ? qsTr("%1 ms (%2 aplausos, ±%3 ms)") : qsTr("%1 ms (%2 aplausos, ±%3 ms) — poco confiable: repetí"))
                            .arg((r.retardo_ms >= 0 ? "+" : "") + r.retardo_ms).arg(r.aplausos).arg(r.dispersion_ms)
        } else {
            pop.resultado = qsTr("No se pudo: %1").arg(r.problema || qsTr("no encontré aplausos"))
        }
    }

    function abrir(f, retardoMs) {
        fuente = f
        ms = retardoMs || 0
        retMs.value = ms
        resultado = ""
        pedido = false
        open()
    }

    parent: Overlay.overlay
    anchors.centerIn: parent
    modal: true
    padding: 16
    width: 400
    background: Rectangle { color: Theme.panel; radius: Theme.radiusSmall; border.color: Theme.glassBorder }

    ColumnLayout {
        anchors.fill: parent
        spacing: 8
        Text { Layout.fillWidth: true; elide: Text.ElideRight
               text: qsTr("Sincronizar «%1» con el video").arg(pop.fuente)
               color: Theme.text; font.pixelSize: 15; font.weight: Font.DemiBold }
        Text { Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.textDim; font.pixelSize: 11
               text: qsTr("Retardo de este audio respecto de su imagen. Positivo ATRASA el audio (lo normal con una cámara IP o con procesamiento de video, que llegan tarde). Mirá a alguien hablando y corregí hasta que coincidan los labios, o medilo con aplausos.") }
        RowLayout {
            // Sin `value: pop.ms` enlazado: el SpinBox editable lo pisaba y entraba en un ciclo.
            SpinBox { id: retMs; from: -5000; to: 5000; stepSize: 10; editable: true; Layout.preferredWidth: 160 }
            Text { text: "ms"; color: Theme.textDim }
            Item { Layout.fillWidth: true }
            GlassButton { compact: true; text: "−40"; onClicked: retMs.value -= 40 }
            GlassButton { compact: true; text: "+40"; onClicked: retMs.value += 40 }
            GlassButton { compact: true; text: "0"; onClicked: retMs.value = 0
                          ToolTip.visible: hovered; ToolTip.text: qsTr("Sin retardo") }
        }
        // Medir con aplauso: mira lo que sale al programa (el de la salida activa) y propone el valor.
        RowLayout {
            Layout.fillWidth: true
            GlassButton {
                compact: true; text: pop.midiendo ? qsTr("Midiendo… aplaudí") : qsTr("Medir con aplauso")
                enabled: !pop.midiendo
                onClicked: {
                    // Si ni empieza (ya se está midiendo), lo avisa el modelo.
                    pop.pedido = true; pop.resultado = ""
                    Studio.mezcla.medirSincroUi(10, Studio.salidas.activa)
                }
            }
            Text { Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.textFaint; font.pixelSize: 10
                   text: pop.resultado !== "" ? pop.resultado
                         : qsTr("Con la cámara de este canal en el programa: 10 s para aplaudir 4 o 5 veces frente a ella, separadas.") }
        }
        RowLayout {
            Item { Layout.fillWidth: true }
            GlassButton { compact: true; text: qsTr("Cancelar"); onClicked: pop.close() }
            GlassButton {
                compact: true; active: true; text: qsTr("Aplicar")
                onClicked: {
                    Studio.mezcla.setRetardoUi(pop.fuente, retMs.value) // si no se pudo, lo avisa el modelo
                    pop.aplicado(pop.fuente, retMs.value)
                    pop.close()
                }
            }
        }
    }
}
