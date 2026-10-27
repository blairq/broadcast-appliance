// Un formulario para las propiedades de un elemento de GStreamer (o de lo que
// sea con el mismo formato: VideoGrafos.propiedadesModulo/propiedadesModuloCon):
// [{name, label, help, kind: bool|int|float|list|text|path|color|header|info,
//   value, min, max, step, options: [{value, label}]}].
// Avisa cada cambio con `cambio(nombre, valor)`; no guarda nada por su cuenta.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import BA

ColumnLayout {
    id: form
    property var props: []
    /// Solo estas (por nombre); vacío = todas.
    property var soloEstas: []
    signal cambio(string nombre, var valor)
    spacing: 6

    Text {
        visible: form.props.length === 0
        text: qsTr("Este elemento no tiene propiedades para ajustar.")
        color: Theme.textFaint; font.pixelSize: 11
    }
    Repeater {
        model: form.soloEstas.length > 0 ? form.props.filter(p => form.soloEstas.indexOf(p.name) >= 0) : form.props
        delegate: ColumnLayout {
            id: prop
            required property var modelData
            Layout.fillWidth: true
            spacing: 2
            Text {
                visible: prop.modelData.kind !== "bool"
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                text: prop.modelData.label + ((prop.modelData.kind === "float" || prop.modelData.kind === "int")
                      ? "  " + Number(desl.value).toFixed(prop.modelData.kind === "int" ? 0 : 2) : "")
                color: prop.modelData.kind === "header" ? Theme.text : Theme.textDim
                font.pixelSize: 12
                ToolTip.visible: ayuda.hovered && !!prop.modelData.help; ToolTip.delay: 400; ToolTip.text: prop.modelData.help || ""
                HoverHandler { id: ayuda }
            }
            Slider {
                id: desl
                visible: prop.modelData.kind === "float" || prop.modelData.kind === "int"
                Layout.fillWidth: true
                // Rangos enormes (enteros de 64 bits): se acota para que el deslizador sirva.
                from: prop.modelData.min !== undefined ? Math.max(prop.modelData.min, -1e7) : 0
                to: prop.modelData.max !== undefined ? Math.min(prop.modelData.max, 1e9) : 1
                stepSize: prop.modelData.step || 0
                value: typeof prop.modelData.value === "number" ? prop.modelData.value : 0
                onMoved: form.cambio(prop.modelData.name, prop.modelData.kind === "int" ? Math.round(value) : value)
            }
            CheckBox {
                visible: prop.modelData.kind === "bool"
                text: prop.modelData.label
                checked: prop.modelData.value === true
                onToggled: form.cambio(prop.modelData.name, checked)
            }
            ComboBox {
                visible: prop.modelData.kind === "list"
                Layout.fillWidth: true
                model: prop.modelData.options || []
                textRole: "label"
                currentIndex: {
                    const o = prop.modelData.options || []
                    for (let i = 0; i < o.length; ++i) if (o[i].value === prop.modelData.value) return i
                    return -1
                }
                onActivated: (i) => form.cambio(prop.modelData.name, prop.modelData.options[i].value)
            }
            TextField {
                visible: prop.modelData.kind === "text" || prop.modelData.kind === "path" || prop.modelData.kind === "color"
                Layout.fillWidth: true
                text: prop.modelData.value !== undefined ? String(prop.modelData.value) : ""
                onEditingFinished: if (text !== String(prop.modelData.value)) form.cambio(prop.modelData.name, text)
            }
        }
    }
}
