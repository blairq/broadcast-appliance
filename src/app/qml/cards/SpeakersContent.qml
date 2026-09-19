// Tarjeta "Oradores" del dashboard: quien esta al aire con su reloj grande y
// quien sigue. El detalle y los comandos estan en la pagina Sala.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import BA
import "../controls"

Item {
    id: root
    readonly property var conf: Studio.conference
    property double clock: Date.now() + conf.clockOffset
    Timer { interval: 250; running: conf.speakers.count > 0; repeat: true; onTriggered: root.clock = Date.now() + root.conf.clockOffset }
    function fmt(ms) {
        const neg = ms < 0
        const t = Math.floor(Math.abs(ms) / 1000)
        return (neg ? "+" : "") + Math.floor(t / 60) + ":" + String(t % 60).padStart(2, "0")
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 8
        RowLayout {
            Layout.fillWidth: true
            StatusPill { text: conf.speakerMode ? "Modo oradores" : "Oradores apagado"; dot: conf.speakerMode ? Theme.ok : Theme.textFaint }
            Item { Layout.fillWidth: true }
            GlassButton { visible: conf.speakerMode; text: "Siguiente ▶"; compact: true; onClicked: conf.speakerNext() }
            GlassButton { text: "Abrir sala"; compact: true; onClicked: ApplicationWindow.window.showPage(2) }
        }
        Text {
            visible: conf.onAir.length === 0
            text: conf.speakerMode ? "Nadie al aire" : "Activalo en la página Sala"
            color: Theme.textFaint; font.pixelSize: 13
        }
        Repeater {
            model: conf.onAir
            delegate: RowLayout {
                required property var modelData
                readonly property double remaining: modelData.allottedSec * 1000 - (clock - modelData.onAirSinceMs)
                Layout.fillWidth: true
                Text { text: modelData.name; color: Theme.text; font { pixelSize: 16; weight: Font.DemiBold } elide: Text.ElideRight; Layout.fillWidth: true }
                Text {
                    text: remaining >= 0 ? fmt(Math.ceil(remaining / 1000) * 1000) : fmt(remaining)
                    color: remaining < 0 ? Theme.live : remaining <= 10000 ? Theme.accent : Theme.text
                    font { pixelSize: remaining <= 10000 ? 40 : 32; weight: Font.Black }
                }
                GlassButton { text: "Sacar"; compact: true; onClicked: conf.speakerOnAir(modelData.uid, false) }
            }
        }
        ListView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            spacing: 4
            model: conf.speakers
            delegate: RowLayout {
                required property string uid
                required property string name
                required property string speaker
                required property int position
                required property int allottedSec
                visible: speaker === "queued"
                height: visible ? implicitHeight : 0
                width: ListView.view.width
                Text { text: position + ". " + name; color: position === 1 ? Theme.accentSoft : Theme.textDim; font.pixelSize: 13; elide: Text.ElideRight; Layout.fillWidth: true }
                Text { text: fmt(allottedSec * 1000); color: Theme.textFaint; font.pixelSize: 12 }
                GlassButton { text: "Al aire"; compact: true; onClicked: conf.speakerOnAir(uid, true) }
            }
        }
    }
}
