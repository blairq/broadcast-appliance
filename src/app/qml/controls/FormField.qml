// Campo de formulario GENERICO por tipo (solo presentacion).
//
// Lo usan la configuracion (SettingsModel) y las propiedades de fuentes
// (PropertiesModel): los dos describen cada opcion igual (kind, value,
// options, minimum, maximum, suffix). Este componente elige el control.
//
// kind: bool int float text textarea password secret path choice list color
//       font button info header
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import BA

ColumnLayout {
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

    spacing: 4
    opacity: enabled ? 1 : 0.45

    // Encabezado de grupo
    Text {
        visible: field.kind === "header"
        Layout.topMargin: 10
        text: field.label.toUpperCase()
        color: Theme.accentSoft
        font { family: Theme.font; pixelSize: 11; weight: Font.Bold; letterSpacing: 1.2 }
    }

    RowLayout {
        visible: field.kind !== "header"
        Layout.fillWidth: true
        spacing: 10

        Text {
            Layout.preferredWidth: 220
            Layout.alignment: Qt.AlignVCenter
            text: field.label + (field.modified ? "  •" : "")
            color: field.modified ? Theme.accentSoft : Theme.text
            wrapMode: Text.WordWrap
            font { family: Theme.font; pixelSize: 13 }
        }

        Loader {
            Layout.fillWidth: true
            sourceComponent: {
                switch (field.kind) {
                case "bool": return boolC
                case "int": case "float": return numC
                case "choice": case "list": return choiceC
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
            visible: field.badge === "gateway" || field.badge === "restart"
            radius: 8; height: 18; width: badgeText.implicitWidth + 12
            color: Qt.rgba(1, 1, 1, 0.08)
            Text {
                id: badgeText
                anchors.centerIn: parent
                text: field.badge === "gateway" ? "reinicia la sala al aplicar" : "al volver a abrir el programa"
                color: Theme.textFaint
                font.pixelSize: 10
            }
            MouseArea { id: badgeMa; anchors.fill: parent; hoverEnabled: true }
            ToolTip.visible: badgeMa.containsMouse; ToolTip.delay: 300
            ToolTip.text: field.badge === "gateway"
                ? "Al aplicar se reinicia solo el servidor de la sala (gateway), sin cerrar el programa: los invitados se cortan y vuelven a entrar. También se puede reiniciar a mano con «Reiniciar la sala» (acá o en la página Sala)."
                : "Rige la próxima vez que se abra el programa."
        }
    }
    Text {
        visible: field.help !== "" && field.kind !== "header" && field.kind !== "info"
        Layout.fillWidth: true
        Layout.leftMargin: 230
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
