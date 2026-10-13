// Tarjeta "Invitados": sala, link de ingreso y miniaturas de cada invitado.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import BA
import "../controls"

Item {
    id: tarjeta
    readonly property var acceso: Studio.accesoRemoto
    readonly property var cx: acceso ? acceso.conexion : ({})
    readonly property var formas: acceso ? acceso.formasDeConectar : []
    // El link que conviene pasar: el de internet si la sala sale por un túnel; si no, el local.
    readonly property string link: cx.estado === "listo" && cx.url ? cx.url.replace(/\/$/, "") + "/invitados/#room=" + Studio.room
                                                                  : Studio.joinUrl
    // Mientras se ve, el estado del túnel se consulta cada 5 s.
    Binding { target: tarjeta.acceso; property: "siguiendo"; value: tarjeta.visible; when: !!tarjeta.acceso }
    function conectar(id) {
        const e = acceso.conectar(id)
        Studio.notify(e !== "" ? e : "Conectando la sala a internet… (la sala se reinicia: los invitados se reconectan solos)",
                      e !== "" ? "error" : "info")
    }
    DialogoQrSala { id: qrDlg }

    ColumnLayout {
        anchors.fill: parent
        spacing: 8
        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            StatusPill {
                text: Studio.gatewayAlive ? "Sala lista" : "Sala caída"
                dot: Studio.gatewayAlive ? Theme.ok : Theme.live; pulse: !Studio.gatewayAlive
                HoverHandler { id: salaHov }
                ToolTip.visible: salaHov.hovered; ToolTip.delay: 400
                ToolTip.text: Studio.gatewayAlive ? "El servidor de la sala (el gateway) está andando: recibe a los invitados por el navegador."
                                                  : "El servidor de la sala (el gateway) no responde: se reinicia solo. Mientras tanto nadie puede entrar."
            }
            // Por dónde se puede entrar: solo la red local, o también por internet.
            Rectangle { implicitWidth: 8; implicitHeight: 8; radius: 4
                        color: tarjeta.cx.estado === "listo" ? Theme.ok : tarjeta.cx.estado === "error" ? Theme.live
                             : tarjeta.cx.activo ? Theme.amber : Theme.textFaint }
            Text {
                Layout.fillWidth: true
                text: tarjeta.cx.texto || ""
                color: tarjeta.cx.estado === "error" ? Theme.live : Theme.textDim
                font.pixelSize: 11; elide: Text.ElideRight
                HoverHandler { id: cxHov }
                ToolTip.visible: cxHov.hovered && truncated; ToolTip.text: text
            }
            Text { text: "Sala: " + Studio.room; color: Theme.textFaint; font.pixelSize: 11 }
        }
        RowLayout {
            Layout.fillWidth: true
            spacing: 6
            TextField {
                Layout.fillWidth: true
                readOnly: true
                text: tarjeta.link
                font.pixelSize: 11
                selectByMouse: true
            }
            GlassButton {
                text: "Copiar"; compact: true
                onClicked: { Studio.copiar(tarjeta.link); Studio.notify("Link copiado", "ok") }
            }
            GlassButton {
                text: "QR"; icon: "⊞"; compact: true
                onClicked: qrDlg.show(tarjeta.cx.estado === "listo" ? "remote" : "local")
                ToolTip.visible: hovered; ToolTip.delay: 400
                ToolTip.text: "El código para escanear: adentro se elige red local o por internet, y si va el PIN."
            }
            GlassButton {
                id: vpnBtn
                compact: true
                text: tarjeta.cx.activo ? "Desconectar" : "Conectar VPN"
                active: !tarjeta.cx.activo
                onClicked: {
                    if (tarjeta.cx.activo) {
                        const e = tarjeta.acceso.desconectar()
                        Studio.notify(e !== "" ? e : "La sala vuelve a ser solo de la red local (se reinicia)", e !== "" ? "error" : "info")
                    } else {
                        vpnMenu.popup(vpnBtn, 0, vpnBtn.height)
                    }
                }
                ToolTip.visible: hovered; ToolTip.delay: 400
                ToolTip.text: tarjeta.cx.activo ? "Corta el acceso desde internet (la sala se reinicia)."
                                                : "Saca la sala a internet para que entren invitados de afuera."
                Menu {
                    id: vpnMenu
                    Repeater {
                        model: tarjeta.formas
                        delegate: MenuItem {
                            required property var modelData
                            text: modelData.nombre + " — " + modelData.detalle
                            onTriggered: tarjeta.conectar(modelData.id)
                        }
                    }
                    MenuSeparator {}
                    MenuItem {
                        text: "Configurar pinggy o Cloudflare…"
                        onTriggered: { Studio.seccionConfig = "Pinggy"; ApplicationWindow.window.showPage(Studio.riel.paginaDeId("configuracion")) }
                    }
                }
            }
        }
        GridLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            columns: Math.max(1, Math.min(Studio.guests, 2))
            columnSpacing: 8; rowSpacing: 8
            Repeater {
                model: Studio.guests
                delegate: Rectangle {
                    required property int index
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    radius: 12
                    color: "#000000"
                    clip: true
                    VideoItem { id: v; anchors.fill: parent; source: "guest:" + (index + 1); fillMode: VideoItem.Cover }
                    Rectangle {
                        anchors { left: parent.left; bottom: parent.bottom; margins: 6 }
                        radius: 8; height: 20; width: gl.implicitWidth + 14
                        color: Qt.rgba(0, 0, 0, 0.55)
                        Text { id: gl; anchors.centerIn: parent; text: Studio.conference.slotNames[index] || ("Invitado " + (index + 1)); color: Theme.text; font.pixelSize: 10 }
                    }
                    // La red de este invitado: semáforo con las cifras y bajar su calidad.
                    Rectangle {
                        id: semaforo
                        property var p: Studio.conference.participanteEnSlot(index + 1)
                        Timer { interval: 2000; running: true; repeat: true; onTriggered: semaforo.p = Studio.conference.participanteEnSlot(index + 1) }
                        readonly property var red: (p && p.red) || ({})
                        visible: !!(p && p.id)
                        anchors { right: parent.right; top: parent.top; margins: 6 }
                        width: 14; height: 14; radius: 7
                        color: red.semaforo === "rojo" ? Theme.live : red.semaforo === "ambar" ? Theme.amber : red.semaforo === "verde" ? Theme.ok : Theme.textFaint
                        border.color: "white"; border.width: 1
                        ToolTip.visible: semHov.hovered
                        ToolTip.text: "Red: " + (red.kbps_in || 0) + " kbps · pérdida " + (red.perdida_pct || 0) + " % · jitter " +
                                      (red.jitter_ms || 0) + " ms · RTT " + (red.rtt_ms || 0) + " ms" + (red.alto ? " · " + red.alto + "p" : "")
                        HoverHandler { id: semHov }
                        TapHandler { onTapped: calidadMenu.popup() }
                        Menu {
                            id: calidadMenu
                            MenuItem { text: "Bajar a 720p"; onTriggered: Studio.conference.request(semaforo.p.id, "calidad-720p") }
                            MenuItem { text: "Bajar a 360p"; onTriggered: Studio.conference.request(semaforo.p.id, "calidad-360p") }
                            MenuItem { text: "Calidad completa"; onTriggered: Studio.conference.request(semaforo.p.id, "calidad-max") }
                            MenuSeparator {}
                            MenuItem {
                                text: "Grabar aparte (ISO)"
                                onTriggered: Studio.isoInvitadoUi(semaforo.p.id)
                            }
                            MenuSeparator {}
                            MenuItem {
                                text: "Aprobar su cargo para el zócalo: «" + ((semaforo.p && semaforo.p.info) || "—") + "»"
                                enabled: !!(semaforo.p && semaforo.p.info)
                                onTriggered: { const e = Studio.aprobarInfoInvitado(semaforo.p.id, ""); Studio.notify(e !== "" ? e : "Cargo aprobado para el zócalo", e !== "" ? "error" : "info") }
                            }
                        }
                    }
                    // Lo que oye este invitado: el programa sin su voz (de fábrica) o el programa entero.
                    ComboBox {
                        id: oye
                        anchors { right: parent.right; bottom: parent.bottom; margins: 6 }
                        width: 150; height: 24
                        font.pixelSize: 10
                        model: [ { t: "Oye: programa sin su voz", v: "mix_minus" }, { t: "Oye: programa entero", v: "programa" } ]
                        textRole: "t"
                        currentIndex: Studio.retornoInvitadoUi(index + 1) === "programa" ? 1 : 0
                        onActivated: (i) => { const e = Studio.setRetornoInvitadoUi(index + 1, model[i].v); if (e !== "") Studio.notify(e, "error") }
                        ToolTip.visible: hovered; ToolTip.delay: 400
                        ToolTip.text: "Sin su voz (mix-minus): no se escucha con retardo a sí mismo ni hace eco. Entero: solo si usa auriculares y lo pide."
                    }
                }
            }
        }
    }
}
