// Tarjeta "Invitados": sala, link de ingreso y miniaturas de cada invitado.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import BA
import "../controls"

Item {
    ColumnLayout {
        anchors.fill: parent
        spacing: 8
        RowLayout {
            Layout.fillWidth: true
            StatusPill { text: Studio.gatewayAlive ? "Gateway" : "Gateway caido"; dot: Studio.gatewayAlive ? Theme.ok : Theme.live; pulse: !Studio.gatewayAlive }
            Item { Layout.fillWidth: true }
            Text { text: "Sala: " + Studio.room; color: Theme.textDim; font.pixelSize: 12 }
        }
        RowLayout {
            Layout.fillWidth: true
            TextField {
                Layout.fillWidth: true
                readOnly: true
                text: Studio.joinUrl
                font.pixelSize: 11
                selectByMouse: true
            }
            GlassButton {
                text: "Copiar"; compact: true
                onClicked: { urlHelper.text = Studio.joinUrl; urlHelper.selectAll(); urlHelper.copy(); ApplicationWindow.window.toast("Link copiado", "ok") }
            }
            TextEdit { id: urlHelper; visible: false }
        }
        GridLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            columns: Math.max(1, Math.min(Studio.guests, 2))
            columnSpacing: 8; rowSpacing: 8
            Repeater {
                model: Studio.guests
                delegate: Rectangle {
                    required property int index
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    radius: 12
                    color: "#000000"
                    clip: true
                    VideoItem { id: v; anchors.fill: parent; source: "guest:" + (index + 1); fillMode: VideoItem.Cover }
                    Rectangle {
                        anchors { left: parent.left; bottom: parent.bottom; margins: 6 }
                        radius: 8; height: 20; width: gl.implicitWidth + 14
                        color: Qt.rgba(0, 0, 0, 0.55)
                        Text { id: gl; anchors.centerIn: parent; text: Studio.conference.slotNames[index] || ("Invitado " + (index + 1)); color: Theme.text; font.pixelSize: 10 }
                    }
                }
            }
        }
    }
}
