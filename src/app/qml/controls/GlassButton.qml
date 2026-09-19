// Boton translucido (solo presentacion).
import QtQuick
import BA

Rectangle {
    id: btn
    property string text: ""
    property string icon: ""
    property bool active: false
    property color activeColor: Theme.accent
    property bool compact: false
    /// El mouse esta encima (para ToolTip.visible, como en los controles de Qt).
    readonly property bool hovered: ma.containsMouse
    signal clicked()

    implicitHeight: compact ? 30 : 38
    implicitWidth: row.implicitWidth + (compact ? 20 : 28)
    radius: height / 2
    color: active ? activeColor : (ma.containsMouse ? Qt.rgba(1, 1, 1, 0.14) : Qt.rgba(1, 1, 1, 0.07))
    border.color: active ? Qt.lighter(activeColor, 1.3) : Qt.rgba(1, 1, 1, 0.14)
    Behavior on color { ColorAnimation { duration: 140 } }
    scale: ma.pressed ? 0.96 : 1.0
    Behavior on scale { NumberAnimation { duration: 90 } }

    Row {
        id: row
        anchors.centerIn: parent
        spacing: 7
        Text {
            visible: btn.icon !== ""
            text: btn.icon
            color: Theme.text
            font { pixelSize: btn.compact ? 12 : 14 }
            anchors.verticalCenter: parent.verticalCenter
        }
        Text {
            visible: btn.text !== ""
            text: btn.text
            color: Theme.text
            font { family: Theme.font; pixelSize: btn.compact ? 12 : 13; weight: Font.Medium }
            anchors.verticalCenter: parent.verticalCenter
        }
    }
    MouseArea { id: ma; anchors.fill: parent; hoverEnabled: true; onClicked: btn.clicked(); cursorShape: Qt.PointingHandCursor }
}
