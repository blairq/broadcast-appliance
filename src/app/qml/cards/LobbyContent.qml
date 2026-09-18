// Tarjeta "Sala": quien espera en el lobby (admitir/rechazar ahi mismo),
// ocupacion y archivos compartidos. El detalle completo esta en la pagina Sala.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import BA
import "../controls"

Item {
    readonly property var conf: Studio.conference

    ColumnLayout {
        anchors.fill: parent
        spacing: 8
        RowLayout {
            Layout.fillWidth: true
            StatusPill { text: conf.online ? "Sala " + conf.room : "Sin sala"; dot: conf.online ? Theme.ok : Theme.live; pulse: !conf.online }
            Item { Layout.fillWidth: true }
            Text {
                visible: conf.online
                text: (conf.maxGuests - conf.slotsFree) + "/" + conf.maxGuests + " adentro"
                color: Theme.textDim; font.pixelSize: 12
            }
        }
        Text {
            text: conf.waiting.count === 0 ? "Nadie en el lobby" : (conf.waiting.count === 1 ? "1 persona espera" : conf.waiting.count + " personas esperan")
            color: conf.waiting.count > 0 ? Theme.accentSoft : Theme.textFaint
            font { pixelSize: 13; weight: conf.waiting.count > 0 ? Font.DemiBold : Font.Normal }
        }
        ListView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            spacing: 6
            model: conf.waiting
            delegate: Rectangle {
                required property string uid
                required property string name
                required property bool connected
                width: ListView.view.width
                height: 40
                radius: 10
                color: Qt.rgba(1, 1, 1, 0.06)
                opacity: connected ? 1 : 0.55
                RowLayout {
                    anchors { fill: parent; leftMargin: 10; rightMargin: 6 }
                    spacing: 6
                    Text { text: name; color: Theme.text; font.pixelSize: 13; elide: Text.ElideRight; Layout.fillWidth: true }
                    GlassButton { text: "Admitir"; compact: true; active: true; activeColor: Theme.ok; onClicked: conf.admit(uid) }
                    GlassButton { text: "✕"; compact: true; onClicked: conf.reject(uid) }
                }
            }
        }
        RowLayout {
            Layout.fillWidth: true
            Text {
                text: conf.shares.count > 0 ? "📎 " + conf.shares.count + " archivo(s) compartido(s)" : ""
                color: Theme.textDim; font.pixelSize: 12; Layout.fillWidth: true
            }
            GlassButton { text: "Abrir sala"; compact: true; onClicked: ApplicationWindow.window.showPage(2) }
        }
    }
}
