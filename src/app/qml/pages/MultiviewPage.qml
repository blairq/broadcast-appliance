// Multiview: varias fuentes en un muro, con separación de color y marco por
// cuadro, tally, vúmetros y nombres. Lo que se arma acá se guarda
// (Config/multiview.json) y lo siguen las ventanas que se sacan a otro monitor
// («Sacar a una ventana»), en vivo. MCP: multiview.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import BA
import "../controls"

Rectangle {
    id: page
    color: Theme.bg
    property real leftInset: 92
    readonly property var mv: Studio.multiview
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
    function nombre(k) { return k.startsWith("entrada:") ? "Entrada · " + k.slice(8) : Studio.frames.label(k) }
    function poner(clave, valor) { const c = {}; c[clave] = valor; Studio.setMultiview(c) }
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
            title: "Multiview"
            Item {
                id: marco
                anchors.fill: parent
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
                    }
                    Text {
                        anchors.centerIn: parent
                        visible: page.fuentes.length === 0
                        text: "Elegí las fuentes a la derecha."
                        color: Theme.textDim; font.pixelSize: 15
                    }
                }
            }
        }

        // --- La configuración ---
        Panel {
            Layout.preferredWidth: 380
            Layout.fillHeight: true
            title: "Armado"
            ScrollView {
                anchors.fill: parent
                clip: true
                contentWidth: availableWidth
                ColumnLayout {
                    width: parent.width
                    spacing: 10

                    RowLayout {
                        spacing: 8
                        GlassButton {
                            text: "Sacar a una ventana"; icon: "⧉"; active: true
                            onClicked: Studio.dashboard.windows.open([], "multiview")
                        }
                        GlassButton { compact: true; text: "De fábrica"; onClicked: Studio.restablecerMultiview() }
                    }
                    Text {
                        Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.textFaint; font.pixelSize: 11
                        text: "La ventana se puede llevar a otro monitor y poner en pantalla completa (doble clic). Sigue lo que cambies acá, en vivo."
                    }

                    Text { Layout.topMargin: 6; text: "Fuentes"; color: Theme.text; font { pixelSize: 15; weight: Font.DemiBold } }
                    Text {
                        Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.textDim; font.pixelSize: 11
                        text: "Tildá las que van; el orden es el de los cuadros (con «Control», las dos primeras van grandes arriba)."
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

                    Text { Layout.topMargin: 8; text: "Distribución"; color: Theme.text; font { pixelSize: 15; weight: Font.DemiBold } }
                    Flow {
                        Layout.fillWidth: true
                        spacing: 6
                        Repeater {
                            model: [{ v: "control", t: "Control" }, { v: "grid", t: "Cuadrícula" }, { v: "focus", t: "Foco" },
                                    { v: "pip", t: "PiP" }, { v: "row", t: "Fila" }]
                            delegate: GlassButton {
                                required property var modelData
                                compact: true; text: modelData.t
                                active: (page.mv.distribucion || "grid") === modelData.v
                                onClicked: page.poner("distribucion", modelData.v)
                            }
                        }
                    }

                    Text { Layout.topMargin: 8; text: "Separación entre cuadros"; color: Theme.text; font { pixelSize: 15; weight: Font.DemiBold } }
                    RowLayout {
                        Layout.fillWidth: true
                        Slider { Layout.fillWidth: true; from: 0; to: 60; stepSize: 1; value: page.mv.separacion || 0
                                 onMoved: page.poner("separacion", Math.round(value)) }
                        Text { text: (page.mv.separacion || 0) + " px"; color: Theme.textDim; font.pixelSize: 12 }
                    }
                    Text { text: "Color de la separación"; color: Theme.textDim; font.pixelSize: 12 }
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

                    Text { Layout.topMargin: 8; text: "Borde de cada cuadro"; color: Theme.text; font { pixelSize: 15; weight: Font.DemiBold } }
                    RowLayout {
                        Layout.fillWidth: true
                        Slider { Layout.fillWidth: true; from: 0; to: 20; stepSize: 1; value: page.mv.borde || 0
                                 onMoved: page.poner("borde", Math.round(value)) }
                        Text { text: (page.mv.borde || 0) + " px"; color: Theme.textDim; font.pixelSize: 12 }
                    }
                    Text { text: "Color del borde"; color: Theme.textDim; font.pixelSize: 12 }
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

                    Text { Layout.topMargin: 8; text: "Marcas"; color: Theme.text; font { pixelSize: 15; weight: Font.DemiBold } }
                    RowLayout {
                        Switch { checked: page.mv.nombres === true; onToggled: page.poner("nombres", checked) }
                        Text { text: "Nombre de cada fuente"; color: Theme.text; font.pixelSize: 13 }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        visible: page.mv.nombres === true
                        Text { text: "Tamaño"; color: Theme.textDim; font.pixelSize: 12 }
                        Slider { Layout.fillWidth: true; from: 10; to: 48; stepSize: 1; value: page.mv.tamNombres || 18
                                 onMoved: page.poner("tamNombres", Math.round(value)) }
                    }
                    RowLayout {
                        Switch { checked: page.mv.tally !== false; onToggled: page.poner("tally", checked) }
                        Text { text: "Tally (rojo al aire, verde armada)"; color: Theme.text; font.pixelSize: 13 }
                    }
                    RowLayout {
                        Switch { checked: page.mv.vumetros !== false; onToggled: page.poner("vumetros", checked) }
                        Text { text: "Vúmetros"; color: Theme.text; font.pixelSize: 13 }
                    }
                }
            }
        }
    }
}
