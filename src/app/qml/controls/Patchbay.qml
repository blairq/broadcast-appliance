// Patchbay genérico: tarjetas con UN puerto de entrada (izquierda) y uno de
// salida (derecha), unidas con cables curvos. Es el del rack de audio
// (AudioPage) sin lo propio del audio (canales L/R, sidechain, medidores):
// lo usa el grafo de video.
//
// Contrato del modelo:
//   nodos:  [{id, tipo, etiqueta, x, y, entradas (0|1), salidas (0|1), resumen?}]
//   cables: [{desde, hacia}]
// La tarjeta se mueve por la cabecera (encaja en la grilla al soltar); un
// cable se tira desde el punto de salida y se suelta sobre una tarjeta o su
// entrada; la ✕ en el medio lo quita.
import QtQuick
import QtQuick.Controls
import QtQuick.Shapes
import BA

Rectangle {
    id: pb
    property var nodos: []
    property var cables: []
    property string seleccionado: ""
    /// El color de la franja de cada tipo de tarjeta.
    property var colorDe: function(tipo) { return Theme.accent }
    signal elegir(string id)
    signal mover(string id, int x, int y)
    signal conectar(string desde, string hacia)
    signal desconectar(string desde, string hacia)

    readonly property int nodeW: 180
    readonly property int headH: 34
    readonly property int bodyH: 52
    readonly property real grid: 20
    property var livePos: ({})
    property int liveTick: 0
    property var wire: null        // {from, x0, y0, x1, y1}
    property string wireTarget: ""
    property bool draggingNode: false

    radius: 16
    color: Qt.rgba(0, 0, 0, 0.25)
    border.color: Qt.rgba(1, 1, 1, 0.06)
    clip: true

    function snap(v) { return Math.round(v / grid) * grid }
    function nodeOf(id) {
        for (let i = 0; i < nodos.length; ++i) if (nodos[i].id === id) return nodos[i]
        return null
    }
    function posOf(n) {
        liveTick
        const p = livePos[n.id]
        return p ? p : Qt.point(n.x, n.y)
    }
    function portPos(id, isIn) {
        const n = nodeOf(id)
        if (!n) return Qt.point(0, 0)
        const p = posOf(n)
        return Qt.point(p.x + (isIn ? 0 : nodeW), p.y + headH + bodyH / 2)
    }
    // La tarjeta (con entrada) más cercana a (x, y): el puerto con margen
    // ancho, o la tarjeta si se soltó encima. Así no hay que apuntarle justo.
    function inputAt(x, y) {
        let best = "", bd = 70
        for (let i = 0; i < nodos.length; ++i) {
            const n = nodos[i]
            if (!n.entradas) continue
            const q = portPos(n.id, true)
            const d = Math.hypot(q.x - x, q.y - y)
            if (d < bd) { bd = d; best = n.id }
        }
        if (best !== "") return best
        for (let i = 0; i < nodos.length; ++i) {
            const n = nodos[i]
            if (!n.entradas) continue
            const p = posOf(n)
            if (x >= p.x && x <= p.x + nodeW && y >= p.y && y <= p.y + headH + bodyH) return n.id
        }
        return ""
    }
    function cableCtl(a, b) { return Math.max(40, Math.min(160, Math.abs(b.x - a.x) / 2 + 20)) }
    function cableMid(a, b) {
        const d = cableCtl(a, b)
        return Qt.point((a.x + 3 * (a.x + d) + 3 * (b.x - d) + b.x) / 8, (a.y + 3 * a.y + 3 * b.y + b.y) / 8)
    }

    // Si el grafo no entra a lo ancho, se achica (hasta 55 %) en vez de
    // dejar tarjetas fuera de vista; con lugar, tamaño normal.
    readonly property real anchoGrafo: {
        let m = 0
        for (let i = 0; i < nodos.length; ++i) m = Math.max(m, nodos[i].x + nodeW)
        return m + 40
    }
    readonly property real escala: Math.max(0.55, Math.min(1, (flick.width - 8) / Math.max(1, anchoGrafo)))

    Flickable {
        id: flick
        anchors.fill: parent
        contentWidth: lienzo.width * pb.escala
        contentHeight: lienzo.height * pb.escala
        interactive: pb.wire === null
        ScrollBar.horizontal: ScrollBar {}
        ScrollBar.vertical: ScrollBar {}

        Item {
            id: lienzo
            width: 2400
            height: 1200
            scale: pb.escala
            transformOrigin: Item.TopLeft
            MouseArea { anchors.fill: parent; onClicked: pb.elegir("") }

            Canvas {
                anchors.fill: parent
                z: -1
                visible: opacity > 0
                opacity: pb.draggingNode || pb.wire !== null ? 1 : 0
                Behavior on opacity { NumberAnimation { duration: 140 } }
                onWidthChanged: requestPaint()
                onHeightChanged: requestPaint()
                onPaint: {
                    const c = getContext("2d")
                    c.reset()
                    c.lineWidth = 1
                    for (let x = 0; x < width; x += pb.grid) {
                        c.strokeStyle = Qt.rgba(1, 1, 1, Math.round(x / pb.grid) % 5 === 0 ? 0.10 : 0.04)
                        c.beginPath(); c.moveTo(Math.round(x) + 0.5, 0); c.lineTo(Math.round(x) + 0.5, height); c.stroke()
                    }
                    for (let y = 0; y < height; y += pb.grid) {
                        c.strokeStyle = Qt.rgba(1, 1, 1, Math.round(y / pb.grid) % 5 === 0 ? 0.10 : 0.04)
                        c.beginPath(); c.moveTo(0, Math.round(y) + 0.5); c.lineTo(width, Math.round(y) + 0.5); c.stroke()
                    }
                }
            }

            // Cables
            Repeater {
                model: pb.cables
                delegate: Item {
                    id: cab
                    required property var modelData
                    readonly property point a: pb.portPos(modelData.desde, false)
                    readonly property point b: pb.portPos(modelData.hacia, true)
                    readonly property real ctl: pb.cableCtl(a, b)
                    readonly property point mid: pb.cableMid(a, b)
                    anchors.fill: parent
                    Shape {
                        anchors.fill: parent
                        preferredRendererType: Shape.CurveRenderer
                        ShapePath {
                            strokeColor: Qt.rgba(0.55, 0.75, 1, 0.9)
                            strokeWidth: 3
                            capStyle: ShapePath.RoundCap
                            fillColor: "transparent"
                            startX: cab.a.x; startY: cab.a.y
                            PathCubic {
                                x: cab.b.x; y: cab.b.y
                                control1X: cab.a.x + cab.ctl; control1Y: cab.a.y
                                control2X: cab.b.x - cab.ctl; control2Y: cab.b.y
                            }
                        }
                    }
                    Rectangle {
                        x: cab.mid.x - 10
                        y: cab.mid.y - 10
                        width: 20; height: 20; radius: 10
                        color: xMa.containsMouse ? Theme.live : Qt.rgba(0.1, 0.12, 0.18, 0.9)
                        border.color: Qt.rgba(1, 1, 1, 0.3)
                        Text { anchors.centerIn: parent; text: "✕"; color: "white"; font.pixelSize: 10 }
                        MouseArea {
                            id: xMa
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: pb.desconectar(cab.modelData.desde, cab.modelData.hacia)
                            ToolTip.visible: containsMouse; ToolTip.text: "Quitar el cable"
                        }
                    }
                }
            }

            // Cable en construcción
            Shape {
                anchors.fill: parent
                visible: pb.wire !== null
                preferredRendererType: Shape.CurveRenderer
                ShapePath {
                    strokeColor: Theme.ok
                    strokeWidth: 3
                    strokeStyle: ShapePath.DashLine
                    fillColor: "transparent"
                    capStyle: ShapePath.RoundCap
                    startX: pb.wire ? pb.wire.x0 : 0
                    startY: pb.wire ? pb.wire.y0 : 0
                    PathCubic {
                        readonly property real d: pb.wire ? pb.cableCtl(Qt.point(pb.wire.x0, pb.wire.y0), Qt.point(pb.wire.x1, pb.wire.y1)) : 60
                        x: pb.wire ? pb.wire.x1 : 0
                        y: pb.wire ? pb.wire.y1 : 0
                        control1X: pb.wire ? pb.wire.x0 + d : 0
                        control1Y: pb.wire ? pb.wire.y0 : 0
                        control2X: pb.wire ? pb.wire.x1 - d : 0
                        control2Y: pb.wire ? pb.wire.y1 : 0
                    }
                }
            }

            // Tarjetas
            Repeater {
                model: pb.nodos
                delegate: Rectangle {
                    id: nd
                    required property var modelData
                    x: modelData.x
                    y: modelData.y
                    width: pb.nodeW
                    height: pb.headH + pb.bodyH
                    radius: 12
                    color: Qt.rgba(0.08, 0.1, 0.15, 0.96)
                    border.width: pb.seleccionado === modelData.id ? 2 : 1
                    border.color: pb.seleccionado === modelData.id ? Theme.accent
                                : pb.wireTarget === modelData.id ? Theme.ok : Qt.rgba(1, 1, 1, 0.14)
                    onXChanged: { pb.livePos[modelData.id] = Qt.point(x, y); pb.liveTick++ }
                    onYChanged: { pb.livePos[modelData.id] = Qt.point(x, y); pb.liveTick++ }

                    Rectangle {
                        id: head
                        width: parent.width
                        height: pb.headH
                        radius: 12
                        color: Qt.rgba(1, 1, 1, 0.06)
                        Rectangle { width: 6; height: parent.height - 12; radius: 3; x: 8; anchors.verticalCenter: parent.verticalCenter; color: pb.colorDe(nd.modelData.tipo) }
                        Text {
                            x: 22
                            width: parent.width - 30
                            anchors.verticalCenter: parent.verticalCenter
                            text: nd.modelData.etiqueta
                            color: Theme.text; font { pixelSize: 13; weight: Font.DemiBold } elide: Text.ElideRight
                        }
                        MouseArea {
                            id: headMa
                            anchors.fill: parent
                            preventStealing: true
                            cursorShape: drag.active ? Qt.ClosedHandCursor : Qt.OpenHandCursor
                            drag.target: nd
                            drag.minimumX: 0; drag.minimumY: 0
                            // No tocar nd.x/nd.y mientras se arrastra (ver AudioPage):
                            // se encaja en la grilla al soltar.
                            drag.onActiveChanged: {
                                pb.draggingNode = drag.active
                                if (drag.active) return
                                nd.x = pb.snap(nd.x)
                                nd.y = pb.snap(nd.y)
                                pb.mover(nd.modelData.id, nd.x, nd.y)
                            }
                            onClicked: pb.elegir(nd.modelData.id)
                        }
                    }
                    Rectangle {
                        visible: headMa.drag.active
                        x: pb.snap(nd.x) - nd.x
                        y: pb.snap(nd.y) - nd.y
                        width: nd.width; height: nd.height
                        z: -1
                        radius: 12
                        color: Qt.rgba(1, 1, 1, 0.05)
                        border { color: Theme.accent; width: 2 }
                    }
                    // Lo que hace (un resumen corto de sus parámetros).
                    Text {
                        anchors { left: parent.left; right: parent.right; top: head.bottom; margins: 22; topMargin: 8 }
                        text: nd.modelData.resumen || ""
                        color: Theme.textFaint; font.pixelSize: 11
                        wrapMode: Text.WordWrap; maximumLineCount: 2; elide: Text.ElideRight
                    }
                    MouseArea {
                        anchors { left: parent.left; right: parent.right; top: head.bottom; bottom: parent.bottom; leftMargin: 14; rightMargin: 14 }
                        onClicked: pb.elegir(nd.modelData.id)
                    }

                    // Entrada (izquierda)
                    Rectangle {
                        visible: nd.modelData.entradas > 0
                        x: -7
                        y: pb.headH + pb.bodyH / 2 - 7
                        width: 14; height: 14; radius: 7
                        color: pb.wireTarget === nd.modelData.id ? Theme.ok : "#93c5fd"
                        border.color: "white"; border.width: 1
                    }
                    // Salida (derecha): de acá se tira el cable.
                    Rectangle {
                        visible: nd.modelData.salidas > 0
                        x: pb.nodeW - 7
                        y: pb.headH + pb.bodyH / 2 - 7
                        width: 14; height: 14; radius: 7
                        color: "#86efac"
                        border.color: "white"; border.width: 1
                        MouseArea {
                            anchors.fill: parent
                            anchors.margins: -12
                            cursorShape: Qt.CrossCursor
                            preventStealing: true
                            hoverEnabled: true
                            ToolTip.visible: containsMouse && !pb.wire
                            ToolTip.delay: 600
                            ToolTip.text: "Arrastrá hasta la tarjeta que quieras alimentar (de una salida pueden salir varios cables)"
                            onPressed: {
                                const p = pb.portPos(nd.modelData.id, false)
                                pb.wire = { from: nd.modelData.id, x0: p.x, y0: p.y, x1: p.x, y1: p.y }
                                pb.wireTarget = ""
                            }
                            onPositionChanged: (ev) => {
                                if (!pb.wire) return
                                const q = mapToItem(lienzo, ev.x, ev.y)
                                const t = pb.inputAt(q.x, q.y)
                                pb.wireTarget = t
                                const end = t !== "" ? pb.portPos(t, true) : q
                                pb.wire = Object.assign({}, pb.wire, { x1: end.x, y1: end.y })
                            }
                            onReleased: {
                                if (!pb.wire) return
                                if (pb.wireTarget !== "" && pb.wireTarget !== pb.wire.from) pb.conectar(pb.wire.from, pb.wireTarget)
                                pb.wire = null
                                pb.wireTarget = ""
                            }
                            onCanceled: { pb.wire = null; pb.wireTarget = "" }
                        }
                    }
                }
            }
        }
    }
}
