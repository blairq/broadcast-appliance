// Sincronizar un canal de audio con su video (sincro labios): el retardo del canal,
// a mano o medido con aplausos. El mismo en la tarjeta del Mezclador, en su ventana
// y en «Sincronía y red» (el modelo es Studio.retardoUi / setRetardoUi, como
// audio_sincro del asistente).
//
// abrir(fuente) lo abre para ese canal; `aplicado` avisa para refrescar la vista.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import BA

Popup {
    id: pop
    property string fuente: ""
    property int ms: 0
    property bool midiendo: false
    property string resultado: ""
    signal aplicado(string fuente, int ms)

    function abrir(f) {
        fuente = f
        ms = Studio.retardoUi(f)
        retMs.value = ms
        resultado = ""
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
               text: "Sincronizar «" + pop.fuente + "» con el video"
               color: Theme.text; font.pixelSize: 15; font.weight: Font.DemiBold }
        Text { Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.textDim; font.pixelSize: 11
               text: "Retardo de este audio respecto de su imagen. Positivo ATRASA el audio (lo normal con una cámara IP o con procesamiento de video, que llegan tarde). Mirá a alguien hablando y corregí hasta que coincidan los labios, o medilo con aplausos." }
        RowLayout {
            // Sin `value: pop.ms` enlazado: el SpinBox editable lo pisaba y entraba en un ciclo.
            SpinBox { id: retMs; from: -5000; to: 5000; stepSize: 10; editable: true; Layout.preferredWidth: 160 }
            Text { text: "ms"; color: Theme.textDim }
            Item { Layout.fillWidth: true }
            GlassButton { compact: true; text: "−40"; onClicked: retMs.value -= 40 }
            GlassButton { compact: true; text: "+40"; onClicked: retMs.value += 40 }
            GlassButton { compact: true; text: "0"; onClicked: retMs.value = 0
                          ToolTip.visible: hovered; ToolTip.text: "Sin retardo" }
        }
        // Medir con aplauso: mira lo que sale al programa y propone el valor.
        RowLayout {
            Layout.fillWidth: true
            GlassButton {
                compact: true; text: pop.midiendo ? "Midiendo… aplaudí" : "Medir con aplauso"
                enabled: !pop.midiendo
                onClicked: {
                    const e = Studio.medirSincroUi(10)
                    if (e !== "") { Studio.notify(e, "error"); return }
                    pop.midiendo = true; pop.resultado = ""
                }
            }
            Text { Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.textFaint; font.pixelSize: 10
                   text: pop.resultado !== "" ? pop.resultado
                         : "Con la cámara de este canal en el programa: 10 s para aplaudir 4 o 5 veces frente a ella, separadas." }
        }
        Connections {
            target: Studio
            function onMedicionSincroLista(r) {
                if (!pop.midiendo) return
                pop.midiendo = false
                if (r.ok) {
                    retMs.value = pop.ms + r.retardo_ms
                    pop.resultado = (r.retardo_ms >= 0 ? "+" : "") + r.retardo_ms + " ms (" + r.aplausos + " aplausos, ±" +
                                    r.dispersion_ms + " ms)" + (r.confiable ? "" : " — poco confiable: repetí")
                } else {
                    pop.resultado = "No se pudo: " + (r.problema || "no encontré aplausos")
                }
            }
        }
        RowLayout {
            Item { Layout.fillWidth: true }
            GlassButton { compact: true; text: "Cancelar"; onClicked: pop.close() }
            GlassButton {
                compact: true; active: true; text: "Aplicar"
                onClicked: {
                    const e = Studio.setRetardoUi(pop.fuente, retMs.value)
                    if (e !== "") { Studio.notify(e, "error"); return }
                    pop.aplicado(pop.fuente, retMs.value)
                    pop.close()
                }
            }
        }
    }
}
