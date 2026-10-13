// Tarjeta "Oradores" del dashboard: quien esta al aire con su reloj grande y
// quien sigue. El detalle y los comandos estan en la pagina Sala.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import BA
import "../controls"

// CON EL TECLADO (un clic en la tarjeta y ya):
//   Enter    pasar al siguiente orador
//
// Una sola tecla y la mas pedida: «que pase el que sigue» se dice en voz alta
// y hay que hacerlo en el momento. Lo demas (mover la fila, cambiar tiempos)
// vive en la pagina Sala, donde hay lugar para pensarlo.
Item {
    id: root
    focus: true
    readonly property var conf: Studio.conference

    Keys.onPressed: (e) => {
        if ((e.key === Qt.Key_Return || e.key === Qt.Key_Enter) && root.conf.speakerMode) {
            root.conf.speakerNext()
            e.accepted = true
        }
    }
    property double clock: Date.now() + conf.clockOffset
    Timer { interval: 250; running: root.visible && (conf.speakers.count > 0 || !!root.bst.al_aire); repeat: true; onTriggered: root.clock = Date.now() + root.conf.clockOffset }
    function fmt(ms) {
        const neg = ms < 0
        const t = Math.floor(Math.abs(ms) / 1000)
        return (neg ? "+" : "") + Math.floor(t / 60) + ":" + String(t % 60).padStart(2, "0")
    }

    // Remotos (los invitados conectados) o Bancas (los presenciales del cuerpo).
    property string vista: "remotos"
    readonly property var ba: Studio.bancas
    readonly property var bst: ba ? ba.estado : ({})

    ColumnLayout {
        anchors.fill: parent
        spacing: 8
        RowLayout {
            spacing: 4
            GlassButton { compact: true; text: "Remotos"; active: root.vista === "remotos"; onClicked: root.vista = "remotos" }
            GlassButton { compact: true; text: "Bancas"; active: root.vista === "bancas"; onClicked: root.vista = "bancas" }
        }
        // --- Bancas: tocar una la suma a la fila; doble toque la arma al aire ----
        ColumnLayout {
            visible: root.vista === "bancas"
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 6
            RowLayout {
                Layout.fillWidth: true
                visible: !!root.bst.al_aire
                Text { text: "🪑 " + (root.bst.al_aire ? root.bst.al_aire.nombre + " · banca " + root.bst.al_aire.banca : "")
                       color: Theme.text; font { pixelSize: 15; weight: Font.DemiBold } elide: Text.ElideRight; Layout.fillWidth: true }
                Text {
                    property double usado: (root.clock - (root.bst.desde_ms || root.clock)) / 1000
                    readonly property double resta: (root.bst.asignado_s || 0) - usado
                    text: fmt(resta * 1000)
                    color: resta < 0 ? Theme.live : resta <= 10 ? Theme.accent : Theme.text
                    font { pixelSize: 28; weight: Font.Black }
                }
                GlassButton { text: "Terminar"; compact: true; onClicked: root.ba.terminar() }
            }
            Text { visible: !!root.bst.aviso; text: root.bst.aviso || ""; color: Theme.amber; font.pixelSize: 11
                   wrapMode: Text.WordWrap; Layout.fillWidth: true }
            RowLayout {
                visible: (root.bst.cola || []).length > 0
                Text { text: "Fila: " + (root.bst.cola || []).map(x => x.banca + " " + x.nombre).join(" · ")
                       color: Theme.textDim; font.pixelSize: 12; elide: Text.ElideRight; Layout.fillWidth: true }
                GlassButton { text: "Siguiente ▶ (armar)"; compact: true
                              onClicked: { const e = root.ba.siguiente("armar"); if (e !== "") Studio.notify(e, "error") } }
            }
            // Seguir al micrófono: cuando se abre el de una banca, se propone (o se arma sola).
            RowLayout {
                id: seguir
                Layout.fillWidth: true
                spacing: 6
                property var e: Studio.seguirMicrofono.estado
                CheckBox {
                    text: "Seguir al micrófono"
                    checked: !!seguir.e.activo
                    onToggled: Studio.seguirMicrofono.configurarUi({ activo: checked })
                }
                ComboBox {
                    implicitWidth: 130
                    enabled: !!seguir.e.activo
                    model: [ { t: "Proponer", v: "proponer" }, { t: "Armar solo", v: "automatico" } ]
                    textRole: "t"
                    currentIndex: seguir.e.modo === "automatico" ? 1 : 0
                    onActivated: (i) => Studio.seguirMicrofono.configurarUi({ modo: model[i].v })
                    ToolTip.visible: hovered; ToolTip.delay: 400
                    ToolTip.text: "Armar solo: la banca queda en el preview; el TAKE sigue siendo tuyo."
                }
                ComboBox {
                    implicitWidth: 150
                    enabled: !!seguir.e.activo
                    model: [ { t: "Por nivel", v: "niveles" }, { t: "Sistema de conferencia", v: "conferencia" } ]
                    textRole: "t"
                    currentIndex: seguir.e.fuente === "conferencia" ? 1 : 0
                    onActivated: (i) => Studio.seguirMicrofono.configurarUi({ fuente: model[i].v })
                    ToolTip.visible: hovered; ToolTip.delay: 400
                    ToolTip.text: "De dónde sale «micrófono abierto»: el nivel de cada micrófono o el sistema de conferencia del recinto (plugin)"
                }
                Item { Layout.fillWidth: true }
            }
            Rectangle {
                visible: !!(Studio.seguirMicrofono.estado.propuesta && Studio.seguirMicrofono.estado.propuesta.banca)
                Layout.fillWidth: true
                implicitHeight: 44
                radius: 10
                color: Qt.rgba(0.96, 0.72, 0.24, 0.16)
                border.color: Theme.amber
                RowLayout {
                    anchors.fill: parent
                    anchors.margins: 8
                    spacing: 6
                    readonly property var p: Studio.seguirMicrofono.estado.propuesta || ({})
                    Text {
                        Layout.fillWidth: true
                        elide: Text.ElideRight
                        text: "Banca " + (parent.p.banca || "") + " – " + (parent.p.nombre || "") + " pidió la palabra"
                        color: Theme.text
                        font.pixelSize: 13
                    }
                    GlassButton {
                        compact: true; text: "Armar"; active: true
                        onClicked: { const e = Studio.seguirMicrofono.aceptarUi(); if (e !== "") Studio.notify(e, "error") }
                    }
                    GlassButton { compact: true; text: "✕"; onClicked: Studio.seguirMicrofono.descartarUi() }
                }
            }
            Text { visible: (root.bst.bancas || []).length === 0; text: "Sin bancas: cargá el cuerpo en Personas → Cuerpo."
                   color: Theme.textFaint; font.pixelSize: 12 }
            GridView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                cellWidth: 92; cellHeight: 56
                model: root.bst.bancas || []
                delegate: Rectangle {
                    required property var modelData
                    width: 86; height: 50; radius: 8
                    readonly property bool hablando: !!root.bst.al_aire && root.bst.al_aire.id === modelData.id
                    readonly property bool enFila: (root.bst.cola || []).some(x => x.id === modelData.id)
                    color: hablando ? Qt.rgba(0.94, 0.27, 0.27, 0.35) : enFila ? Qt.rgba(1, 0.6, 0.1, 0.25) : Qt.rgba(1, 1, 1, 0.06)
                    border.color: hablando ? Theme.live : !modelData.validado_por ? Theme.warn : "transparent"
                    ColumnLayout {
                        anchors.centerIn: parent
                        spacing: 0
                        Text { Layout.alignment: Qt.AlignHCenter; text: modelData.banca; color: Theme.text; font { pixelSize: 14; weight: Font.Bold } }
                        Text { Layout.alignment: Qt.AlignHCenter; Layout.maximumWidth: 80; text: modelData.nombre; color: Theme.textDim
                               font.pixelSize: 9; elide: Text.ElideRight }
                    }
                    TapHandler {
                        onTapped: { const e = root.ba.agregar(modelData.id); if (e !== "") Studio.notify(e, "error") }
                        onDoubleTapped: { const e = root.ba.alAire(modelData.id, "armar"); if (e !== "") Studio.notify(e, "error") }
                    }
                    ToolTip.visible: bancaHov.hovered; ToolTip.delay: 400
                    ToolTip.text: modelData.nombre + (modelData.cargo_oficial ? " · " + modelData.cargo_oficial : "") +
                                  "\nUn toque: a la fila · doble toque: armar al aire" + (!modelData.validado_por ? "\n(sin validar)" : "")
                    HoverHandler { id: bancaHov }
                }
            }
        }
        RowLayout {
            visible: root.vista === "remotos"
            Layout.fillWidth: true
            StatusPill { text: conf.speakerMode ? "Modo oradores" : "Oradores apagado"; dot: conf.speakerMode ? Theme.ok : Theme.textFaint }
            Item { Layout.fillWidth: true }
            GlassButton { visible: conf.speakerMode; text: "Siguiente ▶"; compact: true; onClicked: conf.speakerNext() }
            GlassButton { text: "Abrir sala"; compact: true; onClicked: ApplicationWindow.window.showPage(2) }
        }
        Text {
            visible: root.vista === "remotos" && conf.onAir.length === 0
            text: conf.speakerMode ? "Nadie al aire" : "Activalo en la página Sala"
            color: Theme.textFaint; font.pixelSize: 13
        }
        Repeater {
            model: root.vista === "remotos" ? conf.onAir : []
            delegate: RowLayout {
                required property var modelData
                readonly property double remaining: modelData.allottedSec * 1000 - (clock - modelData.onAirSinceMs)
                Layout.fillWidth: true
                Text { text: modelData.name; color: Theme.text; font { pixelSize: 16; weight: Font.DemiBold } elide: Text.ElideRight; Layout.fillWidth: true }
                Text {
                    text: remaining >= 0 ? fmt(Math.ceil(remaining / 1000) * 1000) : fmt(remaining)
                    color: remaining < 0 ? Theme.live : remaining <= 10000 ? Theme.accent : Theme.text
                    font { pixelSize: remaining <= 10000 ? 40 : 32; weight: Font.Black }
                }
                GlassButton { text: "Sacar"; compact: true; onClicked: conf.speakerOnAir(modelData.uid, false) }
            }
        }
        ListView {
            visible: root.vista === "remotos"
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            spacing: 4
            model: conf.speakers
            delegate: RowLayout {
                required property string uid
                required property string name
                required property string speaker
                required property int position
                required property int allottedSec
                visible: speaker === "queued"
                height: visible ? implicitHeight : 0
                width: ListView.view.width
                Text { text: position + ". " + name; color: position === 1 ? Theme.accentSoft : Theme.textDim; font.pixelSize: 13; elide: Text.ElideRight; Layout.fillWidth: true }
                Text { text: fmt(allottedSec * 1000); color: Theme.textFaint; font.pixelSize: 12 }
                GlassButton { text: "Al aire"; compact: true; onClicked: conf.speakerOnAir(uid, true) }
            }
        }
    }
}
