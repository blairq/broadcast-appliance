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
        text: qsTr("Emparejados")
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
                        text: (ap.modelData.conectado ? qsTr("Conectado") : qsTr("Sin conectar")) +
                              (ap.modelData.ip ? " · " + ap.modelData.ip : "")
                        color: Theme.textDim; font.pixelSize: 11
                    }
                }
                // La salida que maneja (con varias); el aparato también la cambia desde su pantalla.
                ComboBox {
                    Layout.preferredWidth: 160
                    visible: raiz.cw.salidas.length > 1
                    model: raiz.cw.salidas
                    textRole: "nombre"
                    currentIndex: Math.max(0, raiz.cw.salidas.findIndex(s => s.id === ap.modelData.salida || (!ap.modelData.salida && s.por_omision)))
                    onActivated: (i) => {
                        const s = raiz.cw.salidas[i]
                        const e = raiz.cw.salidaUi(ap.modelData.id, s.por_omision ? "" : s.id)
                        if (e !== "") Studio.notify(e, "error")
                    }
                }
                Text { text: qsTr("Perfil"); color: Theme.textDim; font.pixelSize: 12 }
                TextField {
                    id: perfilAp
                    Layout.preferredWidth: 240
                    text: ap.modelData.perfil
                    // «todo», «escenas», «audio» son las claves del perfil (se escriben así): no se traducen.
                    placeholderText: qsTr("%1, o: %2").arg("todo").arg("escenas, audio")
                    color: Theme.text
                    font.pixelSize: 12
                }
                GlassButton {
                    compact: true; text: qsTr("Guardar")
                    enabled: perfilAp.text !== ap.modelData.perfil
                    onClicked: {
                        const e = raiz.cw.perfilUi(ap.modelData.id, perfilAp.text)
                        Studio.notify(e !== "" ? e : qsTr("Perfil guardado."), e !== "" ? "error" : "ok")
                    }
                }
                GlassButton {
                    compact: true; text: qsTr("Quitar")
                    onClicked: raiz.cw.quitarUi(ap.modelData.id)
                }
            }
        }
    }
}
