// Tarjeta "Mezclador": nivel, volumen y silencio de cada fuente con audio.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import BA
import "../controls"

Item {
    RowLayout {
        anchors.fill: parent
        spacing: 14

        // Programa (mezcla final)
        ColumnLayout {
            Layout.fillHeight: true
            spacing: 6
            RowLayout {
                Layout.fillHeight: true
                spacing: 4
                LevelMeter { vertical: true; db: Studio.programPeakL; Layout.fillHeight: true }
                LevelMeter { vertical: true; db: Studio.programPeakR; Layout.fillHeight: true }
            }
            Text { text: "Programa"; color: Theme.accentSoft; font { pixelSize: 11; weight: Font.DemiBold } }
        }
        Rectangle { width: 1; Layout.fillHeight: true; color: Qt.rgba(1, 1, 1, 0.1) }

        ListView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            orientation: ListView.Horizontal
            spacing: 14
            clip: true
            model: Studio.mixer
            delegate: ColumnLayout {
                required property string name
                required property real peakDb
                required property real volumeDb
                required property bool muted
                height: ListView.view.height
                width: 70
                spacing: 6
                RowLayout {
                    Layout.fillHeight: true
                    spacing: 6
                    LevelMeter { vertical: true; db: muted ? -100 : peakDb; Layout.fillHeight: true }
                    Slider {
                        orientation: Qt.Vertical
                        Layout.fillHeight: true
                        from: -60; to: 12
                        value: volumeDb
                        onMoved: Studio.mixer.setVolumeDb(name, value)
                        ToolTip.visible: pressed
                        ToolTip.text: value.toFixed(1) + " dB"
                    }
                }
                Rectangle {
                    Layout.fillWidth: true
                    height: 22; radius: 8
                    color: muted ? Qt.rgba(1, 0.23, 0.19, 0.7) : Qt.rgba(1, 1, 1, 0.08)
                    Text { anchors.centerIn: parent; text: muted ? "MUDO" : "M"; color: Theme.text; font { pixelSize: 10; weight: Font.Bold } }
                    MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: Studio.mixer.setMuted(name, !muted) }
                }
                Text { Layout.fillWidth: true; text: name; color: Theme.textDim; elide: Text.ElideRight
                       horizontalAlignment: Text.AlignHCenter; font.pixelSize: 10 }
            }
        }
    }
}
