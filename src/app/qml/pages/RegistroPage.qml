// El visor de registros: lo que pasó, en los tres procesos, entrelazado por
// hora. Solo presentación: los filtros y la lectura viven en Studio.registro.
//
// Decisiones de esta pantalla, todas por lo mismo (leer un log es leer bajo
// presión, mientras algo está fallando):
//   * El nivel es una fila de botones, no un desplegable: se cambia de un clic.
//   * Cada línea tiene su color por nivel, y los errores se ven sin leer.
//   * «Seguir» va prendido; al apagarlo se deja de leer el disco, así la lista
//     no se mueve sola mientras uno está leyendo una línea.
//   * «Marcar» escribe una línea en el registro: se pone antes de probar algo
//     y después se busca esa marca.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import BA
import "../controls"

Rectangle {
    id: page
    color: Theme.bg
    property real leftInset: 92
    readonly property var r: Studio.registro
    // Solo se releen los logs mientras esta vista está a la vista.
    property bool mirando: false
    function avisarVista() {
        if (visible === mirando) return
        mirando = visible
        r.mirar(visible)
    }
    onVisibleChanged: avisarVista()
    Component.onCompleted: avisarVista()
    Component.onDestruction: if (mirando) r.mirar(false)

    function colorDeNivel(n) {
        switch (n) {
        case 5: return "#ff4d4f"   // fatal
        case 4: return Theme.live  // error
        case 3: return "#ffb02e"   // aviso
        case 2: return Theme.text  // info
        case 1: return Theme.textDim
        default: return Theme.textFaint
        }
    }

    ColumnLayout {
        anchors { fill: parent; leftMargin: page.leftInset; margins: 20 }
        spacing: 12

        RowLayout {
            Layout.fillWidth: true
            spacing: 10
            Text {
                text: "Logs del sistema"
                color: Theme.text
                font { pixelSize: 20; weight: Font.DemiBold }
            }
            Text {
                text: page.r.lineas.length + " líneas"
                color: Theme.textDim
                font.pixelSize: 11
            }
            // Filtros de un clic para lo que más se mira. Leer un log es leer
            // bajo presión: tener que escribir el nombre del módulo, no.
            Repeater {
                model: [{t: "Todo", m: ""}, {t: "MCP", m: "mcp"}, {t: "Agentes", m: "agente."},
                        {t: "Pantallas", m: "pantallas"}, {t: "QR", m: "qr"}]
                delegate: GlassButton {
                    required property var modelData
                    compact: true
                    text: modelData.t
                    active: page.r.modulo === modelData.m
                    onClicked: page.r.modulo = modelData.m
                }
            }
            Item { Layout.fillWidth: true }
            TextField {
                Layout.preferredWidth: 280
                placeholderText: "Buscar en el texto o en el módulo…"
                color: Theme.text
                font.pixelSize: 12
                text: page.r.busca
                onTextEdited: buscaDemora.restart()
                Timer {
                    id: buscaDemora
                    interval: 300 // no releer el disco en cada tecla
                    onTriggered: page.r.busca = parent.text
                }
            }
            GlassButton {
                text: "Marcar"; icon: "⚑"; compact: true
                onClicked: page.r.marcar("marca del operador")
                ToolTip.visible: hovered
                ToolTip.text: "Deja una marca acá, para encontrar este momento después"
            }
            GlassButton {
                text: "Guardar"; icon: "↓"; compact: true
                onClicked: page.r.exportar()
                ToolTip.visible: hovered
                ToolTip.text: "Guarda lo que estás viendo, ya filtrado, en Logs/"
            }
            GlassButton {
                text: "Copiar"; icon: "⧉"; compact: true
                onClicked: {
                    copiador.text = page.r.comoTexto()
                    copiador.selectAll()
                    copiador.copy()
                    Studio.notify("Registro copiado (" + page.r.lineas.length + " líneas)", "ok")
                }
            }
            GlassButton {
                text: page.r.siguiendo ? "Siguiendo" : "Pausado"
                icon: page.r.siguiendo ? "▶" : "⏸"
                compact: true
                active: page.r.siguiendo
                onClicked: page.r.siguiendo = !page.r.siguiendo
            }
        }

        // --- Algo se apagó solo porque rompió el arranque --------------------
        // Va arriba de todo y en rojo: si algo dejó de cargarse, enterarse es
        // más urgente que cualquier filtro.
        Panel {
            visible: page.r.apagados.length > 0
            Layout.fillWidth: true
            padding: 12
            implicitHeight: apag.implicitHeight + 2 * padding
            ColumnLayout {
                id: apag
                anchors.fill: parent // el margen lo pone Panel (`padding`)
                spacing: 6
                Text {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    color: Theme.live
                    font { pixelSize: 12; weight: Font.DemiBold }
                    text: "⚠  No se está cargando: " + page.r.apagados.join(", ")
                }
                Text {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    color: Theme.textDim
                    font.pixelSize: 11
                    text: "Se llevó puesto el arranque dos veces seguidas, así que el programa " +
                          "lo salteó para poder abrir. Buscá su nombre acá abajo para ver qué pasó. " +
                          "Al prenderlo, se vuelve a cargar en el próximo arranque."
                }
                Flow {
                    Layout.fillWidth: true
                    spacing: 6
                    Repeater {
                        model: page.r.apagados
                        delegate: GlassButton {
                            required property var modelData
                            compact: true
                            icon: "↺"
                            text: "Prender " + modelData
                            onClicked: page.r.prender(modelData)
                        }
                    }
                }
            }
        }

        // --- Filtros ---------------------------------------------------------
        Panel {
            Layout.fillWidth: true
            padding: 12
            implicitHeight: filtros.implicitHeight + 2 * padding

            ColumnLayout {
                id: filtros
                anchors.fill: parent // el margen lo pone Panel (`padding`)
                spacing: 8

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 6
                    Text {
                        text: "Desde"
                        color: Theme.textDim
                        font.pixelSize: 11
                    }
                    Repeater {
                        model: page.r.niveles
                        delegate: GlassButton {
                            required property var modelData
                            required property int index
                            compact: true
                            text: modelData
                            active: page.r.minimo === index
                            onClicked: page.r.minimo = index
                        }
                    }
                    Item { Layout.fillWidth: true }
                    CheckBox {
                        checked: page.r.enMemoria
                        text: "Solo esta corrida"
                        onToggled: page.r.enMemoria = checked
                        ToolTip.visible: hovered
                        ToolTip.text: "Prendido: solo lo de la app, desde que arrancó, sin tocar el disco.\n" +
                                      "Apagado: los archivos de Logs/, que traen también al gateway, al " +
                                      "launcher y la corrida anterior."
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 6
                    Text {
                        Layout.alignment: Qt.AlignTop
                        Layout.topMargin: 5
                        text: "Módulo"
                        color: Theme.textDim
                        font.pixelSize: 11
                    }
                    // Los módulos son muchos y no se sabe cuántos: una fila
                    // sola se salía de la pantalla. Envuelve.
                    Item {
                        Layout.fillWidth: true
                        implicitHeight: mods.implicitHeight
                        Flow {
                            id: mods
                            width: parent.width
                            spacing: 5
                            GlassButton {
                                compact: true; text: "todos"
                                active: page.r.modulo === ""
                                onClicked: page.r.modulo = ""
                            }
                            Repeater {
                                model: page.r.modulos
                                delegate: GlassButton {
                                    required property var modelData
                                    compact: true
                                    text: modelData
                                    active: page.r.modulo === modelData
                                    onClicked: page.r.modulo = modelData
                                }
                            }
                        }
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 6
                    Text {
                        text: "Se registra desde"
                        color: Theme.textDim
                        font.pixelSize: 11
                    }
                    Repeater {
                        model: page.r.niveles
                        delegate: GlassButton {
                            required property var modelData
                            required property int index
                            compact: true
                            text: modelData
                            active: page.r.nivelDelProceso === index
                            onClicked: page.r.nivelDelProceso = index
                        }
                    }
                    Text {
                        Layout.fillWidth: true
                        elide: Text.ElideRight
                        color: Theme.textFaint
                        font.pixelSize: 10
                        text: "lo de abajo de este nivel no se escribe en ningún lado" +
                              (page.r.descartadas > 0 ? "  ·  " + page.r.descartadas + " descartadas" : "")
                    }
                }
            }
        }

        // --- Las líneas ------------------------------------------------------
        Panel {
            Layout.fillWidth: true
            Layout.fillHeight: true

            ListView {
                id: lista
                anchors { fill: parent; margins: 10 }
                clip: true
                model: page.r.lineas
                spacing: 1
                // Al llegar algo nuevo, al final: es lo que uno quiere
                // mirando un registro en vivo. Pausado, se queda donde está.
                onCountChanged: if (page.r.siguiendo) positionViewAtEnd()
                ScrollBar.vertical: ScrollBar {}

                delegate: Rectangle {
                    required property var modelData
                    width: lista.width
                    height: Math.max(18, txt.implicitHeight + 4)
                    color: modelData.nivelN >= 4 ? Qt.rgba(1, 0.2, 0.2, 0.07)
                         : (modelData.nivelN === 3 ? Qt.rgba(1, 0.7, 0.2, 0.05) : "transparent")

                    RowLayout {
                        anchors { fill: parent; leftMargin: 4; rightMargin: 4 }
                        spacing: 10
                        Text {
                            Layout.preferredWidth: 110
                            text: modelData.hora
                            color: Theme.textFaint
                            font { family: Theme.mono; pixelSize: 11 }
                        }
                        Text {
                            Layout.preferredWidth: 72
                            text: modelData.nivel
                            color: page.colorDeNivel(modelData.nivelN)
                            font { family: Theme.mono; pixelSize: 11; weight: Font.DemiBold }
                        }
                        Text {
                            Layout.preferredWidth: 90
                            elide: Text.ElideRight
                            text: modelData.modulo
                            color: Theme.textDim
                            font { family: Theme.mono; pixelSize: 11 }
                        }
                        Text {
                            Layout.preferredWidth: 42
                            // El código solo cuando dice algo: una columna de
                            // ceros no informa y roba lugar al texto.
                            text: modelData.codigo !== 0 ? String(modelData.codigo) : ""
                            color: Theme.textFaint
                            font { family: Theme.mono; pixelSize: 11 }
                        }
                        Text {
                            id: txt
                            Layout.fillWidth: true
                            wrapMode: Text.WrapAnywhere
                            text: modelData.texto
                            color: page.colorDeNivel(modelData.nivelN)
                            font { family: Theme.mono; pixelSize: 11 }
                        }
                    }
                }
            }

            Text {
                anchors.centerIn: parent
                visible: page.r.lineas.length === 0
                width: parent.width - 80
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
                color: Theme.textDim
                font.pixelSize: 13
                text: "No hay nada que pase este filtro.\n\n" +
                      "Probá bajando el nivel, sacando el módulo o vaciando la búsqueda. " +
                      "Los archivos están en " + page.r.carpeta
            }
        }
    }

    // Para copiar al portapapeles sin traer un tipo nuevo desde C++.
    TextEdit {
        id: copiador
        visible: false
    }
}
