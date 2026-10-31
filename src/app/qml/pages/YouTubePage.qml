// Pagina YouTube: configurar el acceso a la API (con instrucciones y
// enlaces), conectar la cuenta (elegir cuenta en el navegador) y administrar
// videos, eventos en vivo, grabaciones y chat. Solo presentacion: todo sale de
// Studio.youtube (el servidor MCP usa las mismas capas).
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import BA
import "../controls"

Rectangle {
    id: page
    color: Theme.bg
    property real leftInset: 92
    readonly property var yt: Studio.youtube

    function mb(bytes) { return (bytes / 1048576).toFixed(bytes > 1073741824 ? 0 : 1) + " MB" }
    function day(iso) { return iso ? new Date(iso).toLocaleString(Qt.locale(), "dd/MM/yyyy HH:mm") : "" }
    readonly property var privacyNames: ({ "public": qsTr("Público"), "unlisted": qsTr("No listado"), "private": qsTr("Privado") })
    readonly property var statusNames: ({ "created": qsTr("Creado"), "ready": qsTr("Listo"), "testing": qsTr("En prueba"), "live": qsTr("EN VIVO"),
                                          "complete": qsTr("Terminado"), "revoked": qsTr("Revocado"), "liveStarting": qsTr("Arrancando"), "testStarting": qsTr("Arrancando prueba") })

    ColumnLayout {
        anchors { fill: parent; leftMargin: page.leftInset; margins: 20 }
        spacing: 16

        // --- Encabezado --------------------------------------------------------------------------
        RowLayout {
            Layout.fillWidth: true
            spacing: 12
            Rectangle { width: 40; height: 28; radius: 7; color: "#ff0033"
                Text { anchors.centerIn: parent; text: "▶"; color: "white"; font.pixelSize: 15 } }
            Text { text: qsTr("YouTube"); color: Theme.text; font { family: Theme.font; pixelSize: 24; weight: Font.DemiBold } }
            Item { Layout.fillWidth: true }
            BusyIndicator { running: yt.busy; visible: yt.busy; Layout.preferredWidth: 28; Layout.preferredHeight: 28 }
            RowLayout {
                visible: yt.loggedIn
                spacing: 10
                Rectangle {
                    width: 34; height: 34; radius: 17; clip: true; color: Theme.field
                    Image { anchors.fill: parent; source: yt.accountThumb; fillMode: Image.PreserveAspectCrop }
                }
                ColumnLayout {
                    spacing: 0
                    Text { text: yt.account.title || ""; color: Theme.text; font.pixelSize: 14; font.weight: Font.DemiBold }
                    Text { text: (yt.account.customUrl || "") + (yt.account.subscribers !== undefined ? "  ·  " + yt.account.subscribers + " suscriptores" : "")
                           color: Theme.textFaint; font.pixelSize: 11 }
                }
                GlassButton { text: qsTr("Actualizar"); compact: true; onClicked: yt.refresh() }
                GlassButton { text: qsTr("YouTube Studio"); compact: true; onClicked: yt.openUrl("https://studio.youtube.com/channel/" + (yt.account.id || "")) }
                GlassButton {
                    text: qsTr("Desconectar"); compact: true
                    onClicked: ApplicationWindow.window.confirm({
                        title: qsTr("¿Desconectar la cuenta de YouTube?"),
                        text: qsTr("Se revoca el acceso en Google y se borra el token guardado."),
                        accept: qsTr("Desconectar"), reject: qsTr("Cancelar"),
                        action: function() { yt.logout() }
                    })
                }
            }
        }

        // --- 1) Sin configurar: instrucciones ---------------------------------------------------
        ScrollView {
            visible: !yt.configured
            Layout.fillWidth: true
            Layout.fillHeight: true
            contentWidth: availableWidth
            clip: true
            ColumnLayout {
                width: parent.width
                spacing: 12
                Text {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    color: Theme.textDim; font.pixelSize: 14
                    text: qsTr("Para que la app (y el asistente por MCP) pueda crear transmisiones en vivo y administrar tus videos, Google pide que cada instalación tenga su propia credencial. Se hace una sola vez, en unos 5 minutos:")
                }
                Repeater {
                    model: Studio.cuentas.accountGuide("youtube").steps
                    delegate: Rectangle {
                        required property var modelData
                        Layout.fillWidth: true
                        implicitHeight: stepCol.implicitHeight + 24
                        radius: 14
                        color: Qt.rgba(1, 1, 1, 0.04)
                        border.color: Theme.glassBorder
                        RowLayout {
                            id: stepCol
                            anchors { fill: parent; margins: 12 }
                            spacing: 14
                            Rectangle { Layout.alignment: Qt.AlignTop; width: 30; height: 30; radius: 15; color: Theme.accent
                                Text { anchors.centerIn: parent; text: modelData.n; color: "#140a04"; font { pixelSize: 14; weight: Font.Black } } }
                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 6
                                Text { text: modelData.t; color: Theme.text; font { pixelSize: 15; weight: Font.DemiBold } }
                                Text { Layout.fillWidth: true; text: modelData.d; color: Theme.textDim; font.pixelSize: 13; wrapMode: Text.WordWrap }
                                Flow {
                                    Layout.fillWidth: true
                                    spacing: 8
                                    Repeater {
                                        model: modelData.links
                                        delegate: GlassButton {
                                            required property var modelData
                                            compact: true
                                            icon: "↗"
                                            text: modelData.l
                                            onClicked: yt.openUrl(modelData.u)
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
                // Paso 6: pegar la credencial
                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: pasteCol.implicitHeight + 28
                    radius: 14
                    color: Qt.rgba(1, 0.42, 0.17, 0.08)
                    border.color: Qt.rgba(1, 0.42, 0.17, 0.45)
                    ColumnLayout {
                        id: pasteCol
                        anchors { fill: parent; margins: 14 }
                        spacing: 10
                        Text { text: qsTr("6 · Pegá acá el JSON que descargaste"); color: Theme.text; font { pixelSize: 15; weight: Font.DemiBold } }
                        Text { Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.textDim; font.pixelSize: 12
                               text: qsTr("Abrí el archivo client_secret_….json con un editor de texto y copiá todo su contenido. Se guarda cifrado en Credenciales/, dentro de la carpeta portable (una actualización no lo toca).") }
                        ScrollView {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 110
                            TextArea {
                                id: clientJson
                                placeholderText: qsTr("{\"installed\":{\"client_id\":\"….apps.googleusercontent.com\", \"client_secret\":\"…\", …}}")
                                wrapMode: TextEdit.WrapAnywhere
                                font.family: Theme.mono; font.pixelSize: 12
                                color: Theme.text
                                background: Rectangle { radius: 10; color: Theme.field; border.color: Theme.glassBorder }
                            }
                        }
                        Text { text: qsTr("…o cargá el ID y el secreto a mano:"); color: Theme.textFaint; font.pixelSize: 12 }
                        RowLayout {
                            Layout.fillWidth: true
                            TextField { id: clientId; Layout.fillWidth: true; placeholderText: qsTr("ID de cliente (….apps.googleusercontent.com)") }
                            TextField { id: clientSecret; Layout.preferredWidth: 260; placeholderText: qsTr("Secreto del cliente"); echoMode: TextInput.Password }
                        }
                        RowLayout {
                            GlassButton {
                                text: qsTr("Guardar credencial"); active: true
                                onClicked: {
                                    const e = clientSecret.text !== "" ? yt.saveClient(clientId.text, clientSecret.text)
                                                                       : yt.saveClient(clientJson.text, "")
                                    saveError.text = e
                                    if (e === "") { clientJson.text = ""; clientSecret.text = "" }
                                }
                            }
                            GlassButton {
                                text: qsTr("Importar el JSON de Descargas")
                                onClicked: saveError.text = Studio.cuentas.saveAccountCredential("youtube", { "fromDownloads": true })
                                ToolTip.visible: hovered; ToolTip.text: qsTr("Busca el client_secret_….json más nuevo que bajaste de Google (paso 5)")
                            }
                            Text { id: saveError; color: Theme.live; font.pixelSize: 12; Layout.fillWidth: true; wrapMode: Text.WordWrap }
                        }
                    }
                }
                Text {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    color: Theme.textFaint; font.pixelSize: 12
                    text: qsTr("Cuota: Google da 10.000 unidades por día y proyecto. Crear y administrar eventos consume poco; subir un video cuesta alrededor de 1.600 unidades, y leer el chat en vivo unas 5 por lectura (la app lee cada 6 segundos o más).")
                }
            }
        }

        // --- 2) Configurado, sin cuenta ------------------------------------------------------------------
        ColumnLayout {
            visible: yt.configured && !yt.loggedIn
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 14
            Item { Layout.fillHeight: true }
            Text {
                Layout.alignment: Qt.AlignHCenter
                text: yt.loginPending ? qsTr("Esperando que autorices en el navegador…") : qsTr("Conectá tu cuenta de YouTube")
                color: Theme.text; font { pixelSize: 22; weight: Font.DemiBold }
            }
            Text {
                Layout.alignment: Qt.AlignHCenter
                Layout.maximumWidth: 640
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
                color: Theme.textDim; font.pixelSize: 14
                text: yt.loginPending ? qsTr("Se abrió Google en tu navegador: elegí la cuenta (el canal) que querés usar y aceptá el permiso para administrar YouTube. Cuando termines, volvé acá.")
                                      : qsTr("Se abre el navegador para que elijas la cuenta. La app guarda solo un token de acceso (no tu contraseña), legible solo por tu usuario; lo podés revocar cuando quieras.")
            }
            BusyIndicator { Layout.alignment: Qt.AlignHCenter; visible: yt.loginPending; running: yt.loginPending }
            RowLayout {
                Layout.alignment: Qt.AlignHCenter
                spacing: 10
                GlassButton { visible: !yt.loginPending; text: qsTr("Conectar con Google"); icon: "▶"; active: true; activeColor: "#cc0026"; onClicked: yt.login() }
                GlassButton { visible: yt.loginPending; text: qsTr("Cancelar"); onClicked: yt.cancelLogin() }
                GlassButton {
                    visible: !yt.loginPending; text: qsTr("Cambiar credencial")
                    onClicked: ApplicationWindow.window.confirm({
                        title: qsTr("¿Cambiar la credencial de Google?"),
                        text: qsTr("Se borra el cliente OAuth guardado y vuelven las instrucciones."),
                        accept: qsTr("Cambiar"), reject: qsTr("Cancelar"),
                        action: function() { yt.clearClient() }
                    })
                }
            }
            Text { Layout.alignment: Qt.AlignHCenter; color: Theme.textFaint; font.pixelSize: 11; text: qsTr("Cliente: %1").arg(yt.clientId) }
            Text { Layout.alignment: Qt.AlignHCenter; Layout.maximumWidth: 640; wrapMode: Text.WordWrap; horizontalAlignment: Text.AlignHCenter
                   visible: yt.lastError !== ""; color: Theme.live; font.pixelSize: 13; text: yt.lastError }
            Item { Layout.fillHeight: true }
        }

        // --- 3) Conectado: administrar ----------------------------------------------------------------------
        ColumnLayout {
            visible: yt.loggedIn
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 12
            TabBar {
                id: tabs
                Layout.fillWidth: true
                background: Rectangle { color: "transparent" }
                Repeater {
                    model: [qsTr("Videos"), qsTr("En vivo"), qsTr("Grabaciones"), qsTr("Chat")]
                    TabButton {
                        required property string modelData
                        text: modelData
                        width: implicitWidth + 30
                    }
                }
            }
            // Subida en curso
            Rectangle {
                visible: yt.upload.path !== undefined
                Layout.fillWidth: true
                implicitHeight: 48
                radius: 12
                color: Qt.rgba(1, 1, 1, 0.05)
                RowLayout {
                    anchors { fill: parent; margins: 10 }
                    Text { text: qsTr("Subiendo «%1»").arg(yt.upload.title || ""); color: Theme.text; font.pixelSize: 13 }
                    ProgressBar { Layout.fillWidth: true; from: 0; to: Math.max(1, yt.upload.total || 1); value: yt.upload.sent || 0 }
                    Text { color: Theme.textDim; font.pixelSize: 12
                           text: yt.upload.total ? Math.round(100 * yt.upload.sent / yt.upload.total) + " %" : "" }
                }
            }
            StackLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                currentIndex: tabs.currentIndex

                // Videos
                ListView {
                    id: videoList
                    clip: true
                    spacing: 8
                    model: yt.videos
                    Text { anchors.centerIn: parent; visible: videoList.count === 0; text: qsTr("No hay videos subidos (o todavía se están cargando)."); color: Theme.textFaint }
                    delegate: Rectangle {
                        required property string uid
                        required property string title
                        required property string description
                        required property string privacy
                        required property string publishedAt
                        required property string thumb
                        required property var views
                        required property var likes
                        required property var comments
                        required property string url
                        required property string live
                        width: ListView.view.width
                        height: 96
                        radius: 12
                        color: Qt.rgba(1, 1, 1, 0.04)
                        RowLayout {
                            anchors { fill: parent; margins: 8 }
                            spacing: 12
                            Rectangle { Layout.preferredWidth: 142; Layout.preferredHeight: 80; radius: 8; color: "black"; clip: true
                                Image { anchors.fill: parent; source: thumb; fillMode: Image.PreserveAspectCrop } }
                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 4
                                Text { text: title; color: Theme.text; font { pixelSize: 14; weight: Font.DemiBold } elide: Text.ElideRight; Layout.fillWidth: true }
                                RowLayout {
                                    spacing: 6
                                    StatusPill { text: page.privacyNames[privacy] || privacy; dot: privacy === "public" ? Theme.ok : privacy === "unlisted" ? Theme.amber : Theme.textFaint }
                                    StatusPill { visible: live === "live" || live === "upcoming"; text: live === "live" ? qsTr("en vivo") : qsTr("programado"); dot: Theme.live }
                                    Text { text: page.day(publishedAt) + "  ·  " + qsTr("%n vista", "", views) + "  ·  " + qsTr("%n me gusta", "", likes) + "  ·  " + qsTr("%n comentario", "", comments)
                                           color: Theme.textFaint; font.pixelSize: 11 }
                                }
                                Text { text: description; color: Theme.textDim; font.pixelSize: 11; elide: Text.ElideRight; Layout.fillWidth: true; maximumLineCount: 1 }
                            }
                            GlassButton { compact: true; text: qsTr("Editar"); onClicked: videoDlg.edit(uid, title, description, privacy) }
                            GlassButton { compact: true; text: qsTr("Ver"); onClicked: yt.openUrl(url) }
                            GlassButton {
                                compact: true; text: qsTr("Borrar")
                                onClicked: {
                                    // Si la lista cambia con el dialogo abierto (termina una subida, el asistente crea
                                    // o borra algo) este delegate se destruye, y desde ahi la accion ya no resuelve sus
                                    // roles ni 'yt' (nombre de la pagina: se busca por el contexto del delegate, que ya
                                    // no existe). Por eso va todo copiado a constantes antes de preguntar.
                                    const api = yt, id = uid, nombre = title
                                    ApplicationWindow.window.confirm({
                                        // El titulo de un video llega a 100 caracteres: va en el texto, que corta en lineas.
                                        title: qsTr("¿Borrar este video de YouTube?"),
                                        text: qsTr("«%1» se borra del canal y no se puede deshacer.").arg(nombre),
                                        accept: qsTr("Borrar"), reject: qsTr("Cancelar"),
                                        action: function() { api.deleteVideo(id) }
                                    })
                                }
                            }
                        }
                    }
                }

                // En vivo
                ColumnLayout {
                    spacing: 10
                    RowLayout {
                        Layout.fillWidth: true
                        Text { Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.textDim; font.pixelSize: 12
                               text: qsTr("«Transmitir» toma el servidor y la clave del evento (solo en memoria) y sale al aire desde la app; YouTube lo pasa a EN VIVO cuando recibe la señal. «Terminar» cierra el evento y corta la transmisión.") }
                        GlassButton { text: qsTr("Nuevo evento"); icon: "+"; active: true; onClicked: newDlg.open() }
                    }
                    ListView {
                        id: liveList
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        spacing: 8
                        model: yt.broadcasts
                        Text { anchors.centerIn: parent; visible: liveList.count === 0; text: qsTr("No hay eventos en vivo. Creá uno con «Nuevo evento»."); color: Theme.textFaint }
                        delegate: Rectangle {
                            required property string uid
                            required property string title
                            required property string status
                            required property string privacy
                            required property string scheduledStart
                            required property string url
                            required property string studioUrl
                            required property string thumb
                            readonly property bool fromHere: yt.liveBroadcastId === uid
                            width: ListView.view.width
                            height: 84
                            radius: 12
                            color: status === "live" ? Qt.rgba(1, 0.2, 0.2, 0.10) : Qt.rgba(1, 1, 1, 0.04)
                            border.color: fromHere ? Theme.live : "transparent"
                            RowLayout {
                                anchors { fill: parent; margins: 8 }
                                spacing: 12
                                Rectangle { Layout.preferredWidth: 120; Layout.preferredHeight: 68; radius: 8; color: "black"; clip: true
                                    Image { anchors.fill: parent; source: thumb; fillMode: Image.PreserveAspectCrop } }
                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 4
                                    Text { text: title; color: Theme.text; font { pixelSize: 14; weight: Font.DemiBold } elide: Text.ElideRight; Layout.fillWidth: true }
                                    RowLayout {
                                        spacing: 6
                                        StatusPill { text: page.statusNames[status] || status; dot: status === "live" ? Theme.live : status === "complete" ? Theme.textFaint : Theme.ok; pulse: status === "live" }
                                        StatusPill { text: page.privacyNames[privacy] || privacy; dot: Theme.textFaint }
                                        StatusPill { visible: fromHere; text: qsTr("transmitiendo desde la app"); dot: Theme.live; pulse: true }
                                        StatusPill { visible: yt.chatBroadcastId === uid; text: qsTr("chat en pantalla"); dot: Theme.accent }
                                        Text { text: page.day(scheduledStart); color: Theme.textFaint; font.pixelSize: 11 }
                                    }
                                }
                                GlassButton { compact: true; visible: status !== "complete" && !fromHere; text: qsTr("Transmitir"); active: true; activeColor: "#cc0026"; onClicked: yt.goLive(uid) }
                                GlassButton {
                                    compact: true; visible: status === "live" || status === "testing" || fromHere; text: qsTr("Terminar")
                                    onClicked: {
                                        // Copias locales, como en «Borrar» de Videos. Sin 'reject', queda «No»: «Cancelar»
                                        // se leeria como cancelar el evento.
                                        const api = yt, id = uid, nombre = title
                                        ApplicationWindow.window.confirm({
                                            title: qsTr("¿Terminar este evento?"),
                                            text: (fromHere ? qsTr("«%1» se cierra en YouTube y se corta la transmisión que sale de la app. No se puede reanudar.")
                                                            : qsTr("«%1» se cierra en YouTube. No se puede reanudar.")).arg(nombre),
                                            accept: qsTr("Terminar"),
                                            action: function() { api.endBroadcast(id) }
                                        })
                                    }
                                }
                                GlassButton { compact: true; visible: status !== "complete"; text: yt.chatBroadcastId === uid ? qsTr("Sin chat") : qsTr("Chat")
                                              onClicked: yt.setChatBroadcast(yt.chatBroadcastId === uid ? "" : uid) }
                                GlassButton { compact: true; text: qsTr("Studio"); onClicked: yt.openUrl(studioUrl) }
                                GlassButton { compact: true; text: qsTr("Copiar enlace"); onClicked: yt.copy(url) }
                                GlassButton {
                                    compact: true; visible: status !== "live"; text: qsTr("Borrar")
                                    onClicked: {
                                        const api = yt, id = uid, nombre = title // copias locales, como en «Borrar» de Videos
                                        ApplicationWindow.window.confirm({
                                            title: qsTr("¿Borrar este evento?"),
                                            text: qsTr("«%1» se borra de YouTube y no se puede deshacer.").arg(nombre),
                                            accept: qsTr("Borrar"), reject: qsTr("Cancelar"),
                                            action: function() { api.deleteBroadcast(id) }
                                        })
                                    }
                                }
                            }
                        }
                    }
                }

                // Grabaciones
                ColumnLayout {
                    spacing: 10
                    RowLayout {
                        Layout.fillWidth: true
                        Text { Layout.fillWidth: true; color: Theme.textDim; font.pixelSize: 12; text: qsTr("Grabaciones de la app (carpeta de grabación). Subilas a tu canal: quedan privadas salvo que elijas otra cosa.") }
                        GlassButton { text: qsTr("Actualizar"); compact: true; onClicked: yt.refreshRecordings() }
                    }
                    ListView {
                        id: recList
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        spacing: 6
                        model: yt.recordings
                        Text { anchors.centerIn: parent; visible: recList.count === 0; text: qsTr("Todavía no hay grabaciones."); color: Theme.textFaint }
                        delegate: Rectangle {
                            required property string name
                            required property var size
                            required property string modified
                            required property string path
                            width: ListView.view.width
                            height: 50
                            radius: 10
                            color: Qt.rgba(1, 1, 1, 0.04)
                            RowLayout {
                                anchors { fill: parent; margins: 10 }
                                Text { text: name; color: Theme.text; font.pixelSize: 13; elide: Text.ElideMiddle; Layout.fillWidth: true }
                                Text { text: page.mb(size) + "  ·  " + modified; color: Theme.textFaint; font.pixelSize: 12 }
                                GlassButton { compact: true; text: qsTr("Subir a YouTube"); enabled: yt.upload.path === undefined
                                              onClicked: uploadDlg.pick(path, name) }
                            }
                        }
                    }
                }

                // Chat
                ColumnLayout {
                    spacing: 8
                    Text {
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        color: Theme.textDim; font.pixelSize: 12
                        text: yt.chatBroadcastId === "" ? qsTr("Elegí de qué evento leer el chat (pestaña «En vivo» → «Chat»). Para mostrarlo sobre el video, agregá la fuente «Chat en vivo» a una escena.")
                                                         : qsTr("Chat del evento elegido. Para mostrarlo en el video: fuente «Chat en vivo» en la escena.")
                    }
                    ListView {
                        id: chatList
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        spacing: 4
                        model: yt.chat
                        onCountChanged: positionViewAtEnd()
                        delegate: Item {
                            id: msg
                            required property string author
                            required property string text
                            required property bool owner
                            required property bool moderator
                            required property var model
                            width: ListView.view.width
                            height: Math.max(msgText.implicitHeight, 26)
                            Text {
                                id: msgText
                                width: parent.width - 34
                                wrapMode: Text.WordWrap
                                textFormat: Text.PlainText
                                color: Theme.text
                                font.pixelSize: 13
                                text: (msg.owner ? "★ " : msg.moderator ? "◆ " : "") + msg.author + ": " + msg.text
                            }
                            // Destacar para el aire: los overlays de chat muestran solo lo destacado.
                            GlassButton {
                                anchors { right: parent.right; top: parent.top }
                                compact: true
                                property bool dest: false
                                text: dest ? "★" : "☆"
                                active: dest
                                onClicked: {
                                    const k = "yt:" + msg.model.id
                                    if (dest) { Studio.quitarDestacadoChatUi(k); dest = false }
                                    else dest = Studio.destacarChatUi(k)
                                }
                                ToolTip.visible: hovered; ToolTip.delay: 400; ToolTip.text: qsTr("Destacar para el chat en pantalla")
                            }
                            // A la cola de preguntas (una sola cola para la sala y las plataformas).
                            GlassButton {
                                anchors { right: parent.right; top: parent.top; rightMargin: 40 }
                                compact: true
                                text: "?"
                                onClicked: { const e = Studio.preguntaDesdeChatUi("yt:" + msg.model.id); if (e !== "") Studio.notify(e, "error") }
                                ToolTip.visible: hovered; ToolTip.delay: 400; ToolTip.text: qsTr("Sumar a la cola de preguntas")
                            }
                        }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        enabled: yt.chatBroadcastId !== ""
                        TextField { id: chatInput; Layout.fillWidth: true; placeholderText: qsTr("Escribir en el chat del vivo…")
                                    onAccepted: { yt.sendChat(text); text = "" } }
                        GlassButton { text: qsTr("Enviar"); onClicked: { yt.sendChat(chatInput.text); chatInput.text = "" } }
                    }
                }
            }
        }
    }

    // --- Dialogos -----------------------------------------------------------------------------------
    // Las confirmaciones (borrar, terminar, desconectar) van por el confirm() de la ventana.
    Dialog {
        id: videoDlg
        property string vid: ""
        function edit(id, t, d, p) { vid = id; vTitle.text = t; vDesc.text = d; vPrivacy.currentIndex = ["public", "unlisted", "private"].indexOf(p); open() }
        anchors.centerIn: parent
        modal: true
        title: qsTr("Editar video")
        standardButtons: Dialog.Save | Dialog.Cancel
        onAccepted: yt.updateVideo(vid, vTitle.text, vDesc.text, ["public", "unlisted", "private"][vPrivacy.currentIndex])
        ColumnLayout {
            spacing: 8
            Label { text: qsTr("Título") }
            TextField { id: vTitle; Layout.preferredWidth: 520; maximumLength: 100 }
            Label { text: qsTr("Descripción") }
            ScrollView { Layout.preferredWidth: 520; Layout.preferredHeight: 140
                TextArea { id: vDesc; wrapMode: TextEdit.Wrap } }
            Label { text: qsTr("Visibilidad") }
            ComboBox { id: vPrivacy; model: [qsTr("Público"), qsTr("No listado"), qsTr("Privado")]; Layout.preferredWidth: 200 }
        }
    }
    Dialog {
        id: newDlg
        anchors.centerIn: parent
        modal: true
        title: qsTr("Nuevo evento en vivo")
        standardButtons: Dialog.Ok | Dialog.Cancel
        onAccepted: yt.createBroadcast(nTitle.text || qsTr("Transmisión en vivo"), nDesc.text,
                                       ["private", "unlisted", "public"][nPrivacy.currentIndex], ["normal", "low", "ultraLow"][nLatency.currentIndex])
        ColumnLayout {
            spacing: 8
            Label { text: qsTr("Título") }
            TextField { id: nTitle; Layout.preferredWidth: 480; placeholderText: qsTr("Transmisión en vivo") }
            Label { text: qsTr("Descripción") }
            TextField { id: nDesc; Layout.preferredWidth: 480 }
            RowLayout {
                ColumnLayout { Label { text: qsTr("Visibilidad") }
                    ComboBox { id: nPrivacy; model: [qsTr("Privado"), qsTr("No listado"), qsTr("Público")]; Layout.preferredWidth: 180 } }
                ColumnLayout { Label { text: qsTr("Latencia") }
                    ComboBox { id: nLatency; model: [qsTr("Normal"), qsTr("Baja"), qsTr("Ultra baja")]; Layout.preferredWidth: 180 } }
            }
        }
    }
    Dialog {
        id: uploadDlg
        property string path: ""
        function pick(p, name) { path = p; uTitle.text = name.replace(/\.[^.]+$/, ""); uDesc.text = ""; uPrivacy.currentIndex = 0; open() }
        anchors.centerIn: parent
        modal: true
        title: qsTr("Subir a YouTube")
        standardButtons: Dialog.Ok | Dialog.Cancel
        onAccepted: yt.uploadRecording(path, uTitle.text, uDesc.text, ["private", "unlisted", "public"][uPrivacy.currentIndex])
        ColumnLayout {
            spacing: 8
            Label { text: qsTr("Título") }
            TextField { id: uTitle; Layout.preferredWidth: 480; maximumLength: 100 }
            Label { text: qsTr("Descripción") }
            TextField { id: uDesc; Layout.preferredWidth: 480 }
            Label { text: qsTr("Visibilidad") }
            ComboBox { id: uPrivacy; model: [qsTr("Privado"), qsTr("No listado"), qsTr("Público")]; Layout.preferredWidth: 180 }
        }
    }
}
