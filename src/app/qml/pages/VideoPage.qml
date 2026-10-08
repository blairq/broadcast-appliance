// Página «Video»: el grafo de tarjetas de cada entrada de video
// (Studio.video, video/VideoGrafos), como el patchbay del audio. La entrada
// (la cámara ya encuadrada por el PTZ) pasa por tarjetas — color, croma,
// recorte y espejo — hasta la escena; una tarjeta «Salida» publica ese punto
// como una fuente con nombre para cualquier escena. Lo corre la GPU.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import BA
import "../controls"

Rectangle {
    id: page
    color: Theme.bg
    property real leftInset: 92
    readonly property var vg: Studio.video
    readonly property var g: vg ? vg.grafo : ({})
    readonly property var est: vg && page.vista === "estudio" ? vg.estudio : ({})
    property string sel: ""
    // Dos vistas, como el audio: el rack de UNA entrada, o todo el estudio
    // (cada entrada en su franja y los cables que cruzan entre ellas).
    readonly property string vista: vg ? vg.vista : "racks"
    property string msg: ""
    // Lo que se edita: un controlador, el molde de su tarjeta; una tarjeta, con lo que corre.
    readonly property string tipoEf: page.nodo ? (page.nodo.tipo === "control" ? page.nodo.params.tarjeta : page.nodo.tipo) : ""
    readonly property var paramsEf: page.nodo ? (page.nodo.tipo === "control" ? page.nodo.params.params : (page.nodo.efectivos || page.nodo.params)) : ({})
    // Las tarjetas que maneja el controlador elegido: [{id, grupo}].
    readonly property var manejadas: {
        if (!page.nodo || page.nodo.tipo !== "control" || !page.vg) return []
        const e = page.vg.estudio
        const out = []
        for (const c of (e.cables || []))
            if (c.desde === page.nodo.id && c.salida === "ctl")
                for (const n of (e.nodos || [])) if (n.id === c.hacia) out.push({ id: n.id, grupo: n.grupo })
        return out
    }
    readonly property var nodo: {
        const l = (page.vista === "estudio" ? page.est.nodos : page.g.nodos) || []
        for (let i = 0; i < l.length; ++i) if (l[i].id === page.sel) return l[i]
        return null
    }

    // La imagen en vivo de la entrada elegida (con su grafo), solo mientras se ve.
    property string mirando: ""
    function mirar() {
        const quiero = page.visible && page.vg ? page.vg.entrada : ""
        if (quiero === page.mirando) return
        if (page.mirando !== "") Studio.frames.mirarEntrada(page.mirando, false)
        if (quiero !== "") Studio.frames.mirarEntrada(quiero, true)
        page.mirando = quiero
    }
    onVisibleChanged: mirar()
    Connections { target: page.vg; function onCambio() { page.mirar() } }
    Component.onCompleted: mirar()
    Component.onDestruction: if (page.mirando !== "") Studio.frames.mirarEntrada(page.mirando, false)

    function colorDe(tipo) {
        if (tipo === "entrada" || tipo === "escena") return "#64748b"
        if (tipo === "obs") return "#0ea5e9"
        if (tipo === "gst") return "#10b981"
        if (tipo === "control") return "#fbbf24"
        if (tipo === "compositor") return "#f472b6"
        if (tipo === "croma") return "#22c55e"
        if (tipo === "color") return "#f59e0b"
        if (tipo === "recorte") return "#38bdf8"
        if (tipo === "vista360" || tipo === "panoramica") return "#818cf8"
        if (tipo === "salida") return "#a78bfa"
        if (tipo === "retardo") return "#94a3b8"
        if (tipo === "rostros") return "#f43f5e"
        if (tipo === "ptz") return "#06b6d4"
        if (tipo === "seguir") return "#e879f9"
        return Theme.accent
    }
    function f2(v) { return Number(v).toFixed(2) }
    function hex(n) { return "#" + ("000000" + Number(n).toString(16)).slice(-6).toUpperCase() }
    function resumen(n) {
        const p = n.params || {}
        if (n.error) return "⚠ " + n.error
        if (n.tipo === "entrada") return "La cámara (ya encuadrada por el PTZ)"
        if (n.tipo === "escena") return "Lo que ve la escena"
        if (n.tipo === "obs") return "Filtro de OBS · " + (p.filtro || "")
        if (n.tipo === "control") return "Controlador · " + (p.tarjeta === "gst" ? (p.params || {}).elemento : p.tarjeta === "obs" ? (p.params || {}).filtro : p.tarjeta)
        if (n.controlador) return "↳ del controlador" + (Object.keys(p.local || {}).length ? " · con ajuste propio" : "")
        if (n.tipo === "gst") {
            const pr = p.propiedades || {}
            const ks = Object.keys(pr)
            return "GStreamer · " + (p.elemento || "") + (ks.length ? " · " + ks.slice(0, 2).map(k => k + "=" + (typeof pr[k] === "number" ? Number(pr[k]).toFixed(2).replace(/\.?0+$/, "") : pr[k])).join(" ") : "")
        }
        if (n.tipo === "compositor") return ((n.entradas || []).length - 1) + " entradas · " + (p.ancho > 0 ? p.ancho + "×" + p.alto : "lienzo del estudio") + (n.publicada ? " · en escena" : "")
        if (n.tipo === "color") return "brillo " + f2(p.brillo) + " · contraste " + f2(p.contraste) + " · saturación " + f2(p.saturacion)
        if (n.tipo === "croma") return "fondo " + hex(p.clave) + " · similitud " + f2(p.similitud)
        if (n.tipo === "recorte") {
            const partes = []
            if (p.izquierda > 0 || p.arriba > 0 || p.derecha > 0 || p.abajo > 0) partes.push("bordes recortados")
            if (p.espejoH) partes.push("espejo ↔")
            if (p.espejoV) partes.push("espejo ↕")
            return partes.length ? partes.join(" · ") : "sin cambios"
        }
        if (n.tipo === "vista360" || n.tipo === "panoramica") {
            if (p.ver === "entrada") return "calibrando: la entrada con los círculos"
            if (p.ver === "tira") return "la tira" + (p.ancho > 0 ? " " + p.ancho + "×" + p.alto : " (la suma de las cámaras)") + (n.publicada ? " · en escena" : "")
            if (p.ver === "panorama") return "el panorama 360" + (p.guia && p.guia !== "ninguna" ? " · guía " + p.guia : "")
            return (n.tipo === "panoramica" ? (((n.entradas || []).filter(e => !e.pedido).length) + " cámaras · ") : "") + "mira " + Math.round(p.yaw || 0) + "° · " + Math.round(p.pitch || 0) + "° · abre " + Math.round(p.fov || 90) + "°" + (n.publicada ? " · en escena" : "")
        }
        if (n.tipo === "salida") return "«" + (p.nombre || "") + "»" + (n.publicada ? " · en escena" : "")
        if (n.tipo === "retardo") return (p.cuadros || 0) + " cuadros de atraso"
        if (n.tipo === "ptz") return p.activo === false ? "Apagado" : "Pan, tilt y zoom digital (Cámaras PTZ)"
        if (n.tipo === "seguir") return p.activo === false ? "Apagado" : (p.objetivo ? "Sigue: " + p.objetivo : "Sin nadie elegido")
        if (n.tipo === "rostros") return (({ mirar: "Personas mira", autoalta: "Personas anota sola", puerta: "Puerta: acredita", seguir: "Sigue a una cara" })[p.uso] || "Caras") +
                                          (p.marcar ? " · " + (p.estilo || "recuadro") : "") + (p.texto ? " · «" + p.texto + "»" : "")
        return ""
    }
    // Los nodos de afuera (otra entrada, el estudio) en una columna a la
    // derecha: su posición es la de su propia vista.
    // EN «RACKS», LAS TARJETAS DE LA ENTRADA EMPIEZAN ARRIBA A LA IZQUIERDA. Las
    // posiciones guardadas son las de la vista Estudio (cada entrada en su franja):
    // una entrada nueva quedaba en y=4272 y en Racks no se veía ninguna tarjeta.
    // Se corren al mostrarlas y al moverlas se devuelve la posición de verdad.
    readonly property point corrimiento: {
        const l = page.g.nodos || []
        let minX = Infinity, minY = Infinity
        for (const n of l) if (!n.externo) { minX = Math.min(minX, n.x); minY = Math.min(minY, n.y) }
        return minX === Infinity ? Qt.point(0, 0) : Qt.point(minX - 20, minY - 20)
    }
    // ui_show_page {page: "video", seccion: "<entrada>[/<id de tarjeta>]"}: esa entrada (y esa tarjeta).
    Connections {
        target: Studio
        function onMostrarSeccion(pagina, seccion) {
            if (pagina !== "video" || !page.vg || seccion === "") return
            const partes = seccion.split("/")
            page.vg.entrada = partes[0]
            if (partes.length > 1) page.elegir(partes.slice(1).join("/"))
        }
    }

    // LOS EVENTOS ENTRE TARJETAS (ba/rostros, ba/ptz, ba/encuadre, ba/control…):
    // lo que salió de cada una en los últimos 2 s, ↓ hacia abajo y ↑ hacia arriba.
    property var eventosVivos: ({})
    Timer {
        interval: 1000; repeat: true; triggeredOnStart: true
        running: page.visible && page.vista !== "estudio" && !!page.vg && page.vg.entrada !== ""
        onTriggered: page.eventosVivos = page.vg.eventosDeEntrada()
    }
    function eventosTexto(id) {
        const l = (page.eventosVivos || {})[id] || []
        return l.filter(e => e.hace_ms < 2000).map(e => (e.direccion === "arriba" ? "↑ " : "↓ ") + e.evento).join("  ")
    }
    readonly property var nodosVista: {
        const l = page.g.nodos || []
        const dx = page.corrimiento.x, dy = page.corrimiento.y
        let maxX = 0
        for (const n of l) if (!n.externo) maxX = Math.max(maxX, n.x - dx)
        const out = []
        let yExt = 20
        for (let i = 0; i < l.length; ++i) {
            const ev = page.eventosTexto(l[i].id)
            const n = Object.assign({}, l[i], { resumen: page.resumen(l[i]) + (ev ? "\n" + ev : "") })
            if (n.externo) { n.x = maxX + 260; n.y = yExt; yExt += 140 }
            else { n.x = n.x - dx; n.y = n.y - dy }
            out.push(n)
        }
        return out
    }
    // La vista Estudio: todos los nodos, cada entrada en su franja.
    readonly property var nodosEstudio: {
        const l = page.est.nodos || []
        const out = []
        // Un margen arriba: el nombre de la primera franja tiene que entrar.
        for (let i = 0; i < l.length; ++i) out.push(Object.assign({}, l[i], { resumen: page.resumen(l[i]), externo: false, y: l[i].y + 30 }))
        return out
    }
    readonly property var franjasEstudio: {
        const l = page.est.nodos || []
        const porGrupo = {}
        for (const n of l) {
            const k = n.grupo || ""
            const alto = 34 + Math.max((n.entradas || []).length, (n.salidas || []).length, 1) * 24 + 40
            if (!porGrupo[k]) porGrupo[k] = { min: n.y, max: n.y + alto }
            porGrupo[k].min = Math.min(porGrupo[k].min, n.y)
            porGrupo[k].max = Math.max(porGrupo[k].max, n.y + alto)
        }
        const out = []
        for (const k in porGrupo)
            if (k !== "") out.push({ titulo: k, y: porGrupo[k].min + 30 - 26, alto: porGrupo[k].max - porGrupo[k].min + 40 })
        return out
    }
    function elegir(id) {
        page.sel = id
        // En el estudio, elegir una tarjeta elige su entrada (la vista previa
        // y las tarjetas nuevas van a esa).
        if (page.vista !== "estudio") return
        const l = page.est.nodos || []
        for (const n of l) if (n.id === id && n.grupo && page.vg.entrada !== n.grupo) page.vg.entrada = n.grupo
    }
    // El árbol de entradas, agrupado como el de las fuentes del audio.
    property var plegados: ({})
    property int plegTick: 0
    readonly property var arbol: {
        plegTick
        const info = page.vg ? page.vg.entradasInfo : []
        const orden = ["Cámaras", "Red", "NDI", "Virtuales"]
        const porGrupo = {}
        for (const e of info) { (porGrupo[e.grupo] = porGrupo[e.grupo] || []).push(e) }
        for (const k in porGrupo) if (orden.indexOf(k) < 0) orden.push(k)
        const filas = []
        for (const k of orden) {
            if (!porGrupo[k]) continue
            filas.push({ fila: "grupo", grupo: k, cuantas: porGrupo[k].length })
            if (!page.plegados[k]) for (const e of porGrupo[k]) filas.push({ fila: "entrada", e: e })
        }
        return filas
    }
    function avisar(e) { page.msg = e || "" }
    // Los deslizadores aplican mientras se mueven, sin pedir un cambio por píxel.
    property var pendiente: null
    Timer { id: aplicar; interval: 80; onTriggered: if (page.pendiente) { page.avisar(page.vg.ajustar(page.pendiente.id, page.pendiente.p)); page.pendiente = null } }
    property var pendienteFiltro: null
    Timer { id: aplicarFiltro; interval: 80; onTriggered: if (page.pendienteFiltro) { const p = page.pendienteFiltro; page.pendienteFiltro = null; page.avisar(page.vg.ajustarFiltro(p.id, p.k, p.v)) } }
    function ajustarFiltro(id, clave, valor) {
        page.pendienteFiltro = { id: id, k: clave, v: valor }
        aplicarFiltro.restart()
    }
    function ajustar(id, clave, valor) {
        const p = page.pendiente && page.pendiente.id === id ? page.pendiente.p : {}
        p[clave] = valor
        page.pendiente = { id: id, p: p }
        aplicar.restart()
    }

    RowLayout {
        anchors { fill: parent; leftMargin: page.leftInset; margins: 20 }
        spacing: 16

        // --- Las entradas --------------------------------------------------------------
        Panel {
            Layout.preferredWidth: 230
            Layout.fillHeight: true
            title: "Entradas de video"
            GlassButton {
                id: botonVirtual
                anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
                compact: true
                text: "+ Entrada virtual…"
                onClicked: virtualPop.open()
                ToolTip.visible: hovered; ToolTip.delay: 400
                ToolTip.text: "Señal de ajuste (barras), negro, color, o cualquier origen de GStreamer"
            }
            ListView {
                anchors { left: parent.left; right: parent.right; top: parent.top; bottom: botonVirtual.top; bottomMargin: 8 }
                clip: true
                spacing: 6
                model: page.arbol
                delegate: Item {
                    required property var modelData
                    width: ListView.view.width
                    height: modelData.fila === "grupo" ? 26 : 50
                    // Encabezado de grupo (se pliega).
                    RowLayout {
                        visible: modelData.fila === "grupo"
                        anchors { fill: parent; leftMargin: 2; rightMargin: 4 }
                        spacing: 6
                        Text { text: page.plegados[modelData.grupo] ? "▸" : "▾"; color: Theme.textFaint; font.pixelSize: 11 }
                        Text {
                            Layout.fillWidth: true
                            text: modelData.grupo || ""
                            color: Theme.textDim
                            font { pixelSize: 11; weight: Font.DemiBold; capitalization: Font.AllUppercase; letterSpacing: 0.4 }
                        }
                        Text { text: modelData.cuantas || 0; color: Theme.textFaint; font.pixelSize: 11 }
                    }
                    MouseArea {
                        visible: modelData.fila === "grupo"
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: { page.plegados[modelData.grupo] = !page.plegados[modelData.grupo]; page.plegTick++ }
                    }
                    // Una entrada.
                    Rectangle {
                        visible: modelData.fila === "entrada"
                        anchors { fill: parent; leftMargin: 12 }
                        radius: 12
                        readonly property var e: modelData.e || ({})
                        color: page.vg && page.vg.entrada === e.nombre ? Qt.rgba(1, 1, 1, 0.12) : Qt.rgba(1, 1, 1, 0.04)
                        border.color: page.vg && page.vg.entrada === e.nombre ? Theme.accent : "transparent"
                        RowLayout {
                            anchors { fill: parent; margins: 10 }
                            Text { text: parent.parent.e.nombre || ""; color: Theme.text; font.pixelSize: 13; elide: Text.ElideRight; Layout.fillWidth: true }
                            Text { visible: (parent.parent.e.tarjetas || 0) > 0; text: "⧈ " + parent.parent.e.tarjetas; color: Theme.accent; font.pixelSize: 12 }
                        }
                        MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: { page.vg.entrada = parent.e.nombre; page.sel = "" } }
                    }
                }
                Text {
                    visible: parent.count === 0
                    anchors.centerIn: parent
                    width: parent.width - 20
                    wrapMode: Text.WordWrap
                    horizontalAlignment: Text.AlignHCenter
                    text: "No hay entradas de video. Se agregan en «Entradas»."
                    color: Theme.textFaint; font.pixelSize: 12
                }
            }
        }

        // --- El grafo -----------------------------------------------------------------
        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 10
            // Misma barra que el audio: qué veo · qué agrego · cómo lo miro · ⋯
            BarraGrafo {
                Layout.fillWidth: true
                titulo: page.vista === "estudio" ? "Estudio" : (page.vg ? page.vg.entrada : "")
                vistas: [
                    { id: "racks", t: "Racks", tip: "El pipeline de la entrada elegida" },
                    { id: "estudio", t: "Estudio", icon: "▦", tip: "Todas las entradas, cada una en su franja, y los cables entre ellas" }
                ]
                vista: page.vista
                onElegirVista: (id) => page.vg.vista = id
                puedeAgregar: !!page.vg && page.vg.entrada !== ""
                porQueNoAgregar: "Elegí una entrada a la izquierda"
                agregar: [
                    { titulo: "Tarjetas" + (page.nodo && page.nodo.tipo !== "escena" && !page.nodo.externo ? " (van después de «" + (page.nodo.etiqueta || page.sel) + "»)" : ""),
                      items: (page.vg ? page.vg.catalogo.filter(c => c.tipo !== "control" && c.tipo !== "ptz") : []).map(c => ({
                          t: c.titulo, tip: c.descripcion,
                          f: () => page.avisar(page.vg.agregar(c.tipo, page.nodo && page.nodo.tipo !== "escena" && !page.nodo.externo ? page.sel : ""))
                      })) },
                    { titulo: "Todos los módulos",
                      items: [{ t: "Buscar módulo…", tip: "Cualquier módulo de video de GStreamer o filtro de OBS como tarjeta, con su ayuda: color, llaves, desentrelazado, textos y reloj, nitidez, efectos, OpenGL…", f: () => catalogo.open() }] }
                ]
                acciones: [
                    { t: "Sincronía y red…", icon: "⏱", f: () => sincronia.open(),
                      tip: "Reloj del estudio, entradas de red (SRT, RTMP, RTSP) con su diagnóstico, medir y corregir la sincronía entre entradas" },
                    { t: "Superresolución…", icon: "✦", f: () => superresPop.open(),
                      tip: "Duplicar la resolución de las cámaras de red (Apple, FSR, redes ONNX) y entrenar una red con lo que ve una cámara" },
                    { t: "Scopes…", icon: "◫", f: () => scopesPop.open(),
                      tip: "Forma de onda, RGB parade, vectorscopio e histograma del programa o de la entrada, y el legalizador para TV" },
                    { t: "Programador de superresolución…", icon: "⚙", f: () => programadorPop.open(),
                      tip: "Qué cámaras usan la red según el programa y el preview, cuánto aguanta cada acelerador y cómo entra la mejora" }
                ]
                mas: page.g.trivial === false ? [{
                    t: "Volver a como vino", peligro: true, tip: "Saca todas las tarjetas de esta entrada",
                    f: () => ApplicationWindow.window.confirm({
                        title: "¿Sacar todas las tarjetas de «" + page.vg.entrada + "»?",
                        text: "La entrada vuelve a ir directo a la escena. Las salidas que estén en escenas quedan en negro.",
                        accept: "Sacar todo", reject: "Cancelar",
                        action: function() { page.avisar(page.vg.limpiar()); page.sel = "" }
                    })
                }] : []
                lienzo: grafo
            }
            Patchbay {
                id: grafo
                Layout.fillWidth: true
                Layout.fillHeight: true
                nodos: page.vista === "estudio" ? page.nodosEstudio : page.nodosVista
                cables: (page.vista === "estudio" ? page.est.cables : page.g.cables) || []
                franjas: page.vista === "estudio" ? page.franjasEstudio : []
                seleccionado: page.sel
                colorDe: page.colorDe
                onElegir: (id) => page.elegir(id)
                onMover: (id, x, y) => page.avisar(page.vista === "estudio" ? page.vg.mover(id, x, y - 30)
                                                   : page.vg.mover(id, x + page.corrimiento.x, y + page.corrimiento.y))
                onConectar: (desde, salida, hacia, entrada) => page.avisar(page.vg.conectar(desde, salida, hacia, entrada))
                onDesconectar: (desde, salida, hacia, entrada) => page.avisar(page.vg.desconectar(desde, salida, hacia, entrada))
                onOrdenar: page.avisar(page.vg.ordenar())
            }
            Text {
                visible: page.msg !== ""
                Layout.fillWidth: true
                text: page.msg; color: Theme.warn; font.pixelSize: 12; wrapMode: Text.WordWrap
            }
        }

        // --- Vista previa y la tarjeta elegida ------------------------------------------
        // Se arrastra para darle más lugar a las tarjetas o al panel (se recuerda).
        Divisor { id: divisorPanel; clave: "video.panel"; ancho: 320; minimo: 240; maximo: 720 }
        Panel {
            Layout.preferredWidth: divisorPanel.ancho
            Layout.fillHeight: true
            title: page.nodo ? page.nodo.etiqueta : "Vista previa"
            ColumnLayout {
                anchors.fill: parent
                spacing: 10
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: width * 9 / 16
                    radius: 8; color: "black"; clip: true
                    // Damero: lo transparente (croma, recorte) se ve como tal.
                    Grid {
                        id: damero
                        anchors.fill: parent
                        columns: Math.ceil(width / 12)
                        Repeater {
                            model: damero.columns * Math.ceil(damero.height / 12)
                            delegate: Rectangle {
                                required property int index
                                width: 12; height: 12
                                // Por el id y no por `parent`: un delegado recién creado todavía no lo tiene.
                                color: (Math.floor(index / damero.columns) + index) % 2 ? "#2a2a2a" : "#1a1a1a"
                            }
                        }
                    }
                    VideoItem {
                        id: vistaPrevia
                        anchors.fill: parent
                        source: page.vg && page.vg.entrada !== "" ? "entrada:" + page.vg.entrada : ""
                        fillMode: VideoItem.Fit
                    }
                    // Con la tarjeta Seguimiento elegida: un clic en la imagen sigue a esa cara.
                    MouseArea {
                        anchors.fill: parent
                        visible: page.tipoEf === "seguir" && !!page.vg && page.vg.entrada !== ""
                        cursorShape: Qt.CrossCursor
                        onClicked: (m) => {
                            const asp = vistaPrevia.frameSize.width > 0 ? vistaPrevia.frameSize.width / vistaPrevia.frameSize.height : 16 / 9
                            const iw = Math.min(width, height * asp), ih = iw / asp
                            const u = Math.max(0, Math.min(1, (m.x - (width - iw) / 2) / iw))
                            const v = Math.max(0, Math.min(1, (m.y - (height - ih) / 2) / ih))
                            page.avisar(Studio.seguidor ? Studio.seguidor.seguirPunto(page.vg.entrada, u, v) : "El seguimiento no está disponible")
                        }
                    }
                    Text {
                        visible: !vistaPrevia.hasFrame
                        anchors.centerIn: parent
                        text: "Sin señal"; color: Theme.textFaint; font.pixelSize: 12
                    }
                }
                Text {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    text: "Lo que ve la escena, con las tarjetas."
                    color: Theme.textFaint; font.pixelSize: 11
                }
                // Si se cae la señal estando al aire: avisar, armar o cortar a una escena de respaldo.
                RowLayout {
                    id: respaldo
                    visible: !!page.vg && page.vg.entrada !== "" && !page.nodo
                    Layout.fillWidth: true
                    spacing: 4
                    property var r: page.vg && page.vg.entrada !== "" ? Studio.respaldoEntrada(page.vg.entrada) : ({})
                    Text { text: "Si se cae"; color: Theme.textDim; font.pixelSize: 11 }
                    ComboBox {
                        id: modoResp
                        implicitWidth: 90
                        model: [ { t: "Avisar", v: "avisar" }, { t: "Armar", v: "armar" }, { t: "Cortar", v: "cortar" } ]
                        textRole: "t"
                        currentIndex: Math.max(0, model.findIndex(o => o.v === (respaldo.r.al_perder_aire || "avisar")))
                        onActivated: (i) => { const e = Studio.setRespaldoEntrada(page.vg.entrada, { escena: escResp.currentText, al_perder_aire: model[i].v, ms: 1000 })
                                              if (e !== "") Studio.notify(e, "error") }
                    }
                    ComboBox {
                        id: escResp
                        Layout.fillWidth: true
                        model: page.vg ? page.vg.escenasUi() : []
                        currentIndex: Math.max(0, model.indexOf(respaldo.r.escena || ""))
                        onActivated: (i) => Studio.setRespaldoEntrada(page.vg.entrada, { escena: model[i], al_perder_aire: modoResp.model[modoResp.currentIndex].v, ms: 1000 })
                        ToolTip.visible: hovered; ToolTip.delay: 400; ToolTip.text: "La escena de respaldo si esta entrada se queda sin señal al aire"
                    }
                }

                ScrollView {
                    id: ajustes
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    contentWidth: availableWidth
                    ColumnLayout {
                        width: ajustes.availableWidth
                        spacing: 8

                        Text {
                            visible: !page.nodo
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                            text: "Elegí una tarjeta para ajustarla. Las tarjetas nuevas van después de la elegida; un cable se tira desde el punto verde de la derecha."
                            color: Theme.textDim; font.pixelSize: 12
                        }
                        Text {
                            visible: !!page.nodo && (page.nodo.tipo === "entrada" || page.nodo.tipo === "escena" || page.nodo.tipo === "compositor")
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                            text: !page.nodo ? ""
                                  : page.nodo.tipo === "entrada" ? "La cámara tal como sale del PTZ (Mejorar imagen y el encuadre digital, en «Cámaras PTZ»). De su salida pueden salir varios cables, también hacia otras entradas o un compositor."
                                  : page.nodo.tipo === "escena" ? "Lo que llega acá es lo que ven las escenas que usan esta entrada. Puede venir de otra parte del estudio (un compositor). Sin nada conectado, no se ve."
                                  : "Del estudio: junta varias entradas en un lienzo. Cada cable a su entrada «+» crea una capa nueva; sin posiciones, van en mosaico. Se pone en las escenas como una fuente, o se cablea a la escena de una entrada."
                            color: Theme.textDim; font.pixelSize: 12
                        }

                        // Controladores: la misma tarjeta (con los mismos ajustes) en varias entradas.
                        Rectangle {
                            visible: !!page.nodo && (!!page.nodo.controlador || page.nodo.tipo === "control")
                            Layout.fillWidth: true
                            implicitHeight: ctlCol.implicitHeight + 16
                            radius: 8
                            color: Qt.rgba(0.98, 0.75, 0.14, 0.08)
                            border.color: Qt.rgba(0.98, 0.75, 0.14, 0.45)
                            ColumnLayout {
                                id: ctlCol
                                anchors { left: parent.left; right: parent.right; top: parent.top; margins: 8 }
                                spacing: 6
                                Text {
                                    Layout.fillWidth: true; wrapMode: Text.WordWrap
                                    color: Theme.text; font.pixelSize: 12
                                    text: !page.nodo ? ""
                                          : page.nodo.tipo === "control"
                                            ? "Maneja " + page.manejadas.length + (page.manejadas.length === 1 ? " tarjeta" : " tarjetas") + ": lo que ajustes acá cambia en todas."
                                            : "La maneja un controlador: lo que ajustes acá cambia en todas las entradas que maneja."
                                }
                                Flow {
                                    visible: !!page.nodo && page.nodo.tipo === "control"
                                    Layout.fillWidth: true
                                    spacing: 4
                                    Repeater {
                                        model: page.manejadas
                                        delegate: GlassButton {
                                            required property var modelData
                                            compact: true; text: modelData.grupo
                                            onClicked: { page.vg.entrada = modelData.grupo; page.sel = modelData.id }
                                        }
                                    }
                                }
                                Text {
                                    visible: !!page.nodo && page.nodo.tipo === "control"
                                    text: "Agregarla también a:"; color: Theme.textDim; font.pixelSize: 11
                                }
                                Flow {
                                    visible: !!page.nodo && page.nodo.tipo === "control"
                                    Layout.fillWidth: true
                                    spacing: 4
                                    Repeater {
                                        model: page.vg ? page.vg.entradas.filter(e => !page.manejadas.some(m => m.grupo === e)) : []
                                        delegate: GlassButton {
                                            required property string modelData
                                            compact: true; text: "+ " + modelData
                                            onClicked: page.avisar(page.vg.agregarControlada(page.sel, modelData))
                                        }
                                    }
                                }
                                GlassButton {
                                    visible: !!page.nodo && !!page.nodo.controlador
                                    compact: true
                                    text: "Soltar del controlador"
                                    onClicked: page.avisar(page.vg.soltarControl(page.sel))
                                    ToolTip.visible: hovered; ToolTip.delay: 400
                                    ToolTip.text: "Esta tarjeta deja de seguir al controlador y se queda con los valores de ahora"
                                }
                            }
                        }
                        GlassButton {
                            visible: !!page.nodo && !page.nodo.controlador && ["color", "croma", "recorte", "obs", "gst"].indexOf(page.nodo.tipo) >= 0 && !page.nodo.externo
                            compact: true
                            text: "Usar en varias entradas…"
                            icon: "⇶"
                            onClicked: {
                                const e = page.vg.controlar(page.sel)
                                page.avisar(e)
                            }
                            ToolTip.visible: hovered; ToolTip.delay: 400
                            ToolTip.text: "Hace un controlador con esta tarjeta: después se la agrega a otras entradas (otras cámaras) y un solo ajuste las maneja a todas"
                        }

                        // Un módulo de GStreamer: qué es, y su ayuda completa.
                        RowLayout {
                            visible: page.tipoEf === "gst"
                            Layout.fillWidth: true
                            spacing: 8
                            Text {
                                Layout.fillWidth: true
                                text: page.paramsEf ? page.paramsEf.elemento || "" : ""
                                color: Theme.textFaint; font { pixelSize: 11; family: "Menlo" }
                            }
                            GlassButton {
                                compact: true; text: "Ayuda"; icon: "?"
                                onClicked: catalogo.abrirEn(page.paramsEf.elemento)
                            }
                        }

                        // Deslizadores de los parámetros numéricos.
                        Repeater {
                            model: {
                                if (!page.nodo) return []
                                const t = page.tipoEf
                                if (t === "color") return [
                                    { k: "brillo", t: "Brillo", min: -1, max: 1 },
                                    { k: "contraste", t: "Contraste", min: 0, max: 2 },
                                    { k: "saturacion", t: "Saturación", min: 0, max: 2 },
                                    { k: "gamma", t: "Gamma", min: 0.2, max: 3 }]
                                if (t === "croma") return [
                                    { k: "similitud", t: "Similitud (cuánto se va)", min: 0, max: 1 },
                                    { k: "suavidad", t: "Suavidad del borde", min: 0, max: 1 },
                                    { k: "derrame", t: "Quitar el reflejo de color", min: 0, max: 1 }]
                                if (t === "rostros") return [
                                    { k: "fps", t: "Análisis por segundo", min: 1, max: 15, entero: true },
                                    { k: "confianza", t: "Confianza mínima", min: 0.3, max: 0.95 },
                                    { k: "grosor", t: "Grosor del borde", min: 1, max: 20, entero: true },
                                    { k: "texto_tamano", t: "Tamaño del texto", min: 0.015, max: 0.1 }]
                                if (t === "seguir") return [
                                    { k: "zoom", t: "Zoom que busca (0 = no tocarlo)", min: 0, max: 4 },
                                    { k: "tamano_cara", t: "Tamaño de la cara (0 = no ajustar el zoom)", min: 0, max: 0.6 },
                                    { k: "aire", t: "Aire arriba de la cabeza", min: -0.2, max: 0.35 },
                                    { k: "suavidad", t: "Suavidad del movimiento (s)", min: 0.1, max: 3 },
                                    { k: "zona_muerta", t: "Zona muerta (no tiembla)", min: 0, max: 0.2 },
                                    { k: "perdida_s", t: "Espera si la pierde (s)", min: 0, max: 15 }]
                                if (t === "retardo") return [
                                    { k: "cuadros", t: "Cuadros de atraso (para sincronizar)", min: 0, max: 30, entero: true }]
                                if (t === "recorte") return [
                                    { k: "izquierda", t: "Izquierda", min: 0, max: 0.5 },
                                    { k: "derecha", t: "Derecha", min: 0, max: 0.5 },
                                    { k: "arriba", t: "Arriba", min: 0, max: 0.5 },
                                    { k: "abajo", t: "Abajo", min: 0, max: 0.5 }]
                                return []
                            }
                            delegate: ColumnLayout {
                                required property var modelData
                                Layout.fillWidth: true
                                spacing: 0
                                RowLayout {
                                    Layout.fillWidth: true
                                    Text { text: modelData.t; color: Theme.textDim; font.pixelSize: 12; Layout.fillWidth: true }
                                    Text { text: modelData.entero ? Math.round(desl.value) : page.f2(desl.value); color: Theme.textFaint; font.pixelSize: 11 }
                                }
                                Slider {
                                    id: desl
                                    Layout.fillWidth: true
                                    from: modelData.min; to: modelData.max
                                    stepSize: modelData.entero ? 1 : 0
                                    value: page.nodo && page.paramsEf && typeof page.paramsEf[modelData.k] === "number" ? page.paramsEf[modelData.k] : 0
                                    onMoved: page.ajustar(page.sel, modelData.k, modelData.entero ? Math.round(value) : value)
                                }
                            }
                        }

                        // Una tarjeta de OBS: sus controles, de las propiedades del filtro.
                        Repeater {
                            model: page.nodo && (page.tipoEf === "obs" || page.tipoEf === "gst") ? page.vg.propiedades(page.sel) : []
                            delegate: ColumnLayout {
                                id: prop
                                required property var modelData
                                Layout.fillWidth: true
                                spacing: 2
                                Text {
                                    visible: prop.modelData.kind !== "bool"
                                    Layout.fillWidth: true
                                    wrapMode: Text.WordWrap
                                    text: prop.modelData.label + ((prop.modelData.kind === "float" || prop.modelData.kind === "int")
                                          ? "  " + Number(deslP.value).toFixed(prop.modelData.kind === "int" ? 0 : 2) + (prop.modelData.suffix || "") : "")
                                    color: prop.modelData.kind === "header" ? Theme.text : prop.modelData.kind === "info" ? Theme.textFaint : Theme.textDim
                                    font.pixelSize: prop.modelData.kind === "header" ? 13 : 12
                                }
                                Slider {
                                    id: deslP
                                    visible: prop.modelData.kind === "float" || prop.modelData.kind === "int"
                                    Layout.fillWidth: true
                                    from: prop.modelData.min !== undefined ? prop.modelData.min : 0
                                    to: prop.modelData.max !== undefined ? prop.modelData.max : 1
                                    stepSize: prop.modelData.step || 0
                                    value: typeof prop.modelData.value === "number" ? prop.modelData.value : 0
                                    onMoved: page.ajustarFiltro(page.sel, prop.modelData.name, prop.modelData.kind === "int" ? Math.round(value) : value)
                                }
                                CheckBox {
                                    visible: prop.modelData.kind === "bool"
                                    text: prop.modelData.label
                                    checked: prop.modelData.value === true
                                    onToggled: page.avisar(page.vg.ajustarFiltro(page.sel, prop.modelData.name, checked))
                                }
                                ComboBox {
                                    visible: prop.modelData.kind === "list"
                                    Layout.fillWidth: true
                                    model: prop.modelData.options || []
                                    textRole: "label"
                                    currentIndex: {
                                        const o = prop.modelData.options || []
                                        for (let i = 0; i < o.length; ++i) if (o[i].value === prop.modelData.value) return i
                                        return -1
                                    }
                                    onActivated: (i) => page.avisar(page.vg.ajustarFiltro(page.sel, prop.modelData.name, prop.modelData.options[i].value))
                                }
                                TextField {
                                    visible: prop.modelData.kind === "text" || prop.modelData.kind === "path" || prop.modelData.kind === "color"
                                    Layout.fillWidth: true
                                    text: prop.modelData.value !== undefined ? String(prop.modelData.value) : ""
                                    placeholderText: prop.modelData.kind === "path" ? "Ruta del archivo" : prop.modelData.kind === "color" ? "#RRGGBB" : ""
                                    onEditingFinished: if (text !== String(prop.modelData.value)) page.avisar(page.vg.ajustarFiltro(page.sel, prop.modelData.name, text))
                                }
                            }
                        }

                        // Vista 360 y Cámara 360: la vista, las lentes y la guía para calibrar.
                        Panel360 {
                            visible: page.tipoEf === "vista360" || page.tipoEf === "panoramica"
                            Layout.fillWidth: true
                            p: page.paramsEf || ({})
                            camara: page.tipoEf === "panoramica"
                            lentes: page.nodo && page.nodo.tipo === "panoramica" ? (page.nodo.entradas || []).filter(e => !e.pedido).length : 0
                            onAjustar: (clave, valor) => page.ajustar(page.sel, clave, valor)
                            onAtajo: (preset, campo) => page.avisar(page.vg.presetPanoramicaUi(page.sel, preset, campo))
                        }
                        ColumnLayout {
                            visible: page.tipoEf === "panoramica" && !!page.nodo && page.nodo.tipo === "panoramica"
                            Layout.fillWidth: true
                            spacing: 4
                            Text {
                                Layout.fillWidth: true; wrapMode: Text.WordWrap
                                color: Theme.textFaint; font.pixelSize: 11
                                text: (page.nodo && page.nodo.publicada ? "Es la fuente «" : "Sale como la fuente «") +
                                      (page.nodo ? page.nodo.params.nombre || "" : "") + "». Cada puerto es una cámara, en orden (el de arriba es la lente 1); se agregan tirando un cable al libre."
                            }
                            GlassButton {
                                text: "Poner en la escena al aire"
                                onClicked: page.avisar(page.vg.aEscena(page.sel, ""))
                            }
                        }

                        // Croma: el color de fondo.
                        RowLayout {
                            visible: page.tipoEf === "croma"
                            spacing: 6
                            Text { text: "Fondo"; color: Theme.textDim; font.pixelSize: 12 }
                            Rectangle { width: 22; height: 22; radius: 4; color: page.paramsEf && page.paramsEf.clave !== undefined ? page.hex(page.paramsEf.clave) : "transparent"; border.color: Qt.rgba(1, 1, 1, 0.3) }
                            GlassButton { compact: true; text: "Verde"; onClicked: page.avisar(page.vg.ajustar(page.sel, { clave: 0x00B140 })) }
                            GlassButton { compact: true; text: "Azul"; onClicked: page.avisar(page.vg.ajustar(page.sel, { clave: 0x0047BB })) }
                            TextField {
                                Layout.preferredWidth: 80
                                placeholderText: "#RRGGBB"
                                onAccepted: {
                                    const v = parseInt(text.replace("#", ""), 16)
                                    if (!isNaN(v)) page.avisar(page.vg.ajustar(page.sel, { clave: v }))
                                }
                            }
                        }

                        // Seguimiento (baseguir): a quién sigue.
                        ColumnLayout {
                            visible: page.tipoEf === "seguir"
                            Layout.fillWidth: true
                            spacing: 6
                            readonly property var p: page.paramsEf || ({})
                            Text {
                                Layout.fillWidth: true; wrapMode: Text.WordWrap
                                color: Theme.textDim; font.pixelSize: 12
                                text: "No toca la imagen: lee las caras de una tarjeta Rostros (antes o después) y le manda el encuadre al PTZ (antes o después) con eventos de GStreamer. Clic en la vista previa: seguir a esa cara."
                            }
                            Text {
                                Layout.fillWidth: true; wrapMode: Text.WordWrap
                                color: Theme.text; font.pixelSize: 12
                                text: "Sigue: " + (parent.p.objetivo ? parent.p.objetivo : "nadie") +
                                      (Studio.seguidor && Studio.seguidor.siguiendo[page.vg.entrada]
                                       ? " · " + Studio.seguidor.siguiendo[page.vg.entrada].estado : "")
                            }
                            Flow {
                                Layout.fillWidth: true
                                spacing: 6
                                GlassButton { compact: true; text: "La más grande"; onClicked: page.avisar(Studio.seguidor.seguir(page.vg.entrada, { regla: "mayor" })) }
                                GlassButton { compact: true; text: "La del centro"; onClicked: page.avisar(Studio.seguidor.seguir(page.vg.entrada, { regla: "centro" })) }
                                GlassButton { compact: true; text: "Soltar"; onClicked: Studio.seguidor.dejar(page.vg.entrada) }
                            }
                        }

                        // Rostros (barostros): para qué la usa Personas, el análisis y el overlay.
                        ColumnLayout {
                            id: rostrosPanel
                            visible: page.tipoEf === "rostros"
                            Layout.fillWidth: true
                            spacing: 6
                            readonly property var p: page.paramsEf || ({})
                            readonly property var plantillas: [
                                { t: "Sin texto", v: "" },
                                { t: "Nombre", v: "{nombre}" },
                                { t: "Nombre y cargo", v: "{nombre} — {cargo}" },
                                { t: "Número", v: "Cara {id}" },
                                { t: "Confianza", v: "{confianza}" },
                                { t: "Nombre o número", v: "{nombre} #{id}" }]
                            Text {
                                Layout.fillWidth: true; wrapMode: Text.WordWrap
                                color: Theme.textDim; font.pixelSize: 12
                                text: rostrosPanel.p.auto
                                      ? "La puso Personas (" + (({ mirar: "la página la está mirando", autoalta: "anotado automático", puerta: "cámara de la puerta", seguir: "seguimiento del PTZ" })[rostrosPanel.p.uso] || "") + "): se va sola cuando deja de usarla."
                                      : "Mira la toma entera de la cámara (antes del PTZ digital) sin demorar la imagen. Las caras van en cada cuadro y en eventos «ba/rostros» para el resto de la cadena."
                            }
                            RowLayout {
                                Layout.fillWidth: true
                                CheckBox {
                                    text: "Reconocer (para Personas)"
                                    checked: !!rostrosPanel.p.reconocer
                                    onToggled: page.avisar(page.vg.ajustar(page.sel, { reconocer: checked }))
                                    ToolTip.visible: hovered; ToolTip.delay: 400
                                    ToolTip.text: "Saca el vector de cada cara (lo que compara la libreta). Personas lo prende sola cuando usa la cámara."
                                }
                                Item { Layout.fillWidth: true }
                                Text { text: "Detector"; color: Theme.textDim; font.pixelSize: 12 }
                                ComboBox {
                                    model: [{ t: "Automático", v: "auto" }, { t: "Apple (Vision)", v: "vision" }, { t: "YuNet", v: "yunet" }]
                                    textRole: "t"
                                    currentIndex: Math.max(0, model.findIndex(m => m.v === (rostrosPanel.p.detector || "auto")))
                                    onActivated: (i) => page.avisar(page.vg.ajustar(page.sel, { detector: model[i].v }))
                                }
                            }
                            Rectangle { Layout.fillWidth: true; height: 1; color: Theme.glassBorder }
                            RowLayout {
                                Layout.fillWidth: true
                                CheckBox {
                                    text: "Overlay sobre las caras"
                                    checked: !!rostrosPanel.p.marcar
                                    onToggled: page.avisar(page.vg.ajustar(page.sel, { marcar: checked }))
                                }
                                Item { Layout.fillWidth: true }
                                ComboBox {
                                    enabled: !!rostrosPanel.p.marcar
                                    model: [{ t: "Recuadro", v: "recuadro" }, { t: "Esquinas", v: "esquinas" }, { t: "Relleno", v: "relleno" },
                                            { t: "Tapar", v: "tapar" }, { t: "Pixelar", v: "pixelar" }]
                                    textRole: "t"
                                    currentIndex: Math.max(0, model.findIndex(m => m.v === (rostrosPanel.p.estilo || "recuadro")))
                                    onActivated: (i) => page.avisar(page.vg.ajustar(page.sel, { estilo: model[i].v }))
                                    ToolTip.visible: hovered; ToolTip.delay: 400
                                    ToolTip.text: "Tapar y pixelar sirven para no mostrar una cara (privacidad)"
                                }
                            }
                            RowLayout {
                                enabled: !!rostrosPanel.p.marcar
                                spacing: 6
                                Text { text: "Color"; color: Theme.textDim; font.pixelSize: 12 }
                                Repeater {
                                    model: [0xFFFFC83C, 0xFF35C46B, 0xFF38BDF8, 0xFFFF3B30, 0xFFFFFFFF, 0xFF000000]
                                    delegate: Rectangle {
                                        required property var modelData
                                        width: 20; height: 20; radius: 4
                                        color: "#" + (modelData & 0xFFFFFF).toString(16).padStart(6, "0")
                                        border.width: (rostrosPanel.p.color || 0xFFFFC83C) === modelData ? 2 : 1
                                        border.color: (rostrosPanel.p.color || 0xFFFFC83C) === modelData ? Theme.text : Qt.rgba(1, 1, 1, 0.3)
                                        MouseArea { anchors.fill: parent; onClicked: page.avisar(page.vg.ajustar(page.sel, { color: modelData })) }
                                    }
                                }
                            }
                            RowLayout {
                                Layout.fillWidth: true
                                enabled: !!rostrosPanel.p.marcar
                                Text { text: "Texto"; color: Theme.textDim; font.pixelSize: 12 }
                                ComboBox {
                                    Layout.preferredWidth: 150
                                    model: rostrosPanel.plantillas
                                    textRole: "t"
                                    currentIndex: rostrosPanel.plantillas.findIndex(m => m.v === (rostrosPanel.p.texto || ""))
                                    displayText: currentIndex < 0 ? "Personalizado" : currentText
                                    onActivated: (i) => page.avisar(page.vg.ajustar(page.sel, { texto: rostrosPanel.plantillas[i].v }))
                                }
                                ComboBox {
                                    model: [{ t: "Arriba", v: "arriba" }, { t: "Abajo", v: "abajo" }]
                                    textRole: "t"
                                    currentIndex: rostrosPanel.p.texto_posicion === "abajo" ? 1 : 0
                                    onActivated: (i) => page.avisar(page.vg.ajustar(page.sel, { texto_posicion: model[i].v }))
                                }
                            }
                            TextField {
                                Layout.fillWidth: true
                                enabled: !!rostrosPanel.p.marcar
                                text: rostrosPanel.p.texto || ""
                                placeholderText: "Plantilla: {nombre} {cargo} {id} {confianza} {n}"
                                color: Theme.text
                                onEditingFinished: if (text !== (rostrosPanel.p.texto || "")) page.avisar(page.vg.ajustar(page.sel, { texto: text }))
                            }
                            Text {
                                Layout.fillWidth: true; wrapMode: Text.WordWrap
                                color: Theme.textFaint; font.pixelSize: 11
                                text: "El nombre y el cargo aparecen cuando Personas reconoce a alguien. Para un monitor o para probar, conviene el overlay en una rama con salida, no en lo que va al aire."
                            }
                        }

                        // Recorte: espejo.
                        RowLayout {
                            visible: page.tipoEf === "recorte"
                            CheckBox {
                                text: "Espejo ↔"
                                checked: !!page.paramsEf && !!page.paramsEf.espejoH
                                onToggled: page.avisar(page.vg.ajustar(page.sel, { espejoH: checked }))
                            }
                            CheckBox {
                                text: "Espejo ↕"
                                checked: !!page.paramsEf && !!page.paramsEf.espejoV
                                onToggled: page.avisar(page.vg.ajustar(page.sel, { espejoV: checked }))
                            }
                        }

                        // Superresolución: se elige un PLUGIN (video/SuperRes: el catálogo y los
                        // instalados); la app lo corre con lo mejor de esta máquina.
                        ColumnLayout {
                            id: srPanel
                            visible: page.tipoEf === "superres"
                            Layout.fillWidth: true
                            spacing: 6
                            readonly property var sr: Studio.superres
                            readonly property var motores: sr ? sr.motores : []
                            // (depende de `modelos` para refrescarse cuando se instala algo)
                            readonly property var plugins: sr ? (sr.modelos, sr.pluginsUi()) : []
                            readonly property var visibles: plugins.filter(p => p.aca || !p.instalado)
                            readonly property string elegido: page.paramsEf ? (page.paramsEf.plugin || "") : ""
                            readonly property var actual: visibles.find(p => p.id === elegido) || null
                            readonly property var trabajo: sr ? sr.trabajo : ({})
                            property bool avanzado: false
                            Text {
                                Layout.fillWidth: true; wrapMode: Text.WordWrap
                                color: Theme.textDim; font.pixelSize: 12
                                text: "Agranda x2: lo que sigue trabaja al doble y la escena la recibe grande. Elegí el método; la demora se mide y el audio de la entrada se atrasa lo mismo (solo)."
                            }
                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 6
                                Text { text: "Método"; color: Theme.textDim; font.pixelSize: 12 }
                                ComboBox {
                                    id: srCombo
                                    Layout.fillWidth: true
                                    readonly property var lista: [{ id: "", nombre: "— elegí un método —", instalado: true }].concat(srPanel.visibles)
                                    model: lista.map(p => p.nombre + (!p.instalado ? "  (instalar)" : p.ms_coreml ? "  · " + Number(p.ms_coreml).toFixed(0) + " ms" : ""))
                                    currentIndex: Math.max(0, lista.findIndex(p => p.id === srPanel.elegido))
                                    onActivated: (i) => {
                                        const p = lista[i]
                                        if (p.id === "") return
                                        if (!p.instalado) {
                                            const e = srPanel.sr.instalarPluginUi(p.id)
                                            page.avisar(e === "" ? "" : e)
                                            if (e === "") Studio.notify("Instalando «" + p.nombre + "»: tarda unos minutos", "info")
                                            return
                                        }
                                        page.avisar(page.vg.ajustar(page.sel, { plugin: p.id, activo: true }))
                                    }
                                }
                            }
                            Text {
                                visible: !!srPanel.actual
                                Layout.fillWidth: true; wrapMode: Text.WordWrap
                                color: Theme.text; font.pixelSize: 11
                                text: srPanel.actual ? (srPanel.actual.descripcion || "") + (srPanel.actual.licencia ? "  (" + srPanel.actual.licencia + ")" : "") : ""
                            }
                            // Instalando (el laboratorio): el avance.
                            ColumnLayout {
                                visible: !!srPanel.trabajo.corriendo || (!!srPanel.trabajo.que && String(srPanel.trabajo.que).indexOf("Instalar") === 0)
                                Layout.fillWidth: true
                                spacing: 2
                                Text { text: srPanel.trabajo.que || ""; color: Theme.textDim; font.pixelSize: 11 }
                                ProgressBar { Layout.fillWidth: true; from: 0; to: 100; value: srPanel.trabajo.pct || 0; visible: !!srPanel.trabajo.corriendo }
                                Text {
                                    Layout.fillWidth: true; wrapMode: Text.WordWrap
                                    text: srPanel.trabajo.error ? "✕ " + srPanel.trabajo.error : (srPanel.trabajo.texto || "")
                                    color: srPanel.trabajo.error ? Theme.live : Theme.textFaint; font.pixelSize: 11
                                }
                            }
                            RowLayout {
                                visible: srPanel.elegido === "fsr" || (!!page.paramsEf && page.paramsEf.motor === "fsr" && srPanel.elegido === "")
                                Layout.fillWidth: true
                                spacing: 6
                                Text { text: "Nitidez (FSR)"; color: Theme.textDim; font.pixelSize: 12 }
                                Slider {
                                    id: nitidezSr
                                    Layout.fillWidth: true
                                    // RCAS: 0 = lo más afilado; 2 = casi nada. Se muestra al revés (más = más nítido).
                                    from: 0; to: 2; stepSize: 0.05
                                    value: page.paramsEf ? 2 - (page.paramsEf.nitidez !== undefined ? page.paramsEf.nitidez : 0.2) : 1.8
                                    onMoved: page.ajustar(page.sel, "nitidez", Math.round((2 - value) * 100) / 100)
                                }
                            }
                            CheckBox {
                                text: "Activa (si no, pasa sin tocar)"
                                checked: !page.paramsEf || page.paramsEf.activo !== false
                                onToggled: page.avisar(page.vg.ajustar(page.sel, { activo: checked }))
                            }
                            // Una red CoreML (Mac): dónde corre. Con dos redes a la vez, una en la GPU reparte la carga.
                            RowLayout {
                                visible: srPanel.motores.indexOf("coreml") >= 0 && !!srPanel.actual && !!srPanel.actual.archivos && !!srPanel.actual.archivos.coreml
                                spacing: 6
                                Text { text: "Corre en"; color: Theme.textDim; font.pixelSize: 12 }
                                ComboBox {
                                    Layout.fillWidth: true
                                    model: [ { t: "Elegir solo (Neural Engine)", v: "auto" }, { t: "Neural Engine", v: "ane" },
                                             { t: "GPU (compite con la composición)", v: "gpu" }, { t: "Procesador (lento)", v: "cpu" } ]
                                    textRole: "t"
                                    currentIndex: Math.max(0, model.findIndex(o => o.v === ((page.paramsEf && page.paramsEf.unidades) || "auto")))
                                    onActivated: (i) => page.avisar(page.vg.ajustar(page.sel, { unidades: model[i].v }))
                                }
                            }
                            // Una red ONNX (Windows/Linux): con qué la corre la placa. Vulkan pasa la red a ncnn la
                            // primera vez (tarda unos segundos) y anda en AMD, Intel y NVIDIA.
                            RowLayout {
                                visible: srPanel.motores.indexOf("onnx") >= 0 && srPanel.motores.indexOf("coreml") < 0 &&
                                         !!srPanel.actual && !!srPanel.actual.archivos && (!!srPanel.actual.archivos.onnx || !!srPanel.actual.archivos.onnx_nv12)
                                spacing: 6
                                Text { text: "Corre con"; color: Theme.textDim; font.pixelSize: 12 }
                                ComboBox {
                                    Layout.fillWidth: true
                                    model: [ { t: "Elegir solo", v: "auto" }, { t: "DirectML (cualquier placa, Windows)", v: "dml" },
                                             { t: "Vulkan (ncnn: AMD, Intel, NVIDIA)", v: "vulkan" }, { t: "TensorRT (NVIDIA, compila la red)", v: "tensorrt" },
                                             { t: "CUDA (NVIDIA)", v: "cuda" }, { t: "OpenVINO (Intel)", v: "openvino" },
                                             { t: "MIGraphX (AMD en Linux)", v: "migraphx" }, { t: "Procesador (lento)", v: "cpu" } ]
                                    textRole: "t"
                                    currentIndex: Math.max(0, model.findIndex(o => o.v === ((page.paramsEf && page.paramsEf.proveedor) || "auto")))
                                    onActivated: (i) => page.avisar(page.vg.ajustar(page.sel, { proveedor: model[i].v }))
                                }
                            }
                            RowLayout {
                                spacing: 6
                                GlassButton { compact: true; text: srPanel.avanzado ? "Ocultar avanzado" : "Avanzado…"; onClicked: srPanel.avanzado = !srPanel.avanzado }
                                GlassButton { compact: true; text: "Red propia, placa y plugins…"; onClicked: { panelSr.maestroPedido = ""; superresPop.open() } }
                                // Destilar: una red chica que imita a la red elegida, a medida de esta cámara.
                                GlassButton {
                                    compact: true
                                    visible: !!srPanel.actual && srPanel.actual.tipo === "red" && !srPanel.actual.propio
                                    text: "Destilar una red chica de esta…"
                                    onClicked: { panelSr.maestroPedido = srPanel.elegido; superresPop.open() }
                                }
                            }
                            // A mano: un motor y un modelo (sin plugin).
                            Flow {
                                visible: srPanel.avanzado
                                Layout.fillWidth: true
                                spacing: 4
                                Repeater {
                                    model: [
                                        { m: "apple", t: "Apple" }, { m: "fsr", t: "FSR 1" }, { m: "onnx", t: "Red ONNX" }, { m: "coreml", t: "Red CoreML" }
                                    ].filter(o => srPanel.motores.indexOf(o.m) >= 0)
                                    delegate: GlassButton {
                                        required property var modelData
                                        compact: true; text: modelData.t
                                        active: srPanel.elegido === "" && !!page.paramsEf && page.paramsEf.motor === modelData.m
                                        onClicked: page.avisar(page.vg.ajustar(page.sel, { plugin: "", motor: modelData.m }))
                                    }
                                }
                            }
                            RowLayout {
                                visible: srPanel.avanzado && srPanel.elegido === "" && !!page.paramsEf && (page.paramsEf.motor === "onnx" || page.paramsEf.motor === "coreml")
                                Layout.fillWidth: true
                                spacing: 6
                                Text { text: "Modelo"; color: Theme.textDim; font.pixelSize: 12 }
                                TextField {
                                    Layout.fillWidth: true
                                    text: page.paramsEf ? page.paramsEf.modelo || "" : ""
                                    placeholderText: "ruta del .onnx o del .mlmodelc"
                                    onEditingFinished: page.avisar(page.vg.ajustar(page.sel, { modelo: text }))
                                }
                            }
                        }

                        // Compositor: el lienzo, el armado y cada capa.
                        ColumnLayout {
                            id: compPanel
                            visible: !!page.nodo && page.nodo.tipo === "compositor"
                            Layout.fillWidth: true
                            spacing: 6
                            readonly property var puertos: page.nodo && page.nodo.tipo === "compositor"
                                ? (page.nodo.entradas || []).filter(e => !e.pedido).map(e => e.id) : []
                            readonly property var capas: page.nodo && page.nodo.params ? (page.nodo.params.capas || {}) : ({})
                            // De dónde viene cada puerto (para nombrar la capa).
                            function origen(puerto) {
                                const l = (page.vista === "estudio" ? page.est.cables : page.g.cables) || []
                                for (const c of l) if (c.hacia === page.sel && c.entrada === puerto) {
                                    const ns = (page.vista === "estudio" ? page.est.nodos : page.g.nodos) || []
                                    for (const n of ns) if (n.id === c.desde) return (n.grupo || "") + (n.tipo === "entrada" ? "" : " · " + n.etiqueta)
                                    return c.desde
                                }
                                return puerto
                            }
                            function armar(cual) {
                                const ps = compPanel.puertos
                                const c = {}
                                // Primero se borra todo (null) y después se pone lo del armado.
                                for (const p of ps) c[p] = null
                                if (cual === "pip" && ps.length >= 2) {
                                    c[ps[0]] = { x: 0, y: 0, ancho: 1, alto: 1, orden: 0 }
                                    c[ps[1]] = { x: 0.68, y: 0.06, ancho: 0.28, alto: 0.28, orden: 1 }
                                } else if (cual === "lado" && ps.length >= 2) {
                                    c[ps[0]] = { x: 0, y: 0.25, ancho: 0.5, alto: 0.5 }
                                    c[ps[1]] = { x: 0.5, y: 0.25, ancho: 0.5, alto: 0.5 }
                                }
                                page.avisar(page.vg.ajustar(page.sel, { capas: c }))
                            }
                            Text { text: "Nombre de la fuente"; color: Theme.textDim; font.pixelSize: 12 }
                            TextField {
                                Layout.fillWidth: true
                                text: page.nodo && page.nodo.params && page.nodo.params.nombre !== undefined ? page.nodo.params.nombre : ""
                                enabled: !!page.nodo && !page.nodo.publicada
                                onEditingFinished: if (page.nodo && text !== page.nodo.params.nombre) page.avisar(page.vg.ajustar(page.sel, { nombre: text }))
                            }
                            RowLayout {
                                spacing: 6
                                Text { text: "Lienzo"; color: Theme.textDim; font.pixelSize: 12 }
                                Repeater {
                                    model: [ { t: "Como el estudio", w: 0, h: 0 }, { t: "1080p", w: 1920, h: 1080 }, { t: "720p", w: 1280, h: 720 }, { t: "4K", w: 3840, h: 2160 }, { t: "Vertical", w: 1080, h: 1920 } ]
                                    delegate: GlassButton {
                                        required property var modelData
                                        compact: true; text: modelData.t
                                        active: !!page.nodo && page.nodo.params && (page.nodo.params.ancho || 0) === modelData.w && (page.nodo.params.alto || 0) === modelData.h
                                        onClicked: page.avisar(page.vg.ajustar(page.sel, { ancho: modelData.w, alto: modelData.h }))
                                    }
                                }
                            }
                            // Cualquier otro tamaño (0 = el del lienzo del estudio).
                            RowLayout {
                                spacing: 6
                                Text { text: "Ancho"; color: Theme.textDim; font.pixelSize: 12 }
                                SpinBox {
                                    from: 0; to: 8192; stepSize: 2; editable: true
                                    value: page.nodo && page.nodo.params ? (page.nodo.params.ancho || 0) : 0
                                    onValueModified: page.avisar(page.vg.ajustar(page.sel, { ancho: value }))
                                }
                                Text { text: "Alto"; color: Theme.textDim; font.pixelSize: 12 }
                                SpinBox {
                                    from: 0; to: 8192; stepSize: 2; editable: true
                                    value: page.nodo && page.nodo.params ? (page.nodo.params.alto || 0) : 0
                                    onValueModified: page.avisar(page.vg.ajustar(page.sel, { alto: value }))
                                }
                            }
                            RowLayout {
                                spacing: 6
                                Text { text: "Armado"; color: Theme.textDim; font.pixelSize: 12 }
                                GlassButton { compact: true; text: "Mosaico"; onClicked: compPanel.armar("mosaico") }
                                GlassButton { compact: true; text: "Imagen en imagen"; enabled: compPanel.puertos.length >= 2; onClicked: compPanel.armar("pip") }
                                GlassButton { compact: true; text: "Lado a lado"; enabled: compPanel.puertos.length >= 2; onClicked: compPanel.armar("lado") }
                            }
                            Repeater {
                                model: compPanel.puertos
                                delegate: ColumnLayout {
                                    id: capaCol
                                    required property string modelData
                                    readonly property var k: compPanel.capas[modelData] || ({})
                                    Layout.fillWidth: true
                                    spacing: 2
                                    Text { text: compPanel.origen(modelData) + "  (" + modelData + ")"; color: Theme.text; font { pixelSize: 12; weight: Font.DemiBold } }
                                    Text {
                                        visible: capaCol.k.x === undefined
                                        text: "En el mosaico"; color: Theme.textFaint; font.pixelSize: 11
                                    }
                                    Repeater {
                                        model: [ { c: "x", t: "X" }, { c: "y", t: "Y" }, { c: "ancho", t: "Ancho" }, { c: "alto", t: "Alto" }, { c: "opacidad", t: "Opacidad" } ]
                                        delegate: RowLayout {
                                            required property var modelData
                                            Layout.fillWidth: true
                                            spacing: 6
                                            Text { text: modelData.t; color: Theme.textDim; font.pixelSize: 11; Layout.preferredWidth: 58 }
                                            Slider {
                                                Layout.fillWidth: true
                                                from: 0; to: 1
                                                value: capaCol.k[modelData.c] !== undefined ? capaCol.k[modelData.c]
                                                     : modelData.c === "opacidad" || modelData.c === "ancho" || modelData.c === "alto" ? 1 : 0
                                                onMoved: {
                                                    const c = {}
                                                    c[capaCol.modelData] = {}
                                                    c[capaCol.modelData][modelData.c] = value
                                                    // La primera vez que se mueve, toma una caja completa (sale del mosaico).
                                                    if (capaCol.k.x === undefined && modelData.c !== "opacidad")
                                                        c[capaCol.modelData] = Object.assign({ x: 0, y: 0, ancho: 1, alto: 1 }, c[capaCol.modelData])
                                                    page.ajustar(page.sel, "capas", c)
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                            Text {
                                Layout.fillWidth: true; wrapMode: Text.WordWrap
                                text: page.nodo && page.nodo.publicada
                                      ? "Ya es una fuente: se pone y se saca de las escenas como cualquier otra."
                                      : "Se ve como una fuente con este nombre en cualquier escena."
                                color: Theme.textFaint; font.pixelSize: 11
                            }
                            GlassButton {
                                text: "Poner en la escena al aire"
                                onClicked: page.avisar(page.vg.aEscena(page.sel, ""))
                            }
                        }

                        // Salida: su nombre y ponerla en una escena.
                        ColumnLayout {
                            visible: !!page.nodo && page.nodo.tipo === "salida"
                            Layout.fillWidth: true
                            spacing: 6
                            Text { text: "Nombre de la fuente"; color: Theme.textDim; font.pixelSize: 12 }
                            TextField {
                                Layout.fillWidth: true
                                text: page.nodo && page.nodo.params.nombre !== undefined ? page.nodo.params.nombre : ""
                                enabled: !!page.nodo && !page.nodo.publicada
                                onEditingFinished: if (page.nodo && text !== page.nodo.params.nombre) page.avisar(page.vg.ajustar(page.sel, { nombre: text }))
                            }
                            Text {
                                Layout.fillWidth: true
                                wrapMode: Text.WordWrap
                                text: page.nodo && page.nodo.publicada
                                      ? "Ya es una fuente: se pone y se saca de las escenas como cualquier otra. (El nombre no se cambia mientras exista.)"
                                      : "Publica la imagen de este punto del grafo como una fuente con este nombre."
                                color: Theme.textFaint; font.pixelSize: 11
                            }
                            GlassButton {
                                text: "Poner en la escena al aire"
                                onClicked: page.avisar(page.vg.aEscena(page.sel, ""))
                            }
                        }

                        // La ayuda de la tarjeta: cada parámetro y los eventos que manda y
                        // escucha (lo mismo que da video_tarjeta_ayuda por MCP).
                        ColumnLayout {
                            id: ayudaPanel
                            readonly property var info: page.vg && page.tipoEf ? (page.vg.catalogo.find(c => c.tipo === page.tipoEf) || null) : null
                            property bool abierto: false
                            visible: !!info && ((info.parametros || []).length > 0 || !!(info.eventos && info.eventos.manda))
                            Layout.fillWidth: true
                            spacing: 4
                            GlassButton {
                                compact: true
                                text: (ayudaPanel.abierto ? "▾ " : "▸ ") + "Parámetros y eventos"
                                onClicked: ayudaPanel.abierto = !ayudaPanel.abierto
                            }
                            Repeater {
                                model: ayudaPanel.abierto && ayudaPanel.info ? ayudaPanel.info.parametros : []
                                Text {
                                    required property var modelData
                                    Layout.fillWidth: true; wrapMode: Text.WordWrap
                                    color: Theme.textDim; font.pixelSize: 11; textFormat: Text.StyledText
                                    text: "<b>" + modelData.clave + "</b> <i>(" + modelData.tipo
                                          + (modelData.opciones ? ": " + modelData.opciones.join(" | ") : "")
                                          + (modelData.min !== undefined ? ", " + modelData.min + "…" + modelData.max : "")
                                          + ")</i> — " + modelData.descripcion
                                }
                            }
                            Text {
                                visible: ayudaPanel.abierto && !!ayudaPanel.info && !!ayudaPanel.info.texto_overlay
                                Layout.fillWidth: true; wrapMode: Text.WordWrap
                                color: Theme.textDim; font.pixelSize: 11
                                text: ayudaPanel.info && ayudaPanel.info.texto_overlay ? ayudaPanel.info.texto_overlay.replace(/`/g, "") : ""
                            }
                            Text {
                                visible: ayudaPanel.abierto && !!ayudaPanel.info && !!(ayudaPanel.info.eventos && ayudaPanel.info.eventos.manda)
                                Layout.fillWidth: true; wrapMode: Text.WordWrap
                                color: Theme.textDim; font.pixelSize: 11
                                text: {
                                    const e = ayudaPanel.info && ayudaPanel.info.eventos ? ayudaPanel.info.eventos : null
                                    if (!e || !e.manda) return ""
                                    return "Manda:\n• " + e.manda.join("\n• ") + "\nEscucha:\n• " + (e.escucha || []).join("\n• ")
                                }
                            }
                        }

                        // Mover en la cadena (el orden importa; todas dejan pasar los eventos de las otras).
                        RowLayout {
                            visible: !!page.nodo && page.nodo.tipo !== "entrada" && page.nodo.tipo !== "escena" &&
                                     page.nodo.tipo !== "salida" && page.nodo.tipo !== "compositor" && page.nodo.tipo !== "control"
                            spacing: 6
                            GlassButton {
                                compact: true; text: "◀ Antes"
                                onClicked: page.avisar(page.vg.correr(page.sel, -1))
                                ToolTip.visible: hovered; ToolTip.delay: 400; ToolTip.text: "Un lugar antes en la cadena"
                            }
                            GlassButton {
                                compact: true; text: "Después ▶"
                                onClicked: page.avisar(page.vg.correr(page.sel, 1))
                                ToolTip.visible: hovered; ToolTip.delay: 400; ToolTip.text: "Un lugar después en la cadena"
                            }
                        }
                        GlassButton {
                            visible: !!page.nodo && page.nodo.tipo !== "entrada" && page.nodo.tipo !== "escena" && page.nodo.tipo !== "ptz"
                            compact: true
                            text: "Quitar la tarjeta"
                            onClicked: { page.avisar(page.vg.quitar(page.sel)); page.sel = "" }
                        }
                    }
                }
            }
        }
    }

    PanelSincronia { id: sincronia; vg: page.vg }
    PanelProgramador { id: programadorPop; vg: page.vg }
    PanelScopes { id: scopesPop; entrada: page.vg ? page.vg.entrada : "" }
    Popup {
        id: superresPop
        anchors.centerIn: parent
        width: Math.min(parent.width - 80, 980)
        height: Math.min(parent.height - 80, 760)
        modal: true
        padding: 18
        background: Rectangle { color: Theme.panel; radius: Theme.radiusSmall; border.color: Theme.glassBorder }
        ColumnLayout {
            anchors.fill: parent
            spacing: 10
            RowLayout {
                Layout.fillWidth: true
                Text { text: "Superresolución"; color: Theme.text; font { pixelSize: 18; weight: Font.DemiBold } Layout.fillWidth: true }
                GlassButton { compact: true; text: "Cerrar"; onClicked: superresPop.close() }
            }
            ScrollView {
                id: srScroll
                Layout.fillWidth: true
                Layout.fillHeight: true
                contentWidth: availableWidth
                clip: true
                PanelSuperres { id: panelSr; width: srScroll.availableWidth; vg: page.vg; visible: superresPop.opened }
            }
        }
    }

    CatalogoModulos {
        id: catalogo
        vg: page.vg
        // Abre el catálogo directo en el detalle de un módulo.
        function abrirEn(elemento) {
            open()
            for (const m of modulos) if (m.elemento === elemento) { verDetalle(m); return }
        }
        onElegido: (tipo, id) => {
            const despues = page.nodo && page.nodo.tipo !== "escena" && !page.nodo.externo ? page.sel : ""
            page.avisar(tipo === "gst" ? page.vg.agregarModulo(id, despues) : page.vg.agregarFiltro(id, despues))
        }
    }

    // Una entrada virtual nueva: señal de ajuste, negro… o un pipeline de GStreamer.
    Popup {
        id: virtualPop
        modal: true
        anchors.centerIn: Overlay.overlay
        width: 520
        padding: 18
        property string preset: "barras-hd"
        onOpened: { nombreVirtual.text = ""; pipelineVirtual.text = ""; preset = "barras-hd"; nombreVirtual.forceActiveFocus() }
        background: Rectangle { color: Theme.panel; radius: Theme.radius; border.color: Theme.glassBorder }
        ColumnLayout {
            anchors.fill: parent
            spacing: 10
            Text { text: "Entrada virtual"; color: Theme.text; font { pixelSize: 18; weight: Font.DemiBold } }
            Text {
                Layout.fillWidth: true; wrapMode: Text.WordWrap
                text: "Una entrada más (con su grafo de tarjetas) que genera GStreamer: una señal de ajuste para calibrar, un negro, o cualquier origen (RTSP, SRT…)."
                color: Theme.textDim; font.pixelSize: 12
            }
            TextField { id: nombreVirtual; Layout.fillWidth: true; placeholderText: "Nombre (p. ej. «Barras»)" }
            Flow {
                Layout.fillWidth: true
                spacing: 5
                Repeater {
                    model: page.vg ? page.vg.virtuales() : []
                    delegate: GlassButton {
                        required property var modelData
                        compact: true
                        text: modelData.nombre
                        active: virtualPop.preset === modelData.id
                        onClicked: { virtualPop.preset = modelData.id; if (nombreVirtual.text === "") nombreVirtual.text = modelData.nombre }
                        ToolTip.visible: hovered; ToolTip.delay: 400; ToolTip.text: modelData.descripcion
                    }
                }
                GlassButton {
                    compact: true; text: "Pipeline propio…"
                    active: virtualPop.preset === "pipeline"
                    onClicked: virtualPop.preset = "pipeline"
                }
            }
            TextField {
                id: pipelineVirtual
                visible: virtualPop.preset === "pipeline"
                Layout.fillWidth: true
                font.family: "Menlo"
                placeholderText: "rtspsrc location=rtsp://… latency=200 ! decodebin"
            }
            RowLayout {
                Layout.fillWidth: true
                Item { Layout.fillWidth: true }
                GlassButton { compact: true; text: "Cancelar"; onClicked: virtualPop.close() }
                GlassButton {
                    compact: true; text: "Crear en la escena al aire"; active: true
                    enabled: nombreVirtual.text.trim() !== "" && (virtualPop.preset !== "pipeline" || pipelineVirtual.text.trim() !== "")
                    onClicked: {
                        const e = page.vg.crearVirtual(nombreVirtual.text.trim(),
                                                       virtualPop.preset === "pipeline" ? "pipeline:" + pipelineVirtual.text.trim() : virtualPop.preset, "")
                        page.avisar(e)
                        if (e === "") { page.vg.entrada = nombreVirtual.text.trim(); virtualPop.close() }
                    }
                }
            }
        }
    }
}
