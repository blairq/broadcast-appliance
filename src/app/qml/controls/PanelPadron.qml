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
    /// Lo pone en true VentanaSuelta (ver PanelPreguntas).
    property bool enVentana: false
    readonly property var r: ev.resumenPadron
    // El papel y el canal se guardan con su clave (en español); acá, la etiqueta.
    readonly property var papeles: [ { t: qsTr("público"), v: "publico" }, { t: qsTr("orador"), v: "orador" },
                                     { t: qsTr("panelista"), v: "panelista" }, { t: qsTr("prensa"), v: "prensa" },
                                     { t: qsTr("staff"), v: "staff" } ]
    readonly property var canales: [ { t: qsTr("Sin definir"), v: "sin_definir" }, { t: qsTr("Presencial"), v: "presencial" },
                                     { t: qsTr("En línea"), v: "online" } ]
    function etiqueta(lista, v) { const o = lista.find(x => x.v === v); return o ? o.t : v }

    ColumnLayout {
        anchors.fill: parent
        spacing: 8

        // Hay quórum (reglas del cuerpo: quorum_regla) — grande, se lee de lejos.
        Rectangle {
            readonly property var q: Studio.quorum.estado
            visible: !!q.configurado
            Layout.fillWidth: true
            implicitHeight: 46
            radius: 10
            color: q.hay_quorum ? Qt.rgba(0.18, 0.8, 0.44, 0.18) : Qt.rgba(1, 0.23, 0.19, 0.18)
            border.color: q.hay_quorum ? Theme.ok : Theme.live
            Text {
                anchors.centerIn: parent
                text: (parent.q.hay_quorum ? qsTr("HAY QUÓRUM · %1 de %2 necesarios (%3 miembros)")
                                           : qsTr("SIN QUÓRUM · %1 de %2 necesarios (%3 miembros)"))
                      .arg(parent.q.presentes).arg(parent.q.necesarios).arg(parent.q.miembros)
                color: Theme.text
                font.pixelSize: 17
                font.weight: Font.Black
            }
        }
        RowLayout {
            Layout.fillWidth: true
            spacing: 6
            Text { text: qsTr("Miembros"); color: Theme.textDim; font.pixelSize: 11 }
            SpinBox {
                id: qMiembros; from: 0; to: 1000; editable: true; Layout.preferredWidth: 100
                value: Studio.quorum.estado.miembros || 0
            }
            ComboBox {
                id: qMayoria
                implicitWidth: 200
                model: [ { t: qsTr("Mayoría simple"), v: "simple" }, { t: qsTr("Mayoría absoluta"), v: "absoluta" },
                         { t: qsTr("2/3 de presentes"), v: "dos_tercios_presentes" }, { t: qsTr("2/3 de miembros"), v: "dos_tercios_miembros" } ]
                textRole: "t"
                currentIndex: Math.max(0, model.findIndex(o => o.v === Studio.quorum.estado.mayoria))
            }
            GlassButton {
                compact: true; text: qsTr("Guardar reglas")
                onClicked: {
                    const e = Studio.quorum.setReglaUi({ miembros: qMiembros.value, minimo: "mitad_mas_uno", mayoria: qMayoria.model[qMayoria.currentIndex].v })
                    if (e !== "") Studio.notify(e, "error")
                }
                ToolTip.visible: hovered; ToolTip.delay: 400
                ToolTip.text: qsTr("Quórum: la mitad más uno de los miembros. La mayoría es la de fábrica para el resultado oficial.")
            }
            Item { Layout.fillWidth: true }
        }

        // El quórum arriba y con sus dos números, que es como se lee.
        RowLayout {
            Layout.fillWidth: true
            spacing: 14
            Text {
                text: qsTr("%1 de %2 presentes").arg(raiz.r.presentes).arg(raiz.r.total)
                color: Theme.text
                font { pixelSize: 14; weight: Font.DemiBold }
            }
            Text {
                text: qsTr("%1 en el lugar · %2 en línea").arg(raiz.r.presenciales).arg(raiz.r.enLinea)
                      + (raiz.r.sinDato > 0 ? " · " + qsTr("%1 sin dato").arg(raiz.r.sinDato) : "")
                color: Theme.textDim
                font.pixelSize: 12
            }
            Text {
                visible: raiz.r.conVoto > 0
                text: qsTr("quórum %1/%2").arg(raiz.r.votoPresente).arg(raiz.r.conVoto)
                color: Theme.ok
                font { pixelSize: 12; weight: Font.DemiBold }
            }
            Item { Layout.fillWidth: true }
            GlassButton {
                compact: true
                visible: !raiz.enVentana
                text: "⧉"
                onClicked: ApplicationWindow.window.sacarPanel("padron", qsTr("Padrón"))
                ToolTip.visible: hovered
                ToolTip.delay: 400
                ToolTip.text: qsTr("Sacar a su propia ventana, para el mostrador de acreditación")
            }
            GlassButton { compact: true; text: qsTr("Anotar"); onClicked: altaDlg.open() }
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
                                text: fila.modelData.nombre || qsTr("(sin nombre)")
                                color: Theme.text
                                font.pixelSize: 13
                                elide: Text.ElideRight
                                Layout.fillWidth: true
                            }
                            Text {
                                text: raiz.etiqueta(raiz.papeles, fila.modelData.papel) + " · " +
                                      (fila.modelData.estado === "llego" && fila.modelData.porDonde !== ""
                                       ? qsTr("llegó por %1").arg(fila.modelData.porDonde)
                                       : fila.modelData.canal === "sin_definir"
                                         ? qsTr("sin definir") : raiz.etiqueta(raiz.canales, fila.modelData.canal)) +
                                      (fila.modelData.vota ? " · " + qsTr("vota") : "") +
                                      (fila.modelData.dejarPasarSolo ? " · " + qsTr("entra solo") : "")
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
                            text: fila.modelData.estado === "llego" ? qsTr("Se fue") : qsTr("Llegó")
                            onClicked: fila.modelData.estado === "llego"
                                       ? raiz.ev.despedir(fila.modelData.id)
                                       : raiz.ev.acreditar(fila.modelData.id, "mostrador")
                        }
                        GlassButton {
                            compact: true
                            text: "✕"
                            onClicked: ApplicationWindow.window.confirm({
                                title: qsTr("Sacar del padrón"),
                                text: qsTr("Se saca a «%1» de la lista de este evento. No toca su ficha de rostro.")
                                      .arg(fila.modelData.nombre || qsTr("esta persona")),
                                accept: qsTr("Sacar"), reject: qsTr("Dejarlo"),
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
                      ? qsTr("El padrón vive en el evento: abrí uno primero.")
                      : qsTr("Todavía no hay nadie anotado. Sin padrón no hay quórum, y sin quórum lo " +
                             "que se decida no vale.")
            }
        }
    }

    Dialog {
        id: altaDlg
        anchors.centerIn: Overlay.overlay
        width: 460
        modal: true
        title: qsTr("Anotar a alguien")
        standardButtons: Dialog.Ok | Dialog.Cancel
        onAboutToShow: { aNombre.text = ""; aNota.text = ""; aVota.checked = false }
        onAccepted: raiz.ev.anotar({
            nombre: aNombre.text.trim(),
            papel: aPapel.currentValue,
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
                placeholderText: qsTr("Nombre y apellido")
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                ComboBox {
                    id: aPapel
                    model: raiz.papeles
                    textRole: "t"
                    valueRole: "v"
                    Layout.preferredWidth: 140
                }
                ComboBox {
                    id: aCanal
                    model: raiz.canales
                    textRole: "t"
                    valueRole: "v"
                    Layout.preferredWidth: 140
                }
                Toggle { id: aVota }
                Text { text: qsTr("Vota"); color: Theme.text; font.pixelSize: 12 }
                Item { Layout.fillWidth: true }
            }
            TextField {
                id: aNota
                Layout.fillWidth: true
                color: Theme.text
                placeholderText: qsTr("Nota (por qué está: «panelista», «entra por la puerta lateral»)")
            }
            Text {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                text: qsTr("«Vota» no sale del papel: en una asamblea un propietario es público y vota, y " +
                           "la prensa no. Sin esto no hay quórum.")
                color: Theme.textFaint
                font.pixelSize: 11
            }
        }
    }
}
