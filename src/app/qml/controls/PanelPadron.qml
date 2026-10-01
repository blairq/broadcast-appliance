// El padrón del evento: quién viene, quién llegó y por dónde.
//
// Es UNA SOLA LISTA para los dos mundos —el que entra por la puerta y el que
// entra por la sala— y eso es lo que hace que «¿quiénes están?» tenga quien lo
// conteste en un evento híbrido.
//
// El código de cuatro símbolos es el de votante: se le dice al acreditarlo, y
// en una votación formal es lo que identifica, porque el teléfono no alcanza
// (se borra el sitio y se vota de nuevo).
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import BA
import "."

Item {
    id: raiz

    readonly property var ev: Studio.evento
    /// Lo pone en true PanelWindow (ver PanelPreguntas).
    property bool enVentana: false
    readonly property var r: ev.resumenPadron

    ColumnLayout {
        anchors.fill: parent
        spacing: 8

        // El quórum arriba y con sus dos números, que es como se lee.
        RowLayout {
            Layout.fillWidth: true
            spacing: 14
            Text {
                text: raiz.r.presentes + " de " + raiz.r.total + " presentes"
                color: Theme.text
                font { pixelSize: 14; weight: Font.DemiBold }
            }
            Text {
                text: raiz.r.presenciales + " en el lugar · " + raiz.r.enLinea + " en línea"
                color: Theme.textDim
                font.pixelSize: 12
            }
            Text {
                visible: raiz.r.conVoto > 0
                text: "quórum " + raiz.r.votoPresente + "/" + raiz.r.conVoto
                color: Theme.ok
                font { pixelSize: 12; weight: Font.DemiBold }
            }
            Item { Layout.fillWidth: true }
            GlassButton {
                compact: true
                visible: !raiz.enVentana
                text: "⧉"
                onClicked: ApplicationWindow.window.sacarPanel("padron", "Padrón")
                ToolTip.visible: hovered
                ToolTip.delay: 400
                ToolTip.text: "Sacar a su propia ventana, para el mostrador de acreditación"
            }
            GlassButton { compact: true; text: "Anotar"; onClicked: altaDlg.open() }
        }

        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            ListView {
                id: lista
                anchors.fill: parent
                clip: true
                spacing: 4
                model: raiz.ev.padron
                ScrollBar.vertical: ScrollBar {}

                delegate: Rectangle {
                    id: fila
                    required property var modelData
                    width: ListView.view.width
                    height: 46
                    radius: Theme.radiusSmall
                    color: Qt.rgba(1, 1, 1, 0.04)
                    border.color: fila.modelData.estado === "llego" ? Theme.ok : Theme.glassBorder

                    RowLayout {
                        anchors { fill: parent; leftMargin: 10; rightMargin: 10 }
                        spacing: 10

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 0
                            Text {
                                textFormat: Text.PlainText
                                text: fila.modelData.nombre || "(sin nombre)"
                                color: Theme.text
                                font.pixelSize: 13
                                elide: Text.ElideRight
                                Layout.fillWidth: true
                            }
                            Text {
                                text: fila.modelData.papel + " · " +
                                      (fila.modelData.estado === "llego" && fila.modelData.porDonde !== ""
                                       ? "llegó por " + fila.modelData.porDonde
                                       : fila.modelData.canal === "sin_definir"
                                         ? "sin definir" : fila.modelData.canal) +
                                      (fila.modelData.vota ? " · vota" : "") +
                                      (fila.modelData.dejarPasarSolo ? " · entra solo" : "")
                                color: Theme.textFaint
                                font.pixelSize: 11
                            }
                        }

                        // El código, en monoespaciada: se dicta en voz alta y
                        // hay que poder leerlo de un vistazo.
                        Text {
                            text: fila.modelData.codigo
                            color: Theme.textDim
                            font { family: Theme.mono; pixelSize: 13; letterSpacing: 1 }
                        }
                        GlassButton {
                            compact: true
                            text: fila.modelData.estado === "llego" ? "Se fue" : "Llegó"
                            onClicked: fila.modelData.estado === "llego"
                                       ? raiz.ev.despedir(fila.modelData.id)
                                       : raiz.ev.acreditar(fila.modelData.id, "mostrador")
                        }
                        GlassButton {
                            compact: true
                            text: "✕"
                            onClicked: ApplicationWindow.window.confirm({
                                title: "Sacar del padrón",
                                text: "Se saca a «" + (fila.modelData.nombre || "esta persona") +
                                      "» de la lista de este evento. No toca su ficha de rostro.",
                                accept: "Sacar", reject: "Dejarlo",
                                action: () => raiz.ev.quitarAsistente(fila.modelData.id)
                            })
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
                text: raiz.ev.abierto === ""
                      ? "El padrón vive en el evento: abrí uno primero."
                      : "Todavía no hay nadie anotado. Sin padrón no hay quórum, y sin quórum lo " +
                        "que se decida no vale."
            }
        }
    }

    Dialog {
        id: altaDlg
        anchors.centerIn: Overlay.overlay
        width: 460
        modal: true
        title: "Anotar a alguien"
        standardButtons: Dialog.Ok | Dialog.Cancel
        onAboutToShow: { aNombre.text = ""; aNota.text = ""; aVota.checked = false }
        onAccepted: raiz.ev.anotar({
            nombre: aNombre.text.trim(),
            papel: aPapel.currentText,
            canal: aCanal.currentValue,
            nota: aNota.text.trim(),
            vota: aVota.checked
        })
        ColumnLayout {
            spacing: 8
            TextField {
                id: aNombre
                Layout.fillWidth: true
                color: Theme.text
                placeholderText: "Nombre y apellido"
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                ComboBox {
                    id: aPapel
                    model: ["publico", "orador", "panelista", "prensa", "staff"]
                    Layout.preferredWidth: 140
                }
                ComboBox {
                    id: aCanal
                    model: [
                        { t: "Sin definir", v: "sin_definir" },
                        { t: "Presencial", v: "presencial" },
                        { t: "En línea", v: "online" }
                    ]
                    textRole: "t"
                    valueRole: "v"
                    Layout.preferredWidth: 140
                }
                Toggle { id: aVota }
                Text { text: "Vota"; color: Theme.text; font.pixelSize: 12 }
                Item { Layout.fillWidth: true }
            }
            TextField {
                id: aNota
                Layout.fillWidth: true
                color: Theme.text
                placeholderText: "Nota (por qué está: «panelista», «entra por la puerta lateral»)"
            }
            Text {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                text: "«Vota» no sale del papel: en una asamblea un propietario es público y vota, y " +
                      "la prensa no. Sin esto no hay quórum."
                color: Theme.textFaint
                font.pixelSize: 11
            }
        }
    }
}
