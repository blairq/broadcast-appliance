// Un catálogo para elegir de a una cosa, con búsqueda y categorías: el marco común de
// los filtros de audio (CatalogoFiltros) y los módulos de video (CatalogoModulos), que
// eran dos copias casi iguales. A la izquierda la búsqueda, las condiciones y las
// categorías (con su color, que viene en el dato); a la derecha la grilla de tarjetas; y
// el detalle de una, encima. Lo que se ve lo filtra el modelo (FiltroCatalogo).
//
// Quien lo usa pone el catálogo (`fuente`, `campos`, `exigir`), las condiciones
// (`opciones`: [{k, t}], interruptores atados a propiedades suyas con ese nombre), la
// tarjeta (`tarjeta`: un delegado con `required property var datos`, normalmente una
// TarjetaCatalogo) y el detalle (`detalle`: lo de adentro, que lee `detalleActual`).
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import BA

Popup {
    id: cat
    property string titulo: ""
    property string ayudaBusqueda: ""
    property string pie: ""
    /// «12 de 40» (o lo que quiera decir quien lo usa).
    property string contador: qsTr("%1 de %2").arg(filtro.count).arg(filtro.total)
    property string sinNada: qsTr("Nada coincide con la búsqueda.")
    property alias fuente: filtro.fuente
    property alias clave: filtro.clave
    property alias campos: filtro.campos
    property alias exigir: filtro.exigir
    property alias ordenarCategorias: filtro.ordenarCategorias
    property alias categoria: filtro.categoria
    readonly property alias filtro: filtro
    property var opciones: []
    property Component tarjeta: null
    property Component detalle: null
    property real celdaMinima: 250
    property real celdaAlto: 250
    property real anchoIzquierda: 230
    property var detalleActual: null

    function colorDe(c) { return filtro.colores[c] || Theme.accent }
    function verDetalle(d) { cat.detalleActual = d; detallePop.open() }
    function cerrarDetalle() { detallePop.close() }
    function cerrarTodo() { detallePop.close(); cat.close() }

    modal: true
    anchors.centerIn: Overlay.overlay
    width: Math.min(1180, (Overlay.overlay ? Overlay.overlay.width : 1200) - 80)
    height: Math.min(820, (Overlay.overlay ? Overlay.overlay.height : 860) - 60)
    padding: 0
    background: Rectangle { color: Theme.panel; radius: Theme.radius; border.color: Theme.glassBorder }
    onOpened: busqueda.forceActiveFocus()

    FiltroCatalogo {
        id: filtro
        texto: busqueda.text
    }

    RowLayout {
        anchors.fill: parent
        anchors.margins: 18
        spacing: 18

        // --- Búsqueda ---------------------------------------------------------------------
        ColumnLayout {
            // Ancho fijo: sus hijos con fillWidth (búsqueda, categorías) si no
            // la estiran a todo el ancho y la grilla queda en 0 (no se veía nada).
            Layout.preferredWidth: cat.anchoIzquierda
            Layout.minimumWidth: cat.anchoIzquierda
            Layout.maximumWidth: cat.anchoIzquierda
            Layout.fillWidth: false
            Layout.fillHeight: true
            spacing: 10
            Text { text: cat.titulo; color: Theme.text; font { pixelSize: 19; weight: Font.DemiBold } }
            Text { text: cat.contador; color: Theme.textDim; font.pixelSize: 11; wrapMode: Text.WordWrap; Layout.fillWidth: true }
            TextField {
                id: busqueda
                Layout.fillWidth: true
                placeholderText: cat.ayudaBusqueda
            }
            Repeater {
                model: cat.opciones
                delegate: RowLayout {
                    required property var modelData
                    spacing: 8
                    Toggle { checked: cat[modelData.k]; onToggled: (w) => { cat[modelData.k] = w; cat.categoria = "" } }
                    Text { text: modelData.t; color: Theme.text; font.pixelSize: 12 }
                }
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
                        model: filtro.categorias
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
                visible: cat.pie !== ""
                color: Theme.textFaint; font.pixelSize: 10
                text: cat.pie
            }
            GlassButton { compact: true; text: qsTr("Cerrar"); onClicked: cat.close() }
        }

        // --- Grilla de tarjetas ---------------------------------------------------------
        GridView {
            id: grilla
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            cellWidth: Math.max(cat.celdaMinima, width / Math.max(1, Math.floor(width / (cat.celdaMinima + 20))))
            cellHeight: cat.celdaAlto
            model: filtro
            delegate: cat.tarjeta
            ScrollBar.vertical: ScrollBar {}
            Text {
                anchors.centerIn: parent
                visible: grilla.count === 0
                text: cat.sinNada
                color: Theme.textDim; font.pixelSize: 14
            }
        }
    }

    // --- El detalle de una ------------------------------------------------------------------
    Popup {
        id: detallePop
        modal: true
        anchors.centerIn: parent
        width: Math.min(900, cat.width - 60)
        height: Math.min(720, cat.height - 40)
        padding: 0
        background: Rectangle {
            color: "#141417"; radius: Theme.radius
            border.color: cat.colorDe(cat.detalleActual ? cat.detalleActual.categoria : "")
        }
        Loader {
            anchors.fill: parent
            active: detallePop.visible
            sourceComponent: cat.detalle
        }
    }
}
