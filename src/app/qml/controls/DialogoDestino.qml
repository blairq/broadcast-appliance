// Un destino adicional de la transmisión (respaldo, otra plataforma, otra cuenta):
// nombre, URL (RTMP, SRT o RIST), clave (se guarda cifrada) y si es de respaldo.
// Lo usan la tarjeta «Salidas» (los de la por omisión) y la página Salidas (los de
// cada salida: `salida`, su id; vacío = la por omisión). Guarda por
// Studio.guardarDestinoUi: el modelo valida y avisa.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import BA

Popup {
    id: dlg
    /// La salida de la que toma el programa ("" = la por omisión).
    property string salida: ""
    parent: Overlay.overlay
    anchors.centerIn: parent
    modal: true
    padding: 16
    width: 460
    background: Rectangle { color: Theme.panel; radius: Theme.radiusSmall; border.color: Theme.glassBorder }
    onOpened: { dNombre.text = ""; dUrl.text = ""; dClave.text = ""; dResp.checked = false; dModo.currentIndex = 0; dLat.value = 120 }
    ColumnLayout {
        anchors.fill: parent
        spacing: 8
        Text { text: qsTr("Destino adicional"); color: Theme.text; font { pixelSize: 15; weight: Font.DemiBold } }
        TextField { id: dNombre; Layout.fillWidth: true; placeholderText: qsTr("Nombre (p. ej. YouTube respaldo)") }
        TextField { id: dUrl; Layout.fillWidth: true; placeholderText: qsTr("rtmp://servidor/app · srt://servidor:9000 · rist://servidor:8000") }
        readonly property bool esSrt: dUrl.text.startsWith("srt://")
        TextField { id: dClave; Layout.fillWidth: true; echoMode: TextInput.Password
                    placeholderText: parent.esSrt ? qsTr("Passphrase SRT (10 a 79 caracteres; se guarda cifrada)") : qsTr("Clave (se guarda cifrada; el asistente no la ve)") }
        RowLayout {
            visible: parent.esSrt
            spacing: 8
            Text { text: qsTr("Modo"); color: Theme.textDim; font.pixelSize: 12 }
            ComboBox { id: dModo; model: ["caller", "listener", "rendezvous"]; Layout.preferredWidth: 130 }
            Text { text: qsTr("Latencia (ms)"); color: Theme.textDim; font.pixelSize: 12 }
            SpinBox { id: dLat; from: 20; to: 8000; stepSize: 20; value: 120; editable: true }
        }
        CheckBox { id: dResp; text: qsTr("Es de respaldo") }
        RowLayout {
            Item { Layout.fillWidth: true }
            GlassButton { compact: true; text: qsTr("Cancelar"); onClicked: dlg.close() }
            GlassButton {
                compact: true; active: true; text: qsTr("Guardar")
                onClicked: {
                    const d = { nombre: dNombre.text, url: dUrl.text, clave: dClave.text, respaldo: dResp.checked }
                    if (dUrl.text.startsWith("srt://")) { d.modo = dModo.currentText; d.latencia_ms = dLat.value }
                    if (dlg.salida !== "") d.salida = dlg.salida
                    const e = Studio.guardarDestinoUi(d)
                    if (e !== "") Studio.notify(e, "error"); else dlg.close()
                }
            }
        }
    }
}
