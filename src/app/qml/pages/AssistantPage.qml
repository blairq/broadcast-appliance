// Pagina Asistente: chat con el LLM que maneja la app por MCP. A la izquierda
// la terminal con la interfaz oficial del agente (Claude Code, Gemini CLI u
// opencode); a la derecha el proveedor/modelo, pedidos rapidos y las
// disparadores (alertas) con sus avisos. Todo sale de Studio.agent.
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
    /// true en la copia que está en su propia ventana (la pone PageWindow).
    property bool enVentana: false
    /// En la ventana principal, cuando el asistente está afuera: la terminal vive en
    /// esa ventana (dos vistas de la misma terminal se pelearían por su tamaño).
    readonly property var principal: ApplicationWindow.window
    readonly property bool afuera: !enVentana && principal !== null && principal.paginaAfuera !== undefined
                                   && principal.paginaAfuera("asistente")

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
                    GlassButton {
                        compact: true; icon: page.enVentana ? "⇲" : "⧉"
                        text: page.enVentana ? "Volver" : "Sacar a una ventana"
                        visible: !page.afuera
                        ToolTip.visible: hovered; ToolTip.delay: 400
                        ToolTip.text: page.enVentana ? "Vuelve el asistente a la ventana principal"
                                                     : "Abre el asistente en su propia ventana (para otro monitor)"
                        onClicked: page.enVentana ? Window.window.close()
                                                  : page.principal.sacarAVentana(Studio.riel.paginaDeId("asistente"), "Asistente")
                    }
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
                // La ultima que publico el proveedor (con su hash), sin esperar
                // una version nueva del sistema. Se instala en Agentes/ del bundle.
                GlassButton { compact: true; visible: ag.installed && !ag.installing && !ag.newerAvailable && !ag.updateAvailable
                              text: ag.checkingUpdate ? "Buscando…" : "Buscar actualización"; icon: "↻"
                              onClicked: if (!ag.checkingUpdate) ag.checkUpdate() }
                GlassButton { compact: true; visible: ag.newerAvailable && !ag.installing
                              text: "Actualizar a " + ag.latestVersion; icon: "⇩"; active: true
                              onClicked: ag.installLatest() }
                GlassButton { compact: true; visible: !ag.running && ag.installed; text: "Iniciar"; icon: "▶"; active: true
                                  onClicked: { ag.start(); term.forceActiveFocus() } }
                }
            }

            // Afuera: la terminal está en su ventana.
            CapaTerminal {
                anchors { left: parent.left; right: parent.right; top: bar.bottom; bottom: parent.bottom }
                visible: page.afuera
                z: 5
                Text { Layout.alignment: Qt.AlignHCenter; text: "⧉"; color: Theme.accent; font.pixelSize: 40 }
                Text { Layout.alignment: Qt.AlignHCenter; text: "El asistente está en su propia ventana"
                       color: Theme.text; font { pixelSize: 16; weight: Font.DemiBold } }
                GlassButton { Layout.alignment: Qt.AlignHCenter; text: "Traer de vuelta"; icon: "⇲"; active: true
                              onClicked: page.principal.traerPagina("asistente") }
            }

            TerminalView {
                id: term
                anchors { left: parent.left; right: parent.right; top: bar.bottom; bottom: parent.bottom; margins: 4 }
                session: page.afuera ? null : ag.session
                fontSize: ag.fontSize
                fontFamily: "JetBrains Mono"
                visible: ag.installed && !page.afuera
                Component.onCompleted: if (ag.running) forceActiveFocus()
            }

            // Sin instalar: tarjeta de instalacion
            CapaTerminal {
                anchors { left: parent.left; right: parent.right; top: bar.bottom; bottom: parent.bottom }
                visible: !ag.installed && !page.afuera
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
                visible: ag.installed && !ag.mcpReady && !page.afuera
                anchors { left: parent.left; right: parent.right; bottom: parent.bottom; margins: 14 }
                height: 44; radius: 12
                color: Qt.rgba(0.96, 0.72, 0.24, 0.16); border.color: Theme.warn
                Text { anchors.centerIn: parent; color: Theme.text; font.pixelSize: 13
                       text: "El servidor MCP está apagado: activalo en Configuración → Asistente (MCP) y reiniciá la app." }
            }
            // Arrancar (instalado y detenido)
            CapaTerminal {
                anchors { left: parent.left; right: parent.right; top: bar.bottom; bottom: parent.bottom }
                visible: ag.installed && !ag.running && ag.mcpReady && !page.afuera
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

                        // --- El modelo propio: lo que solo él necesita -------
                        // Dirección, qué modelo pedir y los parámetros extra de
                        // TU servidor. Van acá y no en una tabla del programa
                        // porque son tuyos: cada servidor propio acepta lo
                        // suyo, y una lista fija de casillas se queda corta el
                        // primer día. Lo que se escriba viaja tal cual.
                        ColumnLayout {
                            id: propio
                            Layout.fillWidth: true
                            visible: page.cur.id === "propio"
                            spacing: 6
                            property var cfg: ({})
                            function recargar() { propio.cfg = ag.modeloPropio()
                                                  urlF.text = propio.cfg.url || ""
                                                  modF.text = propio.cfg.modelo || ""
                                                  nomF.text = propio.cfg.nombre || ""
                                                  extraF.text = propio.cfg.extra || "{}"
                                                  instF.text = propio.cfg.instrucciones || ""
                                                  piensaT.checked = propio.cfg.piensa === true }
                            Component.onCompleted: recargar()
                            Connections { target: ag; function onConfigChanged() { propio.recargar() } }

                            Text { text: "Tu servidor"; color: Theme.textDim
                                   font { pixelSize: 12; weight: Font.DemiBold } }
                            TextField { id: urlF; Layout.fillWidth: true
                                        placeholderText: "https://tu-servidor/v1" }
                            RowLayout {
                                Layout.fillWidth: true
                                TextField { id: modF; Layout.fillWidth: true
                                            placeholderText: "modelo (como lo pide tu servidor)" }
                                TextField { id: nomF; Layout.preferredWidth: 130
                                            placeholderText: "cómo mostrarlo" }
                            }
                            RowLayout {
                                Toggle { id: piensaT; onToggled: (w) => checked = w }
                                Text { text: "Razona antes de contestar"; color: Theme.textDim
                                       font.pixelSize: 12; Layout.fillWidth: true }
                            }
                            Text { text: "Parámetros extra (JSON, tal cual al servidor)"
                                   color: Theme.textDim; font.pixelSize: 12 }
                            ScrollView {
                                Layout.fillWidth: true
                                Layout.preferredHeight: 150
                                clip: true
                                TextArea { id: extraF; wrapMode: TextEdit.NoWrap; selectByMouse: true
                                           font { family: Theme.mono; pixelSize: 11 } }
                            }
                            Text { text: "Instrucciones para este modelo (se suman al prompt)"
                                   color: Theme.textDim; font.pixelSize: 12 }
                            ScrollView {
                                Layout.fillWidth: true
                                Layout.preferredHeight: 110
                                clip: true
                                TextArea { id: instF; wrapMode: TextEdit.Wrap; selectByMouse: true
                                           font.pixelSize: 11 }
                            }
                            RowLayout {
                                Layout.fillWidth: true
                                GlassButton {
                                    compact: true; text: "Guardar"
                                    onClicked: {
                                        const e = ag.guardarModeloPropio(urlF.text, modF.text, nomF.text,
                                                                         extraF.text, instF.text,
                                                                         piensaT.checked)
                                        Studio.notify(
                                            e === "" ? "Modelo propio guardado" : e,
                                            e === "" ? "ok" : "error")
                                    }
                                }
                                GlassButton {
                                    compact: true; text: "Probar"
                                    // Pregunta a /models: dice si el servidor
                                    // está, si la clave sirve y si ese modelo
                                    // existe, sin gastar un token.
                                    onClicked: ag.probarModeloPropio()
                                    ToolTip.visible: hovered; ToolTip.delay: 400
                                    ToolTip.text: "Le pregunta al servidor si está y si la clave sirve"
                                }
                                Item { Layout.fillWidth: true }
                                Text { text: "Los cambios se aplican al reiniciar el asistente."
                                       color: Theme.textFaint; font.pixelSize: 10 }
                            }
                        }
                        RowLayout {
                            Toggle { checked: ag.autoStart; onToggled: (wanted) => ag.setAutoStart(wanted) }
                            Text { text: "Arrancar solo al abrir la app"; color: Theme.textDim; font.pixelSize: 12; Layout.fillWidth: true }
                        }
                        RowLayout {
                            Toggle { checked: ag.alertsToChat; onToggled: (wanted) => ag.setAlertsToChat(wanted) }
                            Text { text: "Avisos de los disparadores al chat"; color: Theme.textDim; font.pixelSize: 12; Layout.fillWidth: true; wrapMode: Text.WordWrap }
                        }
                        GlassButton { compact: true; text: "Carpeta de agentes"; icon: "▸"; onClicked: ag.openAgentsFolder() }
                        // Se actualizo desde la pestaña: se puede volver a la que
                        // fija esta version del sistema (la probada con el aparato).
                        GlassButton { compact: true; visible: ag.usingChosenVersion && !ag.installing
                                      text: "Volver a la versión fijada (" + ag.pinnedVersion + ")"; icon: "↺"
                                      onClicked: ag.revertToPinned() }
                    }
                }

                // LIMPIEZA: los chats que se acumulan, las versiones viejas del
                // programa, y reinstalar si algo falla (agent/AgentViewModel).
                Panel {
                    id: limpiezaPanel
                    Layout.fillWidth: true
                    Layout.preferredHeight: limpiezaCol.implicitHeight + 64
                    title: "Limpieza"
                    function mb(b) { return b >= 1073741824 ? (b / 1073741824).toFixed(1) + " GB" : Math.round(b / 1048576) + " MB" }
                    function aviso(r, hecho) {
                        if (r.ok === false) ApplicationWindow.window.toast(r.error, "error")
                        else ApplicationWindow.window.toast(hecho + ": se liberaron " + mb(r.bytes || 0), "ok")
                    }
                    Component.onCompleted: ag.revisarLimpieza()
                    onVisibleChanged: if (visible) ag.revisarLimpieza()
                    ColumnLayout {
                        id: limpiezaCol
                        anchors.fill: parent
                        spacing: 8
                        Text {
                            Layout.fillWidth: true; Layout.maximumWidth: limpiezaCol.width; wrapMode: Text.WordWrap
                            text: "Cada asistente guarda sus conversaciones en su perfil y se van acumulando. Borrarlas no toca " +
                                  "el login ni los ajustes. Si el asistente no arranca o anda raro, «Reinstalar» vuelve a bajar " +
                                  "el programa; «Desde cero» borra también el perfil y hay que volver a entrar. Nada de esto se " +
                                  "puede con el asistente corriendo."
                            color: Theme.textDim; font.pixelSize: 11
                        }
                        Repeater {
                            model: ag.limpieza.filter(m => m.runtime !== "espacios")
                            delegate: ColumnLayout {
                                required property var modelData
                                Layout.fillWidth: true
                                Layout.maximumWidth: limpiezaCol.width
                                spacing: 4
                                Text {
                                    text: modelData.runtime + (modelData.enUso ? "  (el que usa el asistente)" : "")
                                    color: Theme.text; font { pixelSize: 13; weight: Font.DemiBold }
                                }
                                Text {
                                    Layout.fillWidth: true; wrapMode: Text.WordWrap
                                    color: Theme.textDim; font.pixelSize: 11
                                    text: "Programa " + limpiezaPanel.mb(modelData.bytesPrograma) +
                                          (modelData.versiones.length > 0
                                           ? " (" + modelData.versiones.map(v => v.version + (v.enUso ? " en uso" : "")).join(", ") + ")" : "") +
                                          " · chats " + limpiezaPanel.mb(modelData.bytesChats) +
                                          " · perfil " + limpiezaPanel.mb(modelData.bytesPerfil)
                                }
                                Flow {
                                    Layout.fillWidth: true
                                    spacing: 6
                                    GlassButton {
                                        compact: true; text: "Borrar chats"
                                        enabled: !ag.running && modelData.bytesChats > 0
                                        onClicked: ApplicationWindow.window.confirm({
                                            title: "¿Borrar los chats de " + modelData.runtime + "?",
                                            text: limpiezaPanel.mb(modelData.bytesChats) + " de conversaciones guardadas. El login y los ajustes quedan.",
                                            accept: "Borrar", reject: "Cancelar",
                                            action: function() { limpiezaPanel.aviso(ag.borrarChats(modelData.runtime), "Chats borrados") }
                                        })
                                    }
                                    GlassButton {
                                        compact: true; text: "Versiones viejas"
                                        visible: modelData.versiones.filter(v => !v.enUso).length > 0
                                        enabled: !ag.running
                                        onClicked: limpiezaPanel.aviso(ag.borrarVersionesViejas(modelData.runtime), "Versiones viejas borradas")
                                    }
                                    GlassButton {
                                        compact: true; text: "Reinstalar"
                                        enabled: !ag.running && !ag.installing
                                        onClicked: ApplicationWindow.window.confirm({
                                            title: "¿Reinstalar " + modelData.runtime + "?",
                                            text: "Se borra el programa y se vuelve a bajar" +
                                                  (modelData.enUso ? "." : " cuando se lo elija.") + " El login y los chats quedan.",
                                            accept: "Reinstalar", reject: "Cancelar",
                                            action: function() {
                                                const e = ag.reinstalar(modelData.runtime, false)
                                                if (e !== "") ApplicationWindow.window.toast(e, "error")
                                            }
                                        })
                                    }
                                    GlassButton {
                                        compact: true; text: "Desde cero"
                                        enabled: !ag.running && !ag.installing
                                        onClicked: ApplicationWindow.window.confirm({
                                            title: "¿Reinstalar " + modelData.runtime + " desde cero?",
                                            text: "Se borra el programa y el perfil entero: chats, ajustes y el LOGIN (hay que volver a " +
                                                  "entrar). Las claves de API pegadas quedan.",
                                            accept: "Borrar todo", reject: "Cancelar",
                                            action: function() {
                                                const e = ag.reinstalar(modelData.runtime, true)
                                                if (e !== "") ApplicationWindow.window.toast(e, "error")
                                            }
                                        })
                                    }
                                }
                            }
                        }
                        Text {
                            Layout.fillWidth: true; Layout.maximumWidth: limpiezaCol.width; wrapMode: Text.WordWrap
                            readonly property var esp: ag.limpieza.find(m => m.runtime === "espacios")
                            text: "Espacios de trabajo de los agentes: " + limpiezaPanel.mb(esp ? esp.bytesChats : 0) +
                                  " (se rehacen solos al arrancar)"
                            color: Theme.textFaint; font.pixelSize: 11
                        }
                        Flow {
                            Layout.fillWidth: true
                            spacing: 6
                            GlassButton {
                                compact: true; text: "Borrar todos los chats"
                                enabled: !ag.running
                                onClicked: ApplicationWindow.window.confirm({
                                    title: "¿Borrar los chats de todos los asistentes?",
                                    text: "Las conversaciones guardadas de todos los motores y los espacios de trabajo. Los logins y los ajustes quedan.",
                                    accept: "Borrar", reject: "Cancelar",
                                    action: function() { limpiezaPanel.aviso(ag.borrarChats(""), "Chats borrados") }
                                })
                            }
                            GlassButton { compact: true; text: "Revisar"; onClicked: ag.revisarLimpieza() }
                        }
                    }
                }

                // Seguridad en vivo: lo que el asistente (y todo lo automático) no hace al aire sin preguntar.
                Panel {
                    id: segPanel
                    Layout.fillWidth: true
                    Layout.preferredHeight: segCol.implicitHeight + 64
                    title: "Seguridad en vivo"
                    property var info: ({})
                    Timer { interval: 2000; running: page.visible; repeat: true; triggeredOnStart: true; onTriggered: segPanel.info = ag.aireUi() }
                    ColumnLayout {
                        id: segCol
                        anchors.fill: parent
                        spacing: 6
                        RowLayout {
                            Text { text: segPanel.info.hay_aire ? "🔒" : "🔓"; font.pixelSize: 14 }
                            Text {
                                Layout.fillWidth: true; wrapMode: Text.WordWrap
                                text: segPanel.info.hay_aire
                                      ? "Al aire: consola (Bash) apagada; las acciones de aire las confirma una persona (Enter / Esc)."
                                      : "Fuera del aire. Al transmitir o grabar, la consola se apaga y estas acciones piden confirmación."
                                color: segPanel.info.hay_aire ? Theme.warn : Theme.textDim; font.pixelSize: 12
                            }
                        }
                        Flow {
                            Layout.fillWidth: true
                            spacing: 4
                            Repeater {
                                model: segPanel.info.herramientas || []
                                delegate: StatusPill {
                                    required property var modelData
                                    text: modelData.herramienta + (modelData.clase === "aire_si" ? " *" : "")
                                    dot: modelData.fija ? Theme.live : Theme.warn
                                    ToolTip.visible: segHov.hovered; ToolTip.delay: 400
                                    ToolTip.text: (modelData.por_que || "agregada") + (modelData.clase === "aire_si" ? " (según los argumentos)" : "")
                                    HoverHandler { id: segHov }
                                }
                            }
                        }
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
                    id: panelDisparadores
                    // Los de los plugins que se declaran disparadores (cuando ninguno de los tipos alcanza).
                    readonly property var dePlugins: Studio.plugins ? Studio.plugins.lista.filter(p => p.disparador) : []
                    title: "Disparadores (" + (ag.alerts.length + dePlugins.length) + ")"
                    ColumnLayout {
                        id: alertCol
                        anchors.fill: parent
                        spacing: 6
                        RowLayout {
                            Layout.fillWidth: true
                            Text { visible: ag.alerts.length === 0; Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.textFaint; font.pixelSize: 12
                                   text: "Pedile al asistente que vigile algo (audio, oradores, fin de un tema, lobby, chat, cortes, temporizadores) o armalo vos." }
                            Item { visible: ag.alerts.length > 0; Layout.fillWidth: true }
                            GlassButton { compact: true; text: "＋ Nuevo"; onClicked: editorDisparador.open() }
                        }
                        EditorDisparador { id: editorDisparador }
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
                                GlassButton {
                                    compact: true
                                    visible: (modelData.sonido || "ninguno") !== "ninguno"
                                    text: modelData.silenciada ? "🔇" : "🔔"
                                    onClicked: ag.setAlertSilenciada(modelData.id, !modelData.silenciada)
                                }
                                GlassButton { compact: true; text: "✕"; onClicked: ag.deleteAlert(modelData.id) }
                            }
                        }
                        Repeater {
                            model: panelDisparadores.dePlugins
                            delegate: RowLayout {
                                id: filaPlugin
                                required property var modelData
                                Layout.fillWidth: true
                                readonly property bool prendido: modelData.estado !== "apagado" && modelData.estado !== "caido"
                                Toggle { checked: filaPlugin.prendido; onToggled: (wanted) => Studio.plugins.prender(modelData.id, wanted) }
                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 0
                                    Text { text: modelData.nombre; color: filaPlugin.prendido ? Theme.text : Theme.textFaint; font.pixelSize: 12; elide: Text.ElideRight; Layout.fillWidth: true }
                                    Text {
                                        Layout.fillWidth: true; elide: Text.ElideRight
                                        text: "plugin · " + (modelData.dice ? modelData.dice.texto : modelData.estado) + (modelData.error ? " · " + modelData.error : "")
                                        color: modelData.error ? Theme.warn : Theme.textFaint; font.pixelSize: 10
                                    }
                                }
                                GlassButton { compact: true; text: "Plugins"; onClicked: ApplicationWindow.window.page = 23 }
                            }
                        }
                        Rectangle { Layout.fillWidth: true; height: 1; color: Theme.glassBorder; Layout.topMargin: 4 }
                        PanelNotificadores { Layout.fillWidth: true }
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
