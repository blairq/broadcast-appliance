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
    // Entrada candidata mientras se arrastra un cable: {node, port}
    property var wireTarget: null
    // Grilla del patchbay (la misma que usa el rack para ubicar los nodos).
    readonly property real grid: au ? au.gridStep : 20
    property bool showGrid: false      // el operador puede dejarla fija
    property bool draggingNode: false  // mientras se mueve un nodo se ve igual
    // Tarjetas en modo completo (nombres reales de los puertos, mono/estereo).
    property var expanded: ({})
    property int expandTick: 0
    function isExpanded(id) { expandTick; return !!expanded[id] }
    // Con la tarjeta expandida las filas son mas altas y los puntos L/R mas
    // grandes: asi se agarran bien y entran los nombres de los puertos.
    function rowH(id) { return isExpanded(id) ? 48 : 28 }
    function chOff(id) { return isExpanded(id) ? 14 : 8 }   // separacion del centro
    function chW(id) { return isExpanded(id) ? 28 : 18 }
    function chH(id) { return isExpanded(id) ? 18 : 10 }
    function toggleExpanded(id) { expanded[id] = !expanded[id]; expandTick++ }
    function snap(v) { return Math.round(v / grid) * grid }

    function nodeOf(id) {
        for (let i = 0; i < nodes.length; ++i) if (nodes[i].id === id) return nodes[i]
        return null
    }
    function nodeH(n) {
        const rows = Math.max(n.entradas.length, n.salidas.length, 1)
        return headH + rows * rowH(n.id) + 30
    }
    function posOf(n) {
        liveTick // dependencia
        const p = livePos[n.id]
        return p ? p : Qt.point(n.x, n.y)
    }
    // `ch`: -1 el puerto entero, 0 el punto L, 1 el punto R (los estereo se
    // dibujan como dos puntitos, 5 px arriba y abajo del centro).
    function portPos(nodeId, port, isIn, ch) {
        const n = nodeOf(nodeId)
        if (!n) return Qt.point(0, 0)
        const list = isIn ? n.entradas : n.salidas
        const i = Math.max(0, list.indexOf(port))
        const p = posOf(n)
        const info = ((isIn ? n.puertosEntrada : n.puertosSalida) || [])[i] || ({})
        const off = chOff(nodeId)
        const dy = (info.canales === 2 && ch !== undefined && ch >= 0) ? (ch === 0 ? -off : off) : 0
        const h = rowH(nodeId)
        return Qt.point(p.x + (isIn ? 0 : nodeW), p.y + headH + i * h + h / 2 + dy)
    }
    function kindColor(t) {
        if (t === "in" || t === "out") return "#64748b"
        if (t === "analyzer") return "#0ea5e9"
        if (t === "monitor") return "#a78bfa"
        if (t === "generator") return "#f59e0b"
        return Theme.accent
    }
    function fmtDb(v) { return (v === undefined || v === null || v <= -139) ? "—" : (Math.round(v * 10) / 10).toFixed(1) }
    // Entrada a la que iria a parar un cable soltado en (x, y): el puerto mas
    // cercano (con margen ancho) o, si se solto sobre una tarjeta, su primera
    // entrada. Asi no hay que apuntarle justo al puntito.
    function inputAt(x, y) {
        let best = null, bd = 70
        for (let i = 0; i < nodes.length; ++i) {
            const n = nodes[i]
            for (let k = 0; k < n.entradas.length; ++k) {
                const info = (n.puertosEntrada || [])[k] || ({})
                // En un puerto estereo se apunta al canal mas cercano; el punto
                // del medio (sin canal) sigue valiendo para el puerto entero.
                const opciones = info.canales === 2 ? [-1, 0, 1] : [-1]
                for (const ch of opciones) {
                    const p = portPos(n.id, n.entradas[k], true, ch)
                    const d = Math.hypot(p.x - x, p.y - y)
                    if (d < bd) { bd = d; best = { node: n.id, port: n.entradas[k], ch: ch } }
                }
            }
        }
        if (best) return best
        for (let i = 0; i < nodes.length; ++i) {
            const n = nodes[i]
            if (n.entradas.length === 0) continue
            const p = posOf(n)
            if (x >= p.x && x <= p.x + nodeW && y >= p.y && y <= p.y + nodeH(n))
                return { node: n.id, port: n.entradas[0], ch: -1 }
        }
        return null
    }
    function chName(ch) { return ch === 0 ? "L" : ch === 1 ? "R" : "" }
    // Cable curvo: sale hacia la derecha y entra por la izquierda. Los puntos
    // de control se separan segun la distancia, asi la curva no se aplasta.
    function cableCtl(a, b) { return Math.max(40, Math.min(160, Math.abs(b.x - a.x) / 2 + 20)) }
    // Punto medio REAL de la curva (Bezier cubica en t = 0.5): la ✕ tiene que
    // caer sobre el cable, no en el medio de una recta entre las dos puntas.
    function cableMid(a, b) {
        const d = cableCtl(a, b)
        return Qt.point((a.x + 3 * (a.x + d) + 3 * (b.x - d) + b.x) / 8, (a.y + 3 * a.y + 3 * b.y + b.y) / 8)
    }

    // Arbol de fuentes: un encabezado por grupo y debajo sus fuentes, en el
    // orden en que el motor las informa.
    property var collapsed: ({})
    property int groupTick: 0
    function groupOpen(g) { groupTick; return !collapsed[g] }
    function toggleGroup(g) { collapsed[g] = !collapsed[g]; groupTick++ }
    readonly property var sourceTree: {
        groupTick
        const src = au ? au.sources : []
        const orden = ["Invitados", "Micrófonos y líneas del equipo", "Audio del equipo (lo que suena)",
                       "Medios y red", "Otras fuentes"]
        const porGrupo = {}
        for (let i = 0; i < src.length; ++i) {
            const g = src[i].grupo || "Otras fuentes"
            if (!porGrupo[g]) porGrupo[g] = []
            porGrupo[g].push(src[i])
        }
        const filas = []
        for (const g of orden) {
            if (!porGrupo[g]) continue
            filas.push({ fila: "grupo", grupo: g, cuantas: porGrupo[g].length })
            if (groupOpen(g))
                for (const s of porGrupo[g]) filas.push({ fila: "fuente", grupo: g, src: s })
        }
        return filas
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

        // --- Fuentes con audio + salida de escucha -------------------------------------------
        ColumnLayout {
            Layout.preferredWidth: 250
            Layout.fillHeight: true
            spacing: 10

        Panel {
            Layout.fillWidth: true
            Layout.fillHeight: true
            title: "Fuentes con audio"
            ListView {
                id: srcList
                anchors.fill: parent
                clip: true
                spacing: 6
                model: page.sourceTree
                delegate: Item {
                    required property var modelData
                    width: srcList.width
                    height: modelData.fila === "grupo" ? 26 : 56

                    // Encabezado del grupo (se pliega con un clic).
                    Rectangle {
                        anchors.fill: parent
                        visible: modelData.fila === "grupo"
                        color: "transparent"
                        RowLayout {
                            anchors { fill: parent; leftMargin: 2; rightMargin: 4 }
                            spacing: 6
                            Text {
                                text: page.groupOpen(modelData.grupo) ? "▾" : "▸"
                                color: Theme.textFaint; font.pixelSize: 11
                            }
                            Text {
                                Layout.fillWidth: true
                                text: modelData.grupo || ""
                                color: Theme.textDim
                                font { pixelSize: 11; weight: Font.DemiBold; capitalization: Font.AllUppercase; letterSpacing: 0.4 }
                                elide: Text.ElideRight
                            }
                            Text { text: modelData.cuantas || 0; color: Theme.textFaint; font.pixelSize: 11 }
                        }
                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: page.toggleGroup(modelData.grupo)
                        }
                    }

                    // Fuente (sangrada, colgando del grupo).
                    Rectangle {
                        anchors { fill: parent; leftMargin: 12 }
                        visible: modelData.fila === "fuente"
                        radius: 12
                        readonly property var s: modelData.src || ({})
                        color: page.au && page.au.source === s.nombre ? Qt.rgba(1, 1, 1, 0.12) : Qt.rgba(1, 1, 1, 0.04)
                        border.color: page.au && page.au.source === s.nombre ? Theme.accent : "transparent"
                        ColumnLayout {
                            anchors { fill: parent; margins: 8 }
                            spacing: 3
                            RowLayout {
                                Text { text: parent.parent.parent.s.nombre || ""; color: Theme.text; font.pixelSize: 13; elide: Text.ElideRight; Layout.fillWidth: true }
                                Text { visible: !!parent.parent.parent.s.rack; text: "🎚 " + (parent.parent.parent.s.modulos || 0); color: Theme.accent; font.pixelSize: 12 }
                            }
                            LevelMeter {
                                Layout.fillWidth: true
                                Layout.preferredHeight: 6
                                db: parent.parent.s.salida && parent.parent.s.salida.salidaDb !== undefined ? parent.parent.s.salida.salidaDb : -100
                            }
                        }
                        MouseArea { anchors.fill: parent; onClicked: page.au.selectSource(parent.s.nombre) }
                    }
                }
                Text {
                    anchors { centerIn: parent; margins: 10 }
                    width: parent.width - 20
                    visible: srcList.count === 0
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.WordWrap
                    text: "No hay fuentes con audio.\nAgregá un micrófono o una entrada del sistema."
                    color: Theme.textFaint
                }
            }
        }

        // Micrófonos y entradas de audio del equipo (no aparecen solas: hay
        // que agregarlas como fuente).
        GlassButton {
            Layout.fillWidth: true
            text: "+ Entrada de audio del sistema"
            onClicked: { Studio.inputs.refresh(); micMenu.popup() }
        }
        Menu {
            id: micMenu
            Repeater {
                model: Studio.inputs.audioDevices
                delegate: MenuItem {
                    required property var modelData
                    text: modelData.name
                    onTriggered: {
                        page.msg = Studio.inputs.addAudioInput(modelData.value, "")
                        if (page.msg) return
                        // La lista de fuentes con audio se arma aparte: sin esto
                        // la entrada nueva no aparecia hasta tocar otra cosa.
                        page.au.refresh()
                        page.au.selectSource(Studio.inputs.audioInputName(modelData.value))
                    }
                }
            }
            MenuItem {
                enabled: false
                visible: Studio.inputs.audioDevices.length === 0
                text: "No se encontraron entradas de audio"
            }
        }

        }

        // --- Patchbay ---------------------------------------------------------------------
        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 10

            // El Flow va dentro de un Item que el layout dimensiona: si se le
            // deja a el mismo el Layout.fillWidth, su alto depende del ancho y
            // el ancho de su alto, y Qt aborta el layout ("polish loop").
            Item {
                Layout.fillWidth: true
                implicitHeight: barra.implicitHeight
            Flow {
                id: barra
                width: parent.width
                spacing: 6
                Text { text: page.au ? page.au.source : ""; color: Theme.text; font { pixelSize: 17; weight: Font.DemiBold } }
                Item { width: 12; height: 1 }
                // Primero de la fila: manda el audio de esta fuente a una salida
                // del equipo (auriculares, parlantes) sin tocar lo que va al aire.
                GlassButton {
                    compact: true
                    text: "+ Salida"
                    activeColor: "#a78bfa"
                    active: true
                    onClicked: outPopup.open()
                    ToolTip.visible: hovered; ToolTip.delay: 400
                    ToolTip.text: "Agrega una tarjeta de salida de audio a este grafo: conectale lo que quieras oír."
                }
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
                Item { width: 12; height: 1 }
                // Un rack armado a mano es media hora de trabajo: guardarlo con
                // nombre permite ponerselo a otra fuente en un clic.
                GlassButton {
                    compact: true
                    text: "Plantillas"
                    icon: "▤"
                    onClicked: plantillasPopup.open()
                    ToolTip.visible: hovered; ToolTip.delay: 400
                    ToolTip.text: "Guardar este rack con nombre, o ponerle a esta fuente uno guardado"
                }
                Item { width: 12; height: 1 }
                GlassButton {
                    compact: true; text: "Ordenar"
                    onClicked: page.au.tidy()
                    ToolTip.visible: hovered; ToolTip.delay: 400
                    ToolTip.text: "Acomoda las tarjetas en la grilla, siguiendo el orden de la cadena"
                }
                GlassButton {
                    compact: true; text: "Grilla"; active: page.showGrid
                    onClicked: page.showGrid = !page.showGrid
                    ToolTip.visible: hovered; ToolTip.delay: 400
                    ToolTip.text: "Dejar la grilla a la vista (si no, aparece sola al mover algo)"
                }
            }
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

                        // Grilla: fija con el boton, o mientras se acomoda algo.
                        Canvas {
                            anchors.fill: parent
                            z: -1
                            visible: opacity > 0
                            opacity: page.showGrid || page.draggingNode || page.wire !== null ? 1 : 0
                            Behavior on opacity { NumberAnimation { duration: 140 } }
                            onWidthChanged: requestPaint()
                            onHeightChanged: requestPaint()
                            onPaint: {
                                const c = getContext("2d")
                                c.reset()
                                const g = page.grid
                                c.lineWidth = 1
                                for (let x = 0; x < width; x += g) {
                                    // Cada 5 celdas la linea se ve un poco mas.
                                    c.strokeStyle = Qt.rgba(1, 1, 1, Math.round(x / g) % 5 === 0 ? 0.10 : 0.04)
                                    c.beginPath(); c.moveTo(Math.round(x) + 0.5, 0); c.lineTo(Math.round(x) + 0.5, height); c.stroke()
                                }
                                for (let y = 0; y < height; y += g) {
                                    c.strokeStyle = Qt.rgba(1, 1, 1, Math.round(y / g) % 5 === 0 ? 0.10 : 0.04)
                                    c.beginPath(); c.moveTo(0, Math.round(y) + 0.5); c.lineTo(width, Math.round(y) + 0.5); c.stroke()
                                }
                            }
                        }

                        // Cables
                        Repeater {
                            model: page.cables
                            delegate: Item {
                                id: cab
                                required property var modelData
                                readonly property point a: page.portPos(modelData.desde, modelData.salida, false, modelData.canalSalida)
                                readonly property point b: page.portPos(modelData.hacia, modelData.entrada, true, modelData.canalEntrada)
                                readonly property real ctl: page.cableCtl(a, b)
                                readonly property point mid: page.cableMid(a, b)
                                anchors.fill: parent
                                Shape {
                                    anchors.fill: parent
                                    preferredRendererType: Shape.CurveRenderer
                                    ShapePath {
                                        strokeColor: modelData.entrada === "sc" ? "#f59e0b" : Qt.rgba(0.55, 0.75, 1, 0.9)
                                        // Un cable de canal suelto se dibuja mas fino que uno de puerto entero.
                                        strokeWidth: cab.modelData.canalSalida >= 0 ? 2 : 3
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
                                    // Que canal lleva, cuando no es el puerto entero.
                                    Text {
                                        anchors { bottom: parent.top; horizontalCenter: parent.horizontalCenter }
                                        visible: cab.modelData.canalSalida >= 0
                                        text: page.chName(cab.modelData.canalSalida) + "→" + page.chName(cab.modelData.canalEntrada)
                                        color: Theme.textFaint; font { pixelSize: 9; weight: Font.DemiBold }
                                    }
                                    MouseArea {
                                        id: xMa
                                        anchors.fill: parent
                                        hoverEnabled: true
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: page.au.disconnectPorts(cab.modelData.desde, cab.modelData.salida, cab.modelData.hacia, cab.modelData.entrada, cab.modelData.canalSalida, cab.modelData.canalEntrada)
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
                                capStyle: ShapePath.RoundCap
                                startX: page.wire ? page.wire.x0 : 0
                                startY: page.wire ? page.wire.y0 : 0
                                PathCubic {
                                    readonly property real d: page.wire
                                        ? page.cableCtl(Qt.point(page.wire.x0, page.wire.y0),
                                                        Qt.point(page.wire.x1, page.wire.y1)) : 60
                                    x: page.wire ? page.wire.x1 : 0
                                    y: page.wire ? page.wire.y1 : 0
                                    control1X: page.wire ? page.wire.x0 + d : 0
                                    control1Y: page.wire ? page.wire.y0 : 0
                                    control2X: page.wire ? page.wire.x1 - d : 0
                                    control2Y: page.wire ? page.wire.y1 : 0
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
                                        width: parent.width - 52
                                        anchors.verticalCenter: parent.verticalCenter
                                        text: nd.modelData.etiqueta
                                        color: Theme.text; font { pixelSize: 13; weight: Font.DemiBold } elide: Text.ElideRight
                                    }
                                    // Detalle de la tarjeta: simple o con todos los puertos.
                                    // z por encima del area de arrastre (que se
                                    // declara despues y si no se come el clic).
                                    Rectangle {
                                        z: 2
                                        anchors { right: parent.right; rightMargin: 6; verticalCenter: parent.verticalCenter }
                                        width: 22; height: 22; radius: 11
                                        color: detMa.containsMouse ? Qt.rgba(1, 1, 1, 0.16) : "transparent"
                                        Text {
                                            anchors.centerIn: parent
                                            text: page.isExpanded(nd.modelData.id) ? "⌃" : "⌄"
                                            color: Theme.textFaint; font.pixelSize: 12
                                        }
                                        MouseArea {
                                            id: detMa
                                            anchors.fill: parent
                                            hoverEnabled: true
                                            cursorShape: Qt.PointingHandCursor
                                            onClicked: page.toggleExpanded(nd.modelData.id)
                                            ToolTip.visible: containsMouse
                                            ToolTip.delay: 500
                                            ToolTip.text: page.isExpanded(nd.modelData.id)
                                                          ? "Mostrar la tarjeta simple"
                                                          : "Mostrar los nombres de los puertos y si son mono o estéreo"
                                        }
                                    }
                                    MouseArea {
                                        id: headMa
                                        anchors.fill: parent
                                        preventStealing: true
                                        cursorShape: drag.active ? Qt.ClosedHandCursor : Qt.OpenHandCursor
                                        drag.target: nd
                                        drag.minimumX: 0; drag.minimumY: 0
                                        // OJO: no tocar nd.x/nd.y mientras se arrastra. El arrastre
                                        // calcula la posicion a partir de la actual, asi que el ajuste
                                        // a la grilla se le sumaba y la tarjeta se escapaba del cursor.
                                        // Se muestra la celda destino y se encaja al soltar.
                                        drag.onActiveChanged: {
                                            page.draggingNode = drag.active
                                            if (drag.active) return
                                            nd.x = page.snap(nd.x)
                                            nd.y = page.snap(nd.y)
                                            page.au.moveNode(nd.modelData.id, nd.x, nd.y)
                                        }
                                        onClicked: page.au.select(nd.modelData.id)
                                    }
                                }

                                // Celda donde va a quedar al soltar.
                                Rectangle {
                                    visible: headMa.drag.active
                                    x: page.snap(nd.x) - nd.x
                                    y: page.snap(nd.y) - nd.y
                                    width: nd.width
                                    height: nd.height
                                    z: -1
                                    radius: 12
                                    color: Qt.rgba(1, 1, 1, 0.05)
                                    border { color: Theme.accent; width: 2 }
                                }

                                // Puertos de entrada (izquierda)
                                Repeater {
                                    model: nd.modelData.entradas
                                    delegate: Item {
                                        required property string modelData
                                        required property int index
                                        readonly property bool target: page.wireTarget
                                                                       && page.wireTarget.node === nd.modelData.id
                                                                       && page.wireTarget.port === modelData
                                        readonly property int targetCh: target ? page.wireTarget.ch : -2
                                        readonly property var info: (nd.modelData.puertosEntrada || [])[index] || ({})
                                        x: -7
                                        y: page.headH + index * page.rowH(nd.modelData.id)
                                           + page.rowH(nd.modelData.id) / 2 - 7
                                        width: 14; height: 14
                                        // Halo cuando el cable que se esta tirando va a caer aca.
                                        Rectangle {
                                            anchors.centerIn: parent
                                            width: 30; height: 30; radius: 15
                                            visible: parent.target
                                            color: Qt.rgba(0.53, 0.94, 0.67, 0.28)
                                            border.color: Theme.ok; border.width: 2
                                        }
                                        // Un punto si el puerto es mono; dos (L y R)
                                        // si es estereo, para diferenciarlos de un vistazo.
                                        // El cable sigue siendo del PUERTO entero.
                                        Rectangle {
                                            visible: parent.info.canales !== 2
                                            anchors.fill: parent
                                            radius: 7
                                            color: modelData === "sc" ? "#f59e0b" : "#93c5fd"
                                            border.color: "white"; border.width: 1
                                        }
                                        Column {
                                            z: 3
                                            visible: parent.info.canales === 2
                                            anchors.centerIn: parent
                                            spacing: 2 * page.chOff(nd.modelData.id) - page.chH(nd.modelData.id)
                                            Repeater {
                                                model: [0, 1]
                                                delegate: Rectangle {
                                                    id: inDot
                                                    required property int modelData
                                                    width: page.chW(nd.modelData.id)
                                                    height: page.chH(nd.modelData.id)
                                                    radius: height / 2
                                                    // Verde si el cable que se arrastra va a caer aca;
                                                    // mas claro con el mouse encima.
                                                    color: parent.parent.targetCh === inDot.modelData
                                                           ? Theme.ok
                                                           : (inChMa.containsMouse ? Qt.lighter("#93c5fd", 1.35) : "#93c5fd")
                                                    border.color: inChMa.containsMouse ? Theme.accent : "white"
                                                    border.width: inChMa.containsMouse ? 2 : 1
                                                    scale: inChMa.containsMouse ? 1.12 : 1
                                                    Behavior on scale { NumberAnimation { duration: 90 } }
                                                    // Solo para el hover: no acepta botones, asi no
                                                    // interfiere con soltar un cable encima.
                                                    MouseArea {
                                                        id: inChMa
                                                        anchors.fill: parent
                                                        anchors.margins: -3
                                                        hoverEnabled: true
                                                        acceptedButtons: Qt.NoButton
                                                    }
                                                    Text {
                                                        anchors.centerIn: parent
                                                        text: page.chName(inDot.modelData)
                                                        color: "#0b1220"
                                                        font { pixelSize: page.isExpanded(nd.modelData.id) ? 10 : 7; weight: Font.Black }
                                                    }
                                                }
                                            }
                                        }
                                        Column {
                                            x: 18
                                            anchors.verticalCenter: parent.verticalCenter
                                            Text { text: modelData === "sc" ? "sidechain" : "entrada"; color: Theme.textFaint; font.pixelSize: 10 }
                                            Text {
                                                visible: page.isExpanded(nd.modelData.id)
                                                width: page.nodeW / 2 - 24
                                                elide: Text.ElideRight
                                                text: (parent.parent.info.nombre ? parent.parent.info.nombre + " · " : "")
                                                      + (parent.parent.info.canales === 1 ? "mono" : "estéreo")
                                                color: Theme.textFaint; font.pixelSize: 9
                                            }
                                        }
                                    }
                                }
                                // Puertos de salida (derecha): se arrastran para conectar.
                                Repeater {
                                    model: nd.modelData.salidas
                                    delegate: Item {
                                        id: op
                                        required property string modelData
                                        required property int index
                                        readonly property var info: (nd.modelData.puertosSalida || [])[index] || ({})
                                        x: page.nodeW - 7
                                        y: page.headH + index * page.rowH(nd.modelData.id)
                                           + page.rowH(nd.modelData.id) / 2 - 7
                                        width: 14; height: 14
                                        Column {
                                            anchors { right: parent.left; rightMargin: 4; verticalCenter: parent.verticalCenter }
                                            Text { anchors.right: parent.right; text: "salida"; color: Theme.textFaint; font.pixelSize: 10 }
                                            Text {
                                                anchors.right: parent.right
                                                visible: page.isExpanded(nd.modelData.id)
                                                width: page.nodeW / 2 - 24
                                                horizontalAlignment: Text.AlignRight
                                                elide: Text.ElideRight
                                                text: (op.info.nombre ? op.info.nombre + " · " : "")
                                                      + (op.info.canales === 1 ? "mono" : "estéreo")
                                                color: Theme.textFaint; font.pixelSize: 9
                                            }
                                        }
                                        Rectangle {
                                            visible: op.info.canales !== 2
                                            anchors.fill: parent
                                            radius: 7
                                            color: "#86efac"
                                            border.color: "white"; border.width: 1
                                        }
                                        Column {
                                            // z por encima del area del puerto entero, que se declara
                                            // despues: si no, se comia el hover y el clic de los puntos
                                            // (no se podia agarrar el canal de abajo).
                                            z: 3
                                            visible: op.info.canales === 2
                                            anchors.centerIn: parent
                                            spacing: 2 * page.chOff(nd.modelData.id) - page.chH(nd.modelData.id)
                                            Repeater {
                                                model: [0, 1]
                                                delegate: Rectangle {
                                                    id: chDot
                                                    required property int modelData
                                                    width: page.chW(nd.modelData.id)
                                                    height: page.chH(nd.modelData.id)
                                                    radius: height / 2
                                                    // Encendido con el mouse encima y mientras se
                                                    // arrastra SU cable (ahi el hover no se actualiza:
                                                    // el area tiene agarrado el mouse).
                                                    readonly property bool lit: outChMa.containsMouse
                                                        || (page.wire && page.wire.from === nd.modelData.id
                                                            && page.wire.out === op.modelData
                                                            && page.wire.ch === chDot.modelData)
                                                    color: lit ? Qt.lighter("#86efac", 1.25) : "#86efac"
                                                    border.color: lit ? Theme.accent : "white"
                                                    border.width: lit ? 2 : 1
                                                    scale: lit ? 1.12 : 1
                                                    Behavior on scale { NumberAnimation { duration: 90 } }
                                                    Text {
                                                        anchors.centerIn: parent
                                                        text: page.chName(chDot.modelData)
                                                        color: "#0b1220"
                                                        font { pixelSize: page.isExpanded(nd.modelData.id) ? 10 : 7; weight: Font.Black }
                                                    }
                                                    // Arrastrar desde L o R lleva SOLO ese canal.
                                                    MouseArea {
                                                        id: outChMa
                                                        anchors.fill: parent
                                                        // Chico a proposito: el hueco del medio sigue
                                                        // siendo del puerto entero (los dos canales).
                                                        anchors.margins: -3
                                                        cursorShape: Qt.CrossCursor
                                                        preventStealing: true
                                                        hoverEnabled: true
                                                        ToolTip.visible: containsMouse && !page.wire
                                                        ToolTip.delay: 600
                                                        ToolTip.text: "Canal " + page.chName(chDot.modelData) + ": arrastralo a la entrada que quieras"
                                                        onPressed: (ev) => {
                                                            const p = page.portPos(nd.modelData.id, op.modelData, false, chDot.modelData)
                                                            page.wire = { from: nd.modelData.id, out: op.modelData, ch: chDot.modelData,
                                                                          x0: p.x, y0: p.y, x1: p.x, y1: p.y }
                                                            page.wireTarget = null
                                                        }
                                                        onPositionChanged: (ev) => {
                                                            if (!page.wire) return
                                                            const q = mapToItem(canvas, ev.x, ev.y)
                                                            const t = page.inputAt(q.x, q.y)
                                                            page.wireTarget = t
                                                            const end = t ? page.portPos(t.node, t.port, true, t.ch) : q
                                                            page.wire = Object.assign({}, page.wire, { x1: end.x, y1: end.y })
                                                        }
                                                        onReleased: (ev) => {
                                                            if (!page.wire) return
                                                            const t = page.wireTarget
                                                            if (t) page.msg = page.au.connectPorts(page.wire.from, page.wire.out, t.node, t.port,
                                                                                                   page.wire.ch, t.ch >= 0 ? t.ch : page.wire.ch)
                                                            page.wire = null
                                                            page.wireTarget = null
                                                        }
                                                        onCanceled: { page.wire = null; page.wireTarget = null }
                                                    }
                                                }
                                            }
                                        }
                                        MouseArea {
                                            anchors.fill: parent
                                            // Agarre generoso: el punto es chico, la zona no.
                                            anchors.margins: -12
                                            cursorShape: Qt.CrossCursor
                                            preventStealing: true
                                            ToolTip.visible: containsMouse && !page.wire
                                            ToolTip.delay: 600
                                            ToolTip.text: "Arrastrá hasta la tarjeta (o la entrada) que quieras alimentar"
                                            hoverEnabled: true
                                            onPressed: (ev) => {
                                                const p = page.portPos(nd.modelData.id, op.modelData, false)
                                                page.wire = { from: nd.modelData.id, out: op.modelData, x0: p.x, y0: p.y, x1: p.x, y1: p.y }
                                                page.wireTarget = null
                                            }
                                            onPositionChanged: (ev) => {
                                                if (!page.wire) return
                                                const q = mapToItem(canvas, ev.x, ev.y)
                                                // El cable se pega a la entrada candidata: se ve a donde va.
                                                const t = page.inputAt(q.x, q.y)
                                                page.wireTarget = t
                                                const end = t ? page.portPos(t.node, t.port, true) : q
                                                page.wire = Object.assign({}, page.wire, { x1: end.x, y1: end.y })
                                            }
                                            onReleased: (ev) => {
                                                if (!page.wire) return
                                                const t = page.wireTarget
                                                // Soltar en un punto L o R de un puerto estereo manda
                                                // solo ese canal (el izquierdo de la salida).
                                                if (t) page.msg = page.au.connectPorts(page.wire.from, page.wire.out, t.node, t.port,
                                                                                       t.ch >= 0 ? 0 : -1, t.ch)
                                                page.wire = null
                                                page.wireTarget = null
                                            }
                                            onCanceled: { page.wire = null; page.wireTarget = null }
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

    // Salidas de audio del equipo, agrupadas por dispositivo.
    Popup {
        id: outPopup
        anchors.centerIn: parent
        width: 520
        height: 460
        modal: true
        background: Rectangle { radius: 16; color: Theme.panel; border.color: Theme.glassBorder }
        ColumnLayout {
            anchors { fill: parent; margins: 16 }
            spacing: 10
            Text {
                text: "Mandar el audio de esta fuente a una salida"
                color: Theme.text; font { pixelSize: 16; weight: Font.DemiBold }
            }
            Text {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                color: Theme.textDim
                font.pixelSize: 12
                text: "Se agrega una tarjeta con el nombre de la salida al grafo de «" +
                      (page.au ? page.au.source : "") + "». Conectale lo que quieras oír. " +
                      "No cambia lo que sale al aire. El motor reproduce por una salida a la vez: " +
                      "si elegís otra, se usa esa."
            }
            ListView {
                id: outList
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                spacing: 4
                model: {
                    // Encabezado por dispositivo y debajo sus salidas.
                    const grupos = page.au ? page.au.outputDevices : []
                    const filas = []
                    for (let i = 0; i < grupos.length; ++i) {
                        filas.push({ fila: "disp", nombre: grupos[i].dispositivo })
                        const sal = grupos[i].salidas || []
                        for (let k = 0; k < sal.length; ++k)
                            filas.push({ fila: "salida", nombre: sal[k].nombre, id: sal[k].id })
                    }
                    return filas
                }
                delegate: Item {
                    required property var modelData
                    width: outList.width
                    height: modelData.fila === "disp" ? 26 : 34
                    Text {
                        visible: modelData.fila === "disp"
                        anchors { left: parent.left; verticalCenter: parent.verticalCenter }
                        text: modelData.nombre || ""
                        color: Theme.textDim
                        font { pixelSize: 11; weight: Font.DemiBold; capitalization: Font.AllUppercase; letterSpacing: 0.4 }
                        elide: Text.ElideRight
                        width: parent.width
                    }
                    Rectangle {
                        visible: modelData.fila === "salida"
                        anchors { fill: parent; leftMargin: 12 }
                        radius: 8
                        color: outMa.containsMouse ? Qt.rgba(1, 1, 1, 0.10) : "transparent"
                        Text {
                            anchors { left: parent.left; leftMargin: 8; right: parent.right; verticalCenter: parent.verticalCenter }
                            text: modelData.nombre || ""
                            color: Theme.text; font.pixelSize: 13; elide: Text.ElideRight
                        }
                        MouseArea {
                            id: outMa
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                page.msg = page.au.addOutput(modelData.id)
                                outPopup.close()
                            }
                        }
                    }
                }
                Text {
                    anchors.centerIn: parent
                    visible: outList.count === 0
                    text: "No se encontraron salidas de audio."
                    color: Theme.textFaint
                }
            }
            GlassButton { Layout.alignment: Qt.AlignRight; compact: true; text: "Cerrar"; onClicked: outPopup.close() }
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

    // --- Plantillas de rack ---------------------------------------------------
    Popup {
        id: plantillasPopup
        anchors.centerIn: Overlay.overlay
        width: 520
        modal: true
        padding: 16
        background: Rectangle { color: Theme.panel; radius: Theme.radiusSmall; border.color: Theme.glassBorder }

        ColumnLayout {
            anchors.fill: parent
            spacing: 10

            Text {
                text: "Plantillas de rack"
                color: Theme.text
                font { family: Theme.font; pixelSize: 16; weight: Font.DemiBold }
            }
            Text {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                color: Theme.textDim
                font.pixelSize: 12
                text: "Guardá el rack de esta fuente con un nombre y poneselo a otra en un clic. " +
                      "Si la marcás como predeterminada de un tipo de fuente, las nuevas de ese tipo " +
                      "la toman solas al crearse — es lo que hace que un invitado ya suene bien sin " +
                      "que nadie le arme nada."
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                TextField {
                    id: nuevoNombre
                    Layout.fillWidth: true
                    placeholderText: "Nombre para guardar este rack"
                    color: Theme.text
                    font.pixelSize: 13
                }
                GlassButton {
                    text: "Guardar"; compact: true
                    onClicked: {
                        page.msg = page.au.guardarPlantilla(nuevoNombre.text)
                        if (page.msg === "") nuevoNombre.text = ""
                    }
                }
            }

            ListView {
                Layout.fillWidth: true
                Layout.preferredHeight: Math.min(260, 46 * Math.max(1, (page.au ? page.au.plantillas().length : 0)))
                clip: true
                spacing: 4
                model: page.au ? page.au.plantillas() : []
                delegate: RowLayout {
                    required property var modelData
                    width: ListView.view.width
                    spacing: 6
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 0
                        Text {
                            text: modelData.nombre
                            color: Theme.text
                            font { pixelSize: 13; weight: Font.Medium }
                        }
                        Text {
                            text: modelData.modulos + " módulos" +
                                  (modelData.paraTipo !== "" ? "  ·  predeterminada de " + modelData.paraTipo : "")
                            color: modelData.paraTipo !== "" ? Theme.accent : Theme.textFaint
                            font.pixelSize: 11
                        }
                    }
                    GlassButton {
                        text: "Aplicar"; compact: true
                        onClicked: { page.msg = page.au.aplicarPlantilla(modelData.nombre); plantillasPopup.close() }
                        ToolTip.visible: hovered
                        ToolTip.text: "Reemplaza el rack de « " + (page.au ? page.au.source : "") + " » por este"
                    }
                    GlassButton {
                        text: modelData.paraTipo !== "" ? "Quitar defecto" : "Por defecto"
                        compact: true
                        onClicked: page.msg = page.au.plantillaPorDefecto(
                            modelData.nombre, modelData.paraTipo !== "" ? "" : page.au.tipoDeFuente())
                        ToolTip.visible: hovered
                        ToolTip.text: modelData.paraTipo !== ""
                            ? "Las fuentes nuevas dejan de tomarla"
                            : "Las fuentes nuevas del mismo tipo que « " + (page.au ? page.au.source : "") + " » la toman solas"
                    }
                    GlassButton {
                        icon: "✕"; compact: true
                        onClicked: page.msg = page.au.borrarPlantilla(modelData.nombre)
                    }
                }
            }

            Text {
                visible: page.au && page.au.plantillas().length === 0
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignHCenter
                text: "Todavía no guardaste ninguna."
                color: Theme.textFaint
                font.pixelSize: 12
            }

            GlassButton { text: "Cerrar"; compact: true; onClicked: plantillasPopup.close() }
        }
    }
}
