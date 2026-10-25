// Tarjeta «Control PTZ»: mover una cámara desde el tablero (Studio.camaras).
//
// En el tablero es chica: la cámara, el joystick, el zoom y las posiciones
// guardadas. Sacada a su ventana (⧉, VentanaSuelta con id «ptz:<entrada>") es el
// panel completo (controls/PtzVentana.qml): la toma entera con el recuadro del
// PTZ digital, o la imagen en vivo y una foto para apuntar en una cámara
// motorizada; los puntos prefijados a la vista y la configuración desplegable.
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
    /// Lo pone VentanaSuelta: «ptz:<entrada>» cuando vive en su ventana.
    property string panelId: ""
    property bool enVentana: false

    /// La tarjeta misma, con otro nombre: `Joystick { tarjeta: t }` buscaría `t`
    /// primero en el Joystick (que tiene uno) y se enlazaría consigo mismo.
    readonly property var estaTarjeta: t
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

    // La imagen en vivo se pide solo afuera del tablero (renderizarla cuesta). Con el
    // PTZ digital, también la toma entera (antes del recorte), para manejarlo encima.
    property string mirando: ""
    property string mirandoEntera: ""
    function mirar() {
        const quiero = t.enVentana ? t.entrada : ""
        if (quiero !== t.mirando) {
            if (t.mirando !== "") Studio.frames.mirarEntrada(t.mirando, false)
            if (quiero !== "") Studio.frames.mirarEntrada(quiero, true)
            t.mirando = quiero
        }
        const entera = t.enVentana && t.mueveDigital ? t.entrada : ""
        if (entera !== t.mirandoEntera) {
            if (t.mirandoEntera !== "") Studio.frames.mirarEntrada(t.mirandoEntera, false, true)
            if (entera !== "") Studio.frames.mirarEntrada(entera, true, true)
            t.mirandoEntera = entera
        }
    }
    onEnVentanaChanged: mirar()
    onMueveDigitalChanged: mirar()
    Component.onCompleted: { leerPosiciones(); mirar() }
    Component.onDestruction: {
        if (t.mirando !== "") Studio.frames.mirarEntrada(t.mirando, false)
        if (t.mirandoEntera !== "") Studio.frames.mirarEntrada(t.mirandoEntera, false, true)
    }

    // En su ventana: el panel completo (imágenes, puntos a la vista, configuración).
    Loader {
        anchors.fill: parent
        active: t.enVentana && t.cam !== null
        sourceComponent: PtzVentana { tarjeta: estaTarjeta }
    }

    Timer {
        interval: 150; repeat: true
        running: t.visible && t.cam !== null && t.cam.digital
        onTriggered: t.pos = t.cc.posicion(t.entrada)
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 8
        visible: !t.enVentana || t.cam === null

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
                    ItemsDeMenu {
                        menu: menuCamaras
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

        // --- Joystick y zoom -----------------------------------------------------------------
        RowLayout {
            visible: t.cam !== null
            Layout.fillWidth: true
            spacing: 10
            Joystick { tarjeta: estaTarjeta; Layout.preferredWidth: 120; Layout.preferredHeight: 120 }
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
