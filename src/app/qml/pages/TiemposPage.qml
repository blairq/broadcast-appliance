// Página «Latencias del pipeline» (id «latencias»): cuánto tarda cada cosa, de cada entrada a cada salida.
//
// Un mapa de izquierda a derecha (entradas → tarjetas y módulos → escena y mezcla →
// salidas) con una tarjeta por nodo y líneas de luz que laten al ritmo de su demora.
// A la izquierda, qué entradas mostrar y los análisis; a la derecha, el total hasta
// cada salida; abajo, el estado de los análisis. Todo sale de Studio.tiempos
// (TiemposModel): lo mismo que el MCP (tiempos_mapa, tiempos_nodo, tiempos_medir).
// Mientras la página no se ve, el modelo no mide nada (activo = false).
import QtQuick
import QtQuick.Controls
import QtQuick.Effects
import QtQuick.Layouts
import BA
import "../controls"

Rectangle {
    id: page
    color: Theme.bg
    property real leftInset: 92
    readonly property var tm: Studio.tiempos
    readonly property var mapa: tm ? tm.mapa : ({})

    // Mide solo mientras se la mira.
    Binding { target: page.tm; property: "activo"; value: page.visible; when: page.tm !== null }
    // El plan de resolución también (los colores de alarma de las tarjetas).
    property bool mirandoRes: false
    function mirarRes() {
        if (!Studio.resolucion || page.visible === page.mirandoRes) return
        Studio.resolucion.mirar(page.visible)
        page.mirandoRes = page.visible
    }
    onVisibleChanged: mirarRes()
    Component.onCompleted: { mirarRes(); sincronizarMapa() }
    Component.onDestruction: if (page.mirandoRes && Studio.resolucion) Studio.resolucion.mirar(false)
    readonly property var nivelesRes: Studio.resolucion ? Studio.resolucion.niveles : ({})
    /// La clave del nodo en los niveles del plan: la tarjeta por el id de su nodo del grafo; la
    /// entrada (su video) por la fuente.
    function claveAlarma(n) {
        if (n.tipo === "tarjeta") return n.id // «tarjeta:<entrada>:<id>»: el mismo id que el nodo
        if (n.tipo === "entrada" && n.medio === "video") return "entrada:" + page.entradaDe(n)
        return ""
    }

    // --- Lo que se elige mostrar ------------------------------------------------------
    property var ocultas: ({})      ///< entrada → true si se la escondió
    property bool verReposo: false  ///< también las entradas a las que no les llega nada
    property bool verVideo: true
    property bool verAudio: true
    property bool soloMedido: false
    property string elegido: ""     ///< el nodo tocado (resalta su camino)
    /// La pestaña: "mapa" (los tiempos de cada entrada a cada salida) o "sincronia" (la latencia del
    /// estudio, quién la marca, la clase de cada entrada, reloj y red). La vista única de sincronización.
    property string pestana: "mapa"
    Connections {
        target: Studio
        function onMostrarSeccion(pagina, seccion) { if (pagina === "latencias") page.pestana = seccion === "sincronia" ? "sincronia" : "mapa" }
    }
    /// La entrada cuyo recorrido por dentro se ve (al tocar su tarjeta o la de uno de sus pasos).
    property string recorridoDe: ""
    property var recorridoElementos: []   ///< los elementos de la tarjeta elegida (se resaltan)
    function abrirRecorrido(n) {
        const e = (n.medio === "video" || n.medio === "audio") && (n.tipo === "entrada" || n.tipo === "tarjeta") ? page.entradaDe(n) : ""
        if (e === "") return
        page.recorridoDe = e
        page.recorridoElementos = ((n.detalle && n.detalle.elementos) || []).map(x => x.nombre)
    }

    // --- La línea de tiempo -------------------------------------------------------------
    /// Los nodos de la línea de tiempo (en el modelo: el asistente también la elige, tiempos_linea).
    readonly property var serie: page.tm ? page.tm.linea : []
    readonly property var paleta: ["#4fd8ff", "#ffb347", "#c38bff", "#35c46b", "#ff6b9a", "#f5e663", "#7aa2ff", "#ff7a45"]
    property string modoLinea: "superpuestas"
    property bool lineaAcumulado: false
    property int ventanaS: 60
    property bool pausada: false
    property real finPausa: 0
    property var datosPausa: []
    property var eventosPausa: []
    function alternarSerie(id) {
        const s = page.serie.slice()
        const i = s.indexOf(id)
        if (i >= 0) s.splice(i, 1); else s.push(id)
        page.tm.linea = s
    }
    function tituloDe(id) {
        for (const n of (page.mapa.nodos || [])) if (n.id === id) return n.medio === "audio" && n.tipo === "entrada" ? qsTr("%1 (audio)").arg(n.titulo) : n.titulo
        for (const s of (page.mapa.salidas || [])) if (s.id === id) return s.titulo
        return id
    }
    /// Las series con sus datos (renovados con cada vuelta del modelo, salvo en pausa).
    readonly property var seriesVivas: {
        page.vuelta
        return page.serie.map((id, i) => ({ id: id, titulo: page.tituloDe(id), color: page.paleta[i % page.paleta.length],
                                           datos: page.tm ? page.tm.historia(id, page.ventanaS) : [] }))
    }
    function pausar() {
        page.datosPausa = page.seriesVivas
        page.eventosPausa = page.tm ? page.tm.eventos : []
        page.finPausa = Date.now()
        page.pausada = true
    }

    function entradaDe(n) {
        if (n.detalle && n.detalle.entrada) return n.detalle.entrada
        const p = n.id.split(":")
        return (p[0] === "tarjeta" || p[0] === "modulo") ? p[1] : ""
    }
    function visibleNodo(n) {
        const e = entradaDe(n)
        if (e !== "" && page.ocultas[e]) return false
        if (e !== "" && !page.verReposo && n.activo === false) return false
        if (n.medio === "video" && !page.verVideo) return false
        if (n.medio === "audio" && !page.verAudio) return false
        if (page.soloMedido && n.fuente !== "medido" && n.columna < 2) return false
        return true
    }
    /// Las entradas que hay (para los botones de la izquierda), activas primero.
    readonly property var entradas: {
        const vistas = {}, out = []
        for (const n of (mapa.nodos || []))
            if (n.tipo === "entrada") {
                const e = entradaDe(n)
                if (vistas[e]) { vistas[e].activa = vistas[e].activa || n.activo; continue }
                vistas[e] = { id: e, titulo: n.titulo, activa: n.activo, estado: n.estado }
                out.push(vistas[e])
            }
        out.sort((a, b) => (b.activa - a.activa) || a.titulo.localeCompare(b.titulo))
        return out
    }

    // --- El acomodo: columnas por profundidad, filas por entrada ------------------------
    readonly property int anchoCol: 250
    readonly property int altoFila: 140
    readonly property var acomodo: {
        const nodos = (mapa.nodos || []).filter(visibleNodo)
        const porId = {}
        for (const n of nodos) porId[n.id] = n
        const tramos = (mapa.tramos || []).filter(t => porId[t.desde] && porId[t.hacia])
        const entran = {}
        for (const t of tramos) (entran[t.hacia] = entran[t.hacia] || []).push(t.desde)
        // Profundidad: lo más largo desde una fuente.
        const prof = {}
        const profDe = (id, guardia) => {
            if (prof[id] !== undefined) return prof[id]
            if (guardia > 64) return 0
            let p = 0
            for (const d of (entran[id] || [])) p = Math.max(p, profDe(d, guardia + 1) + 1)
            return (prof[id] = p)
        }
        for (const n of nodos) profDe(n.id, 0)
        // Las escenas, mezclas y salidas, alineadas en sus columnas (después de todas las tarjetas).
        let maxRama = 0
        for (const n of nodos) if (n.columna < 2) maxRama = Math.max(maxRama, prof[n.id])
        for (const n of nodos) if (n.columna === 2) prof[n.id] = maxRama + 1
        for (const n of nodos) if (n.columna === 3) prof[n.id] = maxRama + 2
        // Filas: cada rama de entrada en la suya; lo demás, al medio de lo que le llega.
        const fila = {}
        let f = 0
        for (const n of nodos) if (n.tipo === "entrada") fila[n.id] = f++
        for (const n of nodos)
            if (n.columna === 1 && n.tipo !== "derivacion") {
                const ent = (n.medio === "audio" ? "entrada:" + entradaDe(n) + ":audio" : "entrada:" + entradaDe(n))
                fila[n.id] = fila[ent] !== undefined ? fila[ent] : f++
            }
        // Las ramas (voz a texto…): en una fila propia, debajo de las demás.
        for (const n of nodos) if (n.tipo === "derivacion") fila[n.id] = f++
        for (const col of [2, 3]) {
            const enCol = nodos.filter(n => n.columna === col)
            for (const n of enCol) {
                const ds = (entran[n.id] || []).filter(d => fila[d] !== undefined)
                fila[n.id] = ds.length ? ds.reduce((a, d) => a + fila[d], 0) / ds.length : 0
            }
            // Que no se pisen: en orden, al menos una fila de distancia.
            enCol.sort((a, b) => fila[a.id] - fila[b.id])
            for (let i = 1; i < enCol.length; ++i)
                fila[enCol[i].id] = Math.max(fila[enCol[i].id], fila[enCol[i - 1].id] + 1)
        }
        const pos = {}
        let maxX = 0, maxY = 0
        for (const n of nodos) {
            pos[n.id] = Qt.point(40 + prof[n.id] * page.anchoCol, 40 + fila[n.id] * page.altoFila)
            maxX = Math.max(maxX, pos[n.id].x); maxY = Math.max(maxY, pos[n.id].y)
        }
        return { nodos: nodos, tramos: tramos, pos: pos, ancho: maxX + 280, alto: maxY + 180 }
    }
    // LAS TARJETAS Y LAS LÍNEAS VIVEN ENTRE VUELTAS. Con el arreglo de `acomodo` como modelo, el
    // Repeater las destruía y creaba TODAS en cada vuelta del modelo: parpadeaban, perdían el
    // hover y se volvían a animar. Ahora el modelo es un ListModel de claves que solo cambia
    // cuando aparece o se va una; cada una lee lo suyo de estos mapas.
    readonly property var nodoPorId: { const m = {}; for (const n of page.acomodo.nodos) m[n.id] = n; return m }
    readonly property var tramoPorId: { const m = {}; for (const t of page.acomodo.tramos) m[t.desde + ">" + t.hacia] = t; return m }
    ListModel { id: modeloNodos }
    ListModel { id: modeloTramos }
    function sincronizar(modelo, claves) {
        const quiero = {}
        for (const c of claves) quiero[c] = true
        for (let i = modelo.count - 1; i >= 0; --i) if (!quiero[modelo.get(i).clave]) modelo.remove(i)
        const hay = {}
        for (let i = 0; i < modelo.count; ++i) hay[modelo.get(i).clave] = true
        for (const c of claves) if (!hay[c]) modelo.append({ clave: c })
    }
    function sincronizarMapa() {
        sincronizar(modeloNodos, page.acomodo.nodos.map(n => n.id))
        sincronizar(modeloTramos, page.acomodo.tramos.map(t => t.desde + ">" + t.hacia))
    }
    onAcomodoChanged: sincronizarMapa()
    /// La tarjeta dada vuelta (el dorso con el detalle): «Detalle» en la barra de la elegida.
    property string volteada: ""

    /// El camino del nodo elegido (lo que lo explica), para resaltarlo.
    readonly property var camino: {
        if (page.elegido === "") return ({})
        const c = {}
        for (const s of (mapa.salidas || []))
            if (s.id === page.elegido || (s.camino || []).indexOf(page.elegido) >= 0)
                for (const id of (s.camino || [])) c[id] = true
        c[page.elegido] = true
        return c
    }
    // La historia de cada tarjeta, renovada con cada vuelta del modelo.
    property int vuelta: 0
    Connections { target: page.tm; function onCambio() { page.vuelta++ } }

    function ms(v) { return v === null || v === undefined ? "—" : (v >= 1000 ? (v / 1000).toFixed(2) + " s" : Math.round(v) + " ms") }
    function hace(t) {
        const s = Math.round((Date.now() - t) / 1000)
        return s < 60 ? qsTr("hace %1 s").arg(s) : s < 3600 ? qsTr("hace %1 min").arg(Math.round(s / 60)) : new Date(t).toLocaleString(Qt.locale(), "d/M HH:mm")
    }

    // --- Fondo: la profundidad -----------------------------------------------------------
    Rectangle {
        anchors.fill: parent
        gradient: Gradient {
            GradientStop { position: 0.0; color: "#0d1220" }
            GradientStop { position: 0.55; color: Theme.bg }
            GradientStop { position: 1.0; color: "#120c16" }
        }
    }

    ColumnLayout {
        anchors { fill: parent; leftMargin: page.leftInset; margins: 16 }
        spacing: 10

        // --- Encabezado -------------------------------------------------------------------
        RowLayout {
            Layout.fillWidth: true
            spacing: 10
            Text { text: qsTr("Latencias y sincronía"); color: Theme.text; font { pixelSize: 20; weight: Font.DemiBold } }
            Row {
                spacing: 4
                GlassButton { compact: true; text: qsTr("Mapa"); icon: "⤳"; active: page.pestana === "mapa"; onClicked: page.pestana = "mapa" }
                GlassButton { compact: true; text: qsTr("Sincronía"); icon: "⏱"; active: page.pestana === "sincronia"; onClicked: page.pestana = "sincronia" }
            }
            StatusPill {
                readonly property var es: page.mapa.estudio || {}
                text: qsTr("Estudio %1").arg(page.ms(es.latencia_ms)) + (es.tope_ms ? " · " + qsTr("tope %1").arg(page.ms(es.tope_ms)) : "")
                dot: Theme.ok
                TapHandler { onTapped: page.pestana = "sincronia" }
            }
            Text {
                Layout.fillWidth: true
                text: page.pestana === "mapa"
                      ? qsTr("De la captura a cada salida. Medido: lo que pasó de verdad · declarado: la latencia que se le fijó. Tocá una tarjeta para ver su detalle.")
                      : qsTr("Todo alineado con la menor latencia: quién la marca, qué entradas se sincronizan con la sala, reloj y red.")
                color: Theme.textFaint; font.pixelSize: 11; elide: Text.ElideRight
            }
        }

        // --- Sincronía (la otra pestaña) ----------------------------------------------------
        VistaSincronia {
            visible: page.pestana === "sincronia"
            Layout.fillWidth: true
            Layout.fillHeight: true
            vg: Studio.video
            programaMs: {
                const n = (page.mapa.nodos || []).find(x => x.id === "escena:programa")
                return n && n.fuente === "medido" && n.acumulado_ms !== null && n.acumulado_ms !== undefined ? n.acumulado_ms : -1
            }
            onVerMapa: page.pestana = "mapa"
        }

        RowLayout {
            visible: page.pestana === "mapa"
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 12

            // --- Izquierda: qué mostrar y los análisis ------------------------------------
            Rectangle {
                Layout.preferredWidth: 214
                Layout.fillHeight: true
                radius: 14
                color: Theme.glassTint
                border.color: Theme.glassBorder
                ColumnLayout {
                    anchors { fill: parent; margins: 10 }
                    spacing: 6
                    GlassButton {
                        Layout.fillWidth: true
                        text: page.tm && page.tm.analisis.length && page.tm.analisis[0].estado === "midiendo"
                              ? qsTr("Midiendo… %1 %").arg(Math.round(100 * page.tm.analisis[0].progreso)) : qsTr("Analizar 10 s")
                        icon: "▶"
                        active: true
                        enabled: !(page.tm && page.tm.analisis.length && page.tm.analisis[0].estado === "midiendo")
                        onClicked: { const e = page.tm.analizarUi(10); if (e) Studio.notify(e, "error") }
                    }
                    GlassButton {
                        Layout.fillWidth: true
                        text: qsTr("Marca de tiempo")
                        icon: "⏺"
                        enabled: false
                        ToolTip.visible: hovered; ToolTip.delay: 300
                        ToolTip.text: qsTr("Próximamente: cada entrada estampa su hora de captura (en ns) y se lee en la TV, los invitados y YouTube.")
                    }
                    Rectangle { Layout.fillWidth: true; height: 1; color: Theme.glassBorder }
                    Text { text: qsTr("Mostrar"); color: Theme.textDim; font.pixelSize: 11 }
                    Flow {
                        Layout.fillWidth: true
                        spacing: 4
                        GlassButton { compact: true; text: qsTr("Video"); active: page.verVideo; onClicked: page.verVideo = !page.verVideo }
                        GlassButton { compact: true; text: qsTr("Audio"); active: page.verAudio; onClicked: page.verAudio = !page.verAudio }
                        GlassButton { compact: true; text: qsTr("Solo medido"); active: page.soloMedido; onClicked: page.soloMedido = !page.soloMedido }
                        GlassButton { compact: true; text: qsTr("En reposo"); active: page.verReposo; onClicked: page.verReposo = !page.verReposo }
                    }
                    Rectangle { Layout.fillWidth: true; height: 1; color: Theme.glassBorder }
                    RowLayout {
                        Layout.fillWidth: true
                        Text { text: qsTr("Entradas"); color: Theme.textDim; font.pixelSize: 11; Layout.fillWidth: true }
                        Text {
                            text: qsTr("todas"); color: Theme.accent; font.pixelSize: 10
                            TapHandler { onTapped: page.ocultas = ({}) }
                        }
                    }
                    ListView {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        spacing: 2
                        model: page.entradas.filter(e => e.activa || page.verReposo)
                        delegate: Rectangle {
                            id: filaE
                            required property var modelData
                            readonly property bool oculta: !!page.ocultas[modelData.id]
                            width: ListView.view.width
                            height: 26
                            radius: 7
                            color: hv.hovered ? Qt.rgba(1, 1, 1, 0.06) : "transparent"
                            HoverHandler { id: hv }
                            RowLayout {
                                anchors { fill: parent; leftMargin: 6; rightMargin: 6 }
                                spacing: 6
                                Rectangle {
                                    width: 14; height: 14; radius: 4
                                    color: filaE.oculta ? "transparent" : Theme.accent
                                    border.color: filaE.oculta ? Theme.textFaint : Theme.accent
                                    Text { anchors.centerIn: parent; text: filaE.oculta ? "" : "✓"; color: "white"; font.pixelSize: 9 }
                                }
                                Text {
                                    Layout.fillWidth: true
                                    text: filaE.modelData.titulo
                                    color: filaE.modelData.activa ? Theme.text : Theme.textFaint
                                    font.pixelSize: 11; elide: Text.ElideRight
                                }
                                Rectangle {
                                    width: 6; height: 6; radius: 3
                                    color: filaE.modelData.estado === "tarde" ? Theme.live : filaE.modelData.estado === "justo" ? Theme.amber
                                         : filaE.modelData.activa ? Theme.ok : Theme.textFaint
                                }
                            }
                            TapHandler {
                                onTapped: { const o = Object.assign({}, page.ocultas); if (o[filaE.modelData.id]) delete o[filaE.modelData.id]; else o[filaE.modelData.id] = true; page.ocultas = o }
                                onDoubleTapped: {
                                    // Solo esta.
                                    const o = {}
                                    for (const e of page.entradas) if (e.id !== filaE.modelData.id) o[e.id] = true
                                    page.ocultas = o
                                }
                            }
                        }
                    }
                }
            }

            // --- El lienzo: el mapa que late --------------------------------------------------
            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                radius: 16
                color: Qt.rgba(0, 0, 0, 0.25)
                border.color: Theme.glassBorder
                clip: true

                Text {
                    anchors.centerIn: parent
                    visible: page.acomodo.nodos.length === 0
                    text: page.tm ? qsTr("Todavía no hay nada que mostrar: ¿el motor está andando?") : qsTr("El análisis de latencias no está disponible")
                    color: Theme.textFaint; font.pixelSize: 13
                }

                Flickable {
                    id: lienzo
                    anchors.fill: parent
                    anchors.topMargin: barraElegida.visible ? barraElegida.height + 14 : 0 // debajo de la barra de la elegida
                    contentWidth: mundo.width * mundo.escala
                    contentHeight: mundo.height * mundo.escala
                    boundsBehavior: Flickable.StopAtBounds
                    ScrollBar.horizontal: ScrollBar {}
                    ScrollBar.vertical: ScrollBar {}

                    Item {
                        id: mundo
                        property real escala: 1.0
                        width: Math.max(page.acomodo.ancho, lienzo.width)
                        height: Math.max(page.acomodo.alto, lienzo.height)
                        scale: escala
                        transformOrigin: Item.TopLeft

                        // Las líneas, con su luz (una copia desenfocada debajo).
                        Item {
                            id: lineas
                            anchors.fill: parent
                            layer.enabled: true
                            Repeater {
                                model: modeloTramos
                                delegate: LineaTiempo {
                                    required property string clave
                                    readonly property var modelData: page.tramoPorId[clave] || ({ desde: "", hacia: "", medio: "video", ms: null })
                                    anchors.fill: parent
                                    readonly property point a: page.acomodo.pos[modelData.desde] || Qt.point(0, 0)
                                    readonly property point b: page.acomodo.pos[modelData.hacia] || Qt.point(0, 0)
                                    desde: Qt.point(a.x + 196, a.y + 59)
                                    hacia: Qt.point(b.x, b.y + 59)
                                    medio: modelData.medio || "video"
                                    ms: modelData.ms
                                    espera: modelData.espera_ms === undefined ? null : modelData.espera_ms
                                    viva: modelData.ms !== null && modelData.ms !== undefined
                                    resaltada: !!page.camino[modelData.desde] && !!page.camino[modelData.hacia]
                                }
                            }
                        }
                        MultiEffect {
                            anchors.fill: lineas
                            source: lineas
                            blurEnabled: true
                            blur: 1.0
                            blurMax: 24
                            brightness: 0.25
                            opacity: 0.9
                            z: -1
                        }

                        // Las tarjetas.
                        Repeater {
                            model: modeloNodos
                            delegate: TarjetaTiempo {
                                required property string clave
                                readonly property var modelData: page.nodoPorId[clave] || ({ id: clave, titulo: "" })
                                nodo: modelData
                                x: (page.acomodo.pos[modelData.id] || Qt.point(0, 0)).x
                                y: (page.acomodo.pos[modelData.id] || Qt.point(0, 0)).y
                                resaltada: !!page.camino[modelData.id]
                                historia: { page.vuelta; return page.tm ? page.tm.historia(modelData.id) : [] }
                                // Un click la elige (o la suelta); lo que se hace con ella, en la barra de arriba.
                                onElegida: (id) => {
                                    page.elegido = page.elegido === id ? "" : id
                                    if (page.volteada !== page.elegido) page.volteada = ""
                                    if (page.elegido !== "") page.abrirRecorrido(modelData)
                                }
                                marcada: page.elegido === modelData.id
                                volteada: page.volteada === modelData.id
                                alarma: page.nivelesRes[page.claveAlarma(modelData)]
                                enLinea: page.serie.indexOf(modelData.id) >= 0
                                colorLinea: page.paleta[Math.max(0, page.serie.indexOf(modelData.id)) % page.paleta.length]
                                Behavior on x { NumberAnimation { duration: 300; easing.type: Easing.OutCubic } }
                                Behavior on y { NumberAnimation { duration: 300; easing.type: Easing.OutCubic } }
                            }
                        }
                    }
                }
                // QUÉ SE PUEDE HACER CON LA ELEGIDA, a la vista (antes: un click la sumaba a la
                // gráfica y abría el análisis, un doble click la daba vuelta, y nada lo decía).
                Rectangle {
                    id: barraElegida
                    readonly property var n: page.elegido !== "" ? page.nodoPorId[page.elegido] : undefined
                    readonly property string entrada: n && (n.tipo === "entrada" || n.tipo === "tarjeta") ? page.entradaDe(n) : ""
                    visible: page.acomodo.nodos.length > 0
                    anchors { left: parent.left; top: parent.top; margins: 10 }
                    width: Math.min(parent.width - 20, filaElegida.implicitWidth + 20)
                    height: filaElegida.implicitHeight + 14
                    radius: 10
                    color: Qt.rgba(0.07, 0.08, 0.10, 0.92)
                    border.color: n ? Theme.accent : Theme.glassBorder
                    z: 20
                    RowLayout {
                        id: filaElegida
                        anchors { left: parent.left; verticalCenter: parent.verticalCenter; leftMargin: 10 }
                        spacing: 8
                        Text {
                            visible: !barraElegida.n
                            text: qsTr("Tocá una tarjeta para elegirla: después, analizarla por dentro, verla en la gráfica o ver su detalle.")
                            color: Theme.textDim; font.pixelSize: 11
                        }
                        Text {
                            visible: !!barraElegida.n
                            text: barraElegida.n ? "✓ " + (barraElegida.n.titulo || "") : ""
                            color: Theme.text; font { pixelSize: 12; weight: Font.DemiBold }
                            elide: Text.ElideRight
                            Layout.maximumWidth: 220
                        }
                        GlassButton {
                            visible: !!barraElegida.n && barraElegida.entrada !== ""
                            compact: true; icon: "🔍"; text: qsTr("Analizar por dentro (5 s)")
                            onClicked: {
                                page.recorridoDe = barraElegida.entrada
                                const e = page.tm.analizarEntradaUi(barraElegida.entrada, 5)
                                if (e) Studio.notify(e, "error")
                            }
                        }
                        GlassButton {
                            visible: !!barraElegida.n
                            compact: true; icon: "📈"; text: qsTr("En la gráfica")
                            active: page.serie.indexOf(page.elegido) >= 0
                            onClicked: page.alternarSerie(page.elegido)
                        }
                        GlassButton {
                            visible: !!barraElegida.n
                            compact: true; icon: "↻"; text: qsTr("Detalle")
                            active: page.volteada === page.elegido
                            onClicked: page.volteada = page.volteada === page.elegido ? "" : page.elegido
                        }
                        GlassButton {
                            visible: !!barraElegida.n
                            compact: true; text: qsTr("Soltar")
                            onClicked: { page.elegido = ""; page.volteada = "" }
                        }
                    }
                }
                // Zoom con la rueda (con Ctrl o el gesto de pellizcar en el trackpad).
                WheelHandler {
                    target: null
                    acceptedModifiers: Qt.ControlModifier
                    onWheel: (ev) => mundo.escala = Math.max(0.35, Math.min(2.0, mundo.escala * (ev.angleDelta.y > 0 ? 1.1 : 0.9)))
                }
                PinchHandler {
                    target: null
                    onScaleChanged: (delta) => mundo.escala = Math.max(0.35, Math.min(2.0, mundo.escala * delta))
                }
                // Encajar: el zoom para que entre todo el mapa (solo al abrir y cuando cambia cuántas tarjetas hay).
                function encajar() {
                    if (page.acomodo.nodos.length === 0 || lienzo.width <= 0) return
                    mundo.escala = Math.max(0.6, Math.min(1.0, Math.min(lienzo.width / page.acomodo.ancho, lienzo.height / page.acomodo.alto))) // legible: lo que no entra, con scroll
                    lienzo.contentX = 0; lienzo.contentY = 0
                }
                property int cuantas: page.acomodo.nodos.length
                onCuantasChanged: Qt.callLater(encajar)
                onHeightChanged: Qt.callLater(encajar)
                Row {
                    anchors { right: parent.right; bottom: parent.bottom; margins: 10 }
                    spacing: 4
                    GlassButton { compact: true; text: qsTr("Encajar"); icon: "⤢"; onClicked: parent.parent.encajar() }
                    GlassButton { compact: true; text: "−"; onClicked: mundo.escala = Math.max(0.35, mundo.escala * 0.85) }
                    GlassButton { compact: true; text: Math.round(mundo.escala * 100) + " %"; onClicked: mundo.escala = 1.0 }
                    GlassButton { compact: true; text: "+"; onClicked: mundo.escala = Math.min(2.0, mundo.escala * 1.15) }
                }
            }

            // --- Derecha: el análisis de la tarjeta elegida y hasta cada salida ---------------
            Rectangle {
                // Se ensancha con el análisis abierto (en vez de un panel encima del mapa).
                Layout.preferredWidth: page.recorridoDe !== "" ? 420 : 220
                Behavior on Layout.preferredWidth { NumberAnimation { duration: 200; easing.type: Easing.OutCubic } }
                Layout.fillHeight: true
                radius: 14
                color: Theme.glassTint
                border.color: Theme.glassBorder
                clip: true
                Flickable {
                    id: lateral
                    anchors { fill: parent; margins: 10 }
                    contentHeight: colLateral.implicitHeight
                    boundsBehavior: Flickable.StopAtBounds
                    clip: true
                    ScrollBar.vertical: ScrollBar {}
                ColumnLayout {
                    id: colLateral
                    width: lateral.width
                    spacing: 8
                    // Por dentro de la entrada elegida: cada elemento, su memoria y los cruces.
                    RecorridoEntrada {
                        visible: page.recorridoDe !== ""
                        Layout.fillWidth: true
                        tm: page.tm
                        entrada: page.recorridoDe
                        detalle: {
                            const ns = page.mapa.nodos || []
                            const n = ns.find(x => x.id === "entrada:" + page.recorridoDe) || ns.find(x => x.id === "entrada:" + page.recorridoDe + ":audio")
                            return n ? n.detalle : ({})
                        }
                        titulo: page.tituloDe("entrada:" + page.recorridoDe) || page.recorridoDe
                        resaltar: page.recorridoElementos
                        onCerrar: page.recorridoDe = ""
                    }
                    Text { text: qsTr("Hasta cada salida"); color: Theme.textDim; font.pixelSize: 11 }
                    Text {
                        visible: (page.mapa.salidas || []).length === 0
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        text: qsTr("Ninguna salida andando: cuando transmitas o grabes, acá aparece cuánto tarda hasta cada una.")
                        color: Theme.textFaint; font.pixelSize: 11
                    }
                    Repeater {
                        model: page.mapa.salidas || []
                        delegate: Rectangle {
                            id: sal
                            required property var modelData
                            Layout.fillWidth: true
                            implicitHeight: col.implicitHeight + 16
                            radius: 10
                            color: page.elegido === modelData.id ? Qt.rgba(1, 0.42, 0.17, 0.14) : Qt.rgba(1, 1, 1, 0.04)
                            border.color: page.elegido === modelData.id ? Theme.accent : Theme.glassBorder
                            Column {
                                id: col
                                anchors { left: parent.left; right: parent.right; top: parent.top; margins: 8 }
                                spacing: 2
                                Text { text: sal.modelData.titulo; color: Theme.text; font.pixelSize: 12; font.weight: Font.DemiBold }
                                Text { text: page.ms(sal.modelData.total_ms); color: Theme.text; font.pixelSize: 24; font.weight: Font.Light }
                                Text {
                                    width: parent.width; wrapMode: Text.WordWrap
                                    text: sal.modelData.fuente === "medido" ? qsTr("medido de punta a punta") : qsTr("declarado (lo que se le fijó al pipeline)")
                                    color: Theme.textFaint; font.pixelSize: 9
                                }
                            }
                            TapHandler { onTapped: page.elegido = page.elegido === sal.modelData.id ? "" : sal.modelData.id }
                        }
                    }
                }
                }
            }
        }

        // --- La línea de tiempo: las tarjetas elegidas, con los eventos abajo --------------------
        Rectangle {
            visible: page.pestana === "mapa"
            Layout.fillWidth: true
            Layout.preferredHeight: page.serie.length === 0 ? 46 : (page.modoLinea === "apiladas" ? Math.min(360, 80 + 64 * page.serie.length) : 220)
            Behavior on Layout.preferredHeight { NumberAnimation { duration: 220; easing.type: Easing.OutCubic } }
            radius: 14
            color: Theme.glassTint
            border.color: Theme.glassBorder
            ColumnLayout {
                anchors { fill: parent; margins: 10 }
                spacing: 6
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 6
                    Text { text: qsTr("Línea de tiempo"); color: Theme.textDim; font.pixelSize: 11 }
                    // Las series elegidas (✕ para sacarla).
                    Repeater {
                        model: page.serie
                        delegate: Rectangle {
                            id: chip
                            required property var modelData
                            required property int index
                            height: 22
                            width: fila.implicitWidth + 14
                            radius: 11
                            color: Qt.rgba(1, 1, 1, 0.06)
                            border.color: page.paleta[index % page.paleta.length]
                            Row {
                                id: fila
                                anchors.centerIn: parent
                                spacing: 5
                                Rectangle { width: 8; height: 8; radius: 4; anchors.verticalCenter: parent.verticalCenter; color: page.paleta[chip.index % page.paleta.length] }
                                Text { text: page.tituloDe(chip.modelData); color: Theme.text; font.pixelSize: 10 }
                                Text {
                                    text: "✕"; color: Theme.textFaint; font.pixelSize: 10
                                    TapHandler { onTapped: page.alternarSerie(chip.modelData) }
                                }
                            }
                        }
                    }
                    Text {
                        visible: page.serie.length === 0
                        text: qsTr("Elegí una tarjeta del mapa y tocá «En la gráfica» para sumarla acá (cada una con su color).")
                        color: Theme.textFaint; font.pixelSize: 11
                    }
                    Item { Layout.fillWidth: true }
                    GlassButton { compact: true; text: qsTr("Superpuestas"); active: page.modoLinea === "superpuestas"; onClicked: page.modoLinea = "superpuestas"; visible: page.serie.length > 0 }
                    GlassButton { compact: true; text: qsTr("Apiladas"); active: page.modoLinea === "apiladas"; onClicked: page.modoLinea = "apiladas"; visible: page.serie.length > 0 }
                    GlassButton {
                        compact: true; visible: page.serie.length > 0
                        text: page.lineaAcumulado ? qsTr("Desde la captura") : qsTr("Lo que agrega")
                        onClicked: page.lineaAcumulado = !page.lineaAcumulado
                        ToolTip.visible: hovered; ToolTip.delay: 300
                        ToolTip.text: qsTr("Lo que agrega cada tarjeta, o todo lo que lleva desde la captura (con la latencia del estudio de referencia)")
                    }
                    Repeater {
                        model: [{ t: "1 min", s: 60 }, { t: "5 min", s: 300 }, { t: "15 min", s: 900 }, { t: "30 min", s: 1800 }]
                        delegate: GlassButton {
                            required property var modelData
                            visible: page.serie.length > 0
                            compact: true
                            text: modelData.t
                            active: page.ventanaS === modelData.s
                            onClicked: page.ventanaS = modelData.s
                        }
                    }
                    GlassButton {
                        compact: true; visible: page.serie.length > 0
                        text: page.pausada ? qsTr("Reanudar") : qsTr("Pausar")
                        icon: page.pausada ? "▶" : "⏸"
                        active: page.pausada
                        onClicked: if (page.pausada) page.pausada = false; else page.pausar()
                    }
                    GlassButton { compact: true; visible: page.serie.length > 0; text: qsTr("Limpiar"); onClicked: { page.tm.linea = []; page.pausada = false } }
                }
                GraficaLatencia {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    visible: page.serie.length > 0
                    series: page.pausada ? page.datosPausa : page.seriesVivas
                    modo: page.modoLinea
                    verAcumulado: page.lineaAcumulado
                    ventanaMs: page.ventanaS * 1000
                    fin: page.pausada ? page.finPausa : (page.vuelta, Date.now())
                    eventos: page.pausada ? page.eventosPausa : (page.tm ? page.tm.eventos : [])
                    referencia: (page.mapa.estudio || {}).latencia_ms || 0
                    onPedirNota: (t) => { nota.t = t; nota.open() }
                }
            }
        }

        // --- Abajo: los análisis ------------------------------------------------------------
        Rectangle {
            visible: page.pestana === "mapa"
            Layout.fillWidth: true
            // Vacío, una línea; con análisis, la tira de tarjetas.
            Layout.preferredHeight: page.tm && page.tm.analisis.length > 0 ? 112 : 40
            radius: 14
            color: Theme.glassTint
            border.color: Theme.glassBorder
            ColumnLayout {
                anchors { fill: parent; margins: 10 }
                spacing: 4
                RowLayout {
                    Layout.fillWidth: true
                    Text { text: qsTr("Análisis"); color: Theme.textDim; font.pixelSize: 11; Layout.fillWidth: true }
                    Text {
                        visible: page.tm && page.tm.analisis.length > 0
                        text: qsTr("borrar el historial"); color: Theme.textFaint; font.pixelSize: 10
                        TapHandler { onTapped: page.tm.borrarAnalisis() }
                    }
                }
                Text {
                    visible: !page.tm || page.tm.analisis.length === 0
                    text: qsTr("Todavía no corriste ninguno: «Analizar 10 s» toma la mediana y el p95 de cada tramo y queda acá, en el historial del evento.")
                    color: Theme.textFaint; font.pixelSize: 11
                }
                ListView {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    orientation: ListView.Horizontal
                    spacing: 8
                    clip: true
                    model: page.tm ? page.tm.analisis : []
                    delegate: Rectangle {
                        id: an
                        required property var modelData
                        width: 260
                        height: ListView.view.height
                        radius: 10
                        color: Qt.rgba(1, 1, 1, 0.04)
                        border.color: modelData.estado === "fallo" ? Theme.live : modelData.estado === "midiendo" ? Theme.amber : Theme.glassBorder
                        Column {
                            anchors { fill: parent; margins: 8 }
                            spacing: 2
                            Row {
                                spacing: 6
                                StatusPill {
                                    text: an.modelData.estado === "midiendo" ? qsTr("midiendo") : an.modelData.estado === "listo" ? qsTr("listo") : qsTr("falló")
                                    dot: an.modelData.estado === "midiendo" ? Theme.amber : an.modelData.estado === "listo" ? Theme.ok : Theme.live
                                    pulse: an.modelData.estado === "midiendo"
                                    pixelSize: 10
                                }
                                Text {
                                    anchors.verticalCenter: parent.verticalCenter
                                    text: "#" + an.modelData.id + " · " + an.modelData.segundos + " s · " + page.hace(an.modelData.desde_ms)
                                    color: Theme.textDim; font.pixelSize: 10
                                }
                            }
                            Rectangle {
                                visible: an.modelData.estado === "midiendo"
                                width: parent.width; height: 3; radius: 1.5; color: Qt.rgba(1, 1, 1, 0.08)
                                Rectangle { width: parent.width * (an.modelData.progreso || 0); height: 3; radius: 1.5; color: Theme.amber }
                            }
                            Text {
                                visible: an.modelData.estado === "fallo"
                                width: parent.width; wrapMode: Text.WordWrap
                                text: an.modelData.error || ""; color: Theme.live; font.pixelSize: 10
                            }
                            Repeater {
                                model: an.modelData.salidas || []
                                delegate: Text {
                                    required property var modelData
                                    text: modelData.titulo + "  " + page.ms(modelData.total_ms) + "  (p95 " + page.ms(modelData.p95_ms) + ")"
                                    color: Theme.text; font.pixelSize: 11
                                }
                            }
                            Text {
                                visible: an.modelData.estado === "listo" && (an.modelData.salidas || []).length === 0
                                text: qsTr("%n tramo medido (sin salidas andando)", "", Object.keys(an.modelData.nodos || {}).length)
                                color: Theme.textDim; font.pixelSize: 10
                            }
                        }
                    }
                }
            }
        }
    }

    // Marcar un momento de la línea de tiempo (queda como evento «nota»; también por MCP).
    Popup {
        id: nota
        property real t: 0
        anchors.centerIn: parent
        width: 380
        modal: true
        padding: 16
        onOpened: { campoNota.text = ""; campoNota.forceActiveFocus() }
        background: Rectangle { radius: 14; color: Theme.panel; border.color: Theme.glassBorder }
        contentItem: ColumnLayout {
            spacing: 10
            Text {
                text: qsTr("Marcar %1").arg(nota.t > 0 ? new Date(nota.t).toLocaleTimeString(Qt.locale(), "HH:mm:ss") : "")
                color: Theme.text; font.pixelSize: 14; font.weight: Font.DemiBold
            }
            TextField {
                id: campoNota
                Layout.fillWidth: true
                placeholderText: qsTr("Qué pasó (p. ej. «se movió el cable de la cámara 2»)")
                color: Theme.text
                onAccepted: guardarNota.clicked()
            }
            RowLayout {
                Item { Layout.fillWidth: true }
                GlassButton { compact: true; text: qsTr("Cancelar"); onClicked: nota.close() }
                GlassButton {
                    id: guardarNota
                    compact: true; text: qsTr("Marcar"); active: true
                    onClicked: {
                        page.tm.agregarEventoUi(nota.t, campoNota.text.trim())
                        if (page.pausada) page.eventosPausa = page.tm.eventos
                        nota.close()
                    }
                }
            }
        }
    }
}
