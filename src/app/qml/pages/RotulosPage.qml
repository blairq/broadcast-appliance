// Rótulos en vivo: los textos de los gráficos de la escena que está al aire.
//
// No es el editor (eso es Gráficos): acá no se diseña nada, se escribe el
// nombre del que está hablando y se lo tira al aire. Por eso está ordenado por
// FUENTE de la escena y no por gráfico, y por eso cada tarjeta tiene el botón
// grande al lado del texto.
//
// Clic derecho en el riel la saca a su propia ventana: en una cabina, esto va
// en la pantalla chica al lado del teclado.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import BA
import "../controls"

Rectangle {
    id: page
    color: Theme.bg
    property real leftInset: 92
    readonly property var r: Studio.rotulador

    ColumnLayout {
        anchors { fill: parent; leftMargin: page.leftInset; margins: 20 }
        anchors.bottomMargin: pedir.visible ? 20 + pedir.height + 14 : 20
        spacing: 14

        RowLayout {
            Layout.fillWidth: true
            spacing: 10
            Text {
                text: "Rótulos"
                color: Theme.text
                font { pixelSize: 20; weight: Font.DemiBold }
            }
            Rectangle {
                radius: 4
                color: Qt.rgba(1, 1, 1, 0.07)
                implicitWidth: esc.implicitWidth + 16
                implicitHeight: 22
                Text {
                    id: esc
                    anchors.centerIn: parent
                    text: page.r.escena === "" ? "sin escena al aire" : "al aire: " + page.r.escena
                    color: Theme.textDim
                    font.pixelSize: 11
                }
            }
            Item { Layout.fillWidth: true }
            GlassButton {
                icon: "↻"; compact: true
                onClicked: page.r.refrescar()
                ToolTip.visible: hovered
                ToolTip.text: "Volver a mirar qué hay en la escena"
            }
        }

        // --- Sin fuentes de gráfico en esta escena ---------------------------
        Panel {
            visible: page.r.fuentes.length === 0
            Layout.fillWidth: true
            Layout.fillHeight: true
            Text {
                anchors.centerIn: parent
                width: parent.width - 80
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
                color: Theme.textDim
                font.pixelSize: 14
                text: "La escena que está al aire no tiene ninguna fuente de «Gráfico animado».\n\n" +
                      "Agregale una en Escenas y fuentes: ubicala donde querés que se vea el zócalo, " +
                      "y después volvé acá a escribir los textos."
            }
        }

        // --- Una tarjeta por fuente -----------------------------------------
        ScrollView {
            visible: page.r.fuentes.length > 0
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            contentWidth: availableWidth

            ColumnLayout {
                width: parent.width
                spacing: 12

                Repeater {
                    model: page.r.fuentes
                    delegate: Panel {
                        required property var modelData
                        id: tarjeta
                        Layout.fillWidth: true
                        implicitHeight: cuerpo.implicitHeight + 28

                        ColumnLayout {
                            id: cuerpo
                            anchors { fill: parent; margins: 14 }
                            spacing: 10

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 10
                                Rectangle {
                                    width: 8; height: 8; radius: 4
                                    color: tarjeta.modelData.enAire ? Theme.live : Qt.rgba(1, 1, 1, 0.2)
                                }
                                Text {
                                    text: tarjeta.modelData.fuente
                                    color: Theme.text
                                    font { pixelSize: 14; weight: Font.DemiBold }
                                }
                                Item { Layout.fillWidth: true }
                                ComboBox {
                                    Layout.preferredWidth: 200
                                    model: page.r.graficos
                                    currentIndex: page.r.graficos.indexOf(tarjeta.modelData.grafico)
                                    font.pixelSize: 12
                                    onActivated: page.r.setGrafico(tarjeta.modelData.id,
                                                                   page.r.graficos[currentIndex])
                                }
                            }

                            // Los campos del gráfico. Se escriben acá y NO salen
                            // al aire hasta el botón: en vivo, que un texto
                            // cambie letra por letra sería un desastre.
                            Repeater {
                                model: tarjeta.modelData.campos
                                delegate: ColumnLayout {
                                    required property var modelData
                                    Layout.fillWidth: true
                                    spacing: 2
                                    Text {
                                        text: modelData.titulo || modelData.clave
                                        color: Theme.textDim
                                        font.pixelSize: 11
                                    }
                                    TextField {
                                        Layout.fillWidth: true
                                        color: Theme.text
                                        font.pixelSize: 14
                                        text: {
                                            const v = tarjeta.modelData.valores[modelData.clave]
                                            return v === undefined ? "" : String(v)
                                        }
                                        onTextEdited: page.r.setValor(tarjeta.modelData.id,
                                                                      modelData.clave, text)
                                        onAccepted: page.r.disparar(tarjeta.modelData.id, false)
                                        ToolTip.visible: hovered && modelData.ayuda !== ""
                                        ToolTip.text: modelData.ayuda
                                        ToolTip.delay: 600
                                    }
                                }
                            }

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 8
                                GlassButton {
                                    text: "Al aire"; icon: "▶"
                                    onClicked: page.r.disparar(tarjeta.modelData.id, false)
                                }
                                GlassButton {
                                    text: "En bucle"; compact: true
                                    onClicked: page.r.disparar(tarjeta.modelData.id, true)
                                    ToolTip.visible: hovered
                                    ToolTip.text: "Para un ticker: se repite hasta que lo saques"
                                }
                                GlassButton {
                                    text: "Sacar"; icon: "✕"; compact: true
                                    enabled: tarjeta.modelData.enAire
                                    onClicked: page.r.ocultar(tarjeta.modelData.id)
                                }
                                Item { Layout.fillWidth: true }
                            }

                            // Los botones de prompt prearmado que trae el
                            // gráfico en su ficha. Son lo que el que diseñó el
                            // gráfico ya sabe que le van a querer pedir.
                            Flow {
                                Layout.fillWidth: true
                                visible: tarjeta.modelData.acciones.length > 0 && pedir.ag.running
                                spacing: 6
                                Repeater {
                                    model: tarjeta.modelData.acciones
                                    delegate: GlassButton {
                                        required property var modelData
                                        compact: true
                                        icon: modelData.icono
                                        text: modelData.titulo
                                        onClicked: pedir.mandar(modelData.prompt, tarjeta.modelData)
                                        ToolTip.visible: hovered
                                        ToolTip.text: modelData.prompt
                                        ToolTip.delay: 500
                                    }
                                }
                            }
                        }
                    }
                }
                Item { Layout.fillWidth: true; implicitHeight: 4 }
            }
        }
    }

    // --- Pedirle al asistente que automatice esto ----------------------------
    Panel {
        id: pedir
        anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
        anchors { leftMargin: page.leftInset + 20; rightMargin: 20; bottomMargin: 20 }
        height: 72
        readonly property var ag: Studio.agent

        RowLayout {
            anchors { fill: parent; margins: 12 }
            spacing: 8
            TextField {
                id: pedido
                Layout.fillWidth: true
                enabled: pedir.ag.running
                placeholderText: pedir.ag.running
                    ? "Pedile al asistente que llene o automatice estos rótulos…"
                    : "El asistente no está corriendo (pestaña Asistente)"
                color: Theme.text
                font.pixelSize: 13
                onAccepted: { pedir.mandar(text, null); pedido.text = "" }
            }
            GlassButton {
                text: "Pedir"; compact: true
                enabled: pedir.ag.running && pedido.text.trim() !== ""
                onClicked: { pedir.mandar(pedido.text, null); pedido.text = "" }
            }
            GlassButton {
                icon: "⧉"; compact: true
                onClicked: ApplicationWindow.window.sacarAVentana(7, "Asistente")
                ToolTip.visible: hovered
                ToolTip.text: "Abrir el Asistente en otra ventana para ver lo que contesta"
            }
        }

        /// Manda el pedido con el contexto puesto. Sin esto el asistente no
        /// sabe de qué escena ni de qué fuente le están hablando, y termina
        /// preguntando o —peor— adivinando.
        function mandar(texto, fuente) {
            const t = String(texto).trim()
            if (t === "" || !ag.running) return
            let ctx = "Estoy en la pantalla de Rótulos. La escena al aire es «" + page.r.escena + "»."
            if (fuente)
                ctx += " Hablo de la fuente de gráfico «" + fuente.fuente + "», que muestra el gráfico «" +
                       fuente.grafico + "» (id " + fuente.id + ")."
            ag.send(ctx + " " + t)
        }
    }
}
