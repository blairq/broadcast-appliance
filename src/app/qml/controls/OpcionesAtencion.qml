// La atención a quien se conecta (docs/funciones/ATENCION.md), la misma en Sala
// y en Asamblea: «Los atiende el asistente», «Hablando», el alta automática en
// Personas y quién entra solo si se lo reconoce. Fuente: Studio.atencion
// (ui/AtencionViewModel) y Studio.personas; por MCP, atencion_configurar.
//
// QUIÉN ATIENDE lo dice la fila de arriba: el rol de recepción si está
// prendido, si no el asistente de automatización (nunca el del evento). Y
// termina al hacerlo pasar: la atención es del lobby.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import BA

ColumnLayout {
    id: oa
    spacing: 8
    /// "sala" | "asamblea"
    property string ambito: "sala"
    /// false: solo el alta automática y quién entra solo (la página de Personas).
    property bool conAtencion: true
    readonly property var at: Studio.atencion
    readonly property bool enSala: ambito === "sala"
    readonly property bool atiende: !!at && (enSala ? at.salaAtiende : at.asambleaAtiende)

    // --- Los atiende el asistente ---------------------------------------------------
    RowLayout {
        Layout.fillWidth: true
        spacing: 8
        visible: oa.conAtencion
        Toggle {
            checked: oa.atiende
            // Apagarlo siempre se puede; prenderlo, si hay quién atienda.
            enabled: !!oa.at && (oa.atiende || oa.at.quienAtiende !== "")
            onToggled: (wanted) => { if (oa.enSala) oa.at.salaAtiende = wanted; else oa.at.asambleaAtiende = wanted }
        }
        ColumnLayout {
            spacing: 0
            Layout.fillWidth: true
            Layout.preferredWidth: 0
            Text { text: "✦ Los atiende el asistente"; font.pixelSize: 13
                   color: oa.at && oa.at.quienAtiende !== "" ? Theme.text : Theme.textFaint }
            Text {
                Layout.fillWidth: true
                Layout.preferredWidth: 0
                wrapMode: Text.WordWrap
                color: Theme.textFaint; font.pixelSize: 11
                text: !oa.at ? "Esta compilación no trae la atención (falta el motor de voz)."
                      : oa.at.quienAtiende === "" ? oa.at.quienAtiendeTexto
                      : !oa.atiende ? (oa.enSala ? "Cada uno que entra, lo atendés vos." : "Cada uno que espera, lo habilitás vos.")
                      : oa.at.quienAtiendeTexto + " " +
                        (oa.enSala ? "Les pide los datos y te los muestra juntos para que los hagas pasar; termina al admitirlos."
                                   : "Le mira la cara al que espera: si es quien dice y tiene permiso de entrar solo, lo habilita; si no, te pregunta.")
            }
        }
    }

    // --- Hablando -------------------------------------------------------------------
    RowLayout {
        Layout.fillWidth: true
        Layout.leftMargin: 20
        spacing: 8
        visible: oa.conAtencion && oa.atiende
        Toggle {
            checked: !!oa.at && oa.enSala && oa.at.salaHablando
            enabled: !!oa.at && oa.enSala && (oa.at.salaHablando || oa.at.puedeHablar)
            onToggled: (wanted) => oa.at.salaHablando = wanted
        }
        ColumnLayout {
            spacing: 0
            Layout.fillWidth: true
            Layout.preferredWidth: 0
            Text { text: "🎙 Hablando"; font.pixelSize: 13
                   color: !!oa.at && oa.enSala && oa.at.puedeHablar ? Theme.text : Theme.textFaint }
            Text {
                Layout.fillWidth: true
                Layout.preferredWidth: 0
                wrapMode: Text.WordWrap
                color: Theme.textFaint; font.pixelSize: 11
                text: !oa.at ? ""
                      : !oa.enSala ? oa.at.asambleaHablandoMotivo
                      : !oa.at.puedeHablar ? oa.at.hablarFalta
                      : oa.at.salaHablando
                        ? "Lo que dice pasa a texto y se le contesta con voz, solo a esa persona (nunca al aire). De a uno: " +
                          "los demás, por chat. La voz se elige en Configuración → Voz del sistema → Voz del atendedor."
                        : "Además del chat, se le habla: lo que dice pasa a texto y la respuesta le llega con voz."
            }
        }
    }

    // --- Alta automática en Personas ---------------------------------------------------
    RowLayout {
        Layout.fillWidth: true
        spacing: 8
        visible: Studio.personas.disponible
        Toggle {
            checked: Studio.personas.altaAutomatica
            onToggled: (wanted) => Studio.personas.altaAutomatica = wanted
        }
        ColumnLayout {
            spacing: 0
            Layout.fillWidth: true
            Layout.preferredWidth: 0
            Text { text: "☻ Alta automática en Personas"; color: Theme.text; font.pixelSize: 13 }
            Text {
                Layout.fillWidth: true
                Layout.preferredWidth: 0
                wrapMode: Text.WordWrap
                color: Theme.textFaint; font.pixelSize: 11
                text: "Anota en Personas, con su nombre y su cargo, a todo el que dio su nombre y tiene una cara que " +
                      "sirve para reconocerlo, sin esperar a que el asistente se acuerde." +
                      (oa.enSala && oa.atiende ? " Con la atención prendida, el lobby se mira siempre." : "")
            }
        }
    }

    // --- Quién entra solo -------------------------------------------------------------
    RowLayout {
        Layout.fillWidth: true
        spacing: 8
        Text {
            Layout.fillWidth: true
            color: Theme.textDim; font.pixelSize: 12
            text: lista.hayEvento ? "Entran solos si se los reconoce: " + lista.solos : "Entran solos: sin evento abierto"
        }
        GlassButton { compact: true; text: "Elegir…"; onClicked: dlgSolos.open() }
    }
    Dialog {
        id: dlgSolos
        parent: Overlay.overlay
        anchors.centerIn: parent
        modal: true
        title: "Quién entra solo si se lo reconoce"
        standardButtons: Dialog.Close
        width: 460
        ListaEntranSolos { id: lista; width: parent.width }
    }
}
