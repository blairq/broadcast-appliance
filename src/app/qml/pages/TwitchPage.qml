// Pagina Twitch: registrar la app (instrucciones con enlaces), conectar la
// cuenta con un codigo (twitch.tv/activate), editar el canal (titulo y
// categoria), transmitir, VODs y chat. Todo sale de Studio.twitch.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import BA
import "../controls"

Rectangle {
    id: page
    color: Theme.bg
    property real leftInset: 92
    readonly property var tw: Studio.twitch
    readonly property color purple: "#9146ff"
    function day(iso) { return iso ? new Date(iso).toLocaleString(Qt.locale(), "dd/MM/yyyy HH:mm") : "" }

    ColumnLayout {
        anchors { fill: parent; leftMargin: page.leftInset; margins: 20 }
        spacing: 16

        RowLayout {
            Layout.fillWidth: true
            spacing: 12
            Rectangle { width: 34; height: 34; radius: 8; color: page.purple
                Text { anchors.centerIn: parent; text: "◆"; color: "white"; font.pixelSize: 16 } }
            Text { text: "Twitch"; color: Theme.text; font { family: Theme.font; pixelSize: 24; weight: Font.DemiBold } }
            Item { Layout.fillWidth: true }
            BusyIndicator { running: tw.busy; visible: tw.busy; Layout.preferredWidth: 28; Layout.preferredHeight: 28 }
            RowLayout {
                visible: tw.loggedIn
                spacing: 10
                Rectangle { width: 34; height: 34; radius: 17; clip: true; color: Theme.field
                    Image { anchors.fill: parent; source: tw.avatar; fillMode: Image.PreserveAspectCrop } }
                Text { text: tw.user.display_name || ""; color: Theme.text; font.pixelSize: 14; font.weight: Font.DemiBold }
                StatusPill { text: tw.stream.live ? "EN VIVO · " + tw.stream.viewers + " espectadores" : "fuera del aire"
                             dot: tw.stream.live ? Theme.live : Theme.textFaint; pulse: tw.stream.live === true }
                GlassButton { text: "Actualizar"; compact: true; onClicked: tw.refresh() }
                GlassButton { text: "Ver canal"; compact: true; onClicked: tw.openUrl("https://twitch.tv/" + (tw.user.login || "")) }
                GlassButton {
                    text: "Desconectar"; compact: true
                    onClicked: ApplicationWindow.window.confirm({
                        title: "¿Desconectar la cuenta de Twitch?",
                        // Desconectar no corta la salida: la clave ya esta en uso y el vivo sigue.
                        text: "Se revoca el acceso en Twitch y se borra el token guardado: para volver a usar Twitch hay que conectar la cuenta de nuevo."
                              + (tw.streamingHere ? " La transmisión en curso no se corta, pero el chat se apaga." : ""),
                        accept: "Desconectar",
                        reject: "Cancelar",
                        action: function() { tw.logout() }
                    })
                }
            }
        }

        // --- Sin configurar ------------------------------------------------------------------------
        ScrollView {
            visible: !tw.configured
            Layout.fillWidth: true
            Layout.fillHeight: true
            contentWidth: availableWidth
            clip: true
            ColumnLayout {
                width: parent.width
                spacing: 12
                Text { Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.textDim; font.pixelSize: 14
                       text: "Para que la app (y el asistente por MCP) pueda cambiar el título y la categoría del canal, tomar la clave de transmisión, leer y escribir el chat y administrar los VODs, registrá una app en Twitch (una sola vez, unos 3 minutos):" }
                Repeater {
                    model: Studio.accountGuide("twitch").steps
                    delegate: Rectangle {
                        required property var modelData
                        Layout.fillWidth: true
                        implicitHeight: stepRow.implicitHeight + 24
                        radius: 14
                        color: Qt.rgba(1, 1, 1, 0.04)
                        border.color: Theme.glassBorder
                        RowLayout {
                            id: stepRow
                            anchors { fill: parent; margins: 12 }
                            spacing: 14
                            Rectangle { Layout.alignment: Qt.AlignTop; width: 30; height: 30; radius: 15; color: page.purple
                                Text { anchors.centerIn: parent; text: modelData.n; color: "white"; font { pixelSize: 14; weight: Font.Black } } }
                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 6
                                Text { text: modelData.t; color: Theme.text; font { pixelSize: 15; weight: Font.DemiBold } }
                                Text { Layout.fillWidth: true; text: modelData.d; color: Theme.textDim; font.pixelSize: 13; wrapMode: Text.WordWrap }
                                GlassButton { compact: true; icon: "↗"; text: modelData.l; onClicked: tw.openUrl(modelData.u) }
                            }
                        }
                    }
                }
                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: twForm.implicitHeight + 28
                    radius: 14
                    color: Qt.rgba(0.57, 0.27, 1, 0.10)
                    border.color: Qt.rgba(0.57, 0.27, 1, 0.5)
                    ColumnLayout {
                        id: twForm
                        anchors { fill: parent; margins: 14 }
                        spacing: 10
                        Text { text: "4 · Pegá el ID de cliente"; color: Theme.text; font { pixelSize: 15; weight: Font.DemiBold } }
                        RowLayout {
                            Layout.fillWidth: true
                            TextField { id: twId; Layout.fillWidth: true; placeholderText: "ID de cliente (unos 30 caracteres)" }
                            TextField { id: twSecret; Layout.preferredWidth: 260; placeholderText: "Secreto (solo si es Confidencial)"; echoMode: TextInput.Password }
                        }
                        RowLayout {
                            GlassButton { text: "Guardar"; active: true; activeColor: page.purple
                                          onClicked: { twErr.text = tw.saveClient(twId.text, twSecret.text); if (twErr.text === "") twSecret.text = "" } }
                            Text { id: twErr; color: Theme.live; font.pixelSize: 12; Layout.fillWidth: true; wrapMode: Text.WordWrap }
                        }
                        Text { Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.textFaint; font.pixelSize: 11
                               text: "Se guarda cifrado en Credenciales/, dentro de la carpeta portable (una actualización no lo toca)." }
                    }
                }
            }
        }

        // --- Configurado, sin cuenta -----------------------------------------------------------------------
        ColumnLayout {
            visible: tw.configured && !tw.loggedIn
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 14
            Item { Layout.fillHeight: true }
            Text { Layout.alignment: Qt.AlignHCenter; color: Theme.text; font { pixelSize: 22; weight: Font.DemiBold }
                   text: tw.loginPending ? "Autorizá en Twitch con este código" : "Conectá tu cuenta de Twitch" }
            Rectangle {
                visible: tw.loginPending
                Layout.alignment: Qt.AlignHCenter
                implicitWidth: codeText.implicitWidth + 60; implicitHeight: 84; radius: 16
                color: Qt.rgba(0.57, 0.27, 1, 0.15); border.color: page.purple
                Text { id: codeText; anchors.centerIn: parent; text: tw.userCode; color: "white"; font { pixelSize: 40; weight: Font.Black; letterSpacing: 4 } }
            }
            Text { Layout.alignment: Qt.AlignHCenter; Layout.maximumWidth: 620; horizontalAlignment: Text.AlignHCenter; wrapMode: Text.WordWrap
                   color: Theme.textDim; font.pixelSize: 14
                   text: tw.loginPending ? "Se abrió twitch.tv/activate en el navegador: iniciá sesión con la cuenta del canal, confirmá el código y aceptá los permisos. La app se entera sola."
                                         : "Se abre Twitch en el navegador para elegir la cuenta. La app guarda solo tokens de acceso (no tu contraseña), dentro de la carpeta portable." }
            BusyIndicator { Layout.alignment: Qt.AlignHCenter; visible: tw.loginPending; running: tw.loginPending }
            RowLayout {
                Layout.alignment: Qt.AlignHCenter
                GlassButton { visible: !tw.loginPending; text: "Conectar con Twitch"; icon: "◆"; active: true; activeColor: page.purple; onClicked: tw.login() }
                GlassButton { visible: tw.loginPending; text: "Abrir twitch.tv/activate"; onClicked: tw.openUrl(tw.verificationUri) }
                GlassButton { visible: tw.loginPending; text: "Cancelar"; onClicked: tw.cancelLogin() }
                GlassButton {
                    visible: !tw.loginPending; text: "Cambiar app"
                    onClicked: ApplicationWindow.window.confirm({
                        title: "¿Cambiar la app de Twitch?",
                        text: "Se borra el ID de cliente guardado (y el secreto, si había) y vuelven las instrucciones para registrar la app.",
                        accept: "Cambiar app",
                        reject: "Cancelar",
                        action: function() { tw.clearClient() }
                    })
                }
            }
            Text { Layout.alignment: Qt.AlignHCenter; visible: tw.lastError !== ""; color: Theme.live; font.pixelSize: 13; text: tw.lastError }
            Item { Layout.fillHeight: true }
        }

        // --- Conectado ----------------------------------------------------------------------------------
        ColumnLayout {
            visible: tw.loggedIn
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 12
            Rectangle {
                Layout.fillWidth: true
                implicitHeight: chCol.implicitHeight + 28
                radius: 14
                color: Qt.rgba(1, 1, 1, 0.04)
                border.color: Theme.glassBorder
                ColumnLayout {
                    id: chCol
                    anchors { fill: parent; margins: 14 }
                    spacing: 8
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 10
                        ColumnLayout {
                            Layout.fillWidth: true
                            Text { text: "Título del vivo"; color: Theme.textDim; font.pixelSize: 12 }
                            TextField { id: chTitle; Layout.fillWidth: true; text: tw.channel.title || ""; maximumLength: 140 }
                        }
                        ColumnLayout {
                            Layout.preferredWidth: 300
                            Text { text: "Categoría"; color: Theme.textDim; font.pixelSize: 12 }
                            TextField { id: chGame; Layout.fillWidth: true; text: tw.channel.game_name || ""; onTextEdited: tw.searchCategories(text) }
                        }
                        GlassButton { Layout.alignment: Qt.AlignBottom; text: "Guardar"; onClicked: tw.updateChannel(chTitle.text, chGame.text) }
                        GlassButton { Layout.alignment: Qt.AlignBottom; visible: !tw.streamingHere; text: "Transmitir a Twitch"; icon: "●"; active: true; activeColor: page.purple
                                      onClicked: { tw.updateChannel(chTitle.text, chGame.text); tw.goLive() } }
                        GlassButton {
                            Layout.alignment: Qt.AlignBottom; visible: tw.streamingHere; text: "Cortar transmisión"
                            onClicked: ApplicationWindow.window.confirm({
                                title: "¿Cortar la transmisión a Twitch?",
                                text: "El vivo en Twitch termina y los espectadores dejan de ver la señal. Para volver al aire hay que transmitir de nuevo.",
                                accept: "Cortar transmisión",
                                reject: "Cancelar",
                                action: function() { tw.stopLive() }
                            })
                        }
                    }
                    Flow {
                        Layout.fillWidth: true
                        spacing: 6
                        visible: chGame.activeFocus && tw.categories.count > 0
                        Repeater {
                            model: tw.categories
                            delegate: GlassButton { required property string name; compact: true; text: name; onClicked: chGame.text = name }
                        }
                    }
                }
            }
            TabBar {
                id: twTabs
                Layout.fillWidth: true
                background: Rectangle { color: "transparent" }
                TabButton { text: "VODs"; width: implicitWidth + 30 }
                TabButton { text: "Chat"; width: implicitWidth + 30 }
            }
            StackLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                currentIndex: twTabs.currentIndex
                ListView {
                    id: vodList
                    clip: true
                    spacing: 8
                    model: tw.videos
                    Text { anchors.centerIn: parent; visible: vodList.count === 0; text: "No hay VODs (Twitch guarda los vivos si activás «Almacenar transmisiones anteriores»)."; color: Theme.textFaint }
                    delegate: Rectangle {
                        required property string uid
                        required property string title
                        required property string url
                        required property var thumb
                        required property var views
                        required property string created_at
                        required property string duration
                        required property string type
                        width: ListView.view.width
                        height: 84
                        radius: 12
                        color: Qt.rgba(1, 1, 1, 0.04)
                        RowLayout {
                            anchors { fill: parent; margins: 8 }
                            spacing: 12
                            Rectangle { Layout.preferredWidth: 120; Layout.preferredHeight: 68; radius: 8; color: "black"; clip: true
                                Image { anchors.fill: parent; source: thumb || ""; fillMode: Image.PreserveAspectCrop } }
                            ColumnLayout {
                                Layout.fillWidth: true
                                Text { text: title; color: Theme.text; font { pixelSize: 14; weight: Font.DemiBold } elide: Text.ElideRight; Layout.fillWidth: true }
                                Text { text: type + "  ·  " + page.day(created_at) + "  ·  " + duration + "  ·  " + views + " vistas"; color: Theme.textFaint; font.pixelSize: 11 }
                            }
                            GlassButton { compact: true; text: "Ver"; onClicked: tw.openUrl(url) }
                            GlassButton {
                                compact: true; text: "Borrar"
                                onClicked: {
                                    // Copias: la lista se refresca y este delegate puede no existir cuando el operador conteste.
                                    const id = uid
                                    const nombre = title
                                    const twitch = page.tw
                                    ApplicationWindow.window.confirm({
                                        // El titulo de un VOD llega a 140 caracteres: va en el texto, que corta en lineas.
                                        title: "¿Borrar este VOD de Twitch?",
                                        text: "«" + nombre + "» se borra del canal de Twitch y no se puede recuperar.",
                                        accept: "Borrar",
                                        reject: "Cancelar",
                                        action: function() { twitch.deleteVideo(id) }
                                    })
                                }
                            }
                        }
                    }
                }
                ColumnLayout {
                    spacing: 8
                    RowLayout {
                        Toggle { checked: tw.chatOn; onToggled: (wanted) => tw.setChatOn(wanted) }
                        Text { text: tw.chatOn ? "Leyendo el chat del canal" : "Chat apagado"; color: Theme.textDim; font.pixelSize: 13; Layout.fillWidth: true }
                        Text { text: "Para mostrarlo en el video: fuente «Chat en vivo» en una escena."; color: Theme.textFaint; font.pixelSize: 11 }
                    }
                    ListView {
                        id: twChat
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        spacing: 4
                        model: tw.chat
                        onCountChanged: positionViewAtEnd()
                        delegate: Item {
                            id: tmsg
                            required property string author
                            required property string color
                            required property string text
                            required property var model
                            width: ListView.view.width
                            height: Math.max(tmsgText.implicitHeight, 26)
                            Text {
                                id: tmsgText
                                width: parent.width - 34
                                wrapMode: Text.WordWrap
                                textFormat: Text.PlainText
                                color: Theme.text
                                font.pixelSize: 13
                                text: tmsg.author + ": " + tmsg.text
                            }
                            // Destacar para el aire: los overlays de chat muestran solo lo destacado.
                            GlassButton {
                                anchors { right: parent.right; top: parent.top }
                                compact: true
                                property bool dest: false
                                text: dest ? "★" : "☆"
                                active: dest
                                onClicked: {
                                    const k = "tw:" + tmsg.model.id
                                    if (dest) { Studio.quitarDestacadoChatUi(k); dest = false }
                                    else dest = Studio.destacarChatUi(k)
                                }
                                ToolTip.visible: hovered; ToolTip.delay: 400; ToolTip.text: "Destacar para el chat en pantalla"
                            }
                        }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        enabled: tw.chatOn
                        TextField { id: twInput; Layout.fillWidth: true; placeholderText: "Escribir en el chat…"; onAccepted: { tw.sendChat(text); text = "" } }
                        GlassButton { text: "Enviar"; onClicked: { tw.sendChat(twInput.text); twInput.text = "" } }
                    }
                }
            }
        }
    }
}
