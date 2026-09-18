// Tarjeta "Fuentes": capas de la escena al aire con su visibilidad.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import BA
import "../controls"

Item {
    ColumnLayout {
        anchors.fill: parent
        spacing: 6
        Text { text: Studio.currentScene; color: Theme.textFaint; font.pixelSize: 11 }
        ListView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            spacing: 4
            model: Studio.sources
            delegate: Rectangle {
                required property string name
                required property string typeName
                required property bool shown
                width: ListView.view.width
                height: 36
                radius: 10
                color: Qt.rgba(1, 1, 1, shown ? 0.07 : 0.03)
                RowLayout {
                    anchors { fill: parent; leftMargin: 10; rightMargin: 6 }
                    ColumnLayout {
                        spacing: 0
                        Layout.fillWidth: true
                        Text { text: name; color: shown ? Theme.text : Theme.textFaint; elide: Text.ElideRight; Layout.fillWidth: true; font.pixelSize: 12 }
                        Text { text: typeName; color: Theme.textFaint; elide: Text.ElideRight; Layout.fillWidth: true; font.pixelSize: 9 }
                    }
                    Toggle { checked: shown; onToggled: Studio.sources.setVisible(name, checked) }
                }
            }
        }
    }
}
