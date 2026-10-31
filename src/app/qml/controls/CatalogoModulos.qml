// El catálogo de módulos de video: los de GStreamer y los filtros de OBS.
//
// Como el de los filtros de audio (el mismo marco: CatalogoBuscable): búsqueda en
// castellano o en inglés, categorías, y el detalle de cada módulo con su documentación
// (para qué sirve, cómo se usa en vivo; datos/gst-modulos.json), la descripción
// original, el paquete y la licencia, y todas sus propiedades (de la introspección de
// GStreamer). Los de varias entradas (compositor, smpte, glmosaic…) van como «Mezcla de
// GStreamer» del estudio. Los que no van (fuentes, salidas, los internos) se ven igual,
// con por qué no. Datos: Studio.video.modulosGst (con `va` y el color de su categoría).
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import BA

CatalogoBuscable {
    id: cat
    property var vg: Studio.video
    /// Al elegir: tipo "gst" (id = el elemento), "tarjeta" (una propia) o "mezcla" (un módulo de varias entradas).
    signal elegido(string tipo, string id)

    property bool soloTarjetas: true
    /// Solo los de varias entradas (se abre así desde «Mezcla de GStreamer»).
    property bool soloMezclas: false
    onClosed: soloMezclas = false
    function abrirMezclas() { soloMezclas = true; categoria = ""; open() }
    property var detalleProps: []

    /// El título de una tarjeta propia del grafo (comoTarjeta: superres, rostros, seguir).
    function tituloTarjeta(t) {
        const c = vg ? vg.catalogo.find(x => x.tipo === t) : null
        return c ? c.titulo : t
    }
    function verModulo(m) {
        cat.detalleProps = vg ? vg.propiedadesModulo(m.elemento) : []
        cat.verDetalle(m)
    }
    function elegir(m) {
        if (m.comoTarjeta) cat.elegido("tarjeta", m.comoTarjeta)
        else if (m.mezcla) cat.elegido("mezcla", m.elemento)
        else if (m.tarjeta) cat.elegido("gst", m.elemento)
        else return
        cat.cerrarTodo()
    }

    fuente: vg ? vg.modulosGst : []
    titulo: qsTr("Módulos de video")
    contador: qsTr("%1 de %2 · %3").arg(filtro.count).arg(fuente.length).arg(vg ? vg.versionGst : "")
    ayudaBusqueda: qsTr("buscar: «croma», «desentrelazar», «texto», «blur»…")
    pie: qsTr("GStreamer (gstreamer.freedesktop.org), LGPL salvo donde se indica. Viene adentro del programa.")
    sinNada: fuente.length === 0 ? qsTr("No se pudo abrir GStreamer.") : qsTr("Nada coincide con la búsqueda.")
    clave: "elemento"
    campos: ["elemento", "titulo", "nombre", "categoria", "para", "uso", "descripcion", "clase"]
    exigir: (cat.soloTarjetas ? ["va"] : []).concat(cat.soloMezclas ? ["mezcla"] : [])
    ordenarCategorias: true
    opciones: [ { k: "soloTarjetas", t: qsTr("Solo los que se pueden agregar") }, { k: "soloMezclas", t: qsTr("Solo los de varias entradas") } ]
    anchoIzquierda: 240
    celdaMinima: 240
    celdaAlto: 168

    tarjeta: TarjetaCatalogo {
        id: tj
        required property var datos
        width: GridView.view.cellWidth
        height: GridView.view.cellHeight
        margen: 6
        radio: 12
        col: cat.colorDe(datos.categoria)
        opacity: tj.datos.va ? 1 : 0.6
        onClic: cat.verModulo(tj.datos)
        onDobleClic: cat.elegir(tj.datos)
        Rectangle { width: 4; anchors { left: parent.left; top: parent.top; bottom: parent.bottom; margins: 1 } radius: 2; color: tj.col }
        ColumnLayout {
            anchors { fill: parent; margins: 12; leftMargin: 16 }
            spacing: 4
            RowLayout {
                Layout.fillWidth: true
                spacing: 6
                Text { text: tj.datos.categoria; color: tj.col; font { pixelSize: 10; weight: Font.DemiBold } elide: Text.ElideRight; Layout.fillWidth: true }
                Text { visible: !!tj.datos.mezcla; text: qsTr("MEZCLA"); color: "#f472b6"; font { pixelSize: 9; weight: Font.Bold } }
                Text { visible: String(tj.datos.elemento).indexOf("gl") === 0; text: "GPU"; color: "#ef4444"; font { pixelSize: 9; weight: Font.Bold } }
            }
            Text { Layout.fillWidth: true; text: tj.datos.titulo || tj.datos.nombre; color: Theme.text; elide: Text.ElideRight; font { pixelSize: 14; weight: Font.DemiBold } }
            Text { Layout.fillWidth: true; text: tj.datos.elemento; color: Theme.textFaint; elide: Text.ElideRight; font { pixelSize: 10; family: "Menlo" } }
            Text {
                Layout.fillWidth: true; Layout.fillHeight: true
                text: tj.datos.para || tj.datos.descripcion || ""
                wrapMode: Text.WordWrap; elide: Text.ElideRight; maximumLineCount: 3
                color: Theme.textDim; font.pixelSize: 11
            }
        }
    }

    detalle: ScrollView {
        id: det
        readonly property var d: cat.detalleActual || ({})
        readonly property color col: cat.colorDe(d.categoria)
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
                    Text { Layout.fillWidth: true; text: det.d.titulo || det.d.nombre || ""; color: Theme.text; wrapMode: Text.WordWrap; font { pixelSize: 21; weight: Font.DemiBold } }
                    Text { text: (det.d.elemento || "") + "  ·  " + (det.d.nombre || ""); color: Theme.textFaint; font { pixelSize: 11; family: "Menlo" } }
                }
                Text { text: det.d.categoria || ""; color: det.col; font { pixelSize: 12; weight: Font.DemiBold } }
            }
            Text { Layout.fillWidth: true; Layout.leftMargin: 22; Layout.rightMargin: 22; wrapMode: Text.WordWrap; text: det.d.para || ""; color: Theme.text; font.pixelSize: 14 }
            Rectangle {
                visible: !!det.d.uso
                Layout.fillWidth: true; Layout.leftMargin: 22; Layout.rightMargin: 22
                implicitHeight: usoTxt.implicitHeight + 18
                radius: 10
                color: Qt.rgba(1, 0.42, 0.17, 0.1)
                border.color: Qt.rgba(1, 0.42, 0.17, 0.4)
                Text { id: usoTxt; anchors { fill: parent; margins: 9 } wrapMode: Text.WordWrap; textFormat: Text.MarkdownText; text: qsTr("**En vivo:** %1").arg(det.d.uso || ""); color: Theme.text; font.pixelSize: 12 }
            }
            Text {
                Layout.fillWidth: true; Layout.leftMargin: 22; Layout.rightMargin: 22
                wrapMode: Text.WordWrap; visible: !!det.d.descripcion
                text: qsTr("«%1» — %2").arg(det.d.descripcion || "").arg(det.d.clase || "GStreamer")
                color: Theme.textFaint; font { pixelSize: 11; italic: true }
            }
            Text {
                Layout.leftMargin: 22
                text: [det.d.paquete, det.d.plugin ? qsTr("plugin %1").arg(det.d.plugin) : "", det.d.licencia ? qsTr("licencia %1").arg(det.d.licencia) : ""].filter(x => !!x).join("  ·  ")
                color: Theme.textFaint; font.pixelSize: 11
            }
            RowLayout {
                Layout.fillWidth: true; Layout.leftMargin: 22; Layout.rightMargin: 22
                spacing: 10
                GlassButton {
                    visible: !!det.d.tarjeta
                    text: qsTr("+ Agregar a «%1»").arg(cat.vg ? cat.vg.entrada : "")
                    active: true
                    activeColor: det.col
                    onClicked: { cat.elegido("gst", det.d.elemento); cat.cerrarTodo() }
                }
                // Los nuestros que van con una tarjeta propia del grafo (superres, rostros, seguir).
                GlassButton {
                    visible: !!det.d.comoTarjeta
                    text: qsTr("+ Agregar la tarjeta «%1» a «%2»").arg(cat.tituloTarjeta(det.d.comoTarjeta)).arg(cat.vg ? cat.vg.entrada : "")
                    active: true
                    activeColor: det.col
                    onClicked: { cat.elegido("tarjeta", det.d.comoTarjeta); cat.cerrarTodo() }
                }
                // De varias entradas: un nodo del estudio, con un puerto por cada una.
                GlassButton {
                    visible: !!det.d.mezcla
                    text: qsTr("+ Agregar como mezcla")
                    active: true
                    activeColor: det.col
                    onClicked: { cat.elegido("mezcla", det.d.elemento); cat.cerrarTodo() }
                    ToolTip.visible: hovered; ToolTip.delay: 400
                    ToolTip.text: qsTr("Un nodo del estudio («Mezcla de GStreamer») con sus entradas: se cablea desde cualquier entrada y sale como una fuente para las escenas")
                }
                Text {
                    visible: !det.d.va
                    Layout.fillWidth: true; wrapMode: Text.WordWrap
                    text: det.d.rol === "fuente" ? qsTr("Es una fuente: va como entrada virtual (Entradas → «+ Virtual»), no como tarjeta.")
                          : det.d.rol === "mezcla" ? qsTr("Junta varias entradas, pero no de video crudo: no va como tarjeta ni como mezcla.")
                          : det.d.rol === "salida" ? qsTr("Es una salida de GStreamer: no va como tarjeta.")
                          : det.d.uso ? qsTr("No va como tarjeta de filtro: %1").arg(det.d.uso)
                          : qsTr("No va como tarjeta de filtro: no trabaja sobre la imagen cruda de una entrada.")
                    color: Theme.warn; font.pixelSize: 12
                }
                GlassButton { compact: true; text: qsTr("Volver"); onClicked: cat.cerrarDetalle() }
            }
            Text {
                visible: (det.d.entradas || []).length > 0
                Layout.fillWidth: true; Layout.leftMargin: 22; Layout.rightMargin: 22
                wrapMode: Text.WordWrap
                text: qsTr("Entradas: %1").arg((det.d.entradas || []).map(e => e.pedido ? qsTr("%1 (a pedido: las que hagan falta)").arg(e.nombre) : e.nombre).join(", "))
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
