// Confirmacion de acciones de riesgo (borrar, cortar, cerrar...). Hay una sola
// instancia, en Main.qml; paginas y tarjetas la piden con
//   ApplicationWindow.window.confirm({ title, text, accept, reject, action })
// Botones propios: los standardButtons salen en ingles (no hay QTranslator).
import QtQuick
import QtQuick.Controls
import BA

Dialog {
    id: dlg
    property string text: ""
    property string acceptText: "Sí"
    property string rejectText: "No"
    // Corre una sola vez y solo si el operador acepta. Se suelta al cerrar
    // (aceptando o no) para no retener closures de las paginas.
    property var action: null

    /// Pregunta. Si ya habia una pregunta abierta, esta la reemplaza y la
    /// accion de la anterior no corre.
    function ask(opts) {
        opts = opts || {}
        title = opts.title || ""
        text = opts.text || ""
        acceptText = opts.accept || "Sí"
        rejectText = opts.reject || "No"
        action = opts.action || null
        open()
        // El foco arranca en rechazar: un Enter apurado no confirma.
        rejectBtn.forceActiveFocus()
    }
    function finish(ok) {
        const run = ok ? action : null
        action = null
        close()
        if (run) run()
    }

    anchors.centerIn: parent
    width: 444
    modal: true
    focus: true
    onOpened: rejectBtn.forceActiveFocus()
    onClosed: action = null // Esc o clic afuera: es un "no"

    // Boton con foco de teclado (GlassButton no lo tiene): Espacio lo maneja
    // Button; Enter se agrega aca.
    component DialogButton: Button {
        id: b
        property bool risky: false
        implicitHeight: 38
        leftPadding: 18
        rightPadding: 18
        font { family: Theme.font; pixelSize: 13; weight: Font.Medium }
        Keys.onReturnPressed: b.clicked()
        Keys.onEnterPressed: b.clicked()
        contentItem: Text {
            text: b.text
            font: b.font
            color: b.risky ? "white" : Theme.text
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }
        background: Rectangle {
            radius: height / 2
            color: b.risky ? (b.hovered ? Qt.lighter(Theme.live, 1.12) : Theme.live)
                           : (b.hovered ? Qt.rgba(1, 1, 1, 0.14) : Qt.rgba(1, 1, 1, 0.07))
            border.color: b.activeFocus ? "white" : (b.risky ? Qt.lighter(Theme.live, 1.3) : Qt.rgba(1, 1, 1, 0.14))
            border.width: b.activeFocus ? 2 : 1
            Behavior on color { ColorAnimation { duration: 140 } }
        }
    }

    // Como el encabezado del estilo Basic, pero la pregunta hace wrap en vez de cortarse.
    header: Label {
        text: dlg.title
        visible: dlg.title !== ""
        wrapMode: Text.WordWrap
        font.bold: true
        padding: 12
    }

    Label {
        width: dlg.availableWidth
        text: dlg.text
        wrapMode: Text.WordWrap
    }

    footer: Item {
        implicitHeight: buttons.implicitHeight + 24
        Row {
            id: buttons
            anchors { right: parent.right; rightMargin: 12; verticalCenter: parent.verticalCenter }
            spacing: 10
            DialogButton {
                id: rejectBtn
                text: dlg.rejectText
                KeyNavigation.right: acceptBtn
                onClicked: dlg.finish(false)
            }
            // Accion de riesgo: en rojo y nunca con el foco inicial.
            DialogButton {
                id: acceptBtn
                text: dlg.acceptText
                risky: true
                KeyNavigation.left: rejectBtn
                onClicked: dlg.finish(true)
            }
        }
    }
}
