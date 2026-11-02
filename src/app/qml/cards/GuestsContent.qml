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
    // El link que conviene pasar lo decide la sala (sala/Direcciones en el gateway): el de
    // la red local si los invitados entran por ahí; si no, el de afuera (el túnel).
    readonly property var dirInvitados: Studio.direcciones && Studio.direcciones.porId.invitados
                                        ? Studio.direcciones.porId.invitados : ({})
    readonly property string link: Studio.conference.guestUrl
    // Mientras se ve, el estado del túnel se consulta cada 5 s.
    Suscripcion { objetivo: tarjeta.acceso; activa: tarjeta.visible }
    function conectar(id) {
        if (acceso.conectar(id))
            Studio.notify(qsTr("Conectando la conferencia a internet… (la conferencia se reinicia: los invitados se reconectan solos)"), "info")
    }
    DialogoQrSala { id: qrDlg }

    ColumnLayout {
        anchors.fill: parent
        spacing: 8
        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            StatusPill {
                text: Studio.gatewayAlive ? qsTr("Conferencia lista") : qsTr("Conferencia caída")
                dot: Studio.gatewayAlive ? Theme.ok : Theme.live; pulse: !Studio.gatewayAlive
                HoverHandler { id: salaHov }
                ToolTip.visible: salaHov.hovered; ToolTip.delay: 400
                ToolTip.text: Studio.gatewayAlive ? qsTr("El servidor de la conferencia (el gateway) está andando: recibe a los invitados por el navegador.")
                                                  : qsTr("El servidor de la conferencia (el gateway) no responde: se reinicia solo. Mientras tanto nadie puede entrar.")
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
            Text { text: qsTr("Conferencia: %1").arg(Studio.room); color: Theme.textFaint; font.pixelSize: 11 }
        }
        RowLayout {
            Layout.fillWidth: true
            spacing: 6
            TextField {
                Layout.fillWidth: true
                readOnly: true
                text: tarjeta.link
                // Sin enlace, por qué (lo dice la sala): nunca un 127.0.0.1 que no le sirve a nadie.
                placeholderText: tarjeta.dirInvitados.motivo_local || tarjeta.dirInvitados.motivo_remoto || ""
                font.pixelSize: 11
                selectByMouse: true
            }
            GlassButton {
                text: qsTr("Copiar"); compact: true
                enabled: tarjeta.link !== ""
                onClicked: { Studio.copiar(tarjeta.link); Studio.notify(qsTr("Link copiado"), "ok") }
            }
            GlassButton {
                text: "QR"; icon: "⊞"; compact: true
                onClicked: qrDlg.show(tarjeta.dirInvitados.local ? "local" : "remote")
                ToolTip.visible: hovered; ToolTip.delay: 400
                ToolTip.text: qsTr("El código para escanear: adentro se elige red local o por internet, y si va el PIN.")
            }
            GlassButton {
                id: vpnBtn
                compact: true
                text: tarjeta.cx.activo ? qsTr("Desconectar") : qsTr("Conectar VPN")
                active: !tarjeta.cx.activo
                onClicked: {
                    if (tarjeta.cx.activo) {
                        // Desconectar saca a todos los que entraron de afuera: se confirma y
                        // el botón tarda 3 s en habilitarse (no se aprieta de apuro).
                        ApplicationWindow.window.confirm({
                            title: qsTr("¿Desconectar la conferencia de internet?"),
                            text: qsTr("Los invitados y los integrantes que entraron desde afuera se cortan, y nadie de afuera puede volver a entrar hasta que la conectes de nuevo. La conferencia se reinicia (los de la red local se reconectan solos)."),
                            accept: qsTr("Desconectar"), reject: qsTr("Cancelar"), espera: 3,
                            action: function() {
                                if (tarjeta.acceso.desconectar())
                                    Studio.notify(qsTr("La conferencia vuelve a ser solo de la red local (se reinicia)"), "info")
                            }
                        })
                    } else {
                        vpnMenu.popup(vpnBtn, 0, vpnBtn.height)
                    }
                }
                ToolTip.visible: hovered; ToolTip.delay: 400
                ToolTip.text: tarjeta.cx.activo ? qsTr("Corta el acceso desde internet (la conferencia se reinicia).")
                                                : qsTr("Saca la conferencia a internet para que entren invitados de afuera.")
                Menu {
                    id: vpnMenu
                    ItemsDeMenu {
                        menu: vpnMenu
                        model: tarjeta.formas
                        delegate: MenuItem {
                            required property var modelData
                            text: modelData.nombre + " — " + modelData.detalle
                            onTriggered: tarjeta.conectar(modelData.id)
                        }
                    }
                    MenuSeparator {}
                    MenuItem {
                        text: qsTr("Configurar pinggy…")
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
                // Un lugar por fila (ConferenceViewModel::lugares): cada uno se entera solo de lo
                // suyo, y la red (que cambia en cada foto de la sala) no despierta lo demás.
                model: Studio.conference.lugares
                delegate: Rectangle {
                    id: lugar
                    required property int index
                    required property string nombre
                    required property string pid
                    required property string info
                    required property bool sinRetorno
                    required property string semaforo
                    required property var red
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
                        Text { id: gl; anchors.centerIn: parent; text: lugar.nombre || qsTr("Invitado %1").arg(index + 1); color: Theme.text; font.pixelSize: 10 }
                    }
                    // Sin retorno: su mezcla sin su voz no llega al gateway, y el programa entero no
                    // se le manda (se oiría a sí mismo con demora). Lo publica la sala.
                    Rectangle {
                        visible: lugar.sinRetorno
                        anchors { left: parent.left; top: parent.top; margins: 6 }
                        radius: 8; height: 20; width: sinRet.implicitWidth + 14
                        color: Theme.live
                        Text { id: sinRet; anchors.centerIn: parent; text: qsTr("Retorno apagado"); color: "white"; font.pixelSize: 10 }
                        HoverHandler { id: sinRetHov }
                        ToolTip.visible: sinRetHov.hovered; ToolTip.delay: 400
                        ToolTip.text: qsTr("Este invitado no escucha nada: su mezcla sin su voz (mix-minus) no llega a la conferencia. No se le manda el programa entero porque se oiría a sí mismo con demora.")
                    }
                    // La red de este invitado: semáforo con las cifras y bajar su calidad.
                    Rectangle {
                        id: semaforo
                        visible: lugar.pid !== ""
                        anchors { right: parent.right; top: parent.top; margins: 6 }
                        width: 14; height: 14; radius: 7
                        color: lugar.semaforo === "rojo" ? Theme.live : lugar.semaforo === "ambar" ? Theme.amber : lugar.semaforo === "verde" ? Theme.ok : Theme.textFaint
                        border.color: "white"; border.width: 1
                        ToolTip.visible: semHov.hovered
                        // Las cifras, solo mientras se las mira (cambian en cada foto).
                        ToolTip.text: !semHov.hovered ? "" : qsTr("Red: %1 kbps · pérdida %2 % · jitter %3 ms · RTT %4 ms")
                                          .arg(lugar.red.kbps_in || 0).arg(lugar.red.perdida_pct || 0).arg(lugar.red.jitter_ms || 0).arg(lugar.red.rtt_ms || 0)
                                      + (lugar.red.alto ? " · " + lugar.red.alto + "p" : "")
                        HoverHandler { id: semHov }
                        TapHandler { onTapped: calidadMenu.popup() }
                        Menu {
                            id: calidadMenu
                            MenuItem { text: qsTr("Bajar a %1").arg("720p"); onTriggered: Studio.conference.request(lugar.pid, "calidad-720p") }
                            MenuItem { text: qsTr("Bajar a %1").arg("360p"); onTriggered: Studio.conference.request(lugar.pid, "calidad-360p") }
                            MenuItem { text: qsTr("Calidad completa"); onTriggered: Studio.conference.request(lugar.pid, "calidad-max") }
                            MenuSeparator {}
                            MenuItem {
                                text: qsTr("Grabar aparte (ISO)")
                                onTriggered: Studio.emision.isoInvitadoUi(lugar.pid)
                            }
                            MenuSeparator {}
                            MenuItem {
                                text: qsTr("Aprobar su cargo para el zócalo: «%1»").arg(lugar.info || "—")
                                enabled: lugar.info !== ""
                                onTriggered: { const e = Studio.sala.aprobarInfoInvitado(lugar.pid, ""); Studio.notify(e !== "" ? e : qsTr("Cargo aprobado para el zócalo"), e !== "" ? "error" : "info") }
                            }
                        }
                    }
                    // Lo que oye este invitado: el programa sin su voz (de fábrica) o el programa entero.
                    ComboBox {
                        id: oye
                        anchors { right: parent.right; bottom: parent.bottom; margins: 6 }
                        width: 150; height: 24
                        font.pixelSize: 10
                        model: [ { t: qsTr("Oye: programa sin su voz"), v: "mix_minus" }, { t: qsTr("Oye: programa entero"), v: "programa" } ]
                        textRole: "t"
                        currentIndex: Studio.emision.retornos[String(index + 1)] === "programa" ? 1 : 0
                        onActivated: (i) => Studio.emision.setRetornoInvitadoUi(index + 1, model[i].v)
                        ToolTip.visible: hovered; ToolTip.delay: 400
                        ToolTip.text: qsTr("Sin su voz (mix-minus): no se escucha con retardo a sí mismo ni hace eco. Entero: solo si usa auriculares y lo pide.")
                    }
                }
            }
        }
    }
}
