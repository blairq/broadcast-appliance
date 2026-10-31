// UN campo de un formulario armado por esquema: el único motor de campos de la app
// (las propiedades de un nodo de video y de los pads de una mezcla, la configuración de
// un plugin, los parámetros de la acción de un equipo, los controles de texto y opción
// del panel de un plugin). El esquema lo arma el modelo (C++), ya con las opciones
// resueltas; acá solo se dibuja:
//   {name, label, help, kind, value, min, max, step, suffix, placeholder,
//    options: [{value, label}], obligatorio}
// kind: bool | int | float (deslizador) | number (se escribe) | list | text | path |
//       color | clave (oculta) | json | header | info.
// Avisa con `cambio(valor)`: número en int/float/number, sí/no en bool, el `value` de la
// opción en list, y el texto tal cual en los demás (un json lo valida quien guarda).
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import BA

ColumnLayout {
    id: campo
    property var c: ({})
    /// Para acotar los rangos enormes (enteros de 64 bits, posiciones): el deslizador sirve.
    property real piso: -1e7
    property real tope: 1e9
    /// Con el nombre arriba (o a un costado, si `etiquetaAlCostado`).
    property bool etiquetaAlCostado: false
    /// Sin el nombre (quien lo usa ya lo puso al lado).
    property bool conEtiqueta: true
    property int tamano: 12
    /// Avisa en cada tecla (un borrador que se manda con un botón aparte) y no solo al
    /// terminar de escribir (lo que se aplica en vivo).
    property bool alEscribir: false
    signal cambio(var valor)
    spacing: 2

    readonly property string kind: c.kind || "text"
    readonly property bool deslizador: kind === "float" || kind === "int"
    readonly property bool escrito: ["text", "path", "color", "clave", "json", "number"].indexOf(kind) >= 0
    readonly property string valorTexto: c.value === undefined || c.value === null ? ""
                                        : kind === "json" && typeof c.value !== "string" ? JSON.stringify(c.value) : String(c.value)

    Text {
        visible: campo.conEtiqueta && campo.kind !== "bool" && !campo.etiquetaAlCostado
        Layout.fillWidth: true
        wrapMode: Text.WordWrap
        text: (campo.c.label || campo.c.name || "") + (campo.c.obligatorio ? " *" : "")
              + (campo.deslizador ? "  " + Number(desl.value).toFixed(campo.kind === "int" ? 0 : 2) + (campo.c.suffix || "") : "")
        color: campo.kind === "header" ? Theme.text : campo.kind === "info" ? Theme.textFaint : Theme.textDim
        font.pixelSize: campo.kind === "header" ? campo.tamano + 1 : campo.tamano
        ToolTip.visible: ayuda.hovered && !!campo.c.help; ToolTip.delay: 400; ToolTip.text: campo.c.help || ""
        HoverHandler { id: ayuda }
    }
    RowLayout {
        Layout.fillWidth: true
        spacing: 8
        Text {
            visible: campo.conEtiqueta && campo.etiquetaAlCostado && campo.kind !== "bool"
            Layout.preferredWidth: 150
            wrapMode: Text.WordWrap
            text: (campo.c.label || campo.c.name || "") + (campo.c.obligatorio ? " *" : "")
            color: Theme.textDim; font.pixelSize: campo.tamano
        }
        Slider {
            id: desl
            visible: campo.deslizador
            Layout.fillWidth: true
            from: campo.c.min !== undefined ? Math.max(campo.c.min, campo.piso) : 0
            to: campo.c.max !== undefined ? Math.min(campo.c.max, campo.tope) : 1
            stepSize: campo.c.step || 0
            value: typeof campo.c.value === "number" ? campo.c.value : 0
            onMoved: campo.cambio(campo.kind === "int" ? Math.round(value) : value)
        }
        CheckBox {
            visible: campo.kind === "bool"
            text: campo.c.label || campo.c.name || ""
            checked: campo.c.value === true
            onToggled: campo.cambio(checked)
        }
        ComboBox {
            id: combo
            visible: campo.kind === "list"
            Layout.fillWidth: true
            model: campo.c.options || []
            textRole: "label"
            currentIndex: (campo.c.options || []).findIndex(o => o.value === campo.c.value)
            // Uno que ya no está (una escena borrada, una cámara desenchufada): se ve igual.
            displayText: currentIndex >= 0 ? currentText
                         : campo.valorTexto !== "" ? qsTr("%1 (no está)").arg(campo.valorTexto) : qsTr("Elegir…")
            onActivated: (i) => campo.cambio(campo.c.options[i].value)
        }
        TextField {
            visible: campo.escrito
            Layout.fillWidth: true
            color: Theme.text
            font { pixelSize: campo.tamano; family: campo.kind === "json" ? Theme.mono : Theme.font }
            echoMode: campo.kind === "clave" ? TextInput.Password : TextInput.Normal
            validator: campo.kind === "number" ? numeros : null
            DoubleValidator { id: numeros; locale: "C" }
            placeholderText: campo.c.placeholder !== undefined ? campo.c.placeholder
                             : campo.kind === "path" ? qsTr("Ruta del archivo") : campo.kind === "color" ? "#RRGGBB" : (campo.c.help || "")
            // Lo del modelo, salvo mientras se escribe.
            Binding on text { when: !activeFocus; value: campo.valorTexto }
            onTextEdited: if (campo.alEscribir) campo.cambio(campo.kind === "number" ? Number(text) : text)
            onEditingFinished: if (!campo.alEscribir && text !== campo.valorTexto) campo.cambio(campo.kind === "number" ? Number(text) : text)
            background: Rectangle { color: Theme.field; radius: 6; border.color: parent.activeFocus ? Theme.accent : "transparent" }
        }
    }
}
