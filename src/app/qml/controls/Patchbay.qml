// Patchbay genérico: tarjetas con puertos de entrada (izquierda) y de salida
// (derecha), unidas con cables curvos. Es el del rack de audio (AudioPage)
// sin lo propio del audio (canales L/R, sidechain, medidores): lo usa el
// grafo de video.
//
// Contrato del modelo:
//   nodos:  [{id, tipo, etiqueta, x, y, resumen?, externo?,
//             entradas: [{id, pedido?}], salidas: [{id}]}]
//   cables: [{desde, salida, hacia, entrada}]
// Un puerto `pedido` es uno libre de los que se crean al conectar (los de un
// compositor): se dibuja hueco, con «+». Un nodo `externo` es de otra parte
// del grafo (se ve atenuado y no se mueve desde acá).
// La tarjeta se mueve por la cabecera (encaja en la grilla al soltar); un
// cable se tira desde un punto de salida y se suelta sobre una entrada o una
// tarjeta; la ✕ en el medio lo quita.
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
    signal conectar(string desde, string salida, string hacia, string entrada)
    signal desconectar(string desde, string salida, string hacia, string entrada)

    readonly property int nodeW: 180
    readonly property int headH: 34
    readonly property int rowH: 24
    readonly property int bodyH: 40
    readonly property real grid: 20
    property var livePos: ({})
    property int liveTick: 0
    property var wire: null        // {from, out, x0, y0, x1, y1}
    property var wireTarget: null  // {node, port}
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
    function filas(n) { return Math.max((n.entradas || []).length, (n.salidas || []).length, 1) }
    function nodeH(n) { return headH + filas(n) * rowH + bodyH }
    function posOf(n) {
        liveTick
        const p = livePos[n.id]
        return p ? p : Qt.point(n.x, n.y)
    }
    function portPos(id, port, isIn) {
        const n = nodeOf(id)
        if (!n) return Qt.point(0, 0)
        const lista = (isIn ? n.entradas : n.salidas) || []
        let i = 0
        for (let k = 0; k < lista.length; ++k) if (lista[k].id === port) i = k
        const p = posOf(n)
        return Qt.point(p.x + (isIn ? 0 : nodeW), p.y + headH + i * rowH + rowH / 2)
    }
    // La entrada a la que iría un cable soltado en (x, y): el puerto más
    // cercano (con margen ancho) o, sobre una tarjeta, su primer puerto libre.
    function inputAt(x, y) {
        let best = null, bd = 40
        for (let i = 0; i < nodos.length; ++i) {
            const n = nodos[i]
            for (const pt of (n.entradas || [])) {
                const q = portPos(n.id, pt.id, true)
                const d = Math.hypot(q.x - x, q.y - y)
                if (d < bd) { bd = d; best = { node: n.id, port: pt.id } }
            }
        }
        if (best) return best
        for (let i = 0; i < nodos.length; ++i) {
            const n = nodos[i]
            const es = n.entradas || []
            if (es.length === 0) continue
            const p = posOf(n)
            if (x >= p.x && x <= p.x + nodeW && y >= p.y && y <= p.y + nodeH(n)) {
                let libre = es[0]
                for (const pt of es) if (pt.pedido) libre = pt
                return { node: n.id, port: libre.id }
            }
        }
        return null
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
            height: 1600
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
                    readonly property point a: pb.portPos(modelData.desde, modelData.salida, false)
                    readonly property point b: pb.portPos(modelData.hacia, modelData.entrada, true)
                    readonly property real ctl: pb.cableCtl(a, b)
                    readonly property point mid: pb.cableMid(a, b)
                    readonly property bool cruza: {
                        const d = pb.nodeOf(modelData.desde), h = pb.nodeOf(modelData.hacia)
                        return !!(d && d.externo) || !!(h && h.externo)
                    }
                    anchors.fill: parent
                    Shape {
                        anchors.fill: parent
                        preferredRendererType: Shape.CurveRenderer
                        ShapePath {
                            // Un cable que sale de (o va a) otra parte del grafo, en otro tono.
                            strokeColor: cab.cruza ? Qt.rgba(0.78, 0.6, 1, 0.9) : Qt.rgba(0.55, 0.75, 1, 0.9)
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
                            onClicked: pb.desconectar(cab.modelData.desde, cab.modelData.salida, cab.modelData.hacia, cab.modelData.entrada)
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
                    readonly property bool externo: !!modelData.externo
                    x: modelData.x
                    y: modelData.y
                    width: pb.nodeW
                    height: pb.nodeH(modelData)
                    radius: 12
                    color: Qt.rgba(0.08, 0.1, 0.15, nd.externo ? 0.7 : 0.96)
                    opacity: nd.externo ? 0.75 : 1
                    border.width: pb.seleccionado === modelData.id ? 2 : 1
                    border.color: pb.seleccionado === modelData.id ? Theme.accent
                                : (pb.wireTarget && pb.wireTarget.node === modelData.id) ? Theme.ok
                                : nd.externo ? Qt.rgba(0.78, 0.6, 1, 0.6) : Qt.rgba(1, 1, 1, 0.14)
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
                            text: (nd.externo ? "↗ " : "") + nd.modelData.etiqueta
                            color: Theme.text; font { pixelSize: 13; weight: Font.DemiBold } elide: Text.ElideRight
                        }
                        MouseArea {
                            id: headMa
                            anchors.fill: parent
                            preventStealing: true
                            cursorShape: nd.externo ? Qt.PointingHandCursor : drag.active ? Qt.ClosedHandCursor : Qt.OpenHandCursor
                            drag.target: nd.externo ? null : nd
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
                        anchors { left: parent.left; right: parent.right; bottom: parent.bottom; margins: 14; bottomMargin: 6 }
                        height: pb.bodyH - 8
                        text: nd.modelData.resumen || ""
                        color: Theme.textFaint; font.pixelSize: 11
                        verticalAlignment: Text.AlignVCenter
                        wrapMode: Text.WordWrap; maximumLineCount: 2; elide: Text.ElideRight
                    }
                    MouseArea {
                        anchors { left: parent.left; right: parent.right; top: head.bottom; bottom: parent.bottom; leftMargin: 14; rightMargin: 14 }
                        onClicked: pb.elegir(nd.modelData.id)
                    }

                    // Entradas (izquierda)
                    Repeater {
                        model: nd.modelData.entradas || []
                        delegate: Item {
                            id: ip
                            required property var modelData
                            required property int index
                            readonly property bool blanco: !!pb.wireTarget && pb.wireTarget.node === nd.modelData.id && pb.wireTarget.port === modelData.id
                            x: -7
                            y: pb.headH + index * pb.rowH + pb.rowH / 2 - 7
                            width: 14; height: 14
                            Rectangle {
                                anchors.fill: parent
                                radius: 7
                                color: ip.blanco ? Theme.ok : ip.modelData.pedido ? "transparent" : "#93c5fd"
                                border.color: ip.modelData.pedido ? "#93c5fd" : "white"
                                border.width: ip.modelData.pedido ? 2 : 1
                                Text { visible: !!ip.modelData.pedido; anchors.centerIn: parent; text: "+"; color: "#93c5fd"; font.pixelSize: 10; font.weight: Font.Black }
                            }
                            Text {
                                visible: (nd.modelData.entradas || []).length > 1
                                x: 18
                                anchors.verticalCenter: parent.verticalCenter
                                text: ip.modelData.pedido ? "nueva" : ip.modelData.id
                                color: Theme.textFaint; font.pixelSize: 10
                            }
                        }
                    }
                    // Salidas (derecha): de acá se tira el cable.
                    Repeater {
                        model: nd.modelData.salidas || []
                        delegate: Rectangle {
                            id: op
                            required property var modelData
                            required property int index
                            x: pb.nodeW - 7
                            y: pb.headH + index * pb.rowH + pb.rowH / 2 - 7
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
                                ToolTip.text: "Arrastrá hasta la tarjeta (o la entrada) que quieras alimentar: de una salida pueden salir varios cables"
                                onPressed: {
                                    const p = pb.portPos(nd.modelData.id, op.modelData.id, false)
                                    pb.wire = { from: nd.modelData.id, out: op.modelData.id, x0: p.x, y0: p.y, x1: p.x, y1: p.y }
                                    pb.wireTarget = null
                                }
                                onPositionChanged: (ev) => {
                                    if (!pb.wire) return
                                    const q = mapToItem(lienzo, ev.x, ev.y)
                                    const t = pb.inputAt(q.x, q.y)
                                    pb.wireTarget = t
                                    const end = t ? pb.portPos(t.node, t.port, true) : q
                                    pb.wire = Object.assign({}, pb.wire, { x1: end.x, y1: end.y })
                                }
                                onReleased: {
                                    if (!pb.wire) return
                                    const t = pb.wireTarget
                                    if (t && t.node !== pb.wire.from) pb.conectar(pb.wire.from, pb.wire.out, t.node, t.port)
                                    pb.wire = null
                                    pb.wireTarget = null
                                }
                                onCanceled: { pb.wire = null; pb.wireTarget = null }
                            }
                        }
                    }
                }
            }
        }
    }
}
