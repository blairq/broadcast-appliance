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
            label: Studio.streaming ? qsTr("AL AIRE") : qsTr("TRANSMISION")
            time: Studio.streamTime
            detail: Studio.streaming ? Studio.streamKbps + " kbps" : (Studio.streamServer || qsTr("sin servidor"))
            on: Studio.streaming
            tone: Theme.live
            action: Studio.streaming ? qsTr("Cortar") : qsTr("Salir al aire")
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
                    Text { text: modelData.respaldo ? qsTr("%1 · respaldo").arg(modelData.nombre) : modelData.nombre; color: Theme.text; font.pixelSize: 11
                           Layout.fillWidth: true; elide: Text.ElideRight }
                    Text { visible: modelData.activo; text: qsTr("%n perdido", "", modelData.perdidos); color: modelData.perdidos > 0 ? Theme.warn : Theme.textFaint; font.pixelSize: 10 }
                    GlassButton {
                        compact: true; text: modelData.activo ? qsTr("Cortar") : qsTr("Iniciar")
                        active: modelData.activo; activeColor: Theme.live
                        onClicked: {
                            if (!modelData.activo) { const e = Studio.iniciarDestinoUi(modelData.id); if (e !== "") Studio.notify(e, "error"); return }
                            const id = modelData.id, n = modelData.nombre
                            ApplicationWindow.window.confirm({ title: qsTr("¿Cortar «%1»?").arg(n), text: qsTr("Los demás destinos siguen."),
                                                               accept: qsTr("Cortar"), reject: qsTr("Cancelar"), action: function() { Studio.detenerDestinoUi(id) } })
                        }
                    }
                    GlassButton { compact: true; text: "✕"; visible: !modelData.activo
                                  onClicked: { const e = Studio.borrarDestinoUi(modelData.id); if (e !== "") Studio.notify(e, "error") } }
                }
            }
            RowLayout {
                spacing: 6
                GlassButton { compact: true; text: qsTr("+ Destino…"); onClicked: nuevoDestino.open() }
                GlassButton { compact: true; visible: destinos.lista.length > 1; text: qsTr("Iniciar todos")
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
            onOpened: { dNombre.text = ""; dUrl.text = ""; dClave.text = ""; dResp.checked = false; dModo.currentIndex = 0; dLat.value = 120 }
            ColumnLayout {
                anchors.fill: parent
                spacing: 8
                Text { text: qsTr("Destino adicional"); color: Theme.text; font { pixelSize: 15; weight: Font.DemiBold } }
                TextField { id: dNombre; Layout.fillWidth: true; placeholderText: qsTr("Nombre (p. ej. YouTube respaldo)") }
                TextField { id: dUrl; Layout.fillWidth: true; placeholderText: qsTr("rtmp://servidor/app · srt://servidor:9000 · rist://servidor:8000") }
                readonly property bool esSrt: dUrl.text.startsWith("srt://")
                TextField { id: dClave; Layout.fillWidth: true; echoMode: TextInput.Password
                            placeholderText: parent.esSrt ? qsTr("Passphrase SRT (10 a 79 caracteres; se guarda cifrada)") : qsTr("Clave (se guarda cifrada; el asistente no la ve)") }
                RowLayout {
                    visible: parent.esSrt
                    spacing: 8
                    Text { text: qsTr("Modo"); color: Theme.textDim; font.pixelSize: 12 }
                    ComboBox { id: dModo; model: ["caller", "listener", "rendezvous"]; Layout.preferredWidth: 130 }
                    Text { text: qsTr("Latencia (ms)"); color: Theme.textDim; font.pixelSize: 12 }
                    SpinBox { id: dLat; from: 20; to: 8000; stepSize: 20; value: 120; editable: true }
                }
                CheckBox { id: dResp; text: qsTr("Es de respaldo") }
                RowLayout {
                    Item { Layout.fillWidth: true }
                    GlassButton { compact: true; text: qsTr("Cancelar"); onClicked: nuevoDestino.close() }
                    GlassButton {
                        compact: true; active: true; text: qsTr("Guardar")
                        onClicked: {
                            const d = { nombre: dNombre.text, url: dUrl.text, clave: dClave.text, respaldo: dResp.checked }
                            if (dUrl.text.startsWith("srt://")) { d.modo = dModo.currentText; d.latencia_ms = dLat.value }
                            const e = Studio.guardarDestinoUi(d)
                            if (e !== "") Studio.notify(e, "error"); else nuevoDestino.close()
                        }
                    }
                }
            }
        }
        // Salud de cada destino que transmite: kbps (con su gráfica del último minuto),
        // cuadros perdidos, congestión y reconexiones.
        Repeater {
            model: Studio.saludSalidas.filter(d => d.activo)
            delegate: RowLayout {
                required property var modelData
                Layout.fillWidth: true
                spacing: 6
                Text { text: modelData.id; color: Theme.textDim; font.pixelSize: 10; Layout.preferredWidth: 64; elide: Text.ElideRight }
                Canvas {
                    id: grafica
                    Layout.preferredWidth: 90
                    Layout.preferredHeight: 18
                    property var datos: modelData.kbps_historial || []
                    onDatosChanged: requestPaint()
                    onPaint: {
                        const ctx = getContext("2d")
                        ctx.reset()
                        if (datos.length < 2) return
                        const max = Math.max(1, ...datos)
                        ctx.strokeStyle = modelData.congestion > 0.3 ? "#ffb300" : "#2ecc71"
                        ctx.lineWidth = 1.5
                        ctx.beginPath()
                        for (let i = 0; i < datos.length; ++i) {
                            const x = i * width / (datos.length - 1), y = height - datos[i] / max * (height - 2) - 1
                            if (i === 0) ctx.moveTo(x, y); else ctx.lineTo(x, y)
                        }
                        ctx.stroke()
                    }
                }
                Text {
                    Layout.fillWidth: true; elide: Text.ElideRight
                    text: {
                        const partes = [qsTr("%1 kbps · perdidos %2 red / %3 cod").arg(modelData.kbps).arg(modelData.perdidos_red).arg(modelData.perdidos_enc)]
                        if (modelData.congestion > 0) partes.push(qsTr("congestión %1%").arg(Math.round(modelData.congestion * 100)))
                        if (modelData.reconexiones > 0) partes.push(qsTr("%n reconexión", "", modelData.reconexiones))
                        if (modelData.reconectando) partes.push(qsTr("RECONECTANDO"))
                        return partes.join(" · ")
                    }
                    color: modelData.reconectando ? Theme.live : modelData.congestion > 0.3 || modelData.perdidos_red > 0 ? Theme.amber : Theme.textFaint
                    font.pixelSize: 10
                }
            }
        }
        // ISO: las entradas que se graban aparte.
        RowLayout {
            id: isos
            Layout.fillWidth: true
            property var lista: Studio.isosUi()
            Timer { interval: 3000; running: isos.visible; repeat: true; triggeredOnStart: true; onTriggered: isos.lista = Studio.isosUi() }
            Text {
                Layout.fillWidth: true; elide: Text.ElideRight
                text: isos.lista.length === 0 ? qsTr("ISO: ninguna entrada se graba aparte")
                      : qsTr("ISO: %1 grabando · %2").arg(isos.lista.length)
                            .arg(isos.lista.map(g => g.pausada ? qsTr("%1 %2 MB (en pausa)").arg(g.fuente).arg(Math.round(g.mb))
                                                               : qsTr("%1 %2 MB").arg(g.fuente).arg(Math.round(g.mb))).join(", "))
                color: isos.lista.length > 0 ? Theme.live : Theme.textFaint
                font.pixelSize: 10
            }
            GlassButton { compact: true; text: qsTr("Todas"); onClicked: { const e = Studio.isoTodasUi(true); if (e !== "") Studio.notify(e, "warn"); isos.lista = Studio.isosUi() } }
            GlassButton { compact: true; text: qsTr("Ninguna"); enabled: isos.lista.length > 0; onClicked: { Studio.isoTodasUi(false); isos.lista = Studio.isosUi() } }
        }
        // Señal limpia: el programa sin DSK ni placa, a un destino guardado o a disco.
        RowLayout {
            id: limpia
            Layout.fillWidth: true
            property var e: Studio.limpiaUi()
            Timer { interval: 2000; running: limpia.visible; repeat: true; triggeredOnStart: true; onTriggered: limpia.e = Studio.limpiaUi() }
            Text {
                Layout.fillWidth: true; elide: Text.ElideRight
                text: {
                    if (!limpia.e.activa) return qsTr("Señal limpia (sin zócalos ni placa): apagada")
                    const partes = [qsTr("Señal limpia → %1").arg(limpia.e.destino)]
                    if (limpia.e.reconectando) partes.push(qsTr("RECONECTANDO"))
                    if (limpia.e.perdidos > 0) partes.push(qsTr("%n perdido", "", limpia.e.perdidos))
                    return partes.join(" · ")
                }
                color: limpia.e.activa ? (limpia.e.reconectando ? Theme.amber : Theme.live) : Theme.textFaint
                font.pixelSize: 10
            }
            ComboBox {
                id: limpiaA
                visible: !limpia.e.activa
                Layout.preferredWidth: 150
                textRole: "nombre"
                model: [{ id: "", nombre: qsTr("A disco") }].concat(destinos.lista.map(d => ({ id: d.id, nombre: d.nombre })))
            }
            GlassButton {
                compact: true; text: limpia.e.activa ? qsTr("Cortar") : qsTr("Arrancar")
                onClicked: {
                    const x = Studio.limpiaActivarUi(!limpia.e.activa, limpia.e.activa ? "" : limpiaA.model[limpiaA.currentIndex].id)
                    if (x !== "") Studio.notify(x, "error")
                    limpia.e = Studio.limpiaUi()
                }
            }
        }
        // La grabación (T29): calidad, pistas, copia en otro disco, encoder de respaldo y «Repetir».
        RowLayout {
            id: grab
            Layout.fillWidth: true
            property var g: Studio.grabacionUi()
            Timer { interval: 2000; running: grab.visible; repeat: true; triggeredOnStart: true; onTriggered: grab.g = Studio.grabacionUi() }
            Text {
                Layout.fillWidth: true; elide: Text.ElideRight
                text: {
                    const partes = [grab.g.calidad === "archivo" ? qsTr("Calidad de archivo") : qsTr("Como la transmisión")]
                    if (grab.g.pistas && grab.g.pistas.length > 1) partes.push(qsTr("pistas %1").arg(grab.g.pistas.join("+")))
                    if (grab.g.copia) partes.push(grab.g.copia_activa ? qsTr("copia doble ✓") : qsTr("¡la copia se cortó!"))
                    if (grab.g.encoder_respaldo_activo) partes.push(qsTr("codificador de respaldo"))
                    if (grab.g.replay && grab.g.replay.activa) partes.push(qsTr("repetición %1 s").arg(grab.g.replay.segundos))
                    return partes.join(" · ")
                }
                color: (grab.g.copia && !grab.g.copia_activa && grab.g.activa) || grab.g.encoder_respaldo_activo ? Theme.amber : Theme.textFaint
                font.pixelSize: 10
            }
            GlassButton {
                compact: true
                visible: grab.g.replay && grab.g.replay.activa
                text: qsTr("⟲ Repetir %1 s").arg(grab.g.replay ? grab.g.replay.segundos : 0)
                onClicked: { const e = Studio.repetirUi(); if (e !== "") Studio.notify(e, "error") }
            }
        }
        // El disco a este bitrate y el pase a MP4 al terminar.
        Text {
            id: disco
            property var d: Studio.discoUi()
            property var r: Studio.remuxUi()
            Timer { interval: 5000; running: disco.visible; repeat: true; triggeredOnStart: true; onTriggered: { disco.d = Studio.discoUi(); disco.r = Studio.remuxUi() } }
            Layout.fillWidth: true; elide: Text.ElideRight
            text: {
                const partes = [d.horas >= 0 ? qsTr("Disco: %1 h %2 min a este bitrate").arg(Math.floor(d.horas)).arg(Math.round((d.horas % 1) * 60))
                                             : qsTr("Disco: ?")]
                if (r.andando) partes.push(qsTr("pasando a %1 %2%").arg(String(r.destino).split(".").pop().toUpperCase()).arg(Math.round(r.pct)))
                else if (r.destino && r.ok) partes.push(qsTr("último pase a MP4 listo"))
                else if (r.destino && r.error) partes.push(qsTr("el pase a MP4 falló: %1").arg(r.error))
                return partes.join(" · ")
            }
            color: d.horas >= 0 && d.horas < 0.25 ? Theme.live : d.horas >= 0 && d.horas < 2 ? Theme.amber : Theme.textFaint
            font.pixelSize: 10
        }
        OutputRow {
            label: Studio.recording ? qsTr("GRABANDO") : qsTr("GRABACION")
            time: Studio.recordTime
            detail: ""
            on: Studio.recording
            tone: Theme.accent
            action: Studio.recording ? qsTr("Detener") : qsTr("Grabar")
            glyph: Studio.recording ? "■" : "●"
            // Grabar arranca directo; detener pide confirmacion (un clic cortaba el respaldo).
            onTriggered: {
                if (!Studio.recording) { Studio.toggleRecording(); return }
                ApplicationWindow.window.confirm({
                    title: qsTr("¿Detener la grabación?"),
                    text: Studio.streaming ? qsTr("El archivo se cierra y lo que siga no queda grabado. La transmisión sigue al aire.")
                                           : qsTr("El archivo se cierra y lo que siga no queda grabado."),
                    accept: qsTr("Detener"),
                    reject: qsTr("Seguir grabando"),
                    // toggleRecording alterna: si ya se detuvo sola con el dialogo abierto, no volver a grabar.
                    action: function() { if (Studio.recording) Studio.toggleRecording() }
                })
            }
        }
    }
}
