// Muro de video: N fuentes con una distribucion (solo presentacion).
// Lo usan el fondo del dashboard y las ventanas de salida: el mismo video,
// del mismo servicio de frames, sin reabrir nada (sin cortes).
//
// layout: "grid"  cuadricula pareja
//         "focus" la primera grande a la izquierda, el resto en columna
//         "pip"   la primera a pantalla completa, el resto en miniatura
//         "row"   todas en fila
import QtQuick
import BA

Item {
    id: wall
    property var keys: []
    property string layout: "grid"
    property int fillMode: VideoItem.Cover
    property bool labels: false
    property real gap: 0
    /// Tally (rojo al aire, verde armada), vúmetro y cuenta regresiva de medios por celda (T38).
    /// Las marcas de operador (sin señal, tally, vúmetro, cuenta regresiva). Apagadas en lo que ve el público.
    property bool operador: true
    property bool tally: operador
    property bool vumetros: operador
    // Los vúmetros se leen a 10 Hz (un solo reloj para todas las celdas).
    property int pulso: 0
    Timer { interval: 100; repeat: true; running: wall.vumetros && wall.visible; onTriggered: wall.pulso++ }

    /// La fuente del programa que muestra la celda (para el tally y el nivel).
    function fuenteDe(key) {
        if (key.startsWith("entrada:")) return key.slice(8)
        if (key.startsWith("guest:")) return "Invitado " + key.slice(6)
        return ""
    }

    readonly property int n: keys ? keys.length : 0

    function rectFor(i) {
        const W = width, H = height, g = gap
        if (n <= 1) return Qt.rect(0, 0, W, H)
        if (layout === "focus") {
            const bigW = W * 0.72
            if (i === 0) return Qt.rect(0, 0, bigW - g / 2, H)
            const h = (H - g * (n - 2)) / (n - 1)
            return Qt.rect(bigW + g / 2, (i - 1) * (h + g), W - bigW - g / 2, h)
        }
        if (layout === "pip") {
            if (i === 0) return Qt.rect(0, 0, W, H)
            const pw = W * 0.24, ph = pw * 9 / 16, m = Math.max(12, W * 0.015)
            return Qt.rect(W - m - pw, H - m - (i) * (ph + m), pw, ph)
        }
        if (layout === "row") {
            const w = (W - g * (n - 1)) / n
            const h = Math.min(H, w * 9 / 16)
            return Qt.rect(i * (w + g), (H - h) / 2, w, h)
        }
        // grid
        const cols = Math.ceil(Math.sqrt(n))
        const rows = Math.ceil(n / cols)
        const cw = (W - g * (cols - 1)) / cols, ch = (H - g * (rows - 1)) / rows
        return Qt.rect((i % cols) * (cw + g), Math.floor(i / cols) * (ch + g), cw, ch)
    }

    Repeater {
        model: wall.keys
        delegate: Item {
            required property string modelData
            required property int index
            readonly property rect r: wall.rectFor(index)
            x: r.x; y: r.y; width: r.width; height: r.height
            z: (wall.layout === "pip" && index > 0) ? 1 : 0
            // Transicion suave al cambiar la distribucion.
            Behavior on x { NumberAnimation { duration: 260; easing.type: Easing.OutCubic } }
            Behavior on y { NumberAnimation { duration: 260; easing.type: Easing.OutCubic } }
            Behavior on width { NumberAnimation { duration: 260; easing.type: Easing.OutCubic } }
            Behavior on height { NumberAnimation { duration: 260; easing.type: Easing.OutCubic } }

            Rectangle { anchors.fill: parent; color: "black" }
            VideoItem { anchors.fill: parent; source: modelData; fillMode: wall.fillMode }
            // Sin señal: borde rojo titilante y el cartel (solo acá, nunca al aire).
            Rectangle {
                readonly property bool sin: modelData.startsWith("entrada:") && Studio.sinSenal.indexOf(modelData.slice(8)) >= 0
                visible: sin && wall.operador
                anchors.fill: parent; color: "transparent"
                border { color: Theme.live; width: 3 }
                SequentialAnimation on opacity { running: parent.visible; loops: Animation.Infinite
                    NumberAnimation { to: 0.35; duration: 450 } NumberAnimation { to: 1; duration: 450 } }
                Text { anchors.centerIn: parent; text: "SIN SEÑAL"; color: Theme.live; font { pixelSize: 18; weight: Font.Black } }
            }
            // Tally: rojo si está al aire (o es el programa), verde si está armada.
            Rectangle {
                readonly property string f: wall.fuenteDe(modelData)
                // El programa y la armada no llevan borde: ya se sabe qué son (y un borde rojo
                // permanente en el fondo del tablero se confunde con una alarma).
                readonly property bool alAire: f !== "" && (Studio.tally.aire || []).indexOf(f) >= 0
                readonly property bool armada: f !== "" && !alAire && (Studio.tally.armada || []).indexOf(f) >= 0
                visible: wall.tally && (alAire || armada)
                anchors.fill: parent; color: "transparent"; z: 2
                border { color: alAire ? "#e53935" : "#2ecc71"; width: Math.max(3, Math.min(parent.width, parent.height) / 90) }
            }
            // Vúmetro de la fuente (si tiene audio).
            Rectangle {
                id: vum
                readonly property string f: wall.fuenteDe(modelData)
                readonly property real db: (wall.pulso, f !== "" ? Studio.picoDe(f) : -100)
                visible: wall.vumetros && f !== "" && db > -90
                anchors { right: parent.right; top: parent.top; bottom: parent.bottom; margins: 6 }
                width: 6; radius: 3; z: 3
                color: Qt.rgba(0, 0, 0, 0.5)
                Rectangle {
                    anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
                    height: parent.height * Math.max(0, Math.min(1, (vum.db + 60) / 60))
                    radius: 3
                    color: vum.db > -3 ? "#e53935" : vum.db > -12 ? "#ffb300" : "#2ecc71"
                }
            }
            // Cuenta regresiva del medio que está al aire (roja en los últimos 10 s).
            Rectangle {
                readonly property var m: (Studio.mediosAlAire || []).find(x => x.fuente === wall.fuenteDe(modelData) ||
                                                                          (modelData === "program" && (Studio.mediosAlAire || []).length > 0))
                visible: !!m && wall.operador
                anchors { right: parent.right; top: parent.top; margins: 8 }
                radius: 8; height: 30; width: cuenta.implicitWidth + 16; z: 3
                color: m && m.restante_s <= 10 ? "#c62828" : Qt.rgba(0, 0, 0, 0.6)
                Text {
                    id: cuenta
                    anchors.centerIn: parent
                    readonly property int r: parent.m ? parent.m.restante_s : 0
                    text: "−" + Math.floor(r / 60) + ":" + String(r % 60).padStart(2, "0")
                    color: "white"; font.pixelSize: 16; font.weight: Font.DemiBold; font.family: Theme.mono
                }
            }
            Rectangle {
                visible: wall.layout === "pip" && index > 0
                anchors.fill: parent; color: "transparent"
                border { color: Qt.rgba(1, 1, 1, 0.35); width: 1 }
            }
            Rectangle {
                visible: wall.labels
                anchors { left: parent.left; bottom: parent.bottom; margins: 8 }
                radius: 8; height: 22; width: lbl.implicitWidth + 16
                color: Qt.rgba(0, 0, 0, 0.55)
                Text { id: lbl; anchors.centerIn: parent; text: Studio.frames.label(modelData); color: "white"; font.pixelSize: 11 }
            }
        }
    }
}
