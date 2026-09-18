// Tarjeta "Monitor": una entrada o salida en vivo (programa o un invitado).
import QtQuick
import QtQuick.Controls
import BA
import "../controls"

Item {
    id: mon
    property int cardId: 0
    property string source: "program"

    Rectangle { anchors.fill: parent; radius: 14; color: "#000000" }
    VideoItem { id: video; anchors.fill: parent; source: mon.source; fillMode: VideoItem.Fit }
    Text {
        visible: !video.hasFrame
        anchors.centerIn: parent
        text: "Sin señal"
        color: Theme.textFaint
        font.pixelSize: 13
    }
    // Sacar a una ventana aparte (sin cortes: mismo servicio de frames).
    Rectangle {
        anchors { right: selector.left; top: parent.top; margins: 8 }
        radius: 10; height: 26; width: 30
        color: popMa.containsMouse ? Qt.rgba(1, 0.42, 0.17, 0.8) : Qt.rgba(0, 0, 0, 0.55)
        border.color: Qt.rgba(1, 1, 1, 0.15)
        Text { anchors.centerIn: parent; text: "⧉"; color: Theme.text; font.pixelSize: 13 }
        MouseArea { id: popMa; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                    onClicked: Studio.dashboard.windows.open([mon.source], "grid") }
        ToolTip.visible: popMa.containsMouse
        ToolTip.text: "Sacar a una ventana (p. ej. otro monitor)"
    }
    // Selector de fuente
    Rectangle {
        id: selector
        anchors { right: parent.right; top: parent.top; margins: 8 }
        radius: 10; height: 26; width: srcLabel.implicitWidth + 30
        color: Qt.rgba(0, 0, 0, 0.55)
        border.color: Qt.rgba(1, 1, 1, 0.15)
        Text { id: srcLabel; anchors.centerIn: parent; text: Studio.frames.label(mon.source) + "  ▾"; color: Theme.text; font.pixelSize: 11 }
        MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: menu.popup() }
        Menu {
            id: menu
            Repeater {
                model: Studio.frames.keys
                delegate: MenuItem {
                    required property string modelData
                    text: Studio.frames.label(modelData)
                    onTriggered: Studio.dashboard.setCardSource(mon.cardId, modelData)
                }
            }
        }
    }
}
