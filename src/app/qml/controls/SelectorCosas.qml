// Elegir QUÉ mostrar (en un monitor, en una ventana de una distribución, en una TV):
// un modal con un cuadro de búsqueda, botones por tipo y una vista previa de la elegida.
// Reemplaza a los ComboBox con todo el catálogo, que con muchas escenas, invitados y
// distribuciones ya no se podían usar.
//
// Todo lo hace el modelo (BuscadorCatalogo, en C++, sobre Studio.catalogo): filtrar
// lo escrito, el tipo, lo que va a un monitor o a una TV, contar por tipo y saber cuál
// está elegida. La lista es una ListView (solo arma las filas que se ven) y la vista
// previa se enlaza a `buscador.actual`.
//
// Uso:
//     SelectorCosas {
//         id: elegir
//         donde: "monitor"                        // "monitor" | "red" | "todo"
//         excluir: ["distribucion"]               // tipos que no se ofrecen
//         textoAceptar: qsTr("Poner")
//         textoOtra: qsTr("Sumar")                // opcional: un segundo botón
//         onElegida: (itemId, otra) => …
//     }
//     elegir.abrir(loQueYaMuestra)
//
// Teclado: escribir busca; ↑/↓ mueven la elegida; Enter acepta; Esc cierra.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
import BA

