// Pagina Facebook: crear la app de Meta (instrucciones con enlaces), conectar
// la cuenta, elegir la Pagina, transmitir (prueba o en vivo), administrar
// vivos y videos, subir grabaciones y comentarios. Todo sale de Studio.facebook.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import BA
import "../controls"

Rectangle {
    id: page
    color: Theme.bg
    property real leftInset: 92
    readonly property var fb: Studio.facebook
    readonly property color blue: "#1877f2"
    function day(iso) { return iso ? new Date(iso).toLocaleString(Qt.locale(), "dd/MM/yyyy HH:mm") : "" }
    function mb(b) { return (b / 1048576).toFixed(1) + " MB" }
    readonly property var statusNames: ({ "LIVE": "EN VIVO", "UNPUBLISHED": "Prueba (sin publicar)", "LIVE_STOPPED": "Detenido", "VOD": "Terminado",
                                          "SCHEDULED_UNPUBLISHED": "Programado", "PROCESSING": "Procesando" })
    // Confirmaciones. Reciben copias (id, nombre): el delegate que las pide puede destruirse con el dialogo abierto.
    function askEnd(id, name) {
        ApplicationWindow.window.confirm({
            title: name ? "¿Terminar «" + name + "»?" : "¿Terminar el vivo de Facebook?",
            text: (id === fb.liveId ? "La app deja de transmitir y el vivo se cierra en Facebook." : "El vivo se cierra en Facebook.")
                  + " Es definitivo: no se puede reanudar.",
            accept: "Terminar",
            action: () => fb.endLive(id)
        })
    }
    function askDelete(id, name, what) {
        ApplicationWindow.window.confirm({
            title: "¿Borrar " + (name ? "«" + name + "»" : "este " + what) + " de Facebook?",
            text: "El " + what + " se elimina de la Página. No se puede deshacer.",
            accept: "Borrar",
            reject: "Cancelar",
            action: () => fb.deleteItem(id)
        })
    }

    ColumnLayout {
        anchors { fill: parent; leftMargin: page.leftInset; margins: 20 }
        spacing: 16

        RowLayout {
            Layout.fillWidth: true
            spacing: 12
            Rectangle { width: 34; height: 34; radius: 17; color: page.blue
                Text { anchors.centerIn: parent; text: "f"; color: "white"; font { pixelSize: 22; weight: Font.Black } } }
            Text { text: "Facebook"; color: Theme.text; font { family: Theme.font; pixelSize: 24; weight: Font.DemiBold } }
            Item { Layout.fillWidth: true }
            BusyIndicator { running: fb.busy; visible: fb.busy; Layout.preferredWidth: 28; Layout.preferredHeight: 28 }
            RowLayout {
                visible: fb.loggedIn
                spacing: 10
                Text { text: fb.user.name || ""; color: Theme.textDim; font.pixelSize: 13 }
                Text { text: "Página:"; color: Theme.textDim; font.pixelSize: 13 }
                ComboBox {
                    id: pageBox
                    Layout.preferredWidth: 240
                    model: fb.pages
                    textRole: "name"
                    valueRole: "id"
                    currentIndex: { for (let i = 0; i < fb.pages.length; ++i) if (fb.pages[i].id === fb.pageId) return i; return -1 }
                    onActivated: (i) => fb.selectPage(fb.pages[i].id)
                }
                GlassButton { text: "Actualizar"; compact: true; onClicked: fb.refresh() }
                GlassButton { text: "Desconectar"; compact: true
                              onClicked: ApplicationWindow.window.confirm({
                                  title: "¿Desconectar la cuenta de Facebook?",
                                  text: (fb.liveId !== "" ? "Hay un vivo en curso: desconectar no lo termina, pero se cortan los comentarios en pantalla y ya no vas a poder terminarlo desde acá. " : "")
                                        + "Se revoca el acceso en Facebook y se borra el token guardado.",
                                  accept: "Desconectar",
                                  reject: "Cancelar",
                                  action: () => fb.logout() }) }
            }
        }

        // --- Sin configurar ------------------------------------------------------------------------------
        ScrollView {
            visible: !fb.configured
            Layout.fillWidth: true
            Layout.fillHeight: true
            contentWidth: availableWidth
            clip: true
            ColumnLayout {
                width: parent.width
                spacing: 12
                Text { Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.textDim; font.pixelSize: 14
                       text: "La API de Facebook permite transmitir y administrar videos en PÁGINAS (no en el perfil personal). Para usarla, creá una app de Meta propia (queda en modo desarrollo: solo vos la usás) — unos 5 minutos:" }
                Repeater {
                    // Depende de redirectUri: se rearma si cambia el puerto.
                    model: fb.redirectUri !== "" ? Studio.accountGuide("facebook").steps : []
                    delegate: Rectangle {
                        required property var modelData
                        Layout.fillWidth: true
                        implicitHeight: fbStep.implicitHeight + 24
                        radius: 14
                        color: Qt.rgba(1, 1, 1, 0.04)
                        border.color: Theme.glassBorder
                        RowLayout {
                            id: fbStep
                            anchors { fill: parent; margins: 12 }
                            spacing: 14
                            Rectangle { Layout.alignment: Qt.AlignTop; width: 30; height: 30; radius: 15; color: page.blue
                                Text { anchors.centerIn: parent; text: modelData.n; color: "white"; font { pixelSize: 14; weight: Font.Black } } }
                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 6
                                Text { text: modelData.t; color: Theme.text; font { pixelSize: 15; weight: Font.DemiBold } }
                                Text { Layout.fillWidth: true; text: modelData.d; color: Theme.textDim; font.pixelSize: 13; wrapMode: Text.WordWrap }
                                GlassButton { compact: true; icon: "↗"; text: modelData.l; onClicked: fb.openUrl(modelData.u) }
                            }
                        }
                    }
                }
                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: fbForm.implicitHeight + 28
                    radius: 14
                    color: Qt.rgba(0.09, 0.47, 0.95, 0.10)
                    border.color: Qt.rgba(0.09, 0.47, 0.95, 0.5)
                    ColumnLayout {
                        id: fbForm
                        anchors { fill: parent; margins: 14 }
                        spacing: 10
                        Text { text: "4 · Cargá los datos de la app"; color: Theme.text; font { pixelSize: 15; weight: Font.DemiBold } }
                        RowLayout {
                            Layout.fillWidth: true
                            TextField { id: fbId; Layout.fillWidth: true; placeholderText: "Identificador de la app (número)" }
                            TextField { id: fbSecret; Layout.fillWidth: true; placeholderText: "Clave secreta de la app"; echoMode: TextInput.Password }
                            TextField { id: fbPort; Layout.preferredWidth: 90; text: String(fb.port); validator: IntValidator { bottom: 1024; top: 65535 }
                                        ToolTip.visible: hovered; ToolTip.text: "Puerto de la redirección (el de la URI del paso 2)" }
                        }
                        RowLayout {
                            GlassButton { text: "Guardar"; active: true; activeColor: page.blue
                                          onClicked: { fbErr.text = fb.saveClient(fbId.text, fbSecret.text, parseInt(fbPort.text) || 8791); if (fbErr.text === "") fbSecret.text = "" } }
                            Text { id: fbErr; color: Theme.live; font.pixelSize: 12; Layout.fillWidth: true; wrapMode: Text.WordWrap }
                        }
                        Text { Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.textFaint; font.pixelSize: 11
                               text: "Se guarda cifrado en Credenciales/, dentro de la carpeta portable (una actualización no lo toca). Permisos que se piden al conectar: páginas (listar, leer, publicar, comentarios) y video en vivo." }
                    }
                }
            }
        }

        // --- Configurado, sin cuenta ------------------------------------------------------------------------------
        ColumnLayout {
            visible: fb.configured && !fb.loggedIn
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 14
            Item { Layout.fillHeight: true }
            Text { Layout.alignment: Qt.AlignHCenter; color: Theme.text; font { pixelSize: 22; weight: Font.DemiBold }
                   text: fb.loginPending ? "Esperando que autorices en el navegador…" : "Conectá tu cuenta de Facebook" }
            Text { Layout.alignment: Qt.AlignHCenter; Layout.maximumWidth: 640; horizontalAlignment: Text.AlignHCenter; wrapMode: Text.WordWrap
                   color: Theme.textDim; font.pixelSize: 14
                   text: fb.loginPending ? "Elegí la cuenta, marcá las Páginas a las que querés transmitir y aceptá los permisos."
                                         : "Se abre Facebook en el navegador. La app guarda solo los tokens (no tu contraseña), dentro de la carpeta portable." }
            BusyIndicator { Layout.alignment: Qt.AlignHCenter; visible: fb.loginPending; running: fb.loginPending }
            RowLayout {
                Layout.alignment: Qt.AlignHCenter
                GlassButton { visible: !fb.loginPending; text: "Conectar con Facebook"; icon: "f"; active: true; activeColor: page.blue; onClicked: fb.login() }
                GlassButton { visible: fb.loginPending; text: "Cancelar"; onClicked: fb.cancelLogin() }
                GlassButton { visible: !fb.loginPending; text: "Cambiar app"
                              onClicked: ApplicationWindow.window.confirm({
                                  title: "¿Cambiar la app de Meta?",
                                  text: "Se borran el identificador y la clave secreta guardados y vuelven las instrucciones para cargar otra app.",
                                  accept: "Cambiar app",
                                  reject: "Cancelar",
                                  action: () => fb.clearClient() }) }
            }
            Text { Layout.alignment: Qt.AlignHCenter; Layout.maximumWidth: 640; wrapMode: Text.WordWrap; visible: fb.lastError !== ""; color: Theme.live; font.pixelSize: 13; text: fb.lastError }
            Item { Layout.fillHeight: true }
        }

        // --- Conectado --------------------------------------------------------------------------------------------
        ColumnLayout {
            visible: fb.loggedIn
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 12
            Rectangle {
                Layout.fillWidth: true
                implicitHeight: goRow.implicitHeight + 28
                radius: 14
                color: Qt.rgba(1, 1, 1, 0.04)
                border.color: Theme.glassBorder
                RowLayout {
                    id: goRow
                    anchors { fill: parent; margins: 14 }
                    spacing: 10
                    ColumnLayout {
                        Layout.fillWidth: true
                        Text { text: "Título del vivo"; color: Theme.textDim; font.pixelSize: 12 }
                        TextField { id: fbTitle; Layout.fillWidth: true; placeholderText: "Transmisión en vivo" }
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        Text { text: "Descripción"; color: Theme.textDim; font.pixelSize: 12 }
                        TextField { id: fbDesc; Layout.fillWidth: true }
                    }
                    GlassButton { Layout.alignment: Qt.AlignBottom; visible: fb.liveId === ""; text: "Transmitir en prueba"
                                  onClicked: fb.goLive(fbTitle.text || "Transmisión en vivo", fbDesc.text, true) }
                    GlassButton { Layout.alignment: Qt.AlignBottom; visible: fb.liveId === ""; text: "Transmitir en vivo"; icon: "●"; active: true; activeColor: page.blue
                                  onClicked: fb.goLive(fbTitle.text || "Transmisión en vivo", fbDesc.text, false) }
                    GlassButton { Layout.alignment: Qt.AlignBottom; visible: fb.liveId !== ""; text: "Terminar"; onClicked: page.askEnd(fb.liveId, "") }
                }
            }
            Rectangle {
                visible: fb.upload.path !== undefined
                Layout.fillWidth: true
                implicitHeight: 48
                radius: 12
                color: Qt.rgba(1, 1, 1, 0.05)
                RowLayout {
                    anchors { fill: parent; margins: 10 }
                    Text { text: "Subiendo «" + (fb.upload.title || "") + "»"; color: Theme.text; font.pixelSize: 13 }
                    ProgressBar { Layout.fillWidth: true; from: 0; to: Math.max(1, fb.upload.total || 1); value: fb.upload.sent || 0 }
                }
            }
            TabBar {
                id: fbTabs
                Layout.fillWidth: true
                background: Rectangle { color: "transparent" }
                TabButton { text: "Vivos"; width: implicitWidth + 30 }
                TabButton { text: "Videos"; width: implicitWidth + 30 }
                TabButton { text: "Grabaciones"; width: implicitWidth + 30 }
                TabButton { text: "Comentarios"; width: implicitWidth + 30 }
            }
            StackLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                currentIndex: fbTabs.currentIndex
                ListView {
                    id: fbLives
                    clip: true
                    spacing: 8
                    model: fb.lives
                    Text { anchors.centerIn: parent; visible: fbLives.count === 0; text: "No hay vivos en esta Página."; color: Theme.textFaint }
                    delegate: Rectangle {
                        required property string uid
                        required property string title
                        required property string status
                        required property string url
                        required property var views
                        width: ListView.view.width
                        height: 64
                        radius: 12
                        color: status === "LIVE" ? Qt.rgba(1, 0.2, 0.2, 0.10) : Qt.rgba(1, 1, 1, 0.04)
                        border.color: fb.liveId === uid ? Theme.live : "transparent"
                        RowLayout {
                            anchors { fill: parent; margins: 10 }
                            spacing: 8
                            Text { text: title || "(sin título)"; color: Theme.text; font { pixelSize: 14; weight: Font.DemiBold } elide: Text.ElideRight; Layout.fillWidth: true }
                            StatusPill { text: page.statusNames[status] || status; dot: status === "LIVE" ? Theme.live : Theme.textFaint; pulse: status === "LIVE" }
                            StatusPill { visible: fb.chatLiveId === uid; text: "comentarios en pantalla"; dot: Theme.accent }
                            GlassButton { compact: true; visible: status === "UNPUBLISHED"; text: "Publicar"; onClicked: fb.publishLive(uid) }
                            GlassButton { compact: true; visible: status === "LIVE" || status === "UNPUBLISHED"; text: "Terminar"; onClicked: page.askEnd(uid, title) }
                            GlassButton { compact: true; text: fb.chatLiveId === uid ? "Sin comentarios" : "Comentarios"; onClicked: fb.setChatLive(fb.chatLiveId === uid ? "" : uid) }
                            GlassButton { compact: true; text: "Ver"; onClicked: fb.openUrl(url) }
                            GlassButton { compact: true; visible: status !== "LIVE" && fb.liveId !== uid; text: "Borrar"; onClicked: page.askDelete(uid, title, "vivo") }
                        }
                    }
                }
                ListView {
                    id: fbVideos
                    clip: true
                    spacing: 8
                    model: fb.videos
                    Text { anchors.centerIn: parent; visible: fbVideos.count === 0; text: "No hay videos en esta Página."; color: Theme.textFaint }
                    delegate: Rectangle {
                        required property string uid
                        required property string title
                        required property string description
                        required property string url
                        required property string created
                        width: ListView.view.width
                        height: 64
                        radius: 12
                        color: Qt.rgba(1, 1, 1, 0.04)
                        RowLayout {
                            anchors { fill: parent; margins: 10 }
                            ColumnLayout {
                                Layout.fillWidth: true
                                Text { text: title || "(sin título)"; color: Theme.text; font { pixelSize: 14; weight: Font.DemiBold } elide: Text.ElideRight; Layout.fillWidth: true }
                                Text { text: page.day(created); color: Theme.textFaint; font.pixelSize: 11 }
                            }
                            GlassButton { compact: true; text: "Editar"; onClicked: fbEdit.edit(uid, title, description) }
                            GlassButton { compact: true; text: "Ver"; onClicked: fb.openUrl(url) }
                            GlassButton { compact: true; text: "Borrar"; onClicked: page.askDelete(uid, title, "video") }
                        }
                    }
                }
                ListView {
                    id: fbRecs
                    clip: true
                    spacing: 6
                    model: fb.recordings
                    Text { anchors.centerIn: parent; visible: fbRecs.count === 0; text: "Todavía no hay grabaciones."; color: Theme.textFaint }
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
                            GlassButton { compact: true; text: "Subir a la Página"; enabled: fb.upload.path === undefined
                                          onClicked: fb.uploadRecording(path, name.replace(/\.[^.]+$/, ""), "") }
                        }
                    }
                }
                ColumnLayout {
                    spacing: 8
                    Text { Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.textDim; font.pixelSize: 12
                           text: fb.chatLiveId === "" ? "Elegí un vivo («Vivos» → «Comentarios»). Para mostrarlos en el video: fuente «Chat en vivo» en una escena."
                                                      : "Comentarios del vivo elegido (se leen cada 5 s)." }
                    ListView {
                        id: fbChat
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        spacing: 4
                        model: fb.chat
                        onCountChanged: positionViewAtEnd()
                        delegate: Item {
                            id: fmsg
                            required property string author
                            required property string text
                            required property var model
                            width: ListView.view.width
                            height: Math.max(fmsgText.implicitHeight, 26)
                            Text { id: fmsgText; width: parent.width - 34; wrapMode: Text.WordWrap; textFormat: Text.PlainText
                                   color: Theme.text; font.pixelSize: 13; text: fmsg.author + ": " + fmsg.text }
                            // Destacar para el aire: los overlays de chat muestran solo lo destacado.
                            GlassButton {
                                anchors { right: parent.right; top: parent.top }
                                compact: true
                                property bool dest: false
                                text: dest ? "★" : "☆"
                                active: dest
                                onClicked: {
                                    const k = "fb:" + fmsg.model.id
                                    if (dest) { Studio.quitarDestacadoChatUi(k); dest = false }
                                    else dest = Studio.destacarChatUi(k)
                                }
                                ToolTip.visible: hovered; ToolTip.delay: 400; ToolTip.text: "Destacar para el chat en pantalla"
                            }
                            // A la cola de preguntas (una sola cola para la sala y las plataformas).
                            GlassButton {
                                anchors { right: parent.right; top: parent.top; rightMargin: 40 }
                                compact: true
                                text: "?"
                                onClicked: { const e = Studio.preguntaDesdeChatUi("fb:" + fmsg.model.id); if (e !== "") Studio.notify(e, "error") }
                                ToolTip.visible: hovered; ToolTip.delay: 400; ToolTip.text: "Sumar a la cola de preguntas"
                            }
                        }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        enabled: fb.chatLiveId !== ""
                        TextField { id: fbInput; Layout.fillWidth: true; placeholderText: "Comentar como la Página…"; onAccepted: { fb.sendChat(text); text = "" } }
                        GlassButton { text: "Enviar"; onClicked: { fb.sendChat(fbInput.text); fbInput.text = "" } }
                    }
                }
            }
        }
    }

    Dialog {
        id: fbEdit
        property string vid: ""
        function edit(id, t, d) { vid = id; feTitle.text = t; feDesc.text = d; open() }
        anchors.centerIn: parent
        modal: true
        title: "Editar video"
        standardButtons: Dialog.Save | Dialog.Cancel
        onAccepted: fb.updateVideo(vid, feTitle.text, feDesc.text)
        ColumnLayout {
            Label { text: "Título" }
            TextField { id: feTitle; Layout.preferredWidth: 480 }
            Label { text: "Descripción" }
            TextField { id: feDesc; Layout.preferredWidth: 480 }
        }
    }
}
