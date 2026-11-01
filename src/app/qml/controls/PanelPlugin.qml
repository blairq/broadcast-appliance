// El panel de un plugin: la interfaz que el plugin DESCRIBE con plugin.panel([...])
// y que la app dibuja con sus propios controles (ver docs/funciones/PLUGINS.md). El plugin
// corre en su proceso: si se cuelga, acá solo se ve «no está corriendo».
//
// Tipos: seccion · deslizador · interruptor · opcion · texto · numero · boton ·
// indicador · medidor. Lo que mueve el operador va por Studio.plugins.panelUi
// (se guarda y le llega al plugin); los valores en vivo llegan por
// valoresCambio(id) y se leen con valoresUi(id) (aparte de la lista de plugins,
// que se rearma entera: así un medidor a 10 Hz no rehace la página).
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import BA

ColumnLayout {
    id: panel
    property string pluginId: ""
    property var controles: []
    property bool corriendo: true
    readonly property var pl: Studio.plugins
    // Lo que muestra el panel, del modelo (avisa con cada valor que manda el plugin).
    readonly property var valores: pl && pluginId !== "" ? (pl.valores[pluginId] || ({})) : ({})
    spacing: 8

    function poner(clave, valor) {
        const e = pl.panelUi(pluginId, clave, valor)
        if (e !== "") Studio.notify(e, "error")
    }
    function num(v, d) { return typeof v === "number" ? v : (v !== undefined && v !== null && v !== "" && !isNaN(Number(v)) ? Number(v) : d) }
    function mostrar(v, c) {
        if (v === undefined || v === null) return "—"
        if (typeof v === "number") return (Number.isInteger(v) ? v : v.toFixed(Math.abs(v) < 10 ? 2 : 1)) + (c.unidad ? " " + c.unidad : "")
        if (typeof v === "boolean") return v ? qsTr("sí") : qsTr("no")
        if (typeof v === "object") return JSON.stringify(v)
        return String(v) + (c.unidad ? " " + c.unidad : "")
    }


    Text {
        visible: !panel.corriendo
        Layout.fillWidth: true
        text: qsTr("El plugin no está corriendo: prendelo para usar su panel.")
        color: Theme.textFaint; font.pixelSize: 12; wrapMode: Text.WordWrap
    }

    Repeater {
        model: panel.controles
        delegate: RowLayout {
            id: fila
            required property var modelData
            readonly property var c: modelData
            readonly property var v: panel.valores[c.clave]
            Layout.fillWidth: true
            spacing: 10
            enabled: panel.corriendo

            // Título de sección: ocupa toda la fila.
            Text {
                visible: fila.c.tipo === "seccion"
                Layout.fillWidth: true
                Layout.topMargin: 6
                text: fila.c.etiqueta || ""
                color: Theme.textFaint; font { pixelSize: 11; weight: Font.DemiBold; capitalization: Font.AllUppercase; letterSpacing: 0.5 }
            }
            // La etiqueta (todos los demás; el botón lleva la suya adentro).
            Text {
                visible: fila.c.tipo !== "seccion" && fila.c.tipo !== "boton"
                Layout.preferredWidth: 160
                Layout.maximumWidth: 160
                text: fila.c.etiqueta || fila.c.clave
                color: Theme.textDim; font.pixelSize: 12; wrapMode: Text.WordWrap
                ToolTip.visible: !!fila.c.ayuda && etiHover.hovered; ToolTip.text: fila.c.ayuda || ""; ToolTip.delay: 400
                HoverHandler { id: etiHover }
            }

            Loader {
                visible: fila.c.tipo !== "seccion"
                Layout.fillWidth: true
                sourceComponent: ({ deslizador: cDeslizador, interruptor: cInterruptor, opcion: cOpcion, texto: cTexto, numero: cTexto,
                                    boton: cBoton, indicador: cIndicador, medidor: cMedidor })[fila.c.tipo] || null

                Component {
                    id: cDeslizador
                    RowLayout {
                        spacing: 8
                        Slider {
                            id: sl
                            Layout.fillWidth: true
                            from: panel.num(fila.c.minimo, 0); to: panel.num(fila.c.maximo, 1)
                            stepSize: panel.num(fila.c.paso, 0)
                            snapMode: stepSize > 0 ? Slider.SnapAlways : Slider.NoSnap
                            // Mientras se arrastra manda el operador; si no, lo que dice el plugin.
                            Binding on value { when: !sl.pressed; value: panel.num(fila.v, panel.num(fila.c.defecto, sl.from)) }
                            onMoved: panel.poner(fila.c.clave, value)
                        }
                        Text {
                            Layout.preferredWidth: 64
                            horizontalAlignment: Text.AlignRight
                            text: panel.mostrar(sl.stepSize >= 1 ? Math.round(sl.value) : Math.round(sl.value * 100) / 100, fila.c)
                            color: Theme.text; font { pixelSize: 12; family: Theme.mono }
                        }
                    }
                }
                Component {
                    id: cInterruptor
                    Item {
                        implicitHeight: 24
                        Toggle {
                            checked: fila.v === true
                            onToggled: (quiero) => panel.poner(fila.c.clave, quiero)
                        }
                    }
                }
                Component {
                    id: cOpcion
                    Item {
                        implicitHeight: (fila.c.opciones || []).length <= 4 ? botones.implicitHeight : combo.implicitHeight
                        // Pocas opciones: a la vista. Muchas: una lista.
                        Flow {
                            id: botones
                            width: parent.width
                            visible: (fila.c.opciones || []).length <= 4
                            spacing: 4
                            Repeater {
                                model: fila.c.opciones || []
                                delegate: GlassButton {
                                    required property string modelData
                                    compact: true; text: modelData
                                    active: (fila.v !== undefined ? fila.v : fila.c.defecto) === modelData
                                    onClicked: panel.poner(fila.c.clave, modelData)
                                }
                            }
                        }
                        CampoFormulario {
                            id: combo
                            width: Math.min(parent.width, 280)
                            visible: (fila.c.opciones || []).length > 4
                            conEtiqueta: false
                            c: ({ name: fila.c.clave, kind: "list", value: fila.v !== undefined ? fila.v : fila.c.defecto,
                                  options: (fila.c.opciones || []).map(o => ({ value: o, label: o })) })
                            onCambio: (valor) => panel.poner(fila.c.clave, valor)
                        }
                    }
                }
                Component {
                    id: cTexto
                    // Lo del plugin, salvo mientras se escribe (el mismo campo que los formularios).
                    CampoFormulario {
                        readonly property bool esNumero: fila.c.tipo === "numero"
                        conEtiqueta: false
                        c: ({ name: fila.c.clave, kind: esNumero ? "number" : "text",
                              value: fila.v !== undefined && fila.v !== null ? fila.v : fila.c.defecto,
                              placeholder: (fila.c.ayuda || "") + (esNumero && fila.c.unidad ? " (" + fila.c.unidad + ")" : "") })
                        onCambio: (valor) => panel.poner(fila.c.clave, valor)
                    }
                }
                Component {
                    id: cBoton
                    Item {
                        implicitHeight: 30
                        GlassButton {
                            compact: true
                            text: fila.c.etiqueta || fila.c.clave
                            activeColor: Theme.live
                            active: !!fila.c.peligro
                            onClicked: panel.poner(fila.c.clave, true)
                            ToolTip.visible: hovered && !!fila.c.ayuda; ToolTip.delay: 400; ToolTip.text: fila.c.ayuda || ""
                        }
                    }
                }
                Component {
                    id: cIndicador
                    Text {
                        text: panel.mostrar(fila.v, fila.c)
                        color: Theme.text; font { pixelSize: 14; weight: Font.DemiBold }
                        elide: Text.ElideRight
                    }
                }
                Component {
                    id: cMedidor
                    RowLayout {
                        spacing: 8
                        Rectangle {
                            Layout.fillWidth: true
                            height: 8; radius: 4
                            color: Qt.rgba(1, 1, 1, 0.08)
                            readonly property real minimo: panel.num(fila.c.minimo, 0)
                            readonly property real maximo: panel.num(fila.c.maximo, 1)
                            Rectangle {
                                height: parent.height; radius: 4
                                width: parent.width * Math.max(0, Math.min(1, (panel.num(fila.v, parent.minimo) - parent.minimo) / Math.max(1e-9, parent.maximo - parent.minimo)))
                                color: Theme.ok
                                Behavior on width { NumberAnimation { duration: 90 } }
                            }
                        }
                        Text {
                            Layout.preferredWidth: 64
                            horizontalAlignment: Text.AlignRight
                            text: panel.mostrar(fila.v, fila.c)
                            color: Theme.text; font { pixelSize: 12; family: Theme.mono }
                        }
                    }
                }
            }
        }
    }
}
