// Pagina Audio: patchbay tipo JACK por fuente. Cada fuente con audio (microfono,
// placa, video con sonido, invitado) tiene su rack: modulos LSP conectados con
// cables. Arrastrar de una salida (punto a la derecha) a una entrada (punto a la
// izquierda) conecta; la ✕ en el medio de un cable lo quita. A la derecha, los
// parametros del modulo elegido (y su interfaz LSP completa), sus medidores, o
// el analisis (LUFS, espectro, piso de ruido) de la entrada, la salida o un
// analizador. Todo esto tambien lo maneja el asistente por MCP (audio_*).
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Shapes
import BA
import "../controls"

Rectangle {
    id: page
    color: Theme.bg
    property real leftInset: 92
    readonly property var au: Studio.audio
    property string msg: ""
    onVisibleChanged: if (au) au.setActive(visible)
    Component.onCompleted: if (au) au.setActive(visible)

    // --- Geometria de los nodos -------------------------------------------------------------
    readonly property int nodeW: 178
    readonly property int headH: 34
    readonly property int portH: 22
    readonly property var nodes: au && au.graph.nodos ? au.graph.nodos : []
    readonly property var cables: au && au.graph.cables ? au.graph.cables : []
    // Posiciones mientras se arrastra un nodo (los cables las siguen en vivo).
    property var livePos: ({})
    property int liveTick: 0
    // Cable en construccion: {from, out, x0, y0, x1, y1}
    property var wire: null

    function nodeOf(id) {
        for (let i = 0; i < nodes.length; ++i) if (nodes[i].id === id) return nodes[i]
        return null
    }
    function nodeH(n) { return headH + Math.max(n.entradas.length, n.salidas.length, 1) * portH + 30 }
    function posOf(n) {
        liveTick // dependencia
        const p = livePos[n.id]
        return p ? p : Qt.point(n.x, n.y)
    }
    function portPos(nodeId, port, isIn) {
        const n = nodeOf(nodeId)
        if (!n) return Qt.point(0, 0)
        const list = isIn ? n.entradas : n.salidas
        const i = Math.max(0, list.indexOf(port))
        const p = posOf(n)
        return Qt.point(p.x + (isIn ? 0 : nodeW), p.y + headH + i * portH + portH / 2)
    }
    function kindColor(t) {
        if (t === "in" || t === "out") return "#64748b"
        if (t === "analyzer") return "#0ea5e9"
        if (t === "generator") return "#f59e0b"
        return Theme.accent
    }
    function fmtDb(v) { return (v === undefined || v === null || v <= -139) ? "—" : (Math.round(v * 10) / 10).toFixed(1) }
    // Entrada mas cercana a (x, y) del lienzo, o null.
    function inputAt(x, y) {
        let best = null, bd = 18
        for (let i = 0; i < nodes.length; ++i) {
            const n = nodes[i]
            for (let k = 0; k < n.entradas.length; ++k) {
                const p = portPos(n.id, n.entradas[k], true)
                const d = Math.hypot(p.x - x, p.y - y)
                if (d < bd) { bd = d; best = { node: n.id, port: n.entradas[k] } }
            }
        }
        return best
    }

    Text {
        anchors.centerIn: parent
        visible: !page.au
        text: "Esta versión no trae los módulos de audio (CLAP/LSP)."
        color: Theme.textDim; font.pixelSize: 15
    }

    RowLayout {
        visible: !!page.au
        anchors { fill: parent; leftMargin: page.leftInset; margins: 18 }
        spacing: 14

        // --- Fuentes con audio --------------------------------------------------------------
        Panel {
            Layout.preferredWidth: 250
            Layout.fillHeight: true
            title: "Fuentes con audio"
            ListView {
                id: srcList
                anchors.fill: parent
                clip: true
                spacing: 6
                model: page.au ? page.au.sources : []
                delegate: Rectangle {
                    required property var modelData
                    width: srcList.width
                    height: 56
                    radius: 12
                    color: page.au && page.au.source === modelData.nombre ? Qt.rgba(1, 1, 1, 0.12) : Qt.rgba(1, 1, 1, 0.04)
                    border.color: page.au && page.au.source === modelData.nombre ? Theme.accent : "transparent"
                    ColumnLayout {
                        anchors { fill: parent; margins: 8 }
                        spacing: 3
                        RowLayout {
                            Text { text: modelData.nombre; color: Theme.text; font.pixelSize: 13; elide: Text.ElideRight; Layout.fillWidth: true }
                            Text { visible: modelData.rack; text: "🎚 " + (modelData.modulos || 0); color: Theme.accent; font.pixelSize: 12 }
                        }
                        LevelMeter {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 6
                            db: modelData.salida && modelData.salida.salidaDb !== undefined ? modelData.salida.salidaDb : -100
                        }
                    }
                    MouseArea { anchors.fill: parent; onClicked: page.au.selectSource(modelData.nombre) }
                }
                Text { anchors.centerIn: parent; visible: srcList.count === 0; text: "No hay fuentes con audio."; color: Theme.textFaint }
            }
        }

        // --- Patchbay ---------------------------------------------------------------------
        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 10

            Flow {
                Layout.fillWidth: true
                spacing: 6
                Text { text: page.au ? page.au.source : ""; color: Theme.text; font { pixelSize: 17; weight: Font.DemiBold } }
                Item { width: 12; height: 1 }
                Repeater {
                    model: page.au ? page.au.catalog : []
                    delegate: GlassButton {
                        required property var modelData
                        compact: true
                        text: "+ " + modelData.titulo
                        onClicked: page.msg = page.au.addModule(modelData.clave, "")
                        ToolTip.visible: hovered; ToolTip.text: modelData.ayuda; ToolTip.delay: 400
                    }
                }
                GlassButton { compact: true; text: "+ Analizador"; onClicked: page.msg = page.au.addAnalyzer() }
                GlassButton { compact: true; text: "+ Generador"; onClicked: page.msg = page.au.addGenerator() }
                GlassButton { compact: true; text: "+ Otro LSP…"; onClicked: lspPopup.open() }
            }
            Text {
                Layout.fillWidth: true
                text: page.msg !== "" ? page.msg
                      : (page.au && page.au.error !== "" ? page.au.error
                         : "Arrastrá desde una salida (●, derecha) a una entrada (●, izquierda) para conectar. La ✕ en el medio de un cable lo quita. Varios cables a una entrada se suman.")
                color: page.msg !== "" || (page.au && page.au.error !== "") ? Theme.live : Theme.textFaint
                font.pixelSize: 12; wrapMode: Text.WordWrap
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                radius: 16
                color: Qt.rgba(0, 0, 0, 0.25)
                border.color: Qt.rgba(1, 1, 1, 0.06)
                clip: true

                Flickable {
                    id: flick
                    anchors.fill: parent
                    contentWidth: 2600
                    contentHeight: 1400
                    interactive: page.wire === null
                    ScrollBar.horizontal: ScrollBar {}
                    ScrollBar.vertical: ScrollBar {}

                    Item {
                        id: canvas
                        width: flick.contentWidth
                        height: flick.contentHeight
                        MouseArea { anchors.fill: parent; onClicked: page.au.select("") }

                        // Cables
                        Repeater {
                            model: page.cables
                            delegate: Item {
                                id: cab
                                required property var modelData
                                readonly property point a: page.portPos(modelData.desde, modelData.salida, false)
                                readonly property point b: page.portPos(modelData.hacia, modelData.entrada, true)
                                readonly property real dx: Math.max(40, Math.abs(b.x - a.x) / 2)
                                anchors.fill: parent
                                Shape {
                                    anchors.fill: parent
                                    preferredRendererType: Shape.CurveRenderer
                                    ShapePath {
                                        strokeColor: modelData.entrada === "sc" ? "#f59e0b" : Qt.rgba(0.55, 0.75, 1, 0.9)
                                        strokeWidth: 3
                                        fillColor: "transparent"
                                        startX: cab.a.x; startY: cab.a.y
                                        PathCubic {
                                            x: cab.b.x; y: cab.b.y
                                            control1X: cab.a.x + cab.dx; control1Y: cab.a.y
                                            control2X: cab.b.x - cab.dx; control2Y: cab.b.y
                                        }
                                    }
                                }
                                Rectangle {
                                    x: (cab.a.x + cab.b.x) / 2 - 10
                                    y: (cab.a.y + cab.b.y) / 2 - 10
                                    width: 20; height: 20; radius: 10
                                    color: xMa.containsMouse ? Theme.live : Qt.rgba(0.1, 0.12, 0.18, 0.9)
                                    border.color: Qt.rgba(1, 1, 1, 0.3)
                                    Text { anchors.centerIn: parent; text: "✕"; color: "white"; font.pixelSize: 10 }
                                    MouseArea {
                                        id: xMa
                                        anchors.fill: parent
                                        hoverEnabled: true
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: page.au.disconnectPorts(cab.modelData.desde, cab.modelData.salida, cab.modelData.hacia, cab.modelData.entrada)
                                        ToolTip.visible: containsMouse; ToolTip.text: "Quitar el cable"
                                    }
                                }
                            }
                        }

                        // Cable en construccion
                        Shape {
                            anchors.fill: parent
                            visible: page.wire !== null
                            preferredRendererType: Shape.CurveRenderer
                            ShapePath {
                                strokeColor: Theme.ok
                                strokeWidth: 3
                                strokeStyle: ShapePath.DashLine
                                fillColor: "transparent"
                                startX: page.wire ? page.wire.x0 : 0; startY: page.wire ? page.wire.y0 : 0
                                PathCubic {
                                    x: page.wire ? page.wire.x1 : 0; y: page.wire ? page.wire.y1 : 0
                                    control1X: page.wire ? page.wire.x0 + 60 : 0; control1Y: page.wire ? page.wire.y0 : 0
                                    control2X: page.wire ? page.wire.x1 - 60 : 0; control2Y: page.wire ? page.wire.y1 : 0
                                }
                            }
                        }

                        // Nodos
                        Repeater {
                            model: page.nodes
                            delegate: Rectangle {
                                id: nd
                                required property var modelData
                                readonly property var m: page.au && page.au.meters[modelData.id] ? page.au.meters[modelData.id] : ({})
                                x: modelData.x
                                y: modelData.y
                                width: page.nodeW
                                height: page.nodeH(modelData)
                                radius: 12
                                color: Qt.rgba(0.08, 0.1, 0.15, 0.96)
                                border.width: page.au && page.au.selected === modelData.id ? 2 : 1
                                border.color: page.au && page.au.selected === modelData.id ? Theme.accent : Qt.rgba(1, 1, 1, 0.14)
                                opacity: modelData.bypass ? 0.55 : 1
                                onXChanged: { page.livePos[modelData.id] = Qt.point(x, y); page.liveTick++ }
                                onYChanged: { page.livePos[modelData.id] = Qt.point(x, y); page.liveTick++ }

                                // Cabecera: elegir y arrastrar.
                                Rectangle {
                                    id: head
                                    width: parent.width
                                    height: page.headH
                                    radius: 12
                                    color: Qt.rgba(1, 1, 1, 0.06)
                                    Rectangle { width: 6; height: parent.height - 12; radius: 3; x: 8; anchors.verticalCenter: parent.verticalCenter; color: page.kindColor(nd.modelData.tipo) }
                                    Text {
                                        x: 22
                                        width: parent.width - 30
                                        anchors.verticalCenter: parent.verticalCenter
                                        text: nd.modelData.etiqueta
                                        color: Theme.text; font { pixelSize: 13; weight: Font.DemiBold } elide: Text.ElideRight
                                    }
                                    MouseArea {
                                        anchors.fill: parent
                                        drag.target: nd
                                        drag.minimumX: 0; drag.minimumY: 0
                                        preventStealing: true
                                        cursorShape: Qt.OpenHandCursor
                                        onClicked: page.au.select(nd.modelData.id)
                                        onReleased: if (drag.active || nd.x !== nd.modelData.x || nd.y !== nd.modelData.y) page.au.moveNode(nd.modelData.id, nd.x, nd.y)
                                    }
                                }

                                // Puertos de entrada (izquierda)
                                Repeater {
                                    model: nd.modelData.entradas
                                    delegate: Item {
                                        required property string modelData
                                        required property int index
                                        x: -7
                                        y: page.headH + index * page.portH + page.portH / 2 - 7
                                        width: 14; height: 14
                                        Rectangle { anchors.fill: parent; radius: 7; color: modelData === "sc" ? "#f59e0b" : "#93c5fd"; border.color: "white"; border.width: 1 }
                                        Text { x: 18; anchors.verticalCenter: parent.verticalCenter; text: modelData === "sc" ? "sidechain" : "entrada"; color: Theme.textFaint; font.pixelSize: 10 }
                                    }
                                }
                                // Puertos de salida (derecha): se arrastran para conectar.
                                Repeater {
                                    model: nd.modelData.salidas
                                    delegate: Item {
                                        id: op
                                        required property string modelData
                                        required property int index
                                        x: page.nodeW - 7
                                        y: page.headH + index * page.portH + page.portH / 2 - 7
                                        width: 14; height: 14
                                        Text { anchors { right: parent.left; rightMargin: 4; verticalCenter: parent.verticalCenter } text: "salida"; color: Theme.textFaint; font.pixelSize: 10 }
                                        Rectangle { anchors.fill: parent; radius: 7; color: "#86efac"; border.color: "white"; border.width: 1 }
                                        MouseArea {
                                            anchors.fill: parent
                                            anchors.margins: -6
                                            cursorShape: Qt.CrossCursor
                                            preventStealing: true
                                            onPressed: (ev) => {
                                                const p = page.portPos(nd.modelData.id, op.modelData, false)
                                                page.wire = { from: nd.modelData.id, out: op.modelData, x0: p.x, y0: p.y, x1: p.x, y1: p.y }
                                            }
                                            onPositionChanged: (ev) => {
                                                if (!page.wire) return
                                                const q = mapToItem(canvas, ev.x, ev.y)
                                                page.wire = Object.assign({}, page.wire, { x1: q.x, y1: q.y })
                                            }
                                            onReleased: (ev) => {
                                                if (!page.wire) return
                                                const q = mapToItem(canvas, ev.x, ev.y)
                                                const t = page.inputAt(q.x, q.y)
                                                if (t) page.msg = page.au.connectPorts(page.wire.from, page.wire.out, t.node, t.port)
                                                page.wire = null
                                            }
                                        }
                                    }
                                }

                                // Medidor del nodo
                                Column {
                                    anchors { left: parent.left; right: parent.right; bottom: parent.bottom; margins: 10 }
                                    spacing: 3
                                    LevelMeter {
                                        width: parent.width
                                        height: 5
                                        db: nd.m.salidaDb !== undefined ? nd.m.salidaDb : (nd.m.entradaDb !== undefined ? nd.m.entradaDb : -100)
                                    }
                                    Text {
                                        width: parent.width
                                        horizontalAlignment: Text.AlignRight
                                        color: nd.m.cambioDb !== undefined && nd.m.cambioDb < -0.5 ? Theme.amber : Theme.textFaint
                                        font.pixelSize: 10
                                        text: nd.modelData.bypass ? "anulado"
                                              : nd.m.cambioDb !== undefined ? (nd.m.cambioDb > 0 ? "+" : "") + page.fmtDb(nd.m.cambioDb) + " dB"
                                              : page.fmtDb(nd.m.salidaDb !== undefined ? nd.m.salidaDb : nd.m.entradaDb) + " dBFS"
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }

        // --- Detalle del nodo -------------------------------------------------------------
        Panel {
            Layout.preferredWidth: 360
            Layout.fillHeight: true
            title: page.au && page.au.detail.nodo ? (page.au.detail.etiqueta || page.au.detail.nombre || page.au.detail.nodo) : "Detalle"
            ScrollView {
                id: detScroll
                anchors.fill: parent
                // Ancho fijo (sin availableWidth): evita el ciclo barra <-> alto del contenido.
                contentWidth: width - 14
                clip: true
                ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
                ColumnLayout {
                    width: detScroll.width - 14
                    spacing: 8
                    readonly property var d: page.au ? page.au.detail : ({})

                    Text {
                        Layout.fillWidth: true
                        visible: !parent.d.nodo
                        text: "Elegí un nodo del patchbay. La entrada y la salida muestran el análisis del audio (LUFS, espectro, piso de ruido); los módulos, sus parámetros."
                        color: Theme.textDim; font.pixelSize: 13; wrapMode: Text.WordWrap
                    }

                    // Modulo: acciones
                    Flow {
                        Layout.fillWidth: true
                        spacing: 6
                        visible: parent.d.tipo === "plugin"
                        GlassButton { compact: true; text: parent.parent.d.bypass ? "Activar" : "Anular"; onClicked: page.au.setBypass(page.au.selected, !parent.parent.d.bypass) }
                        GlassButton { compact: true; text: "Interfaz LSP"; enabled: !!parent.parent.d.tieneEditor; onClicked: page.msg = page.au.openEditor(page.au.selected) }
                        Repeater {
                            model: parent.parent.d.presets || []
                            delegate: GlassButton { required property string modelData; compact: true; text: "Preset: " + modelData; onClicked: page.msg = page.au.applyPreset(page.au.selected, modelData) }
                        }
                        GlassButton { compact: true; text: "Quitar"; onClicked: page.au.removeNode(page.au.selected) }
                    }
                    Flow {
                        Layout.fillWidth: true
                        spacing: 6
                        visible: parent.d.tipo === "analyzer" || parent.d.tipo === "generator"
                        GlassButton { compact: true; text: "Quitar"; onClicked: page.au.removeNode(page.au.selected) }
                    }
                    Text {
                        Layout.fillWidth: true
                        visible: !!parent.d.ayuda
                        text: parent.d.ayuda || ""
                        color: Theme.textFaint; font.pixelSize: 11; wrapMode: Text.WordWrap
                    }

                    // Medidores
                    GridLayout {
                        Layout.fillWidth: true
                        visible: !!parent.d.medidores && parent.d.tipo !== undefined
                        columns: 2
                        columnSpacing: 8
                        Text { text: "Entrada"; color: Theme.textDim; font.pixelSize: 12; visible: !!(parent.parent.d.medidores && parent.parent.d.medidores.entradaDb !== undefined) }
                        LevelMeter { Layout.fillWidth: true; Layout.preferredHeight: 8; visible: !!(parent.parent.d.medidores && parent.parent.d.medidores.entradaDb !== undefined)
                                     db: parent.parent.d.medidores && parent.parent.d.medidores.entradaDb !== undefined ? parent.parent.d.medidores.entradaDb : -100 }
                        Text { text: "Salida"; color: Theme.textDim; font.pixelSize: 12; visible: !!(parent.parent.d.medidores && parent.parent.d.medidores.salidaDb !== undefined) }
                        LevelMeter { Layout.fillWidth: true; Layout.preferredHeight: 8; visible: !!(parent.parent.d.medidores && parent.parent.d.medidores.salidaDb !== undefined)
                                     db: parent.parent.d.medidores && parent.parent.d.medidores.salidaDb !== undefined ? parent.parent.d.medidores.salidaDb : -100 }
                        Text { text: "Cambio"; color: Theme.textDim; font.pixelSize: 12; visible: !!(parent.parent.d.medidores && parent.parent.d.medidores.cambioDb !== undefined) }
                        Text { visible: !!(parent.parent.d.medidores && parent.parent.d.medidores.cambioDb !== undefined)
                               text: parent.parent.d.medidores ? page.fmtDb(parent.parent.d.medidores.cambioDb) + " dB" : ""
                               color: Theme.amber; font { pixelSize: 13; weight: Font.DemiBold } }
                    }

                    // Parametros del modulo (catalogo)
                    Repeater {
                        model: parent.d.parametros ? Object.keys(parent.d.parametros).filter(k => k !== "bandas" && k !== "opcionesBandas") : []
                        delegate: ColumnLayout {
                            id: pr
                            required property string modelData
                            readonly property var p: page.au.detail.parametros[modelData]
                            Layout.fillWidth: true
                            spacing: 2
                            RowLayout {
                                Layout.fillWidth: true
                                Text { text: pr.modelData.replace(/_/g, " "); color: Theme.text; font.pixelSize: 12; Layout.fillWidth: true
                                       ToolTip.visible: prMa.containsMouse; ToolTip.text: pr.p.ayuda || ""
                                       MouseArea { id: prMa; anchors.fill: parent; hoverEnabled: true } }
                                Text { text: pr.p.texto; color: Theme.accent; font.pixelSize: 12 }
                            }
                            ComboBox {
                                Layout.fillWidth: true
                                visible: !!pr.p.opciones
                                model: pr.p.opciones || []
                                currentIndex: pr.p.opciones ? pr.p.opciones.indexOf(pr.p.texto) : -1
                                onActivated: page.msg = page.au.setParam(page.au.selected, pr.modelData, currentText)
                            }
                            TextField {
                                Layout.fillWidth: true
                                visible: !pr.p.opciones
                                placeholderText: "nuevo valor (" + (pr.p.rango ? pr.p.rango.join(" … ") : "") + ")"
                                onAccepted: { page.msg = page.au.setParam(page.au.selected, pr.modelData, text); text = "" }
                            }
                        }
                    }

                    // Bandas (EQ / multibanda)
                    Repeater {
                        model: parent.d.parametros && parent.d.parametros.bandas ? parent.d.parametros.bandas : []
                        delegate: Rectangle {
                            id: bd
                            required property var modelData
                            Layout.fillWidth: true
                            implicitHeight: bandCol.implicitHeight + 14
                            radius: 10
                            color: Qt.rgba(1, 1, 1, 0.04)
                            readonly property var opts: page.au.detail.parametros.opcionesBandas || ({})
                            ColumnLayout {
                                id: bandCol
                                anchors { left: parent.left; right: parent.right; top: parent.top; margins: 7 }
                                spacing: 3
                                Text { text: "Banda " + bd.modelData.banda; color: Theme.text; font { pixelSize: 12; weight: Font.DemiBold } }
                                Repeater {
                                    model: Object.keys(bd.modelData).filter(k => k !== "banda")
                                    delegate: RowLayout {
                                        id: br
                                        required property string modelData
                                        Layout.fillWidth: true
                                        Text { text: br.modelData.replace(/_/g, " "); color: Theme.textDim; font.pixelSize: 11; Layout.preferredWidth: 90 }
                                        ComboBox {
                                            Layout.fillWidth: true
                                            visible: !!bd.opts[br.modelData]
                                            model: bd.opts[br.modelData] || []
                                            currentIndex: bd.opts[br.modelData] ? bd.opts[br.modelData].indexOf(bd.modelData[br.modelData]) : -1
                                            onActivated: page.msg = page.au.setParam(page.au.selected, br.modelData, currentText, bd.modelData.banda)
                                        }
                                        TextField {
                                            Layout.fillWidth: true
                                            visible: !bd.opts[br.modelData]
                                            placeholderText: bd.modelData[br.modelData]
                                            onAccepted: { page.msg = page.au.setParam(page.au.selected, br.modelData, text, bd.modelData.banda); text = "" }
                                        }
                                    }
                                }
                            }
                        }
                    }

                    // Generador
                    ColumnLayout {
                        Layout.fillWidth: true
                        visible: parent.d.tipo === "generator"
                        readonly property var s: parent.d.senal || ({})
                        ComboBox {
                            id: genKind
                            Layout.fillWidth: true
                            model: ["silencio", "seno", "rosa", "blanco", "barrido"]
                            currentIndex: Math.max(0, model.indexOf(parent.s.tipo || "silencio"))
                            onActivated: page.au.setGenerator(page.au.selected, currentText, Number(genFreq.text || 1000), Number(genLvl.text || -20))
                        }
                        RowLayout {
                            TextField { id: genFreq; Layout.fillWidth: true; placeholderText: "Hz (" + (parent.parent.s.frecuencia || 1000) + ")"
                                        onAccepted: page.au.setGenerator(page.au.selected, genKind.currentText, Number(text), Number(genLvl.text || parent.parent.s.nivelDb || -20)) }
                            TextField { id: genLvl; Layout.fillWidth: true; placeholderText: "dBFS (" + (parent.parent.s.nivelDb || -20) + ")"
                                        onAccepted: page.au.setGenerator(page.au.selected, genKind.currentText, Number(genFreq.text || parent.parent.s.frecuencia || 1000), Number(text)) }
                        }
                        Text { Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.textFaint; font.pixelSize: 11
                               text: "El generador queda suelto: conectalo a un analizador para medir. Si lo conectás a la cadena, se escucha al aire." }
                    }

                    // Analisis (entrada, salida, analizador)
                    ColumnLayout {
                        Layout.fillWidth: true
                        visible: !!parent.d.analisis
                        readonly property var an: parent.d.analisis || ({})
                        spacing: 4
                        GridLayout {
                            columns: 2
                            columnSpacing: 12
                            Text { text: "LUFS M / S / I"; color: Theme.textDim; font.pixelSize: 12 }
                            Text { text: parent.parent.an.lufs ? page.fmtDb(parent.parent.an.lufs.momentaneo) + " / " + page.fmtDb(parent.parent.an.lufs.corto) + " / " + page.fmtDb(parent.parent.an.lufs.integrado) : "—"
                                   color: Theme.text; font.pixelSize: 12 }
                            Text { text: "Pico (máx.)"; color: Theme.textDim; font.pixelSize: 12 }
                            Text { text: page.fmtDb(parent.parent.an.picoDbfs) + " (" + page.fmtDb(parent.parent.an.picoMaximoDbfs) + ") dBFS"; color: Theme.text; font.pixelSize: 12 }
                            Text { text: "Piso de ruido"; color: Theme.textDim; font.pixelSize: 12 }
                            Text { text: page.fmtDb(parent.parent.an.pisoDeRuidoDbfs) + " dBFS"; color: Theme.text; font.pixelSize: 12 }
                            Text { text: "Nivel activo"; color: Theme.textDim; font.pixelSize: 12 }
                            Text { text: page.fmtDb(parent.parent.an.nivelActivoDbfs) + " dBFS"; color: Theme.text; font.pixelSize: 12 }
                        }
                        // Espectro en tercios de octava
                        Item {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 120
                            Row {
                                anchors.fill: parent
                                spacing: 1
                                Repeater {
                                    model: parent.parent.parent.an.bandas || []
                                    delegate: Rectangle {
                                        required property real modelData
                                        width: (parent.width - 29) / 30
                                        height: Math.max(1, parent.height * Math.max(0, (modelData + 90) / 90))
                                        anchors.bottom: parent.bottom
                                        radius: 2
                                        color: Qt.rgba(0.4, 0.7, 1, 0.85)
                                    }
                                }
                            }
                        }
                        RowLayout { Layout.fillWidth: true
                            Text { text: "25 Hz"; color: Theme.textFaint; font.pixelSize: 10; Layout.fillWidth: true }
                            Text { text: "1 kHz"; color: Theme.textFaint; font.pixelSize: 10; Layout.fillWidth: true; horizontalAlignment: Text.AlignHCenter }
                            Text { text: "20 kHz"; color: Theme.textFaint; font.pixelSize: 10 } }
                        Repeater {
                            model: parent.an.pistas || []
                            delegate: Text { required property string modelData; Layout.fillWidth: true; text: "• " + modelData; color: Theme.textDim; font.pixelSize: 11; wrapMode: Text.WordWrap }
                        }
                    }
                }
            }
        }
    }

    // Otro modulo LSP por nombre
    Popup {
        id: lspPopup
        anchors.centerIn: parent
        width: 460
        height: 420
        modal: true
        background: Rectangle { radius: 16; color: Theme.panel; border.color: Theme.glassBorder }
        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 12
            TextField { id: lspQuery; Layout.fillWidth: true; placeholderText: "Buscar en los ~200 plugins LSP (p. ej. «delay», «filter», «loud»)" }
            ListView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                model: page.au ? page.au.search(lspQuery.text) : []
                delegate: ItemDelegate {
                    required property var modelData
                    width: ListView.view.width
                    text: modelData.nombre + "  —  " + modelData.id
                    onClicked: { page.msg = page.au.addModule(modelData.id, ""); lspPopup.close() }
                }
            }
        }
    }
}
