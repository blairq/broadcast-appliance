// Una imagen (video en vivo o foto) para apuntar con el mouse: lo que se toca
// sale en coordenadas de la IMAGEN (0..1), descontadas las bandas de «Fit».
//
//   clic            clic(u, v)
//   arrastrar       cuadro(u, v, ancho, alto): el centro y el tamaño, 0..1
//   rueda           rueda(+1 | -1)
//   doble clic      dobleClic()
//
// Lo que se dibuja encima (el recuadro del PTZ, las caras) va como hijo y se
// ubica con ix, iy, iw, ih (dónde cae la imagen dentro del rectángulo).
// `arrastrable`: un rectángulo (en coordenadas de la imagen) que se puede
// agarrar y mover; mientras se mueve avisa movido(u, v) con su centro nuevo.
import QtQuick
import QtQuick.Controls
import BA

Rectangle {
    id: im
    radius: 10
    color: "black"
    clip: true

    /// "entrada:X" / "entrada-entera:X" (video) o la URL de una foto.
    property string fuente: ""
    readonly property bool esVideo: fuente.startsWith("entrada")
    property string titulo: ""
    property string ayuda: ""
    /// {x, y, w, h} en 0..1 de la imagen, o null: lo que se agarra y se mueve.
    property var arrastrable: null
    default property alias capas: capa.data

    signal clic(real u, real v)
    signal cuadro(real u, real v, real ancho, real alto)
    signal rueda(int sentido)
    signal dobleClic()
    signal movido(real u, real v)
    signal soltado()

    readonly property bool hayImagen: esVideo ? video.hasFrame : foto.status === Image.Ready
    readonly property real aspecto: {
        const w = esVideo ? video.frameSize.width : foto.sourceSize.width
        const h = esVideo ? video.frameSize.height : foto.sourceSize.height
        return w > 0 && h > 0 ? w / h : 16 / 9
    }
    readonly property real iw: Math.min(width, height * aspecto)
    readonly property real ih: iw / aspecto
    readonly property real ix: (width - iw) / 2
    readonly property real iy: (height - ih) / 2
    function u(x) { return Math.max(0, Math.min(1, (x - ix) / iw)) }
    function v(y) { return Math.max(0, Math.min(1, (y - iy) / ih)) }

    VideoItem { id: video; visible: im.esVideo; anchors.fill: parent; source: im.esVideo ? im.fuente : ""; fillMode: VideoItem.Fit }
    Image {
        id: foto
        visible: !im.esVideo
        anchors.fill: parent
        source: im.esVideo ? "" : im.fuente
        fillMode: Image.PreserveAspectFit
        cache: false
        asynchronous: true
    }
    Text {
        visible: !im.hayImagen
        anchors.centerIn: parent
        width: parent.width - 24
        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.WordWrap
        text: im.esVideo ? qsTr("Sin señal") : qsTr("Sin foto todavía")
        color: Theme.textFaint; font.pixelSize: 13
    }
    // Lo que va encima (recuadros, caras): en coordenadas del rectángulo.
    Item { id: capa; anchors.fill: parent; z: 2 }

    // El cuadro que se dibuja.
    Rectangle {
        id: marco
        visible: false
        z: 3
        color: Qt.rgba(1, 0.42, 0.17, 0.15)
        border { color: Theme.accent; width: 2 }
    }
    Text {
        visible: im.titulo !== ""
        anchors { left: parent.left; top: parent.top; margins: 8 }
        z: 4
        text: im.titulo
        color: Theme.text; font { pixelSize: 11; weight: Font.DemiBold; letterSpacing: 0.8 }
        style: Text.Outline; styleColor: Qt.rgba(0, 0, 0, 0.8)
    }
    Text {
        visible: im.ayuda !== ""
        anchors { left: parent.left; right: parent.right; bottom: parent.bottom; margins: 6 }
        z: 4
        horizontalAlignment: Text.AlignHCenter
        elide: Text.ElideRight
        text: im.ayuda
        color: Qt.rgba(1, 1, 1, 0.6); font.pixelSize: 10
        style: Text.Outline; styleColor: Qt.rgba(0, 0, 0, 0.7)
    }

    MouseArea {
        anchors.fill: parent
        z: 1
        cursorShape: moviendo ? Qt.ClosedHandCursor : Qt.CrossCursor
        preventStealing: true
        property point desde
        property bool moviendo: false
        property real dx: 0
        property real dy: 0
        function dentro(m) {
            const a = im.arrastrable
            if (!a) return false
            const x0 = im.ix + (a.x - a.w / 2) * im.iw, y0 = im.iy + (a.y - a.h / 2) * im.ih
            return m.x >= x0 && m.x <= x0 + a.w * im.iw && m.y >= y0 && m.y <= y0 + a.h * im.ih && a.w < 0.999
        }
        onPressed: (m) => {
            desde = Qt.point(m.x, m.y)
            marco.visible = false
            moviendo = dentro(m)
            if (moviendo) { dx = im.u(m.x) - im.arrastrable.x; dy = im.v(m.y) - im.arrastrable.y }
        }
        onPositionChanged: (m) => {
            if (!pressed) return
            if (moviendo) { im.movido(im.u(m.x) - dx, im.v(m.y) - dy); return }
            if (Math.abs(m.x - desde.x) < 6 && Math.abs(m.y - desde.y) < 6) return
            marco.x = Math.min(m.x, desde.x); marco.y = Math.min(m.y, desde.y)
            marco.width = Math.abs(m.x - desde.x); marco.height = Math.abs(m.y - desde.y)
            marco.visible = true
        }
        onReleased: (m) => {
            if (moviendo) { moviendo = false; im.soltado(); return }
            if (marco.visible) {
                marco.visible = false
                im.cuadro(im.u(marco.x + marco.width / 2), im.v(marco.y + marco.height / 2),
                          marco.width / im.iw, marco.height / im.ih)
            } else if (Math.abs(m.x - desde.x) < 6 && Math.abs(m.y - desde.y) < 6) {
                im.clic(im.u(m.x), im.v(m.y))
            }
        }
        onDoubleClicked: im.dobleClic()
        onWheel: (w) => im.rueda(w.angleDelta.y > 0 ? 1 : -1)
    }
}
