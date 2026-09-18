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

    RowLayout {
        anchors { fill: parent; leftMargin: page.leftInset; margins: 20 }
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
                        width: grid.cellWidth
                        height: grid.cellHeight
                        Rectangle {
                            anchors { fill: parent; margins: 6 }
                            radius: 16
                            color: "#000000"
                            clip: true
                            border.color: hand ? Theme.amber : Theme.glassBorder
                            border.width: hand ? 2 : 1
                            // El video que B publica para ese slot (lo mismo que ve el motor).
                            VideoItem { anchors.fill: parent; source: "guest:" + slot; fillMode: VideoItem.Fit; opacity: connected ? 1 : 0.4 }
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
                                    Rectangle { width: 22; height: 22; radius: 7; color: Theme.accent
                                        Text { anchors.centerIn: parent; text: slot; color: "#140a04"; font { pixelSize: 12; weight: Font.Bold } } }
                                    Text { text: name; color: "white"; font.pixelSize: 14; font.weight: Font.DemiBold; elide: Text.ElideRight; Layout.fillWidth: true }
                                    Text { visible: hand; text: "✋"; font.pixelSize: 16 }
                                    StatusPill { text: mic ? "mic" : "mic off"; dot: mic ? Theme.ok : Theme.live }
                                    StatusPill { text: cam ? "cámara" : "cámara off"; dot: cam ? Theme.ok : Theme.live }
                                    StatusPill { visible: screen; text: "pantalla"; dot: Theme.accent }
                                    StatusPill { text: !connected ? "reconectando" : mediaUp ? "en vivo" : "sin medios"; dot: !connected ? Theme.live : mediaUp ? Theme.ok : Theme.amber }
                                }
                                Flow {
                                    Layout.fillWidth: true
                                    spacing: 6
                                    GlassButton { visible: mic; compact: true; text: "Silenciar"; onClicked: conf.request(uid, "mute-mic") }
                                    GlassButton { visible: cam; compact: true; text: "Apagar cámara"; onClicked: conf.request(uid, "mute-cam") }
                                    GlassButton { visible: screen; compact: true; text: "Cortar pantalla"; onClicked: conf.request(uid, "stop-screen") }
                                    GlassButton { visible: hand; compact: true; text: "Bajar mano"; onClicked: conf.request(uid, "lower-hand") }
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
                    RowLayout {
                        Layout.fillWidth: true
                        TextField { Layout.fillWidth: true; readOnly: true; text: conf.guestUrl; font.pixelSize: 11; selectByMouse: true }
                        GlassButton { text: "Copiar"; compact: true; onClicked: conf.copyToClipboard(conf.guestUrl) }
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
                        Toggle { checked: conf.lobby; onToggled: conf.setLobby(!conf.lobby) }
                        ColumnLayout {
                            spacing: 0
                            Layout.fillWidth: true
                            Text { text: "Lobby de admisión"; color: Theme.text; font.pixelSize: 13 }
                            Text { text: conf.lobby ? "Vos decidís quién entra" : "Entran directo mientras haya lugar"; color: Theme.textFaint; font.pixelSize: 11 }
                        }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        Toggle { checked: conf.locked; onToggled: conf.setLocked(!conf.locked) }
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
                            pulse: w.enabled && !w.up
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
