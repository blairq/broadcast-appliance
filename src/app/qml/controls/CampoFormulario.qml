// UN campo de un formulario armado por esquema: el único motor de campos de la app
// (la configuración, las propiedades de una fuente, las de un nodo de video y de los pads
// de una mezcla, la configuración de un plugin, los parámetros de la acción de un equipo,
// el control de una cámara PTZ, los controles de texto y opción del panel de un plugin).
// El esquema lo arma el modelo (C++, ui/Formulario.hpp), ya con las opciones resueltas;
// acá solo se dibuja:
//   {name, label, help, kind, value, min, max, step, decimales, suffix, placeholder,
//    options: [{value, label}], obligatorio, modified, badge, enabled}
// kind: bool | int | float (deslizador) | number (se escribe) | list | combo (se escribe o
//       se elige) | text | textarea | path | color | font | clave (oculta) | json |
//       button | header | info.
// Avisa con `cambio(valor)`: número en int/float/number ("" si se borró un number), sí/no
// en bool, el `value` de la opción en list, {face, size} en font, y el texto tal cual en
// los demás (un json lo valida quien guarda); un button avisa con `apretado()`.
//
// SIN LAYOUTS ANIDADOS: la etiqueta, el control, la insignia y la ayuda se ubican con
// bindings (x, y, width) y el alto sale de una expresión. Con un ColumnLayout que tenía un
// RowLayout y textos que se parten adentro de otros layouts (los de Configuración), el
// alto que depende del ancho necesitaba una pasada de acomodo por nivel y Qt cortaba a las
// dos («Layout polish loop»).
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts // los controles de una línea (número, color, letra)
import BA

