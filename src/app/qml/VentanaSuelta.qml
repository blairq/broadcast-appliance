// Una cosa del catálogo sacada a su propia ventana, para mandarla a otro
// monitor: una página entera (clic derecho en el riel), un panel (el padrón,
// la cola de preguntas, el Mezclador, la PTZ de una cámara) o una tarjeta del
// tablero (doble clic en la barra de tarjetas).
//
// Antes eran dos ventanas casi iguales (PageWindow y PanelWindow), cada una
// con su copia de la ubicación en un monitor libre. Ahora es una, y lo de
// adentro lo dibuja VistaCatalogo, igual que en los monitores de Salidas →
// Pantallas.
//
// No se saca nada de la ventana principal: se abre OTRA VISTA de lo mismo.
//
//   Esc   cierra la ventana
import QtQuick
import QtQuick.Controls
import QtQuick.Window
import BA
import "controls"

Window {
    id: w
    /// El id del catálogo («pagina:guion», «preguntas», «ptz:Cam 1», «tarjeta:aire»).
    required property string itemId
    /// La ventana principal (le da los componentes a VistaCatalogo).
    required property var principal

    readonly property var cosa: Studio.catalogo.porId[itemId] || null
    readonly property bool esPagina: itemId.startsWith("pagina:")
    /// El id corto de la página (el de ui_show_page), si es una página.
    readonly property string idPagina: esPagina ? itemId.slice(7) : ""

    // Como la encuentran `ventana_listar` y `ventana_mover`: va por
    // `objectName` y no por el título, que es para leer y puede repetirse. Los
    // mismos nombres de siempre («ba:pagina:<id>», «ba:panel:<id>»).
    objectName: esPagina ? "ba:pagina:" + idPagina : "ba:panel:" + (cosa && cosa.dibujo ? cosa.dibujo.panel : itemId)

    title: "Broadcast Appliance — " + (cosa ? cosa.titulo : itemId)
    color: Theme.bg

    // Los controles de Qt, en oscuro. Los colores salen de Theme porque esta
    // misma paleta la necesitan todas las ventanas (ver Theme.qml).
    palette {
        window: Theme.panel
        windowText: Theme.text
        base: Theme.paletaBase
        alternateBase: Theme.paletaAlterna
        text: Theme.text
        button: Theme.paletaBoton
        buttonText: Theme.text
        highlight: Theme.accent
        highlightedText: "#ffffff"
        mid: Theme.paletaMedio
        dark: Theme.paletaOscura
        light: Theme.paletaClara
        placeholderText: Theme.textFaint
        toolTipBase: Theme.paletaBoton
        toolTipText: Theme.text
    }
    // Una página pide el ancho de una pantalla; un panel, menos (así entran dos
    // cómodos en un monitor). Un panel grande (el Mezclador) pide el suyo.
    width: esPagina ? 1180 : (vista.anchoPedido > 0 ? vista.anchoPedido : 620)
    height: esPagina ? 760 : (vista.altoPedido > 0 ? vista.altoPedido : 720)
    minimumWidth: esPagina ? 720 : 380
    minimumHeight: esPagina ? 460 : 320
    visible: true

    signal cerrada(string itemId)
    onClosing: w.cerrada(w.itemId)

    // AL ABRIRSE, A UN MONITOR LIBRE. Sin esto cada ventana que se saca nace
    // encima de la anterior, en el mismo monitor, y hay que acomodarlas una
    // por una. Se busca uno donde no haya ya una ventana nuestra; si están
    // todos ocupados, se queda donde el escritorio la ponga —amontonar en un
    // orden inventado no es mejor que amontonar—.
    Component.onCompleted: {
        const pantallas = Qt.application.screens
        if (pantallas.length < 2) return
        const ocupados = {}
        const vs = Studio.ventanasAbiertas()
        for (let i = 0; i < vs.length; ++i) ocupados[vs[i]] = true
        for (let k = 0; k < pantallas.length; ++k) {
            if (ocupados[pantallas[k].name]) continue
            w.screen = pantallas[k]
            if (w.esPagina && !Studio.puedeUbicarVentanas) w.visibility = Window.FullScreen
            return
        }
    }

    // La misma barra que las ventanas de video: monitor, pantalla completa, cerrar.
    BarraVentana {
        ventana: w
        z: 100
        alCerrar: () => w.close()
    }

    VistaCatalogo {
        id: vista
        anchors { fill: parent; topMargin: w.esPagina ? 0 : 32 }
        itemId: w.itemId
        principal: w.principal
        focus: true
    }

    Shortcut {
        // `sequences`: StandardKey.Cancel tiene varias teclas (Esc y Cmd+.).
        sequences: [StandardKey.Cancel]
        onActivated: w.close()
    }
}
