// El disco en apuros (Studio.emision.apurosDisco, de la foto del motor): cuando lo grabado espera para entrar al
// disco o el registro no puede escribir. Ofrece darle el disco a la grabación
// del programa: se pausan las ISO, la repetición, la copia (si va al mismo
// disco) y la señal limpia a disco, y el registro se junta en memoria. Cuando
// el disco anda bien de nuevo (30 s), todo sigue solo.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import BA

Rectangle {
    id: a
    readonly property var d: Studio.emision.apurosDisco
    readonly property bool apuros: !!d.apuros
    readonly property bool prioridad: !!d.prioridad
    visible: apuros || prioridad
    implicitHeight: visible ? col.implicitHeight + 16 : 0
    radius: 8
    color: Qt.tint(Theme.panel, Qt.rgba(Theme.live.r, Theme.live.g, Theme.live.b, 0.18)) // opaco: flota sobre la página
    border.color: Theme.live
    border.width: 1


    ColumnLayout {
        id: col
        anchors { left: parent.left; right: parent.right; top: parent.top; margins: 8 }
        spacing: 6
        Text {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            color: Theme.text
            font.pixelSize: 13
            font.weight: Font.DemiBold
            text: {
                if (a.prioridad) {
                    const pausadas = (a.d.pausadas || []).join(", ")
                    return a.d.automatica ? qsTr("El disco es de la grabación del programa (se hizo solo). En pausa: %1.").arg(pausadas)
                                          : qsTr("El disco es de la grabación del programa. En pausa: %1.").arg(pausadas)
                }
                const atraso = a.d.atraso_s || 0
                const desde = a.d.desde_s > 0
                if (atraso >= 1)
                    return desde ? qsTr("El disco no da abasto: lo grabado espera %1 s para entrar (hace %2 s).").arg(Math.round(atraso)).arg(a.d.desde_s)
                                 : qsTr("El disco no da abasto: lo grabado espera %1 s para entrar.").arg(Math.round(atraso))
                return desde ? qsTr("El disco no da abasto: el registro no puede escribir (hace %1 s).").arg(a.d.desde_s)
                             : qsTr("El disco no da abasto: el registro no puede escribir.")
            }
        }
        Text {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            color: Theme.textDim
            font.pixelSize: 12
            text: a.prioridad
                  ? (a.apuros ? qsTr("Cuando el disco ande bien 30 s seguidos, todo sigue solo (las ISO en el mismo archivo, con el salto).")
                              : qsTr("El disco ya anda bien: en unos segundos sigue todo."))
                  : (a.d.graba_programa
                     ? qsTr("Para no perder la grabación del programa, pausá lo demás que escribe al disco: las ISO, la repetición y el registro.")
                     : qsTr("No se está grabando el programa. Si el disco es externo por USB, probá otro puerto o cable."))
        }
        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            GlassButton {
                visible: !a.prioridad && !!a.d.graba_programa
                compact: true
                active: true
                activeColor: Theme.live
                text: qsTr("Darle el disco al programa")
                onClicked: Studio.emision.priorizarProgramaUi(true)
            }
            GlassButton {
                visible: a.prioridad
                compact: true
                text: qsTr("Seguir con todo ya")
                onClicked: Studio.emision.priorizarProgramaUi(false)
            }
            Item { Layout.fillWidth: true }
            CheckBox {
                text: qsTr("Hacerlo solo")
                checked: a.d.modo === "auto"
                onToggled: Studio.emision.modoDiscoUi(checked ? "auto" : "preguntar")
                ToolTip.visible: hovered; ToolTip.delay: 400
                ToolTip.text: qsTr("La próxima vez que el disco no dé abasto grabando el programa, pausar lo demás sin preguntar")
            }
        }
    }
}
