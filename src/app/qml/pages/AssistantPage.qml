// Pagina Asistente: chat con el LLM que maneja la app por MCP. A la izquierda
// la terminal con la interfaz oficial del agente (Claude Code, Gemini CLI u
// opencode); a la derecha el proveedor/modelo, pedidos rapidos y las
// vigilancias (alertas) con sus avisos. Todo sale de Studio.agent.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import BA
import "../controls"

Rectangle {
    id: page
    color: Theme.bg
    property real leftInset: 92
    readonly property var ag: Studio.agent
    readonly property var cur: ag.current
    readonly property color termBg: "#0d1016"
    property string keyError: ""

    function stateText() {
        if (ag.running) return "en linea"
        if (ag.installing) return "instalando"
        if (!ag.installed) return "sin instalar"
        return "detenido"
    }

    RowLayout {
        anchors { fill: parent; leftMargin: page.leftInset; margins: 18 }
        spacing: 16

        // --- Terminal ----------------------------------------------------------------------------
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            radius: 18
            color: page.termBg
            border.color: term.activeFocus ? Qt.rgba(1, 0.42, 0.17, 0.55) : Theme.glassBorder
            clip: true

            // Barra superior
            Rectangle {
                id: bar
                anchors { left: parent.left; right: parent.right; top: parent.top }
                height: 48
                color: Qt.rgba(1, 1, 1, 0.035)
                radius: 18
                Rectangle { anchors { left: parent.left; right: parent.right; bottom: parent.bottom } height: 18; color: parent.color }
                RowLayout {
                    anchors { fill: parent; leftMargin: 16; rightMargin: 10 }
                    spacing: 10
                    Row {
                        spacing: 6
                        Repeater {
                            model: ["#ff5f57", "#febc2e", "#28c840"]
                            Rectangle { required property string modelData; width: 11; height: 11; radius: 6; color: modelData; opacity: 0.85 }
                        }
                    }
                    Text { text: "✦"; color: Theme.accent; font.pixelSize: 16; Layout.leftMargin: 6 }
                    Text { text: (page.cur.agentName || "Asistente") + (ag.model !== "" ? "  ·  " + ag.model : "")
                           color: Theme.text; font { pixelSize: 14; weight: Font.DemiBold } }
                    Text { text: page.cur.name || ""; color: Theme.textFaint; font.pixelSize: 12; elide: Text.ElideRight; Layout.fillWidth: true }
                    StatusPill { text: page.stateText(); dot: ag.running ? Theme.ok : (ag.installing ? Theme.warn : Theme.textFaint); pulse: ag.running }
                    GlassButton { compact: true; text: "A−"; onClicked: ag.setFontSize(ag.fontSize - 1) }
                    GlassButton { compact: true; text: "A+"; onClicked: ag.setFontSize(ag.fontSize + 1) }
                    GlassButton { compact: true; visible: term.hasSelection; text: "Copiar"; onClicked: term.copySelection() }
                    GlassButton { compact: true; visible: ag.running; text: "Pegar"; onClicked: term.pasteClipboard() }
                    GlassButton { compact: true; visible: ag.running; text: "Reiniciar"; onClicked: ag.restart() }
                    GlassButton { compact: true; visible: ag.running; text: "Detener"; onClicked: ag.stop() }
                    // Hay una version usable pero el sistema fija otra: se ofrece,
                // no se obliga (la instalada sigue andando).
                GlassButton { compact: true; visible: ag.updateAvailable && !ag.installing
                              text: "Actualizar a " + (page.cur.version || ""); icon: "⇩"
                              onClicked: ag.install() }
                GlassButton { compact: true; visible: !ag.running && ag.installed; text: "Iniciar"; icon: "▶"; active: true
                                  onClicked: { ag.start(); term.forceActiveFocus() } }
                }
            }

            TerminalView {
                id: term
                anchors { left: parent.left; right: parent.right; top: bar.bottom; bottom: parent.bottom; margins: 4 }
                session: ag.session
                fontSize: ag.fontSize
                fontFamily: "JetBrains Mono"
                visible: ag.installed
                Component.onCompleted: if (ag.running) forceActiveFocus()
            }

            // Sin instalar: tarjeta de instalacion
            CapaTerminal {
                anchors { left: parent.left; right: parent.right; top: bar.bottom; bottom: parent.bottom }
                visible: !ag.installed
                Text { Layout.alignment: Qt.AlignHCenter; text: "✦"; color: Theme.accent; font.pixelSize: 44 }
                Text { Layout.alignment: Qt.AlignHCenter; text: "Instalar " + (page.cur.agentName || "el agente")
                       color: Theme.text; font { pixelSize: 22; weight: Font.DemiBold } }
                Text { Layout.fillWidth: true; horizontalAlignment: Text.AlignHCenter; wrapMode: Text.WordWrap
                       color: Theme.textDim; font.pixelSize: 13
                       text: "Se descarga la versión " + (page.cur.version || "") + " (fijada y verificada con SHA-256) dentro de la carpeta portable (Agentes/). Una sola vez; después funciona igual desde el pendrive." }
                ProgressBar { Layout.fillWidth: true; visible: ag.installing; from: 0; to: 1; value: ag.installProgress }
                Text { Layout.alignment: Qt.AlignHCenter; visible: ag.installText !== ""; text: ag.installText; color: Theme.textDim; font.pixelSize: 12 }
                RowLayout {
                    Layout.alignment: Qt.AlignHCenter
                    GlassButton { visible: !ag.installing; text: "Instalar"; icon: "⇩"; active: true; onClicked: ag.install() }
                    GlassButton { visible: ag.installing; text: "Cancelar"; onClicked: ag.cancelInstall() }
                }
            }

            // MCP apagado
            Rectangle {
                visible: ag.installed && !ag.mcpReady
                anchors { left: parent.left; right: parent.right; bottom: parent.bottom; margins: 14 }
                height: 44; radius: 12
                color: Qt.rgba(0.96, 0.72, 0.24, 0.16); border.color: Theme.warn
                Text { anchors.centerIn: parent; color: Theme.text; font.pixelSize: 13
                       text: "El servidor MCP está apagado: activalo en Configuración → Asistente (MCP) y reiniciá la app." }
            }
            // Arrancar (instalado y detenido)
            CapaTerminal {
                anchors { left: parent.left; right: parent.right; top: bar.bottom; bottom: parent.bottom }
                visible: ag.installed && !ag.running && ag.mcpReady
                Text { Layout.alignment: Qt.AlignHCenter; text: "✦"; color: Theme.accent; font.pixelSize: 32 }
                Text { Layout.alignment: Qt.AlignHCenter; text: page.cur.agentName || ""; color: Theme.text; font { pixelSize: 20; weight: Font.DemiBold } }
                Text { Layout.fillWidth: true; horizontalAlignment: Text.AlignHCenter; wrapMode: Text.WordWrap
                       color: Theme.textDim; font.pixelSize: 13; text: page.cur.help || "" }
                GlassButton { Layout.alignment: Qt.AlignHCenter; text: "Iniciar el asistente"; icon: "▶"; active: true
                              enabled: !page.cur.needsKey || ag.hasKey
                              onClicked: { ag.start(); term.forceActiveFocus() } }
                Text { Layout.fillWidth: true; horizontalAlignment: Text.AlignHCenter; visible: page.cur.needsKey && !ag.hasKey
                       color: Theme.warn; font.pixelSize: 12; wrapMode: Text.WordWrap
                       text: "Falta la clave de API (panel de la derecha)." }
                // El último error solo si dice algo NUEVO: cuando falta la clave, el
                // motor devuelve lo mismo que ya está escrito arriba, y el cartel
                // terminaba repitiendo la misma frase dos veces con otras palabras.
                Text { Layout.fillWidth: true; horizontalAlignment: Text.AlignHCenter
                       visible: ag.lastError !== "" && !(page.cur.needsKey && !ag.hasKey)
                       color: Theme.live; font.pixelSize: 12; text: ag.lastError; wrapMode: Text.WordWrap }
            }
        }

        // --- Panel lateral ---------------------------------------------------------------------
        ScrollView {
            Layout.preferredWidth: 340
            Layout.fillHeight: true
            contentWidth: availableWidth
            clip: true
            ColumnLayout {
                width: parent.width
                spacing: 14

                Panel {
                    Layout.fillWidth: true
                    Layout.preferredHeight: cfgCol.implicitHeight + 64
                    title: "Modelo"
                    ColumnLayout {
                        id: cfgCol
                        anchors.fill: parent
                        spacing: 8
                        ComboBox {
                            Layout.fillWidth: true
                            model: ag.providers
                            textRole: "name"
                            valueRole: "id"
                            currentIndex: { for (let i = 0; i < ag.providers.length; ++i) if (ag.providers[i].id === ag.provider) return i; return 0 }
                            onActivated: (i) => ag.setProvider(ag.providers[i].id)
                        }
                        Text { Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.textFaint; font.pixelSize: 11; text: page.cur.help || "" }
                        Text { text: "Modelo (vacío = el que elija el agente)"; color: Theme.textDim; font.pixelSize: 12 }
                        ComboBox {
                            id: modelBox
                            Layout.fillWidth: true
                            editable: true
                            model: page.cur.models || []
                            currentIndex: (page.cur.models || []).indexOf(ag.model)
                            editText: ag.model
                            onAccepted: ag.setModel(editText)
                            onActivated: ag.setModel(currentText)
                        }
                        ColumnLayout {
                            Layout.fillWidth: true
                            visible: page.cur.needsKey === true
                            spacing: 6
                            RowLayout {
                                Text { text: ag.hasKey ? "Clave de API guardada (cifrada)" : "Clave de API"; color: ag.hasKey ? Theme.ok : Theme.textDim; font.pixelSize: 12; Layout.fillWidth: true }
                                Text { text: "<a href='" + (page.cur.keyUrl || "") + "'>Conseguir ↗</a>"; textFormat: Text.RichText; linkColor: Theme.accentSoft
                                       font.pixelSize: 12; onLinkActivated: (l) => Qt.openUrlExternally(l) }
                            }
                            RowLayout {
                                Layout.fillWidth: true
                                TextField { id: keyField; Layout.fillWidth: true; echoMode: TextInput.Password; placeholderText: ag.hasKey ? "•••••• (reemplazar)" : "pegá la clave" }
                                GlassButton { compact: true; text: "Guardar"; onClicked: { page.keyError = ag.saveKey(keyField.text); if (page.keyError === "") keyField.text = "" } }
                                GlassButton { compact: true; visible: ag.hasKey; text: "Borrar"; onClicked: ag.clearKey() }
                            }
                            Text { visible: page.keyError !== ""; text: page.keyError; color: Theme.live; font.pixelSize: 11 }
                        }
                        RowLayout {
                            Toggle { checked: ag.autoStart; onToggled: (wanted) => ag.setAutoStart(wanted) }
                            Text { text: "Arrancar solo al abrir la app"; color: Theme.textDim; font.pixelSize: 12; Layout.fillWidth: true }
                        }
                        RowLayout {
                            Toggle { checked: ag.alertsToChat; onToggled: (wanted) => ag.setAlertsToChat(wanted) }
                            Text { text: "Avisos de las vigilancias al chat"; color: Theme.textDim; font.pixelSize: 12; Layout.fillWidth: true; wrapMode: Text.WordWrap }
                        }
                        GlassButton { compact: true; text: "Carpeta de agentes"; icon: "▸"; onClicked: ag.openAgentsFolder() }
                    }
                }

                Panel {
                    Layout.fillWidth: true
                    Layout.preferredHeight: quickCol.implicitHeight + 64
                    title: "Pedidos rápidos"
                    ColumnLayout {
                        id: quickCol
                        anchors.fill: parent
                        spacing: 6
                        Repeater {
                            model: [
                                { t: "¿Cómo está todo?", p: "Mirá el estado general, el programa y la sala y contame en pocas líneas cómo está todo." },
                                { t: "Cuidá el audio", p: "Vigilá que ningún canal de audio sature (más de -1 dBFS) y que no haya silencio al aire; si satura, bajá 3 dB ese canal y avisame." },
                                { t: "Revisá cómo se ve", p: "Capturá el programa y todas las escenas y decime si ves algo mal (encuadres, textos, fuentes fuera de lugar)." },
                                { t: "Oradores", p: "Vigilá el tiempo de los oradores: avisame a los 30 segundos y cuando se cumple, y mandales un aviso por chat." },
                                { t: "Lobby y manos", p: "Avisame cuando alguien entre al lobby o levante la mano." },
                                { t: "Ordená las ventanas", p: "Mirá los monitores disponibles y armá ventanas de salida para aprovecharlos (programa en el principal, multiview en otro)." }
                            ]
                            delegate: GlassButton {
                                required property var modelData
                                Layout.fillWidth: true
                                compact: true
                                text: modelData.t
                                enabled: ag.running
                                onClicked: { ag.send(modelData.p); term.forceActiveFocus() }
                            }
                        }
                    }
                }

                Panel {
                    Layout.fillWidth: true
                    Layout.preferredHeight: alertCol.implicitHeight + 64
                    title: "Vigilancias (" + ag.alerts.length + ")"
                    ColumnLayout {
                        id: alertCol
                        anchors.fill: parent
                        spacing: 6
                        Text { visible: ag.alerts.length === 0; Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.textFaint; font.pixelSize: 12
                               text: "Pedile al asistente que vigile algo (audio, oradores, fin de un tema, lobby, chat, cortes, temporizadores) y te avisa solo." }
                        Repeater {
                            model: ag.alerts
                            delegate: RowLayout {
                                required property var modelData
                                Layout.fillWidth: true
                                Toggle { checked: modelData.enabled; onToggled: (wanted) => ag.setAlertEnabled(modelData.id, wanted) }
                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 0
                                    Text { text: modelData.name; color: modelData.enabled ? Theme.text : Theme.textFaint; font.pixelSize: 12; elide: Text.ElideRight; Layout.fillWidth: true }
                                    Text { text: modelData.type + (modelData.fired > 0 ? " · " + modelData.fired + " avisos" : ""); color: Theme.textFaint; font.pixelSize: 10 }
                                }
                                GlassButton { compact: true; text: "✕"; onClicked: ag.deleteAlert(modelData.id) }
                            }
                        }
                        Text { visible: ag.events.length > 0; text: "Últimos avisos"; color: Theme.textDim; font { pixelSize: 12; weight: Font.DemiBold } Layout.topMargin: 6 }
                        Repeater {
                            model: ag.events.slice(0, 8)
                            delegate: Text {
                                required property var modelData
                                Layout.fillWidth: true
                                wrapMode: Text.WordWrap
                                color: Theme.textDim
                                font.pixelSize: 11
                                text: "<b>" + modelData.at.substring(11, 19) + "</b> " + modelData.text
                                textFormat: Text.StyledText
                            }
                        }
                    }
                }
            }
        }
    }
}
