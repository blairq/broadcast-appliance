// Tarjeta "Salidas": al aire y grabacion.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import BA
import "../controls"

Item {
    ColumnLayout {
        anchors.fill: parent
        spacing: 8

        component OutputRow: Rectangle {
            id: row
            property string label
            property string time
            property string detail
            property bool on
            property color tone
            property string action
            property string glyph
            signal triggered()
            Layout.fillWidth: true
            Layout.fillHeight: true
            radius: 14
            color: on ? Qt.rgba(tone.r, tone.g, tone.b, 0.22) : Qt.rgba(1, 1, 1, 0.05)
            border.color: on ? Qt.rgba(tone.r, tone.g, tone.b, 0.6) : Qt.rgba(1, 1, 1, 0.08)
            Behavior on color { ColorAnimation { duration: 200 } }
            RowLayout {
                anchors { fill: parent; leftMargin: 12; rightMargin: 10 }
                spacing: 8
                ColumnLayout {
                    spacing: 0
                    Layout.fillWidth: true
                    RowLayout {
                        spacing: 8
                        Text { text: row.label; color: row.on ? row.tone : Theme.textDim
                               font { family: Theme.font; pixelSize: 11; weight: Font.Bold; letterSpacing: 1 } }
                        Text { text: row.detail; color: Theme.textFaint; elide: Text.ElideRight; Layout.fillWidth: true; font.pixelSize: 10 }
                    }
                    Text { text: row.time; color: Theme.text; font { family: Theme.font; pixelSize: 22; weight: Font.Light } }
                }
                GlassButton { text: row.action; icon: row.glyph; compact: true; active: row.on; activeColor: row.tone; onClicked: row.triggered() }
            }
        }

        OutputRow {
            label: Studio.streaming ? "AL AIRE" : "TRANSMISION"
            time: Studio.streamTime
            detail: Studio.streaming ? Studio.streamKbps + " kbps" : (Studio.streamServer || "sin servidor")
            on: Studio.streaming
            tone: Theme.live
            action: Studio.streaming ? "Cortar" : "Salir al aire"
            onTriggered: Studio.streaming ? ApplicationWindow.window.confirmStopStream() : ApplicationWindow.window.openStreamDialog()
        }
        // Destinos adicionales (respaldo, otras plataformas): mismos codificadores.
        ColumnLayout {
            id: destinos
            Layout.fillWidth: true
            spacing: 3
            property var lista: Studio.destinosUi()
            function refrescar() { lista = Studio.destinosUi() }
            Connections { target: Studio; function onDestinosChanged() { destinos.refrescar() } }
            Timer { interval: 2000; running: destinos.lista.some(d => d.activo); repeat: true; onTriggered: destinos.refrescar() }
            Repeater {
                model: destinos.lista
                delegate: RowLayout {
                    required property var modelData
                    Layout.fillWidth: true
                    spacing: 6
                    Rectangle { width: 8; height: 8; radius: 4
                                color: modelData.activo ? (modelData.reconectando ? Theme.amber : Theme.live) : Theme.textFaint }
                    Text { text: modelData.nombre + (modelData.respaldo ? " · respaldo" : ""); color: Theme.text; font.pixelSize: 11
                           Layout.fillWidth: true; elide: Text.ElideRight }
                    Text { visible: modelData.activo; text: modelData.perdidos + " perdidos"; color: modelData.perdidos > 0 ? Theme.warn : Theme.textFaint; font.pixelSize: 10 }
                    GlassButton {
                        compact: true; text: modelData.activo ? "Cortar" : "Iniciar"
                        active: modelData.activo; activeColor: Theme.live
                        onClicked: {
                            if (!modelData.activo) { const e = Studio.iniciarDestinoUi(modelData.id); if (e !== "") Studio.notify(e, "error"); return }
                            const id = modelData.id, n = modelData.nombre
                            ApplicationWindow.window.confirm({ title: "¿Cortar «" + n + "»?", text: "Los demás destinos siguen.",
                                                               accept: "Cortar", reject: "Cancelar", action: function() { Studio.detenerDestinoUi(id) } })
                        }
                    }
                    GlassButton { compact: true; text: "✕"; visible: !modelData.activo
                                  onClicked: { const e = Studio.borrarDestinoUi(modelData.id); if (e !== "") Studio.notify(e, "error") } }
                }
            }
            RowLayout {
                spacing: 6
                GlassButton { compact: true; text: "+ Destino…"; onClicked: nuevoDestino.open() }
                GlassButton { compact: true; visible: destinos.lista.length > 1; text: "Iniciar todos"
                              onClicked: { const e = Studio.iniciarDestinoUi("todos"); if (e !== "") Studio.notify(e, "error") } }
            }
        }
        Popup {
            id: nuevoDestino
            parent: Overlay.overlay
            anchors.centerIn: parent
            modal: true
            padding: 16
            width: 460
            background: Rectangle { color: Theme.panel; radius: Theme.radiusSmall; border.color: Theme.glassBorder }
            onOpened: { dNombre.text = ""; dUrl.text = ""; dClave.text = ""; dResp.checked = false }
            ColumnLayout {
                anchors.fill: parent
                spacing: 8
                Text { text: "Destino adicional"; color: Theme.text; font { pixelSize: 15; weight: Font.DemiBold } }
                TextField { id: dNombre; Layout.fillWidth: true; placeholderText: "Nombre (p. ej. YouTube respaldo)" }
                TextField { id: dUrl; Layout.fillWidth: true; placeholderText: "rtmp://servidor/app" }
                TextField { id: dClave; Layout.fillWidth: true; placeholderText: "Clave (se guarda cifrada; el asistente no la ve)"; echoMode: TextInput.Password }
                CheckBox { id: dResp; text: "Es de respaldo" }
                RowLayout {
                    Item { Layout.fillWidth: true }
                    GlassButton { compact: true; text: "Cancelar"; onClicked: nuevoDestino.close() }
                    GlassButton {
                        compact: true; active: true; text: "Guardar"
                        onClicked: {
                            const e = Studio.guardarDestinoUi({ nombre: dNombre.text, url: dUrl.text, clave: dClave.text, respaldo: dResp.checked })
                            if (e !== "") Studio.notify(e, "error"); else nuevoDestino.close()
                        }
                    }
                }
            }
        }
        OutputRow {
            label: Studio.recording ? "GRABANDO" : "GRABACION"
            time: Studio.recordTime
            detail: ""
            on: Studio.recording
            tone: Theme.accent
            action: Studio.recording ? "Detener" : "Grabar"
            glyph: Studio.recording ? "■" : "●"
            // Grabar arranca directo; detener pide confirmacion (un clic cortaba el respaldo).
            onTriggered: {
                if (!Studio.recording) { Studio.toggleRecording(); return }
                ApplicationWindow.window.confirm({
                    title: "¿Detener la grabación?",
                    text: "El archivo se cierra y lo que siga no queda grabado."
                          + (Studio.streaming ? " La transmisión sigue al aire." : ""),
                    accept: "Detener",
                    reject: "Seguir grabando",
                    // toggleRecording alterna: si ya se detuvo sola con el dialogo abierto, no volver a grabar.
                    action: function() { if (Studio.recording) Studio.toggleRecording() }
                })
            }
        }
    }
}
