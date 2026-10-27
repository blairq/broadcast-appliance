// Tarjeta "Sala": quien espera en el lobby (admitir/rechazar ahi mismo),
// ocupacion y archivos compartidos. El detalle completo esta en la pagina Sala.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import BA
import "../controls"

// CON EL TECLADO (un clic en la tarjeta y ya):
//   ↑ ↓      elegir a quien espera
//   Enter    admitirlo
//   Supr     rechazarlo
//
// Con gente esperando y el evento por empezar, admitir de a uno con el mouse
// es lo que hace que alguien entre tarde.
Item {
    id: tarjeta
    focus: true
    readonly property var conf: Studio.conference
    property int elegido: -1

    function mover(paso) {
        const n = tarjeta.conf.waiting.count
        if (n === 0) return
        tarjeta.elegido = tarjeta.elegido < 0 ? 0 : Math.max(0, Math.min(n - 1, tarjeta.elegido + paso))
    }
    /// Despues de sacar a alguien de la lista, la posicion elegida apunta al
    /// que ocupo su lugar; si era el ultimo, al nuevo ultimo.
    function acomodar() {
        const n = tarjeta.conf.waiting.count
        tarjeta.elegido = n === 0 ? -1 : Math.min(tarjeta.elegido, n - 1)
    }

    Keys.onPressed: (e) => {
        const p = tarjeta.conf.waiting.fila(tarjeta.elegido)
        switch (e.key) {
            case Qt.Key_Up:   tarjeta.mover(-1); e.accepted = true; break
            case Qt.Key_Down: tarjeta.mover(1);  e.accepted = true; break
            case Qt.Key_Return:
            case Qt.Key_Enter:
                if (p && p.uid !== undefined) { tarjeta.conf.admit(p.uid); tarjeta.acomodar() }
                e.accepted = true
                break
            case Qt.Key_Delete:
                if (p && p.uid !== undefined) { tarjeta.conf.reject(p.uid); tarjeta.acomodar() }
                e.accepted = true
                break
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 8
        RowLayout {
            Layout.fillWidth: true
            StatusPill { text: conf.online ? "Conferencia " + conf.room : "Sin conferencia"; dot: conf.online ? Theme.ok : Theme.live; pulse: !conf.online }
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
                required property int index
                required property string uid
                required property string name
                required property bool connected
                width: ListView.view.width
                height: 40
                radius: 10
                color: Qt.rgba(1, 1, 1, 0.06)
                opacity: connected ? 1 : 0.55
                // Admitir a la persona equivocada se arregla echandola, con la
                // sala mirando: por eso se marca a quien apunta el teclado.
                border.width: tarjeta.activeFocus && index === tarjeta.elegido ? 2 : 0
                border.color: Theme.ok
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
            GlassButton { text: "Abrir conferencia"; compact: true; onClicked: ApplicationWindow.window.showPage(2) }
        }
    }
}
