// Sala de videoconferencia: lobby | participantes (con su video) + chat |
// acceso + archivos compartidos. Solo presentacion: todo sale de
// Studio.conference, que a su vez es un cliente de la API de control de B.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import BA
import "../controls"

Rectangle {
    id: page
    color: Theme.bg
    property real leftInset: 92
    readonly property var conf: Studio.conference
    // El resultado de una «Prueba técnica» (eco y red).
    Connections {
        target: page.conf
        function onPruebaLista(id, r) {
            Studio.notify("Prueba técnica: " + r.recomendacion + (r.red && r.red.rtt_ms !== undefined ? " (rtt " + Math.round(r.red.rtt_ms) + " ms, pérdida " + (r.red.perdida_pct || 0) + " %)" : ""),
                          r.eco || (r.red && r.red.semaforo === "rojo") || !r.ok ? "warn" : "ok")
        }
    }

    // La columna derecha (Uso, Acceso, Proxy…): se cambia de ancho con el
    // tirador de su borde izquierdo y, angosta, queda un riel de íconos (QR,
    // PIN, lobby, sala cerrada, consola). Cuando entra la segunda persona se
    // angosta sola para que las baldosas de la sala tengan lugar; cuando queda
    // una, vuelve al ancho que tenía.
    readonly property real anchoRiel: 64
    property real anchoDer: 374
    property real anchoDerAntes: 374
    readonly property bool derAngosta: anchoDer < 220
    property int genteAntes: 0
    function expandirDer() { anchoDer = anchoDerAntes >= 300 ? anchoDerAntes : 374 }

    // Reloj para los "hace N min" (sin tocar el modelo).
    property double now: Date.now()
    Timer { interval: 5000; running: page.visible; repeat: true; onTriggered: page.now = Date.now() }
    // El hilo de chat que se está leyendo: "" es la sala, o el uid de alguien.
    // Ver el comentario del TabBar: es también el destinatario de lo que se
    // escriba, para que no se pueda contestar en público algo privado.
    property string hiloChat: ""
    function abrirHilo(uid) {
        page.hiloChat = uid
        const h = conf.chatHilos
        for (let i = 0; i < h.length; ++i)
            if (h[i].uid === uid) { hilos.currentIndex = i + 1; return }
        // Todavía no hay hilo con esa persona: el tab aparece solo cuando se
        // manda el primer mensaje, y hasta entonces se escribe desde acá.
    }
    function since(ms) {
        return Theme.hace((page.now - ms) / 1000)
    }
    function mb(bytes) { return (bytes / 1048576).toFixed(1) + " MB" }
    /// kbit/s a algo que se lea de un vistazo.
    function mbits(kbps) {
        const k = kbps || 0
        return k >= 1000 ? (k / 1000).toFixed(1) + " Mb/s" : Math.round(k) + " kb/s"
    }
    function fmt(ms) {
        const neg = ms < 0
        const t = Math.floor(Math.abs(ms) / 1000)
        return (neg ? "+" : "") + Math.floor(t / 60) + ":" + String(t % 60).padStart(2, "0")
    }
    // Reloj de la sala para los tiempos de los oradores (4 veces por segundo).
    property double clock: Date.now() + conf.clockOffset
    Timer { interval: 250; running: page.visible && conf.speakers.count > 0; repeat: true
            onTriggered: page.clock = Date.now() + conf.clockOffset }

    ColumnLayout {
        anchors { fill: parent; leftMargin: page.leftInset; margins: 20 }
        spacing: 16

    // --- Oradores: fila de tarjetas + configuracion ------------------------------
        Panel {
            id: spkPanel
            Layout.fillWidth: true
            readonly property bool shown: conf.speakerMode || conf.speakers.count > 0
            Layout.preferredHeight: shown ? 400 : 64
            title: shown ? "Oradores" : ""
            ColumnLayout {
                anchors.fill: parent
                spacing: 10
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 10
                    Toggle { checked: conf.speakerMode; onToggled: (wanted) => conf.setSpeakerConfig({ enabled: wanted }) }
                    Text { text: "Modo oradores"; color: Theme.text; font.pixelSize: 13 }
                    Text { visible: !spkPanel.shown; Layout.fillWidth: true; elide: Text.ElideRight; color: Theme.textFaint; font.pixelSize: 12
                           text: "Los admitidos pasan a una fila (miran el programa) y los ponés al aire por turno, con un reloj que nunca los saca solo." }
                    RowLayout {
                        visible: conf.speakerMode
                        spacing: 8
                        Text { text: "Tiempo"; color: Theme.textDim; font.pixelSize: 12 }
                        TextField {
                            id: spkDefault
                            Layout.preferredWidth: 64
                            text: page.fmt((conf.speakerCfg.defaultSec || 0) * 1000)
                            font.pixelSize: 12
                            ToolTip.visible: hovered; ToolTip.text: "Tiempo general para cada orador (m:ss o minutos)"
                            onEditingFinished: { const s = conf.parseDuration(text); if (s > 0) conf.setSpeakerConfig({ defaultSec: s }) }
                        }
                        ComboBox {
                            model: ["Uno a la vez", "Varios"]
                            currentIndex: conf.speakerCfg.multi ? 1 : 0
                            onActivated: (i) => conf.setSpeakerConfig({ multi: i === 1 })
                        }
                        Text { text: "Slot"; color: Theme.textDim; font.pixelSize: 12 }
                        ComboBox {
                            Layout.preferredWidth: 64
                            model: Math.max(1, conf.maxGuests)
                            displayText: String(currentIndex + 1)
                            delegate: ItemDelegate { required property int index; text: String(index + 1); width: parent ? parent.width : 64 }
                            currentIndex: Math.max(0, (conf.speakerCfg.defaultSlot || 1) - 1)
                            onActivated: (i) => conf.setSpeakerConfig({ defaultSlot: i + 1 })
                        }
                        ComboBox {
                            id: carouselBox
                            readonly property var modes: ["manual", "chain", "time"]
                            model: ["Carrusel manual", "Al sacar, entra el siguiente", "Automático por tiempo"]
                            Layout.preferredWidth: 220
                            currentIndex: Math.max(0, modes.indexOf(conf.speakerCfg.carousel || "manual"))
                            onActivated: (i) => conf.setSpeakerConfig({ carousel: modes[i] })
                        }
                        SpinBox {
                            visible: conf.speakerCfg.carousel === "time"
                            from: 0; to: 600; stepSize: 5
                            value: conf.speakerCfg.graceSec || 0
                            editable: true
                            textFromValue: (v) => "+" + v + " s"
                            valueFromText: (t) => parseInt(t.replace(/[^0-9]/g, "")) || 0
                            // "+N s" no pasa el IntValidator por defecto (binding loop).
                            validator: RegularExpressionValidator { regularExpression: /\+?\s*\d{0,3}\s*s?/ }
                            ToolTip.visible: hovered; ToolTip.text: "Margen después de cero antes de rotar"
                            onValueModified: conf.setSpeakerConfig({ graceSec: value })
                        }
                    }
                    Item { Layout.fillWidth: true; visible: spkPanel.shown }
                    GlassButton { visible: conf.speakerMode; text: "Siguiente ▶"; compact: true; active: true; activeColor: Theme.ok; onClicked: conf.speakerNext() }
                    GlassButton {
                        id: boardBtn
                        visible: conf.speakerMode
                        text: "Tablero ▾"; compact: true
                        onClicked: boardMenu.popup(boardBtn, 0, boardBtn.height + 4)
                        Menu {
                            id: boardMenu
                            MenuItem { text: "Tablero en la red local"; onTriggered: qrDlg.show("board-local") }
                            MenuItem { text: "Tablero por internet"; onTriggered: qrDlg.show("board-remote") }
                        }
                    }
                }
                ListView {
                    id: spkList
                    visible: spkPanel.shown
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    orientation: ListView.Horizontal
                    spacing: 12
                    clip: true
                    model: conf.speakers
                    ScrollBar.horizontal: ScrollBar { policy: ScrollBar.AsNeeded }
                    delegate: SpeakerCard {
                        now: page.clock
                        onEdit: profDlg.edit(uid, name, info, contact, allottedSec)
                    }
                    Text {
                        anchors.centerIn: parent
                        visible: spkList.count === 0
                        text: "Nadie en la fila. Admití gente del lobby, o usá «→ Oradores» en alguien que ya esté en la sala."
                        color: Theme.textFaint; font.pixelSize: 13
                    }
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 16

            // --- Lobby ----------------------------------------------------------------
            Panel {
                Layout.preferredWidth: 310
                Layout.maximumWidth: 310
                Layout.fillHeight: true
                title: "Lobby — solo te ven a vos"
                ColumnLayout {
                    anchors.fill: parent
                    spacing: 10
                    // Que el asistente atienda SOLO a cada uno que llega (por
                    // chat o hablando), el alta automática en Personas y
                    // quién entra solo: lo mismo que en la Asamblea.
                    OpcionesAtencion { Layout.fillWidth: true; ambito: "sala" }
                    // --- Reconocer a los que llegan ------------------------
                    // EL LOBBY ES EL MOMENTO: la gente llega antes de entrar y
                    // se queda quieta frente a su cámara esperando. Es el único
                    // rato en que hay tiempo de saber quién es sin que nada de
                    // esto salga al aire.
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8
                        visible: Studio.personas.disponible
                        Toggle {
                            // Con la atención prendida se mira siempre: el
                            // interruptor lo muestra prendido y no se apaga acá.
                            checked: Studio.personas.lobbyMirando
                            enabled: !Studio.personas.lobbyPorAtencion
                            onToggled: (wanted) => Studio.personas.lobbyAuto = wanted
                        }
                        ColumnLayout {
                            spacing: 2
                            Layout.fillWidth: true
                            // preferredWidth en 0: un Text que envuelve pide,
                            // como ancho implícito, TODO el renglón sin cortar.
                            // Sin esto el panel entero se ensanchaba para darle
                            // ese ancho y el botón «QR ▾» de más abajo terminaba
                            // fuera del panel. Con 0, el texto se conforma con
                            // lo que le quede y envuelve, que es lo que se le
                            // pidió al ponerle WordWrap.
                            Layout.preferredWidth: 0
                            Text { text: "☻ Reconocer a los que llegan"
                                   color: Theme.text; font.pixelSize: 13 }
                            Text {
                                Layout.fillWidth: true
                                Layout.preferredWidth: 0
                                wrapMode: Text.WordWrap
                                color: Theme.textFaint; font.pixelSize: 11
                                text: Studio.personas.lobbyPorAtencion
                                      ? "Prendido por la atención: mientras el asistente atiende, el lobby se mira siempre."
                                      : Studio.personas.lobbyAuto
                                      ? "Sobre cada cámara dice quién es. Al que no conoce lo anota sin " +
                                        "nombre, con su foto, para que le pongas quién es."
                                      : "Mira las cámaras de los que esperan y te dice a quién reconoce."
                            }
                            // NO va acá el contador de fichas sin nombre de
                            // toda la libreta: en el panel del lobby se lee
                            // como si esa gente estuviera esperando, y puede
                            // venir de cualquier otra cámara. Ese número vive
                            // en la página de Personas, que es de donde habla.
                        }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        Text { text: conf.waiting.count === 0 ? "Nadie esperando" : conf.waiting.count + " esperando"
                               color: Theme.textDim; font.pixelSize: 12; Layout.fillWidth: true }
                        GlassButton { visible: conf.waiting.count > 1; text: "Admitir a todos"; compact: true; onClicked: conf.admitAll() }
                        // QR de ingreso: se despliega con las dos opciones.
                        GlassButton {
                            id: qrBtn
                            text: "QR ▾"; compact: true
                            onClicked: qrMenu.popup(qrBtn, 0, qrBtn.height + 4)
                            Menu {
                                id: qrMenu
                                MenuItem { text: "QR local (red local)"; onTriggered: qrDlg.show("local") }
                                MenuItem { text: "QR remoto (por internet)"; onTriggered: qrDlg.show("remote") }
                            }
                        }
                    }
                    ListView {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        spacing: 8
                        model: conf.waiting
                        add: Transition { NumberAnimation { property: "opacity"; from: 0; to: 1; duration: 220 } }
                        delegate: Rectangle {
                            id: waitCard
                            required property string uid
                            required property string name
                            required property bool connected
                            required property double joinedMs
                            required property int lobbySlot
                            required property bool talk
                            required property string info
                            required property string contact
                            width: ListView.view.width
                            height: waitCol.implicitHeight + 20
                            radius: 14
                            color: Qt.rgba(1, 1, 1, 0.05)
                            border.color: talk ? Theme.ok : Qt.rgba(1, 0.42, 0.17, 0.35)
                            border.width: talk ? 2 : 1
                            opacity: connected ? 1 : 0.55
                            ColumnLayout {
                                id: waitCol
                                anchors { fill: parent; margins: 10 }
                                spacing: 8
                                // Su camara: solo la ves vos (preview privado del lobby).
                                Rectangle {
                                    Layout.fillWidth: true
                                    // Del ancho de la columna: tomandolo del suyo propio
                                    // quedaba a medio calcular en el primer armado.
                                    Layout.preferredHeight: (waitCol.width - 20) * 9 / 16
                                    radius: 10
                                    color: "#000000"
                                    clip: true
                                    VideoItem { anchors.fill: parent; visible: lobbySlot > 0; source: "lobby:" + lobbySlot; fillMode: VideoItem.Cover }
                                    Text {
                                        anchors.centerIn: parent
                                        visible: lobbySlot === 0
                                        horizontalAlignment: Text.AlignHCenter
                                        text: "Sin lugar de preview\n(esperando que se libere)"
                                        color: Theme.textFaint; font.pixelSize: 11
                                    }
                                    StatusPill { anchors.left: parent.left; anchors.top: parent.top; anchors.margins: 6; visible: talk; text: "hablando"; dot: Theme.ok; pulse: true }

                                    // --- Quién es, según el reconocimiento ---
                                    // Va ENCIMA de su cámara y no al lado del
                                    // nombre que escribió él: son dos cosas
                                    // distintas. Lo que escribió es lo que dice
                                    // ser; esto es a quién se parece. Juntarlas
                                    // haría pasar una por la otra.
                                    Rectangle {
                                        // Por `uid` y no por el número de posición: las posiciones
                                        // se reciclan y el cartel del anterior le quedaría puesto
                                        // al siguiente.
                                        readonly property var quien: Studio.personas.enLobby[uid]
                                        visible: quien !== undefined
                                        anchors { left: parent.left; right: parent.right
                                                  bottom: parent.bottom; margins: 6 }
                                        height: quienTxt.implicitHeight + 10
                                        radius: 7
                                        color: Qt.rgba(0, 0, 0, 0.62)
                                        border.width: 1
                                        border.color: !parent.visible || quien === undefined ? "transparent"
                                                    : quien.nombre !== "" ? Theme.ok
                                                    : quien.id !== "" ? Theme.accent : Theme.warn
                                        Text {
                                            id: quienTxt
                                            anchors { fill: parent; margins: 5 }
                                            wrapMode: Text.WordWrap
                                            font.pixelSize: 11
                                            color: Theme.text
                                            text: {
                                                const q = parent.quien
                                                if (q === undefined) return ""
                                                if (q.nombre !== "")
                                                    return "✓ " + q.nombre +
                                                           (q.cargo !== "" ? " · " + q.cargo : "") +
                                                           "  " + Math.round(q.parecido * 100) + "%"
                                                if (q.id !== "")
                                                    return "Anotado sin nombre · " + q.caras + " cara(s)"
                                                return q.sirve ? "No lo tengo en la libreta"
                                                               : q.sugerencia
                                            }
                                        }
                                    }
                                }
                                RowLayout {
                                    spacing: 8
                                    ColumnLayout {
                                        spacing: 0
                                        Layout.fillWidth: true
                                        Text { text: name; color: Theme.text; font.pixelSize: 14; font.weight: Font.DemiBold; elide: Text.ElideRight; Layout.fillWidth: true }
                                        Text { text: (connected ? "espera hace " : "desconectado · ") + page.since(joinedMs); color: Theme.textFaint; font.pixelSize: 11 }
                                    }
                                }
                                // Flow y no RowLayout: con varios botones ya no
                                // entraban en el ancho de la tarjeta y la
                                // descuadraban (el video se salia del marco).
                                Flow {
                                    Layout.fillWidth: true
                                    spacing: 6
                                    GlassButton { text: "Admitir"; compact: true; active: true; activeColor: Theme.ok; onClicked: conf.admit(uid) }
                                    GlassButton { text: talk ? "Cortar" : "Hablar"; compact: true; enabled: lobbySlot > 0; active: talk; activeColor: Theme.ok
                                                  onClicked: conf.talk(uid, !talk) }
                                    GlassButton { text: "Rechazar"; compact: true; onClicked: conf.reject(uid) }
                                    GlassButton { text: "✉"; compact: true; onClicked: { chatTo.pick(uid); chatInput.forceActiveFocus() } }
                                    GlassButton {
                                        text: "✎"; compact: true
                                        onClicked: profDlg.editPerson(waitCard.uid, waitCard.name, waitCard.info, waitCard.contact)
                                        ToolTip.visible: hovered; ToolTip.delay: 400
                                        ToolTip.text: "Editar nombre, cargo y contacto"
                                    }
                                    // Que la atienda el asistente: le pide los datos por
                                    // chat y despues te muestra todo junto con su foto.
                                    GlassButton {
                                        text: "✦ Que lo atienda"; compact: true
                                        visible: Studio.agent.running
                                        onClicked: {
                                            const e = Studio.assistantIntake(waitCard.uid, waitCard.name)
                                            Studio.notify(
                                                e !== "" ? e : "El asistente está hablando con " + waitCard.name,
                                                e !== "" ? "error" : "ok")
                                        }
                                        ToolTip.visible: hovered; ToolTip.delay: 400
                                        ToolTip.text: "El asistente le pide nombre, cargo, cómo presentarlo y contacto, y te muestra todo junto con su foto para que decidas si pasa."
                                    }
                                }
                            }
                        }
                    }
                }
            }

            // --- Participantes + chat -----------------------------------------------
            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 16

                Panel {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    // Puede achicarse hasta acá. Sin un mínimo explícito, el
                    // layout no la deja bajar de lo que mide su contenido, y
                    // lo que no entra se desborda por abajo de la página en vez
                    // de apretarse: las baldosas de la sala tienen de sobra con
                    // menos, y quedarse sin Chat visible es peor.
                    Layout.minimumHeight: 220
                    title: "En la sala"
                    RowLayout {
                        anchors { right: parent.right; top: parent.top; topMargin: -34 }
                        spacing: 8
                        StatusPill { text: conf.online ? "Sala " + conf.room : "Sala sin conexion"; dot: conf.online ? Theme.ok : Theme.live; pulse: !conf.online }
                        StatusPill { visible: conf.online; text: (conf.maxGuests - conf.slotsFree) + "/" + conf.maxGuests + " lugares"; dot: conf.slotsFree > 0 ? Theme.ok : Theme.amber }
                        // Reinicia SOLO la sala (el gateway): relee su configuración (IP,
                        // puertos, túnel) sin cerrar el programa.
                        GlassButton {
                            compact: true; text: "Reiniciar la sala"; icon: "↻"
                            ToolTip.visible: hovered; ToolTip.delay: 400
                            ToolTip.text: "Reinicia solo el servidor de la sala (gateway) con su configuración actual, sin cerrar el programa. Los invitados se cortan y vuelven a entrar."
                            onClicked: ApplicationWindow.window.confirm({
                                title: "¿Reiniciar la sala?",
                                text: "Se reinicia solo el servidor de la sala con su configuración actual. Los invitados conectados se cortan y vuelven a entrar solos.",
                                accept: "Reiniciar", reject: "Cancelar",
                                action: function() {
                                    const e = conf.reiniciarSala()
                                    Studio.notify(e === "" ? "La sala se está reiniciando." : e, e === "" ? "ok" : "error")
                                }
                            })
                        }
                    }
                    GridView {
                        id: grid
                        anchors.fill: parent
                        onCountChanged: {
                            if (count > 1 && page.genteAntes <= 1 && !page.derAngosta) {
                                page.anchoDerAntes = page.anchoDer
                                page.anchoDer = page.anchoRiel
                            } else if (count <= 1 && page.genteAntes > 1 && page.derAngosta) {
                                page.expandirDer()
                            }
                            page.genteAntes = count
                        }
                        clip: true
                        readonly property int cols: count <= 1 ? 1 : 2
                        cellWidth: width / cols
                        cellHeight: Math.min(height / Math.max(1, Math.ceil(count / cols)), cellWidth * 0.75)
                        model: conf.inside
                        delegate: Item {
                            required property string uid
                            required property string name
                            required property int slot
                            required property bool connected
                            required property bool mediaUp
                            required property bool mic
                            required property bool cam
                            required property bool screen
                            required property bool hand
                            required property bool presenter
                            required property string speaker
                            required property string info
                            required property string contact
                            required property bool talk
                            width: grid.cellWidth
                            height: grid.cellHeight
                            Rectangle {
                                id: tarjeta
                                anchors { fill: parent; margins: 6 }
                                radius: 16
                                color: "#000000"
                                clip: true
                                border.color: presenter ? "#a78bfa" : hand ? Theme.amber : Theme.glassBorder
                                border.width: presenter || hand ? 2 : 1
                                // El video que B publica para ese slot (lo mismo que ve el motor).
                                // Sin lugar en el programa no hay video que mostrar
                                // (esta en la sala igual: se lo ve y se lo maneja).
                                VideoItem {
                                    anchors.fill: parent
                                    visible: slot > 0
                                    source: "guest:" + slot
                                    fillMode: VideoItem.Fit
                                    opacity: connected ? 1 : 0.4
                                }
                                Text {
                                    anchors.centerIn: parent
                                    visible: slot <= 0
                                    text: "sin lugar en el programa"
                                    color: Theme.textFaint; font.pixelSize: 12
                                }
                                Rectangle {
                                    anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
                                    height: 74
                                    gradient: Gradient {
                                        GradientStop { position: 0; color: "transparent" }
                                        GradientStop { position: 1; color: Qt.rgba(0, 0, 0, 0.8) }
                                    }
                                }
                                ColumnLayout {
                                    anchors { left: parent.left; right: parent.right; bottom: parent.bottom; margins: 8 }
                                    spacing: 6
                                    RowLayout {
                                        spacing: 8
                                        Rectangle { width: 22; height: 22; radius: 7; color: slot > 0 ? Theme.accent : Qt.rgba(1, 1, 1, 0.18)
                                            Text { anchors.centerIn: parent; text: slot > 0 ? slot : "–"; color: slot > 0 ? "#140a04" : "white"; font { pixelSize: 12; weight: Font.Bold } } }
                                        Text { text: name; color: "white"; font.pixelSize: 14; font.weight: Font.DemiBold; elide: Text.ElideRight; Layout.fillWidth: true }
                                        Text { visible: hand; text: "✋"; font.pixelSize: 16 }
                                        // Achicada, solo lo que avisa algo: el nombre tiene que leerse.
                                        StatusPill { visible: !barra.compacta || !mic; text: mic ? "mic" : "mic off"; dot: mic ? Theme.ok : Theme.live }
                                        StatusPill { visible: !barra.compacta || !cam; text: cam ? "cámara" : "cámara off"; dot: cam ? Theme.ok : Theme.live }
                                        StatusPill { visible: screen; text: "pantalla"; dot: Theme.accent }
                                        StatusPill { visible: presenter; text: "PRESENTADOR"; dot: "#a78bfa" }
                                        StatusPill { visible: speaker === "onair"; text: "ORADOR"; dot: Theme.live }
                                        StatusPill { visible: !barra.compacta || !connected || !mediaUp; text: !connected ? "reconectando" : mediaUp ? "en vivo" : "sin medios"; dot: !connected ? Theme.live : mediaUp ? Theme.ok : Theme.amber }
                                    }
                                    // Los controles, en una barra oscura propia (antes iban sueltos
                                    // sobre el video y no se veía que eran botones). Si no entran
                                    // (la tarjeta se achica cuando entra más gente), quedan
                                    // «Hablarle» (se mantiene apretado: no puede ir en un menú) y
                                    // «Controles ▾» con todo lo demás.
                                    Rectangle {
                                        id: barra
                                        Layout.fillWidth: true
                                        readonly property bool compacta: flujo.implicitHeight > tarjeta.height * 0.42
                                        implicitHeight: (compacta ? filaCompacta.implicitHeight : flujo.implicitHeight) + 12
                                        radius: 10
                                        clip: true
                                        color: Qt.rgba(0.05, 0.05, 0.07, 0.92)
                                        border.color: Qt.rgba(1, 1, 1, 0.12)
                                        Flow {
                                            id: flujo
                                            anchors { left: parent.left; right: parent.right; top: parent.top; margins: 6 }
                                            spacing: 6
                                            // Se sigue midiendo aunque no se vea: de su alto sale `compacta`.
                                            opacity: barra.compacta ? 0 : 1
                                            enabled: !barra.compacta
                                            GlassButton {
                                                compact: true
                                                text: mic ? "Silenciar" : "Activar micrófono"
                                                onClicked: conf.request(uid, mic ? "mute-mic" : "unmute-mic")
                                            }
                                            GlassButton {
                                                compact: true
                                                text: cam ? "Apagar cámara" : "Encender cámara"
                                                onClicked: conf.request(uid, cam ? "mute-cam" : "unmute-cam")
                                            }
                                            // IFB: mientras lo mantenés apretado le llega tu voz (mezclada con el
                                            // retorno, el programa baja). No sale al aire.
                                            GlassButton {
                                                compact: true; text: talk ? "🎙 Hablando…" : "🎙 Hablarle"
                                                active: talk; activeColor: Theme.ok
                                                onPressed2: conf.talk(uid, true)
                                                onReleased2: conf.talk(uid, false)
                                                onCanceled2: conf.talk(uid, false)
                                                ToolTip.visible: hovered && !pressed; ToolTip.delay: 400
                                                ToolTip.text: "Mantené apretado para hablarle (le llega tu voz por su retorno; no sale al aire)"
                                            }
                                            GlassButton {
                                                compact: true; text: "Prueba técnica"
                                                onClicked: { conf.pruebaTecnica(uid); Studio.notify("Probando a " + name + ": un tono por su retorno durante 4 s…", "info") }
                                                ToolTip.visible: hovered; ToolTip.delay: 400
                                                ToolTip.text: "Le manda un tono por el retorno y mide si vuelve por su micrófono (eco), y mira su red"
                                            }
                                            GlassButton { visible: screen; compact: true; text: "Cortar pantalla"; onClicked: conf.request(uid, "stop-screen") }
                                            GlassButton { visible: hand; compact: true; text: "Bajar mano"; onClicked: conf.request(uid, "lower-hand") }
                                            // Pasarlo a la fila de oradores o dejarlo de presentador
                                            // fijo: la sala sigue siendo la misma para todos.
                                            GlassButton {
                                                visible: !presenter && speaker !== "queued"
                                                compact: true; text: "→ Oradores"
                                                onClicked: conf.speakerRequeue(uid)
                                                ToolTip.visible: hovered; ToolTip.delay: 400
                                                ToolTip.text: "Lo pone en la fila de oradores (sale del aire hasta que le toque)"
                                            }
                                            GlassButton {
                                                compact: true
                                                text: presenter ? "Quitar presentador" : "★ Presentador"
                                                active: presenter
                                                activeColor: "#a78bfa"
                                                onClicked: conf.setPresenter(uid, !presenter)
                                                ToolTip.visible: hovered; ToolTip.delay: 400
                                                ToolTip.text: presenter ? "Deja de ser presentador y sale del aire"
                                                                        : "Se queda al aire con su lugar mientras rotan los oradores"
                                            }
                                            GlassButton {
                                                compact: true; text: "✉ Mensaje"
                                                // Abre SU hilo. Si todavía no hay
                                                // ninguno con esa persona, el tab
                                                // aparece con el primer mensaje.
                                                onClicked: { page.abrirHilo(uid); chatInput.forceActiveFocus() }
                                            }
                                            GlassButton {
                                                compact: true; text: "✎ Datos"
                                                onClicked: profDlg.editPerson(uid, name, info, contact)
                                                ToolTip.visible: hovered; ToolTip.delay: 400
                                                ToolTip.text: "Editar nombre, cargo y contacto"
                                            }
                                            GlassButton { compact: true; text: "Sacar"; onClicked: { kickDlg.who = uid; kickDlg.whoName = name; kickDlg.open() } }
                                        }
                                        RowLayout {
                                            id: filaCompacta
                                            visible: barra.compacta
                                            anchors { left: parent.left; right: parent.right; top: parent.top; margins: 6 }
                                            spacing: 6
                                            GlassButton {
                                                compact: true; text: talk ? "🎙 Hablando…" : "🎙 Hablarle"
                                                active: talk; activeColor: Theme.ok
                                                onPressed2: conf.talk(uid, true)
                                                onReleased2: conf.talk(uid, false)
                                                onCanceled2: conf.talk(uid, false)
                                                ToolTip.visible: hovered && !pressed; ToolTip.delay: 400
                                                ToolTip.text: "Mantené apretado para hablarle (le llega tu voz por su retorno; no sale al aire)"
                                            }
                                            GlassButton {
                                                id: controlesBtn
                                                compact: true; text: "Controles ▾"
                                                onClicked: controlesMenu.popup(controlesBtn, 0, controlesBtn.height + 4)
                                                Menu {
                                                    id: controlesMenu
                                                    MenuItem { text: mic ? "Silenciar" : "Activar micrófono"; onTriggered: conf.request(uid, mic ? "mute-mic" : "unmute-mic") }
                                                    MenuItem { text: cam ? "Apagar cámara" : "Encender cámara"; onTriggered: conf.request(uid, cam ? "mute-cam" : "unmute-cam") }
                                                    MenuItem { text: "Prueba técnica"; onTriggered: { conf.pruebaTecnica(uid); Studio.notify("Probando a " + name + ": un tono por su retorno durante 4 s…", "info") } }
                                                    MenuItem { visible: screen; height: visible ? implicitHeight : 0; text: "Cortar pantalla"; onTriggered: conf.request(uid, "stop-screen") }
                                                    MenuItem { visible: hand; height: visible ? implicitHeight : 0; text: "Bajar mano"; onTriggered: conf.request(uid, "lower-hand") }
                                                    MenuItem { visible: !presenter && speaker !== "queued"; height: visible ? implicitHeight : 0; text: "→ Oradores"; onTriggered: conf.speakerRequeue(uid) }
                                                    MenuItem { text: presenter ? "Quitar presentador" : "★ Presentador"; onTriggered: conf.setPresenter(uid, !presenter) }
                                                    MenuItem { text: "✉ Mensaje"; onTriggered: { page.abrirHilo(uid); chatInput.forceActiveFocus() } }
                                                    MenuItem { text: "✎ Datos"; onTriggered: profDlg.editPerson(uid, name, info, contact) }
                                                    MenuSeparator {}
                                                    MenuItem { text: "Sacar"; onTriggered: { kickDlg.who = uid; kickDlg.whoName = name; kickDlg.open() } }
                                                }
                                            }
                                            Item { Layout.fillWidth: true }
                                        }
                                    }
                                }
                            }
                        }
                        Text {
                            anchors.centerIn: parent
                            visible: grid.count === 0
                            horizontalAlignment: Text.AlignHCenter
                            text: conf.online ? "Todavía no entró nadie.\nCompartí el enlace y el PIN (panel Acceso)." : "Esperando al gateway…"
                            color: Theme.textFaint
                            font.pixelSize: 14
                        }
                    }
                }

                Panel {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 250
                    Layout.minimumHeight: 150
                    title: "Chat"
                    // EL TAB ES EL DESTINATARIO, y eso es el punto. Antes había
                    // una sola lista con todo mezclado y un selector aparte:
                    // la charla privada en la que se le piden los datos a
                    // alguien que llega se leía como si fuera para toda la
                    // sala, y contestar ahí con el selector en «A todos»
                    // mandaba a todo el mundo lo que era para uno. Con el
                    // destinatario pegado al hilo que estás leyendo, ese error
                    // no se puede cometer.
                    ColumnLayout {
                        anchors.fill: parent
                        spacing: 8
                        TabBar {
                            id: hilos
                            Layout.fillWidth: true
                            currentIndex: 0
                            onCurrentIndexChanged: page.hiloChat =
                                currentIndex === 0 || currentIndex > conf.chatHilos.length
                                    ? "" : conf.chatHilos[currentIndex - 1].uid
                            TabButton { text: "Sala"; width: implicitWidth }
                            Repeater {
                                model: conf.chatHilos
                                delegate: TabButton {
                                    required property var modelData
                                    text: "✉ " + (modelData.nombre !== "" ? modelData.nombre : "privado")
                                    width: implicitWidth
                                }
                            }
                        }
                        ListView {
                            id: chatList
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            clip: true
                            spacing: 6
                            // Se nombra `chatRev` para que esto se vuelva a
                            // pedir cuando llega un mensaje: QML no re-evalúa
                            // una función porque cambie un modelo que no mira.
                            model: { conf.chatRev; return conf.chatCon(page.hiloChat) }
                            onCountChanged: Qt.callLater(positionViewAtEnd)
                            delegate: Item {
                                id: msg
                                required property var modelData
                                readonly property string fromName: modelData.fromName
                                readonly property string to: modelData.to
                                readonly property string text: modelData.text
                                readonly property bool mine: modelData.mine === true
                                                             || modelData.fromId === "host"
                                width: ListView.view.width
                                height: bubble.height
                                Rectangle {
                                    id: bubble
                                    anchors.right: msg.mine ? parent.right : undefined
                                    width: Math.min(parent.width * 0.8, msgCol.implicitWidth + 22)
                                    height: msgCol.implicitHeight + 14
                                    radius: 12
                                    color: msg.mine ? Qt.rgba(1, 0.42, 0.17, 0.22) : Qt.rgba(1, 1, 1, 0.07)
                                    Column {
                                        id: msgCol
                                        anchors { left: parent.left; top: parent.top; margins: 7; leftMargin: 11 }
                                        width: Math.min(implicitWidth, chatList.width * 0.8 - 22)
                                        // 🎙: lo DIJO hablando (la atención hablada lo pasó a texto).
                                        Text { text: (msg.modelData.voz === true ? "🎙 " : "") + (msg.mine ? "Vos" : msg.fromName) + (msg.to === "all" ? "" : msg.to === "host" ? " → operador" : " → " + conf.nameOf(msg.to))
                                               color: Theme.textFaint; font.pixelSize: 10 }
                                        Text { text: msg.text; color: Theme.text; font.pixelSize: 13; wrapMode: Text.Wrap
                                               width: Math.min(implicitWidth, chatList.width * 0.8 - 22) }
                                    }
                                }
                            }
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            TextField {
                                id: chatInput
                                Layout.fillWidth: true
                                placeholderText: page.hiloChat === ""
                                                 ? "Escribí a toda la sala…"
                                                 : "En privado a " + conf.nameOf(page.hiloChat) + "…"
                                onAccepted: send()
                                function send() {
                                    if (text.trim() === "") return
                                    // Al hilo que se está leyendo. Ver el
                                    // comentario del TabBar.
                                    conf.sendChat(page.hiloChat === "" ? "all" : page.hiloChat, text)
                                    text = ""
                                }
                            }
                            GlassButton { text: "Enviar"; compact: true; onClicked: chatInput.send() }
                        }
                    }
                }
            }

            // --- Acceso + archivos ---------------------------------------------------
            // --- Columna derecha, CON SCROLL -----------------------------
            // Las tarjetas de acá suman más de 1300 px de alto fijo. En una
            // ventana normal eso no entra: sin scroll, la columna se desbordaba
            // hacia abajo y lo que sobraba quedaba debajo del zócalo del
            // asistente —parecía que la barra se superponía, y en realidad era
            // la página la que se salía de su lugar—. Acá se scrollea, que es
            // lo que hace cualquier panel lateral que no entra.
            Item {
                id: colDerMarco
                Layout.preferredWidth: page.anchoDer
                Layout.minimumWidth: page.anchoDer
                Layout.maximumWidth: page.anchoDer
                Layout.fillWidth: false
                Layout.fillHeight: true

                // El vestigio: siempre está, aunque la columna sea un riel.
                // Arrastrarlo cambia el ancho; soltado muy angosto, queda el riel.
                Rectangle {
                    id: tirador
                    anchors { left: parent.left; top: parent.top; bottom: parent.bottom }
                    width: 10
                    radius: 5
                    color: tiradorMa.containsMouse || tiradorMa.pressed ? Qt.rgba(1, 1, 1, 0.10) : "transparent"
                    Column {
                        anchors.centerIn: parent
                        spacing: 3
                        Repeater { model: 3; Rectangle { width: 3; height: 3; radius: 1.5; color: Theme.textFaint } }
                    }
                    MouseArea {
                        id: tiradorMa
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.SizeHorCursor
                        property real x0: 0
                        property real ancho0: 0
                        onPressed: (m) => { x0 = mapToItem(page, m.x, 0).x; ancho0 = page.anchoDer }
                        onPositionChanged: (m) => {
                            if (!pressed) return
                            const dx = mapToItem(page, m.x, 0).x - x0
                            page.anchoDer = Math.max(page.anchoRiel, Math.min(560, ancho0 - dx))
                        }
                        onReleased: {
                            if (page.anchoDer < 260) page.anchoDer = page.anchoRiel
                            else { page.anchoDer = Math.max(300, page.anchoDer); page.anchoDerAntes = page.anchoDer }
                        }
                        onDoubleClicked: page.derAngosta ? page.expandirDer() : (page.anchoDer = page.anchoRiel)
                        ToolTip.visible: containsMouse && !pressed; ToolTip.delay: 600
                        ToolTip.text: "Arrastrá para cambiar el ancho (doble clic: abrir o cerrar)"
                    }
                }

                // Angosta: los atajos de lo que más se usa.
                ColumnLayout {
                    id: riel
                    visible: page.derAngosta
                    anchors { left: tirador.right; right: parent.right; top: parent.top; leftMargin: 4 }
                    spacing: 8
                    GlassButton {
                        Layout.alignment: Qt.AlignHCenter
                        icon: "⟨"; compact: true
                        onClicked: page.expandirDer()
                        ToolTip.visible: hovered; ToolTip.delay: 300; ToolTip.text: "Abrir el panel (Acceso, uso de la sala…)"
                    }
                    GlassButton {
                        Layout.alignment: Qt.AlignHCenter
                        icon: "⊞"; compact: true; active: true
                        onClicked: qrDlg.show("local")
                        ToolTip.visible: hovered; ToolTip.delay: 300; ToolTip.text: "Invitar con QR"
                    }
                    GlassButton {
                        Layout.alignment: Qt.AlignHCenter
                        text: "PIN"; compact: true
                        onClicked: { Studio.copiar(conf.pin); Studio.notify("PIN copiado: " + conf.pin, "info") }
                        ToolTip.visible: hovered; ToolTip.delay: 300; ToolTip.text: "Copiar el PIN de la sala"
                    }
                    GlassButton {
                        Layout.alignment: Qt.AlignHCenter
                        icon: "⏸"; compact: true
                        active: conf.lobby
                        onClicked: conf.setLobby(!conf.lobby)
                        ToolTip.visible: hovered; ToolTip.delay: 300
                        ToolTip.text: conf.lobby ? "Lobby de admisión: prendido (vos decidís quién entra)" : "Lobby de admisión: apagado (entran directo)"
                    }
                    GlassButton {
                        Layout.alignment: Qt.AlignHCenter
                        icon: "🔒"; compact: true
                        active: conf.locked; activeColor: Theme.live
                        onClicked: conf.setLocked(!conf.locked)
                        ToolTip.visible: hovered; ToolTip.delay: 300
                        ToolTip.text: conf.locked ? "Sala cerrada: nadie nuevo puede pedir entrar" : "Sala abierta a quien tenga el PIN"
                    }
                }

            ScrollView {
                id: colDer
                visible: !page.derAngosta
                anchors { left: tirador.right; right: parent.right; top: parent.top; bottom: parent.bottom; leftMargin: 4 }
                clip: true
                // Sin barra horizontal: la columna tiene ancho fijo y lo que
                // sobra es alto. Una barra de abajo acá solo roba 12 px.
                ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
                contentWidth: availableWidth

            ColumnLayout {
                width: colDer.availableWidth
                spacing: 16

                // Uso de la sala: lo que se satura cuando hay mucha gente es el
                // procesador del gateway (un video por persona que decodificar).
                Panel {
                    Layout.fillWidth: true
                    Layout.preferredHeight: usoCol.implicitHeight + 56
                    title: "Uso de la sala"
                    ColumnLayout {
                        id: usoCol
                        anchors.fill: parent
                        spacing: 6
                        readonly property var u: conf.usage
                        readonly property real cpu: u.cpuPct !== undefined ? u.cpuPct : 0
                        readonly property bool apretado: cpu >= 70
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 6
                            StatusPill {
                                text: (usoCol.u.enSala !== undefined ? usoCol.u.enSala : 0) + "/" +
                                      (usoCol.u.lugares !== undefined ? usoCol.u.lugares : 0) + " en la sala"
                                dot: Theme.accent
                            }
                            StatusPill {
                                text: (usoCol.u.conVideo !== undefined ? usoCol.u.conVideo : 0) + " con video"
                                dot: Theme.ok
                            }
                            StatusPill {
                                visible: (usoCol.u.conectados !== undefined ? usoCol.u.conectados : 0) >
                                         (usoCol.u.enSala !== undefined ? usoCol.u.enSala : 0)
                                text: ((usoCol.u.conectados !== undefined ? usoCol.u.conectados : 0) -
                                       (usoCol.u.enSala !== undefined ? usoCol.u.enSala : 0)) + " esperando"
                                dot: Theme.amber
                            }
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            Text { text: "Procesador del gateway"; color: Theme.textDim; font.pixelSize: 12; Layout.fillWidth: true }
                            Text {
                                text: Math.round(usoCol.cpu) + "%"
                                color: usoCol.apretado ? Theme.live : Theme.text
                                font { pixelSize: 13; weight: Font.DemiBold }
                            }
                        }
                        Rectangle {
                            Layout.fillWidth: true
                            height: 6; radius: 3
                            color: Qt.rgba(1, 1, 1, 0.10)
                            Rectangle {
                                width: parent.width * Math.min(1, usoCol.cpu / 100)
                                height: parent.height; radius: 3
                                color: usoCol.cpu >= 85 ? Theme.live : usoCol.cpu >= 70 ? Theme.amber : Theme.ok
                                Behavior on width { NumberAnimation { duration: 300 } }
                            }
                        }
                        // Ancho de banda: el otro limite posible. Con muchos
                        // invitados puede sobrar CPU y faltar red, y hasta
                        // ahora no habia forma de saberlo.
                        RowLayout {
                            Layout.fillWidth: true
                            visible: usoCol.u.kbpsEntrada !== undefined
                            Text {
                                Layout.fillWidth: true
                                text: "Video por la red"
                                color: Theme.textDim; font.pixelSize: 11
                            }
                            Text {
                                text: "↓ " + page.mbits(usoCol.u.kbpsEntrada) +
                                      "   ↑ " + page.mbits(usoCol.u.kbpsSalida)
                                color: Theme.textFaint
                                font { family: Theme.mono; pixelSize: 11 }
                            }
                        }
                        // Hilo mas cargado: si uno solo se va a 100 mientras el
                        // total esta bajo, el techo es ese nucleo y no la maquina.
                        RowLayout {
                            Layout.fillWidth: true
                            visible: usoCol.u.hiloTop !== undefined
                            Text {
                                Layout.fillWidth: true
                                text: "Hilo más cargado: " + (usoCol.u.hiloTop || "")
                                color: Theme.textDim; font.pixelSize: 11; elide: Text.ElideRight
                            }
                            Text {
                                text: Math.round(usoCol.u.hiloPct || 0) + "% de un núcleo"
                                color: (usoCol.u.hiloPct || 0) >= 85 ? Theme.live
                                     : (usoCol.u.hiloPct || 0) >= 65 ? Theme.amber : Theme.textFaint
                                font { pixelSize: 11; weight: Font.DemiBold }
                            }
                        }
                        Text {
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                            color: usoCol.apretado ? Theme.amber : Theme.textFaint
                            font.pixelSize: 11
                            text: usoCol.u.cpuPct === undefined
                                  ? "Esperando datos del gateway…"
                                  : usoCol.apretado
                                    ? "Está apretado: no sumes más invitados con cámara. Pedí que apaguen la suya a quien no esté hablando."
                                    : "Memoria " + (usoCol.u.memMb !== undefined ? usoCol.u.memMb : 0) + " MB · " +
                                      (usoCol.u.nucleos !== undefined ? usoCol.u.nucleos : 0) + " núcleos. " +
                                      "Cada invitado con cámara es un video que hay que decodificar."
                        }
                    }
                }

                Panel {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 352
                    title: "Acceso"
                    ColumnLayout {
                        anchors.fill: parent
                        spacing: 10
                        // Invitar: el QR es lo que se le muestra a la persona.
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 6
                            GlassButton {
                                Layout.fillWidth: true
                                text: "Invitar con QR"; icon: "⊞"; active: true
                                onClicked: qrDlg.show("local")
                                ToolTip.visible: hovered; ToolTip.delay: 400
                                ToolTip.text: "Muestra el código para escanear. Adentro se elige red local o internet, y si el PIN va incluido."
                            }
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            Text { text: "PIN"; color: Theme.textDim; font.pixelSize: 12 }
                            Text {
                                id: pinText
                                property bool shown: false
                                text: shown ? (conf.pin || "—") : "••••••"
                                color: Theme.text
                                font { pixelSize: 20; weight: Font.DemiBold; letterSpacing: 3 }
                                Layout.fillWidth: true
                            }
                            GlassButton { text: pinText.shown ? "Ocultar" : "Ver"; compact: true; onClicked: pinText.shown = !pinText.shown }
                            GlassButton { text: "Nuevo"; compact: true; onClicked: pinDlg.open() }
                            GlassButton { text: "Bloqueos"; compact: true; onClicked: bloqueosDlg.open() }
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            Toggle { checked: conf.lobby; onToggled: (wanted) => conf.setLobby(wanted) }
                            ColumnLayout {
                                spacing: 0
                                Layout.fillWidth: true
                                Text { text: "Lobby de admisión"; color: Theme.text; font.pixelSize: 13 }
                                Text { text: conf.lobby ? "Vos decidís quién entra" : "Entran directo mientras haya lugar"; color: Theme.textFaint; font.pixelSize: 11 }
                            }
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            Toggle { checked: conf.locked; onToggled: (wanted) => conf.setLocked(wanted) }
                            ColumnLayout {
                                spacing: 0
                                Layout.fillWidth: true
                                Text { text: "Sala cerrada"; color: Theme.text; font.pixelSize: 13 }
                                Text { text: conf.locked ? "Nadie nuevo puede pedir entrar" : "Abierta a quien tenga el PIN"; color: Theme.textFaint; font.pixelSize: 11 }
                            }
                        }
                        // Lo que ve y oye quien espera en el lobby: solo vos.
                        RowLayout {
                            Layout.fillWidth: true
                            Text { text: "Te ven con"; color: Theme.textDim; font.pixelSize: 12; Layout.preferredWidth: 78 }
                            ComboBox {
                                Layout.fillWidth: true
                                model: ["(placa de espera)"].concat(conf.videoSourceNames)
                                currentIndex: Math.max(0, model.indexOf(conf.operatorVideo))
                                onActivated: (i) => conf.setOperatorFeed(i === 0 ? "" : model[i], conf.operatorAudio)
                                onPressedChanged: if (pressed) conf.refreshSources()
                            }
                        }
                        // El micrófono del operador se elige en Configuración -> Voz del sistema:
                        // es el mismo para el lobby y para dictarle al asistente.
                        RowLayout {
                            Layout.fillWidth: true
                            Text { text: "Te oyen con"; color: Theme.textDim; font.pixelSize: 12; Layout.preferredWidth: 78 }
                            Text {
                                Layout.fillWidth: true
                                text: conf.operatorAudio !== "" ? conf.operatorAudio : "(sin micrófono elegido)"
                                color: conf.operatorAudio !== "" ? Theme.text : Theme.textFaint
                                font.pixelSize: 13; elide: Text.ElideRight
                            }
                            GlassButton {
                                compact: true; text: "Cambiar…"
                                onClicked: { Studio.seccionConfig = "Voz del sistema"; ApplicationWindow.window.showPage(Studio.riel.paginaDeId("configuracion")) }
                            }
                        }
                    }
                }

                // --- Proxy por nombre (T61): un DNS y un certificado por servicio ---------
                Panel {
                    id: proxyPanel
                    Layout.fillWidth: true
                    Layout.preferredHeight: proxyCol.implicitHeight + 56
                    title: "Proxy por nombre (un DNS por servicio)"
                    readonly property var px: conf.proxy || ({})
                    readonly property var cfgp: px.configuracion || ({})
                    readonly property var acme: px.acme || ({})
                    Timer { interval: 5000; repeat: true; running: proxyPanel.visible && conf.online; triggeredOnStart: true; onTriggered: conf.refrescarProxy() }
                    function fecha(s) { return s > 0 ? new Date(s * 1000).toLocaleDateString(Qt.locale("es_AR"), "d/M/yyyy") : "—" }
                    ColumnLayout {
                        id: proxyCol
                        anchors.fill: parent
                        spacing: 8
                        RowLayout {
                            spacing: 8
                            StatusPill { text: proxyPanel.px.activo ? "andando en " + proxyPanel.px.puertoTls + (proxyPanel.px.puertoHttp ? "/" + proxyPanel.px.puertoHttp : "") : "apagado"
                                         dot: proxyPanel.px.activo ? Theme.ok : Theme.textFaint }
                            StatusPill { visible: !!proxyPanel.acme.activo
                                         text: proxyPanel.acme.andando ? "pidiendo certificados…" : (proxyPanel.acme.ultimo_error ? "ACME: error" : "ACME al día")
                                         dot: proxyPanel.acme.ultimo_error ? Theme.amber : Theme.ok; pulse: !!proxyPanel.acme.andando }
                            Text { visible: !!proxyPanel.px.activo; text: (proxyPanel.px.conexiones || 0) + " conexiones"; color: Theme.textFaint; font.pixelSize: 11 }
                        }
                        Text { Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.textDim; font.pixelSize: 11
                               text: "Cada servicio en su nombre: sala.<dominio>, sesion.<dominio>, botonera.<dominio>… Apuntá esos nombres a esta máquina en el DNS. Con ACME, Let's Encrypt firma cada uno (hace falta que el 80 llegue desde internet); si no, el certificado de la casa." }
                        Text { visible: !!proxyPanel.acme.ultimo_error; Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.amber; font.pixelSize: 11; text: proxyPanel.acme.ultimo_error || "" }
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 6
                            CheckBox { id: pxActivo; text: "Activo"; checked: !!proxyPanel.cfgp.activo }
                            TextField { id: pxDominio; Layout.fillWidth: true; placeholderText: "dominio (p. ej. midominio.org)"; text: proxyPanel.cfgp.dominio || "" }
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 6
                            CheckBox { id: pxAcme; text: "Certificados de Let's Encrypt"; checked: !!(proxyPanel.cfgp.acme && proxyPanel.cfgp.acme.activo) }
                            TextField { id: pxEmail; Layout.fillWidth: true; placeholderText: "email para avisos de la CA"; text: proxyPanel.cfgp.acme ? (proxyPanel.cfgp.acme.email || "") : "" }
                            CheckBox { id: pxPruebas; text: "de prueba"; checked: !!(proxyPanel.cfgp.acme && proxyPanel.cfgp.acme.pruebas) }
                        }
                        RowLayout {
                            Item { Layout.fillWidth: true }
                            GlassButton {
                                compact: true; text: "Guardar y reiniciar la sala"
                                onClicked: ApplicationWindow.window.confirm({
                                    title: "¿Reiniciar la sala con el proxy nuevo?",
                                    text: "Los invitados conectados se cortan y vuelven a entrar.",
                                    accept: "Reiniciar", reject: "Cancelar",
                                    action: function() {
                                        const e = conf.configurarProxy({ activo: pxActivo.checked, dominio: pxDominio.text.trim(),
                                                                         acme: { activo: pxAcme.checked, email: pxEmail.text.trim(), pruebas: pxPruebas.checked } })
                                        Studio.notify(e === "" ? "Proxy guardado: la sala se reinicia." : e, e === "" ? "ok" : "error")
                                    }
                                })
                            }
                        }
                        Repeater {
                            model: proxyPanel.px.servicios || []
                            delegate: RowLayout {
                                required property var modelData
                                Layout.fillWidth: true
                                spacing: 8
                                Text { text: modelData.host; color: Theme.text; font.pixelSize: 12; Layout.preferredWidth: 240; elide: Text.ElideRight }
                                Text { text: "→ :" + modelData.destino + modelData.ruta + (modelData.soloLan ? " · solo red local" : ""); color: Theme.textFaint; font.pixelSize: 11; Layout.fillWidth: true; elide: Text.ElideRight }
                                StatusPill { text: (modelData.certificado.origen === "acme" ? "Let's Encrypt" : "de la casa") + " · vence " + proxyPanel.fecha(modelData.certificado.vence)
                                             dot: modelData.certificado.origen === "acme" ? Theme.ok : Theme.amber }
                                GlassButton { compact: true; text: "Renovar"; visible: !!proxyPanel.acme.activo; onClicked: { conf.renovarProxy(modelData.host); Studio.notify("Pidiendo el certificado de " + modelData.host + "…", "info") } }
                            }
                        }
                    }
                }

                // --- Acceso directo: HTTPS propio + tunel WireGuard -------------------
                Panel {
                    id: direct
                    Layout.fillWidth: true
                    Layout.preferredHeight: 196
                    title: "Acceso directo"
                    readonly property var net: conf.network
                    readonly property var wg: net && net.wireguard ? net.wireguard : ({})
                    readonly property var tls: net && net.https ? net.https : ({})
                    ColumnLayout {
                        anchors.fill: parent
                        spacing: 8
                        RowLayout {
                            spacing: 6
                            StatusPill {
                                text: direct.tls.enabled ? (direct.tls.selfSigned ? "HTTPS (autofirmado)" : "HTTPS") : "Sin HTTPS"
                                dot: direct.tls.enabled ? Theme.ok : Theme.amber
                            }
                            StatusPill {
                                readonly property var w: direct.wg
                                text: !w.enabled ? "Tunel apagado" : (w.up ? "Tunel conectado" : "Tunel sin conexion")
                                dot: !w.enabled ? Theme.textFaint : (w.up ? Theme.ok : Theme.live)
                                pulse: !!w.enabled && !w.up
                            }
                        }
                        Text {
                            Layout.fillWidth: true
                            readonly property var w: direct.wg
                            text: w.enabled ? ("Publico: " + (w.publicAddress || "—") + "  ·  servidor " + (w.endpoint || "—"))
                                            : "Los invitados entran por Cloudflare o por la red local."
                            color: Theme.textDim; font.pixelSize: 11; elide: Text.ElideRight
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            TextField { Layout.fillWidth: true; readOnly: true; font.pixelSize: 11
                                        id: bestUrlField; text: direct.net && direct.net.bestUrl ? direct.net.bestUrl : "" }
                            GlassButton { text: "Copiar"; compact: true; onClicked: Studio.copiar(bestUrlField.text) }
                        }
                        GlassButton { text: "Configurar servidor WireGuard…"; compact: true; enabled: conf.online; onClicked: wgDlg.open() }
                    }
                }

                Panel {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    title: "Archivos compartidos"
                    ListView {
                        id: sharesList
                        anchors.fill: parent
                        clip: true
                        spacing: 8
                        model: conf.shares
                        delegate: Rectangle {
                            required property string uid
                            required property string name
                            required property string kind
                            required property double bytes
                            required property string participantName
                            required property double atMs
                            required property string url
                            width: ListView.view.width
                            height: 76
                            radius: 14
                            color: Qt.rgba(1, 1, 1, 0.05)
                            RowLayout {
                                anchors { fill: parent; margins: 8 }
                                spacing: 10
                                Rectangle {
                                    Layout.preferredWidth: 96
                                    Layout.fillHeight: true
                                    radius: 10
                                    color: "#000000"
                                    clip: true
                                    Image { anchors.fill: parent; visible: kind === "image"; source: kind === "image" ? url : ""
                                            fillMode: Image.PreserveAspectCrop; asynchronous: true; sourceSize.width: 192 }
                                    Text { anchors.centerIn: parent; visible: kind !== "image"; text: kind === "video" ? "▶" : "♪"; color: Theme.textDim; font.pixelSize: 26 }
                                }
                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 2
                                    Text { text: name; color: Theme.text; font.pixelSize: 13; font.weight: Font.DemiBold; elide: Text.ElideMiddle; Layout.fillWidth: true }
                                    Text { text: participantName + " · " + page.mb(bytes) + " · hace " + page.since(atMs); color: Theme.textFaint; font.pixelSize: 11; elide: Text.ElideRight; Layout.fillWidth: true }
                                    RowLayout {
                                        spacing: 6
                                        GlassButton { text: "Tomar"; compact: true; active: true; onClicked: conf.takeShare(uid) }
                                        GlassButton { text: "Borrar"; compact: true; onClicked: conf.removeShare(uid) }
                                    }
                                }
                            }
                        }
                        Text {
                            anchors.centerIn: parent
                            width: parent.width - 20
                            visible: sharesList.count === 0
                            horizontalAlignment: Text.AlignHCenter
                            wrapMode: Text.Wrap
                            text: "Los invitados pueden compartir imágenes, videos o audio desde su pantalla.\n«Tomar» los agrega a la escena actual."
                            color: Theme.textFaint
                            font.pixelSize: 12
                        }
                    }
                }
            } // ColumnLayout de la columna derecha
            } // ScrollView
            } // colDerMarco
        }
    }

    // Datos del orador (los carga el invitado al entrar; aca se corrigen).
    Dialog {
        id: profDlg
        anchors.centerIn: parent
        modal: true
        title: "Datos de " + (profDlg.whoName !== "" ? "«" + profDlg.whoName + "»" : "la persona")
        standardButtons: Dialog.Save | Dialog.Cancel
        property string who: ""
        property string whoName: ""
        property bool withTime: true
        function edit(id, name, info, contact, sec) {
            who = id; whoName = name; withTime = true
            profName.text = name; profInfo.text = info; profContact.text = contact
            profTime.text = page.fmt(sec * 1000)
            open()
        }
        /// Desde el lobby o la sala: los mismos datos, sin el tiempo de orador.
        function editPerson(id, name, info, contact) {
            who = id; whoName = name; withTime = false
            profName.text = name; profInfo.text = info; profContact.text = contact
            profTime.text = ""
            open()
        }
        onAccepted: conf.guardarFicha(who, profName.text, profInfo.text, profContact.text,
                                      withTime ? profTime.text : "")
        GridLayout {
            columns: 2
            columnSpacing: 10
            rowSpacing: 8
            Text { text: "Nombre"; color: Theme.textDim }
            TextField { id: profName; Layout.preferredWidth: 300; maximumLength: 40 }
            Text { text: "Información"; color: Theme.textDim }
            TextField { id: profInfo; Layout.preferredWidth: 300; maximumLength: 200 }
            Text { text: "Contacto"; color: Theme.textDim }
            TextField { id: profContact; Layout.preferredWidth: 300; maximumLength: 120 }
            Text { text: "Tiempo"; color: Theme.textDim; visible: profDlg.withTime }
            TextField { id: profTime; Layout.preferredWidth: 90; placeholderText: "m:ss"; visible: profDlg.withTime }
        }
    }

    Dialog {
        id: kickDlg
        property string who: ""
        property string whoName: ""
        anchors.centerIn: parent
        modal: true
        title: "¿Sacar a " + whoName + " de la sala?"
        standardButtons: Dialog.Ok | Dialog.Cancel
        onAccepted: conf.kick(who)
    }

    // QR para entrar al lobby (lo arma la API de control del gateway).
    DialogoQrSala { id: qrDlg; conf: page.conf }

    // Asistente: claves + wg0.conf para el servidor con IP publica.
    Dialog {
        id: wgDlg
        anchors.centerIn: parent
        width: Math.min(page.width - 80, 760)
        modal: true
        title: "Acceso directo por un servidor propio (WireGuard)"
        property string config: ""
        property string error: ""
        onOpened: { config = ""; error = "" }
        standardButtons: Dialog.Close
        Connections {
            target: conf
            function onServerConfigReady(config, error) { wgDlg.config = config; wgDlg.error = error }
        }
        ColumnLayout {
            anchors.fill: parent
            spacing: 10
            Text {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                color: Theme.textDim; font.pixelSize: 12
                text: "Un servidor con IP publica (un VPS con Linux) le da a esta sala su propia direccion en internet: los invitados entran directo, sin Cloudflare. " +
                      "Este equipo se conecta por WireGuard en espacio de usuario: no necesita permisos de administrador ni drivers."
            }
            GridLayout {
                columns: 2
                columnSpacing: 10; rowSpacing: 6
                Layout.fillWidth: true
                Text { text: "Servidor (host:puerto)"; color: Theme.text; font.pixelSize: 12 }
                TextField { id: wgEndpoint; Layout.fillWidth: true; placeholderText: "vps.midominio.com:51820" }
                Text { text: "IP o dominio publico"; color: Theme.text; font.pixelSize: 12 }
                TextField { id: wgPublic; Layout.fillWidth: true; placeholderText: "(el mismo host si se deja vacio)" }
                Text { text: "Interfaz publica del servidor"; color: Theme.text; font.pixelSize: 12 }
                TextField { id: wgIface; Layout.fillWidth: true; text: "eth0" }
            }
            GlassButton { text: "Generar configuracion"; active: true; compact: true
                          onClicked: conf.configureDirectAccess(wgEndpoint.text, wgPublic.text, wgIface.text) }
            Text { visible: wgDlg.error !== ""; text: wgDlg.error; color: Theme.live; font.pixelSize: 12 }
            Text {
                visible: wgDlg.config !== ""
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                color: Theme.textDim; font.pixelSize: 12
                text: "1) En el servidor, guarda esto como /etc/wireguard/wg0.conf y activalo (instrucciones en el encabezado). " +
                      "2) Este equipo ya quedo configurado y el gateway se reinicia solo. Contiene la clave PRIVADA del servidor: no la compartas."
            }
            ScrollView {
                visible: wgDlg.config !== ""
                Layout.fillWidth: true
                Layout.preferredHeight: 260
                TextArea { readOnly: true; text: wgDlg.config; font.family: Theme.mono; font.pixelSize: 11; wrapMode: TextEdit.NoWrap; selectByMouse: true }
            }
            GlassButton { visible: wgDlg.config !== ""; text: "Copiar configuracion"; compact: true; onClicked: Studio.copiar(wgDlg.config) }
        }
    }

    // Los que quedaron bloqueados por poner mal el PIN (por IP, y el general de último
    // recurso). Desbloquear no cambia el PIN: si alguien lo está adivinando, mejor «Nuevo».
    Dialog {
        id: bloqueosDlg
        anchors.centerIn: parent
        modal: true
        title: "Bloqueos por PIN mal"
        standardButtons: Dialog.Close
        readonly property var b: conf.bloqueosPin || ({})
        readonly property var g: b.global || ({})
        readonly property var ips: (b.ips || []).filter(x => x.bloqueada)
        onOpened: conf.pinBloqueos()
        Timer { interval: 3000; running: bloqueosDlg.visible; repeat: true; onTriggered: conf.pinBloqueos() }
        ColumnLayout {
            spacing: 10
            width: 420
            Text {
                Layout.fillWidth: true; wrapMode: Text.Wrap
                text: "Tras " + (bloqueosDlg.b.intentosPorIp || 5) + " PIN mal desde la misma conexión, esa conexión espera "
                      + (bloqueosDlg.b.bloqueoS || 60) + " s. Desbloquear no cambia el PIN."
                color: Theme.textDim; font.pixelSize: 12
            }
            Text {
                visible: bloqueosDlg.g.bloqueado === true
                Layout.fillWidth: true; wrapMode: Text.Wrap
                text: "Bloqueo general: nadie puede entrar por " + bloqueosDlg.g.quedanS + " s más (" + bloqueosDlg.g.fallos + " fallos entre todos)."
                color: Theme.live; font.pixelSize: 13
            }
            Text {
                visible: bloqueosDlg.ips.length === 0 && bloqueosDlg.g.bloqueado !== true
                text: "No hay nadie bloqueado."
                color: Theme.text; font.pixelSize: 13
            }
            Repeater {
                model: bloqueosDlg.ips
                delegate: RowLayout {
                    required property var modelData
                    Layout.fillWidth: true
                    Text { text: modelData.ip; color: Theme.text; font { pixelSize: 13; family: Theme.mono } Layout.fillWidth: true }
                    Text { text: modelData.quedanS + " s"; color: Theme.textDim; font.pixelSize: 12 }
                    GlassButton { text: "Desbloquear"; compact: true; onClicked: conf.desbloquearPin(modelData.ip) }
                }
            }
            GlassButton {
                text: "Desbloquear todos"
                active: true; compact: true
                enabled: bloqueosDlg.ips.length > 0 || bloqueosDlg.g.bloqueado === true
                onClicked: conf.desbloquearPin("")
            }
        }
    }

    Dialog {
        id: pinDlg
        anchors.centerIn: parent
        modal: true
        title: "PIN nuevo"
        standardButtons: Dialog.Ok | Dialog.Cancel
        onOpened: pinField.text = ""
        onAccepted: conf.newPin(pinField.text)
        ColumnLayout {
            spacing: 8
            Text { text: "Dejalo vacío para generar uno al azar.\nEl PIN anterior deja de servir para entrar."; color: Theme.textDim; font.pixelSize: 12 }
            TextField { id: pinField; Layout.preferredWidth: 260; placeholderText: "4 a 32 caracteres (opcional)"; maximumLength: 32 }
        }
    }
}
