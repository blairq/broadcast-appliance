// Agregar una entrada de audio: elegir el micrófono o la entrada de la máquina
// (del registro, con el nombre que le puso el operador), un nombre si se
// quiere, y «Agregar».
//
// UNA SOLA FORMA DE CREAR UNA ENTRADA DE AUDIO. La usan la página Entradas de
// audio y el botón «+ Entrada de audio» de la página Audio. Antes Audio tenía
// su propio menú, con otra lista (sin el nombre técnico al lado, sin nombre
// propio para la entrada, sin el aviso de error): dos maneras de hacer lo
// mismo que se desacomodaban por separado.
//
// Todo sale de Studio.inputs (InputsViewModel): la lista de entradas se rehace
// sola cuando el modelo avisa.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import BA

ColumnLayout {
    id: agregar
    spacing: 8
    readonly property var inp: Studio.inputs
    property string error: ""
    /// Se agregó (con ese nombre): quien la usa puede elegirla o cerrarse.
    signal agregada(string nombre)

    Text { Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.textDim; font.pixelSize: 12
           text: "Micrófonos, líneas e interfaces de audio de esta máquina. Se escuchan en todas las escenas; su procesamiento se arma en la página Audio." }
    RowLayout {
        Layout.fillWidth: true
        ComboBox {
            id: micBox
            Layout.fillWidth: true
            model: agregar.inp.audioDevices
            textRole: "label"
            valueRole: "value"
        }
        GlassButton { compact: true; text: "↻"; onClicked: agregar.inp.refresh()
                      ToolTip.visible: hovered; ToolTip.text: "Buscar dispositivos de nuevo" }
    }
    // El nombre del sistema, si el operador le puso otro: para reconocerlo.
    Text {
        readonly property var elegido: micBox.currentIndex >= 0 ? agregar.inp.audioDevices[micBox.currentIndex] : null
        visible: !!elegido && elegido.propio
        text: elegido ? "Del sistema: " + elegido.tecnico : ""
        Layout.fillWidth: true; elide: Text.ElideRight
        color: Theme.textFaint; font.pixelSize: 11
    }
    Text { visible: agregar.inp.audioDevices.length === 0; text: "No se encontraron entradas de audio."; color: Theme.textFaint; font.pixelSize: 12 }
    RowLayout {
        Layout.fillWidth: true
        TextField { id: micName; Layout.fillWidth: true; placeholderText: "Nombre (opcional: si no, el del dispositivo)" }
        GlassButton {
            text: "Agregar"; active: true; enabled: micBox.currentIndex >= 0
            onClicked: {
                const nombre = micName.text.trim() !== "" ? micName.text.trim() : agregar.inp.audioInputName(micBox.currentValue)
                agregar.error = agregar.inp.addAudioInput(micBox.currentValue, micName.text)
                if (agregar.error !== "") return
                micName.text = ""
                agregar.agregada(nombre)
            }
        }
    }
    Text { visible: agregar.error !== ""; text: agregar.error; color: Theme.live; font.pixelSize: 12; Layout.fillWidth: true; wrapMode: Text.WordWrap }
    LinkDispositivos { Layout.fillWidth: true }
}
