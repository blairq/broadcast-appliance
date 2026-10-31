// Un cable de patchbay: la curva de `a` a `b` (sale hacia la derecha y entra por la
// izquierda) y la ✕ en su punto medio para quitarlo. El mismo en los dos patchbays
// (Patchbay y el rack de AudioPage). Sin `quitable`, es el que se está tirando: punteado
// y sin ✕.
import QtQuick
import QtQuick.Controls
import QtQuick.Shapes
import BA

Item {
    id: cable
    required property GeometriaPatchbay geo
    property point a: Qt.point(0, 0)
    property point b: Qt.point(0, 0)
    property color color: Qt.rgba(0.55, 0.75, 1, 0.9)
    property real grosor: 3
    property bool punteado: false
    property bool quitable: true
    /// Qué lleva, chiquito arriba de la ✕ (los canales de un cable de audio: «L→R»).
    property string etiqueta: ""
    signal quitar()
    readonly property real ctl: geo.cableCtl(a, b)
    readonly property point mid: geo.cableMid(a, b)

    Shape {
        anchors.fill: parent
        preferredRendererType: Shape.CurveRenderer
        ShapePath {
            strokeColor: cable.color
            strokeWidth: cable.grosor
            strokeStyle: cable.punteado ? ShapePath.DashLine : ShapePath.SolidLine
            dashPattern: [4, 3]
            capStyle: ShapePath.RoundCap
            fillColor: "transparent"
            startX: cable.a.x; startY: cable.a.y
            PathCubic {
                x: cable.b.x; y: cable.b.y
                control1X: cable.a.x + cable.ctl; control1Y: cable.a.y
                control2X: cable.b.x - cable.ctl; control2Y: cable.b.y
            }
        }
    }
    Rectangle {
        visible: cable.quitable
        x: cable.mid.x - 10
        y: cable.mid.y - 10
        width: 20; height: 20; radius: 10
        color: xMa.containsMouse ? Theme.live : Qt.rgba(0.1, 0.12, 0.18, 0.9)
        border.color: Qt.rgba(1, 1, 1, 0.3)
        Text { anchors.centerIn: parent; text: "✕"; color: "white"; font.pixelSize: 10 }
        Text {
            anchors { bottom: parent.top; horizontalCenter: parent.horizontalCenter }
            visible: cable.etiqueta !== ""
            text: cable.etiqueta
            color: Theme.textFaint; font { pixelSize: 9; weight: Font.DemiBold }
        }
        MouseArea {
            id: xMa
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: cable.quitar()
            ToolTip.visible: containsMouse; ToolTip.text: qsTr("Quitar el cable")
        }
    }
}
