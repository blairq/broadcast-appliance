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
    // Su contenido (con pasos), del modelo: cambia mientras corre.
    readonly property var detalle: page.abierta !== "" ? (page.d.detalles[page.abierta] || ({})) : ({})

    // El estado es una clave ("corriendo", "ok"...): se compara; lo que se lee es esto.
    function etiquetaDeEstado(e) {
        switch (e) {
        case "corriendo": return qsTr("corriendo")
        case "ok": return qsTr("ok")
        case "error": return qsTr("error")
        case "detenida": return qsTr("detenida")
        case "esperando": return qsTr("esperando")
        default: return e
        }
    }

    ColumnLayout {
        anchors { fill: parent; leftMargin: page.leftInset; margins: 20 }
        spacing: 12

        RowLayout {
            Layout.fillWidth: true
            spacing: 10
            Text { text: qsTr("Agentes"); color: Theme.text; font { pixelSize: 20; weight: Font.DemiBold } }
            StatusPill {
                text: page.d.enCurso > 0 ? qsTr("%1 trabajando").arg(page.d.enCurso) : qsTr("en espera")
                dot: page.d.enCurso > 0 ? Theme.accent : Theme.textFaint
                pulse: page.d.enCurso > 0
            }
            Item { Layout.fillWidth: true }
            Text {
                text: qsTr("Cada rol atiende solo lo suyo, con un agente de un tiro por encargo. El asistente ✦ no se entera.")
                color: Theme.textFaint; font.pixelSize: 11
                elide: Text.ElideRight; Layout.maximumWidth: 520
            }
        }

        // Lo de arriba (el oído y los roles) se desplaza si no entra: con la ventana baja, la
        // lista de roles se salía por abajo. Ocupa lo que necesita, hasta el 60 % del alto; el
        // resto es de las corridas.
        ScrollView {
            id: arriba
            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(arribaCol.implicitHeight, page.height * 0.6)
            contentWidth: availableWidth
            contentHeight: arribaCol.implicitHeight
            clip: true
        ColumnLayout {
            id: arribaCol
            width: arriba.availableWidth
            spacing: 12

        // --- El oído: Parakeet o Nemotron para cada uno (y la alerta de los dos a la vez) ---
        PanelOidos { Layout.fillWidth: true }

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
                        // Dos renglones: en uno solo (interruptor, nombre, dos llaves, motor,
                        // modelo, estado y dos botones: ~1500 px) se pisaban con la ventana angosta.
                        // Arriba: qué rol es, cómo está y qué se le puede pedir.
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 12
                            Toggle {
                                checked: fila.modelData.activo
                                onToggled: (wanted) => page.d.setActivo(fila.modelData.id, wanted)
                            }
                            ColumnLayout {
                                Layout.fillWidth: true
                                Layout.minimumWidth: 160
                                spacing: 1
                                Text { Layout.fillWidth: true; elide: Text.ElideRight
                                       text: fila.modelData.titulo; color: Theme.text; font { pixelSize: 14; weight: Font.DemiBold } }
                                Text {
                                    Layout.fillWidth: true
                                    text: fila.modelData.descripcion; color: Theme.textDim; font.pixelSize: 11
                                    wrapMode: Text.WordWrap
                                }
                            }
                            StatusPill {
                                text: fila.modelData.corriendo ? qsTr("trabajando") : (fila.modelData.enCola > 0 ? qsTr("%1 en cola").arg(fila.modelData.enCola) : (fila.modelData.activo ? qsTr("escuchando") : qsTr("apagado")))
                                dot: fila.modelData.corriendo ? Theme.accent : (fila.modelData.activo ? Theme.ok : Theme.textFaint)
                                pulse: fila.modelData.corriendo
                                medidaDe: qsTr("escuchando")
                            }
                            GlassButton {
                                compact: true; text: qsTr("Instrucciones"); icon: "☰"
                                active: instrucciones.visible
                                onClicked: {
                                    if (!instrucciones.visible) promptTexto.text = fila.modelData.prompt || ""
                                    instrucciones.visible = !instrucciones.visible
                                }
                                ToolTip.visible: hovered; ToolTip.delay: 400
                                ToolTip.text: fila.modelData.promptEditado ? qsTr("Qué es y cómo trabaja este rol (su prompt) · editado") : qsTr("Qué es y cómo trabaja este rol (su prompt)")
                            }
                            GlassButton {
                                compact: true; text: qsTr("Hablar directo"); icon: "✎"
                                enabled: fila.modelData.activo
                                visible: fila.modelData.enLaApp !== true // no se le encargan cosas: lo usa la app
                                onClicked: encargo.visible = !encargo.visible
                                ToolTip.visible: hovered; ToolTip.delay: 400
                                ToolTip.text: qsTr("Pedirle algo a este rol ahora, sin esperar un disparador")
                            }
                        }
                        // Abajo: cómo trabaja (llaves, motor y modelo). Un Flow: si no entra en el
                        // ancho, baja de renglón en vez de apretar y pisarse.
                        Flow {
                            id: ajustesRol
                            Layout.fillWidth: true
                            Layout.leftMargin: 56
                            spacing: 12
                            // La libertad de este rol: las herramientas propias
                            // del agente (consola, archivos, internet) ademas de
                            // su juego MCP. Se ve de lejos cuando esta prendida
                            // porque no es un detalle: la salvaguarda de
                            // aprobaciones no la cubre.
                            ColumnLayout {
                                width: 140
                                spacing: 1
                                // Un rol que usa la app por dentro (el tótem) no corre en una consola.
                                visible: fila.modelData.enLaApp !== true
                                RowLayout {
                                    spacing: 6
                                    Toggle {
                                        checked: fila.modelData.consola === true
                                        onToggled: (w) => page.d.setConsola(fila.modelData.id, w)
                                    }
                                    Text {
                                        text: qsTr("Usa la máquina")
                                        color: fila.modelData.consola === true ? Theme.amber : Theme.textDim
                                        font.pixelSize: 12
                                    }
                                }
                                Text {
                                    Layout.fillWidth: true
                                    text: qsTr("terminal, archivos e internet")
                                    color: fila.modelData.consola === true ? Theme.amber : Theme.textFaint
                                    font.pixelSize: 10
                                    wrapMode: Text.WordWrap
                                }
                                HoverHandler { id: consolaHov }
                                ToolTip.visible: consolaHov.hovered
                                ToolTip.delay: 400
                                ToolTip.text: qsTr("Además de manejar la app, puede usar la terminal, leer y escribir archivos y " +
                                              "salir a internet (por ejemplo, bajar una imagen y dejarla en Medios). Lo que " +
                                              "hace así NO pasa por las aprobaciones del asistente: prendelo solo si el rol lo necesita.")
                            }
                            // Dónde corre cada encargo: en la conversación del evento (la
                            // del asistente de automatización, con OpenCode) o en su sesión.
                            ColumnLayout {
                                width: 160
                                spacing: 1
                                visible: fila.modelData.enLaApp !== true
                                RowLayout {
                                    spacing: 6
                                    Toggle {
                                        checked: fila.modelData.aparte === true
                                        onToggled: (w) => page.d.setAparte(fila.modelData.id, w)
                                    }
                                    Text {
                                        text: qsTr("Aparte")
                                        color: Theme.textDim
                                        font.pixelSize: 12
                                    }
                                }
                                Text {
                                    Layout.fillWidth: true
                                    text: fila.modelData.aparte === true ? qsTr("su propia sesión") : qsTr("en la conversación del evento")
                                    color: Theme.textFaint
                                    font.pixelSize: 10
                                    wrapMode: Text.WordWrap
                                }
                                HoverHandler { id: aparteHov }
                                ToolTip.visible: aparteHov.hovered
                                ToolTip.delay: 400
                                ToolTip.text: qsTr("Apagado: con el asistente de automatización en OpenCode, cada encargo va a su " +
                                              "conversación del evento (con todo lo que pasó, sin abrir una sesión nueva por aviso). " +
                                              "Prendido: cada encargo corre en su propia sesión, con las herramientas, las instrucciones " +
                                              "y el motor del rol. Un rol con otro motor u otro modelo corre aparte siempre.")
                            }
                            ComboBox {
                                id: provBox
                                width: 230
                                model: page.d.proveedores
                                textRole: "name"; valueRole: "id"
                                displayText: fila.modelData.proveedor === "" ? qsTr("El del asistente") : currentText
                                currentIndex: { for (let i = 0; i < page.d.proveedores.length; ++i) if (page.d.proveedores[i].id === fila.modelData.proveedor) return i; return -1 }
                                onActivated: (i) => page.d.setProveedor(fila.modelData.id, page.d.proveedores[i].id)
                                ToolTip.visible: hovered
                                ToolTip.text: (fila.modelData.proveedor !== "" ? currentText + "\n" : "") + qsTr("Motor de este rol. Vacío: el mismo que el asistente ✦.")
                            }
                            ComboBox {
                                id: modeloBox
                                // Los nombres son largos: ancho, pero nunca más que el renglón.
                                width: Math.min(460, ajustesRol.width)
                                editable: true
                                model: page.d.modelos[fila.modelData.proveedor] || []
                                editText: fila.modelData.modelo
                                onAccepted: page.d.setModelo(fila.modelData.id, editText)
                                onActivated: page.d.setModelo(fila.modelData.id, currentText)
                                // Los nombres son largos (org/repo-GGUF): la lista se abre
                                // más ancha que el campo y el nombre entero va en el tooltip.
                                popup.width: Math.max(modeloBox.width, 520)
                                ToolTip.visible: hovered
                                ToolTip.text: editText !== "" ? qsTr("%1\n(vacío: el de fábrica del motor)").arg(editText)
                                                              : qsTr("Modelo (vacío: el de fábrica del motor)")
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
                                placeholderText: qsTr("Qué tiene que hacer %1…").arg(fila.modelData.titulo.toLowerCase())
                                color: Theme.text; font.pixelSize: 12
                                onAccepted: mandar.clicked()
                            }
                            GlassButton {
                                id: mandar
                                compact: true; text: qsTr("Mandar"); active: true
                                onClicked: {
                                    if (page.d.encargarDesdeUi(fila.modelData.id, encargoTexto.text)) { encargoTexto.text = ""; encargo.visible = false }
                                }
                            }
                        }
                        // Las instrucciones del rol: lo propio se edita; las reglas comunes (el aire no es
                        // suyo, el resumen al final) se agregan siempre (Roles::instrucciones).
                        ColumnLayout {
                            id: instrucciones
                            visible: false
                            Layout.fillWidth: true
                            Layout.leftMargin: 56
                            spacing: 6
                            Text {
                                Layout.fillWidth: true; wrapMode: Text.WordWrap
                                text: (fila.modelData.promptEditado
                                       ? qsTr("Instrucciones de %1 (editadas). Las reglas comunes de todos los roles se agregan siempre y no se editan acá.")
                                       : qsTr("Instrucciones de %1 (de fábrica). Las reglas comunes de todos los roles se agregan siempre y no se editan acá."))
                                      .arg(fila.modelData.titulo)
                                color: Theme.textDim; font.pixelSize: 11
                            }
                            ScrollView {
                                Layout.fillWidth: true
                                Layout.preferredHeight: 220
                                TextArea {
                                    id: promptTexto
                                    wrapMode: TextEdit.Wrap
                                    color: Theme.text; font { pixelSize: 12; family: "Menlo" }
                                    placeholderText: qsTr("Sin instrucciones propias: solo las reglas comunes.")
                                    background: Rectangle { color: Theme.field; radius: 8 }
                                }
                            }
                            RowLayout {
                                spacing: 8
                                GlassButton {
                                    compact: true; text: qsTr("Guardar"); active: true
                                    enabled: promptTexto.text !== (fila.modelData.prompt || "")
                                    onClicked: { page.d.setPrompt(fila.modelData.id, promptTexto.text); instrucciones.visible = false }
                                }
                                GlassButton {
                                    compact: true; text: qsTr("Volver a las de fábrica")
                                    visible: fila.modelData.promptEditado === true
                                    onClicked: ApplicationWindow.window.confirm({
                                        title: qsTr("¿Volver a las instrucciones de fábrica?"),
                                        text: qsTr("Se pierde lo que escribiste para %1.").arg(fila.modelData.titulo),
                                        accept: qsTr("Volver a las de fábrica"), reject: qsTr("Cancelar"),
                                        action: function() { page.d.setPrompt(fila.modelData.id, ""); promptTexto.text = fila.modelData.promptFabrica || "" }
                                    })
                                }
                                GlassButton { compact: true; text: qsTr("Cancelar"); onClicked: instrucciones.visible = false }
                            }
                        }
                        Text {
                            Layout.leftMargin: 56
                            visible: fila.modelData.enLaApp !== true
                            text: qsTr("ve: %1   ·   atiende: %2").arg(fila.modelData.categorias.join(", ")).arg(fila.modelData.disparadores.join(", "))
                            color: Theme.textFaint; font.pixelSize: 10
                        }
                        // El rol del tótem: corre en la app (sin consola) y entiende lo que dice la gente.
                        // Con un motor que no es el local, lo que dicen (sus datos) sale de la máquina.
                        Text {
                            Layout.leftMargin: 56
                            Layout.fillWidth: true
                            visible: fila.modelData.enLaApp === true
                            wrapMode: Text.WordWrap
                            readonly property bool local: fila.modelData.proveedor === "" || fila.modelData.proveedor === "local"
                            text: local ? qsTr("Corre en la app, con el modelo local: contesta en el acto y lo que dice la gente no sale de esta máquina.")
                                        : qsTr("Atención: con este motor, lo que dice la gente en el tótem (sus datos) sale de esta máquina hacia ese servicio. " +
                                          "Sirven el modelo propio, OpenRouter y Gemini con clave; los de cuenta (Claude, OpenCode Go) no.")
                            color: local ? Theme.textFaint : Theme.amber
                            font.pixelSize: 10
                        }
                    }
                }
            }
        }
        } // arribaCol
        } // arriba (ScrollView)

        // --- Corridas ------------------------------------------------------------
        RowLayout {
            Layout.fillWidth: true
            Text { text: qsTr("Corridas"); color: Theme.text; font { pixelSize: 15; weight: Font.DemiBold } }
            Text { text: qsTr("%n reciente", "", page.d.corridas.length); color: Theme.textDim; font.pixelSize: 11 }
            Item { Layout.fillWidth: true }
        }
        Panel {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: 160
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
                        }
                    }
                    ColumnLayout {
                        anchors { fill: parent; margins: 8 }
                        spacing: 6
                        RowLayout {
                            id: cab
                            Layout.fillWidth: true
                            spacing: 10
                            Rectangle { Layout.preferredWidth: 8; Layout.preferredHeight: 8; radius: 4; color: Theme.colorDeEstado(item.modelData.estado) }
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
                                text: item.modelData.inicio !== "" ? qsTr("%1 · %2 s · %3 herr.").arg(page.etiquetaDeEstado(item.modelData.estado)).arg(item.modelData.duracionS).arg(item.modelData.herramientas)
                                                                    : page.etiquetaDeEstado(item.modelData.estado)
                                color: Theme.colorDeEstado(item.modelData.estado); font.pixelSize: 11
                            }
                            GlassButton {
                                compact: true; text: qsTr("Detener")
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
                                              : (modelData.texto || (modelData.tipo === "fin" ? qsTr("fin: %n turno, %1 s", "", modelData.turnos).arg(Math.round((modelData.duracionMs || 0) / 1000)) : ""))
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
                    text: qsTr("Todavía no hubo corridas. Prendé un rol y esperá que llegue algo, o encargale algo a mano.")
                    color: Theme.textFaint; font.pixelSize: 12
                }
            }
        }
    }
}
