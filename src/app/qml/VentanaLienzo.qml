// La ventana madre de una distribución de pantallas en UN monitor (un «lienzo»:
// pantallas/Distribuciones.hpp, LienzosModel).
//
// Una sola ventana del sistema por monitor, SIN MARCO, y adentro cada «ventana» de
// la distribución como un rectángulo con su vista (VistaCosas, la misma de las
// ventanas sueltas y los monitores). A pantalla completa si la distribución ocupa
// el monitor entero; si no, del tamaño justo del rectángulo que encierra sus
// ventanas (lo que queda entre ellas se ve negro).
//
// QUIETA: no tiene barra, ni botones, ni bordes; no se cierra con Cmd+W/Alt+F4 (el
// cierre se rechaza) y si el sistema la corre, vuelve a su lugar. Lo que muestra y
// dónde lo cambia SOLO el menú Pantallas (el editor de distribuciones) o el MCP: la
// geometría está enlazada al modelo. Su id es el del monitor, así que cambiar de
// distribución no la cierra ni la abre: cambian sus rectángulos.
//
// Salida de emergencia: Ctrl+Shift+F12 (Main.qml) o Pantallas → Distribuciones →
// «Cerrar las ventanas de la distribución».
import QtQuick
import QtQuick.Window
import BA
import "controls"

Window {
    id: w
    required property string lienzoId
    required property rect geometria
    required property bool pantallaCompleta
    required property bool encima
    required property int orden
    required property var piezas
    /// La ventana principal (le da los componentes a VistaCatalogo).
    required property var principal

    // Para ventana_listar / ventana_captura (MCP).
    objectName: "ba:lienzo:" + lienzoId
    title: qsTr("Broadcast Appliance — Distribución, monitor %1").arg(orden)
    color: "black"
    flags: Qt.Window | Qt.FramelessWindowHint | (encima ? Qt.WindowStaysOnTopHint : 0)
    minimumWidth: 1
    minimumHeight: 1
    visibility: Window.Hidden

    /// El QScreen de su monitor, o null si (todavía) no está.
    readonly property var pantalla: {
        const ss = Qt.application.screens
        for (let i = 0; i < ss.length; ++i)
            if (Studio.pantallasConectadas.idDePantalla(ss[i].name, ss[i].manufacturer, ss[i].model,
                                                         ss[i].serialNumber) === w.lienzoId)
                return ss[i]
        return null
    }
    /// Mientras se está llevando (salir de pantalla completa tiene animación en Mac).
    property bool llevando: false

    function ubicar() {
        // Sin su monitor no se muestra: el modelo vuelve a resolver la distribución y
        // sus rectángulos pasan al lienzo del monitor que haya.
        if (!w.pantalla) { w.visibility = Window.Hidden; return }
        // De pantalla completa a ventana: salir primero y mover después.
        if (w.visibility === Window.FullScreen && !w.pantallaCompleta) {
            w.llevando = true
            w.visibility = Window.Windowed
            despues.interval = Qt.platform.os === "osx" ? 800 : 150
            despues.restart()
            return
        }
        w.llevando = true // lo que sigue mueve la ventana: que vigilar() no lo tome por el sistema
        w.screen = w.pantalla
        if (w.geometria.width > 0 && w.geometria.height > 0) {
            w.x = w.geometria.x; w.y = w.geometria.y
            w.width = w.geometria.width; w.height = w.geometria.height
        } else {
            w.x = w.pantalla.virtualX; w.y = w.pantalla.virtualY
            w.width = w.pantalla.width; w.height = w.pantalla.height
        }
        // Donde el escritorio no deja ubicar ventanas (Wayland), lo único que respeta
        // es pantalla completa EN un monitor.
        w.visibility = w.pantallaCompleta || !Studio.escritorio.puedeUbicarVentanas ? Window.FullScreen : Window.Windowed
        w.llevando = false
    }
    Timer { id: despues; onTriggered: { w.llevando = false; w.ubicar() } }
    onPantallaChanged: ubicar()
    onGeometriaChanged: ubicar()
    onPantallaCompletaChanged: ubicar()
    Component.onCompleted: ubicar()

    // El sistema la corrió o la sacó de pantalla completa (un atajo del escritorio, un
    // cambio de monitores): vuelve a su lugar, con una pausa para no pelear.
    function vigilar() {
        if (w.llevando || !w.pantalla || w.visibility === Window.Hidden) return
        const completa = w.pantallaCompleta || !Studio.escritorio.puedeUbicarVentanas
        const g = w.geometria
        const corrida = !completa && g.width > 0 &&
                        (w.x !== g.x || w.y !== g.y || w.width !== g.width || w.height !== g.height)
        if ((w.visibility === Window.FullScreen) !== completa || corrida) reponer.restart()
    }
    Timer { id: reponer; interval: 400; onTriggered: w.ubicar() }
    onXChanged: vigilar()
    onYChanged: vigilar()
    onWidthChanged: vigilar()
    onHeightChanged: vigilar()
    onVisibilityChanged: vigilar()

    // No se cierra a mano (Cmd+W, Alt+F4, el menú del sistema): solo desde Pantallas o el MCP.
    onClosing: (close) => { close.accepted = false }

    // --- Lo de adentro: un rectángulo por ventana de la distribución -------------------
    Repeater {
        model: w.piezas
        delegate: Item {
            required property var modelData
            x: Math.round(modelData.x * w.width)
            y: Math.round(modelData.y * w.height)
            width: Math.round(modelData.ancho * w.width)
            height: Math.round(modelData.alto * w.height)
            clip: true
            VistaCosas {
                anchors.fill: parent
                cosas: modelData.cosas
                distribucion: modelData.disposicion
                principal: w.principal
                marcoSolo: false
            }
        }
    }
}
