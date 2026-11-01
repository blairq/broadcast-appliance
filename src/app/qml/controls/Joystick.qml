// Un joystick: se arrastra la perilla y, mientras se tiene apretado, avisa cada 60 ms
// hacia dónde está corrida (`mover(px, py)`, -1..1, y hacia arriba positivo); al soltar,
// `soltado()`. El centro es quieto (zona muerta); cuanto más lejos, más rápido. Lo que
// mueve lo decide quien lo usa: una cámara (JoystickPtz), lo que se ve de un PDF.
import QtQuick
import BA

Rectangle {
    id: pad
    /// Las flechas en los bordes (la página grande).
    property bool flechas: false
    /// La rueda encima avisa `rueda(+1 | -1)` (si no, sigue de largo: desplaza la página).
    property bool conRueda: false
    /// Al apretar (con dónde se apretó), mientras se tiene apretado y al soltar.
    signal apretado(real px, real py)
    signal mover(real px, real py)
    signal soltado()
    signal rueda(int sentido)

    property real px: 0
    property real py: 0
    readonly property real perilla: Math.max(26, width * 0.3)
    implicitWidth: 120; implicitHeight: 120
    radius: width / 2
    color: Qt.rgba(1, 1, 1, 0.05); border.color: Qt.rgba(1, 1, 1, 0.18)
    Repeater {
        model: pad.flechas ? [["▲", 0, -1], ["▼", 0, 1], ["◀", -1, 0], ["▶", 1, 0]] : []
        delegate: Text {
            required property var modelData
            x: pad.width / 2 - width / 2 + modelData[1] * (pad.width / 2 - width / 2 - 8)
            y: pad.height / 2 - height / 2 + modelData[2] * (pad.height / 2 - height / 2 - 6)
            text: modelData[0]; color: Theme.textFaint
        }
    }
    Rectangle {
        width: pad.perilla; height: pad.perilla; radius: width / 2
        color: area.pressed ? Theme.accent : Qt.rgba(1, 1, 1, 0.22)
        x: pad.width / 2 - width / 2 + pad.px * (pad.width / 2 - width / 2)
        y: pad.height / 2 - height / 2 - pad.py * (pad.height / 2 - height / 2)
    }
    // Mientras se tiene apretado, la orden se repite con la posición de ahora (es una
    // acción, no una lectura: una cámara para sola si deja de recibirla).
    Timer {
        interval: 60; repeat: true; running: area.pressed
        onTriggered: pad.mover(pad.px, pad.py)
    }
    MouseArea {
        id: area
        anchors.fill: parent
        preventStealing: true
        function leer(m) {
            const r0 = pad.width / 2 - pad.perilla / 2
            let dx = (m.x - pad.width / 2) / r0
            let dy = -(m.y - pad.height / 2) / r0
            const r = Math.sqrt(dx * dx + dy * dy)
            if (r > 1) { dx /= r; dy /= r }
            pad.px = Math.abs(dx) < 0.08 ? 0 : dx
            pad.py = Math.abs(dy) < 0.08 ? 0 : dy
        }
        function soltar() { pad.px = 0; pad.py = 0; pad.soltado() }
        onPressed: (m) => { leer(m); pad.apretado(pad.px, pad.py) }
        onPositionChanged: (m) => leer(m)
        onReleased: soltar()
        onCanceled: soltar()
        onWheel: (w) => {
            if (!pad.conRueda) { w.accepted = false; return }
            pad.rueda(w.angleDelta.y > 0 ? 1 : -1)
        }
    }
}
