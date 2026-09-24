// Un QR con su enlace, para abrir algo de esto en un teléfono.
//
// Se usa en varias páginas (rótulos, sala, teleprompter) y siempre es lo
// mismo: se pide el enlace al gateway, vuelve con los módulos del QR y se
// dibujan. Lo único que cambia es quién lo pide y qué texto lo acompaña, así
// que eso entra por propiedades.
//
// La clave puede ir DENTRO del QR (se entra sin tipear nada) o no: si el QR
// va a quedar pegado en una pared, la clave se dice aparte.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import BA
import "."

Dialog {
    id: dlg
    anchors.centerIn: parent
    modal: true
    standardButtons: Dialog.Close

    /// Quien lo pide: tiene que tener `pedirQr(target, vista, conClave)` y la
    /// señal `qrListo(url, modulos, mensaje, clave)`.
    property var fuente: null
    /// Si se ofrece elegir entre la red local y por internet.
    property bool ofreceRemoto: true
    /// Si se ofrece dejar la clave afuera del QR (lo que no lleva clave, no).
    property bool ofreceClave: true
    /// La vista que se pide (la entiende `fuente`; "" si no aplica).
    property string vista: ""
    /// El texto de abajo, cuando la clave va adentro.
    property string avisoConClave: "La clave viaja en el fragmento del enlace: no llega al servidor ni " +
                                   "queda en ningún registro. Aun así, el que tenga este QR puede usarlo: " +
                                   "no lo dejes pegado en una pared."
    /// El texto de abajo, cuando no va.
    property string avisoSinClave: "Sin la clave adentro, el QR abre la página y la pide. Decísela vos:"

    property string target: "local"
    property bool conClave: true
    property string url: ""
    property string clave: ""
    property var modulos: []
    property string mensaje: ""
    property bool cargando: false

    function mostrar(t, k) {
        target = t
        conClave = k
        url = ""; modulos = []; mensaje = ""; cargando = true
        open()
        if (fuente) fuente.pedirQr(t, dlg.vista, k)
    }

    Connections {
        target: dlg.fuente
        enabled: dlg.fuente !== null
        function onQrListo(url, modulos, mensaje, clave) {
            dlg.cargando = false
            dlg.url = url
            dlg.modulos = modulos
            dlg.mensaje = mensaje
            dlg.clave = clave
            lienzo.requestPaint()
        }
    }

    ColumnLayout {
        spacing: 12

        Rectangle {
            Layout.alignment: Qt.AlignHCenter
            Layout.preferredWidth: 300
            Layout.preferredHeight: 300
            radius: 12
            color: dlg.url !== "" ? "white" : Qt.rgba(1, 1, 1, 0.05)
            Canvas {
                id: lienzo
                anchors.fill: parent
                anchors.margins: 12
                visible: dlg.url !== ""
                onPaint: {
                    const ctx = getContext("2d")
                    ctx.reset()
                    const filas = dlg.modulos
                    if (!filas || filas.length === 0) return
                    const n = filas.length + 8 // zona silenciosa de 4 módulos
                    const cell = Math.floor(Math.min(width, height) / n)
                    const off = Math.floor((Math.min(width, height) - cell * n) / 2) + 4 * cell
                    ctx.fillStyle = "#000000"
                    for (let y = 0; y < filas.length; ++y)
                        for (let x = 0; x < filas[y].length; ++x)
                            if (filas[y][x] === "1") ctx.fillRect(off + x * cell, off + y * cell, cell, cell)
                }
            }
            Text {
                anchors.fill: parent
                anchors.margins: 20
                visible: dlg.url === ""
                wrapMode: Text.Wrap
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
                color: Theme.textDim
                font.pixelSize: 13
                text: dlg.cargando ? "Generando…" : dlg.mensaje
            }
        }

        RowLayout {
            Layout.alignment: Qt.AlignHCenter
            spacing: 6
            visible: dlg.ofreceRemoto
            GlassButton {
                compact: true; text: "Red local"
                active: dlg.target === "local"
                onClicked: dlg.mostrar("local", dlg.conClave)
            }
            GlassButton {
                compact: true; text: "Por internet"
                active: dlg.target === "remote"
                onClicked: dlg.mostrar("remote", dlg.conClave)
            }
        }

        CheckBox {
            visible: dlg.ofreceClave
            checked: dlg.conClave
            text: "Incluir la clave en el QR (entra sin tipearla)"
            onToggled: dlg.mostrar(dlg.target, checked)
        }

        Text {
            Layout.preferredWidth: 300
            wrapMode: Text.WordWrap
            color: Theme.textDim
            font.pixelSize: 11
            text: !dlg.ofreceClave ? ""
                : (dlg.conClave ? dlg.avisoConClave
                                : dlg.avisoSinClave + " " + (dlg.clave === "" ? "" : dlg.clave))
        }

        // El enlace, PARA PODER USARLO. Estaba como texto muerto: se veía y no
        // se podía ni seleccionar. No todo el mundo escanea un QR —el que está
        // en la misma computadora quiere pegar la dirección en su navegador, y
        // a veces hay que mandarla por mensaje—.
        RowLayout {
            Layout.preferredWidth: 300
            visible: dlg.url !== ""
            spacing: 6
            TextField {
                id: campoUrl
                Layout.fillWidth: true
                readOnly: true
                text: dlg.url
                font.pixelSize: 11
                // Que se pueda seleccionar a mano tambien: copiar un pedazo
                // (solo el host, por ejemplo) es algo que se hace.
                selectByMouse: true
            }
            GlassButton {
                text: "Copiar"
                icon: "⧉"
                compact: true
                onClicked: { campoUrl.selectAll(); Studio.copiar(dlg.url) }
                ToolTip.visible: hovered
                ToolTip.delay: 400
                ToolTip.text: "Copia el enlace al portapapeles"
            }
        }
    }
}
