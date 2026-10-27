// Una cosa del catálogo (Studio.catalogo), dibujada: un panel, una tarjeta del
// tablero, una página entera, el programa, una escena, una multivista o lo de
// fondo del tablero. Es lo que va adentro de una ventana suelta (VentanaSuelta)
// y de cada celda de un monitor (PantallaWindow): UNA sola manera de dibujar
// cada cosa, sea donde sea.
//
// Como con las ventanas de siempre, no se saca nada de la ventana principal:
// es OTRA VISTA de lo mismo. Todo lee su estado de los `Studio.*`, así que las
// copias muestran lo mismo y se actualizan juntas.
//
// Los componentes de las páginas y los paneles los conoce Main.qml (es quien
// arma el StackLayout): se los pide por `principal`.
import QtQuick
import BA

Item {
    id: v
    property string itemId: ""
    /// La ventana principal (Main.qml): componenteDePagina(i) y componenteDePanel(id).
    property var principal: null
    /// Un panel, con su marco y su título (en una ventana suelta sí; en una
    /// grilla de un monitor también, para saber qué es cada cosa).
    property bool conMarco: true
    /// Se enlaza al catálogo: si la escena se renombra o el fondo del tablero
    /// cambia, lo que se dibuja cambia solo.
    readonly property var cosa: Studio.catalogo.porId[itemId] || null
    readonly property var dibujo: cosa ? cosa.dibujo : null
    readonly property string modo: dibujo && dibujo.modo ? dibujo.modo : ""
    /// Lo que pide medir (un panel grande, como el Mezclador): lo usa VentanaSuelta.
    property int anchoPedido: 0
    property int altoPedido: 0

    Loader {
        id: lugar
        anchors.fill: parent
        // Un Loader es un ámbito de foco: sin esto lo de adentro no recibe el teclado.
        focus: true
        sourceComponent: v.modo === "video" || v.modo === "multiview" ? cmpVideo
                       : v.modo === "panel" ? cmpPanel
                       : v.modo === "pagina" ? cmpPagina
                       : cmpFalta
    }

    Component {
        id: cmpVideo
        VistaVideo {
            claves: v.modo === "video" ? (v.dibujo.claves || []) : []
            distribucion: v.modo === "video" ? (v.dibujo.distribucion || "grid") : "grid"
            multiview: v.modo === "multiview" ? (v.dibujo.id || "*") : ""
        }
    }

    Component {
        id: cmpPanel
        Rectangle {
            color: Theme.bg
            Panel {
                id: marco
                anchors { fill: parent; margins: v.conMarco ? 12 : 0 }
                title: v.conMarco && v.cosa ? v.cosa.titulo : ""
                color: v.conMarco ? Qt.rgba(1, 1, 1, 0.04) : "transparent"
                border.width: v.conMarco ? 1 : 0
                Loader {
                    anchors.fill: parent
                    focus: true
                    sourceComponent: v.principal && v.dibujo ? v.principal.componenteDePanel(v.dibujo.panel) : null
                    onLoaded: {
                        if (!item) return
                        // Los paneles con algo propio (la PTZ: «ptz:<entrada>», una
                        // tarjeta: «tarjeta:<kind>») lo leen de su id.
                        if (item.panelId !== undefined) item.panelId = v.dibujo.panel
                        // Ya está afuera: que no ofrezca sacarlo otra vez.
                        if (item.enVentana !== undefined) item.enVentana = true
                        if (item.anchoVentana !== undefined) v.anchoPedido = item.anchoVentana
                        if (item.altoVentana !== undefined) v.altoPedido = item.altoVentana
                    }
                }
            }
        }
    }

    Component {
        id: cmpPagina
        Loader {
            focus: true
            sourceComponent: v.principal && v.dibujo ? v.principal.componenteDePagina(v.dibujo.pagina) : null
            // Sin el riel al costado la página no necesita dejarle lugar.
            onLoaded: {
                if (item && item.leftInset !== undefined) item.leftInset = 14
                if (item && item.enVentana !== undefined) item.enVentana = true // la página sabe que está afuera
            }
        }
    }

    // Lo recordado que ya no está (una escena borrada, una cámara que se fue).
    Component {
        id: cmpFalta
        Rectangle {
            color: Theme.bg
            Text {
                anchors.centerIn: parent
                width: parent.width - 40
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
                text: v.itemId === "" ? "" : qsTr("%1\nElegí otra cosa en Salidas → Pantallas.").arg(Studio.catalogo.tituloDe(v.itemId))
                color: Theme.textDim; font.pixelSize: 16
            }
        }
    }
}