Item {
    id: campo
    property var c: ({})
    /// Para acotar los rangos enormes (enteros de 64 bits, posiciones): el deslizador sirve.
    property real piso: -1e7
    property real tope: 1e9
    /// Con el nombre a un costado (los formularios anchos: Configuración) y no arriba.
    property bool etiquetaAlCostado: false
    /// Sin el nombre (quien lo usa ya lo puso al lado).
    property bool conEtiqueta: true
    /// La ayuda escrita abajo del control (y no en el globito del nombre).
    property bool ayudaAbajo: false
    property int tamano: 12
    /// Avisa en cada tecla (un borrador que se manda con un botón aparte) y no solo al
    /// terminar de escribir (lo que se aplica en vivo).
    property bool alEscribir: false
    signal cambio(var valor)
    signal apretado()

    readonly property string kind: c.kind || "text"
    readonly property bool deslizador: kind === "float" || kind === "int"
    readonly property string etiqueta: (c.label || c.name || "") + (c.obligatorio ? " *" : "")
    readonly property bool modificado: c.modified === true
    readonly property string valorTexto: c.value === undefined || c.value === null ? ""
                                        : kind === "json" && typeof c.value !== "string" ? JSON.stringify(c.value) : String(c.value)
    /// Lo que ocupa la etiqueta al costado (y la sangría de la ayuda): 220, o menos en un
    /// panel angosto, que el control no quede apretado.
    readonly property int anchoEtiqueta: campo.etiquetaAlCostado && campo.conEtiqueta ? Math.min(220, Math.round(width * 0.4)) : 0
    readonly property int separacion: anchoEtiqueta > 0 ? 10 : 0
    /// El nombre va adentro del control (la casilla) o es el control (el botón).
    readonly property bool nombreEnControl: kind === "button" || (kind === "bool" && !etiquetaAlCostado)

    enabled: c.enabled !== false
    opacity: enabled ? 1 : 0.45
    implicitWidth: 300 // fijo: anchoEtiqueta depende del ancho (con él, binding loop)
    implicitHeight: kind === "header" ? encabezado.y + encabezado.height
                  : ayuda.visible ? ayuda.y + ayuda.height : fila.y + fila.height

    Text {
        id: encabezado
        visible: campo.kind === "header"
        y: 8
        width: campo.width
        text: campo.etiqueta.toUpperCase()
        color: Theme.accentSoft
        elide: Text.ElideRight
        font { family: Theme.font; pixelSize: 11; weight: Font.Bold; letterSpacing: 1.2 }
    }

    // El nombre arriba (con el valor del deslizador).
    Text {
        id: arriba
        visible: campo.kind !== "header" && campo.conEtiqueta && !campo.etiquetaAlCostado && !campo.nombreEnControl
        width: campo.width
        height: visible ? implicitHeight : 0
        wrapMode: Text.WordWrap
        text: campo.etiqueta + (campo.modificado ? "  •" : "")
              + (campo.deslizador && control.item ? "  " + Number(control.item.value).toFixed(campo.kind === "int" ? 0 : 2) + (campo.c.suffix || "") : "")
        color: campo.modificado ? Theme.accentSoft : campo.kind === "info" ? Theme.textFaint : Theme.textDim
        font.pixelSize: campo.tamano
        ToolTip.visible: globo.hovered && !campo.ayudaAbajo && !!campo.c.help; ToolTip.delay: 400; ToolTip.text: campo.c.help || ""
        HoverHandler { id: globo }
    }

    // Nombre al costado | control | insignia, centrados en el alto del más alto.
    Item {
        id: fila
        visible: campo.kind !== "header"
        y: arriba.visible ? arriba.height + 2 : 0
        width: campo.width
        height: visible ? Math.max(costado.visible ? costado.height : 0, control.height, insignia.visible ? insignia.height : 0) : 0

        Text {
            id: costado
            visible: campo.anchoEtiqueta > 0 && !campo.nombreEnControl
            width: campo.anchoEtiqueta
            anchors.verticalCenter: parent.verticalCenter
            text: campo.etiqueta + (campo.modificado ? "  •" : "")
                  + (campo.deslizador && control.item ? "  " + Number(control.item.value).toFixed(campo.kind === "int" ? 0 : 2) + (campo.c.suffix || "") : "")
            color: campo.modificado ? Theme.accentSoft : Theme.text
            wrapMode: Text.WordWrap
            font { family: Theme.font; pixelSize: campo.tamano + 1 }
            ToolTip.visible: globoCostado.hovered && !campo.ayudaAbajo && !!campo.c.help; ToolTip.delay: 400; ToolTip.text: campo.c.help || ""
            HoverHandler { id: globoCostado }
        }

        Loader {
            id: control
            x: campo.anchoEtiqueta + campo.separacion
            // Solo el ancho: el alto es el del control (su implicitHeight).
            width: Math.max(0, fila.width - x - (insignia.visible ? insignia.width + 10 : 0))
            anchors.verticalCenter: parent.verticalCenter
            sourceComponent: {
                switch (campo.kind) {
                case "header": return null
                case "bool": return campo.etiquetaAlCostado ? interruptorC : casillaC
                case "int": case "float": return deslizadorC
                case "number": return numeroC
                case "list": return listaC
                case "combo": return comboC
                case "color": return colorC
                case "font": return letraC
                case "button": return botonC
                case "info": return infoC
                case "textarea": return areaC
                default: return textoC
                }
            }
        }

        Rectangle {
            id: insignia
            visible: campo.c.badge === "gateway" || campo.c.badge === "motor" || campo.c.badge === "restart"
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            radius: 8; height: 18; width: textoInsignia.implicitWidth + 12
            color: Qt.rgba(1, 1, 1, 0.08)
            Text {
                id: textoInsignia
                anchors.centerIn: parent
                text: campo.c.badge === "gateway" ? qsTr("reinicia el servidor al aplicar")
                      : campo.c.badge === "motor" ? qsTr("reinicia el motor al aplicar") : qsTr("al volver a abrir el programa")
                color: campo.c.badge === "motor" ? Theme.amber : Theme.textFaint
                font.pixelSize: 10
            }
            HoverHandler { id: globoInsignia }
            ToolTip.visible: globoInsignia.hovered; ToolTip.delay: 300
            ToolTip.text: campo.c.badge === "gateway"
                ? qsTr("Al aplicar se reinicia solo el servidor (gateway: la conferencia, la sesión remota, la botonera web, las pantallas…), sin cerrar el programa: lo que está conectado se corta y vuelve a entrar. También se puede reiniciar a mano con «Reiniciar el servidor» (acá) o «Reiniciar la conferencia» (en la página Conferencia).")
                : campo.c.badge === "motor"
                ? qsTr("Al aplicar se rearma el motor de video con todo lo que tenía: por unos segundos se corta todo lo que sale (el programa, la sala, las pantallas, lo que ven los invitados) y las cámaras se vuelven a abrir. Con una transmisión o una grabación andando no se aplica: queda para cuando terminen.")
                : qsTr("Rige la próxima vez que se abra el programa.")
        }
    }

    Text {
        id: ayuda
        visible: campo.ayudaAbajo && !!campo.c.help && campo.kind !== "header" && campo.kind !== "info"
        x: campo.anchoEtiqueta + campo.separacion
        y: fila.y + fila.height + 4
        width: Math.max(0, campo.width - x)
        text: campo.c.help || ""
        color: Theme.textFaint
        wrapMode: Text.WordWrap
        font { family: Theme.font; pixelSize: campo.tamano - 1 }
    }

    // --- Los controles, uno por kind ------------------------------------------------------
    Component {
        id: deslizadorC
        Slider {
            from: campo.c.min !== undefined ? Math.max(campo.c.min, campo.piso) : 0
            to: campo.c.max !== undefined ? Math.min(campo.c.max, campo.tope) : 1
            stepSize: campo.c.step || 0
            value: typeof campo.c.value === "number" ? campo.c.value : 0
            onMoved: campo.cambio(campo.kind === "int" ? Math.round(value) : value)
        }
    }
    Component {
        id: casillaC
        CheckBox {
            text: campo.etiqueta + (campo.modificado ? "  •" : "")
            checked: campo.c.value === true
            onToggled: campo.cambio(checked)
            ToolTip.visible: hovered && !campo.ayudaAbajo && !!campo.c.help; ToolTip.delay: 400; ToolTip.text: campo.c.help || ""
        }
    }
    Component {
        id: interruptorC
        Item {
            implicitHeight: 28
            // Sigue al modelo: muestra el valor pedido y vuelve solo si el modelo lo revierte.
            Toggle {
                anchors.verticalCenter: parent.verticalCenter
                checked: campo.c.value === true
                enabled: campo.enabled
                onToggled: (wanted) => campo.cambio(wanted)
            }
        }
    }
    Component {
        id: listaC
        Item {
            implicitHeight: combo.implicitHeight
            ComboBox {
                id: combo
                width: Math.min(420, parent.width)
                model: campo.c.options || []
                textRole: "label"
                currentIndex: (campo.c.options || []).findIndex(o => o.value === campo.c.value)
                // Uno que ya no está (una escena borrada, una cámara desenchufada): se ve igual.
                displayText: currentIndex >= 0 ? currentText
                             : campo.valorTexto !== "" ? qsTr("%1 (no está)").arg(campo.valorTexto) : qsTr("Elegir…")
                onActivated: (i) => campo.cambio(campo.c.options[i].value)
            }
        }
    }
    // Se escribe a mano o se elige de la lista (lo elegido queda escrito): una dirección,
    // un nombre de equipo… Vacío = el primero de la lista (normalmente «automático»).
    Component {
        id: comboC
        RowLayout {
            spacing: 6
            Escrito {
                cf: campo
                Layout.preferredWidth: 260
                placeholderText: (campo.c.options || []).length > 0 ? String(campo.c.options[0].label) : ""
                onListo: (t) => campo.cambio(t.trim())
            }
            ComboBox {
                Layout.preferredWidth: 160
                model: campo.c.options || []
                textRole: "label"
                displayText: qsTr("Elegir…")
                onActivated: (i) => campo.cambio(campo.c.options[i].value)
            }
            Item { Layout.fillWidth: true }
        }
    }
    Component {
        id: numeroC
        RowLayout {
            spacing: 8
            Escrito {
                cf: campo
                Layout.fillWidth: true
                Layout.maximumWidth: 200
                validator: DoubleValidator {
                    locale: "C"
                    notation: DoubleValidator.StandardNotation
                    decimals: campo.c.decimales !== undefined ? campo.c.decimales : 1000
                    bottom: campo.c.min !== undefined && campo.c.max > campo.c.min ? campo.c.min : -Infinity
                    top: campo.c.min !== undefined && campo.c.max > campo.c.min ? campo.c.max : Infinity
                }
                // Vacío = «no se manda» (lo decide quien guarda, ver Formulario::convertir).
                onListo: (t) => campo.cambio(t.trim() === "" ? "" : Number(t))
            }
            Text { visible: !!campo.c.suffix; text: campo.c.suffix || ""; color: Theme.textDim; font.pixelSize: campo.tamano }
            Item { Layout.fillWidth: true }
        }
    }
    Component {
        id: colorC
        RowLayout {
            spacing: 8
            Rectangle { Layout.preferredWidth: 26; Layout.preferredHeight: 26; radius: 6; color: campo.valorTexto || "#ffffff"; border.color: Theme.glassBorder }
            Escrito { cf: campo; Layout.preferredWidth: 110; placeholderText: "#RRGGBB"; onListo: (t) => campo.cambio(t) }
            Item { Layout.fillWidth: true }
        }
    }
    Component {
        id: letraC
        RowLayout {
            spacing: 8
            readonly property var letra: campo.c.value || ({ face: "Sans", size: 48 })
            TextField {
                Layout.preferredWidth: 160
                text: parent.letra.face || "Sans"
                onEditingFinished: if (text !== parent.letra.face) campo.cambio({ face: text, size: parent.letra.size || 48 })
            }
            SpinBox {
                from: 6; to: 512; editable: true
                value: parent.letra.size || 48
                onValueModified: campo.cambio({ face: parent.letra.face || "Sans", size: value })
            }
            Item { Layout.fillWidth: true }
        }
    }
    Component {
        id: botonC
        Item {
            implicitHeight: 30
            GlassButton {
                compact: true
                text: campo.etiqueta
                onClicked: campo.apretado()
                ToolTip.visible: hovered && !!campo.c.help; ToolTip.delay: 400; ToolTip.text: campo.c.help || ""
            }
        }
    }
    Component {
        id: infoC
        Text {
            text: campo.valorTexto
            height: text === "" ? 0 : implicitHeight
            color: Theme.textDim; wrapMode: Text.WordWrap; font.pixelSize: campo.tamano
        }
    }
    Component {
        id: areaC
        TextArea {
            color: Theme.text
            wrapMode: TextEdit.Wrap
            font.pixelSize: campo.tamano
            Binding on text { when: !activeFocus; value: campo.valorTexto }
            onEditingFinished: if (text !== campo.valorTexto) campo.cambio(text)
            background: Rectangle { color: Theme.field; radius: 6; border.color: parent.activeFocus ? Theme.accent : "transparent" }
        }
    }
    Component {
        id: textoC
        Escrito { cf: campo; onListo: (t) => campo.cambio(t) }
    }

    // El campo escrito de todos (texto, ruta, clave, json, número, color, el del combo):
    // muestra lo del modelo salvo mientras se escribe, y avisa con `listo(texto)` al
    // terminar (o en cada tecla, con `alEscribir`).
    component Escrito: TextField {
        id: escrito
        /// El campo dueño (un componente en línea no ve los id del archivo).
        required property var cf
        signal listo(string t)
        color: Theme.text
        font { pixelSize: cf.tamano; family: cf.kind === "json" ? Theme.mono : Theme.font }
        echoMode: cf.kind === "clave" ? TextInput.Password : TextInput.Normal
        placeholderText: cf.c.placeholder !== undefined ? cf.c.placeholder
                         : cf.kind === "path" ? qsTr("Ruta del archivo") : (cf.ayudaAbajo ? "" : (cf.c.help || ""))
        Binding on text { when: !escrito.activeFocus; value: cf.valorTexto }
        onTextEdited: if (cf.alEscribir) escrito.listo(text)
        onEditingFinished: if (!cf.alEscribir && text !== cf.valorTexto) escrito.listo(text)
        background: Rectangle { color: Theme.field; radius: 6; border.color: escrito.activeFocus ? Theme.accent : "transparent" }
    }
}
