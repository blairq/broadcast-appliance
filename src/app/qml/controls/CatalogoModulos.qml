// El catálogo de módulos de video: los de GStreamer y los filtros de OBS.
//
// Como el de los filtros de audio (CatalogoFiltros): búsqueda en castellano o
// en inglés, categorías, y el detalle de cada módulo con su documentación
// (para qué sirve, cómo se usa en vivo; datos/gst-modulos.json), la
// descripción original, el paquete y la licencia, y todas sus propiedades
// (de la introspección de GStreamer). Los de varias entradas (compositor,
// smpte, glmosaic…) van como «Mezcla de GStreamer» del estudio. Los que no van
// (fuentes, salidas, los internos) se ven igual, con por qué no.
// Datos: Studio.video.modulosGst (el catálogo de GStreamer).
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import BA

Popup {
    id: cat
    property var vg: Studio.video
    /// Al elegir: tipo "gst" (id = el elemento), "tarjeta" (una propia) o "mezcla" (un módulo de varias entradas).
    signal elegido(string tipo, string id)

    modal: true
    anchors.centerIn: Overlay.overlay
    width: Math.min(1180, (Overlay.overlay ? Overlay.overlay.width : 1200) - 80)
    height: Math.min(820, (Overlay.overlay ? Overlay.overlay.height : 860) - 60)
    padding: 0
    background: Rectangle { color: Theme.panel; radius: Theme.radius; border.color: Theme.glassBorder }

    property var modulos: []
    property string version: ""
    property string categoria: ""
    property bool soloTarjetas: true
    /// Solo los de varias entradas (se abre así desde «Mezcla de GStreamer»).
    property bool soloMezclas: false
    onClosed: soloMezclas = false
    function abrirMezclas() { soloMezclas = true; categoria = ""; open() }
    /// Si se puede agregar (como tarjeta, como tarjeta propia o como mezcla).
    function va(m) { return !!(m.tarjeta || m.comoTarjeta || m.mezcla) }
    property var detalle: null
    property var detalleProps: []

    onOpened: {
        if (modulos.length === 0 && vg) {
            modulos = vg.modulosGst.map(m => Object.assign({ tipo: "gst" }, m))
            version = vg.versionGst
        }
        busqueda.forceActiveFocus()
    }

    readonly property var colores: ({
        "Color": "#f59e0b", "Llaves y transparencia": "#22c55e", "Geometría": "#38bdf8",
        "Nitidez, ruido y desenfoque": "#94a3b8", "Formato y entrelazado": "#64748b",
        "Textos y superposiciones": "#e879f9", "Análisis y medición": "#10b981", "Efectos artísticos": "#ec4899",
        "Distorsión": "#a78bfa", "Transiciones": "#f97316", "OpenGL (en la GPU)": "#ef4444",
        "Subtítulos (closed captions)": "#eab308", "Fuentes": "#14b8a6", "Salidas": "#0891b2",
        "Mezcla y composición": "#f472b6", "Internos (no van como tarjeta)": "#475569" })
    function colorDe(c) { return colores[c] || Theme.accent }
    /// El título de una tarjeta propia del grafo (comoTarjeta: superres, rostros, seguir).
    function tituloTarjeta(t) {
        const c = vg ? vg.catalogo.find(x => x.tipo === t) : null
        return c ? c.titulo : t
    }
    function normal(s) { return String(s || "").toLowerCase().normalize("NFD").replace(/[̀-ͯ]/g, "") }

    readonly property var visibles: {
        const q = normal(busqueda.text.trim())
        const palabras = q === "" ? [] : q.split(/\s+/)
        return modulos.filter(m => {
            if (soloMezclas && !m.mezcla) return false
            if (soloTarjetas && !va(m)) return false
            if (categoria !== "" && m.categoria !== categoria) return false
            if (palabras.length === 0) return true
            const h = normal([m.elemento, m.titulo, m.nombre, m.categoria, m.para, m.uso, m.descripcion, m.clase].join(" "))
            return palabras.every(p => h.indexOf(p) >= 0)
        })
    }
    readonly property var categorias: {
        const vistas = []
        for (const m of modulos)
            if ((!soloTarjetas || va(m)) && (!soloMezclas || m.mezcla) && vistas.indexOf(m.categoria) < 0) vistas.push(m.categoria)
        return vistas.sort()
    }
    function verDetalle(m) {
        cat.detalle = m
        cat.detalleProps = m.tipo === "gst" && vg ? vg.propiedadesModulo(m.elemento) : []
        detallePop.open()
    }

    RowLayout {
        anchors.fill: parent
        anchors.margins: 18
        spacing: 18

        // --- Búsqueda ---------------------------------------------------------------------
        ColumnLayout {
            Layout.preferredWidth: 240
            Layout.minimumWidth: 240
            Layout.maximumWidth: 240
            Layout.fillWidth: false
            Layout.fillHeight: true
            spacing: 10
            Text { text: qsTr("Módulos de video"); color: Theme.text; font { pixelSize: 19; weight: Font.DemiBold } }
            Text { text: qsTr("%1 de %2 · %3").arg(cat.visibles.length).arg(cat.modulos.length).arg(cat.version); color: Theme.textDim; font.pixelSize: 11; wrapMode: Text.WordWrap; Layout.fillWidth: true }
            TextField {
                id: busqueda
                Layout.fillWidth: true
                placeholderText: qsTr("buscar: «croma», «desentrelazar», «texto», «blur»…")
            }
            RowLayout {
                spacing: 8
                Toggle { checked: cat.soloTarjetas; onToggled: (w) => { cat.soloTarjetas = w; cat.categoria = "" } }
                Text { text: qsTr("Solo los que se pueden agregar"); color: Theme.text; font.pixelSize: 12 }
            }
            RowLayout {
                spacing: 8
                Toggle { checked: cat.soloMezclas; onToggled: (w) => { cat.soloMezclas = w; cat.categoria = "" } }
                Text { text: qsTr("Solo los de varias entradas"); color: Theme.text; font.pixelSize: 12 }
            }
            Text { text: qsTr("Categoría"); color: Theme.textDim; font { pixelSize: 11; capitalization: Font.AllUppercase; letterSpacing: 0.5 } }
            ScrollView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                contentWidth: availableWidth
                clip: true
                Flow {
                    width: parent.width
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
            }
            Text {
                Layout.fillWidth: true; wrapMode: Text.WordWrap
                color: Theme.textFaint; font.pixelSize: 10
                text: qsTr("GStreamer (gstreamer.freedesktop.org), LGPL salvo donde se indica. Viene adentro del programa.")
            }
            GlassButton { compact: true; text: qsTr("Cerrar"); onClicked: cat.close() }
        }

        // --- Grilla -------------------------------------------------------------------------
        GridView {
            id: grilla
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            cellWidth: Math.max(240, width / Math.max(1, Math.floor(width / 260)))
            cellHeight: 168
            model: cat.visibles
            ScrollBar.vertical: ScrollBar {}
            delegate: Item {
                id: tarjeta
                required property var modelData
                width: grilla.cellWidth
                height: grilla.cellHeight
                readonly property color col: cat.colorDe(modelData.categoria)
                Rectangle {
                    anchors { fill: parent; margins: 6 }
                    radius: 12
                    color: hov.containsMouse ? Qt.rgba(1, 1, 1, 0.07) : Qt.rgba(1, 1, 1, 0.035)
                    border.color: hov.containsMouse ? tarjeta.col : Qt.rgba(1, 1, 1, 0.08)
                    Behavior on border.color { ColorAnimation { duration: 120 } }
                    opacity: cat.va(tarjeta.modelData) ? 1 : 0.6
                    clip: true
                    Rectangle { width: 4; anchors { left: parent.left; top: parent.top; bottom: parent.bottom; margins: 1 } radius: 2; color: tarjeta.col }
                    ColumnLayout {
                        anchors { fill: parent; margins: 12; leftMargin: 16 }
                        spacing: 4
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 6
                            Text { text: tarjeta.modelData.categoria; color: tarjeta.col; font { pixelSize: 10; weight: Font.DemiBold } elide: Text.ElideRight; Layout.fillWidth: true }
                            Text { visible: !!tarjeta.modelData.mezcla; text: qsTr("MEZCLA"); color: "#f472b6"; font { pixelSize: 9; weight: Font.Bold } }
                            Text { visible: String(tarjeta.modelData.elemento).indexOf("gl") === 0; text: "GPU"; color: "#ef4444"; font { pixelSize: 9; weight: Font.Bold } }
                        }
                        Text { Layout.fillWidth: true; text: tarjeta.modelData.titulo || tarjeta.modelData.nombre; color: Theme.text; elide: Text.ElideRight; font { pixelSize: 14; weight: Font.DemiBold } }
                        Text { Layout.fillWidth: true; text: tarjeta.modelData.elemento; color: Theme.textFaint; elide: Text.ElideRight; font { pixelSize: 10; family: "Menlo" } }
                        Text {
                            Layout.fillWidth: true; Layout.fillHeight: true
                            text: tarjeta.modelData.para || tarjeta.modelData.descripcion || ""
                            wrapMode: Text.WordWrap; elide: Text.ElideRight; maximumLineCount: 3
                            color: Theme.textDim; font.pixelSize: 11
                        }
                    }
                    MouseArea {
                        id: hov
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: cat.verDetalle(tarjeta.modelData)
                        onDoubleClicked: {
                            const m = tarjeta.modelData
                            if (m.comoTarjeta) cat.elegido("tarjeta", m.comoTarjeta)
                            else if (m.mezcla) cat.elegido("mezcla", m.elemento)
                            else if (m.tarjeta) cat.elegido(m.tipo, m.elemento)
                            else return
                            detallePop.close(); cat.close()
                        }
                    }
                }
            }
            Text {
                anchors.centerIn: parent
                visible: grilla.count === 0
                text: cat.modulos.length === 0 ? qsTr("No se pudo abrir GStreamer.") : qsTr("Nada coincide con la búsqueda.")
                color: Theme.textDim; font.pixelSize: 14
            }
        }
    }

    // --- Detalle de un módulo -------------------------------------------------------------
    Popup {
        id: detallePop
        modal: true
        anchors.centerIn: parent
        width: Math.min(900, cat.width - 60)
        height: Math.min(720, cat.height - 40)
        padding: 0
        readonly property var d: cat.detalle || ({})
        readonly property color col: cat.colorDe(d.categoria)
        background: Rectangle { color: "#141417"; radius: Theme.radius; border.color: detallePop.col }
        ScrollView {
            anchors.fill: parent
            contentWidth: availableWidth
            clip: true
            ColumnLayout {
                width: parent.width
                spacing: 8
                Item { Layout.preferredHeight: 10 }
                RowLayout {
                    Layout.fillWidth: true; Layout.leftMargin: 22; Layout.rightMargin: 22
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 2
                        Text { Layout.fillWidth: true; text: detallePop.d.titulo || detallePop.d.nombre || ""; color: Theme.text; wrapMode: Text.WordWrap; font { pixelSize: 21; weight: Font.DemiBold } }
                        Text { text: (detallePop.d.elemento || "") + (detallePop.d.tipo === "gst" ? "  ·  " + (detallePop.d.nombre || "") : ""); color: Theme.textFaint; font { pixelSize: 11; family: "Menlo" } }
                    }
                    Text { text: detallePop.d.categoria || ""; color: detallePop.col; font { pixelSize: 12; weight: Font.DemiBold } }
                }
                Text { Layout.fillWidth: true; Layout.leftMargin: 22; Layout.rightMargin: 22; wrapMode: Text.WordWrap; text: detallePop.d.para || ""; color: Theme.text; font.pixelSize: 14 }
                Rectangle {
                    visible: !!detallePop.d.uso
                    Layout.fillWidth: true; Layout.leftMargin: 22; Layout.rightMargin: 22
                    implicitHeight: usoTxt.implicitHeight + 18
                    radius: 10
                    color: Qt.rgba(1, 0.42, 0.17, 0.1)
                    border.color: Qt.rgba(1, 0.42, 0.17, 0.4)
                    Text { id: usoTxt; anchors { fill: parent; margins: 9 } wrapMode: Text.WordWrap; textFormat: Text.MarkdownText; text: qsTr("**En vivo:** %1").arg(detallePop.d.uso || ""); color: Theme.text; font.pixelSize: 12 }
                }
                Text {
                    Layout.fillWidth: true; Layout.leftMargin: 22; Layout.rightMargin: 22
                    wrapMode: Text.WordWrap; visible: !!detallePop.d.descripcion
                    text: qsTr("«%1» — %2").arg(detallePop.d.descripcion || "").arg(detallePop.d.clase || "GStreamer")
                    color: Theme.textFaint; font { pixelSize: 11; italic: true }
                }
                Text {
                    Layout.leftMargin: 22
                    text: [detallePop.d.paquete, detallePop.d.plugin ? qsTr("plugin %1").arg(detallePop.d.plugin) : "", detallePop.d.licencia ? qsTr("licencia %1").arg(detallePop.d.licencia) : ""].filter(x => !!x).join("  ·  ")
                    color: Theme.textFaint; font.pixelSize: 11
                }
                RowLayout {
                    Layout.fillWidth: true; Layout.leftMargin: 22; Layout.rightMargin: 22
                    spacing: 10
                    GlassButton {
                        visible: !!detallePop.d.tarjeta
                        text: qsTr("+ Agregar a «%1»").arg(cat.vg ? cat.vg.entrada : "")
                        active: true
                        activeColor: detallePop.col
                        onClicked: { cat.elegido(detallePop.d.tipo, detallePop.d.elemento); detallePop.close(); cat.close() }
                    }
                    // Los nuestros que van con una tarjeta propia del grafo (superres, rostros, seguir).
                    GlassButton {
                        visible: !!detallePop.d.comoTarjeta
                        text: qsTr("+ Agregar la tarjeta «%1» a «%2»").arg(cat.tituloTarjeta(detallePop.d.comoTarjeta)).arg(cat.vg ? cat.vg.entrada : "")
                        active: true
                        activeColor: detallePop.col
                        onClicked: { cat.elegido("tarjeta", detallePop.d.comoTarjeta); detallePop.close(); cat.close() }
                    }
                    // De varias entradas: un nodo del estudio, con un puerto por cada una.
                    GlassButton {
                        visible: !!detallePop.d.mezcla
                        text: qsTr("+ Agregar como mezcla")
                        active: true
                        activeColor: detallePop.col
                        onClicked: { cat.elegido("mezcla", detallePop.d.elemento); detallePop.close(); cat.close() }
                        ToolTip.visible: hovered; ToolTip.delay: 400
                        ToolTip.text: qsTr("Un nodo del estudio («Mezcla de GStreamer») con sus entradas: se cablea desde cualquier entrada y sale como una fuente para las escenas")
                    }
                    Text {
                        visible: !detallePop.d.tarjeta && !detallePop.d.comoTarjeta && !detallePop.d.mezcla
                        Layout.fillWidth: true; wrapMode: Text.WordWrap
                        text: detallePop.d.rol === "fuente" ? qsTr("Es una fuente: va como entrada virtual (Entradas → «+ Virtual»), no como tarjeta.")
                              : detallePop.d.rol === "mezcla" ? qsTr("Junta varias entradas, pero no de video crudo: no va como tarjeta ni como mezcla.")
                              : detallePop.d.rol === "salida" ? qsTr("Es una salida de GStreamer: no va como tarjeta.")
                              : detallePop.d.uso ? qsTr("No va como tarjeta de filtro: %1").arg(detallePop.d.uso)
                              : qsTr("No va como tarjeta de filtro: no trabaja sobre la imagen cruda de una entrada.")
                        color: Theme.warn; font.pixelSize: 12
                    }
                    GlassButton { compact: true; text: qsTr("Volver"); onClicked: detallePop.close() }
                }
                Text {
                    visible: (detallePop.d.entradas || []).length > 0
                    Layout.fillWidth: true; Layout.leftMargin: 22; Layout.rightMargin: 22
                    wrapMode: Text.WordWrap
                    text: qsTr("Entradas: %1").arg((detallePop.d.entradas || []).map(e => e.pedido ? qsTr("%1 (a pedido: las que hagan falta)").arg(e.nombre) : e.nombre).join(", "))
                    color: Theme.textDim; font.pixelSize: 11
                }
                Text {
                    visible: cat.detalleProps.length > 0
                    Layout.leftMargin: 22; Layout.topMargin: 6
                    text: qsTr("Propiedades (%1)").arg(cat.detalleProps.length)
                    color: Theme.textDim; font { pixelSize: 11; capitalization: Font.AllUppercase; letterSpacing: 0.5 }
                }
                Repeater {
                    model: cat.detalleProps
                    delegate: RowLayout {
                        required property var modelData
                        Layout.fillWidth: true; Layout.leftMargin: 22; Layout.rightMargin: 22
                        spacing: 12
                        Text { Layout.preferredWidth: 170; text: modelData.name; color: Theme.text; elide: Text.ElideRight; font { pixelSize: 12; family: "Menlo" } }
                        Text {
                            Layout.preferredWidth: 150
                            color: Theme.textFaint; font.pixelSize: 11; elide: Text.ElideRight
                            text: modelData.kind === "list" ? qsTr("opciones: %1").arg((modelData.options || []).length)
                                  : (modelData.kind === "int" || modelData.kind === "float") ? modelData.kind + " " + Number(modelData.min).toPrecision(3) + " … " + Number(modelData.max).toPrecision(3)
                                  : modelData.kind
                        }
                        Text {
                            Layout.fillWidth: true; wrapMode: Text.WordWrap
                            text: modelData.help + (modelData.value !== undefined && modelData.value !== "" ? qsTr("  [de fábrica: %1]").arg(modelData.value) : "")
                            color: Theme.textDim; font.pixelSize: 11
                        }
                    }
                }
                Item { Layout.preferredHeight: 16 }
            }
        }
    }
}
