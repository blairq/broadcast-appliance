// Un tramo de la página Latencias del pipeline: una curva de luz de un nodo al otro que respira, con
// un pulso que viaja por ella. El pulso tarda más cuanto más tarda el tramo (`ms`): una
// línea lenta late lento. Cian el video, ámbar el audio; apagada si no llega nada.
import QtQuick
import QtQuick.Shapes

Item {
    id: l
    property point desde: Qt.point(0, 0)
    property point hacia: Qt.point(100, 0)
    property string medio: "video"
    property var ms: null
    /// Lo que espera el cuadro en este tramo (el colchón de la entrada antes de la composición), medido.
    property var espera: null
    property bool viva: true
    property bool resaltada: false

    readonly property color color: medio === "audio" ? "#ffb347" : "#4fd8ff"
    readonly property real dx: Math.max(40, (hacia.x - desde.x) * 0.5)
    // El pulso: de 0,6 s (instantáneo) a 4 s (medio segundo o más de demora).
    readonly property int periodo: ms === null || ms === undefined ? 2400 : Math.round(Math.min(4000, 600 + ms * 7))

    // La respiración del trazo.
    property real brillo: 0.55
    SequentialAnimation on brillo {
        running: l.viva
        loops: Animation.Infinite
        NumberAnimation { to: 1.0; duration: l.periodo * 0.5; easing.type: Easing.InOutSine }
        NumberAnimation { to: 0.55; duration: l.periodo * 0.5; easing.type: Easing.InOutSine }
    }

    Shape {
        anchors.fill: parent
        preferredRendererType: Shape.CurveRenderer
        // El halo ancho y tenue (la luz).
        ShapePath {
            strokeColor: Qt.rgba(l.color.r, l.color.g, l.color.b, l.viva ? 0.16 * l.brillo + (l.resaltada ? 0.15 : 0) : 0.04)
            strokeWidth: l.resaltada ? 11 : 8
            fillColor: "transparent"
            capStyle: ShapePath.RoundCap
            startX: l.desde.x; startY: l.desde.y
            PathCubic {
                x: l.hacia.x; y: l.hacia.y
                control1X: l.desde.x + l.dx; control1Y: l.desde.y
                control2X: l.hacia.x - l.dx; control2Y: l.hacia.y
            }
        }
        // El trazo de adentro (el filamento).
        ShapePath {
            strokeColor: Qt.rgba(l.color.r, l.color.g, l.color.b, l.viva ? 0.5 + 0.5 * l.brillo : 0.18)
            strokeWidth: l.resaltada ? 2.4 : 1.6
            fillColor: "transparent"
            capStyle: ShapePath.RoundCap
            strokeStyle: l.viva ? ShapePath.SolidLine : ShapePath.DashLine
            startX: l.desde.x; startY: l.desde.y
            PathCubic {
                x: l.hacia.x; y: l.hacia.y
                control1X: l.desde.x + l.dx; control1Y: l.desde.y
                control2X: l.hacia.x - l.dx; control2Y: l.hacia.y
            }
        }
    }

    // El pulso que viaja: un punto con su halo.
    Item {
        id: pulso
        visible: l.viva
        width: 18; height: 18
        Rectangle { anchors.centerIn: parent; width: 18; height: 18; radius: 9; color: Qt.rgba(l.color.r, l.color.g, l.color.b, 0.22) }
        Rectangle { anchors.centerIn: parent; width: 8; height: 8; radius: 4; color: Qt.rgba(l.color.r, l.color.g, l.color.b, 0.6) }
        Rectangle { anchors.centerIn: parent; width: 4; height: 4; radius: 2; color: "white" }
        PathAnimation {
            target: pulso
            running: l.viva
            loops: Animation.Infinite
            duration: l.periodo
            easing.type: Easing.InOutQuad
            anchorPoint: Qt.point(9, 9)
            path: Path {
                startX: l.desde.x; startY: l.desde.y
                PathCubic {
                    x: l.hacia.x; y: l.hacia.y
                    control1X: l.desde.x + l.dx; control1Y: l.desde.y
                    control2X: l.hacia.x - l.dx; control2Y: l.hacia.y
                }
            }
        }
    }

    // Lo que tarda el tramo, en el medio de la curva.
    Text {
        visible: l.ms !== null && l.ms !== undefined && l.resaltada
        x: (l.desde.x + l.hacia.x) / 2 - width / 2
        y: (l.desde.y + l.hacia.y) / 2 - height - 6
        text: Math.round(l.ms) + " ms"
        color: l.color
        font.pixelSize: 10
    }
    // La espera del tramo, siempre a la vista: es justo lo que no aparece en ninguna tarjeta.
    Rectangle {
        visible: l.espera !== null && l.espera !== undefined && l.espera >= 5
        // Más cerca del origen: en el medio la tapaba la tarjeta de la escena.
        x: l.desde.x + (l.hacia.x - l.desde.x) * 0.3 - width / 2
        y: l.desde.y + (l.hacia.y - l.desde.y) * 0.3 + 4
        width: textoEspera.implicitWidth + 10; height: 15; radius: 7.5
        color: Qt.rgba(0.05, 0.06, 0.08, 0.9)
        border.color: Qt.rgba(l.color.r, l.color.g, l.color.b, 0.6)
        Text {
            id: textoEspera
            anchors.centerIn: parent
            text: "espera " + Math.round(l.espera || 0) + " ms (colchón)"
            color: l.color
            font.pixelSize: 9
        }
    }
}
