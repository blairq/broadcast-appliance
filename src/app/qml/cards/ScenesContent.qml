// Tarjeta "Escenas": tocar una escena la pone al aire con la transicion.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import BA
import "../controls"

Item {
    ColumnLayout {
        anchors.fill: parent
        spacing: 8
        Flow {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 8
            Repeater {
                model: Studio.scenes
                delegate: Rectangle {
                    required property string name
                    required property bool live
                    height: 44
                    width: Math.max(110, label.implicitWidth + 32)
                    radius: 14
                    color: live ? Qt.rgba(1, 0.42, 0.17, 0.85) : (sma.containsMouse ? Qt.rgba(1, 1, 1, 0.14) : Qt.rgba(1, 1, 1, 0.07))
                    border.color: live ? Theme.accentSoft : Qt.rgba(1, 1, 1, 0.1)
                    Behavior on color { ColorAnimation { duration: 180 } }
                    Text { id: label; anchors.centerIn: parent; text: name; color: Theme.text
                           font { family: Theme.font; pixelSize: 13; weight: live ? Font.DemiBold : Font.Normal } }
                    MouseArea { id: sma; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                                onClicked: Studio.takeScene(name) }
                }
            }
        }
        RowLayout {
            Layout.fillWidth: true
            Text { text: "Transicion: " + Studio.transitionType; color: Theme.textFaint; font.pixelSize: 11 }
            Item { Layout.fillWidth: true }
            GlassButton { text: "Editar"; compact: true; onClicked: ApplicationWindow.window.showPage(1) }
        }
    }
}
