// Los aparatos emparejados del control web (dashboard /control/), con su perfil.
// Lo hace una PERSONA: cambiar el perfil (las categorías de herramientas que puede
// usar: «todo» o «escenas, audio, camaras») y quitar. Va en la fila «Control web»
// de Servicios; los que piden entrar se aprueban ahí mismo, arriba de esta lista
// (SolicitudesPendientes). Todo sale de Studio.controlWeb (control/ControlWeb): la
// misma lista que el MCP (control_web_aparatos), que solo puede achicar perfiles.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import BA

ColumnLayout {
    id: raiz
    readonly property var cw: Studio.controlWeb
    visible: cw && cw.aparatos.length > 0
    spacing: 8

    Text {
        text: "Emparejados"
        color: Theme.textFaint
        font.pixelSize: 11
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
