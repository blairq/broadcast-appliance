// Confirmacion de acciones de riesgo (borrar, cortar, cerrar...). Hay una sola
// instancia, en Main.qml; paginas y tarjetas la piden con
//   ApplicationWindow.window.confirm({ title, text, accept, reject, action, espera })
// `espera` (segundos): para lo que no tiene vuelta atrás fácil, el botón de
// aceptar recién se habilita después de esa cuenta (se ve en el botón).
// Botones propios: los standardButtons salen en ingles (no hay QTranslator).
import QtQuick
import QtQuick.Controls
import BA

Dialog {
    id: dlg
    property string text: ""
    property string acceptText: qsTr("Sí")
    property string rejectText: qsTr("No")
    // Corre una sola vez y solo si el operador acepta. Se suelta al cerrar
    // (aceptando o no) para no retener closures de las paginas.
    property var action: null
    property int restante: 0 // segundos que faltan para poder aceptar (opción `espera`)

    /// Pregunta. Si ya habia una pregunta abierta, esta la reemplaza y la
    /// accion de la anterior no corre.
    function ask(opts) {
        opts = opts || {}
        title = opts.title || ""
        text = opts.text || ""
        acceptText = opts.accept || qsTr("Sí")
        rejectText = opts.reject || qsTr("No")
        action = opts.action || null
        restante = Math.max(0, opts.espera || 0)
        cuenta.restart()
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
    onClosed: { action = null; cuenta.stop() } // Esc o clic afuera: es un "no"
    Timer { id: cuenta; interval: 1000; repeat: true; running: false; onTriggered: if (--dlg.restante <= 0) stop() }

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

    Column {
        width: dlg.availableWidth
        spacing: 10

        // Modo evento: con el guion corriendo de verdad, cualquier cosa que se
        // apriete sale al aire. No se bloquea nada -el operador sabe lo que
        // hace- pero se le recuerda donde esta parado antes de decidir.
        Rectangle {
            visible: Studio.evento.enVivo
            width: parent.width
            height: enVivoTxt.implicitHeight + 16
            radius: 6
            color: Qt.rgba(1, 0.23, 0.19, 0.16)
            border.color: Theme.live
            Text {
                id: enVivoTxt
                anchors { fill: parent; margins: 8 }
                wrapMode: Text.WordWrap
                color: Theme.text
                font { family: Theme.font; pixelSize: 12; weight: Font.DemiBold }
                text: qsTr("● EL EVENTO ESTÁ CORRIENDO — %1. Lo que hagas ahora sale al aire.")
                      .arg(Studio.evento.actual.texto || Studio.evento.actual.tipo || "")
            }
        }

        Label {
            width: parent.width
            text: dlg.text
            wrapMode: Text.WordWrap
        }
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
                text: dlg.restante > 0 ? qsTr("%1 (%2)").arg(dlg.acceptText).arg(dlg.restante) : dlg.acceptText
                enabled: dlg.restante <= 0
                opacity: enabled ? 1 : 0.5
                risky: true
                KeyNavigation.left: rejectBtn
                onClicked: if (dlg.restante <= 0) dlg.finish(true)
            }
        }
    }
}
