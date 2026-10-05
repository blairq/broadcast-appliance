// La playlist de medios (T46): los clips en orden, con in/out, el que suena y
// cuánto le queda, play / siguiente / parar, auto-next, loop y «Armar en PVW».
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import BA

Item {
    id: pp
    readonly property var pl: Studio.playlist
    readonly property var est: pl ? pl.estado : ({})
    function mmss(s) { s = Math.max(0, Math.round(s || 0)); return Math.floor(s / 60) + ":" + ("0" + (s % 60)).slice(-2) }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 12
        spacing: 10
        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            GlassButton { text: pp.est.reproduciendo ? "▶ Desde el elegido" : "▶ Play"; active: !pp.est.reproduciendo
                          onClicked: { const e = pp.pl.controlUi("play", lista.currentIndex); if (e !== "") Studio.notify(e, "error") } }
            GlassButton { text: "⏭ Siguiente"; enabled: !!pp.est.reproduciendo; onClicked: pp.pl.controlUi("next") }
            GlassButton { text: "⏹ Parar"; enabled: !!pp.est.reproduciendo; onClicked: pp.pl.controlUi("stop") }
            GlassButton { text: "Armar en PVW"; onClicked: pp.pl.armarUi()
                          ToolTip.visible: hovered; ToolTip.text: "Deja la escena «Playlist» armada para el take" }
            Item { Layout.fillWidth: true }
            CheckBox { text: "Pasar solo al siguiente"; checked: !!pp.est.auto_next; onToggled: pp.pl.autoNextUi(checked) }
            CheckBox { text: "Repetir la lista"; checked: !!pp.est.loop; onToggled: pp.pl.loopUi(checked) }
        }
        // El que suena: grande y en rojo los últimos 10 s.
        RowLayout {
            visible: !!pp.est.reproduciendo
            spacing: 12
            Text { text: "AL AIRE"; color: Theme.live; font.pixelSize: 11; font.weight: Font.Bold }
            Text { Layout.fillWidth: true; elide: Text.ElideRight; color: Theme.text; font.pixelSize: 15
                   text: pp.est.indice >= 0 && pp.pl.items[pp.est.indice] ? (pp.est.indice + 1) + ". " + pp.pl.items[pp.est.indice].nombre : "" }
            Text { text: "−" + pp.mmss(pp.est.restante_s); color: (pp.est.restante_s || 0) <= 10 ? Theme.live : Theme.text
                   font.pixelSize: 22; font.weight: Font.DemiBold; font.family: "Menlo" }
        }
        Text { visible: !pp.pl || pp.pl.items.length === 0; color: Theme.textFaint; font.pixelSize: 12; Layout.fillWidth: true; wrapMode: Text.WordWrap
               text: "Vacía. En «Archivos», elegí videos o audios y «Sumar a la playlist» (o pedile al asistente que la arme)." }
        ListView {
            id: lista
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            spacing: 4
            model: pp.pl ? pp.pl.items : []
            delegate: Rectangle {
                required property var modelData
                required property int index
                width: ListView.view.width
                height: 40
                radius: 8
                color: index === pp.est.indice && pp.est.reproduciendo ? Qt.rgba(1, 0.23, 0.19, 0.18)
                       : ListView.isCurrentItem ? Qt.rgba(1, 1, 1, 0.08) : Qt.rgba(1, 1, 1, 0.03)
                MouseArea { anchors.fill: parent; onClicked: lista.currentIndex = index; onDoubleClicked: pp.pl.controlUi("play", index) }
                RowLayout {
                    anchors { fill: parent; leftMargin: 10; rightMargin: 6 }
                    spacing: 8
                    Text { text: (index + 1) + "."; color: Theme.textDim; font.pixelSize: 12; Layout.preferredWidth: 26 }
                    Text { text: modelData.nombre; color: Theme.text; font.pixelSize: 13; Layout.fillWidth: true; elide: Text.ElideRight }
                    Text { text: (modelData.in_s > 0 ? "in " + pp.mmss(modelData.in_s) + "  " : "") + (modelData.out_s > 0 ? "out " + pp.mmss(modelData.out_s) : "")
                           color: Theme.textFaint; font.pixelSize: 11 }
                    GlassButton { compact: true; text: "↑"; enabled: index > 0; onClicked: pp.pl.moverUi(index, index - 1) }
                    GlassButton { compact: true; text: "↓"; enabled: index < pp.pl.items.length - 1; onClicked: pp.pl.moverUi(index, index + 1) }
                    GlassButton { compact: true; text: "✕"; onClicked: pp.pl.quitarUi(index) }
                }
            }
        }
    }
}
