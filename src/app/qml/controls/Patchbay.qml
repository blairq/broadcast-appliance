// Patchbay genérico: tarjetas con puertos de entrada (izquierda) y de salida
// (derecha), unidas con cables curvos. UNO para todos: el grafo de video, el estudio
// de audio y el rack de filtros de una fuente (AudioPage), que suma lo suyo con las
// propiedades de abajo (puertos estéreo, tarjetas que se agrandan, su cuerpo).
//
// Contrato del modelo:
//   nodos:  [{id, tipo, etiqueta, x, y, resumen?, externo?, bypass?,
//             entradas: [{id, tipo?, pedido?} | "id"], salidas: [{id, tipo?} | "id"],
//             puertosEntrada?/puertosSalida?: [{nombre, canales}] (de los puertos texto)}]
//   cables: [{desde, salida, hacia, entrada, canalSalida?, canalEntrada?}]
// Los puertos tienen tipo: "video" (de fábrica) o "control" (ámbar; sus
// cables van punteados). Con `soloMismoTipo`, un cable se suelta solo en un puerto de
// su tipo. Un puerto `pedido` es uno libre de los que se crean al conectar (los de un
// compositor): se dibuja hueco, con «+». Un nodo `externo` es de otra parte
// del grafo (se ve atenuado y no se mueve desde acá); uno `bypass` está anulado.
// Un puerto con `canales: 2` se dibuja con dos puntos (L y R): arrastrar desde uno
// lleva solo ese canal, y soltar sobre uno llega solo a ese (los `ch` de conectar:
// -1 el puerto entero, 0 L, 1 R). `franjas` dibuja bandas de fondo con nombre (una por
// entrada en la vista Estudio).
// La tarjeta se mueve por la cabecera (encaja en la grilla al soltar); un
// cable se tira desde un punto de salida y se suelta sobre una entrada o una
// tarjeta; la ✕ en el medio lo quita.
import QtQuick
import QtQuick.Controls
import BA

