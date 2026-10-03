// «Sincronía y red» (desde la página Video): lo que el asistente hace con
// video_reloj, video_entrada_red, video_sincronia, video_desfase y
// video_estado, para un humano.
//   - El reloj del estudio (sistema, NTP, PTP, el de otro aparato) y publicar
//     el propio; el colchón de las entradas de red.
//   - Las entradas de red y de plugins: cómo llegan (fps, cuadros tarde,
//     reloj, colchón, desfase) y qué dice el diagnóstico, con su arreglo.
//   - Medir cuánto atrasa una de otra y corregirlo; el desfase a mano.
//   - Una entrada de red nueva (SRT, RTMP, RTSP, UDP, HTTP).
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import BA

Popup {
    id: pop
    property var vg: null
    modal: true
    anchors.centerIn: Overlay.overlay
    width: Math.min(1080, (Overlay.overlay ? Overlay.overlay.width : 1100) - 80)
    height: Math.min(820, (Overlay.overlay ? Overlay.overlay.height : 860) - 60)
    padding: 18
    background: Rectangle { color: Theme.panel; radius: Theme.radius; border.color: Theme.glassBorder }

    property var reloj: ({})
    property var red: []
    property var diag: []
    property string medicionTexto: ""
    property bool midiendo: false

    function refrescar() {
        if (!pop.vg) return
        pop.reloj = pop.vg.relojUi()
        pop.red = pop.vg.estadoRed()
        pop.diag = pop.vg.diagnosticoUi()
    }
    function avisar(e, ok) { Studio.notify(e !== "" ? e : ok, e !== "" ? "error" : "info"); pop.refrescar() }
    function colorGravedad(g) { return g === "error" ? Theme.live : g === "aviso" ? Theme.warn : Theme.textDim }
    function entradasTodas() { return pop.vg ? pop.vg.entradas : [] }

    onOpened: { pop.refrescar(); tipoReloj.currentIndex = Math.max(0, ["sistema", "ntp", "ptp", "red"].indexOf(pop.reloj.tipo || "sistema")) }
    Timer { interval: 2000; repeat: true; running: pop.opened; onTriggered: { pop.red = pop.vg.estadoRed(); pop.diag = pop.vg.diagnosticoUi() } }
    Connections {
        target: pop.vg
        function onMedicion(r) {
            pop.midiendo = false
            if (r.error) { pop.medicionTexto = "✕ " + r.error; return }
            let t = (r.significa ? r.significa + ": " : "") + "«" + r.b + "» va " + r.desfase_ms + " ms detrás de «" + r.a + "»"
            if (r.confianza !== undefined) t += " (confianza " + Number(r.confianza).toFixed(2) + ", ±" + (r.resolucion_ms || 0) + " ms)"
            if (r.aplicado) t += "\nCorregido. Medido de nuevo: " + (r.despues ? r.despues.desfase_ms + " ms — " + r.despues.significa : "—")
            if (r.no_aplicado) t += "\nNo se pudo corregir: " + r.no_aplicado
            pop.medicionTexto = t
            pop.refrescar()
        }
    }

    contentItem: ScrollView {
        id: sv
        contentWidth: availableWidth
        clip: true
        ColumnLayout {
            width: sv.availableWidth
            spacing: 14

            RowLayout {
                Layout.fillWidth: true
                Text { text: "Sincronía y red"; color: Theme.text; font { pixelSize: 18; weight: Font.DemiBold } }
                Item { Layout.fillWidth: true }
                GlassButton { compact: true; text: "Cerrar"; onClicked: pop.close() }
            }

            // --- Reloj ------------------------------------------------------------------
            Panel {
                Layout.fillWidth: true
                padding: 12
                implicitHeight: relojCol.implicitHeight + 2 * padding
                ColumnLayout {
                    id: relojCol
                    anchors.fill: parent
                    spacing: 8
                    Text { text: "Reloj del estudio"; color: Theme.text; font { pixelSize: 14; weight: Font.DemiBold } }
                    Text {
                        Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.textDim; font.pixelSize: 11
                        text: "Las entradas de red presentan cada cuadro a su hora de captura + el colchón. Con el mismo reloj en todos los aparatos " +
                              "(NTP, PTP o el de uno publicado) las cámaras RTSP quedan alineadas solas; SRT y RTMP se alinean midiendo (abajo)."
                    }
                    RowLayout {
                        spacing: 8
                        ComboBox {
                            id: tipoReloj
                            Layout.preferredWidth: 220
                            model: ["Sistema (este aparato)", "NTP", "PTP (IEEE 1588)", "El de otro aparato"]
                        }
                        TextField {
                            id: servidor
                            visible: tipoReloj.currentIndex === 1 || tipoReloj.currentIndex === 3
                            Layout.preferredWidth: 200
                            placeholderText: tipoReloj.currentIndex === 1 ? "pool.ntp.org" : "IP del otro aparato"
                            text: pop.reloj.servidor || ""
                        }
                        SpinBox {
                            id: puertoReloj
                            visible: tipoReloj.currentIndex === 3
                            from: 1; to: 65535; editable: true
                            value: pop.reloj.puerto || 8554
                        }
                        SpinBox {
                            id: dominio
                            visible: tipoReloj.currentIndex === 2
                            from: 0; to: 127; editable: true
                            value: pop.reloj.dominio || 0
                        }
                        Text { text: "Colchón"; color: Theme.textDim; font.pixelSize: 12 }
                        SpinBox { id: latencia; from: 0; to: 5000; stepSize: 50; editable: true; value: pop.reloj.latencia_ms || 200 }
                        Text { text: "ms"; color: Theme.textDim; font.pixelSize: 12 }
                        GlassButton {
                            compact: true; text: "Aplicar"; active: true
                            onClicked: {
                                const tipo = ["sistema", "ntp", "ptp", "red"][tipoReloj.currentIndex]
                                const c = { tipo: tipo, latencia_ms: latencia.value }
                                if (tipo === "ntp" || tipo === "red") c.servidor = servidor.text
                                if (tipo === "red") c.puerto = puertoReloj.value
                                if (tipo === "ptp") c.dominio = dominio.value
                                pop.avisar(pop.vg.configurarRelojUi(c), "Reloj del estudio: " + tipo)
                            }
                        }
                    }
                    RowLayout {
                        spacing: 8
                        Toggle {
                            checked: (pop.reloj.publicar_puerto || 0) > 0
                            onToggled: (w) => pop.avisar(pop.vg.configurarRelojUi({ publicar_puerto: w ? publicar.value : 0 }),
                                                         w ? "Publicando el reloj en el puerto " + publicar.value : "Ya no se publica el reloj")
                        }
                        Text { text: "Publicar el reloj de este aparato (para que otros lo usen) en el puerto"; color: Theme.textDim; font.pixelSize: 12 }
                        SpinBox { id: publicar; from: 1; to: 65535; editable: true; value: (pop.reloj.publicar_puerto || 0) > 0 ? pop.reloj.publicar_puerto : 8554 }
                    }
                }
            }

            // --- Entradas de red --------------------------------------------------------
            Panel {
                Layout.fillWidth: true
                padding: 12
                implicitHeight: redCol.implicitHeight + 2 * padding
                ColumnLayout {
                    id: redCol
                    anchors.fill: parent
                    spacing: 6
                    Text { text: "Entradas de red y de plugins"; color: Theme.text; font { pixelSize: 14; weight: Font.DemiBold } }
                    Text {
                        visible: pop.red.length === 0
                        text: "No hay. Agregá una abajo, o buscá cámaras en la red (Entradas → «Buscar cámaras en la red…»)."
                        color: Theme.textFaint; font.pixelSize: 11
                    }
                    Repeater {
                        model: pop.red
                        delegate: ColumnLayout {
                            id: fila
                            required property var modelData
                            readonly property var g: modelData.gstreamer || {}
                            Layout.fillWidth: true
                            spacing: 2
                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 10
                                Text { text: fila.modelData.entrada; color: Theme.text; font { pixelSize: 13; weight: Font.DemiBold } Layout.preferredWidth: 160; elide: Text.ElideRight }
                                StatusPill {
                                    text: fila.g.error ? "sin señal" : (fila.modelData.ancho > 0 ? fila.modelData.ancho + "x" + fila.modelData.alto + " · " + Number(fila.g.fps_recibidos || 0).toFixed(1) + " fps" : "conectando…")
                                    dot: fila.g.error ? Theme.live : fila.modelData.ancho > 0 ? Theme.ok : Theme.amber
                                    medidaDe: "1920x1080 · 30.0 fps"
                                }
                                Text {
                                    Layout.fillWidth: true
                                    text: "colchón " + (fila.g.latencia_ms || 0) + " ms · desfase " + (fila.g.desfase_ms || 0) + " ms · reloj " + (fila.g.reloj || "?") +
                                          (fila.g.reloj_sincronizado === false ? " (sin sincronizar)" : "") +
                                          (fila.g.recibidos_10s > 0 ? " · tarde " + Math.round(100 * (fila.g.tarde_10s || 0) / fila.g.recibidos_10s) + "%" : "") +
                                          (fila.g.normalizado ? " · normalizada" : "")
                                    color: Theme.textDim; font.pixelSize: 11; elide: Text.ElideRight
                                }
                                SpinBox {
                                    id: desf
                                    from: -5000; to: 5000; stepSize: 10; editable: true
                                    value: fila.g.desfase_ms || 0
                                    ToolTip.visible: hovered; ToolTip.delay: 400; ToolTip.text: "Desfase (ms): positivo la atrasa"
                                }
                                GlassButton { compact: true; text: "Desfasar"; onClicked: pop.avisar(pop.vg.desfaseUi(fila.modelData.entrada, desf.value), "Desfase aplicado") }
                            }
                            Repeater {
                                model: fila.modelData.diagnostico || []
                                delegate: Text {
                                    required property var modelData
                                    Layout.fillWidth: true
                                    Layout.leftMargin: 170
                                    text: "• " + modelData.problema + (modelData.arreglo ? "  →  " + modelData.arreglo : "")
                                    color: pop.colorGravedad(modelData.gravedad); font.pixelSize: 11; wrapMode: Text.WordWrap
                                }
                            }
                        }
                    }
                }
            }

            // --- Medir sincronía --------------------------------------------------------
            Panel {
                Layout.fillWidth: true
                padding: 12
                implicitHeight: sincCol.implicitHeight + 2 * padding
                ColumnLayout {
                    id: sincCol
                    anchors.fill: parent
                    spacing: 8
                    Text { text: "Sincronizar dos entradas"; color: Theme.text; font { pixelSize: 14; weight: Font.DemiBold } }
                    Text {
                        Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.textDim; font.pixelSize: 11
                        text: "Compara lo que se ve en las dos durante unos segundos (tiene que haber algo en común que cambie: movimiento, un aplauso, " +
                              "la misma imagen) y dice cuánto atrasa una. «Medir y corregir» la alinea y vuelve a medir."
                    }
                    RowLayout {
                        spacing: 8
                        ComboBox { id: entA; Layout.preferredWidth: 220; model: pop.entradasTodas(); displayText: currentIndex < 0 ? "Referencia…" : currentText; currentIndex: -1 }
                        Text { text: "contra"; color: Theme.textDim; font.pixelSize: 12 }
                        ComboBox { id: entB; Layout.preferredWidth: 220; model: pop.entradasTodas(); displayText: currentIndex < 0 ? "La otra…" : currentText; currentIndex: -1 }
                        GlassButton {
                            compact: true; text: pop.midiendo ? "Midiendo…" : "Medir"
                            enabled: !pop.midiendo && entA.currentIndex >= 0 && entB.currentIndex >= 0 && entA.currentIndex !== entB.currentIndex
                            onClicked: { pop.midiendo = true; pop.medicionTexto = ""; pop.vg.medirUi(entA.currentText, entB.currentText, false) }
                        }
                        GlassButton {
                            compact: true; text: "Medir y corregir"; active: true
                            enabled: !pop.midiendo && entA.currentIndex >= 0 && entB.currentIndex >= 0 && entA.currentIndex !== entB.currentIndex
                            onClicked: { pop.midiendo = true; pop.medicionTexto = ""; pop.vg.medirUi(entA.currentText, entB.currentText, true) }
                        }
                    }
                    Text {
                        visible: pop.medicionTexto !== ""
                        Layout.fillWidth: true
                        text: pop.medicionTexto
                        color: pop.medicionTexto.startsWith("✕") ? Theme.live : Theme.text
                        font.pixelSize: 12; wrapMode: Text.WordWrap
                    }
                }
            }

            // --- Entrada de red nueva ---------------------------------------------------
            Panel {
                Layout.fillWidth: true
                padding: 12
                implicitHeight: nuevaCol.implicitHeight + 2 * padding
                ColumnLayout {
                    id: nuevaCol
                    anchors.fill: parent
                    spacing: 8
                    Text { text: "Entrada de red nueva"; color: Theme.text; font { pixelSize: 14; weight: Font.DemiBold } }
                    Text {
                        Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.textDim; font.pixelSize: 11
                        text: "SRT (srt://:9000?mode=listener para recibir), RTMP, RTSP, UDP o HTTP. Entra sincronizada con el reloj del estudio. " +
                              "«Normalizar» la deja como las demás (desentrelaza, píxel cuadrado, los cuadros del estudio, color BT.709)."
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8
                        TextField { id: nNombre; Layout.preferredWidth: 160; placeholderText: "Nombre" }
                        TextField { id: nUri; Layout.fillWidth: true; placeholderText: "srt://… / rtmp://… / rtsp://…" }
                        ComboBox { id: nEscena; Layout.preferredWidth: 180; editable: true; model: pop.vg ? pop.vg.escenasUi() : []; editText: "" }
                        SpinBox { id: nLat; from: 0; to: 5000; stepSize: 50; editable: true; value: pop.reloj.latencia_ms || 200 }
                        CheckBox { id: nNorm; text: "Normalizar"; checked: true }
                        GlassButton {
                            compact: true; text: "Agregar"; active: true
                            enabled: nNombre.text.trim() !== "" && nUri.text.trim() !== "" && nEscena.editText.trim() !== ""
                            onClicked: {
                                const e = pop.vg.nuevaEntradaRed(nNombre.text, nUri.text, nEscena.editText, nLat.value, nNorm.checked)
                                pop.avisar(e, "Entrada «" + nNombre.text + "» en «" + nEscena.editText + "»")
                                if (e === "") { nNombre.text = ""; nUri.text = "" }
                            }
                        }
                    }
                }
            }

            // --- Lo que dice el diagnóstico de las demás ---------------------------------
            Panel {
                Layout.fillWidth: true
                visible: pop.diag.length > 0
                padding: 12
                implicitHeight: diagCol.implicitHeight + 2 * padding
                ColumnLayout {
                    id: diagCol
                    anchors.fill: parent
                    spacing: 4
                    Text { text: "Diagnóstico de todas las entradas"; color: Theme.text; font { pixelSize: 14; weight: Font.DemiBold } }
                    Repeater {
                        model: pop.diag
                        delegate: ColumnLayout {
                            id: dfila
                            required property var modelData
                            Layout.fillWidth: true
                            spacing: 1
                            Text { text: dfila.modelData.entrada; color: Theme.text; font { pixelSize: 12; weight: Font.DemiBold } }
                            Repeater {
                                model: dfila.modelData.diagnostico
                                delegate: Text {
                                    required property var modelData
                                    Layout.fillWidth: true
                                    Layout.leftMargin: 12
                                    text: "• " + modelData.problema + (modelData.arreglo ? "  →  " + modelData.arreglo : "")
                                    color: pop.colorGravedad(modelData.gravedad); font.pixelSize: 11; wrapMode: Text.WordWrap
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
