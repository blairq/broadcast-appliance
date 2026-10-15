// Los aparatos del control web (dashboard /control/): los que piden entrar y los
// emparejados, con su perfil. Lo hace una PERSONA: aprobar, cambiar el perfil
// (las categorías de herramientas que puede usar: «todo» o «escenas, audio, camaras»)
// y quitar. Todo sale de Studio.controlWeb (control/ControlWeb): la misma lista que
// el MCP (control_web_aparatos), que solo puede achicar perfiles.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import BA

ColumnLayout {
    id: raiz
    readonly property var cw: Studio.controlWeb
    visible: cw && (cw.solicitudes.length > 0 || cw.aparatos.length > 0)
    spacing: 8

    Text {
        text: "CONTROL WEB · APARATOS"
        color: Theme.textDim
        font { pixelSize: 11; weight: Font.DemiBold; letterSpacing: 1.2 }
    }

    // Los que esperan a una persona.
    Repeater {
        model: raiz.cw ? raiz.cw.solicitudes : []
        delegate: Panel {
            id: sol
            required property var modelData
            Layout.fillWidth: true
            padding: 12
            implicitHeight: filaSol.implicitHeight + 2 * padding
            RowLayout {
                id: filaSol
                anchors.fill: parent
                spacing: 10
                Text {
                    Layout.fillWidth: true
                    text: "«" + sol.modelData.nombre + "» (" + sol.modelData.ip + ") pide entrar al control web"
                    color: Theme.amber; font.pixelSize: 14; wrapMode: Text.WordWrap
                }
                Text { text: "Perfil"; color: Theme.textDim; font.pixelSize: 12 }
                TextField {
                    id: perfilSol
                    Layout.preferredWidth: 200
                    text: sol.modelData.perfil
                    placeholderText: "todo, o: escenas, audio"
                    color: Theme.text
                    font.pixelSize: 12
                }
                GlassButton {
                    compact: true; text: "Aprobar"; active: true
                    onClicked: {
                        const e = raiz.cw.aprobarUi(sol.modelData.id, true, perfilSol.text)
                        Studio.notify(e !== "" ? e : "Aparato emparejado.", e !== "" ? "error" : "ok")
                    }
                }
                GlassButton {
                    compact: true; text: "Rechazar"
                    onClicked: raiz.cw.aprobarUi(sol.modelData.id, false, "")
                }
            }
        }
    }

    // Los emparejados.
    Repeater {
        model: raiz.cw ? raiz.cw.aparatos : []
        delegate: Panel {
            id: ap
            required property var modelData
            Layout.fillWidth: true
            padding: 12
            implicitHeight: filaAp.implicitHeight + 2 * padding
            RowLayout {
                id: filaAp
                anchors.fill: parent
                spacing: 10
                Rectangle {
                    width: 10; height: 10; radius: 5
                    color: ap.modelData.conectado ? Theme.ok : Theme.textFaint
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 2
                    Text { text: ap.modelData.nombre; color: Theme.text; font { pixelSize: 13; weight: Font.DemiBold } }
                    Text {
                        text: (ap.modelData.conectado ? "Conectado" : "Sin conectar") +
                              (ap.modelData.ip ? " · " + ap.modelData.ip : "")
                        color: Theme.textDim; font.pixelSize: 11
                    }
                }
                Text { text: "Perfil"; color: Theme.textDim; font.pixelSize: 12 }
                TextField {
                    id: perfilAp
                    Layout.preferredWidth: 240
                    text: ap.modelData.perfil
                    placeholderText: "todo, o: escenas, audio"
                    color: Theme.text
                    font.pixelSize: 12
                }
                GlassButton {
                    compact: true; text: "Guardar"
                    enabled: perfilAp.text !== ap.modelData.perfil
                    onClicked: {
                        const e = raiz.cw.perfilUi(ap.modelData.id, perfilAp.text)
                        Studio.notify(e !== "" ? e : "Perfil guardado.", e !== "" ? "error" : "ok")
                    }
                }
                GlassButton {
                    compact: true; text: "Quitar"
                    onClicked: raiz.cw.quitarUi(ap.modelData.id)
                }
            }
        }
    }
}