Popup {
    id: sel
    property string titulo: qsTr("Qué mostrar")
    property alias donde: buscador.donde
    property alias excluir: buscador.excluir
    property string textoAceptar: qsTr("Elegir")
    /// Un segundo botón (vacío = no hay): «Sumar» en un monitor que ya muestra algo.
    property string textoOtra: ""
    property bool otraHabilitada: true
    readonly property alias buscador: buscador
    /// La ventana principal (para la vista previa de páginas y paneles): la de esta
    /// ventana si es la principal, o la que le pasó una ventana suelta.
    readonly property var principal: {
        const w = Window.window
        if (!w) return null
        return w.componenteDePagina !== undefined ? w : (w.principal || null)
    }

    /// Lo eligió: `otra` = con el segundo botón.
    signal elegida(string itemId, bool otra)

    /// Abre con esa ya elegida (lo que muestra ahora), la búsqueda vacía y todos los tipos.
    function abrir(actual) {
        busqueda.text = ""
        buscador.grupo = ""
        buscador.filtrarYa()
        buscador.vista = ""
        buscador.elegido = actual || ""
        sel.open()
    }
    function aceptar(otra) {
        // Nada elegido, o lo elegido ya no está en el catálogo.
        if (buscador.elegido === "" || buscador.actual.itemId === undefined) return
        const id = buscador.elegido
        sel.close()
        sel.elegida(id, otra === true)
    }

    modal: true
    focus: true
    parent: Overlay.overlay
    anchors.centerIn: Overlay.overlay
    width: Math.min(1100, (Overlay.overlay ? Overlay.overlay.width : 1200) - 60)
    height: Math.min(760, (Overlay.overlay ? Overlay.overlay.height : 800) - 60)
    padding: 0
    background: Rectangle { color: Theme.panel; radius: Theme.radiusSmall; border.color: Theme.glassBorder }
    onOpened: {
        busqueda.forceActiveFocus()
        if (buscador.filaElegida >= 0) lista.positionViewAtIndex(buscador.filaElegida, ListView.Contain)
    }

    BuscadorCatalogo {
        id: buscador
        // Solo mientras el selector está abierto: cerrado no mira el catálogo (hay uno por
        // página que lo usa y el catálogo cambia con cada invitado que entra).
        fuente: sel.visible ? Studio.catalogo : null
        texto: busqueda.text
        // Otra elegida (al abrir, con lo que ya muestra; con las flechas): que se vea en la
        // lista. Solo cuando cambia CUÁL: si el catálogo cambia, no se le mueve la lista a
        // quien está mirando más abajo.
        property string vista: ""
        onElegidoCambio: {
            if (buscador.elegido === buscador.vista || buscador.filaElegida < 0) return
            buscador.vista = buscador.elegido
            lista.positionViewAtIndex(buscador.filaElegida, ListView.Contain)
        }
    }

    RowLayout {
        anchors { fill: parent; margins: 16 }
        spacing: 16

        // --- Búsqueda, tipos y lista --------------------------------------------------------
        ColumnLayout {
            Layout.preferredWidth: 400
            Layout.minimumWidth: 300
            Layout.fillHeight: true
            spacing: 8

            RowLayout {
                Layout.fillWidth: true
                Text { text: sel.titulo; color: Theme.text; font { pixelSize: 17; weight: Font.DemiBold } }
                Item { Layout.fillWidth: true }
                Text {
                    text: qsTr("%1 de %2").arg(buscador.cantidad).arg(buscador.total)
                    color: Theme.textDim; font.pixelSize: 11
                }
            }
            TextField {
                id: busqueda
                Layout.fillWidth: true
                placeholderText: qsTr("Buscar: escena, invitado, página, distribución…")
                color: Theme.text
                Keys.onDownPressed: buscador.mover(1)
                Keys.onUpPressed: buscador.mover(-1)
                Keys.onReturnPressed: { buscador.filtrarYa(); if (buscador.filaElegida < 0) buscador.mover(0); sel.aceptar(false) }
                Keys.onEnterPressed: { buscador.filtrarYa(); if (buscador.filaElegida < 0) buscador.mover(0); sel.aceptar(false) }
                Keys.onEscapePressed: sel.close()
            }
            // Por tipo: los que hay, con cuántos coinciden con lo escrito.
            Flow {
                Layout.fillWidth: true
                spacing: 5
                GlassButton { compact: true; text: qsTr("Todo"); active: buscador.grupo === ""; onClicked: buscador.grupo = "" }
                Repeater {
                    model: buscador.grupos
                    delegate: GlassButton {
                        required property var modelData
                        compact: true
                        visible: modelData.cuenta > 0 || buscador.grupo === modelData.grupo
                        text: modelData.titulo + " · " + modelData.cuenta
                        active: buscador.grupo === modelData.grupo
                        onClicked: buscador.grupo = buscador.grupo === modelData.grupo ? "" : modelData.grupo
                    }
                }
            }
            ListView {
                id: lista
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                spacing: 2
                model: buscador
                currentIndex: buscador.filaElegida
                highlightFollowsCurrentItem: false
                reuseItems: true
                ScrollBar.vertical: ScrollBar {}
                delegate: Rectangle {
                    id: fila
                    required property int index
                    required property string itemId
                    required property string titulo
                    required property string grupoTitulo
                    required property string icono
                    width: lista.width
                    height: 36
                    radius: 6
                    readonly property bool elegida: buscador.elegido === fila.itemId
                    color: elegida ? Qt.rgba(1, 0.42, 0.17, 0.18) : (raton.containsMouse ? Qt.rgba(1, 1, 1, 0.04) : "transparent")
                    border.color: elegida ? Theme.accent : "transparent"
                    MouseArea {
                        id: raton
                        anchors.fill: parent
                        hoverEnabled: true
                        onClicked: buscador.elegirFila(fila.index)
                        onDoubleClicked: { buscador.elegirFila(fila.index); sel.aceptar(false) }
                    }
                    Row {
                        anchors { left: parent.left; right: parent.right; verticalCenter: parent.verticalCenter; leftMargin: 10; rightMargin: 10 }
                        spacing: 8
                        Text {
                            width: 20; horizontalAlignment: Text.AlignHCenter
                            text: fila.icono; color: Theme.textDim; font.pixelSize: 13
                        }
                        Text {
                            width: parent.width - 20 - 110 - 16
                            text: fila.titulo; elide: Text.ElideRight
                            color: Theme.text; font.pixelSize: 13
                        }
                        Text {
                            width: 110; horizontalAlignment: Text.AlignRight
                            text: fila.grupoTitulo; elide: Text.ElideRight
                            color: Theme.textFaint; font.pixelSize: 10
                            anchors.verticalCenter: parent.verticalCenter
                        }
                    }
                }
                Text {
                    anchors.centerIn: parent
                    visible: lista.count === 0
                    width: parent.width - 30
                    horizontalAlignment: Text.AlignHCenter; wrapMode: Text.WordWrap
                    text: buscador.total === 0 ? qsTr("No hay nada para mostrar acá todavía.") : qsTr("Nada coincide con la búsqueda.")
                    color: Theme.textDim; font.pixelSize: 13
                }
            }
        }

        // --- La vista previa y qué es --------------------------------------------------------
        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 10

            PreviewCosa {
                id: preview
                Layout.fillWidth: true
                Layout.fillHeight: true
                cosa: buscador.elegido !== "" && buscador.actual.itemId !== undefined ? buscador.actual : null
                principal: sel.principal
                // Lo armado no se queda con el teclado: sigue en la búsqueda.
                onArmada: if (sel.opened) busqueda.forceActiveFocus()
            }
            Text {
                Layout.fillWidth: true
                text: preview.cosa ? preview.cosa.titulo : ""
                color: Theme.text; font { pixelSize: 15; weight: Font.DemiBold }
                elide: Text.ElideRight
            }
            Text {
                Layout.fillWidth: true
                visible: preview.cosa !== null
                text: preview.cosa ? preview.cosa.grupoTitulo + (preview.cosa.dibujo && preview.cosa.dibujo.resumen ? " · " + preview.cosa.dibujo.resumen : "") : ""
                color: Theme.textDim; font.pixelSize: 11
                elide: Text.ElideRight
            }
            // La elegida se borró mientras estaba abierto (otro operador, el MCP).
            Text {
                Layout.fillWidth: true
                visible: buscador.elegido !== "" && buscador.actual.itemId === undefined
                text: qsTr("Lo elegido ya no existe: elegí otra cosa.")
                color: Theme.warn; font.pixelSize: 11
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                Item { Layout.fillWidth: true }
                GlassButton { compact: true; text: qsTr("Cancelar"); onClicked: sel.close() }
                GlassButton {
                    compact: true
                    visible: sel.textoOtra !== ""
                    text: sel.textoOtra; icon: "+"
                    enabled: preview.cosa !== null && sel.otraHabilitada
                    onClicked: sel.aceptar(true)
                }
                GlassButton {
                    compact: true; active: true
                    text: sel.textoAceptar
                    enabled: preview.cosa !== null
                    onClicked: sel.aceptar(false)
                }
            }
        }
    }
}
