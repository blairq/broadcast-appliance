// El disco en apuros (Studio.apurosDiscoUi): cuando lo grabado espera para entrar al
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
    property var d: ({})
    readonly property bool apuros: !!d.apuros
    readonly property bool prioridad: !!d.prioridad
    visible: apuros || prioridad
    implicitHeight: visible ? col.implicitHeight + 16 : 0
    radius: 8
    color: Qt.tint(Theme.panel, Qt.rgba(Theme.live.r, Theme.live.g, Theme.live.b, 0.18)) // opaco: flota sobre la página
    border.color: Theme.live
    border.width: 1

    Timer {
        // Mostrándose, cada segundo; si no, alcanza con enterarse a los pocos segundos.
        interval: a.visible ? 1000 : 4000; running: true; repeat: true; triggeredOnStart: true
        onTriggered: a.d = Studio.apurosDiscoUi()
    }

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
                if (a.prioridad)
                    return "El disco es de la grabación del programa" + (a.d.automatica ? " (se hizo solo)" : "") +
                           ". En pausa: " + (a.d.pausadas || []).join(", ") + "."
                const atraso = a.d.atraso_s || 0
                return "El disco no da abasto: " + (atraso >= 1 ? "lo grabado espera " + Math.round(atraso) + " s para entrar"
                                                                : "el registro no puede escribir") +
                       (a.d.desde_s > 0 ? " (hace " + a.d.desde_s + " s)" : "") + "."
            }
        }
        Text {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            color: Theme.textDim
            font.pixelSize: 12
            text: a.prioridad
                  ? (a.apuros ? "Cuando el disco ande bien 30 s seguidos, todo sigue solo (las ISO en el mismo archivo, con el salto)."
                              : "El disco ya anda bien: en unos segundos sigue todo.")
                  : (a.d.graba_programa
                     ? "Para no perder la grabación del programa, pausá lo demás que escribe al disco: las ISO, la repetición y el registro."
                     : "No se está grabando el programa. Si el disco es externo por USB, probá otro puerto o cable.")
        }
        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            GlassButton {
                visible: !a.prioridad && !!a.d.graba_programa
                compact: true
                active: true
                activeColor: Theme.live
                text: "Darle el disco al programa"
                onClicked: { Studio.priorizarProgramaUi(true); a.d = Studio.apurosDiscoUi() }
            }
            GlassButton {
                visible: a.prioridad
                compact: true
                text: "Seguir con todo ya"
                onClicked: { Studio.priorizarProgramaUi(false); a.d = Studio.apurosDiscoUi() }
            }
            Item { Layout.fillWidth: true }
            CheckBox {
                text: "Hacerlo solo"
                checked: a.d.modo === "auto"
                onToggled: { Studio.modoDiscoUi(checked ? "auto" : "preguntar"); a.d = Studio.apurosDiscoUi() }
                ToolTip.visible: hovered; ToolTip.delay: 400
                ToolTip.text: "La próxima vez que el disco no dé abasto grabando el programa, pausar lo demás sin preguntar"
            }
        }
    }
}
