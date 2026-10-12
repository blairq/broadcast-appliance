// Un divisor vertical que se arrastra: cambia el ancho del panel de al lado y lo
// recuerda (Studio.uiValor/uiGuardar, Config/interfaz.json).
//
//   Divisor { clave: "video.panel"; ancho: 320; minimo: 220; maximo: 640 }
//   Panel { Layout.preferredWidth: divisor.ancho }
//
// `alDerecha`: el panel está a la derecha del divisor (arrastrar a la izquierda
// lo agranda).
import QtQuick
import QtQuick.Layouts
import BA

Item {
    id: d
    property string clave: ""
    property real ancho: 320
    property real minimo: 200
    property real maximo: 700
    property bool alDerecha: true
    Layout.preferredWidth: 8
    Layout.fillHeight: true
    implicitWidth: 8

    Component.onCompleted: {
        if (clave === "") return
        const v = Number(Studio.uiValor(clave, ancho))
        if (!isNaN(v)) ancho = Math.max(minimo, Math.min(maximo, v))
    }

    Rectangle {
        anchors.centerIn: parent
        width: ma.containsMouse || ma.pressed ? 3 : 1
        height: parent.height * 0.5
        radius: 2
        color: ma.pressed ? Theme.accent : ma.containsMouse ? Theme.textDim : Theme.glassBorder
    }
    MouseArea {
        id: ma
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.SplitHCursor
        property real x0: 0
        property real ancho0: 0
        onPressed: (m) => { x0 = mapToItem(null, m.x, 0).x; ancho0 = d.ancho }
        onPositionChanged: (m) => {
            if (!pressed) return
            const dx = mapToItem(null, m.x, 0).x - x0
            d.ancho = Math.max(d.minimo, Math.min(d.maximo, ancho0 + (d.alDerecha ? -dx : dx)))
        }
        onReleased: if (d.clave !== "") Studio.uiGuardar(d.clave, Math.round(d.ancho))
        onDoubleClicked: { d.ancho = 320; if (d.clave !== "") Studio.uiGuardar(d.clave, 320) }
    }
}
