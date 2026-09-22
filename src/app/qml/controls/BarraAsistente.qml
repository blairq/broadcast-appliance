// El zócalo para hablarle al asistente, abajo de todo y en todas las páginas.
//
// POR QUÉ ES GLOBAL Y NO UNO POR PÁGINA. Empezó siendo un panel adentro del
// editor de gráficos, y después otro igual en Rótulos. Dos copias de lo mismo
// que había que mantener, y en el resto de las páginas no había ninguno: para
// pedir algo mientras se opera había que irse a la pestaña Asistente, perder
// de vista lo que se estaba mirando y explicarle desde cero dónde estabas.
//
// EL CONTEXTO ES LA MITAD DEL ASUNTO. Lo que se manda no es lo que se escribe:
// es lo que se escribe con una frase adelante que dice dónde está parado el
// operador y qué está mirando. Sin eso el asistente pregunta —o, peor,
// adivina—. Con eso, «achicalo un poco» se entiende.
//
// NO MUESTRA LA RESPUESTA, y es a propósito: el asistente es una sesión de
// terminal, no una lista de mensajes, y meter media terminal en una franja de
// 70 px sería ilegible. Lo dice y ofrece el botón que la saca a otra ventana,
// para tener las dos cosas a la vez.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import BA

Panel {
    id: barra

    /// La frase que se le antepone al pedido. La arma quien la usa, que es el
    /// único que sabe qué página está a la vista.
    property string contexto: ""
    /// Pedidos ya escritos para esta página: [{ t: "rótulo", p: "el pedido" }].
    property var sugerencias: []
    /// Para el botón que saca al asistente a su propia ventana.
    property int paginaAsistente: 7

    readonly property var ag: Studio.agent
    readonly property bool listo: ag.running

    // Alto fijo: la barra no puede crecer y achicar la pagina cada vez que
    // se cambia de pantalla. Con sugerencias entran dos filas.
    implicitHeight: sugerencias.length > 0 && listo ? 104 : 64

    signal mandado(string texto)

    function mandar(texto) {
        const t = String(texto).trim()
        if (t === "" || !listo) return
        ag.send(barra.contexto === "" ? t : barra.contexto + " " + t)
        campo.text = ""
        barra.mandado(t)
    }

    ColumnLayout {
        anchors { fill: parent; margins: 10 }
        spacing: 6

        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            Text {
                text: "✦"
                color: barra.listo ? Theme.accent : Theme.textFaint
                font.pixelSize: 16
            }

            TextField {
                id: campo
                Layout.fillWidth: true
                enabled: barra.listo
                placeholderText: barra.listo
                    ? "Pedile algo al asistente sobre lo que estás viendo…"
                    : "El asistente no está corriendo (pestaña Asistente)"
                color: Theme.text
                font.pixelSize: 13
                onAccepted: barra.mandar(text)
            }

            GlassButton {
                text: "Pedir"; compact: true
                enabled: barra.listo && campo.text.trim() !== ""
                onClicked: barra.mandar(campo.text)
            }
            GlassButton {
                icon: "⧉"; compact: true
                onClicked: ApplicationWindow.window.sacarAVentana(barra.paginaAsistente, "Asistente")
                ToolTip.visible: hovered
                ToolTip.text: "La respuesta aparece en el Asistente.\nEsto lo abre en otra ventana, " +
                              "para tener las dos cosas a la vez."
            }
        }

        // Los pedidos ya escritos de esta página. Envuelven: son distintos en
        // cada una y no se sabe cuántos entran.
        Item {
            Layout.fillWidth: true
            visible: barra.sugerencias.length > 0 && barra.listo
            implicitHeight: chips.implicitHeight
            Flow {
                id: chips
                width: parent.width
                spacing: 5
                Repeater {
                    model: barra.sugerencias
                    delegate: GlassButton {
                        required property var modelData
                        compact: true
                        text: modelData.t
                        onClicked: barra.mandar(modelData.p)
                        ToolTip.visible: hovered
                        ToolTip.text: modelData.p
                        ToolTip.delay: 500
                    }
                }
            }
        }
    }
}
