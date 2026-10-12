// Tarjeta «Control PTZ»: mover una cámara desde el tablero (Studio.camaras).
//
// En el tablero es chica: la cámara, el joystick, el zoom y las posiciones
// guardadas. Sacada a su ventana (⧉, PanelWindow con id «ptz:<entrada>")
// suma la imagen en vivo de la cámara, y sobre ella se apunta con el mouse:
//
//   clic            ese punto va al centro
//   arrastrar       un cuadro: se centra en él y se acerca hasta llenarlo
//   rueda           zoom
//   doble clic      cuadro completo (digital)
//
// Lo hace lo que mueve el joystick (ControlCamaras::apuntar): el PTZ digital,
// exacto; una cámara motorizada, aproximado.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import BA
import "../controls"

Item {
    id: t
    property int cardId: 0
    /// La entrada elegida en el tablero (se guarda como `source` de la tarjeta).
    property string source: ""
    /// Lo pone PanelWindow: «ptz:<entrada>» cuando vive en su ventana.
    property string panelId: ""
    property bool enVentana: false

    readonly property var cc: Studio.camaras
    readonly property string entrada: {
        if (panelId.startsWith("ptz:")) return panelId.slice(4)
        if (source !== "") return source
        return cc && cc.camaras.length > 0 ? cc.camaras[0].entrada : ""
    }
    readonly property var cam: {
        const l = cc ? cc.camaras : []
        for (let i = 0; i < l.length; ++i) if (l[i].entrada === entrada) return l[i]
        return null
    }
    readonly property bool mueveDigital: cam !== null && (cam.modo === "digital" || cam.backend === "ninguno")
    readonly property var caps: cam === null ? ({}) : (mueveDigital ? cam.capacidadesDigital : cam.capacidades)
    property real velocidad: 0.6
    property string error: ""
    property var pos: ({})
    property var posiciones: []
    // Rostros y seguimiento (Studio.seguidor): las caras que se vieron, la elegida y si se la sigue.
    readonly property var sg: Studio.seguidor
    readonly property var caras: sg && sg.caras[entrada] ? sg.caras[entrada] : []
    readonly property var siguiendo: sg && sg.siguiendo[entrada] ? sg.siguiendo[entrada] : null
    property int elegida: -1
    property bool analizando: false
    function nombreCara(c, i) { return c.nombre ? c.nombre : "Cara " + (i + 1) + " (sin nombre)" }
    function seguirElegida() {
        const i = t.elegida >= 0 && t.elegida < t.caras.length ? t.elegida : (t.caras.length === 1 ? 0 : -1)
        if (i < 0) { t.avisar(t.caras.length === 0 ? "Primero «Analizar rostros» (o no hay caras en cuadro)" : "Elegí a quién seguir: tocá su recuadro o elegilo en la lista"); return }
        const c = t.caras[i]
        t.avisar(t.sg.seguir(t.entrada, c.id ? { id: c.id, nombre: c.nombre } : { x: c.x + c.ancho / 2, y: c.y + c.alto / 2 }))
    }
    Connections {
        target: t.sg
        function onAnalizado(cual, error) {
            if (cual !== t.entrada) return
            t.analizando = false
            if (error !== "") t.avisar(error)
            else if (t.caras.length === 0) t.avisar("No se ve ninguna cara")
            else { t.avisar(""); if (t.elegida >= t.caras.length) t.elegida = -1 }
        }
    }

    function avisar(e) { t.error = e || "" }
    function leerPosiciones() { t.posiciones = cc && entrada !== "" ? cc.posiciones(entrada) : [] }
    onEntradaChanged: { leerPosiciones(); mirar() }
    Connections { target: t.cc; function onCambio() { t.leerPosiciones() } }

    // La imagen en vivo se pide solo afuera del tablero (renderizarla cuesta).
    property string mirando: ""
    function mirar() {
        const quiero = t.enVentana ? t.entrada : ""
        if (quiero === t.mirando) return
        if (t.mirando !== "") Studio.frames.mirarEntrada(t.mirando, false)
        if (quiero !== "") Studio.frames.mirarEntrada(quiero, true)
        t.mirando = quiero
    }
    onEnVentanaChanged: mirar()
    Component.onCompleted: { leerPosiciones(); mirar() }
    Component.onDestruction: if (t.mirando !== "") Studio.frames.mirarEntrada(t.mirando, false)

    Timer {
        interval: 150; repeat: true
        running: t.visible && t.cam !== null && t.cam.digital
        onTriggered: t.pos = t.cc.posicion(t.entrada)
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 8

        // --- Cámara, qué se mueve, sacar a ventana ---------------------------------------
        RowLayout {
            Layout.fillWidth: true
            spacing: 6
            Rectangle {
                Layout.fillWidth: true
                height: 28; radius: 10
                color: Qt.rgba(0, 0, 0, 0.35); border.color: Qt.rgba(1, 1, 1, 0.15)
                Text {
                    anchors { left: parent.left; right: parent.right; verticalCenter: parent.verticalCenter; margins: 10 }
                    text: (t.entrada !== "" ? t.entrada : "Sin cámaras") + (t.enVentana ? "" : "  ▾")
                    elide: Text.ElideRight
                    color: Theme.text; font.pixelSize: 12
                }
                MouseArea {
                    anchors.fill: parent
                    enabled: !t.enVentana
                    cursorShape: Qt.PointingHandCursor
                    onClicked: menuCamaras.popup()
                }
                Menu {
                    id: menuCamaras
                    Repeater {
                        model: t.cc ? t.cc.camaras : []
                        delegate: MenuItem {
                            required property var modelData
                            text: modelData.entrada
                            onTriggered: Studio.dashboard.setCardSource(t.cardId, modelData.entrada)
                        }
                    }
                }
            }
            StatusPill {
                visible: t.cam !== null
                text: t.mueveDigital ? "DIGITAL" : "ÓPTICO"
                dot: t.cam && t.cam.listo ? Theme.ok : Theme.warn
            }
            GlassButton {
                compact: true
                text: "⧉"
                visible: !t.enVentana && t.entrada !== "" && ApplicationWindow.window &&
                         ApplicationWindow.window.sacarPanel !== undefined
                onClicked: ApplicationWindow.window.sacarPanel("ptz:" + t.entrada, "PTZ — " + t.entrada)
                ToolTip.visible: hovered
                ToolTip.delay: 400
                ToolTip.text: "Sacar a una ventana con la imagen de la cámara, para apuntar con el mouse"
            }
        }

        Text {
            visible: t.cam === null
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            text: t.entrada === "" ? "No hay entradas de video con PTZ." : "«" + t.entrada + "» ya no es una entrada de video."
            color: Theme.textDim; font.pixelSize: 12
        }

        // --- La imagen, para apuntar (solo en ventana) -----------------------------------
        Rectangle {
            id: vista
            visible: t.enVentana && t.cam !== null
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: 160
            radius: 10; color: "black"; clip: true

            VideoItem { id: video; anchors.fill: parent; source: "entrada:" + t.entrada; fillMode: VideoItem.Fit }
            Text {
                visible: !video.hasFrame
                anchors.centerIn: parent
                text: "Sin señal"; color: Theme.textFaint; font.pixelSize: 13
            }

            // Donde cae la imagen dentro del rectángulo (Fit deja bandas).
            readonly property real aspecto: video.frameSize.width > 0 ? video.frameSize.width / video.frameSize.height : 16 / 9
            readonly property real iw: Math.min(width, height * aspecto)
            readonly property real ih: iw / aspecto
            readonly property real ix: (width - iw) / 2
            readonly property real iy: (height - ih) / 2
            function u(x) { return Math.max(0, Math.min(1, (x - ix) / iw)) }
            function v(y) { return Math.max(0, Math.min(1, (y - iy) / ih)) }

            // El cuadro que se dibuja.
            Rectangle {
                id: cuadro
                visible: false
                color: Qt.rgba(1, 0.42, 0.17, 0.15)
                border { color: Theme.accent; width: 2 }
            }
            // Las caras analizadas (y la que se sigue), sobre la imagen. Tocar una la elige.
            Repeater {
                model: t.caras
                delegate: Rectangle {
                    required property var modelData
                    required property int index
                    readonly property bool esObjetivo: t.siguiendo !== null &&
                        (t.siguiendo.id ? modelData.id === t.siguiendo.id
                                        : Math.abs(modelData.x + modelData.ancho / 2 - t.siguiendo.x) < 0.06 && Math.abs(modelData.y + modelData.alto / 2 - t.siguiendo.y) < 0.06)
                    x: vista.ix + modelData.x * vista.iw
                    y: vista.iy + modelData.y * vista.ih
                    width: modelData.ancho * vista.iw
                    height: modelData.alto * vista.ih
                    z: 2
                    color: "transparent"
                    border.width: esObjetivo || index === t.elegida ? 3 : 2
                    border.color: esObjetivo ? Theme.ok : index === t.elegida ? Theme.accent : (modelData.nombre ? "#4fc3f7" : Qt.rgba(1, 1, 1, 0.7))
                    Text {
                        anchors { left: parent.left; bottom: parent.top; bottomMargin: 2 }
                        text: (parent.esObjetivo ? "◎ " : "") + t.nombreCara(modelData, index)
                        color: parent.border.color; font.pixelSize: 11; font.weight: Font.DemiBold
                        style: Text.Outline; styleColor: Qt.rgba(0, 0, 0, 0.8)
                    }
                    MouseArea {
                        anchors.fill: parent; cursorShape: Qt.PointingHandCursor
                        onClicked: t.elegida = index
                        // Doble clic: seguirla ya.
                        onDoubleClicked: { t.elegida = index; t.seguirElegida() }
                    }
                }
            }
            // Cómo se usa (abajo, chico).
            Text {
                anchors { left: parent.left; right: parent.right; bottom: parent.bottom; margins: 6 }
                z: 3
                horizontalAlignment: Text.AlignHCenter
                elide: Text.ElideRight
                text: "Clic: apuntar · arrastrar: encuadrar · clic derecho o Alt+clic: seguir a esa cara"
                color: Qt.rgba(1, 1, 1, 0.55); font.pixelSize: 10
                style: Text.Outline; styleColor: Qt.rgba(0, 0, 0, 0.7)
            }
            // La mira en el centro: ahí va lo que se toque.
            Rectangle { anchors.centerIn: parent; width: 18; height: 1; color: Qt.rgba(1, 1, 1, 0.35) }
            Rectangle { anchors.centerIn: parent; width: 1; height: 18; color: Qt.rgba(1, 1, 1, 0.35) }

            MouseArea {
                anchors.fill: parent
                cursorShape: Qt.CrossCursor
                acceptedButtons: Qt.LeftButton | Qt.RightButton
                property point desde
                onPressed: (m) => { desde = Qt.point(m.x, m.y); cuadro.visible = false }
                onPositionChanged: (m) => {
                    if (!pressed) return
                    if (Math.abs(m.x - desde.x) < 6 && Math.abs(m.y - desde.y) < 6) return
                    cuadro.x = Math.min(m.x, desde.x); cuadro.y = Math.min(m.y, desde.y)
                    cuadro.width = Math.abs(m.x - desde.x); cuadro.height = Math.abs(m.y - desde.y)
                    cuadro.visible = true
                }
                onReleased: (m) => {
                    // Alt+clic o clic derecho: SEGUIR a la cara más cercana a ese punto
                    // (la tarjeta Seguimiento del pipeline la mantiene encuadrada).
                    if (m.button === Qt.RightButton || (m.modifiers & Qt.AltModifier)) {
                        cuadro.visible = false
                        t.avisar(t.sg ? t.sg.seguirPunto(t.entrada, vista.u(m.x), vista.v(m.y)) : "El seguimiento no está disponible")
                        return
                    }
                    if (cuadro.visible) {
                        cuadro.visible = false
                        const cx = cuadro.x + cuadro.width / 2, cy = cuadro.y + cuadro.height / 2
                        t.avisar(t.cc.apuntar(t.entrada, vista.u(cx), vista.v(cy),
                                              cuadro.width / vista.iw, cuadro.height / vista.ih))
                    } else {
                        t.avisar(t.cc.apuntar(t.entrada, vista.u(m.x), vista.v(m.y), 0, 0))
                    }
                }
                onDoubleClicked: if (t.mueveDigital) t.avisar(t.cc.encuadre(t.entrada, 0.5, 0.5, 1, 0.8))
                onWheel: (w) => {
                    if (t.mueveDigital) {
                        const z = t.pos.zoom || 1
                        t.avisar(t.cc.encuadre(t.entrada, t.pos.x || 0.5, t.pos.y || 0.5,
                                               Math.max(1, Math.min(8, z * (w.angleDelta.y > 0 ? 1.15 : 1 / 1.15))), 0.15))
                    } else {
                        t.avisar(t.cc.mover(t.entrada, 0, 0, (w.angleDelta.y > 0 ? 1 : -1) * t.velocidad))
                        pararZoom.restart()
                    }
                }
            }
            Timer { id: pararZoom; interval: 180; onTriggered: t.cc.parar(t.entrada) }

            Text {
                anchors { left: parent.left; bottom: parent.bottom; margins: 8 }
                text: "clic: centrar · arrastrar: encuadrar · rueda: zoom" +
                      (t.mueveDigital ? " · doble clic: cuadro completo" : " (aproximado en cámaras motorizadas)")
                color: Qt.rgba(1, 1, 1, 0.6); font.pixelSize: 10
                style: Text.Outline; styleColor: Qt.rgba(0, 0, 0, 0.6)
            }
            Text {
                visible: t.mueveDigital && t.pos.zoom !== undefined
                anchors { right: parent.right; bottom: parent.bottom; margins: 8 }
                text: (t.pos.zoom || 1).toFixed(2) + "×"
                color: Theme.text; font.pixelSize: 11
                style: Text.Outline; styleColor: Qt.rgba(0, 0, 0, 0.6)
            }
        }

        // --- Rostros y seguimiento (en ventana) -----------------------------------------------
        RowLayout {
            visible: t.enVentana && t.cam !== null
            Layout.fillWidth: true
            spacing: 6
            GlassButton {
                compact: true
                text: t.analizando ? "Analizando…" : "Analizar rostros"
                icon: "☻"
                enabled: !t.analizando && t.sg && t.sg.disponible
                onClicked: { t.analizando = true; t.sg.analizar(t.entrada) }
                ToolTip.visible: hovered; ToolTip.delay: 400
                ToolTip.text: t.sg && t.sg.disponible ? "Busca las caras en la imagen y reconoce a las que conoce (Personas)" : "Este programa se armó sin reconocimiento de rostros"
            }
            ComboBox {
                id: elegirCara
                Layout.fillWidth: true
                Layout.minimumWidth: 140
                model: t.caras.map((c, i) => t.nombreCara(c, i))
                currentIndex: t.elegida
                displayText: t.elegida >= 0 && t.elegida < t.caras.length ? t.nombreCara(t.caras[t.elegida], t.elegida)
                                                                           : (t.caras.length ? "¿A quién seguir?" : "Sin caras analizadas")
                onActivated: (i) => t.elegida = i
                enabled: t.caras.length > 0
            }
            GlassButton {
                compact: true
                text: t.siguiendo ? "Dejar de seguir" : "Tracking"
                icon: t.siguiendo ? "■" : "◎"
                active: t.siguiendo !== null
                enabled: t.sg && t.sg.disponible
                onClicked: t.siguiendo ? t.sg.dejar(t.entrada) : t.seguirElegida()
                ToolTip.visible: hovered; ToolTip.delay: 400
                ToolTip.text: "La cámara sigue a la persona elegida y la mantiene en el centro (con el motor si tiene; si no, con el recorte)"
            }
        }
        Text {
            visible: t.enVentana && t.siguiendo !== null
            Layout.fillWidth: true
            text: t.siguiendo ? "Siguiendo a " + (t.siguiendo.nombre || "la cara elegida") + " — " + t.siguiendo.estado : ""
            color: t.siguiendo && t.siguiendo.estado.startsWith("perdida") ? Theme.warn : Theme.ok
            font.pixelSize: 11; elide: Text.ElideRight
        }

        // --- Joystick y zoom -----------------------------------------------------------------
        RowLayout {
            visible: t.cam !== null
            Layout.fillWidth: true
            spacing: 10
            Rectangle {
                id: pad
                property real px: 0
                property real py: 0
                Layout.preferredWidth: 120; Layout.preferredHeight: 120
                radius: 60
                color: Qt.rgba(1, 1, 1, 0.05); border.color: Qt.rgba(1, 1, 1, 0.18)
                Rectangle {
                    width: 36; height: 36; radius: 18
                    color: area.pressed ? Theme.accent : Qt.rgba(1, 1, 1, 0.22)
                    x: pad.width / 2 - width / 2 + pad.px * (pad.width / 2 - 18)
                    y: pad.height / 2 - height / 2 - pad.py * (pad.height / 2 - 18)
                }
                Timer {
                    interval: 60; repeat: true; running: area.pressed
                    onTriggered: t.cc.mover(t.entrada, pad.px * t.velocidad, pad.py * t.velocidad, 0)
                }
                MouseArea {
                    id: area
                    anchors.fill: parent
                    preventStealing: true
                    function leer(m) {
                        let dx = (m.x - pad.width / 2) / (pad.width / 2 - 18)
                        let dy = -(m.y - pad.height / 2) / (pad.height / 2 - 18)
                        const r = Math.sqrt(dx * dx + dy * dy)
                        if (r > 1) { dx /= r; dy /= r }
                        pad.px = Math.abs(dx) < 0.08 ? 0 : dx
                        pad.py = Math.abs(dy) < 0.08 ? 0 : dy
                    }
                    onPressed: (m) => { leer(m); t.avisar(t.cc.mover(t.entrada, pad.px * t.velocidad, pad.py * t.velocidad, 0)) }
                    onPositionChanged: (m) => leer(m)
                    onReleased: { pad.px = 0; pad.py = 0; t.cc.parar(t.entrada) }
                    onCanceled: { pad.px = 0; pad.py = 0; t.cc.parar(t.entrada) }
                }
            }
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 6
                Repeater {
                    model: t.caps.zoom === true ? [{t: "+ Acercar", v: 1}, {t: "− Alejar", v: -1}] : []
                    delegate: Rectangle {
                        required property var modelData
                        Layout.fillWidth: true
                        height: 30; radius: 10
                        color: zm.pressed ? Theme.accent : Qt.rgba(1, 1, 1, 0.08)
                        border.color: Qt.rgba(1, 1, 1, 0.16)
                        Text { anchors.centerIn: parent; text: modelData.t; color: Theme.text; font.pixelSize: 12 }
                        MouseArea {
                            id: zm
                            anchors.fill: parent
                            onPressed: t.avisar(t.cc.mover(t.entrada, 0, 0, modelData.v * t.velocidad))
                            onReleased: t.cc.parar(t.entrada)
                            onCanceled: t.cc.parar(t.entrada)
                        }
                    }
                }
                RowLayout {
                    spacing: 4
                    Text { text: "Vel."; color: Theme.textDim; font.pixelSize: 11 }
                    Slider { Layout.fillWidth: true; from: 0.1; to: 1; value: t.velocidad; onMoved: t.velocidad = value }
                }
                GlassButton {
                    compact: true
                    visible: t.caps.home === true
                    text: "Inicio"
                    onClicked: t.avisar(t.cc.home(t.entrada))
                }
            }
        }

        // --- Posiciones guardadas ------------------------------------------------------------
        Text {
            visible: t.cam !== null && t.posiciones.length > 0
            text: "Posiciones"; color: Theme.textDim; font.pixelSize: 11
        }
        Flow {
            visible: t.cam !== null && t.posiciones.length > 0
            Layout.fillWidth: true
            Layout.maximumHeight: t.enVentana ? 80 : 999
            spacing: 6
            clip: true
            Repeater {
                model: t.posiciones
                delegate: GlassButton {
                    required property var modelData
                    compact: true
                    text: modelData.nombre
                    // Al aire, en gris con candado: tocarla pregunta (se vería el movimiento).
                    opacity: t.cc && t.cc.alAire(t.entrada) ? 0.55 : 1
                    onClicked: {
                        const ir = function() { const r = t.cc.irAPosicion(t.entrada, modelData.id, -1); t.avisar(r.ok ? (r.aviso || "") : r.error) }
                        // La regla del interlock es del modelo: si pide confirmar, se pregunta.
                        const motivo = t.cc.chequearMovimiento(t.entrada, false, "")
                        if (motivo === "") { ir(); return }
                        ApplicationWindow.window.confirm({ title: "La cámara está al aire", text: motivo + " ¿Mover igual?",
                                                           accept: "Mover igual", reject: "Cancelar", action: ir })
                    }
                    ToolTip.visible: hovered && modelData.comentario !== ""
                    ToolTip.delay: 400
                    ToolTip.text: modelData.comentario || ""
                }
            }
        }

        Text {
            visible: t.error !== ""
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            text: t.error; color: Theme.warn; font.pixelSize: 11
        }
        Item { Layout.fillHeight: !t.enVentana; Layout.preferredHeight: 0 }
    }
}
