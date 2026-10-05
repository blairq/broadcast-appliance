// Ventana de salida: video sacado del dashboard (p. ej. a otro monitor).
// Solo presentacion: su estado (fuentes, distribucion, monitor, pantalla
// completa, geometria) vive en Studio.dashboard.windows.
//
//   doble clic   pantalla completa / ventana
//   Esc          sale de pantalla completa
//   mouse        muestra los controles (se esconden solos)
import QtQuick
import QtQuick.Controls
import BA
import "controls"

Window {
    id: w
    required property int winId
    required property var keys
    required property string layout
    required property string screenName
    required property bool fullscreen
    required property rect geometry

    objectName: "ba:salida:" + winId // ver PageWindow.qml
    title: "Broadcast Appliance — " + (keys.length === 1 ? Studio.frames.label(keys[0]) : "Salida de video")
    color: "black"
    x: geometry.x; y: geometry.y
    width: geometry.width; height: geometry.height
    minimumWidth: 320; minimumHeight: 180
    visible: true
    visibility: fullscreen ? Window.FullScreen : Window.Windowed

    function screenByName(name) {
        for (let i = 0; i < Qt.application.screens.length; ++i)
            if (Qt.application.screens[i].name === name) return Qt.application.screens[i]
        return null
    }
    Component.onCompleted: { const s = screenByName(screenName); if (s) w.screen = s }
    onScreenNameChanged: { const s = screenByName(screenName); if (s) w.screen = s }
    onClosing: (close) => Studio.dashboard.windows.close(winId)

    // Guarda la geometria (con pausa: no en cada pixel del arrastre).
    function remember() { if (!fullscreen) saveGeom.restart() }
    onXChanged: remember(); onYChanged: remember(); onWidthChanged: remember(); onHeightChanged: remember()
    Timer { id: saveGeom; interval: 600; onTriggered: Studio.dashboard.windows.setGeometry(w.winId, w.x, w.y, w.width, w.height) }

    VideoWall {
        anchors.fill: parent
        keys: w.keys
        layout: w.layout
        fillMode: VideoItem.Fit
        // Una ventana con UNA sola fuente suele ir a un proyector o un televisor
        // de la sala: sin marcas de operador. Con varias es un multiview de control.
        operador: w.keys.length > 1
    }

    // --- Controles que aparecen con el mouse --------------------------------
    MouseArea {
        id: hover
        anchors.fill: parent
        hoverEnabled: true
        onPositionChanged: { bar.opacity = 1; hide.restart() }
        onDoubleClicked: Studio.dashboard.windows.setFullscreen(w.winId, !w.fullscreen)
        cursorShape: bar.opacity > 0 ? Qt.ArrowCursor : Qt.BlankCursor
    }
    Timer { id: hide; interval: 2200; onTriggered: bar.opacity = 0 }
    /// Volver a pantalla completa DESPUES de cambiar de monitor (ver el menu).
    /// Con una pausa a proposito: el compositor tiene que terminar de sacarla
    /// antes de que se la pida de nuevo, o ignora el segundo pedido.
    property bool pedidoFull: false
    // CAMBIAR `screen` NO MUEVE UNA VENTANA SIN PANTALLA COMPLETA (Mac): queda
    // donde estaba y el botón «Monitor» parecía roto. Además de la pantalla se
    // la lleva al centro del monitor elegido (achicada si no entra).
    property var destino: null
    function llevarYa() {
        const s = w.destino
        if (!s) return
        Studio.dashboard.windows.setScreen(w.winId, s.name)
        w.screen = s
        const ww = Math.min(w.width, s.width - 40), hh = Math.min(w.height, s.height - 80)
        // Directo a la ventana (setGeometry del modelo solo guarda); al
        // moverse, remember() la guarda.
        w.width = ww
        w.height = hh
        w.x = s.virtualX + Math.round((s.width - ww) / 2)
        w.y = s.virtualY + Math.round((s.height - hh) / 2)
        w.raise()
        if (w.pedidoFull) volverAFull.restart()
    }
    Timer { id: llevar; onTriggered: w.llevarYa() }
    Timer {
        id: volverAFull
        interval: 300
        onTriggered: if (w.pedidoFull) Studio.dashboard.windows.setFullscreen(w.winId, true)
    }
    Shortcut { sequence: "Esc"; enabled: w.fullscreen; onActivated: Studio.dashboard.windows.setFullscreen(w.winId, false) }
    Shortcut { sequence: "F"; onActivated: Studio.dashboard.windows.setFullscreen(w.winId, !w.fullscreen) }

    Rectangle {
        id: bar
        anchors { top: parent.top; horizontalCenter: parent.horizontalCenter; topMargin: 16 }
        height: 44; radius: 22
        width: row.implicitWidth + 24
        color: Qt.rgba(0.07, 0.07, 0.08, 0.82)
        border.color: Qt.rgba(1, 1, 1, 0.14)
        opacity: 1
        visible: opacity > 0
        Behavior on opacity { NumberAnimation { duration: 250 } }
        Component.onCompleted: hide.start()
        HoverHandler { onHoveredChanged: if (hovered) { bar.opacity = 1; hide.stop() } else hide.restart() }

        Row {
            id: row
            anchors.centerIn: parent
            spacing: 8
            GlassButton {
                compact: true; text: "Monitor"; icon: "▭"
                onClicked: screenMenu.popup()
                Menu {
                    id: screenMenu
                    // La barra se esconde sola a los 2,2 s, y un menú cuyo
                    // ancestro se vuelve invisible se cierra con él. Mientras
                    // está abierto, la barra se queda.
                    onAboutToShow: hide.stop()
                    onClosed: hide.restart()

                    MenuItem {
                        text: "Identificar pantallas"
                        // Los nombres («eDP-1», «DP-1») no le dicen nada a
                        // nadie: para saber cuál es cuál había que mandar una
                        // ventana y ver adónde iba.
                        onTriggered: Studio.identificarPantallas()
                    }
                    MenuSeparator {}
                    Repeater {
                        model: Qt.application.screens
                        delegate: MenuItem {
                            required property var modelData
                            text: modelData.name + "  (" + modelData.width + "x" + modelData.height + ")"
                            checkable: true
                            checked: w.screen && w.screen.name === modelData.name
                            onTriggered: {
                                // ESTANDO EN PANTALLA COMPLETA NO SE MUEVE. El
                                // compositor recibe «pantalla completa en el
                                // monitor A» y cambiar de pantalla despues no
                                // le vuelve a pedir nada, asi que se queda
                                // donde estaba. Hay que SALIR, cambiar, y
                                // volver a entrar: ahi si recibe la peticion
                                // nueva, con el monitor nuevo.
                                w.pedidoFull = w.fullscreen || !Studio.puedeUbicarVentanas
                                w.destino = modelData
                                if (w.fullscreen) {
                                    Studio.dashboard.windows.setFullscreen(w.winId, false)
                                    // Salir tiene animación (Mac): mover recién después.
                                    llevar.interval = Qt.platform.os === "osx" ? 800 : 150
                                    llevar.restart()
                                } else {
                                    w.llevarYa()
                                }
                                // EN WAYLAND `setScreen` NO MUEVE LA VENTANA, y
                                // encima no falla: Qt informa la pantalla nueva
                                // y la ventana se queda donde estaba —el botón
                                // parecía roto—. Lo que el compositor sí
                                // respeta es pantalla completa EN UN MONITOR,
                                // que además es lo que uno quiere el 95% de las
                                // veces al mandar un video a la otra pantalla.
                            }
                        }
                    }
                    MenuItem {
                        enabled: false
                        visible: !Studio.puedeUbicarVentanas
                        height: visible ? implicitHeight : 0
                        text: "(este escritorio solo deja mandarla a pantalla completa)"
                    }
                }
            }
            GlassButton {
                compact: true; visible: w.keys.length > 1; text: "Distribucion"
                onClicked: layoutMenu.popup()
                Menu {
                    id: layoutMenu
                    Repeater {
                        model: [ { id: "grid", t: "Cuadricula" }, { id: "focus", t: "Foco" }, { id: "pip", t: "Imagen en imagen" }, { id: "row", t: "Fila" } ]
                        delegate: MenuItem {
                            required property var modelData
                            text: modelData.t; checkable: true; checked: w.layout === modelData.id
                            onTriggered: Studio.dashboard.windows.setLayout(w.winId, modelData.id)
                        }
                    }
                }
            }
            GlassButton {
                compact: true
                text: w.fullscreen ? "Salir de pantalla completa" : "Pantalla completa"
                active: w.fullscreen
                onClicked: Studio.dashboard.windows.setFullscreen(w.winId, !w.fullscreen)
            }
            GlassButton { compact: true; text: "Cerrar"; icon: "✕"; onClicked: w.close() }
        }
    }
}
