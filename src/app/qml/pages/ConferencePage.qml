// Sala de videoconferencia: lobby | participantes (con su video) + chat |
// acceso + archivos compartidos. Solo presentacion: todo sale de
// Studio.conference, que a su vez es un cliente de la API de control de B
// (la misma que usa la consola web).
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

    // Reloj para los "hace N min" (sin tocar el modelo).
    property double now: Date.now()
    Timer { interval: 5000; running: page.visible; repeat: true; onTriggered: page.now = Date.now() }
    function since(ms) {
        const s = Math.max(0, Math.round((page.now - ms) / 1000))
        return s < 60 ? s + " s" : Math.floor(s / 60) + " min"
    }
    function mb(bytes) { return (bytes / 1048576).toFixed(1) + " MB" }
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
                                MenuItem { text: "QR remoto (Cloudflare)"; onTriggered: qrDlg.show("remote") }
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
                                    Layout.preferredHeight: width * 9 / 16
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
                                RowLayout {
                                    spacing: 6
                                    GlassButton { text: "Admitir"; compact: true; active: true; activeColor: Theme.ok; onClicked: conf.admit(uid) }
                                    GlassButton { text: talk ? "Cortar" : "Hablar"; compact: true; enabled: lobbySlot > 0; active: talk; activeColor: Theme.ok
                                                  onClicked: conf.talk(uid, !talk) }
                                    GlassButton { text: "Rechazar"; compact: true; onClicked: conf.reject(uid) }
                                    Item { Layout.fillWidth: true }
                                    GlassButton { text: "✉"; compact: true; onClicked: { chatTo.pick(uid); chatInput.forceActiveFocus() } }
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
                    title: "En la sala"
                    RowLayout {
                        anchors { right: parent.right; top: parent.top; topMargin: -34 }
                        spacing: 8
                        StatusPill { text: conf.online ? "Sala " + conf.room : "Sala sin conexion"; dot: conf.online ? Theme.ok : Theme.live; pulse: !conf.online }
                        StatusPill { visible: conf.online; text: (conf.maxGuests - conf.slotsFree) + "/" + conf.maxGuests + " lugares"; dot: conf.slotsFree > 0 ? Theme.ok : Theme.amber }
                    }
                    GridView {
                        id: grid
                        anchors.fill: parent
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
                            width: grid.cellWidth
                            height: grid.cellHeight
                            Rectangle {
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
                                    anchors { left: parent.left; right: parent.right; bottom: parent.bottom; margins: 10 }
                                    spacing: 6
                                    RowLayout {
                                        spacing: 8
                                        Rectangle { width: 22; height: 22; radius: 7; color: slot > 0 ? Theme.accent : Qt.rgba(1, 1, 1, 0.18)
                                            Text { anchors.centerIn: parent; text: slot > 0 ? slot : "–"; color: slot > 0 ? "#140a04" : "white"; font { pixelSize: 12; weight: Font.Bold } } }
                                        Text { text: name; color: "white"; font.pixelSize: 14; font.weight: Font.DemiBold; elide: Text.ElideRight; Layout.fillWidth: true }
                                        Text { visible: hand; text: "✋"; font.pixelSize: 16 }
                                        StatusPill { text: mic ? "mic" : "mic off"; dot: mic ? Theme.ok : Theme.live }
                                        StatusPill { text: cam ? "cámara" : "cámara off"; dot: cam ? Theme.ok : Theme.live }
                                        StatusPill { visible: screen; text: "pantalla"; dot: Theme.accent }
                                        StatusPill { visible: presenter; text: "PRESENTADOR"; dot: "#a78bfa" }
                                        StatusPill { visible: speaker === "onair"; text: "ORADOR"; dot: Theme.live }
                                        StatusPill { text: !connected ? "reconectando" : mediaUp ? "en vivo" : "sin medios"; dot: !connected ? Theme.live : mediaUp ? Theme.ok : Theme.amber }
                                    }
                                    Flow {
                                        Layout.fillWidth: true
                                        spacing: 6
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
                                        GlassButton { compact: true; text: "✉ Mensaje"; onClicked: { chatTo.pick(uid); chatInput.forceActiveFocus() } }
                                        GlassButton { compact: true; text: "Sacar"; onClicked: { kickDlg.who = uid; kickDlg.whoName = name; kickDlg.open() } }
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
                    title: "Chat"
                    ColumnLayout {
                        anchors.fill: parent
                        spacing: 8
                        ListView {
                            id: chatList
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            clip: true
                            spacing: 6
                            model: conf.chat
                            onCountChanged: Qt.callLater(positionViewAtEnd)
                            delegate: Item {
                                id: msg
                                required property string fromName
                                required property string to
                                required property string text
                                required property bool mine
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
                                        Text { text: (msg.mine ? "Vos" : msg.fromName) + (msg.to === "all" ? "" : msg.to === "host" ? " → operador" : " → " + conf.nameOf(msg.to))
                                               color: Theme.textFaint; font.pixelSize: 10 }
                                        Text { text: msg.text; color: Theme.text; font.pixelSize: 13; wrapMode: Text.Wrap
                                               width: Math.min(implicitWidth, chatList.width * 0.8 - 22) }
                                    }
                                }
                            }
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            ComboBox {
                                id: chatTo
                                Layout.preferredWidth: 170
                                textRole: "label"
                                valueRole: "value"
                                model: conf.chatTargets
                                function pick(uid) {
                                    const i = indexOfValue(uid)
                                    currentIndex = i >= 0 ? i : 0
                                }
                            }
                            TextField {
                                id: chatInput
                                Layout.fillWidth: true
                                placeholderText: "Escribí un mensaje…"
                                onAccepted: send()
                                function send() {
                                    if (text.trim() === "") return
                                    conf.sendChat(chatTo.currentValue, text)
                                    text = ""
                                }
                            }
                            GlassButton { text: "Enviar"; compact: true; onClicked: chatInput.send() }
                        }
                    }
                }
            }

            // --- Acceso + archivos ---------------------------------------------------
            // fillWidth explicito en false: un ColumnLayout hereda fillWidth de sus
            // hijos y, sin esto, le disputa el ancho a la columna central.
            ColumnLayout {
                Layout.preferredWidth: 360
                Layout.maximumWidth: 360
                Layout.fillWidth: false
                Layout.fillHeight: true
                spacing: 16

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
                                text: "Invitar con QR"; icon: "⬚"; active: true
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
                        RowLayout {
                            Layout.fillWidth: true
                            Text { text: "Te oyen con"; color: Theme.textDim; font.pixelSize: 12; Layout.preferredWidth: 78 }
                            ComboBox {
                                Layout.fillWidth: true
                                model: ["(sin voz)"].concat(conf.audioSourceNames)
                                currentIndex: Math.max(0, model.indexOf(conf.operatorAudio))
                                onActivated: (i) => conf.setOperatorFeed(conf.operatorVideo, i === 0 ? "" : model[i])
                                onPressedChanged: if (pressed) conf.refreshSources()
                            }
                        }
                        GlassButton { text: "Abrir consola web"; icon: "↗"; compact: true; enabled: conf.online; onClicked: conf.openWebConsole() }
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
                            GlassButton { text: "Copiar"; compact: true; onClicked: conf.copyToClipboard(bestUrlField.text) }
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
            }
        }
    }

    // Datos del orador (los carga el invitado al entrar; aca se corrigen).
    Dialog {
        id: profDlg
        anchors.centerIn: parent
        modal: true
        title: "Datos del orador"
        standardButtons: Dialog.Save | Dialog.Cancel
        property string who: ""
        function edit(id, name, info, contact, sec) {
            who = id; profName.text = name; profInfo.text = info; profContact.text = contact
            profTime.text = page.fmt(sec * 1000)
            open()
        }
        onAccepted: {
            conf.setProfile(who, profName.text, profInfo.text, profContact.text)
            const s = conf.parseDuration(profTime.text)
            if (s > 0) conf.speakerTime(who, s)
        }
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
            Text { text: "Tiempo"; color: Theme.textDim }
            TextField { id: profTime; Layout.preferredWidth: 90; placeholderText: "m:ss" }
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
    Dialog {
        id: qrDlg
        anchors.centerIn: parent
        modal: true
        standardButtons: Dialog.Close
        property string target: "local"
        property string url: ""
        property var modules: []
        property string message: ""
        property bool loading: false
        readonly property bool board: target.startsWith("board-")
        title: board ? "Tablero de oradores (solo lectura) — " + (target === "board-remote" ? "por internet" : "red local")
                     : target === "remote" ? "QR remoto — por internet (Cloudflare)" : "QR local — misma red"
        function show(t) {
            target = t; url = ""; modules = []; message = ""; loading = true
            open()
            if (board) conf.requestBoardLink(t.substring(6))
            else conf.requestQr(t, qrPin.checked)
        }
        Connections {
            target: conf
            function onQrReady(t, url, modules, message) {
                if (t !== qrDlg.target) return
                qrDlg.loading = false
                qrDlg.url = url
                qrDlg.modules = modules
                qrDlg.message = message
                qrCanvas.requestPaint()
            }
        }
        ColumnLayout {
            spacing: 12
            Rectangle {
                Layout.alignment: Qt.AlignHCenter
                Layout.preferredWidth: 300
                Layout.preferredHeight: 300
                radius: 12
                color: qrDlg.url !== "" ? "white" : Qt.rgba(1, 1, 1, 0.05)
                Canvas {
                    id: qrCanvas
                    anchors.fill: parent
                    anchors.margins: 12
                    visible: qrDlg.url !== ""
                    onPaint: {
                        const ctx = getContext("2d")
                        ctx.reset()
                        const rows = qrDlg.modules
                        if (!rows || rows.length === 0) return
                        const n = rows.length + 8 // zona silenciosa de 4 modulos
                        const cell = Math.floor(Math.min(width, height) / n)
                        const off = Math.floor((Math.min(width, height) - cell * n) / 2) + 4 * cell
                        ctx.fillStyle = "#000000"
                        for (let y = 0; y < rows.length; ++y)
                            for (let x = 0; x < rows[y].length; ++x)
                                if (rows[y][x] === "1") ctx.fillRect(off + x * cell, off + y * cell, cell, cell)
                    }
                }
                Text {
                    anchors.fill: parent
                    anchors.margins: 20
                    visible: qrDlg.url === ""
                    wrapMode: Text.Wrap
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                    color: Theme.textDim; font.pixelSize: 13
                    text: qrDlg.loading ? "Generando…" : qrDlg.message
                }
            }
            // Donde sirve el QR: se puede cambiar sin cerrar.
            RowLayout {
                Layout.alignment: Qt.AlignHCenter
                visible: !qrDlg.board
                spacing: 6
                GlassButton {
                    compact: true; text: "Red local"
                    active: qrDlg.target === "local"
                    onClicked: qrDlg.show("local")
                }
                GlassButton {
                    compact: true; text: "Por internet"
                    active: qrDlg.target === "remote"
                    onClicked: qrDlg.show("remote")
                }
            }
            CheckBox {
                id: qrPin
                visible: !qrDlg.board
                text: "Incluir el PIN en el QR (entran sin tipearlo)"
                onToggled: qrDlg.show(qrDlg.target)
            }
            RowLayout {
                visible: qrDlg.url !== ""
                Layout.preferredWidth: 300
                TextField { id: qrUrl; Layout.fillWidth: true; readOnly: true; text: qrDlg.url; font.pixelSize: 11 }
                GlassButton { text: "Copiar"; compact: true; onClicked: conf.copyToClipboard(qrDlg.url) }
            }
            Text {
                Layout.preferredWidth: 300
                wrapMode: Text.Wrap
                color: Theme.textFaint; font.pixelSize: 11
                visible: qrDlg.url !== ""
                text: qrDlg.board ? "Abrilo en la tablet: muestra la fila, las caras y los tiempos. La clave cambia en cada arranque." :
                      (qrDlg.message !== "" ? qrDlg.message + " " : "") +
                      (qrDlg.target === "remote" ? "Sirve desde cualquier lugar." : "Sirve para dispositivos en la misma red (wifi) que este equipo.") +
                      (qrPin.checked ? " Incluye el PIN: quien tenga este QR puede pedir entrar (igual pasa por el lobby)." : " El PIN no va en el QR: pasalo aparte.")
            }
        }
    }

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
                TextArea { readOnly: true; text: wgDlg.config; font.family: "monospace"; font.pixelSize: 11; wrapMode: TextEdit.NoWrap; selectByMouse: true }
            }
            GlassButton { visible: wgDlg.config !== ""; text: "Copiar configuracion"; compact: true; onClicked: conf.copyToClipboard(wgDlg.config) }
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
