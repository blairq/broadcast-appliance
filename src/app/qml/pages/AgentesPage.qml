// Página Agentes: el panel de control de los roles automáticos.
//
// Arriba, los roles: cada uno con su interruptor, su motor y su modelo, y si
// tiene una corrida en curso o en cola. Abajo, las corridas: una fila por
// cada vez que un rol atendió algo, que se despliega para ver paso a paso
// qué dijo, qué herramienta llamó y qué le contestaron. Es la vista de «qué
// están haciendo», y sale de la salida JSON de cada motor, no de una
// terminal: por eso es igual para Claude Code y para opencode.
//
// Todo lo que se toca acá se puede tocar por MCP (agente_configurar,
// agente_encargar...) y al revés: la fuente es una sola, Studio.despachador.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import BA
import "../controls"

Rectangle {
    id: page
    color: Theme.bg
    property real leftInset: 92
    readonly property var d: Studio.despachador
    property string abierta: ""      // id de la corrida desplegada
    property var detalle: ({})       // su contenido (con pasos)

    function colorDeEstado(e) {
        switch (e) {
        case "corriendo": return Theme.accent
        case "ok": return Theme.ok
        case "error": return Theme.live
        case "detenida": return Theme.warn
        default: return Theme.textFaint
        }
    }
    function refrescarDetalle() {
        if (page.abierta !== "") page.detalle = page.d.corridaQml(page.abierta)
    }
    Connections {
        target: page.d
        function onCambio() { page.refrescarDetalle() }
    }

    ColumnLayout {
        anchors { fill: parent; leftMargin: page.leftInset; margins: 20 }
        spacing: 12

        RowLayout {
            Layout.fillWidth: true
            spacing: 10
            Text { text: "Agentes"; color: Theme.text; font { pixelSize: 20; weight: Font.DemiBold } }
            StatusPill {
                text: page.d.enCurso > 0 ? page.d.enCurso + " trabajando" : "en espera"
                dot: page.d.enCurso > 0 ? Theme.accent : Theme.textFaint
                pulse: page.d.enCurso > 0
            }
            Item { Layout.fillWidth: true }
            Text {
                text: "Cada rol atiende solo lo suyo, con un agente de un tiro por encargo. El asistente ✦ no se entera."
                color: Theme.textFaint; font.pixelSize: 11
                elide: Text.ElideRight; Layout.maximumWidth: 520
            }
        }

        // --- Roles ---------------------------------------------------------------
        Panel {
            Layout.fillWidth: true
            padding: 12
            implicitHeight: rolesCol.implicitHeight + 2 * padding
            ColumnLayout {
                id: rolesCol
                anchors.fill: parent // el margen lo pone Panel (`padding`)
                spacing: 8
                Repeater {
                    model: page.d.roles
                    delegate: ColumnLayout {
                        id: fila
                        required property var modelData
                        Layout.fillWidth: true
                        spacing: 6
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 12
                            Toggle {
                                checked: fila.modelData.activo
                                onToggled: (wanted) => page.d.setActivo(fila.modelData.id, wanted)
                            }
                            ColumnLayout {
                                // Un layout adentro de otro tiene fillWidth en TRUE
                                // por defecto: sin esto se come el ancho de los
                                // desplegables de la derecha.
                                Layout.fillWidth: false
                                Layout.preferredWidth: 220
                                spacing: 1
                                Text { text: fila.modelData.titulo; color: Theme.text; font { pixelSize: 14; weight: Font.DemiBold } }
                                Text {
                                    Layout.fillWidth: true
                                    text: fila.modelData.descripcion; color: Theme.textDim; font.pixelSize: 11
                                    wrapMode: Text.WordWrap
                                }
                            }
                            // La libertad de este rol: las herramientas propias
                            // del agente (consola, archivos, internet) ademas de
                            // su juego MCP. Se ve de lejos cuando esta prendida
                            // porque no es un detalle: la salvaguarda de
                            // aprobaciones no la cubre.
                            ColumnLayout {
                                Layout.fillWidth: false
                                Layout.preferredWidth: 110
                                spacing: 1
                                RowLayout {
                                    spacing: 6
                                    Toggle {
                                        checked: fila.modelData.consola === true
                                        onToggled: (w) => page.d.setConsola(fila.modelData.id, w)
                                    }
                                    Text {
                                        text: "Consola"
                                        color: fila.modelData.consola === true ? Theme.amber : Theme.textDim
                                        font.pixelSize: 12
                                    }
                                }
                                Text {
                                    Layout.fillWidth: true
                                    visible: fila.modelData.consola === true
                                    text: "disco e internet"
                                    color: Theme.amber
                                    font.pixelSize: 10
                                    wrapMode: Text.WordWrap
                                }
                                ToolTip.visible: false
                            }
                            ComboBox {
                                id: provBox
                                Layout.preferredWidth: 220
                                model: page.d.proveedores
                                textRole: "name"; valueRole: "id"
                                displayText: fila.modelData.proveedor === "" ? "El del asistente" : currentText
                                currentIndex: { for (let i = 0; i < page.d.proveedores.length; ++i) if (page.d.proveedores[i].id === fila.modelData.proveedor) return i; return -1 }
                                onActivated: (i) => page.d.setProveedor(fila.modelData.id, page.d.proveedores[i].id)
                                ToolTip.visible: hovered; ToolTip.text: "Motor de este rol. Vacío: el mismo que el asistente ✦."
                            }
                            ComboBox {
                                Layout.preferredWidth: 200
                                editable: true
                                model: page.d.modelosDe(fila.modelData.proveedor)
                                editText: fila.modelData.modelo
                                onAccepted: page.d.setModelo(fila.modelData.id, editText)
                                onActivated: page.d.setModelo(fila.modelData.id, currentText)
                                ToolTip.visible: hovered; ToolTip.text: "Modelo (vacío: el de fábrica del motor)"
                            }
                            StatusPill {
                                text: fila.modelData.corriendo ? "trabajando" : (fila.modelData.enCola > 0 ? fila.modelData.enCola + " en cola" : (fila.modelData.activo ? "escuchando" : "apagado"))
                                dot: fila.modelData.corriendo ? Theme.accent : (fila.modelData.activo ? Theme.ok : Theme.textFaint)
                                pulse: fila.modelData.corriendo
                                medidaDe: "escuchando"
                            }
                            Item { Layout.fillWidth: true }
                            GlassButton {
                                compact: true; text: "Encargar"; icon: "✎"
                                enabled: fila.modelData.activo
                                onClicked: encargo.visible = !encargo.visible
                            }
                        }
                        RowLayout {
                            id: encargo
                            visible: false
                            Layout.fillWidth: true
                            Layout.leftMargin: 56
                            spacing: 8
                            TextField {
                                id: encargoTexto
                                Layout.fillWidth: true
                                placeholderText: "Qué tiene que hacer " + fila.modelData.titulo.toLowerCase() + "…"
                                color: Theme.text; font.pixelSize: 12
                                onAccepted: mandar.clicked()
                            }
                            GlassButton {
                                id: mandar
                                compact: true; text: "Mandar"; active: true
                                onClicked: {
                                    const err = page.d.encargarDesdeUi(fila.modelData.id, encargoTexto.text)
                                    if (err !== "") Studio.notify(err, "error")
                                    else { encargoTexto.text = ""; encargo.visible = false }
                                }
                            }
                        }
                        Text {
                            Layout.leftMargin: 56
                            text: "ve: " + fila.modelData.categorias.join(", ") + "   ·   atiende: " + fila.modelData.disparadores.join(", ")
                            color: Theme.textFaint; font.pixelSize: 10
                        }
                    }
                }
            }
        }

        // --- Corridas ------------------------------------------------------------
        RowLayout {
            Layout.fillWidth: true
            Text { text: "Corridas"; color: Theme.text; font { pixelSize: 15; weight: Font.DemiBold } }
            Text { text: page.d.corridas.length + " recientes"; color: Theme.textDim; font.pixelSize: 11 }
            Item { Layout.fillWidth: true }
        }
        Panel {
            Layout.fillWidth: true
            Layout.fillHeight: true
            ListView {
                id: lista
                anchors { fill: parent; margins: 8 }
                clip: true
                spacing: 4
                model: page.d.corridas
                ScrollBar.vertical: ScrollBar {}
                delegate: Rectangle {
                    id: item
                    required property var modelData
                    readonly property bool abierta: page.abierta === modelData.id
                    width: lista.width
                    radius: 10
                    color: abierta ? Qt.rgba(1, 1, 1, 0.05) : "transparent"
                    implicitHeight: cab.implicitHeight + (abierta ? det.implicitHeight + 16 : 0) + 16
                    Behavior on implicitHeight { NumberAnimation { duration: 120 } }
                    MouseArea {
                        anchors.fill: parent
                        onClicked: {
                            page.abierta = item.abierta ? "" : item.modelData.id
                            page.refrescarDetalle()
                        }
                    }
                    ColumnLayout {
                        anchors { fill: parent; margins: 8 }
                        spacing: 6
                        RowLayout {
                            id: cab
                            Layout.fillWidth: true
                            spacing: 10
                            Rectangle { Layout.preferredWidth: 8; Layout.preferredHeight: 8; radius: 4; color: page.colorDeEstado(item.modelData.estado) }
                            Text { text: item.modelData.creada; color: Theme.textDim; font { pixelSize: 11; family: "JetBrains Mono" } }
                            Text { text: item.modelData.rolTitulo; color: Theme.text; font { pixelSize: 12; weight: Font.DemiBold } Layout.preferredWidth: 90 }
                            Text { text: item.modelData.origen; color: Theme.textFaint; font.pixelSize: 11; Layout.preferredWidth: 150; elide: Text.ElideRight }
                            Text {
                                Layout.fillWidth: true
                                text: item.modelData.resumen !== "" ? item.modelData.resumen
                                    : (item.modelData.error !== "" ? item.modelData.error : item.modelData.encargo)
                                color: item.modelData.error !== "" ? Theme.live : Theme.textDim
                                font.pixelSize: 11; elide: Text.ElideRight
                            }
                            Text {
                                text: item.modelData.estado + (item.modelData.inicio !== "" ? " · " + item.modelData.duracionS + " s · " + item.modelData.herramientas + " herr." : "")
                                color: page.colorDeEstado(item.modelData.estado); font.pixelSize: 11
                            }
                            GlassButton {
                                compact: true; text: "Detener"
                                visible: item.modelData.estado === "corriendo" || item.modelData.estado === "esperando"
                                onClicked: page.d.detener(item.modelData.id)
                            }
                        }
                        ColumnLayout {
                            id: det
                            visible: item.abierta
                            Layout.fillWidth: true
                            Layout.leftMargin: 18
                            spacing: 4
                            Text {
                                Layout.fillWidth: true
                                text: page.detalle.encargo || ""
                                color: Theme.textDim; font.pixelSize: 11; wrapMode: Text.WordWrap
                            }
                            Repeater {
                                model: item.abierta ? (page.detalle.pasos || []) : []
                                delegate: RowLayout {
                                    required property var modelData
                                    Layout.fillWidth: true
                                    spacing: 8
                                    Text { text: modelData.t; color: Theme.textFaint; font { pixelSize: 10; family: "JetBrains Mono" } }
                                    Text {
                                        text: modelData.tipo === "herramienta" ? "⚙" : (modelData.tipo === "texto" ? "💬" : (modelData.tipo === "resultado" ? (modelData.error ? "✗" : "↩") : "·"))
                                        color: modelData.error ? Theme.live : Theme.textDim; font.pixelSize: 11
                                        Layout.preferredWidth: 16
                                    }
                                    Text {
                                        Layout.fillWidth: true
                                        text: modelData.tipo === "herramienta"
                                              ? modelData.herramienta + " " + JSON.stringify(modelData.args || {})
                                              : (modelData.texto || (modelData.tipo === "fin" ? "fin: " + modelData.turnos + " turnos, " + Math.round((modelData.duracionMs || 0) / 1000) + " s" : ""))
                                        color: modelData.tipo === "texto" ? Theme.text : Theme.textDim
                                        font { pixelSize: 11; family: modelData.tipo === "herramienta" ? "JetBrains Mono" : Theme.font }
                                        wrapMode: Text.WrapAnywhere
                                    }
                                }
                            }
                        }
                    }
                }
                Text {
                    anchors.centerIn: parent
                    visible: lista.count === 0
                    text: "Todavía no hubo corridas. Prendé un rol y esperá que llegue algo, o encargale algo a mano."
                    color: Theme.textFaint; font.pixelSize: 12
                }
            }
        }
    }
}
