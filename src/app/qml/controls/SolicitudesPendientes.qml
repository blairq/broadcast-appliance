// Los pedidos que esperan a una persona: un aparato (o alguien) pide entrar y se
// aprueba o rechaza acá mismo. Lo usan todos los servicios que piden aprobación
// por aparato (la tablet de la botonera, el control web, Companion Satellite, el
// lobby de invitados), en Servicios y en sus páginas.
//
// No sabe de ningún servicio: muestra la lista que le dan (enlazada a una
// propiedad del modelo que avisa) y emite `resolver`; quien lo usa llama a su
// modelo. Cada pedido: {id, nombre, ip, ms (cuándo pidió; 0 = no se sabe),
// perfil (el propuesto), detalle}.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import BA

ColumnLayout {
    id: raiz
    /// [{id, nombre, ip, ms, perfil, detalle}]
    property var solicitudes: []
    /// Cómo se elige el perfil: "" (no hay), "lista" (de `perfiles`) o "texto".
    property string perfilModo: ""
    /// En "lista": [{id, nombre}].
    property var perfiles: []
    /// En "texto": la ayuda del campo.
    property string perfilAyuda: ""
    property string textoAprobar: "Aprobar"
    /// Cuántos se muestran; el resto, en una línea («y N más»).
    property int maximo: 6
    /// Una persona aprobó o rechazó (perfil: el elegido, "" si no hay).
    signal resolver(string id, bool aprobar, string perfil)

    visible: solicitudes.length > 0
    spacing: 6

    function hace(ms, ahora) {
        if (!ms) return ""
        const s = Math.max(0, Math.round((ahora - ms) / 1000))
        if (s < 60) return "recién"
        const m = Math.round(s / 60)
        return m < 60 ? "hace " + m + " min" : "hace " + Math.floor(m / 60) + " h " + (m % 60) + " min"
    }

    // Para que «hace N min» avance mientras se mira.
    Timer {
        id: reloj
        property double ahora: Date.now()
        interval: 15000; repeat: true; triggeredOnStart: true
        running: raiz.visible
        onTriggered: ahora = Date.now()
    }

    Repeater {
        model: raiz.solicitudes.slice(0, raiz.maximo)
        delegate: Rectangle {
            id: sol
            required property var modelData
            Layout.fillWidth: true
            implicitHeight: filaSol.implicitHeight + 16
            radius: 8
            color: Qt.rgba(0.96, 0.72, 0.24, 0.08)
            border.color: Qt.rgba(0.96, 0.72, 0.24, 0.35)

            RowLayout {
                id: filaSol
                anchors { left: parent.left; right: parent.right; verticalCenter: parent.verticalCenter; margins: 10 }
                spacing: 10
                Rectangle { width: 8; height: 8; radius: 4; color: Theme.amber }
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 1
                    Text {
                        Layout.fillWidth: true
                        text: "«" + (sol.modelData.nombre || "Sin nombre") + "»" +
                              (sol.modelData.detalle ? " " + sol.modelData.detalle : "")
                        color: Theme.amber
                        font.pixelSize: 13
                        wrapMode: Text.WordWrap
                    }
                    Text {
                        readonly property var partes: [sol.modelData.ip || "", raiz.hace(sol.modelData.ms, reloj.ahora)]
                                                      .filter(t => t !== "")
                        visible: partes.length > 0
                        text: partes.join(" · ")
                        color: Theme.textFaint
                        font { pixelSize: 11; family: Theme.mono }
                    }
                }
                Text {
                    visible: raiz.perfilModo !== ""
                    text: "Perfil"
                    color: Theme.textDim
                    font.pixelSize: 12
                }
                ComboBox {
                    id: perfilLista
                    visible: raiz.perfilModo === "lista"
                    Layout.preferredWidth: 200
                    model: raiz.perfiles
                    textRole: "nombre"
                    valueRole: "id"
                    // El propuesto (el del código), mientras la persona no elija otro.
                    Component.onCompleted: {
                        const i = raiz.perfiles.findIndex(p => p.id === sol.modelData.perfil)
                        currentIndex = i >= 0 ? i : (raiz.perfiles.length > 0 ? 0 : -1)
                    }
                }
                TextField {
                    id: perfilTexto
                    visible: raiz.perfilModo === "texto"
                    Layout.preferredWidth: 200
                    text: sol.modelData.perfil || ""
                    placeholderText: raiz.perfilAyuda
                    color: Theme.text
                    font.pixelSize: 12
                }
                GlassButton {
                    compact: true; text: raiz.textoAprobar; active: true
                    enabled: raiz.perfilModo !== "lista" || perfilLista.currentIndex >= 0
                    onClicked: raiz.resolver(sol.modelData.id, true,
                                             raiz.perfilModo === "lista" ? (perfilLista.currentValue || "")
                                             : raiz.perfilModo === "texto" ? perfilTexto.text : "")
                }
                GlassButton {
                    compact: true; text: "Rechazar"
                    onClicked: raiz.resolver(sol.modelData.id, false, "")
                }
            }
        }
    }
    Text {
        visible: raiz.solicitudes.length > raiz.maximo
        text: "y " + (raiz.solicitudes.length - raiz.maximo) + " más esperando"
        color: Theme.textFaint
        font.pixelSize: 11
    }
}