Rectangle {
    id: pb
    property var nodos: []
    property var cables: []
    property string seleccionado: ""
    /// Franjas de fondo con nombre (la vista Estudio: una por entrada):
    /// [{titulo, y, alto, color?}]
    property var franjas: []
    /// El color de la franja de cada tipo de tarjeta.
    property var colorDe: function(tipo) { return Theme.accent }
    /// Las alarmas de cada tarjeta por id: {nivel: "mejorable"|"mal_armado"|"mal", motivos: [texto]}
    /// (el plan de resolución: ResolucionModel::niveles). Pintan el borde y ponen un «!» con los motivos.
    property var alarmas: ({})
    /// Un cable se suelta solo en una entrada de su tipo (video con video, control con
    /// control); sin esto, en cualquiera (el audio: también en el sidechain).
    property bool soloMismoTipo: true
    /// Cuán cerca de una entrada hay que soltar el cable para que vaya a ella.
    property real margenSoltar: 40
    /// Las tarjetas se agrandan (el botón ⌄ de la cabecera): filas más altas, puntos L/R
    /// más grandes y el nombre de cada puerto con si es mono o estéreo.
    property bool expandibles: false
    /// El tipo de un puerto (la información del puerto y su id): "video", "control"…
    property var tipoDe: function(info, id) { return info && info.tipo ? info.tipo : "video" }
    /// El nombre de un puerto al lado del punto ("" = ninguno).
    property var etiquetaPuerto: function(info, id, entrada, nodo) {
        if (!entrada || (nodo.entradas || []).length <= 1 || id === "in") return ""
        return info.pedido ? qsTr("nueva") : id === "ctl" ? qsTr("control") : id
    }
    /// Lo que va abajo de los puertos (por defecto, el `resumen` del nodo). El componente
    /// tiene una propiedad `nodo` (el del modelo) que el patchbay le pone.
    property Component cuerpo: resumenC
    signal elegir(string id)
    signal mover(string id, int x, int y)
    /// `chSalida`/`chEntrada`: -1 el puerto entero, 0 L, 1 R (los estéreo).
    signal conectar(string desde, string salida, string hacia, string entrada, int chSalida, int chEntrada)
    signal desconectar(string desde, string salida, string hacia, string entrada, int chSalida, int chEntrada)
    /// Pide acomodar las tarjetas (la página decide cómo: VideoGrafos::ordenar).
    /// Zoom, grilla y ordenar se manejan desde BarraGrafo (`lienzo`).
    signal ordenar()
    /// Zoom a mano (0 = que entre a lo ancho, como siempre).
    property real zoom: 0
    /// La grilla de fondo (las tarjetas encajan en ella al soltarlas).
    property bool grilla: true

    readonly property int nodeW: 180
    readonly property int headH: 34
    property int bodyH: 40
    property real grid: 20
    /// Las tarjetas agrandadas (id -> true).
    property var expandidas: ({})
    function expandida(id) { return pb.expandibles && !!pb.expandidas[id] }
    function alternarExpandida(id) { const e = Object.assign({}, pb.expandidas); e[id] = !e[id]; pb.expandidas = e }
    function rowH(id) { return !pb.expandibles ? 24 : pb.expandida(id) ? 48 : 28 }
    /// Los puntos L/R de un puerto estéreo: cuánto se separan del centro y su tamaño.
    function chOff(id) { return pb.expandida(id) ? 14 : 8 }
    function chW(id) { return pb.expandida(id) ? 28 : 18 }
    function chH(id) { return pb.expandida(id) ? 18 : 10 }
    function chName(ch) { return ch === 0 ? "L" : ch === 1 ? "R" : "" }
    /// Dónde está cada tarjeta y cada puerto, la curva de los cables y la entrada a la que
    /// va uno que se suelta: la misma geometría que el rack de audio.
    readonly property GeometriaPatchbay geo: GeometriaPatchbay {
        nodos: pb.nodos
        nodeW: pb.nodeW
        headH: pb.headH
        grid: pb.grid
        altoFila: function(id) { return pb.rowH(id) }
        altoCuerpo: function(n) { return pb.bodyH }
        separacion: function(id) { return pb.chOff(id) }
    }
    property var wire: null        // {from, out, x0, y0, x1, y1}
    property var wireTarget: null  // {node, port}
    property bool draggingNode: false

    radius: 16
    color: Qt.rgba(0, 0, 0, 0.25)
    border.color: Qt.rgba(1, 1, 1, 0.06)
    clip: true

    readonly property color colorControl: "#fbbf24"
    function tipoPuerto(id, port, isIn) {
        const n = geo.nodeOf(id)
        const lista = (n && (isIn ? n.entradas : n.salidas)) || []
        for (let i = 0; i < lista.length; ++i)
            if (geo.idDe(lista[i]) === port) return pb.tipoDe(geo.infoDe(n, isIn, i), port)
        return "video"
    }
    // El cable que se tira: desde el puerto entero (ch -1) o desde un canal.
    function empezarCable(nodo, salida, tipo, ch) {
        const p = geo.portPos(nodo, salida, false, ch)
        pb.wire = { from: nodo, out: salida, tipo: tipo, ch: ch, x0: p.x, y0: p.y, x1: p.x, y1: p.y }
        pb.wireTarget = null
    }
    function moverCable(desde, ev) {
        if (!pb.wire) return
        const q = desde.mapToItem(lienzo, ev.x, ev.y)
        // El cable se pega a la entrada candidata: se ve a dónde va.
        const t = geo.inputAt(q.x, q.y, pb.soloMismoTipo ? (info => pb.tipoDe(info, "") === pb.wire.tipo) : null, pb.margenSoltar)
        pb.wireTarget = t
        const end = t ? geo.portPos(t.node, t.port, true, t.ch) : q
        pb.wire = Object.assign({}, pb.wire, { x1: end.x, y1: end.y })
    }
    function soltarCable() {
        const w = pb.wire, t = pb.wireTarget
        pb.wire = null
        pb.wireTarget = null
        if (!w || !t || t.node === w.from) return
        // Desde un canal: ese canal, al canal donde cae (o al mismo). Desde el puerto
        // entero soltado sobre un canal: el izquierdo de la salida a ese canal.
        if (w.ch >= 0) pb.conectar(w.from, w.out, t.node, t.port, w.ch, t.ch >= 0 ? t.ch : w.ch)
        else pb.conectar(w.from, w.out, t.node, t.port, t.ch >= 0 ? 0 : -1, t.ch)
    }
    function cancelarCable() { pb.wire = null; pb.wireTarget = null }

    // Si el grafo no entra a lo ancho, se achica hasta 75 % (más chico no se lee:
    // a 40 % las tarjetas eran rayitas) y el resto se recorre desplazando; con
    // lugar, tamaño normal. «Ajustar» (BarraGrafo) sigue pudiendo verlo todo junto.
    readonly property real anchoGrafo: {
        let m = 0
        for (let i = 0; i < nodos.length; ++i) m = Math.max(m, nodos[i].x + nodeW)
        return m + 40
    }
    /// La que hace entrar todo a lo ancho, por chica que sea (la usa «Ajustar»).
    readonly property real escalaParaTodo: Math.max(0.25, Math.min(1, (flick.width - 8) / Math.max(1, anchoGrafo)))
    readonly property real escalaAjustada: Math.max(0.75, Math.min(1, (flick.width - 8) / Math.max(1, anchoGrafo)))
    readonly property real escala: pb.zoom > 0 ? pb.zoom : pb.escalaAjustada
    function acercar(f) { pb.zoom = Math.max(0.25, Math.min(2.5, pb.escala * f)) }

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
            width: 2600
            height: 1600
            scale: pb.escala
            transformOrigin: Item.TopLeft
            MouseArea { anchors.fill: parent; onClicked: pb.elegir("") }

            // La grilla (debajo de todo): líneas cada 20 px y más marcadas cada 100.
            Canvas {
                anchors.fill: parent
                visible: pb.grilla
                z: -3
                onPaint: {
                    const c = getContext("2d")
                    c.clearRect(0, 0, width, height)
                    // Con fillRect y no con stroke(): en una pantalla Retina el
                    // pintor de Qt trazaba cada línea vertical renglón por
                    // renglón repartiéndolos a un grupo de hilos, y la grilla
                    // trababa la interfaz varios segundos al arrancar.
                    for (let x = 0; x <= width; x += pb.grid) {
                        c.fillStyle = x % 100 === 0 ? "rgba(255,255,255,0.07)" : "rgba(255,255,255,0.03)"
                        c.fillRect(x, 0, 1, height)
                    }
                    for (let y = 0; y <= height; y += pb.grid) {
                        c.fillStyle = y % 100 === 0 ? "rgba(255,255,255,0.07)" : "rgba(255,255,255,0.03)"
                        c.fillRect(0, y, width, 1)
                    }
                }
            }

            // Franjas (debajo de todo).
            Repeater {
                model: pb.franjas
                delegate: Rectangle {
                    required property var modelData
                    z: -2
                    x: 0
                    y: modelData.y
                    width: Math.max(pb.anchoGrafo, flick.width / pb.escala)
                    height: modelData.alto
                    color: modelData.color || Qt.rgba(1, 1, 1, 0.025)
                    border.color: Qt.rgba(1, 1, 1, 0.06)
                    Text {
                        anchors { left: parent.left; top: parent.top; leftMargin: 10; topMargin: 6 }
                        text: modelData.titulo
                        color: Theme.textFaint
                        font { pixelSize: 11; weight: Font.DemiBold; capitalization: Font.AllUppercase; letterSpacing: 0.4 }
                    }
                }
            }

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
                    // fillRect y no stroke(): ver la grilla de abajo de todo.
                    for (let x = 0; x < width; x += pb.grid) {
                        c.fillStyle = Qt.rgba(1, 1, 1, Math.round(x / pb.grid) % 5 === 0 ? 0.10 : 0.04)
                        c.fillRect(Math.round(x), 0, 1, height)
                    }
                    for (let y = 0; y < height; y += pb.grid) {
                        c.fillStyle = Qt.rgba(1, 1, 1, Math.round(y / pb.grid) % 5 === 0 ? 0.10 : 0.04)
                        c.fillRect(0, Math.round(y), width, 1)
                    }
                }
            }

            // Cables
            Repeater {
                model: pb.cables
                delegate: CablePatchbay {
                    id: cab
                    required property var modelData
                    readonly property bool control: pb.tipoPuerto(modelData.hacia, modelData.entrada, true) === "control"
                    readonly property bool cruza: {
                        const d = pb.geo.nodeOf(modelData.desde), h = pb.geo.nodeOf(modelData.hacia)
                        return !!(d && d.externo) || !!(h && h.externo)
                    }
                    readonly property int chSalida: modelData.canalSalida !== undefined ? modelData.canalSalida : -1
                    readonly property int chEntrada: modelData.canalEntrada !== undefined ? modelData.canalEntrada : -1
                    anchors.fill: parent
                    geo: pb.geo
                    a: pb.geo.portPos(modelData.desde, modelData.salida, false, cab.chSalida)
                    b: pb.geo.portPos(modelData.hacia, modelData.entrada, true, cab.chEntrada)
                    // Un cable que sale de (o va a) otra parte del grafo, en otro tono.
                    // El de control (un controlador que maneja la tarjeta, el sidechain de un
                    // filtro), ámbar y punteado; el de un canal suelto, más fino y con sus canales.
                    color: control ? pb.colorControl : cruza ? Qt.rgba(0.78, 0.6, 1, 0.9) : Qt.rgba(0.55, 0.75, 1, 0.9)
                    grosor: control || cab.chSalida >= 0 ? 2 : 3
                    punteado: control
                    etiqueta: cab.chSalida >= 0 ? pb.chName(cab.chSalida) + "→" + pb.chName(cab.chEntrada) : ""
                    onQuitar: pb.desconectar(modelData.desde, modelData.salida, modelData.hacia, modelData.entrada, cab.chSalida, cab.chEntrada)
                }
            }

            // Cable en construcción
            CablePatchbay {
                anchors.fill: parent
                visible: pb.wire !== null
                geo: pb.geo
                quitable: false
                punteado: true
                color: pb.wire && pb.wire.tipo === "control" ? pb.colorControl : Theme.ok
                a: pb.wire ? Qt.point(pb.wire.x0, pb.wire.y0) : Qt.point(0, 0)
                b: pb.wire ? Qt.point(pb.wire.x1, pb.wire.y1) : Qt.point(0, 0)
            }

            // Tarjetas
            Repeater {
                model: pb.nodos
                delegate: Rectangle {
                    id: nd
                    required property var modelData
                    readonly property bool externo: !!modelData.externo
                    readonly property bool grande: pb.expandida(modelData.id)
                    x: modelData.x
                    y: modelData.y
                    width: pb.nodeW
                    height: pb.geo.nodeH(modelData)
                    radius: 12
                    color: Qt.rgba(0.08, 0.1, 0.15, nd.externo ? 0.7 : 0.96)
                    opacity: nd.externo ? 0.75 : modelData.bypass ? 0.55 : 1
                    readonly property var alarma: pb.alarmas ? pb.alarmas[modelData.id] : undefined
                    border.width: pb.seleccionado === modelData.id || nd.alarma ? 2 : 1
                    border.color: pb.seleccionado === modelData.id ? Theme.accent
                                : (pb.wireTarget && pb.wireTarget.node === modelData.id) ? Theme.ok
                                : nd.alarma ? Theme.colorAlarma(nd.alarma.nivel)
                                : nd.externo ? Qt.rgba(0.78, 0.6, 1, 0.6) : Qt.rgba(1, 1, 1, 0.14)
                    onXChanged: pb.geo.mover(modelData.id, x, y)
                    onYChanged: pb.geo.mover(modelData.id, x, y)

                    // Un clic en cualquier lado de la tarjeta la elige. Va primero: los puertos
                    // y la cabecera quedan encima.
                    MouseArea { anchors.fill: parent; onClicked: pb.elegir(nd.modelData.id) }

                    Rectangle {
                        id: head
                        width: parent.width
                        height: pb.headH
                        radius: 12
                        color: Qt.rgba(1, 1, 1, 0.06)
                        Rectangle { width: 6; height: parent.height - 12; radius: 3; x: 8; anchors.verticalCenter: parent.verticalCenter; color: pb.colorDe(nd.modelData.tipo) }
                        Text {
                            x: 22
                            width: parent.width - (pb.expandibles ? 52 : 30)
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
                            // OJO: no tocar nd.x/nd.y mientras se arrastra. El arrastre calcula la
                            // posición a partir de la actual, así que el ajuste a la grilla se le
                            // sumaba y la tarjeta se escapaba del cursor. Se muestra la celda
                            // destino y se encaja al soltar.
                            drag.onActiveChanged: {
                                pb.draggingNode = drag.active
                                if (drag.active) return
                                nd.x = pb.geo.snap(nd.x)
                                nd.y = pb.geo.snap(nd.y)
                                pb.mover(nd.modelData.id, nd.x, nd.y)
                            }
                            onClicked: pb.elegir(nd.modelData.id)
                        }
                        // Agrandar la tarjeta: los nombres de los puertos y si son mono o estéreo.
                        // Después del área de arrastre: si no, se come el clic.
                        Rectangle {
                            visible: pb.expandibles
                            anchors { right: parent.right; rightMargin: 6; verticalCenter: parent.verticalCenter }
                            width: 22; height: 22; radius: 11
                            color: detMa.containsMouse ? Qt.rgba(1, 1, 1, 0.16) : "transparent"
                            Text {
                                anchors.centerIn: parent
                                text: nd.grande ? "⌃" : "⌄"
                                color: Theme.textFaint; font.pixelSize: 12
                            }
                            MouseArea {
                                id: detMa
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: pb.alternarExpandida(nd.modelData.id)
                                ToolTip.visible: containsMouse
                                ToolTip.delay: 500
                                ToolTip.text: nd.grande ? qsTr("Mostrar la tarjeta simple")
                                                        : qsTr("Mostrar los nombres de los puertos y si son mono o estéreo")
                            }
                        }
                    }
                    // Celda donde va a quedar al soltar.
                    Rectangle {
                        visible: headMa.drag.active
                        x: pb.geo.snap(nd.x) - nd.x
                        y: pb.geo.snap(nd.y) - nd.y
                        width: nd.width; height: nd.height
                        z: -1
                        radius: 12
                        color: Qt.rgba(1, 1, 1, 0.05)
                        border { color: Theme.accent; width: 2 }
                    }
                    // La alarma: un «!» del color del nivel; los motivos al pasar por encima.
                    Rectangle {
                        visible: !!nd.alarma
                        anchors { right: parent.right; top: parent.top; margins: -7 }
                        width: 20; height: 20; radius: 10
                        color: nd.alarma ? Theme.colorAlarma(nd.alarma.nivel) : "transparent"
                        border.color: Qt.rgba(0, 0, 0, 0.5)
                        z: 5
                        Text { anchors.centerIn: parent; text: "!"; color: "#1a1a1a"; font { pixelSize: 13; weight: Font.Bold } }
                        MouseArea { id: alarmaMa; anchors.fill: parent; hoverEnabled: true; onClicked: pb.elegir(nd.modelData.id) }
                        ToolTip.visible: alarmaMa.containsMouse
                        ToolTip.text: nd.alarma ? Theme.textoAlarma(nd.alarma.nivel) + ":\n• " + (nd.alarma.motivos || []).join("\n• ") : ""
                    }
                    // El cuerpo: el resumen, o lo que ponga quien lo usa (los medidores del audio).
                    Loader {
                        id: cuerpo
                        anchors { left: parent.left; right: parent.right; bottom: parent.bottom; margins: 10; bottomMargin: 6 }
                        height: pb.bodyH - 8
                        sourceComponent: pb.cuerpo
                        Binding { target: cuerpo.item; property: "nodo"; value: nd.modelData; when: cuerpo.status === Loader.Ready }
                    }

                    // Entradas (izquierda)
                    Repeater {
                        model: nd.modelData.entradas || []
                        delegate: Item {
                            id: ip
                            required property var modelData
                            required property int index
                            readonly property string pid: pb.geo.idDe(modelData)
                            readonly property var info: pb.geo.infoDe(nd.modelData, true, index)
                            readonly property color base: pb.tipoDe(ip.info, ip.pid) === "control" ? pb.colorControl : "#93c5fd"
                            readonly property bool blanco: !!pb.wireTarget && pb.wireTarget.node === nd.modelData.id && pb.wireTarget.port === ip.pid
                            readonly property int chBlanco: blanco ? pb.wireTarget.ch : -2
                            x: -7
                            y: pb.headH + index * pb.rowH(nd.modelData.id) + pb.rowH(nd.modelData.id) / 2 - 7
                            width: 14; height: 14
                            // Halo cuando el cable que se está tirando va a caer acá.
                            Rectangle {
                                anchors.centerIn: parent
                                width: 30; height: 30; radius: 15
                                visible: ip.blanco && ip.info.canales === 2
                                color: Qt.rgba(0.53, 0.94, 0.67, 0.28)
                                border.color: Theme.ok; border.width: 2
                            }
                            Rectangle {
                                visible: ip.info.canales !== 2
                                anchors.fill: parent
                                radius: 7
                                color: ip.blanco ? Theme.ok : ip.info.pedido ? "transparent" : ip.base
                                border.color: ip.info.pedido ? ip.base : "white"
                                border.width: ip.info.pedido ? 2 : 1
                                Text { visible: !!ip.info.pedido; anchors.centerIn: parent; text: "+"; color: "#93c5fd"; font.pixelSize: 10; font.weight: Font.Black }
                            }
                            // Estéreo: dos puntos (L y R), para diferenciarlos de un vistazo.
                            Column {
                                z: 3
                                visible: ip.info.canales === 2
                                anchors.centerIn: parent
                                spacing: 2 * pb.chOff(nd.modelData.id) - pb.chH(nd.modelData.id)
                                Repeater {
                                    model: [0, 1]
                                    delegate: Rectangle {
                                        id: inDot
                                        required property int modelData
                                        width: pb.chW(nd.modelData.id)
                                        height: pb.chH(nd.modelData.id)
                                        radius: height / 2
                                        // Verde si el cable que se arrastra va a caer acá; más
                                        // claro con el mouse encima.
                                        color: ip.chBlanco === inDot.modelData ? Theme.ok
                                               : (inChMa.containsMouse ? Qt.lighter(ip.base, 1.35) : ip.base)
                                        border.color: inChMa.containsMouse ? Theme.accent : "white"
                                        border.width: inChMa.containsMouse ? 2 : 1
                                        scale: inChMa.containsMouse ? 1.12 : 1
                                        Behavior on scale { NumberAnimation { duration: 90 } }
                                        // Solo para el hover: no acepta botones, así no interfiere
                                        // con soltar un cable encima.
                                        MouseArea { id: inChMa; anchors.fill: parent; anchors.margins: -3; hoverEnabled: true; acceptedButtons: Qt.NoButton }
                                        Text {
                                            anchors.centerIn: parent
                                            text: pb.chName(inDot.modelData)
                                            color: "#0b1220"
                                            font { pixelSize: nd.grande ? 10 : 7; weight: Font.Black }
                                        }
                                    }
                                }
                            }
                            Column {
                                x: 18
                                anchors.verticalCenter: parent.verticalCenter
                                Text {
                                    readonly property string t: pb.etiquetaPuerto(ip.info, ip.pid, true, nd.modelData)
                                    visible: t !== ""
                                    text: t; color: Theme.textFaint; font.pixelSize: 10
                                }
                                Text {
                                    visible: nd.grande
                                    width: pb.nodeW / 2 - 24
                                    elide: Text.ElideRight
                                    text: (ip.info.nombre ? ip.info.nombre + " · " : "") + (ip.info.canales === 1 ? qsTr("mono") : qsTr("estéreo"))
                                    color: Theme.textFaint; font.pixelSize: 9
                                }
                            }
                        }
                    }
                    // Salidas (derecha): de acá se tira el cable (del puerto entero o de un canal).
                    Repeater {
                        model: nd.modelData.salidas || []
                        delegate: Item {
                            id: op
                            required property var modelData
                            required property int index
                            readonly property string pid: pb.geo.idDe(modelData)
                            readonly property var info: pb.geo.infoDe(nd.modelData, false, index)
                            readonly property string tipo: pb.tipoDe(op.info, op.pid)
                            readonly property color base: op.tipo === "control" ? pb.colorControl : "#86efac"
                            x: pb.nodeW - 7
                            y: pb.headH + index * pb.rowH(nd.modelData.id) + pb.rowH(nd.modelData.id) / 2 - 7
                            width: 14; height: 14
                            Column {
                                anchors { right: parent.left; rightMargin: 4; verticalCenter: parent.verticalCenter }
                                Text {
                                    readonly property string t: pb.etiquetaPuerto(op.info, op.pid, false, nd.modelData)
                                    visible: t !== ""
                                    anchors.right: parent.right
                                    text: t; color: Theme.textFaint; font.pixelSize: 10
                                }
                                Text {
                                    anchors.right: parent.right
                                    visible: nd.grande
                                    width: pb.nodeW / 2 - 24
                                    horizontalAlignment: Text.AlignRight
                                    elide: Text.ElideRight
                                    text: (op.info.nombre ? op.info.nombre + " · " : "") + (op.info.canales === 1 ? qsTr("mono") : qsTr("estéreo"))
                                    color: Theme.textFaint; font.pixelSize: 9
                                }
                            }
                            Rectangle {
                                visible: op.info.canales !== 2
                                anchors.fill: parent
                                radius: 7
                                color: op.base
                                border.color: "white"; border.width: 1
                            }
                            Column {
                                // Por encima del área del puerto entero (que se declara después):
                                // si no, se comía el hover y el clic de los puntos.
                                z: 3
                                visible: op.info.canales === 2
                                anchors.centerIn: parent
                                spacing: 2 * pb.chOff(nd.modelData.id) - pb.chH(nd.modelData.id)
                                Repeater {
                                    model: [0, 1]
                                    delegate: Rectangle {
                                        id: chDot
                                        required property int modelData
                                        width: pb.chW(nd.modelData.id)
                                        height: pb.chH(nd.modelData.id)
                                        radius: height / 2
                                        // Encendido con el mouse encima y mientras se arrastra SU
                                        // cable (ahí el hover no se actualiza: el área tiene agarrado
                                        // el mouse).
                                        readonly property bool lit: outChMa.containsMouse
                                            || (!!pb.wire && pb.wire.from === nd.modelData.id && pb.wire.out === op.pid && pb.wire.ch === chDot.modelData)
                                        color: lit ? Qt.lighter(op.base, 1.25) : op.base
                                        border.color: lit ? Theme.accent : "white"
                                        border.width: lit ? 2 : 1
                                        scale: lit ? 1.12 : 1
                                        Behavior on scale { NumberAnimation { duration: 90 } }
                                        Text {
                                            anchors.centerIn: parent
                                            text: pb.chName(chDot.modelData)
                                            color: "#0b1220"
                                            font { pixelSize: nd.grande ? 10 : 7; weight: Font.Black }
                                        }
                                        // Arrastrar desde L o R lleva SOLO ese canal. Chico a
                                        // propósito: el hueco del medio sigue siendo del puerto entero.
                                        MouseArea {
                                            id: outChMa
                                            anchors.fill: parent
                                            anchors.margins: -3
                                            cursorShape: Qt.CrossCursor
                                            preventStealing: true
                                            hoverEnabled: true
                                            ToolTip.visible: containsMouse && !pb.wire
                                            ToolTip.delay: 600
                                            ToolTip.text: qsTr("Canal %1: arrastralo a la entrada que quieras").arg(pb.chName(chDot.modelData))
                                            onPressed: pb.empezarCable(nd.modelData.id, op.pid, op.tipo, chDot.modelData)
                                            onPositionChanged: (ev) => pb.moverCable(outChMa, ev)
                                            onReleased: pb.soltarCable()
                                            onCanceled: pb.cancelarCable()
                                        }
                                    }
                                }
                            }
                            MouseArea {
                                id: outMa
                                anchors.fill: parent
                                // Agarre generoso: el punto es chico, la zona no.
                                anchors.margins: -12
                                cursorShape: Qt.CrossCursor
                                preventStealing: true
                                hoverEnabled: true
                                ToolTip.visible: containsMouse && !pb.wire
                                ToolTip.delay: 600
                                ToolTip.text: op.tipo === "control"
                                              ? qsTr("Arrastrá hasta una tarjeta igual (de cualquier entrada): pasa a usar los ajustes de este controlador")
                                              : qsTr("Arrastrá hasta la tarjeta (o la entrada) que quieras alimentar: de una salida pueden salir varios cables")
                                onPressed: pb.empezarCable(nd.modelData.id, op.pid, op.tipo, -1)
                                onPositionChanged: (ev) => pb.moverCable(outMa, ev)
                                onReleased: pb.soltarCable()
                                onCanceled: pb.cancelarCable()
                            }
                        }
                    }
                }
            }
        }
    }

    // El cuerpo de siempre: lo que hace la tarjeta (un resumen corto de sus parámetros).
    Component {
        id: resumenC
        Text {
            property var nodo: ({})
            text: nodo.resumen || ""
            color: Theme.textFaint; font.pixelSize: 11
            leftPadding: 4; rightPadding: 4
            verticalAlignment: Text.AlignVCenter
            wrapMode: Text.WordWrap; maximumLineCount: 2; elide: Text.ElideRight
        }
    }

    // Cmd/Ctrl + rueda: zoom (la rueda sola sigue desplazando).
    WheelHandler {
        target: null
        acceptedModifiers: Qt.ControlModifier | Qt.MetaModifier
        onWheel: (ev) => pb.acercar(ev.angleDelta.y > 0 ? 1.12 : 1 / 1.12)
    }
}
