// El joystick de pan/tilt de una cámara (la tarjeta PTZ y su ventana): se
// arrastra la perilla y la cámara se mueve con esa velocidad mientras se tiene
// apretado; al soltar, para.
import QtQuick
import BA

Rectangle {
    id: pad
    /// La tarjeta PTZ (PtzContent): entrada, velocidad, cc, avisar(). (Quien la pasa no puede
    /// escribir `tarjeta: t`: `t` es también de acá; PtzContent da `estaTarjeta`.)
    required property var tarjeta
    readonly property var t: tarjeta
    property real px: 0
    property real py: 0
    readonly property real perilla: Math.max(26, width * 0.3)
    implicitWidth: 120; implicitHeight: 120
    radius: width / 2
    color: Qt.rgba(1, 1, 1, 0.05); border.color: Qt.rgba(1, 1, 1, 0.18)
    Rectangle {
        width: pad.perilla; height: pad.perilla; radius: width / 2
        color: area.pressed ? Theme.accent : Qt.rgba(1, 1, 1, 0.22)
        x: pad.width / 2 - width / 2 + pad.px * (pad.width / 2 - width / 2)
        y: pad.height / 2 - height / 2 - pad.py * (pad.height / 2 - height / 2)
    }
    Timer {
        interval: 60; repeat: true; running: area.pressed
        onTriggered: pad.t.cc.mover(pad.t.entrada, pad.px * pad.t.velocidad, pad.py * pad.t.velocidad, 0)
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
        onPressed: (m) => { leer(m); pad.t.avisar(pad.t.cc.mover(pad.t.entrada, pad.px * pad.t.velocidad, pad.py * pad.t.velocidad, 0)) }
        onPositionChanged: (m) => leer(m)
        onReleased: { pad.px = 0; pad.py = 0; pad.t.cc.parar(pad.t.entrada) }
        onCanceled: { pad.px = 0; pad.py = 0; pad.t.cc.parar(pad.t.entrada) }
    }
}
