// Multiview: varias fuentes en un muro, con separación de color y marco por
// cuadro, tally, vúmetros y nombres. Varias multivistas, en pestañas. Lo que se
// arma se guarda (Config/multiview.json) y lo siguen, en vivo, las ventanas que
// se sacan a otro monitor (cada una la suya). MCP: multiview.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import BA
import "../controls"

Rectangle {
    id: page
    color: Theme.bg
    property real leftInset: 92
    readonly property var mv: Studio.multivistas.elegida
    readonly property var fuentes: page.mv.fuentes || []
    /// Todo lo que se puede poner: programa, armada, invitados, pantallas compartidas y las entradas.
    readonly property var disponibles: {
        const out = (Studio.frames.keys || []).slice()
        const ent = Studio.video ? Studio.video.entradas : []
        for (const e of ent) out.push("entrada:" + e)
        return out
    }
    readonly property var colores: ["#202024", "#000000", "#ffffff", "#5a5a64", "#16a34a", "#2563eb",
                                    "#dc2626", "#eab308", "#ea580c", "#9333ea", "#0891b2"]
    function nombre(k) { return k.startsWith("entrada:") ? qsTr("Entrada · %1").arg(k.slice(8)) : Studio.frames.label(k) }
    function poner(clave, valor) { const c = {}; c[clave] = valor; Studio.multivistas.cambiar(c) }
    function alternar(k) {
        const f = page.fuentes.slice(); const i = f.indexOf(k)
        if (i >= 0) f.splice(i, 1); else f.push(k)
        page.poner("fuentes", f)
    }
    function mover(k, d) {
        const f = page.fuentes.slice(); const i = f.indexOf(k); const j = i + d
        if (i < 0 || j < 0 || j >= f.length) return
        f[i] = f[j]; f[j] = k
        page.poner("fuentes", f)
    }

    RowLayout {
        anchors { fill: parent; leftMargin: page.leftInset; margins: 20 }
        spacing: 16

        // --- La vista previa (16:9) ---
        Panel {
            Layout.fillWidth: true
            Layout.fillHeight: true
            title: ""
            // Las pestañas: una por multivista, y «+» para otra.
            RowLayout {
                id: pestanas
                anchors { left: parent.left; right: parent.right; top: parent.top }
                spacing: 6
                Flickable {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 34
                    contentWidth: filaPestanas.implicitWidth
                    clip: true
                    flickableDirection: Flickable.HorizontalFlick
                    Row {
                        id: filaPestanas
                        spacing: 4
                        Repeater {
                            model: Studio.multivistas.lista
                            delegate: Rectangle {
                                required property var modelData
                                readonly property bool elegida: modelData.id === Studio.multivistas.actual
                                height: 32; radius: 8
                                width: nombrePestana.implicitWidth + 28
                                color: elegida ? Qt.rgba(1, 0.42, 0.17, 0.28) : (pestMa.containsMouse ? Qt.rgba(1, 1, 1, 0.08) : Qt.rgba(1, 1, 1, 0.03))
                                border.color: elegida ? Theme.accent : Theme.glassBorder
                                // Punto: esta multivista tiene una ventana abierta.
                                Rectangle {
                                    visible: (Studio.multivistas.conVentana || []).indexOf(modelData.id) >= 0
                                    width: 7; height: 7; radius: 4; color: Theme.ok
                                    anchors { right: parent.right; top: parent.top; margins: 4 }
                                }
                                Text {
                                    id: nombrePestana
                                    anchors.centerIn: parent
                                    text: modelData.nombre || "Multiview"
                                    color: parent.elegida ? Theme.text : Theme.textDim
                                    font { pixelSize: 13; weight: parent.elegida ? Font.DemiBold : Font.Normal }
                                }
                                MouseArea {
                                    id: pestMa; anchors.fill: parent; hoverEnabled: true
                                    onClicked: Studio.multivistas.actual = modelData.id
                                    onDoubleClicked: { Studio.multivistas.actual = modelData.id; nombreCampo.forceActiveFocus(); nombreCampo.selectAll() }
                                }
                            }
                        }
                    }
                }
                GlassButton {
                    compact: true; text: qsTr("Todas en ventanas"); icon: "⧉"
                    ToolTip.visible: hovered; ToolTip.delay: 300
                    ToolTip.text: qsTr("Una ventana por multivista: una en cada monitor (los que no son el principal), en pantalla completa; si sobran, escalonadas en el principal.")
                    onClicked: { const n = Studio.multivistas.abrirTodas(); Studio.notify(qsTr("%n multivista en ventanas.", "", n), "ok") }
                }
                GlassButton {
                    compact: true; text: qsTr("Cerrar ventanas")
                    visible: (Studio.multivistas.conVentana || []).length > 0
                    onClicked: Studio.multivistas.cerrarVentanas()
                }
                GlassButton {
                    compact: true; text: "+"; implicitWidth: 34
                    ToolTip.visible: hovered; ToolTip.text: qsTr("Otra multivista"); ToolTip.delay: 300
                    onClicked: Studio.multivistas.nueva("", false)
                }
            }
            Item {
                id: marco
                anchors { left: parent.left; right: parent.right; bottom: parent.bottom; top: pestanas.bottom; topMargin: 10 }
                readonly property real ancho: Math.min(width, height * 16 / 9)
                Rectangle {
                    anchors.centerIn: parent
                    width: marco.ancho; height: marco.ancho * 9 / 16
                    color: "black"
                    VideoWall {
                        anchors.fill: parent
                        keys: page.fuentes
                        layout: page.mv.distribucion || "grid"
                        fillMode: VideoItem.Fit
                        gap: page.mv.separacion || 0
                        fondo: page.mv.fondo || "transparent"
                        borde: page.mv.borde || 0
                        colorBorde: page.mv.colorBorde || "white"
                        labels: page.mv.nombres === true
                        tamEtiqueta: page.mv.tamNombres || 18
                        tally: page.mv.tally !== false
                        vumetros: page.mv.vumetros !== false
                        escala: width / 1920
                        grosorTally: page.mv.grosorTally !== undefined ? page.mv.grosorTally : 2
                    }
                    Text {
                        anchors.centerIn: parent
                        visible: page.fuentes.length === 0
                        text: qsTr("Elegí las fuentes a la derecha.")
                        color: Theme.textDim; font.pixelSize: 15
                    }
                }
            }
        }

        // --- La configuración ---
        Panel {
            Layout.preferredWidth: 380
            Layout.fillHeight: true
            title: qsTr("Armado")
            ScrollView {
                anchors.fill: parent
                clip: true
                contentWidth: availableWidth
                ColumnLayout {
                    width: parent.width
                    spacing: 10

                    Text { text: qsTr("Nombre"); color: Theme.textDim; font.pixelSize: 12 }
                    TextField {
                        id: nombreCampo
                        Layout.fillWidth: true
                        text: page.mv.nombre || ""
                        placeholderText: qsTr("Nombre de esta multivista (p. ej. «Control», «Sala», «Cámaras»)")
                        onEditingFinished: if (text.trim() !== "" && text !== page.mv.nombre) Studio.multivistas.renombrar(page.mv.id, text)
                    }
                    Flow {
                        Layout.fillWidth: true
                        spacing: 8
                        GlassButton {
                            text: (Studio.multivistas.conVentana || []).indexOf(page.mv.id) >= 0 ? qsTr("Llevar la ventana") : qsTr("Sacar a una ventana")
                            icon: "⧉"; active: true
                            onClicked: Studio.multivistas.abrirEnVentana(page.mv.id, monitorCombo.monitor, completaSw.checked)
                        }
                        GlassButton { compact: true; text: qsTr("Duplicar"); onClicked: Studio.multivistas.nueva(qsTr("%1 (copia)").arg(page.mv.nombre || "Multiview"), true) }
                        GlassButton { compact: true; text: qsTr("De fábrica"); onClicked: Studio.multivistas.restablecer() }
                        GlassButton {
                            compact: true; text: qsTr("Borrar")
                            enabled: (Studio.multivistas.lista || []).length > 1
                            onClicked: ApplicationWindow.window.confirm({
                                title: qsTr("¿Borrar «%1»?").arg(page.mv.nombre || ""),
                                text: qsTr("Se borra esta multivista. Las ventanas que la mostraban pasan a mostrar otra."),
                                accept: qsTr("Borrar"), reject: qsTr("Cancelar"),
                                action: function() { Studio.multivistas.borrar(page.mv.id) }
                            })
                        }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8
                        Text { text: qsTr("Monitor"); color: Theme.textDim; font.pixelSize: 12 }
                        ComboBox {
                            id: monitorCombo
                            Layout.fillWidth: true
                            // Los de Salidas → Pantallas: orden físico, con el número que se ve al
                            // identificarlos, y se los pide por su id estable.
                            readonly property var lista: Studio.monitores
                            readonly property string monitor: currentIndex >= 0 && currentIndex < lista.length ? lista[currentIndex].id : ""
                            model: lista.map(m => (m.principal ? qsTr("Monitor %1 — %2  (%3×%4, principal)") : qsTr("Monitor %1 — %2  (%3×%4)"))
                                                     .arg(m.orden).arg(m.nombre).arg(m.ancho).arg(m.alto))
                            currentIndex: Math.max(0, lista.findIndex(m => !m.principal))
                        }
                    }
                    RowLayout {
                        Switch { id: completaSw; checked: true }
                        Text { text: qsTr("En pantalla completa"); color: Theme.text; font.pixelSize: 13 }
                    }
                    Text {
                        Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.textFaint; font.pixelSize: 11
                        text: qsTr("La ventana sigue lo que cambies acá, en vivo. En la ventana (mové el mouse): elegir monitor, otra multivista, maximizar, pantalla completa (o doble clic) y cerrar.")
                    }

                    Text { Layout.topMargin: 6; text: qsTr("Fuentes"); color: Theme.text; font { pixelSize: 15; weight: Font.DemiBold } }
                    Text {
                        Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.textDim; font.pixelSize: 11
                        text: qsTr("Tildá las que van; el orden es el de los cuadros (con «Control», las dos primeras van grandes arriba).")
                    }
                    // Con varias salidas: el programa de cada una, con su color y su nombre.
                    GlassButton {
                        visible: Studio.salidas.varias
                        compact: true; text: qsTr("Los programas de todas las salidas")
                        onClicked: page.poner("fuentes", Studio.frames.programas)
                        ToolTip.visible: hovered; ToolTip.delay: 400
                        ToolTip.text: qsTr("Un cuadro por salida, con el marco y el nombre en su color")
                    }
                    Repeater {
                        model: page.disponibles
                        delegate: RowLayout {
                            required property string modelData
                            readonly property int pos: page.fuentes.indexOf(modelData)
                            Layout.fillWidth: true
                            spacing: 6
                            CheckBox { checked: parent.pos >= 0; onToggled: page.alternar(modelData) }
                            Text {
                                Layout.fillWidth: true; elide: Text.ElideRight
                                text: (parent.pos >= 0 ? (parent.pos + 1) + ". " : "") + page.nombre(modelData)
                                color: parent.pos >= 0 ? Theme.text : Theme.textDim; font.pixelSize: 13
                            }
                            GlassButton { compact: true; text: "▲"; visible: parent.pos > 0; onClicked: page.mover(modelData, -1) }
                            GlassButton { compact: true; text: "▼"; visible: parent.pos >= 0 && parent.pos < page.fuentes.length - 1; onClicked: page.mover(modelData, 1) }
                        }
                    }

                    Text { Layout.topMargin: 8; text: qsTr("Distribución"); color: Theme.text; font { pixelSize: 15; weight: Font.DemiBold } }
                    Flow {
                        Layout.fillWidth: true
                        spacing: 6
                        Repeater {
                            model: [{ v: "control", t: qsTr("Control") }, { v: "grid", t: qsTr("Cuadrícula") }, { v: "focus", t: qsTr("Foco") },
                                    { v: "pip", t: "PiP" }, { v: "row", t: qsTr("Fila") }]
                            delegate: GlassButton {
                                required property var modelData
                                compact: true; text: modelData.t
                                active: (page.mv.distribucion || "grid") === modelData.v
                                onClicked: page.poner("distribucion", modelData.v)
                            }
                        }
                    }

                    Text { Layout.topMargin: 8; text: qsTr("Separación entre cuadros"); color: Theme.text; font { pixelSize: 15; weight: Font.DemiBold } }
                    RowLayout {
                        Layout.fillWidth: true
                        Slider { Layout.fillWidth: true; from: 0; to: 60; stepSize: 1; value: page.mv.separacion || 0
                                 onMoved: page.poner("separacion", Math.round(value)) }
                        Text { text: (page.mv.separacion || 0) + " px"; color: Theme.textDim; font.pixelSize: 12 }
                    }
                    Text { text: qsTr("Color de la separación"); color: Theme.textDim; font.pixelSize: 12 }
                    Flow {
                        Layout.fillWidth: true; spacing: 6
                        Repeater {
                            model: page.colores
                            delegate: Rectangle {
                                required property string modelData
                                width: 24; height: 24; radius: 12; color: modelData
                                border.color: (page.mv.fondo || "") === modelData ? Theme.accent : Qt.rgba(1, 1, 1, 0.25)
                                border.width: (page.mv.fondo || "") === modelData ? 3 : 1
                                MouseArea { anchors.fill: parent; onClicked: page.poner("fondo", modelData) }
                            }
                        }
                    }

                    Text { Layout.topMargin: 8; text: qsTr("Borde de cada cuadro"); color: Theme.text; font { pixelSize: 15; weight: Font.DemiBold } }
                    RowLayout {
                        Layout.fillWidth: true
                        Slider { Layout.fillWidth: true; from: 0; to: 20; stepSize: 1; value: page.mv.borde || 0
                                 onMoved: page.poner("borde", Math.round(value)) }
                        Text { text: (page.mv.borde || 0) + " px"; color: Theme.textDim; font.pixelSize: 12 }
                    }
                    Text { text: qsTr("Color del borde"); color: Theme.textDim; font.pixelSize: 12 }
                    Flow {
                        Layout.fillWidth: true; spacing: 6
                        Repeater {
                            model: page.colores
                            delegate: Rectangle {
                                required property string modelData
                                width: 24; height: 24; radius: 12; color: modelData
                                border.color: (page.mv.colorBorde || "") === modelData ? Theme.accent : Qt.rgba(1, 1, 1, 0.25)
                                border.width: (page.mv.colorBorde || "") === modelData ? 3 : 1
                                MouseArea { anchors.fill: parent; onClicked: page.poner("colorBorde", modelData) }
                            }
                        }
                    }

                    Text { Layout.topMargin: 8; text: qsTr("Marcas"); color: Theme.text; font { pixelSize: 15; weight: Font.DemiBold } }
                    RowLayout {
                        Switch { checked: page.mv.nombres === true; onToggled: page.poner("nombres", checked) }
                        Text { text: qsTr("Nombre de cada fuente"); color: Theme.text; font.pixelSize: 13 }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        visible: page.mv.nombres === true
                        Text { text: qsTr("Tamaño"); color: Theme.textDim; font.pixelSize: 12 }
                        Slider { Layout.fillWidth: true; from: 10; to: 48; stepSize: 1; value: page.mv.tamNombres || 18
                                 onMoved: page.poner("tamNombres", Math.round(value)) }
                    }
                    RowLayout {
                        Switch { checked: page.mv.tally !== false; onToggled: page.poner("tally", checked) }
                        Text { text: qsTr("Tally (rojo al aire, verde armada)"); color: Theme.text; font.pixelSize: 13 }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        visible: page.mv.tally !== false
                        Text { text: qsTr("Grosor del tally"); color: Theme.textDim; font.pixelSize: 12 }
                        Slider { Layout.fillWidth: true; from: 1; to: 12; stepSize: 1
                                 value: page.mv.grosorTally !== undefined ? page.mv.grosorTally : 2
                                 onMoved: page.poner("grosorTally", Math.round(value)) }
                        Text { text: (page.mv.grosorTally !== undefined ? page.mv.grosorTally : 2) + " px"; color: Theme.textDim; font.pixelSize: 12 }
                    }
                    RowLayout {
                        Switch { checked: page.mv.vumetros !== false; onToggled: page.poner("vumetros", checked) }
                        Text { text: qsTr("Vúmetros"); color: Theme.text; font.pixelSize: 13 }
                    }
                }
            }
        }
    }
}
