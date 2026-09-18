// Panel translucido fijo (paginas de edicion y configuracion).
import QtQuick
import BA

Rectangle {
    id: panel
    property string title: ""
    default property alias content: body.data
    radius: Theme.radius
    color: Qt.rgba(1, 1, 1, 0.04)
    border.color: Theme.glassBorder

    Text {
        id: head
        visible: panel.title !== ""
        anchors { left: parent.left; top: parent.top; leftMargin: 18; topMargin: 14 }
        text: panel.title
        color: Theme.text
        font { family: Theme.font; pixelSize: 15; weight: Font.DemiBold }
    }
    Item {
        id: body
        anchors { fill: parent; margins: 16; topMargin: panel.title !== "" ? 46 : 16 }
    }
}
