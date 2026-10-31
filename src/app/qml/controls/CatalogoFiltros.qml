// El catálogo de filtros de audio (LSP): búsqueda avanzada y detalle.
//
// Cada familia (compresor, compuerta, EQ paramétrico...) con su captura real
// (del código fuente de LSP, LGPL-3.0: assets/lsp-capturas), qué hace en
// castellano, para qué sirve en vivo y sus variantes (mono, estéreo, L/R,
// M/S, con sidechain, cantidad de bandas). Elegir una variante la agrega al
// rack de la fuente abierta. Datos: AudioViewModel.catalogoLsp() (con el color
// de cada categoría); el marco, la búsqueda y las categorías: CatalogoBuscable.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import BA

CatalogoBuscable {
    id: cat
    property var au: Studio.audio
    /// Al elegir una variante: (id del plugin).
    signal elegido(string id)

    property bool soloVivo: false
    property bool soloEstereo: false
    property bool soloSidechain: false
    property bool conInstrumentos: false
    onOpened: fuente = au ? au.catalogoLsp() : []

    titulo: qsTr("Filtros de audio")
    contador: qsTr("%1 de %n familias", "", filtro.total).arg(filtro.count)
    ayudaBusqueda: qsTr("buscar: «voz», «ruido», «graves», «limiter»…")
    pie: qsTr("Plugins y capturas: Linux Studio Plugins (lsp-plug.in), LGPL-3.0.")
    sinNada: fuente.length === 0 ? qsTr("No se pudieron abrir los módulos LSP.") : qsTr("Nada coincide con la búsqueda.")
    clave: "clave"
    // Busca en castellano y en inglés: nombre, etiquetas, descripción, la original y las variantes.
    campos: ["nombre", "clave", "categoria", "etiquetas", "descripcion", "usoVivo", "descripcionOriginal", "variantes"]
    exigir: (cat.conInstrumentos ? [] : ["esFiltro"]).concat(cat.soloVivo ? ["vivo"] : [])
                                                        .concat(cat.soloEstereo ? ["estereo"] : [])
                                                        .concat(cat.soloSidechain ? ["sidechain"] : [])
    opciones: [ { k: "soloVivo", t: qsTr("Recomendados para vivo") }, { k: "soloEstereo", t: qsTr("Con versión estéreo") },
                { k: "soloSidechain", t: qsTr("Con sidechain") }, { k: "conInstrumentos", t: qsTr("Incluir instrumentos") } ]
    celdaMinima: 250
    celdaAlto: 250

    tarjeta: TarjetaCatalogo {
        id: tj
        required property var datos
        width: GridView.view.cellWidth
        height: GridView.view.cellHeight
        col: cat.colorDe(datos.categoria)
        onClic: cat.verDetalle(tj.datos)
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
                    visible: !!tj.datos.captura
                    source: tj.datos.captura || ""
                    fillMode: Image.PreserveAspectCrop
                    asynchronous: true
                    smooth: true
                    scale: tj.encima ? 1.06 : 1
                    Behavior on scale { NumberAnimation { duration: 220; easing.type: Easing.OutCubic } }
                }
                IlustracionFiltro {
                    anchors.fill: parent
                    visible: !tj.datos.captura
                    tipo: tj.datos.ilustracion
                    color: tj.col
                }
                Rectangle {
                    anchors { left: parent.left; top: parent.top; margins: 6 }
                    radius: 6; height: 18; width: catTxt.implicitWidth + 12
                    color: Qt.rgba(0, 0, 0, 0.65)
                    Text { id: catTxt; anchors.centerIn: parent; text: tj.datos.categoria; color: tj.col; font { pixelSize: 10; weight: Font.DemiBold } }
                }
                Rectangle {
                    visible: !!tj.datos.vivo
                    anchors { right: parent.right; top: parent.top; margins: 6 }
                    radius: 6; height: 18; width: vivoTxt.implicitWidth + 12
                    color: Qt.rgba(0.94, 0.27, 0.27, 0.85)
                    Text { id: vivoTxt; anchors.centerIn: parent; text: qsTr("VIVO"); color: "white"; font { pixelSize: 9; weight: Font.Bold } }
                }
            }
            Text { Layout.fillWidth: true; text: tj.datos.nombre; color: Theme.text; elide: Text.ElideRight; font { pixelSize: 14; weight: Font.DemiBold } }
            Text {
                Layout.fillWidth: true; Layout.fillHeight: true
                text: tj.datos.descripcion
                wrapMode: Text.WordWrap; elide: Text.ElideRight; maximumLineCount: 3
                color: Theme.textDim; font.pixelSize: 11
            }
            Text {
                text: qsTr("%n variante", "", (tj.datos.variantes || []).length)
                color: Theme.textFaint; font.pixelSize: 10
            }
        }
    }

    detalle: ColumnLayout {
        id: det
        readonly property var d: cat.detalleActual || ({ variantes: [] })
        readonly property color col: cat.colorDe(d.categoria)
        spacing: 0
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: parent.height * 0.52
            color: "#0b0b0d"
            radius: Theme.radius
            clip: true
            Image {
                anchors { fill: parent; margins: 12 }
                visible: !!det.d.captura
                source: det.d.captura || ""
                fillMode: Image.PreserveAspectFit
                smooth: true
            }
            IlustracionFiltro {
                anchors { fill: parent; margins: 12 }
                visible: !det.d.captura
                tipo: det.d.ilustracion || "eq"
                color: det.col
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
                    Text { Layout.fillWidth: true; text: det.d.nombre || ""; color: Theme.text; font { pixelSize: 20; weight: Font.DemiBold } }
                    Text { text: det.d.categoria || ""; color: det.col; font { pixelSize: 12; weight: Font.DemiBold } }
                }
                Text { Layout.fillWidth: true; Layout.leftMargin: 20; Layout.rightMargin: 20; wrapMode: Text.WordWrap; text: det.d.descripcion || ""; color: Theme.text; font.pixelSize: 13 }
                Rectangle {
                    Layout.fillWidth: true; Layout.leftMargin: 20; Layout.rightMargin: 20
                    implicitHeight: usoTxt.implicitHeight + 16
                    radius: 10
                    color: Qt.rgba(1, 0.42, 0.17, 0.1)
                    border.color: Qt.rgba(1, 0.42, 0.17, 0.4)
                    Text { id: usoTxt; anchors { fill: parent; margins: 8 } wrapMode: Text.WordWrap; text: qsTr("En vivo: %1").arg(det.d.usoVivo || ""); color: Theme.text; font.pixelSize: 12 }
                }
                Text {
                    Layout.fillWidth: true; Layout.leftMargin: 20; Layout.rightMargin: 20
                    wrapMode: Text.WordWrap; visible: !!det.d.descripcionOriginal
                    text: qsTr("«%1» — LSP").arg(det.d.descripcionOriginal || "")
                    color: Theme.textFaint; font { pixelSize: 11; italic: true }
                }
                Text { Layout.leftMargin: 20; text: qsTr("Agregar a «%1»").arg(cat.au ? cat.au.source : ""); color: Theme.textDim; font { pixelSize: 11; capitalization: Font.AllUppercase; letterSpacing: 0.5 } }
                Flow {
                    Layout.fillWidth: true; Layout.leftMargin: 20; Layout.rightMargin: 20
                    spacing: 6
                    Repeater {
                        model: det.d.variantes
                        delegate: GlassButton {
                            required property var modelData
                            compact: true
                            text: "+ " + modelData.etiqueta
                            active: true
                            activeColor: det.col
                            onClicked: { cat.elegido(modelData.id); cat.cerrarTodo() }
                        }
                    }
                }
                Item { Layout.preferredHeight: 14 }
            }
        }
    }
}
