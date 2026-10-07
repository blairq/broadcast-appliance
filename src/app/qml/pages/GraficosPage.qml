// Editor de gráficos animados: la lista, el código, la vista previa y la
// barra de tiempo. Solo presentación: todo sale de Studio.grafico, que es el
// MISMO motor que usa el asistente por MCP.
//
// La vista previa se dibuja con lo que hay en el editor, no con el archivo
// guardado: se ve lo que se está escribiendo.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import BA
import "../controls"

Rectangle {
    id: page
    color: Theme.bg
    property real leftInset: 92
    readonly property var g: Studio.grafico

    function mmss(ms) {
        const s = Math.round(ms / 100) / 10
        return s.toFixed(1) + " s"
    }

    ColumnLayout {
        anchors { fill: parent; leftMargin: page.leftInset; margins: 20 }
        spacing: 14

        // --- Sin motor -------------------------------------------------------
        Panel {
            visible: !page.g.disponible
            Layout.fillWidth: true
            Layout.fillHeight: true
            Text {
                anchors.centerIn: parent
                width: parent.width - 80
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
                color: Theme.textDim
                font.pixelSize: 14
                text: "Este bundle no trae el renderizador de gráficos (ba-grafico).\n" +
                      "Sin él no se pueden ver ni usar gráficos animados."
            }
        }

        RowLayout {
            visible: page.g.disponible
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 14

            // ---- Los gráficos que hay ----------------------------------------
            Panel {
                Layout.preferredWidth: 230
                Layout.fillHeight: true
                title: "Gráficos"
                ColumnLayout {
                    anchors.fill: parent
                    spacing: 8
                    ListView {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        spacing: 3
                        model: page.g.graficos
                        delegate: Item {
                          id: filaG
                          required property var modelData
                          required property int index
                          // El título de la categoría arriba del primero de cada una.
                          readonly property bool primeroDeSuCategoria: index === 0 ||
                              page.g.graficos[index - 1].categoria !== modelData.categoria
                          width: ListView.view.width
                          height: fichaG.height + (primeroDeSuCategoria ? 22 : 0)
                          Text {
                            visible: filaG.primeroDeSuCategoria
                            y: 6; x: 4
                            text: (filaG.modelData.categoria || "General").toUpperCase()
                            color: Theme.textFaint
                            font { pixelSize: 10; weight: Font.DemiBold; letterSpacing: 1.5 }
                          }
                          Rectangle {
                            id: fichaG
                            readonly property var modelData: filaG.modelData
                            y: filaG.primeroDeSuCategoria ? 22 : 0
                            width: parent.width
                            height: 44
                            radius: 5
                            color: modelData.nombre === page.g.abierto ? Qt.rgba(1, 1, 1, 0.10)
                                 : (ma.containsMouse ? Qt.rgba(1, 1, 1, 0.05) : "transparent")
                            ColumnLayout {
                                anchors { fill: parent; leftMargin: 10; rightMargin: 8 }
                                spacing: 0
                                Text {
                                    Layout.fillWidth: true
                                    elide: Text.ElideRight
                                    text: modelData.titulo
                                    color: Theme.text
                                    font { pixelSize: 13; weight: Font.Medium }
                                }
                                // Qué es (antes decía «descripción» a secas: el tipo, que parecía un texto que faltaba).
                                Text {
                                    Layout.fillWidth: true
                                    elide: Text.ElideRight
                                    text: modelData.descripcion
                                          ? modelData.descripcion
                                          : (modelData.declarativo ? "armado con capas, sin código" : "escrito en QML")
                                    color: Theme.textFaint
                                    font.pixelSize: 10
                                    ToolTip.visible: maDesc.containsMouse && truncated
                                    ToolTip.text: text
                                    MouseArea { id: maDesc; anchors.fill: parent; hoverEnabled: true; acceptedButtons: Qt.NoButton }
                                }
                            }
                            MouseArea {
                                id: ma
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: page.g.abrir(modelData.nombre)
                            }
                          }
                        }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 6
                        GlassButton {
                            text: "+ Descripción"; compact: true
                            Layout.fillWidth: true
                            onClicked: nuevoDlg.abrir(true)
                            ToolTip.visible: hovered
                            ToolTip.text: "Un gráfico sin código: capas y animaciones de un repertorio"
                        }
                        GlassButton {
                            text: "+ QML"; compact: true
                            onClicked: nuevoDlg.abrir(false)
                            ToolTip.visible: hovered
                            ToolTip.text: "Para lo que el repertorio no alcance"
                        }
                    }
                }
            }

            // ---- El editor ----------------------------------------------------
            Panel {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.minimumWidth: 320
                title: page.g.abierto === "" ? "Editor"
                     : (page.g.abierto + (page.g.sucio ? " — sin guardar" : ""))

                ColumnLayout {
                    anchors.fill: parent
                    spacing: 8

                    Text {
                        visible: page.g.abierto === ""
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                        wrapMode: Text.WordWrap
                        color: Theme.textFaint
                        font.pixelSize: 13
                        text: "Elegí un gráfico de la izquierda, o creá uno.\n\n" +
                              "Los de «descripción» se arman con capas y animaciones de un " +
                              "repertorio, sin escribir código."
                    }

                    ScrollView {
                        visible: page.g.abierto !== ""
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        TextArea {
                            id: editor
                            text: page.g.fuente
                            color: Theme.text
                            font { family: Theme.mono; pixelSize: 12 }
                            selectByMouse: true
                            wrapMode: TextArea.NoWrap
                            background: Rectangle { color: Qt.rgba(0, 0, 0, 0.30); radius: 4 }
                            // Se manda al motor al soltar la tecla, no en cada
                            // pulsación: redibujar por letra no lo aguanta nadie.
                            onTextChanged: reboteEdicion.restart()
                            Timer {
                                id: reboteEdicion
                                interval: 350
                                onTriggered: page.g.fuente = editor.text
                            }
                            Connections {
                                target: page.g
                                function onAbiertoChanged() { editor.text = page.g.fuente }
                            }
                        }
                    }

                    RowLayout {
                        visible: page.g.abierto !== ""
                        Layout.fillWidth: true
                        spacing: 6
                        GlassButton {
                            text: "Guardar"; compact: true
                            active: page.g.sucio
                            onClicked: page.g.guardar()
                        }
                        Item { Layout.fillWidth: true }
                        Text {
                            visible: page.g.error !== ""
                            Layout.fillWidth: true
                            Layout.maximumWidth: 420
                            wrapMode: Text.WordWrap
                            text: page.g.error
                            color: Theme.live
                            font.pixelSize: 11
                        }
                    }
                }
            }

            // ---- La vista previa y los parámetros ------------------------------
            ColumnLayout {
                Layout.preferredWidth: 430
                Layout.maximumWidth: 430
                Layout.fillHeight: true
                spacing: 14

                Panel {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 320
                    title: "Cómo se ve"
                    ColumnLayout {
                        anchors.fill: parent
                        spacing: 8

                        // Damero: sin esto no se ve qué parte es transparente,
                        // que es justo lo que hay que mirar en un gráfico.
                        Rectangle {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            radius: 4
                            color: "#20242a"
                            Canvas {
                                anchors.fill: parent
                                onPaint: {
                                    const c = getContext("2d")
                                    const p = 12
                                    for (let y = 0; y < height; y += p)
                                        for (let x = 0; x < width; x += p) {
                                            c.fillStyle = ((x / p + y / p) % 2 === 0) ? "#20242a" : "#272c33"
                                            c.fillRect(x, y, p, p)
                                        }
                                }
                            }
                            Image {
                                id: vista
                                anchors.fill: parent
                                anchors.margins: 6
                                fillMode: Image.PreserveAspectFit
                                // La URL es un data: con la imagen adentro, así
                                // que cambia sola cuando cambia el dibujo. NO
                                // hay que blanquearla antes de reasignar: eso
                                // dejaba un cuadro vacío en el medio y se veía
                                // como parpadeo al arrastrar la barra.
                                cache: false
                                source: page.g.vistaPreviaUrl()
                                Connections {
                                    target: page.g
                                    function onVistaChanged() { vista.source = page.g.vistaPreviaUrl() }
                                }
                            }
                        }

                        // La barra de tiempo. Arrastrar hacia atrás funciona
                        // porque el motor dibuja cada instante desde cero.
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 8
                            Text {
                                text: page.mmss(page.g.momentoMs)
                                color: Theme.text
                                font { family: Theme.mono; pixelSize: 12 }
                                Layout.preferredWidth: 46
                            }
                            Slider {
                                id: barra
                                Layout.fillWidth: true
                                from: 0
                                to: page.g.duracionMs
                                value: page.g.momentoMs
                                onMoved: page.g.momentoMs = value
                            }
                            Text {
                                text: "de " + page.mmss(page.g.duracionMs)
                                color: Theme.textFaint
                                font.pixelSize: 11
                            }
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 6
                            GlassButton {
                                icon: "⏮"; compact: true
                                onClicked: page.g.momentoMs = 0
                            }
                            GlassButton {
                                text: "▶ Reproducir"; compact: true
                                Layout.fillWidth: true
                                onClicked: reproduccion.running = !reproduccion.running
                                active: reproduccion.running
                            }
                            Text { text: "Dura"; color: Theme.textDim; font.pixelSize: 11 }
                            TextField {
                                Layout.preferredWidth: 70
                                text: String(page.g.duracionMs)
                                color: Theme.text
                                font.pixelSize: 12
                                validator: IntValidator { bottom: 100; top: 15000 }
                                onEditingFinished: page.g.duracionMs = parseInt(text) || 1500
                            }
                        }
                        // Reproducir es adelantar la barra: cada cuadro se pide
                        // al motor, así que no va a 60 por segundo — alcanza
                        // para ver el movimiento.
                        Timer {
                            id: reproduccion
                            interval: 80
                            repeat: true
                            onTriggered: {
                                if (page.g.momentoMs >= page.g.duracionMs) { running = false; return }
                                page.g.momentoMs = Math.min(page.g.duracionMs, page.g.momentoMs + 80)
                            }
                        }
                    }
                }

                Panel {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    title: "Parámetros"
                    ScrollView {
                        anchors.fill: parent
                        clip: true
                        ColumnLayout {
                            width: parent.parent.width
                            spacing: 8
                            Repeater {
                                model: page.g.parametros
                                delegate: ColumnLayout {
                                    required property var modelData
                                    Layout.fillWidth: true
                                    spacing: 1
                                    Text {
                                        text: modelData.titulo
                                        color: Theme.textDim
                                        font.pixelSize: 11
                                    }
                                    TextField {
                                        Layout.fillWidth: true
                                        text: page.g.valores[modelData.clave] !== undefined
                                              ? String(page.g.valores[modelData.clave]) : ""
                                        color: Theme.text
                                        font.pixelSize: 12
                                        onEditingFinished: page.g.setValor(modelData.clave, text)
                                    }
                                    Text {
                                        visible: modelData.ayuda !== ""
                                        Layout.fillWidth: true
                                        wrapMode: Text.WordWrap
                                        text: modelData.ayuda
                                        color: Theme.textFaint
                                        font.pixelSize: 10
                                    }
                                }
                            }

                            Rectangle {
                                Layout.fillWidth: true
                                Layout.topMargin: 6
                                height: 1
                                color: Theme.glassBorder
                            }
                            // Los botones que este gráfico le va a ofrecer al
                            // operador en la pantalla de Rótulos. Se diseñan
                            // ACÁ, mientras se diseña la animación, que es
                            // cuando se sabe para qué sirve cada parámetro.
                            Text {
                                text: "Botones que ofrece"
                                color: Theme.textDim
                                font { pixelSize: 11; weight: Font.DemiBold }
                            }
                            Text {
                                visible: page.g.acciones.length === 0
                                Layout.fillWidth: true
                                wrapMode: Text.WordWrap
                                text: "Todavía no ofrece ninguno. Un botón es un pedido ya escrito " +
                                      "(«poné al orador que sigue») que el operador aprieta sin tener " +
                                      "que explicarle nada al asistente."
                                color: Theme.textFaint
                                font.pixelSize: 11
                            }
                            Repeater {
                                model: page.g.acciones
                                delegate: ColumnLayout {
                                    required property var modelData
                                    Layout.fillWidth: true
                                    spacing: 0
                                    Text {
                                        text: modelData.icono + "  " + modelData.titulo
                                        color: Theme.text
                                        font.pixelSize: 12
                                    }
                                    Text {
                                        Layout.fillWidth: true
                                        wrapMode: Text.WordWrap
                                        text: modelData.prompt
                                        color: Theme.textFaint
                                        font.pixelSize: 10
                                    }
                                }
                            }
                            GlassButton {
                                text: page.g.acciones.length === 0 ? "Proponer botones" : "Revisar los botones"
                                compact: true
                                enabled: Studio.agent.running
                                onClicked: ApplicationWindow.window.pedirAlAsistente(
                                    "Proponé los botones («acciones») de este gráfico y escribilos en su " +
                                    "grafico.json: cada uno con «titulo» corto, «icono» de un carácter y " +
                                    "«prompt» redactado como se lo pediría el operador en vivo. Mirá qué " +
                                    "parámetros tiene para saber qué le van a querer cambiar, y no pongas " +
                                    "más de cuatro.")
                            }

                            Rectangle {
                                Layout.fillWidth: true
                                Layout.topMargin: 6
                                height: 1
                                color: Theme.glassBorder
                            }
                            Text {
                                text: "Ponerlo en una escena"
                                color: Theme.textDim
                                font { pixelSize: 11; weight: Font.DemiBold }
                            }
                            Text {
                                visible: page.g.enEscena.length === 0
                                Layout.fillWidth: true
                                wrapMode: Text.WordWrap
                                text: "No hay ninguna fuente «Gráfico animado» en las escenas. " +
                                      "Agregá una desde Escenas y volvé."
                                color: Theme.textFaint
                                font.pixelSize: 11
                            }
                            Repeater {
                                model: page.g.enEscena
                                delegate: RowLayout {
                                    required property var modelData
                                    Layout.fillWidth: true
                                    spacing: 6
                                    Text {
                                        Layout.fillWidth: true
                                        elide: Text.ElideRight
                                        text: modelData.fuente + "  " + modelData.ancho + "×" + modelData.alto
                                        color: modelData.reproduciendo ? Theme.ok : Theme.text
                                        font.pixelSize: 12
                                    }
                                    GlassButton {
                                        text: "Al aire"; compact: true
                                        onClicked: {
                                            const e = page.g.mostrarEnEscena(modelData.id, false)
                                            if (e !== "") page.g.notify(e, "error")
                                        }
                                    }
                                    GlassButton {
                                        icon: "✕"; compact: true
                                        onClicked: page.g.ocultarDeEscena(modelData.id)
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
