// Una ventana suelta: una cosa del catálogo (o varias) en su propia ventana,
// para mandarla a otro monitor. El programa, una escena, una multivista, un
// invitado, lo de fondo del tablero, una página entera (clic derecho en el
// riel), un panel (el padrón, la cola de preguntas, el Mezclador, la PTZ de
// una cámara) o una tarjeta del tablero (doble clic en la barra de tarjetas).
//
// SE RECUERDA. Su estado vive en Studio.ventanas (pantallas/Ventanas.hpp):
// qué muestra, a qué monitor fue (por su id estable, el de Salidas →
// Pantallas), su tamaño y si estaba en pantalla completa. Al reiniciar vuelve
// igual. Antes había dos ventanas para esto —la de video (OutputWindow), que se
// recordaba, y esta, que no— con dos barras y dos maneras de ubicarse.
//
// No se saca nada de la ventana principal: se abre OTRA VISTA de lo mismo. Lo
// de adentro lo dibuja VistaCosas, igual que en los monitores.
//
//   doble clic   pantalla completa / ventana (con video)
//   Esc          sale de pantalla completa; si no, cierra
import QtQuick
import QtQuick.Controls
import QtQuick.Window
import BA
import "controls"

Window {
    id: w
    LayoutMirroring.enabled: Qt.application.layoutDirection === Qt.RightToLeft
    LayoutMirroring.childrenInherit: true
    required property int ventanaId
    required property var cosas
    required property string distribucion
    /// El id estable del monitor (Salidas → Pantallas), o vacío.
    required property string monitor
    /// Solo lo migrado de las ventanas viejas: el nombre del sistema del monitor.
    required property string sistema
    required property bool pantallaCompleta
    required property rect geometria
    required property string titulo
    /// La ventana principal (le da los componentes a VistaCatalogo).
    required property var principal

    readonly property var cosa: cosas.length === 1 ? (Studio.catalogo.porId[cosas[0]] || null) : null
    readonly property bool esPagina: cosas.length === 1 && cosas[0].startsWith("pagina:")
    readonly property bool esPanel: !!cosa && !!cosa.dibujo && cosa.dibujo.modo === "panel"
    readonly property bool esMultiview: cosas.length === 1 && cosas[0].startsWith("multiview:")
    /// Video (o una multivista): fondo negro, doble clic a pantalla completa.
    readonly property bool deVideo: vista.soloVideo || esMultiview
    /// El id corto de la página (el de ui_show_page), si es una página.
    readonly property string idPagina: esPagina ? cosas[0].slice(7) : ""

    // Como la encuentran `ventana_listar` y `ventana_mover`: va por `objectName`
    // y no por el título, que es para leer y puede repetirse. Los nombres de
    // siempre: «ba:pagina:<id>», «ba:panel:<id>» y, para el video, «ba:salida:<n>».
    objectName: esPagina ? "ba:pagina:" + idPagina
              : esPanel ? "ba:panel:" + cosa.dibujo.panel
              : "ba:salida:" + ventanaId

    title: "Broadcast Appliance — " + titulo
    color: deVideo ? "black" : Theme.bg

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
    // Sin tamaño recordado: una página pide el ancho de una pantalla; un panel,
    // menos (así entran dos cómodos en un monitor), o el suyo si es grande (el
    // Mezclador); el video, 16:9.
    width: esPagina ? 1180 : deVideo ? 960 : (vista.anchoPedido > 0 ? vista.anchoPedido : 620)
    height: esPagina ? 760 : deVideo ? 540 : (vista.altoPedido > 0 ? vista.altoPedido : 720)
    minimumWidth: esPagina ? 720 : deVideo ? 320 : 380
    minimumHeight: esPagina ? 460 : deVideo ? 180 : 320
    visible: true

    /// Para ventana_mover (MCP): normal | maximizada | pantalla-completa | minimizada.
    function ponerEstado(e) {
        if (e === "minimizada") { w.showMinimized(); return }
        w.visibility = e === "pantalla-completa" ? Window.FullScreen : e === "maximizada" ? Window.Maximized : Window.Windowed
    }

    // --- Dónde va ---------------------------------------------------------------------
    /// El QScreen de su monitor (por id estable; lo migrado, por nombre), o null.
    function pantallaRecordada() {
        const ss = Qt.application.screens
        for (let i = 0; i < ss.length; ++i) {
            if (w.monitor !== "" && barra.idDeMonitor(ss[i]) === w.monitor) return ss[i]
            if (w.monitor === "" && w.sistema !== "" && ss[i].name === w.sistema) return ss[i]
        }
        return null
    }
    /// ¿El centro de `r` cae en algún monitor de los que hay? (Si el monitor de la
    /// geometría recordada no está, la ventana quedaría fuera de la vista.)
    function seVe(r) {
        const ss = Qt.application.screens
        const cx = r.x + r.width / 2, cy = r.y + r.height / 2
        for (let i = 0; i < ss.length; ++i)
            if (cx >= ss[i].virtualX && cx < ss[i].virtualX + ss[i].width &&
                cy >= ss[i].virtualY && cy < ss[i].virtualY + ss[i].height) return true
        return false
    }
    function centrarEn(s) {
        w.screen = s
        const ww = Math.min(w.width, s.width - 40), hh = Math.min(w.height, s.height - 80)
        w.width = ww
        w.height = hh
        w.x = s.virtualX + Math.round((s.width - ww) / 2)
        w.y = s.virtualY + Math.round((s.height - hh) / 2)
    }
    /// Ya se ubicó: desde acá, lo que cambie la ventana se guarda en el modelo.
    property bool listo: false
    Component.onCompleted: {
        const s = w.pantallaRecordada()
        const g = w.geometria
        if (g.width > 0 && g.height > 0 && w.seVe(g)) {
            if (s) w.screen = s
            w.x = g.x; w.y = g.y; w.width = g.width; w.height = g.height
        } else if (s) {
            w.centrarEn(s)
        } else {
            // AL ABRIRSE, A UN MONITOR LIBRE. Sin esto cada ventana que se saca
            // nace encima de la anterior, en el mismo monitor. Se busca uno
            // donde no haya ya una ventana nuestra; si están todos ocupados, se
            // queda donde el escritorio la ponga.
            const pantallas = Qt.application.screens
            if (pantallas.length > 1) {
                const ocupados = {}
                const vs = Studio.escritorio.ventanasAbiertas()
                for (let i = 0; i < vs.length; ++i) ocupados[vs[i]] = true
                for (let k = 0; k < pantallas.length; ++k)
                    if (!ocupados[pantallas[k].name]) { w.screen = pantallas[k]; break }
            }
        }
        // Donde el escritorio no deja ubicar ventanas (Wayland), una página va a
        // pantalla completa en su monitor.
        if (w.pantallaCompleta || (w.esPagina && !Studio.escritorio.puedeUbicarVentanas)) w.visibility = Window.FullScreen
        w.listo = true
        if (w.monitor === "") Studio.ventanas.setMonitor(w.ventanaId, barra.idDeMonitor(w.screen))
    }

    // --- Lo que cambia el modelo (el MCP, «Llevar la ventana») ----------------------------
    onPantallaCompletaChanged: {
        const completa = w.visibility === Window.FullScreen
        if (w.listo && completa !== w.pantallaCompleta) w.visibility = w.pantallaCompleta ? Window.FullScreen : Window.Windowed
    }
    onMonitorChanged: {
        if (!w.listo || w.monitor === "" || barra.idDeMonitor(w.screen) === w.monitor) return
        const s = w.pantallaRecordada()
        if (s) barra.aMonitor(s)
    }
    Connections {
        target: Studio.ventanas
        function onTraerAlFrente(id) { if (id === w.ventanaId) { w.raise(); w.requestActivate() } }
    }

    // --- Lo que cambia la ventana (el operador), al modelo ---------------------------------
    onVisibilityChanged: {
        if (!w.listo) return
        if (w.visibility === Window.FullScreen) Studio.ventanas.setPantallaCompleta(w.ventanaId, true)
        else if (w.visibility === Window.Windowed || w.visibility === Window.Maximized)
            Studio.ventanas.setPantallaCompleta(w.ventanaId, false)
    }
    onScreenChanged: if (w.listo) Studio.ventanas.setMonitor(w.ventanaId, barra.idDeMonitor(w.screen))
    // La geometría, con pausa (no en cada pixel del arrastre) y solo en ventana.
    function recordar() { if (w.listo && w.visibility === Window.Windowed) guardarGeometria.restart() }
    onXChanged: recordar(); onYChanged: recordar(); onWidthChanged: recordar(); onHeightChanged: recordar()
    Timer {
        id: guardarGeometria
        interval: 600
        onTriggered: Studio.ventanas.setGeometria(w.ventanaId, w.x, w.y, w.width, w.height)
    }
    // Cerrarla (Cmd+W, el botón, el MCP) es olvidarla.
    onClosing: (close) => Studio.ventanas.cerrar(w.ventanaId)

    // --- Lo de adentro ----------------------------------------------------------------------
    VistaCosas {
        id: vista
        anchors { fill: parent; topMargin: w.esPanel || (!w.esPagina && !w.deVideo) ? 32 : 0 }
        cosas: w.cosas
        distribucion: w.distribucion
        principal: w.principal
        focus: true
    }
    TapHandler {
        enabled: w.deVideo
        onDoubleTapped: w.visibility = w.visibility === Window.FullScreen ? Window.Windowed : Window.FullScreen
    }

    // La barra de siempre (monitor, pantalla completa, cerrar) y lo propio del video.
    BarraVentana {
        id: barra
        ventana: w
        z: 100
        alCerrar: () => w.close()
        alFijar: (monitorId) => {
            if (monitorId === "") return
            const err = Studio.pantallasConectadas.asignar(monitorId, w.cosas, true)
            if (err !== "") { Studio.notify(err, "warn"); return }
            Studio.notify(qsTr("«%1» quedó fijo en ese monitor (Salidas → Pantallas).").arg(w.titulo), "ok")
            w.close()
        }
        extras: [
            GlassButton {
                compact: true; visible: vista.muro; text: qsTr("Distribución")
                onClicked: menuDistribucion.popup()
                Menu {
                    id: menuDistribucion
                    ItemsDeMenu {
                        menu: menuDistribucion
                        model: [ { id: "grid", t: qsTr("Cuadrícula") }, { id: "focus", t: qsTr("Foco") }, { id: "pip", t: qsTr("Imagen en imagen") }, { id: "row", t: qsTr("Fila") } ]
                        delegate: MenuItem {
                            required property var modelData
                            text: modelData.t; checkable: true; checked: w.distribucion === modelData.id
                            onTriggered: Studio.ventanas.setDistribucion(w.ventanaId, modelData.id)
                        }
                    }
                }
            },
            // Qué multivista muestra esta ventana (las de la página Multiview).
            GlassButton {
                compact: true; visible: w.esMultiview; icon: "▥"
                text: w.cosa ? w.cosa.titulo : qsTr("Multivista")
                onClicked: menuVista.popup()
                Menu {
                    id: menuVista
                    ItemsDeMenu {
                        menu: menuVista
                        model: Studio.multivistas.lista
                        delegate: MenuItem {
                            required property var modelData
                            text: modelData.nombre; checkable: true
                            checked: w.cosas[0] === "multiview:" + modelData.id
                            onTriggered: Studio.ventanas.setCosas(w.ventanaId, ["multiview:" + modelData.id])
                        }
                    }
                }
            }
        ]
    }

    Shortcut {
        // `sequences`: StandardKey.Cancel tiene varias teclas (Esc y Cmd+.).
        sequences: [StandardKey.Cancel]
        onActivated: if (w.visibility === Window.FullScreen) w.visibility = Window.Windowed; else w.close()
    }
    Shortcut { sequence: "F"; enabled: w.deVideo; onActivated: w.visibility = w.visibility === Window.FullScreen ? Window.Windowed : Window.FullScreen }
}
