// Tarjeta de un orador en la fila: cara, lugar, estado, datos, reloj y
// comandos (al aire, reordenar, tiempo, editar). Solo presentacion: cada
// boton es un pedido a la API (Studio.conference).
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import BA

Rectangle {
    id: card
    required property string uid
    required property string name
    required property string info
    required property string contact
    required property string speaker   // "queued" | "onair" | "done"
    required property int position      // lugar en la fila (solo "queued")
    required property int allottedSec
    required property double onAirSinceMs
    required property double usedMs
    required property bool connected
    required property int slot
    required property string avatar     // data: URL o vacio
    required property int backstageSlot // preview en vivo de la fila (0 = sin lugar)
    required property int index
    /// Reloj de la sala (ms), lo actualiza la pagina.
    property double now: Date.now()
    signal edit()

    readonly property double leftMs: allottedSec * 1000 - (now - onAirSinceMs)
    readonly property bool onAir: speaker === "onair"
    readonly property bool warn: onAir && leftMs >= 0 && leftMs <= 10000
    readonly property bool over: onAir && leftMs < 0
    readonly property var conf: Studio.conference

    function fmt(ms) {
        const neg = ms < 0
        const t = Math.floor(Math.abs(ms) / 1000)
        return (neg ? "+" : "") + Math.floor(t / 60) + ":" + String(t % 60).padStart(2, "0")
    }

    width: 210
    height: col.implicitHeight + 20
    radius: 16
    color: over ? Qt.rgba(1, 0.23, 0.19, 0.14) : onAir ? Qt.rgba(0.21, 0.77, 0.42, 0.12) : Qt.rgba(1, 1, 1, 0.05)
    border.color: over ? Theme.live : warn ? Theme.accent : onAir ? Theme.ok : Theme.glassBorder
    border.width: onAir ? 2 : 1
    opacity: speaker === "done" ? 0.6 : (connected ? 1 : 0.75)

    ColumnLayout {
        id: col
        anchors { left: parent.left; right: parent.right; top: parent.top; margins: 10 }
        spacing: 6

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: width * 3 / 4
            radius: 10
            color: "#000000"
            clip: true
            // Video en vivo: al aire, el del programa; en la fila, su preview
            // (los primeros de la fila). Sin video, la foto que manda su navegador.
            readonly property string liveKey: card.slot > 0 ? "guest:" + card.slot
                                              : card.backstageSlot > 0 ? "backstage:" + card.backstageSlot : ""
            VideoItem { anchors.fill: parent; visible: parent.liveKey !== "" && card.connected; source: parent.liveKey; fillMode: VideoItem.Cover }
            Image {
                anchors.fill: parent
                visible: !(parent.liveKey !== "" && card.connected) && card.avatar !== ""
                source: card.avatar
                fillMode: Image.PreserveAspectCrop
                asynchronous: true
                cache: false
            }
            Text {
                anchors.centerIn: parent
                visible: !(parent.liveKey !== "" && card.connected) && card.avatar === ""
                text: card.name.length > 0 ? card.name.charAt(0).toUpperCase() : "?"
                color: Theme.textFaint
                font { pixelSize: 40; weight: Font.Bold }
            }
            Rectangle {
                anchors { left: parent.left; top: parent.top; margins: 6 }
                width: Math.max(26, ordText.implicitWidth + 12); height: 26; radius: 8
                color: Theme.accent
                Text { id: ordText; anchors.centerIn: parent; color: "#140a04"; font { pixelSize: 14; weight: Font.Black }
                       text: card.speaker === "queued" ? card.position : card.onAir ? "●" : "✓" }
            }
            StatusPill {
                anchors { right: parent.right; top: parent.top; margins: 6 }
                text: !card.connected ? "desconectado" : card.onAir ? "al aire" : card.speaker === "done" ? "habló" : "en fila"
                dot: !card.connected ? Theme.live : card.onAir ? Theme.ok : Theme.textFaint
                pulse: card.onAir
            }
        }
        Text { text: card.name; color: Theme.text; font { pixelSize: 14; weight: Font.DemiBold } elide: Text.ElideRight; Layout.fillWidth: true }
        Text { visible: card.info !== ""; text: card.info; color: Theme.textDim; font.pixelSize: 11; elide: Text.ElideRight; Layout.fillWidth: true }
        Text { visible: card.contact !== ""; text: "☎ " + card.contact; color: Theme.accentSoft; font.pixelSize: 11; elide: Text.ElideRight; Layout.fillWidth: true }
        RowLayout {
            Layout.fillWidth: true
            Text {
                text: card.onAir ? (card.leftMs >= 0 ? card.fmt(Math.ceil(card.leftMs / 1000) * 1000) : card.fmt(card.leftMs))
                      : card.speaker === "done" ? "usó " + card.fmt(card.usedMs) : card.fmt(card.allottedSec * 1000)
                color: card.over ? Theme.live : card.warn ? Theme.accent : Theme.text
                font { pixelSize: card.warn || card.over ? 26 : 20; weight: Font.Black; family: Theme.font }
                Layout.fillWidth: true
            }
            Text { visible: card.over; text: "se pasó"; color: Theme.live; font.pixelSize: 11 }
        }
        Flow {
            Layout.fillWidth: true
            spacing: 4
            GlassButton { compact: true; text: "◀"; onClicked: card.conf.speakerMove(card.uid, card.index - 1) }
            GlassButton { compact: true; text: "▶"; onClicked: card.conf.speakerMove(card.uid, card.index + 1) }
            GlassButton {
                compact: true
                text: card.onAir ? "Sacar" : "Al aire"
                active: !card.onAir; activeColor: Theme.ok
                onClicked: card.conf.speakerOnAir(card.uid, !card.onAir)
            }
            GlassButton { visible: card.speaker === "done"; compact: true; text: "↺"; onClicked: card.conf.speakerRequeue(card.uid) }
            GlassButton { compact: true; text: "✎"; onClicked: card.edit() }
        }
    }
}
