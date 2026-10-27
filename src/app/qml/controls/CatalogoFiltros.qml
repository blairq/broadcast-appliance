// El catálogo de filtros de audio (LSP): búsqueda avanzada y detalle.
//
// Cada familia (compresor, compuerta, EQ paramétrico...) con su captura real
// (del código fuente de LSP, LGPL-3.0: assets/lsp-capturas), qué hace en
// castellano, para qué sirve en vivo y sus variantes (mono, estéreo, L/R,
// M/S, con sidechain, cantidad de bandas). Elegir una variante la agrega al
// rack de la fuente abierta. Datos: AudioViewModel.catalogoLsp().
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import BA

Popup {
    id: cat
    property var au: Studio.audio
    /// Al elegir una variante: (id del plugin).
    signal elegido(string id)

    modal: true
    anchors.centerIn: Overlay.overlay
    width: Math.min(1180, (Overlay.overlay ? Overlay.overlay.width : 1200) - 80)
    height: Math.min(820, (Overlay.overlay ? Overlay.overlay.height : 860) - 60)
    padding: 0
    background: Rectangle { color: Theme.panel; radius: Theme.radius; border.color: Theme.glassBorder }

    property var familias: []
    property string categoria: ""
    property bool soloVivo: false
    property bool soloEstereo: false
    property bool soloSidechain: false
    property bool conInstrumentos: false
    property var detalle: null
    onOpened: { familias = au ? au.catalogoLsp() : []; busqueda.forceActiveFocus() }

    readonly property var colores: ({
        "Dinámica": "#ff6a2b", "Ecualizadores": "#0ea5e9", "Multibanda": "#f59e0b", "Retardo": "#a78bfa",
        "Reverberación": "#8b5cf6", "Modulación": "#ec4899", "Análisis": "#22c55e", "Utilidades": "#64748b",
        "Generadores": "#eab308", "Instrumentos": "#14b8a6" })
    function colorDe(c) { return colores[c] || Theme.accent }
    function normal(s) { return String(s).toLowerCase().normalize("NFD").replace(/[̀-ͯ]/g, "") }

    readonly property var visibles: {
        const q = normal(busqueda.text.trim())
        const palabras = q === "" ? [] : q.split(/\s+/)
        return familias.filter(f => {
            if (!conInstrumentos && !f.esFiltro) return false
            if (categoria !== "" && f.categoria !== categoria) return false
            if (soloVivo && !f.vivo) return false
            if (soloEstereo && !f.variantes.some(v => v.estereo)) return false
            if (soloSidechain && !f.variantes.some(v => v.sidechain)) return false
            if (palabras.length === 0) return true
            // Busca en castellano y en inglés: nombre, etiquetas, descripción y la original.
            const h = normal([f.nombre, f.clave, f.categoria, (f.etiquetas || []).join(" "), f.descripcion,
                              f.usoVivo, f.descripcionOriginal, f.variantes.map(v => v.id).join(" ")].join(" "))
            return palabras.every(p => h.indexOf(p) >= 0)
        })
    }
    readonly property var categorias: {
        const vistas = []
        for (const f of familias) if ((conInstrumentos || f.esFiltro) && vistas.indexOf(f.categoria) < 0) vistas.push(f.categoria)
        return vistas
    }

    RowLayout {
        anchors.fill: parent
        anchors.margins: 18
        spacing: 18

        // --- Búsqueda avanzada ----------------------------------------------------------
        ColumnLayout {
            // Ancho fijo: sus hijos con fillWidth (búsqueda, categorías) si no
            // la estiran a todo el ancho y la grilla queda en 0 (no se veía nada).
            Layout.preferredWidth: 230
            Layout.minimumWidth: 230
            Layout.maximumWidth: 230
            Layout.fillWidth: false
            Layout.fillHeight: true
            spacing: 10
            Text { text: qsTr("Filtros de audio"); color: Theme.text; font { pixelSize: 19; weight: Font.DemiBold } }
            Text { text: qsTr("%1 de %n familias", "", cat.familias.filter(f => cat.conInstrumentos || f.esFiltro).length).arg(cat.visibles.length); color: Theme.textDim; font.pixelSize: 11 }
            TextField {
                id: busqueda
                Layout.fillWidth: true
                placeholderText: qsTr("buscar: «voz», «ruido», «graves», «limiter»…")
            }
            Text { text: qsTr("Categoría"); color: Theme.textDim; font { pixelSize: 11; capitalization: Font.AllUppercase; letterSpacing: 0.5 } }
            Flow {
                Layout.fillWidth: true
                spacing: 5
                GlassButton { compact: true; text: qsTr("Todas"); active: cat.categoria === ""; onClicked: cat.categoria = "" }
                Repeater {
                    model: cat.categorias
                    delegate: GlassButton {
                        required property string modelData
                        compact: true; text: modelData
                        active: cat.categoria === modelData
                        activeColor: cat.colorDe(modelData)
                        onClicked: cat.categoria = cat.categoria === modelData ? "" : modelData
                    }
                }
            }
            Text { text: qsTr("Mostrar"); color: Theme.textDim; font { pixelSize: 11; capitalization: Font.AllUppercase; letterSpacing: 0.5 } }
            Repeater {
                model: [ { k: "soloVivo", t: qsTr("Recomendados para vivo") }, { k: "soloEstereo", t: qsTr("Con versión estéreo") },
                         { k: "soloSidechain", t: qsTr("Con sidechain") }, { k: "conInstrumentos", t: qsTr("Incluir instrumentos") } ]
                delegate: RowLayout {
                    required property var modelData
                    spacing: 8
                    Toggle { checked: cat[modelData.k]; onToggled: (w) => cat[modelData.k] = w }
                    Text { text: modelData.t; color: Theme.text; font.pixelSize: 12 }
                }
            }
            Item { Layout.fillHeight: true }
            Text {
                Layout.fillWidth: true; wrapMode: Text.WordWrap
                color: Theme.textFaint; font.pixelSize: 10
                text: qsTr("Plugins y capturas: Linux Studio Plugins (lsp-plug.in), LGPL-3.0.")
            }
            GlassButton { compact: true; text: qsTr("Cerrar"); onClicked: cat.close() }
        }

        // --- Grilla de tarjetas ---------------------------------------------------------
        GridView {
            id: grilla
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            cellWidth: Math.max(250, width / Math.max(1, Math.floor(width / 270)))
            cellHeight: 250
            model: cat.visibles
            ScrollBar.vertical: ScrollBar {}
            delegate: Item {
                id: tarjeta
                required property var modelData
                width: grilla.cellWidth
                height: grilla.cellHeight
                readonly property color col: cat.colorDe(modelData.categoria)
                Rectangle {
                    anchors { fill: parent; margins: 7 }
                    radius: 14
                    color: hov.containsMouse ? Qt.rgba(1, 1, 1, 0.07) : Qt.rgba(1, 1, 1, 0.035)
                    border.color: hov.containsMouse ? tarjeta.col : Qt.rgba(1, 1, 1, 0.08)
                    Behavior on border.color { ColorAnimation { duration: 120 } }
                    clip: true
                    ColumnLayout {
                        anchors { fill: parent; margins: 10 }
                        spacing: 6
                        Rectangle {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 126
                            radius: 8
                            color: "#0d0d10"
                            clip: true
                            Image {
                                anchors.fill: parent
                                visible: !!tarjeta.modelData.captura
                                source: tarjeta.modelData.captura || ""
                                fillMode: Image.PreserveAspectCrop
                                asynchronous: true
                                smooth: true
                                scale: hov.containsMouse ? 1.06 : 1
                                Behavior on scale { NumberAnimation { duration: 220; easing.type: Easing.OutCubic } }
                            }
                            IlustracionFiltro {
                                anchors.fill: parent
                                visible: !tarjeta.modelData.captura
                                tipo: tarjeta.modelData.ilustracion
                                color: tarjeta.col
                            }
                            Rectangle {
                                anchors { left: parent.left; top: parent.top; margins: 6 }
                                radius: 6; height: 18; width: catTxt.implicitWidth + 12
                                color: Qt.rgba(0, 0, 0, 0.65)
                                Text { id: catTxt; anchors.centerIn: parent; text: tarjeta.modelData.categoria; color: tarjeta.col; font { pixelSize: 10; weight: Font.DemiBold } }
                            }
                            Rectangle {
                                visible: tarjeta.modelData.vivo
                                anchors { right: parent.right; top: parent.top; margins: 6 }
                                radius: 6; height: 18; width: vivoTxt.implicitWidth + 12
                                color: Qt.rgba(0.94, 0.27, 0.27, 0.85)
                                Text { id: vivoTxt; anchors.centerIn: parent; text: qsTr("VIVO"); color: "white"; font { pixelSize: 9; weight: Font.Bold } }
                            }
                        }
                        Text { Layout.fillWidth: true; text: tarjeta.modelData.nombre; color: Theme.text; elide: Text.ElideRight; font { pixelSize: 14; weight: Font.DemiBold } }
                        Text {
                            Layout.fillWidth: true; Layout.fillHeight: true
                            text: tarjeta.modelData.descripcion
                            wrapMode: Text.WordWrap; elide: Text.ElideRight; maximumLineCount: 3
                            color: Theme.textDim; font.pixelSize: 11
                        }
                        Text {
                            text: qsTr("%n variante", "", tarjeta.modelData.variantes.length)
                            color: Theme.textFaint; font.pixelSize: 10
                        }
                    }
                    MouseArea {
                        id: hov
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: { cat.detalle = tarjeta.modelData; detallePop.open() }
                    }
                }
            }
            Text {
                anchors.centerIn: parent
                visible: grilla.count === 0
                text: cat.familias.length === 0 ? qsTr("No se pudieron abrir los módulos LSP.") : qsTr("Nada coincide con la búsqueda.")
                color: Theme.textDim; font.pixelSize: 14
            }
        }
    }

    // --- Detalle de una familia ---------------------------------------------------------
    Popup {
        id: detallePop
        modal: true
        anchors.centerIn: parent
        width: Math.min(900, cat.width - 60)
        height: Math.min(700, cat.height - 40)
        padding: 0
        readonly property var d: cat.detalle || ({ variantes: [] })
        readonly property color col: cat.colorDe(d.categoria)
        background: Rectangle { color: "#141417"; radius: Theme.radius; border.color: detallePop.col }
        ColumnLayout {
            anchors.fill: parent
            spacing: 0
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: parent.height * 0.52
                color: "#0b0b0d"
                radius: Theme.radius
                clip: true
                Image {
                    anchors { fill: parent; margins: 12 }
                    visible: !!detallePop.d.captura
                    source: detallePop.d.captura || ""
                    fillMode: Image.PreserveAspectFit
                    smooth: true
                }
                IlustracionFiltro {
                    anchors { fill: parent; margins: 12 }
                    visible: !detallePop.d.captura
                    tipo: detallePop.d.ilustracion || "eq"
                    color: detallePop.col
                    grande: true
                }
            }
            ScrollView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                contentWidth: availableWidth
                clip: true
                ColumnLayout {
                    width: parent.width
                    spacing: 8
                    Item { Layout.preferredHeight: 6 }
                    RowLayout {
                        Layout.fillWidth: true; Layout.leftMargin: 20; Layout.rightMargin: 20
                        Text { Layout.fillWidth: true; text: detallePop.d.nombre || ""; color: Theme.text; font { pixelSize: 20; weight: Font.DemiBold } }
                        Text { text: detallePop.d.categoria || ""; color: detallePop.col; font { pixelSize: 12; weight: Font.DemiBold } }
                    }
                    Text { Layout.fillWidth: true; Layout.leftMargin: 20; Layout.rightMargin: 20; wrapMode: Text.WordWrap; text: detallePop.d.descripcion || ""; color: Theme.text; font.pixelSize: 13 }
                    Rectangle {
                        Layout.fillWidth: true; Layout.leftMargin: 20; Layout.rightMargin: 20
                        implicitHeight: usoTxt.implicitHeight + 16
                        radius: 10
                        color: Qt.rgba(1, 0.42, 0.17, 0.1)
                        border.color: Qt.rgba(1, 0.42, 0.17, 0.4)
                        Text { id: usoTxt; anchors { fill: parent; margins: 8 } wrapMode: Text.WordWrap; text: qsTr("En vivo: %1").arg(detallePop.d.usoVivo || ""); color: Theme.text; font.pixelSize: 12 }
                    }
                    Text {
                        Layout.fillWidth: true; Layout.leftMargin: 20; Layout.rightMargin: 20
                        wrapMode: Text.WordWrap; visible: !!detallePop.d.descripcionOriginal
                        text: qsTr("«%1» — LSP").arg(detallePop.d.descripcionOriginal || "")
                        color: Theme.textFaint; font { pixelSize: 11; italic: true }
                    }
                    Text { Layout.leftMargin: 20; text: qsTr("Agregar a «%1»").arg(cat.au ? cat.au.source : ""); color: Theme.textDim; font { pixelSize: 11; capitalization: Font.AllUppercase; letterSpacing: 0.5 } }
                    Flow {
                        Layout.fillWidth: true; Layout.leftMargin: 20; Layout.rightMargin: 20
                        spacing: 6
                        Repeater {
                            model: detallePop.d.variantes
                            delegate: GlassButton {
                                required property var modelData
                                compact: true
                                text: "+ " + modelData.etiqueta
                                active: true
                                activeColor: detallePop.col
                                onClicked: { cat.elegido(modelData.id); detallePop.close(); cat.close() }
                            }
                        }
                    }
                    Item { Layout.preferredHeight: 14 }
                }
            }
        }
    }
}
