// El joystick de pan/tilt de una cámara (la tarjeta PTZ, su ventana y la página
// Cámaras PTZ): se arrastra la perilla y la cámara se mueve con esa velocidad
// mientras se tiene apretado; al soltar, para. El centro es quieto (zona muerta);
// cuanto más lejos, más rápido.
import QtQuick
import BA

Rectangle {
    id: pad
    /// La entrada que mueve y la velocidad máxima (0..1).
    property string entrada: ""
    property real velocidad: 0.6
    /// Las flechas en los bordes (la página grande).
    property bool flechas: false
    /// Lo que contestó el modelo al empezar a mover ("" si anduvo).
    signal aviso(string texto)

    readonly property var cc: Studio.camaras
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
    // Mientras se tiene apretado, la orden se repite con la velocidad de ahora (es una
    // acción, no una lectura: la cámara para sola si deja de recibirla).
    Timer {
        interval: 60; repeat: true; running: area.pressed
        onTriggered: pad.cc.mover(pad.entrada, pad.px * pad.velocidad, pad.py * pad.velocidad, 0)
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
        onPressed: (m) => { leer(m); pad.cc.mover(pad.entrada, pad.px * pad.velocidad, pad.py * pad.velocidad, 0) }
        onPositionChanged: (m) => leer(m)
        onReleased: { pad.px = 0; pad.py = 0; pad.cc.parar(pad.entrada) }
        onCanceled: { pad.px = 0; pad.py = 0; pad.cc.parar(pad.entrada) }
    }
}
