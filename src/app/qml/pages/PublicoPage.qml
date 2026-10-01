// El público: la cola de preguntas y la urna, en la pantalla del que conduce.
//
// PAGINA APARTE Y NO UNA PESTAÑA DEL EVENTO, aunque también esté ahí: esto es
// lo de DURANTE. Con el aire encendido hay que poder mirar la cola y cerrar una
// votación mientras se corre el guion, y si están detrás de una pestaña del
// guion hay que elegir cuál se mira.
//
// Los paneles son los mismos que usa la página de Evento (controls/Panel*.qml)
// y leen el mismo modelo, así que las dos vistas muestran lo mismo y se
// actualizan juntas.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import BA
import "../controls"

Rectangle {
    id: page
    color: Theme.bg
    property real leftInset: 92

    readonly property var pub: Studio.publico

    ColumnLayout {
        anchors { fill: parent; topMargin: 18; bottomMargin: 14 }
        anchors.leftMargin: page.leftInset
        anchors.rightMargin: 20
        spacing: 12

        // --- Encabezado: por dónde entra la gente ----------------------------
        Panel {
            Layout.fillWidth: true
            RowLayout {
                anchors.fill: parent
                spacing: 12
                ColumnLayout {
                    spacing: 0
                    Text {
                        text: "Público"
                        color: Theme.text
                        font { pixelSize: 17; weight: Font.DemiBold }
                    }
                    Text {
                        text: "El que está en la butaca, con su teléfono. No entra a la sala: no " +
                              "pide cámara ni micrófono."
                        color: Theme.textDim
                        font.pixelSize: 12
                    }
                }
                Item { Layout.fillWidth: true }
                Text {
                    visible: !page.pub.enLinea
                    text: "Sin conexión con la sala"
                    color: Theme.live
                    font.pixelSize: 12
                }
                GlassButton {
                    text: "QR para proyectar"
                    icon: "⊞"
                    onClicked: { page.pub.pedirEnlace(false); qrDlg.open() }
                    ToolTip.visible: hovered
                    ToolTip.delay: 400
                    ToolTip.text: "El enlace con la clave adentro, para proyectar o imprimir en el programa"
                }
            }
        }

        // --- Lo de siempre: preguntas y urna ---------------------------------
        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 12

            Panel {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.minimumWidth: 380
                title: "Preguntas"
                PanelPreguntas { anchors.fill: parent }
            }
            Panel {
                Layout.preferredWidth: 480
                Layout.fillHeight: true
                title: "Votación"
                PanelVotacion { anchors.fill: parent }
            }
        }
    }

    // El QR de la página del público. Este es el de TODOS y no acredita a
    // nadie: el que acredita es el personal de cada invitación.
    Dialog {
        id: qrDlg
        anchors.centerIn: Overlay.overlay
        modal: true
        title: "Para que el público pregunte y vote"
        standardButtons: Dialog.Close
        // Sin `anchors.fill: parent`: el Dialog se mide por su contenido, y
        // atar el contenido al tamaño del Dialog es un bucle de bindings.
        ColumnLayout {
            spacing: 10
            Rectangle {
                Layout.alignment: Qt.AlignHCenter
                Layout.preferredWidth: 300
                Layout.preferredHeight: 300
                radius: 12
                color: "white"
                Canvas {
                    id: lienzo
                    anchors { fill: parent; margins: 12 }
                    Connections {
                        target: page.pub
                        function onEnlaceCambio() { lienzo.requestPaint() }
                    }
                    onPaint: {
                        const ctx = getContext("2d")
                        ctx.reset()
                        const filas = (page.pub.enlace && page.pub.enlace.modulos) || []
                        if (filas.length === 0) return
                        const n = filas.length + 8 // zona silenciosa
                        const cell = Math.floor(Math.min(width, height) / n)
                        const off = Math.floor((Math.min(width, height) - cell * n) / 2) + 4 * cell
                        ctx.fillStyle = "#000000"
                        for (let y = 0; y < filas.length; ++y)
                            for (let x = 0; x < filas[y].length; ++x)
                                if (filas[y][x] === "1")
                                    ctx.fillRect(off + x * cell, off + y * cell, cell, cell)
                    }
                }
            }
            RowLayout {
                Layout.preferredWidth: 300
                spacing: 6
                TextField {
                    id: campoUrl
                    Layout.fillWidth: true
                    readOnly: true
                    selectByMouse: true
                    font.pixelSize: 11
                    text: (page.pub.enlace && page.pub.enlace.url) || "…"
                }
                GlassButton {
                    text: "Copiar"
                    compact: true
                    onClicked: { campoUrl.selectAll(); Studio.copiar(campoUrl.text) }
                }
            }
            Text {
                Layout.preferredWidth: 300
                wrapMode: Text.WordWrap
                text: "Lleva la clave adentro a propósito: pedirle a doscientas personas que tipeen " +
                      "una clave es perder a la mitad. Este QR NO acredita a nadie — el que acredita " +
                      "es el personal de cada invitación."
                color: Theme.textDim
                font.pixelSize: 11
            }
        }
    }
}
