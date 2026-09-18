// Tarjeta "Salidas": al aire y grabacion.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import BA
import "../controls"

Item {
    ColumnLayout {
        anchors.fill: parent
        spacing: 8

        component OutputRow: Rectangle {
            id: row
            property string label
            property string time
            property string detail
            property bool on
            property color tone
            property string action
            property string glyph
            signal triggered()
            Layout.fillWidth: true
            Layout.fillHeight: true
            radius: 14
            color: on ? Qt.rgba(tone.r, tone.g, tone.b, 0.22) : Qt.rgba(1, 1, 1, 0.05)
            border.color: on ? Qt.rgba(tone.r, tone.g, tone.b, 0.6) : Qt.rgba(1, 1, 1, 0.08)
            Behavior on color { ColorAnimation { duration: 200 } }
            RowLayout {
                anchors { fill: parent; leftMargin: 12; rightMargin: 10 }
                spacing: 8
                ColumnLayout {
                    spacing: 0
                    Layout.fillWidth: true
                    RowLayout {
                        spacing: 8
                        Text { text: row.label; color: row.on ? row.tone : Theme.textDim
                               font { family: Theme.font; pixelSize: 11; weight: Font.Bold; letterSpacing: 1 } }
                        Text { text: row.detail; color: Theme.textFaint; elide: Text.ElideRight; Layout.fillWidth: true; font.pixelSize: 10 }
                    }
                    Text { text: row.time; color: Theme.text; font { family: Theme.font; pixelSize: 22; weight: Font.Light } }
                }
                GlassButton { text: row.action; icon: row.glyph; compact: true; active: row.on; activeColor: row.tone; onClicked: row.triggered() }
            }
        }

        OutputRow {
            label: Studio.streaming ? "AL AIRE" : "TRANSMISION"
            time: Studio.streamTime
            detail: Studio.streaming ? Studio.streamKbps + " kbps" : (Studio.streamServer || "sin servidor")
            on: Studio.streaming
            tone: Theme.live
            action: Studio.streaming ? "Cortar" : "Salir al aire"
            onTriggered: Studio.streaming ? ApplicationWindow.window.confirmStopStream() : ApplicationWindow.window.openStreamDialog()
        }
        OutputRow {
            label: Studio.recording ? "GRABANDO" : "GRABACION"
            time: Studio.recordTime
            detail: ""
            on: Studio.recording
            tone: Theme.accent
            action: Studio.recording ? "Detener" : "Grabar"
            glyph: Studio.recording ? "■" : "●"
            onTriggered: Studio.toggleRecording()
        }
    }
}
