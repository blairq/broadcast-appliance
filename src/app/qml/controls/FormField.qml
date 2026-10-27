// Campo de formulario GENERICO por tipo (solo presentacion).
//
// Lo usan la configuracion (SettingsModel) y las propiedades de fuentes
// (PropertiesModel): los dos describen cada opcion igual (kind, value,
// options, minimum, maximum, suffix). Este componente elige el control.
//
// kind: bool int float text textarea password secret path choice list color
//       font button info header combo (se escribe, o se elige de la lista)
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts // los controles de una línea (numC, comboC…)
import BA

// SIN LAYOUTS ANIDADOS: la etiqueta, el control, el aviso y la ayuda se ubican con
// bindings (x, y, width) y el alto sale de una expresión. Con un ColumnLayout que
// tenía un RowLayout y textos que se parten adentro de otros layouts (los de
// Configuración), el alto que depende del ancho necesitaba una pasada de acomodo por
// nivel y Qt cortaba a las dos («Layout polish loop»).
Item {
    id: field
    property string kind: "text"
    property string label: ""
    property var value
    property var options: []
    property real minimum: 0
    property real maximum: 100
    property real step: 1
    property string suffix: ""
    property string help: ""
    property bool modified: false
    property string badge: "" // "gateway", "restart"
    signal edited(var v)
    signal pressed()

    /// Lo que ocupa la etiqueta (y la sangría de la ayuda): 220, o menos en un panel
    /// angosto (las propiedades de una fuente), que el control no quede apretado.
    readonly property int anchoEtiqueta: Math.min(220, Math.round(width * 0.4))
    readonly property int separacion: 10

    implicitWidth: 550 // fijo: anchoEtiqueta depende del ancho (con él, binding loop)
    implicitHeight: kind === "header" ? encabezado.y + encabezado.height
                  : ayuda.visible ? ayuda.y + ayuda.height : fila.height
    opacity: enabled ? 1 : 0.45

    // Encabezado de grupo
    Text {
        id: encabezado
        visible: field.kind === "header"
        y: 10
        width: field.width
        text: field.label.toUpperCase()
        color: Theme.accentSoft
        elide: Text.ElideRight
        font { family: Theme.font; pixelSize: 11; weight: Font.Bold; letterSpacing: 1.2 }
    }

    // Etiqueta | control | aviso, centrados en el alto del más alto.
    Item {
        id: fila
        visible: field.kind !== "header"
        width: field.width
        height: Math.max(etiqueta.height, control.height, insignia.visible ? insignia.height : 0)

        Text {
            id: etiqueta
            width: field.anchoEtiqueta
            anchors.verticalCenter: parent.verticalCenter
            text: field.label + (field.modified ? "  •" : "")
            color: field.modified ? Theme.accentSoft : Theme.text
            wrapMode: Text.WordWrap
            font { family: Theme.font; pixelSize: 13 }
        }

        Loader {
            id: control
            x: field.anchoEtiqueta + field.separacion
            // Solo el ancho: el alto es el del control (su implicitHeight).
            width: Math.max(0, fila.width - x - (insignia.visible ? insignia.width + field.separacion : 0))
            anchors.verticalCenter: parent.verticalCenter
            sourceComponent: {
                switch (field.kind) {
                case "bool": return boolC
                case "int": case "float": return numC
                case "choice": case "list": return choiceC
                case "combo": return comboC
                case "color": return colorC
                case "font": return fontC
                case "button": return buttonC
                case "info": return infoC
                case "textarea": return areaC
                default: return textC
                }
            }
        }

        Rectangle {
            id: insignia
            visible: field.badge === "gateway" || field.badge === "restart"
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            radius: 8; height: 18; width: badgeText.implicitWidth + 12
            color: Qt.rgba(1, 1, 1, 0.08)
            Text {
                id: badgeText
                anchors.centerIn: parent
                text: field.badge === "gateway" ? "reinicia el servidor al aplicar" : "al volver a abrir el programa"
                color: Theme.textFaint
                font.pixelSize: 10
            }
            MouseArea { id: badgeMa; anchors.fill: parent; hoverEnabled: true }
            ToolTip.visible: badgeMa.containsMouse; ToolTip.delay: 300
            ToolTip.text: field.badge === "gateway"
                ? "Al aplicar se reinicia solo el servidor (gateway: la conferencia, la sesión remota, la botonera web, las pantallas…), sin cerrar el programa: lo que está conectado se corta y vuelve a entrar. También se puede reiniciar a mano con «Reiniciar el servidor» (acá) o «Reiniciar la conferencia» (en la página Conferencia)."
                : "Rige la próxima vez que se abra el programa."
        }
    }

    Text {
        id: ayuda
        visible: field.help !== "" && field.kind !== "header" && field.kind !== "info"
        x: field.anchoEtiqueta + field.separacion
        y: fila.height + 4
        width: Math.max(0, field.width - x)
        text: field.help
        color: Theme.textFaint
        wrapMode: Text.WordWrap
        font { family: Theme.font; pixelSize: 11 }
    }

    Component {
        id: boolC
        Item {
            implicitHeight: 28
            // `checked` sigue al modelo: los dos (SettingsModel y PropertiesModel)
            // reflejan el valor editado al instante, asi que el interruptor
            // muestra el valor pendiente y vuelve solo con "Revertir".
            Toggle {
                anchors.verticalCenter: parent.verticalCenter
                checked: !!field.value
                enabled: field.enabled
                onToggled: (wanted) => field.edited(wanted)
            }
        }
    }
    Component {
        id: numC
        RowLayout {
            spacing: 8
            SpinBox {
                id: spin
                readonly property real factor: field.kind === "float" ? 100 : 1
                from: Math.round(field.minimum * factor)
                to: Math.round((field.maximum > field.minimum ? field.maximum : 1e9) * factor)
                stepSize: Math.max(1, Math.round(field.step * factor))
                value: Math.round(Number(field.value) * factor)
                editable: true
                enabled: field.enabled
                Layout.preferredWidth: 150
                textFromValue: (v) => field.kind === "float" ? (v / 100).toFixed(2) : String(v)
                valueFromText: (t) => Math.round(Number(String(t).replace(",", ".")) * factor)
                // El IntValidator por defecto rechaza "0.50" y el texto entra en
                // un binding loop: validador acorde al formato.
                validator: RegularExpressionValidator {
                    regularExpression: field.kind === "float" ? /-?\d*[.,]?\d{0,2}/ : /-?\d*/
                }
                onValueModified: field.edited(field.kind === "float" ? value / 100 : value)
            }
            Text { text: field.suffix; color: Theme.textDim; font.pixelSize: 12 }
            Item { Layout.fillWidth: true }
        }
    }
    Component {
        id: choiceC
        Item {
            implicitHeight: combo.implicitHeight
            ComboBox {
                id: combo
                width: Math.min(420, parent.width)
                textRole: "label"
                valueRole: "value"
                model: field.options
                enabled: field.enabled
                Component.onCompleted: currentIndex = indexOfValue(field.value)
                onActivated: field.edited(currentValue)
                Connections {
                    target: field
                    function onValueChanged() { combo.currentIndex = combo.indexOfValue(field.value) }
                }
            }
        }
    }
    // Se escribe a mano o se elige de la lista (lo elegido queda escrito): una dirección,
    // un nombre de equipo… Vacío = el primero de la lista (normalmente «automático»).
    Component {
        id: comboC
        RowLayout {
            spacing: 6
            TextField {
                id: escrito
                Layout.preferredWidth: 260
                text: field.value !== undefined && field.value !== null ? String(field.value) : ""
                placeholderText: field.options.length > 0 ? String(field.options[0].label) : ""
                enabled: field.enabled
                onEditingFinished: if (text !== String(field.value || "")) field.edited(text.trim())
            }
            ComboBox {
                id: lista
                Layout.preferredWidth: 160
                textRole: "label"
                valueRole: "value"
                model: field.options
                enabled: field.enabled
                displayText: "Elegir…"
                onActivated: { escrito.text = String(currentValue); field.edited(currentValue) }
            }
            Item { Layout.fillWidth: true }
        }
    }
    Component {
        id: textC
        TextField {
            text: field.value !== undefined ? String(field.value) : ""
            echoMode: field.kind === "password" || field.kind === "secret" ? TextInput.Password : TextInput.Normal
            enabled: field.enabled
            placeholderText: field.kind === "path" ? "/ruta/al/archivo" : ""
            onEditingFinished: if (text !== String(field.value)) field.edited(text)
        }
    }
    Component {
        id: areaC
        TextArea {
            text: field.value !== undefined ? String(field.value) : ""
            enabled: field.enabled
            wrapMode: TextEdit.Wrap
            onEditingFinished: if (text !== String(field.value)) field.edited(text)
        }
    }
    Component {
        id: colorC
        RowLayout {
            spacing: 8
            Rectangle { width: 26; height: 26; radius: 6; color: field.value || "#ffffff"; border.color: Theme.glassBorder }
            TextField {
                text: field.value || "#ffffff"
                Layout.preferredWidth: 110
                enabled: field.enabled
                onEditingFinished: field.edited(text)
            }
        }
    }
    Component {
        id: fontC
        RowLayout {
            spacing: 8
            TextField {
                text: field.value ? field.value.face : "Sans"
                Layout.preferredWidth: 160
                onEditingFinished: field.edited({ face: text, size: field.value ? field.value.size : 48 })
            }
            SpinBox {
                from: 6; to: 512; editable: true
                value: field.value ? field.value.size : 48
                onValueModified: field.edited({ face: field.value ? field.value.face : "Sans", size: value })
            }
        }
    }
    Component {
        id: buttonC
        GlassButton { text: field.label; compact: true; onClicked: field.pressed() }
    }
    Component {
        id: infoC
        Text { text: String(field.value || ""); color: Theme.textDim; wrapMode: Text.WordWrap; font.pixelSize: 12 }
    }
}
