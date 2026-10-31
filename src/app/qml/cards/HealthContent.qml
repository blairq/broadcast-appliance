// Tarjeta "Sistema": lo que usa la máquina (CPU, GPU, RAM, red: valor, línea de
// los últimos minutos, pico y color por umbral), fps, codificador, frames perdidos.
// Los recursos los mide ui/RecursosViewModel (Studio.recursos) una vez por segundo.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import BA
import "../controls"

Item {
    id: tarjeta
    readonly property var rec: Studio.recursos
    function colorDe(estado) {
        return estado === "critico" ? Theme.live : estado === "aviso" ? Theme.amber
             : estado === "normal" ? Theme.ok : Theme.textFaint
    }

    Flickable {
        anchors.fill: parent
        contentHeight: columna.implicitHeight
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

        ColumnLayout {
            id: columna
            width: parent.width
            spacing: 10

            // --- Recursos de la máquina ---------------------------------------------------
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 4
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 6
                    Rectangle { width: 8; height: 8; radius: 4; color: tarjeta.colorDe(tarjeta.rec ? tarjeta.rec.estado : "") }
                    Text { text: qsTr("Recursos"); color: Theme.textFaint; font.pixelSize: 11 }
                    Item { Layout.fillWidth: true }
                    // Lo que le queda al más cargado: con varias salidas, lo que dice si entra otra.
                    Text {
                        visible: !!tarjeta.rec && tarjeta.rec.margen >= 0
                        text: tarjeta.rec ? qsTr("margen %1 % (%2)").arg(Math.round(tarjeta.rec.margen)).arg(tarjeta.rec.limita) : ""
                        color: tarjeta.colorDe(tarjeta.rec ? tarjeta.rec.estado : "")
                        font.pixelSize: 11
                    }
                }
                Repeater {
                    // Por cantidad (no por la lista): las filas no se rearman cada segundo.
                    model: tarjeta.rec ? tarjeta.rec.filas.length : 0
                    delegate: RowLayout {
                        id: filaRec
                        required property int index
                        readonly property var f: tarjeta.rec.filas[index] || ({})
                        readonly property color tono: tarjeta.colorDe(f.estado)
                        Layout.fillWidth: true
                        spacing: 6
                        onFChanged: chispa.requestPaint()
                        Text { Layout.preferredWidth: 34; text: filaRec.f.titulo || ""; color: Theme.textDim; font.pixelSize: 11 }
                        // La línea de los últimos minutos y, abajo, la barra del valor de ahora.
                        Item {
                            Layout.preferredWidth: 84
                            Layout.preferredHeight: 22
                            Canvas {
                                id: chispa
                                anchors { left: parent.left; right: parent.right; top: parent.top }
                                height: 16
                                onWidthChanged: requestPaint()
                                onPaint: {
                                    const c = getContext("2d")
                                    c.reset()
                                    const techo = filaRec.f.techo || 100
                                    const linea = (datos, color, ancho) => {
                                        if (!datos || datos.length < 2) return
                                        const n = datos.length, paso = width / Math.max(1, (tarjeta.rec ? tarjeta.rec.segundosHistoria : n) - 1)
                                        const x0 = width - (n - 1) * paso
                                        c.strokeStyle = color
                                        c.lineWidth = ancho
                                        c.beginPath()
                                        let abierto = false
                                        for (let i = 0; i < n; ++i) {
                                            const v = datos[i]
                                            if (v < 0) { abierto = false; continue }   // desconocido: un hueco
                                            const x = x0 + i * paso, y = height - 1 - Math.min(1, v / techo) * (height - 2)
                                            if (abierto) c.lineTo(x, y); else c.moveTo(x, y)
                                            abierto = true
                                        }
                                        c.stroke()
                                    }
                                    linea(filaRec.f.historia2, Qt.rgba(1, 1, 1, 0.28), 1)
                                    // Sin umbral (la red sin la velocidad del enlace): la línea, neutra.
                                    linea(filaRec.f.historia, filaRec.f.estado === "desconocido" ? Theme.textDim : filaRec.tono, 1.4)
                                }
                            }
                            Rectangle {
                                anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
                                height: 3; radius: 1.5
                                color: Qt.rgba(1, 1, 1, 0.1)
                                Rectangle {
                                    height: parent.height; radius: parent.radius
                                    visible: (filaRec.f.pct ?? -1) >= 0
                                    width: parent.width * Math.min(1, Math.max(0, filaRec.f.pct || 0) / 100)
                                    color: filaRec.tono
                                    Behavior on width { NumberAnimation { duration: 300 } }
                                }
                            }
                        }
                        Text {
                            id: valorRec
                            Layout.fillWidth: true
                            text: filaRec.f.texto || ""
                            color: filaRec.f.conocido ? Theme.text : Theme.textFaint
                            font.pixelSize: 11; elide: Text.ElideRight
                            // Angosta (la tarjeta chica del tablero): el valor entero y el pico, al pasar el mouse.
                            HoverHandler { id: sobreValor }
                            ToolTip.visible: sobreValor.hovered && (truncated || !picoRec.visible)
                            ToolTip.delay: 300
                            ToolTip.text: (filaRec.f.texto || "") + (filaRec.f.pico ? "  ·  " + filaRec.f.pico : "")
                        }
                        // El pico, solo si entra: si no, empujaba la fila fuera de la tarjeta.
                        Text { id: picoRec; visible: filaRec.width >= 300; text: filaRec.f.pico || ""; color: Theme.textFaint; font.pixelSize: 10 }
                    }
                }
                // Nuestros procesos (la app, el gateway, el launcher): CPU y memoria de cada uno.
                Text {
                    Layout.fillWidth: true
                    visible: !!tarjeta.rec && tarjeta.rec.procesos.length > 0
                    text: tarjeta.rec ? tarjeta.rec.procesos.map(p => p.texto).join("  ·  ") : ""
                    color: Theme.textFaint; font.pixelSize: 10; elide: Text.ElideRight
                }
                // La térmica (F4-05): el nivel y lo que se apagó para priorizar el programa.
                Text {
                    Layout.fillWidth: true
                    visible: !!tarjeta.rec && !!tarjeta.rec.termica.texto && tarjeta.rec.termica.nivel !== "desconocido"
                    text: tarjeta.rec ? qsTr("Térmica: %1").arg(tarjeta.rec.termica.texto || "") : ""
                    color: tarjeta.colorDe(tarjeta.rec ? tarjeta.rec.termica.estado : ""); font.pixelSize: 10; elide: Text.ElideRight
                }
            }

            GridLayout {
                Layout.fillWidth: true
                columns: 2
                columnSpacing: 10; rowSpacing: 6
                Text { text: "FPS"; color: Theme.textFaint; font.pixelSize: 11 }
                Text { text: Studio.fps.toFixed(1); color: Theme.text; font { pixelSize: 15; weight: Font.DemiBold } }
                Text { text: qsTr("Codificador"); color: Theme.textFaint; font.pixelSize: 11 }
                Text { text: Studio.encoder || qsTr("se elige al grabar o transmitir"); color: Studio.encoder ? Theme.text : Theme.textFaint; elide: Text.ElideRight; Layout.fillWidth: true; font.pixelSize: 12 }
                Text { text: qsTr("Perdidos"); color: Theme.textFaint; font.pixelSize: 11 }
                Text { text: Studio.droppedFrames + " / " + Studio.totalFrames; color: Studio.droppedFrames > 0 ? Theme.warn : Theme.text; font.pixelSize: 12 }
                Text { text: qsTr("Lienzo"); color: Theme.textFaint; font.pixelSize: 11 }
                Text { text: Studio.canvasLabel; color: Theme.text; font.pixelSize: 12 }
                Text { text: "Gateway"; color: Theme.textFaint; font.pixelSize: 11 }
                Text { text: Studio.gatewayAlive ? qsTr("en linea") : qsTr("sin respuesta"); color: Studio.gatewayAlive ? Theme.ok : Theme.live; font.pixelSize: 12 }
                // El latido del supervisor: si la interfaz se traba, el motor sigue y el aire no se corta.
                Text { text: qsTr("Interfaz"); color: Theme.textFaint; font.pixelSize: 11 }
                Text {
                    property var l: ({})
                    Timer { interval: 1000; running: parent.visible; repeat: true; triggeredOnStart: true; onTriggered: parent.l = Studio.latidoUi() }
                    text: {
                        if (l.ui_viva === false) return qsTr("trabada %1 s (el aire sigue)").arg(Math.round((l.ui_trabada_ms || 0) / 1000))
                        const partes = [qsTr("viva")]
                        if (l.motor_vivo === false) partes.push(qsTr("motor sin cuadros"))
                        if ((l.ultima_traba_ms || 0) > 0) partes.push(qsTr("última traba %1 s").arg(((l.ultima_traba_ms || 0) / 1000).toFixed(1)))
                        return partes.join(" · ")
                    }
                    color: l.ui_viva === false || l.motor_vivo === false ? Theme.warn : Theme.ok
                    font.pixelSize: 12; elide: Text.ElideRight; Layout.fillWidth: true
                }
                // Alarmas de los disparadores que esperan que alguien las reconozca.
                Text { text: qsTr("Alarmas"); color: Theme.textFaint; font.pixelSize: 11; Layout.alignment: Qt.AlignTop }
                ColumnLayout {
                    id: alarmas
                    Layout.fillWidth: true
                    spacing: 4
                    readonly property var lista: Studio.alarmas.activas
                    Text { visible: alarmas.lista.length === 0; text: qsTr("ninguna"); color: Theme.ok; font.pixelSize: 12 }
                    Repeater {
                        model: alarmas.lista
                        delegate: RowLayout {
                            required property var modelData
                            Layout.fillWidth: true
                            spacing: 6
                            Rectangle { width: 8; height: 8; radius: 4; color: modelData.sonido === "critico" ? Theme.live : Theme.warn }
                            Text { Layout.fillWidth: true; text: modelData.text; color: Theme.text; font.pixelSize: 11; elide: Text.ElideRight }
                            // Sin sonido para este disparador (la alarma sigue acá, con su color).
                            GlassButton {
                                compact: true
                                visible: modelData.sonido !== "ninguno"
                                text: modelData.silenciada ? qsTr("🔇 Silenciada") : qsTr("Silenciar")
                                onClicked: Studio.alarmas.silenciar(modelData.ruleId, !modelData.silenciada)
                            }
                            GlassButton { compact: true; text: qsTr("Reconocer"); onClicked: Studio.alarmas.reconocer(modelData.id) }
                        }
                    }
                    RowLayout {
                        spacing: 6
                        readonly property var al: Studio.alarmas.sonido
                        Text { text: qsTr("Volumen"); color: Theme.textFaint; font.pixelSize: 11 }
                        Slider {
                            Layout.preferredWidth: 110
                            from: 0; to: 1
                            value: parent.al ? parent.al.ajustesUi().volumen : 0.8
                            onPressedChanged: if (!pressed && parent.al) parent.al.ponerAjustesUi({ volumen: value })
                        }
                        GlassButton { compact: true; text: qsTr("Probar"); onClicked: if (parent.al) parent.al.probar("aviso") }
                    }
                }
                Text { text: qsTr("Espera UI"); color: Theme.textFaint; font.pixelSize: 11 }
                RowLayout {
                    spacing: 6
                    SpinBox {
                        from: 10; to: 3600; stepSize: 10; editable: true
                        value: { const p = (Studio.latidoUi().politica || {}); return p.espera_ui_s || 60 }
                        onValueModified: { const e = Studio.latidoPoliticaUi({ espera_ui_s: value }); if (e !== "") Studio.notify(e, "error") }
                        ToolTip.visible: hovered; ToolTip.delay: 400
                        ToolTip.text: qsTr("Con la interfaz trabada y el motor andando, cuánto esperar antes de relanzar la app (solo si no hay transmisión ni grabación)")
                    }
                    Text { text: qsTr("s sin aire"); color: Theme.textFaint; font.pixelSize: 11 }
                }
            }
        }
    }
}
