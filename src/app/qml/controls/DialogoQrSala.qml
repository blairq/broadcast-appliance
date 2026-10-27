// DialogoQrSala.qml — el QR para entrar a la sala: red local o por internet, con o
// sin el PIN, y el del tablero de oradores. Lo arma la API de control del gateway
// (conf.requestQr / requestBoardLink → qrReady). Lo usan la página Sala y la
// tarjeta Invitados del tablero: show("local"|"remote"|"board-local"|"board-remote").
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import BA

Dialog {
    id: qrDlg
    /// La sala (Studio.conference).
    property var conf: Studio.conference
    anchors.centerIn: Overlay.overlay
    modal: true
    standardButtons: Dialog.Close
    property string target: "local"
    property string url: ""
    property var modules: []
    property string message: ""
    property bool loading: false
    readonly property bool board: target.startsWith("board-")
    title: board ? (target === "board-remote" ? qsTr("Tablero de oradores (solo lectura) — por internet")
                                              : qsTr("Tablero de oradores (solo lectura) — red local"))
                 : target === "remote" ? qsTr("QR remoto — por internet") : qsTr("QR local — misma red")
    function show(t) {
        target = t; url = ""; modules = []; message = ""; loading = true
        open()
        if (board) qrDlg.conf.requestBoardLink(t.substring(6))
        else qrDlg.conf.requestQr(t, qrPin.checked)
    }
    Connections {
        target: qrDlg.conf
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
                text: qrDlg.loading ? qsTr("Generando…") : qrDlg.message
            }
        }
        // Donde sirve el QR: se puede cambiar sin cerrar.
        RowLayout {
            Layout.alignment: Qt.AlignHCenter
            visible: !qrDlg.board
            spacing: 6
            GlassButton {
                compact: true; text: qsTr("Red local")
                active: qrDlg.target === "local"
                onClicked: qrDlg.show("local")
            }
            GlassButton {
                compact: true; text: qsTr("Por internet")
                active: qrDlg.target === "remote"
                onClicked: qrDlg.show("remote")
            }
        }
        CheckBox {
            id: qrPin
            visible: !qrDlg.board
            text: qsTr("Incluir el PIN en el QR (entran sin tipearlo)")
            onToggled: qrDlg.show(qrDlg.target)
        }
        RowLayout {
            visible: qrDlg.url !== ""
            Layout.preferredWidth: 300
            TextField { id: qrUrl; Layout.fillWidth: true; readOnly: true; text: qrDlg.url; font.pixelSize: 11 }
            GlassButton { text: qsTr("Copiar"); compact: true; onClicked: Studio.copiar(qrDlg.url) }
        }
        Text {
            Layout.preferredWidth: 300
            wrapMode: Text.Wrap
            color: Theme.textFaint; font.pixelSize: 11
            visible: qrDlg.url !== ""
            text: qrDlg.board ? qsTr("Abrilo en la tablet: muestra la fila, las caras y los tiempos. La clave cambia en cada arranque.") :
                  (qrDlg.message !== "" ? qrDlg.message + " " : "") +
                  (qrDlg.target === "remote" ? qsTr("Sirve desde cualquier lugar.") : qsTr("Sirve para dispositivos en la misma red (wifi) que este equipo.")) +
                  " " + (qrPin.checked ? qsTr("Incluye el PIN: quien tenga este QR puede pedir entrar (igual pasa por el lobby).") : qsTr("El PIN no va en el QR: pasalo aparte."))
        }
    }
}
