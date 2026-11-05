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
        acceso.conectar(id) // el «conectando» o el error los avisa el modelo
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
                            action: function() { tarjeta.acceso.desconectar() }
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
                    MenuSeparator { visible: !Theme.web; height: visible ? implicitHeight : 0 }
                    MenuItem {
                        visible: !Theme.web; height: visible ? implicitHeight : 0
                        text: qsTr("Configurar pinggy…")
                        onTriggered: { Studio.seccionConfig = "Pinggy"; ApplicationWindow.window.showPage((Studio.riel.paginaPorId["configuracion"] ?? -1)) }
                    }
                }
            }
        }
        // Un lugar por celda (ConferenceViewModel::lugares): cada uno se entera solo de lo
        // suyo, y la red (que cambia en cada foto de la sala) no despierta lo demás. Con
        // muchos, celdas de tamaño legible y se desliza (GrillaAdaptable); los controles,
        // al pasar el mouse («⋯») o tocando la celda.
        GrillaAdaptable {
            id: grilla
            Layout.fillWidth: true
            Layout.fillHeight: true
            celdaMinima: 160
            model: Studio.conference.lugares
            delegate: Item {
                id: lugar
                required property int index
                required property string nombre
                required property string pid
                required property string info
                required property bool sinRetorno
                required property string semaforo
                // `red` (las cifras de su red) NO es un rol de la celda: cambia en cada foto
                // de la sala y despertaría a todas; la sigue un FilaViva mientras está el «⋯».
                width: GridView.view.cellWidth
                height: GridView.view.cellHeight
                readonly property bool chica: width < 300
                readonly property bool elegido: acciones.visible && acciones.lugar === lugar
                readonly property bool mostrarControles: hov.hovered || elegido
                // En la reserva de la grilla (salió de la vista y espera que la reusen): sin video.
                property bool enReserva: false
                GridView.onPooled: {
                    enReserva = true
                    if (acciones.lugar === lugar) acciones.close()
                }
                GridView.onReused: enReserva = false
                Rectangle {
                    id: caja
                    anchors { fill: parent; margins: grilla.margen }
                    radius: lugar.chica ? 10 : 12
                    color: "#000000"
                    // Sin clip: el video se ajusta a la caja y el nombre se corta solo.
                    // Tocarla abre sus acciones donde se tocó (debajo de todo: el «⋯» y los
                    // avisos se quedan con lo suyo).
                    MouseArea {
                        anchors.fill: parent
                        acceptedButtons: Qt.LeftButton | Qt.RightButton
                        cursorShape: Qt.PointingHandCursor
                        onClicked: (ev) => acciones.abrir(lugar, caja, ev.x, ev.y)
                    }
                    border.color: lugar.elegido ? Theme.accent : Theme.glassBorder
                    border.width: lugar.elegido ? 2 : 1
                    VideoItem {
                        anchors { fill: parent; margins: 1 }
                        source: "guest:" + (lugar.index + 1); fillMode: VideoItem.Cover
                        enVista: !lugar.enReserva && grilla.enVista(lugar.index)
                    }
                    Rectangle {
                        anchors { left: parent.left; bottom: parent.bottom; margins: 5 }
                        radius: 8; height: 20; width: Math.min(gl.implicitWidth + 14, parent.width - 10)
                        color: Qt.rgba(0, 0, 0, 0.6)
                        Text { id: gl; anchors.centerIn: parent; width: Math.min(implicitWidth, parent.width - 14); elide: Text.ElideRight
                               text: lugar.nombre || qsTr("Invitado %1").arg(lugar.index + 1)
                               color: lugar.pid !== "" ? Theme.text : Theme.textFaint; font.pixelSize: lugar.chica ? 10 : 11 }
                    }
                    // Sin retorno: su mezcla sin su voz no llega al gateway, y el programa entero no
                    // se le manda (se oiría a sí mismo con demora). Lo publica la sala.
                    Rectangle {
                        visible: lugar.sinRetorno
                        anchors { left: parent.left; top: parent.top; margins: 5 }
                        radius: 10; height: 20; width: lugar.chica ? 20 : sinRet.implicitWidth + 14
                        color: Theme.live
                        Text { id: sinRet; anchors.centerIn: parent; text: lugar.chica ? "🎧" : qsTr("Retorno apagado"); color: "white"; font.pixelSize: 10 }
                        HoverHandler { id: sinRetHov }
                        ToolTip.visible: sinRetHov.hovered; ToolTip.delay: 400
                        ToolTip.text: qsTr("Este invitado no escucha nada: su mezcla sin su voz (mix-minus) no llega a la conferencia. No se le manda el programa entero porque se oiría a sí mismo con demora.")
                    }
                    // La red de este invitado: el semáforo (lo que cambia seguido, un rectángulo
                    // suelto); las cifras, en la ayuda del «⋯».
                    Rectangle {
                        visible: lugar.pid !== "" && !lugar.mostrarControles
                        anchors { right: parent.right; top: parent.top; margins: 6 }
                        width: 12; height: 12; radius: 6
                        color: lugar.semaforo === "rojo" ? Theme.live : lugar.semaforo === "ambar" ? Theme.amber : lugar.semaforo === "verde" ? Theme.ok : Theme.textFaint
                        border.color: "white"; border.width: 1
                    }
                    // El «⋯», solo al pasar el mouse o con su menú abierto (se crea ahí).
                    Loader {
                        anchors { right: parent.right; top: parent.top; margins: 4 }
                        active: lugar.mostrarControles
                        sourceComponent: Component {
                            GlassButton {
                                id: masBtn
                                compact: true; text: "⋯"
                                onClicked: acciones.abrir(lugar, masBtn, 0, masBtn.height + 4)
                                // Las cifras de la red de ESTE lugar, al día mientras está el «⋯» (solo
                                // con el mouse encima): una fila del modelo, no un rol de cada celda.
                                FilaViva { id: suFila; modelo: grilla.model; fila: lugar.index }
                                function cifras() {
                                    const red = lugar.pid !== "" ? (suFila.datos.red || {}) : null
                                    if (!red || red.kbps_in === undefined) return ""
                                    return "\n" + qsTr("Red: %1 kbps · pérdida %2 % · jitter %3 ms · RTT %4 ms")
                                        .arg(red.kbps_in || 0).arg(red.perdida_pct || 0).arg(red.jitter_ms || 0).arg(red.rtt_ms || 0)
                                        + (red.alto ? " · " + red.alto + "p" : "")
                                }
                                ToolTip.visible: hovered; ToolTip.delay: 400
                                ToolTip.text: hovered ? qsTr("Calidad, retorno, ISO y cargo") + cifras() : ""
                            }
                        }
                    }
                    HoverHandler { id: hov }
                }
            }
        }
    }
    // Las acciones de un lugar (un solo menú para todos).
    Menu {
        id: acciones
        property var lugar: null
        readonly property int n: lugar ? lugar.index + 1 : 0
        readonly property string pid: lugar ? lugar.pid : ""
        readonly property bool programa: Studio.emision.retornos[String(n)] === "programa"
        function abrir(l, donde, x, y) { lugar = l; popup(donde, x, y) }
        MenuItem { enabled: false; text: acciones.lugar ? (acciones.lugar.nombre || qsTr("Invitado %1").arg(acciones.n)) : "" }
        MenuSeparator {}
        // Lo que oye este invitado: el programa sin su voz (de fábrica) o el programa entero.
        MenuItem { checkable: true; checked: !acciones.programa; text: qsTr("Oye: programa sin su voz")
                   onTriggered: Studio.emision.setRetornoInvitadoUi(acciones.n, "mix_minus") }
        MenuItem { checkable: true; checked: acciones.programa; text: qsTr("Oye: programa entero")
                   onTriggered: Studio.emision.setRetornoInvitadoUi(acciones.n, "programa") }
        MenuSeparator {}
        MenuItem { enabled: acciones.pid !== ""; text: qsTr("Bajar a %1").arg("720p"); onTriggered: Studio.conference.request(acciones.pid, "calidad-720p") }
        MenuItem { enabled: acciones.pid !== ""; text: qsTr("Bajar a %1").arg("360p"); onTriggered: Studio.conference.request(acciones.pid, "calidad-360p") }
        MenuItem { enabled: acciones.pid !== ""; text: qsTr("Calidad completa"); onTriggered: Studio.conference.request(acciones.pid, "calidad-max") }
        MenuSeparator {}
        MenuItem {
            enabled: acciones.pid !== ""
            text: qsTr("Grabar aparte (ISO)")
            onTriggered: Studio.emision.isoInvitadoUi(acciones.pid)
        }
        MenuSeparator {}
        MenuItem {
            readonly property string info: acciones.lugar ? acciones.lugar.info : ""
            text: qsTr("Aprobar su cargo para el zócalo: «%1»").arg(info || "—")
            enabled: info !== "" && acciones.pid !== ""
            onTriggered: Studio.sala.aprobarInfoInvitado(acciones.pid, "")
        }
    }
}
