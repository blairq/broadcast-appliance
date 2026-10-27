// La cola de preguntas del público, para moderarla.
//
// Se usa en DOS lados —la página de Público y la pestaña del Evento— y por eso
// es un control y no una página: el estado vive en `Studio.publico` (uno solo)
// y esto solo lo dibuja. Dos copias del mismo panel muestran lo mismo y se
// actualizan juntas.
//
// El recorrido de una pregunta: nueva → elegida → al aire → respondida, o
// descartada. `elegida` es la cola de verdad —lo que se va a preguntar, en
// orden— y tenerla aparte de `nueva` es lo que permite moderar antes en vez de
// leer en vivo lo primero que llegó.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import BA
import "."

// La raíz es un Item y no el ColumnLayout para que el diálogo quede AFUERA del
// layout: un Popup declarado adentro lo cuenta como un hijo más al que hay que
// darle lugar.
Item {
    id: raiz

    readonly property var pub: Studio.publico
    /// Lo pone en true VentanaSuelta: ya está en su ventana, no ofrece sacarlo
    /// de nuevo.
    property bool enVentana: false
    // De dónde llegó: la clave viaja al gateway (en español); acá, la etiqueta.
    readonly property var origenes: [ { t: qsTr("sala"), v: "sala" }, { t: qsTr("público"), v: "publico" },
                                      { t: "YouTube", v: "youtube" }, { t: "Twitch", v: "twitch" }, { t: "Facebook", v: "facebook" } ]
    function origen(v) { const o = origenes.find(x => x.v === v); return o ? o.t : v }

    // Prende la consulta al gateway mientras se ve, y la apaga al irse: si no,
    // el aparato le preguntaría cada dos segundos para siempre.
    Component.onCompleted: pub.activo = true
    Component.onDestruction: pub.activo = false

    ColumnLayout {
    anchors.fill: parent
    spacing: 8

    RowLayout {
        Layout.fillWidth: true
        spacing: 8
        Text {
            text: {
                const r = raiz.pub.recuento
                if (!raiz.pub.enLinea) return qsTr("Sin conexión con el servidor")
                if (!r || r.total === undefined) return "…"
                return qsTr("%1 sin mirar · %2 en la cola · %3 respondidas").arg(r.nuevas).arg(r.elegidas).arg(r.respondidas)
            }
            color: raiz.pub.enLinea ? Theme.textDim : Theme.live
            font.pixelSize: 12
        }
        Item { Layout.fillWidth: true }
        GlassButton {
            compact: true
            visible: !raiz.enVentana
            text: "⧉"
            onClicked: ApplicationWindow.window.sacarPanel("preguntas", qsTr("Preguntas"))
            ToolTip.visible: hovered
            ToolTip.delay: 400
            ToolTip.text: qsTr("Sacar a su propia ventana, para mandarla a otro monitor")
        }
        GlassButton {
            compact: true
            text: qsTr("Sumar una")
            onClicked: sumarDlg.open()
            ToolTip.visible: hovered
            ToolTip.delay: 400
            ToolTip.text: qsTr("Meter en la cola una que llegó por el chat o que dijeron a viva voz")
        }
        GlassButton {
            compact: true
            text: qsTr("Limpiar")
            enabled: raiz.pub.recuento && (raiz.pub.recuento.respondidas + raiz.pub.recuento.descartadas) > 0
            onClicked: raiz.pub.moderar("limpiar")
            ToolTip.visible: hovered
            ToolTip.delay: 400
            ToolTip.text: qsTr("Saca las respondidas y las descartadas; no toca las que esperan")
        }
    }

    // El ListView va adentro de un Item para que el cartel de «no hay nada»
    // sea HERMANO y no hijo: un hijo directo de un ListView entra en su
    // contenido y se desplaza con la lista.
    Item {
        Layout.fillWidth: true
        Layout.fillHeight: true

    ListView {
        id: lista
        anchors.fill: parent
        clip: true
        spacing: 6
        model: raiz.pub.preguntas
        ScrollBar.vertical: ScrollBar {}

        delegate: Rectangle {
            id: fila
            required property var modelData
            width: ListView.view.width
            height: cuerpo.implicitHeight + 18
            radius: Theme.radiusSmall
            color: Qt.rgba(1, 1, 1, 0.04)
            // La que está al aire se ve de lejos: es la que se está
            // preguntando AHORA y no puede confundirse con las demás.
            border.color: fila.modelData.estado === "al_aire" ? Theme.live
                        : fila.modelData.estado === "elegida" ? Theme.ok
                                                              : Theme.glassBorder
            opacity: fila.modelData.estado === "descartada" ||
                     fila.modelData.estado === "respondida" ? 0.5 : 1.0

            RowLayout {
                id: cuerpo
                anchors { left: parent.left; right: parent.right; verticalCenter: parent.verticalCenter
                          leftMargin: 10; rightMargin: 10 }
                spacing: 10

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 2
                    Text {
                        Layout.fillWidth: true
                        // El texto lo escribe cualquiera con el enlace: se
                        // muestra como TEXTO y nunca como formato enriquecido.
                        textFormat: Text.PlainText
                        wrapMode: Text.WordWrap
                        text: fila.modelData.texto
                        color: Theme.text
                        font.pixelSize: 13
                    }
                    RowLayout {
                        spacing: 8
                        Text {
                            text: (fila.modelData.nombre || qsTr("Anónimo")) + " · " +
                                  raiz.origen(fila.modelData.deDonde) + " · " + qsTr("%n voto", "", fila.modelData.votos)
                            color: Theme.textFaint
                            font.pixelSize: 11
                        }
                        Text {
                            visible: fila.modelData.estado !== "nueva"
                            text: fila.modelData.estado === "al_aire" ? qsTr("AL AIRE")
                                : fila.modelData.estado === "elegida" ? qsTr("EN LA COLA")
                                : fila.modelData.estado === "respondida" ? qsTr("respondida")
                                                                         : qsTr("descartada")
                            color: fila.modelData.estado === "al_aire" ? Theme.live
                                 : fila.modelData.estado === "elegida" ? Theme.ok
                                                                       : Theme.textFaint
                            font { pixelSize: 10; weight: Font.DemiBold; letterSpacing: 0.5 }
                        }
                    }
                }

                GlassButton {
                    compact: true
                    visible: fila.modelData.estado === "nueva"
                    text: qsTr("A la cola")
                    onClicked: raiz.pub.moderar("elegir", fila.modelData.id)
                }
                GlassButton {
                    compact: true
                    visible: fila.modelData.estado === "elegida" || fila.modelData.estado === "nueva"
                    text: qsTr("Al aire")
                    onClicked: raiz.pub.moderar("al_aire", fila.modelData.id)
                }
                GlassButton {
                    compact: true
                    visible: fila.modelData.estado === "al_aire"
                    text: qsTr("Respondida")
                    onClicked: raiz.pub.moderar("responder", fila.modelData.id)
                }
                GlassButton {
                    compact: true
                    visible: fila.modelData.estado === "nueva" || fila.modelData.estado === "elegida"
                    text: "✕"
                    onClicked: raiz.pub.moderar("descartar", fila.modelData.id)
                    ToolTip.visible: hovered
                    ToolTip.delay: 400
                    ToolTip.text: qsTr("Descartarla: el que la mandó lo ve, los demás no")
                }
            }
        }

    }

        Text {
            anchors.centerIn: parent
            visible: lista.count === 0
            width: parent.width - 60
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
            color: Theme.textFaint
            font.pixelSize: 12
            text: raiz.pub.enLinea
                  ? qsTr("Todavía no preguntó nadie. El QR de la página del público es por donde entran.")
                  : qsTr("No puedo ver las preguntas: el servidor no responde.")
        }
    }

    }

    Dialog {
        id: sumarDlg
        anchors.centerIn: Overlay.overlay
        width: 460
        modal: true
        title: qsTr("Sumar una pregunta a la cola")
        standardButtons: Dialog.Ok | Dialog.Cancel
        onAboutToShow: { qTexto.text = ""; qNombre.text = "" }
        onAccepted: raiz.pub.sumar(qTexto.text, qNombre.text, qDonde.currentValue)
        ColumnLayout {
            spacing: 8
            Text {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                text: qsTr("Para lo que llegó por otro lado: el chat de la conferencia, el de una plataforma, o " +
                           "alguien que levantó la mano en el salón. Va a la MISMA cola que las demás.")
                color: Theme.textDim
                font.pixelSize: 12
            }
            TextArea {
                id: qTexto
                Layout.fillWidth: true
                Layout.preferredHeight: 70
                wrapMode: TextArea.Wrap
                color: Theme.text
                placeholderText: qsTr("La pregunta")
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                TextField {
                    id: qNombre
                    Layout.fillWidth: true
                    color: Theme.text
                    placeholderText: qsTr("Quién la hizo (opcional)")
                }
                ComboBox {
                    id: qDonde
                    model: raiz.origenes
                    textRole: "t"
                    valueRole: "v"
                }
            }
        }
    }
}
