// Vumetro en dBFS (-60..0) con zonas verde / ambar / rojo.
//
// Balística (T31, la elige el operador en el Mezclador: Studio.mezcla.balistica):
//   pico  sube y baja con el pico (como antes);
//   ppm   EBU tipo IIb: sube al instante y baja 24 dB en 2,8 s (8,6 dB/s), así un
//         transitorio se alcanza a leer;
//   vu    integra ~300 ms (sube y baja despacio): muestra el volumen percibido,
//         no los picos.
import QtQuick
import BA

Item {
    id: m
    property real db: -100
    property bool vertical: false
    property string balistica: (typeof Studio !== "undefined" && Studio && Studio.mezcla) ? Studio.mezcla.balistica : "pico"
    property real mostrado: -100
    readonly property real frac: Math.max(0, Math.min(1, ((m.balistica === "pico" ? m.db : m.mostrado) + 60) / 60))

    // La balística corre a ~33 cuadros por segundo; con «pico» no hace falta.
    Timer {
        interval: 30; repeat: true; running: m.visible && m.balistica !== "pico"
        property double antes: 0
        onTriggered: {
            const ahora = Date.now(), dt = antes > 0 ? Math.min(0.2, (ahora - antes) / 1000) : 0.03
            antes = ahora
            const d = Math.max(-100, m.db)
            if (m.balistica === "ppm") {
                m.mostrado = d >= m.mostrado ? d : Math.max(d, m.mostrado - 8.57 * dt)
            } else { // vu: primer orden en amplitud, ~300 ms al 99 %
                const a = 1 - Math.exp(-dt / 0.065)
                const amp = Math.pow(10, m.mostrado / 20) + (Math.pow(10, d / 20) - Math.pow(10, m.mostrado / 20)) * a
                m.mostrado = amp > 0.00001 ? 20 * Math.log10(amp) : -100
            }
        }
    }

    implicitWidth: vertical ? 8 : 120
    implicitHeight: vertical ? 120 : 8

    Rectangle {
        anchors.fill: parent
        radius: Math.min(width, height) / 2
        color: Qt.rgba(1, 1, 1, 0.08)
    }
    Rectangle {
        radius: Math.min(width, height) / 2
        width: m.vertical ? m.width : m.width * m.frac
        height: m.vertical ? m.height * m.frac : m.height
        anchors { left: parent.left; bottom: parent.bottom }
        gradient: Gradient {
            orientation: m.vertical ? Gradient.Vertical : Gradient.Horizontal
            GradientStop { position: 0.0; color: m.vertical ? "#ff3b30" : "#35c46b" }
            GradientStop { position: 0.25; color: m.vertical ? "#f5b83d" : "#35c46b" }
            GradientStop { position: 0.7; color: m.vertical ? "#35c46b" : "#f5b83d" }
            GradientStop { position: 1.0; color: m.vertical ? "#35c46b" : "#ff3b30" }
        }
        Behavior on width { enabled: m.balistica === "pico"; NumberAnimation { duration: 90 } }
        Behavior on height { enabled: m.balistica === "pico"; NumberAnimation { duration: 90 } }
    }
}
