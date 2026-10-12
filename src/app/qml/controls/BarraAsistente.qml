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
    readonly property var voz: Studio.voz

    // Lo dictado entra como lo escrito: con el contexto de la página y por
    // el mismo camino (agent/Voz.hpp). Si el asistente no corre, queda en el
    // campo para que no se pierda.
    Connections {
        target: barra.voz
        function onDictado(texto) {
            if (barra.listo) barra.mandar(texto)
            else campo.text = texto
        }
    }

    // El alto SALE DEL CONTENIDO, no de un numero escrito a mano. Estaba en 64
    // y no cerraba: Panel ya mete su contenido 16 px, esta barra le sumaba 10
    // mas, y quedaban 26 px arriba de la fila y ninguno abajo —el cuadro de
    // texto terminaba pegado al borde de abajo—. Dos margenes y un alto fijo
    // que hay que mantener de acuerdo a mano se desacomodan a la primera.
    padding: 10
    implicitHeight: cuerpo.implicitHeight + 2 * padding

    signal mandado(string texto)

    function mandar(texto) {
        const t = String(texto).trim()
        if (t === "" || !listo) return
        ag.send(barra.contexto === "" ? t : barra.contexto + " " + t)
        campo.text = ""
        barra.mandado(t)
    }

    ColumnLayout {
        id: cuerpo
        anchors.fill: parent // el margen ya lo puso Panel (`padding`)
        spacing: 6

        // Lo que el modelo local propone con lo dictado: nada sale al aire sin
        // un click, salvo lo que el operador dejó «sin volver a preguntar»
        // (Configuración → Voz del operador).
        Rectangle {
            id: propuesta
            readonly property var p: barra.voz.propuesta || ({})
            visible: !!p.herramienta
            Layout.fillWidth: true
            implicitHeight: filaPropuesta.implicitHeight + 16
            radius: 10
            color: Qt.rgba(1, 0.42, 0.17, 0.10)
            border.color: Qt.rgba(1, 0.42, 0.17, 0.45)
            ColumnLayout {
                id: filaPropuesta
                anchors { fill: parent; margins: 8 }
                spacing: 6
                Text {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    color: Theme.text; font.pixelSize: 13
                    text: "«" + (propuesta.p.frase || "") + "» → ¿<b>" + (propuesta.p.titulo || "") + "</b>?"
                    textFormat: Text.StyledText
                }
                // Exactamente lo que se va a hacer (los datos los completó el modelo local).
                Text {
                    Layout.fillWidth: true
                    visible: text !== ""
                    wrapMode: Text.WordWrap
                    color: Theme.text; font { pixelSize: 12; family: "Menlo" }
                    text: propuesta.p.resumen || ""
                }
                Text {
                    Layout.fillWidth: true
                    visible: !!propuesta.p.notas
                    wrapMode: Text.WordWrap
                    color: Theme.textDim; font.pixelSize: 11
                    text: "Ajustado: " + (propuesta.p.notas || "")
                }
                Text {
                    Layout.fillWidth: true
                    visible: !!propuesta.p.bloqueos
                    wrapMode: Text.WordWrap
                    color: Theme.warn; font.pixelSize: 11
                    text: "No se puede hacer así: " + (propuesta.p.bloqueos || "") + ". Decilo de nuevo o pasalo al asistente."
                }
                Flow {
                    Layout.fillWidth: true
                    spacing: 6
                    GlassButton { compact: true; text: "Hacer"; active: true; enabled: !!propuesta.p.puedeHacer; onClicked: barra.voz.confirmar(false) }
                    GlassButton {
                        compact: true; text: "Hacer y no volver a preguntar"; enabled: !!propuesta.p.puedeHacer
                        ToolTip.visible: hovered; ToolTip.delay: 400
                        ToolTip.text: "«" + (propuesta.p.titulo || "") + "» se va a hacer sola la próxima vez. Se puede quitar en Configuración → Voz del operador."
                        onClicked: barra.voz.confirmar(true)
                    }
                    GlassButton { compact: true; text: "Al asistente"; onClicked: barra.voz.alAsistente() }
                    GlassButton { compact: true; text: "No"; onClicked: barra.voz.descartar() }
                }
            }
        }

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

            // Mantener apretado para hablar (o la tecla, en cualquier página).
            GlassButton {
                visible: barra.voz.disponible
                compact: true
                icon: barra.voz.grabando ? "●" : barra.voz.transcribiendo ? "…" : "🎙"
                active: barra.voz.grabando
                activeColor: Theme.live
                enabled: barra.voz.lista || barra.voz.grabando
                ToolTip.visible: hovered
                ToolTip.text: barra.voz.lista
                    ? "Mantené apretado (o " + barra.voz.tecla + ") y hablá. Mientras tanto el lobby no te oye."
                    : barra.voz.estado
                MouseArea {
                    anchors.fill: parent
                    enabled: parent.enabled
                    onPressed: barra.voz.apretar()
                    onReleased: barra.voz.soltar()
                    onCanceled: barra.voz.cancelar()
                }
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
