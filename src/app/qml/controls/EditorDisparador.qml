// Crear un disparador sin pedírselo al asistente: el tipo (de alert_types), sus
// parámetros (un campo por parámetro, con lo que significa), el sonido, si
// queda como alarma, a quién avisar afuera (notificadores) y una acción
// opcional (una herramienta con sus argumentos).
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import BA

Popup {
    id: ed
    modal: true
    width: 560
    height: Math.min(parent ? parent.height - 40 : 700, 720)
    padding: 14
    anchors.centerIn: Overlay.overlay
    readonly property var ag: Studio.agent
    property var tipos: []
    property var tipo: tipos.length > 0 ? tipos[Math.max(0, tipoBox.currentIndex)] : ({})
    property var valores: ({})
    onOpened: {
        tipos = ag.alertTypesUi()
        valores = ({})
        nombre.text = ""
        accionHerr.editText = ""
        accionArgs.text = ""
    }
    background: Rectangle { color: Theme.bg; border.color: Theme.glassBorder; radius: 12 }

    function convertir(v) {
        const t = String(v).trim()
        if (t === "true") return true
        if (t === "false") return false
        if (t !== "" && !isNaN(Number(t))) return Number(t)
        return t
    }

    contentItem: ColumnLayout {
        spacing: 8
        Text {
            text: qsTr("Nuevo disparador")
            color: Theme.text
            font.pixelSize: 16
            font.weight: Font.DemiBold
        }
        ComboBox {
            id: tipoBox
            Layout.fillWidth: true
            model: ed.tipos
            textRole: "summary"
            onActivated: ed.valores = ({})
        }
        TextField { id: nombre; Layout.fillWidth: true; placeholderText: qsTr("Nombre (si no, el del tipo)"); color: Theme.text }
        ScrollView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            ColumnLayout {
                width: parent.width
                spacing: 6
                Repeater {
                    model: Object.keys(ed.tipo.params || {})
                    delegate: ColumnLayout {
                        required property string modelData
                        Layout.fillWidth: true
                        spacing: 2
                        Text { text: modelData; color: Theme.textDim; font.pixelSize: 11; font.family: Theme.mono }
                        TextField {
                            Layout.fillWidth: true
                            color: Theme.text
                            placeholderText: String(ed.tipo.params[modelData])
                            onTextEdited: { const v = Object.assign({}, ed.valores); v[modelData] = text; ed.valores = v }
                        }
                    }
                }
                Text { visible: Object.keys(ed.tipo.params || {}).length === 0; text: qsTr("Este tipo no tiene parámetros."); color: Theme.textFaint; font.pixelSize: 11 }
                RowLayout {
                    Text { text: qsTr("Sonido"); color: Theme.textDim; font.pixelSize: 12 }
                    // La clave (lo que se guarda) va aparte de la etiqueta (lo que se lee).
                    ComboBox {
                        id: sonido; textRole: "t"; valueRole: "v"
                        model: [{ v: "ninguno", t: qsTr("ninguno") }, { v: "aviso", t: qsTr("aviso") }, { v: "critico", t: qsTr("crítico") }]
                    }
                    CheckBox { id: reconocer; text: qsTr("Queda como alarma hasta que alguien la reconoce") }
                }
                Text { text: qsTr("Avisar afuera"); color: Theme.textDim; font.pixelSize: 12; visible: Studio.notificador.lista.length > 0 }
                Flow {
                    Layout.fillWidth: true
                    spacing: 6
                    Repeater {
                        id: notifRep
                        model: Studio.notificador.lista
                        delegate: CheckBox {
                            required property var modelData
                            text: modelData.nombre + " (" + modelData.tipo + ")"
                        }
                    }
                }
                Text { text: qsTr("Acción al dispararse (opcional)"); color: Theme.textDim; font.pixelSize: 12 }
                ComboBox { id: accionHerr; Layout.fillWidth: true; editable: true; model: Studio.botonera.herramientasUi("") }
                TextField { id: accionArgs; Layout.fillWidth: true; color: Theme.text; placeholderText: qsTr("Argumentos en JSON: {\"name\": \"Placa\"}") }
            }
        }
        RowLayout {
            GlassButton {
                compact: true; text: qsTr("Crear"); active: true
                onClicked: {
                    const params = {}
                    for (const k in ed.valores) if (String(ed.valores[k]).trim() !== "") params[k] = ed.convertir(ed.valores[k])
                    const notificar = []
                    for (let i = 0; i < notifRep.count; ++i) if (notifRep.itemAt(i).checked) notificar.push(Studio.notificador.lista[i].id)
                    const spec = { type: ed.tipo.type, params: params, sonido: sonido.currentValue, reconocer: reconocer.checked, notificar: notificar }
                    if (nombre.text.trim() !== "") spec.name = nombre.text.trim()
                    if (accionHerr.editText.trim() !== "") {
                        let args = {}
                        try { if (accionArgs.text.trim() !== "") args = JSON.parse(accionArgs.text) }
                        catch (e) { Studio.notify(qsTr("Los argumentos no son JSON válido"), "error"); return }
                        spec.actions = [{ tool: accionHerr.editText.trim(), arguments: args }]
                    }
                    const e = ed.ag.createAlertUi(spec)
                    if (e !== "") Studio.notify(e, "error"); else { Studio.notify(qsTr("Disparador creado"), "ok"); ed.close() }
                }
            }
            Item { Layout.fillWidth: true }
            GlassButton { compact: true; text: qsTr("Cancelar"); onClicked: ed.close() }
        }
    }
}
