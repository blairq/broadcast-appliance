// Tarjeta "Sistema": medidor de CPU + fps, codificador, frames perdidos.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Shapes
import BA
import "../controls"

Item {
    RowLayout {
        anchors.fill: parent
        spacing: 12

        // Medidor circular de CPU
        Item {
            Layout.preferredWidth: Math.min(parent.height, 150)
            Layout.fillHeight: true
            readonly property real frac: Math.min(1, Studio.cpuPercent / 100)
            id: gauge
            Shape {
                anchors.fill: parent
                layer.enabled: true
                layer.samples: 4
                ShapePath {
                    strokeColor: Qt.rgba(1, 1, 1, 0.1); strokeWidth: 10; fillColor: "transparent"; capStyle: ShapePath.RoundCap
                    PathAngleArc { centerX: gauge.width / 2; centerY: gauge.height / 2
                                   radiusX: Math.min(gauge.width, gauge.height) / 2 - 8; radiusY: radiusX
                                   startAngle: 135; sweepAngle: 270 }
                }
                ShapePath {
                    strokeColor: gauge.frac > 0.8 ? Theme.live : (gauge.frac > 0.6 ? Theme.amber : Theme.accent)
                    strokeWidth: 10; fillColor: "transparent"; capStyle: ShapePath.RoundCap
                    PathAngleArc { centerX: gauge.width / 2; centerY: gauge.height / 2
                                   radiusX: Math.min(gauge.width, gauge.height) / 2 - 8; radiusY: radiusX
                                   startAngle: 135; sweepAngle: 270 * gauge.frac
                                   Behavior on sweepAngle { NumberAnimation { duration: 300 } } }
                }
            }
            Column {
                anchors.centerIn: parent
                Text { anchors.horizontalCenter: parent.horizontalCenter; text: Math.round(Studio.cpuPercent) + "%"
                       color: Theme.text; font { family: Theme.font; pixelSize: 26; weight: Font.Light } }
                Text { anchors.horizontalCenter: parent.horizontalCenter; text: "CPU"; color: Theme.textFaint; font.pixelSize: 10 }
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
                property var lista: Studio.alarmasActivasUi()
                Connections { target: Studio; function onAlarmasCambio() { alarmas.lista = Studio.alarmasActivasUi() } }
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
                            onClicked: Studio.silenciarAlarmaUi(modelData.ruleId, !modelData.silenciada)
                        }
                        GlassButton { compact: true; text: qsTr("Reconocer"); onClicked: Studio.reconocerAlarmaUi(modelData.id) }
                    }
                }
                RowLayout {
                    spacing: 6
                    readonly property var al: Studio.alarmaSonora()
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
