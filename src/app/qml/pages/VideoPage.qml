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
    readonly property var est: vg && vista === "estudio" ? vg.estudio : ({})
    property string sel: ""
    // Dos vistas, como el audio: el rack de UNA entrada, o todo el estudio
    // (cada entrada en su franja y los cables que cruzan entre ellas).
    readonly property string vista: vg ? vg.vista : "racks"
    property string msg: ""
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
        if (tipo === "compositor") return "#f472b6"
        if (tipo === "croma") return "#22c55e"
        if (tipo === "color") return "#f59e0b"
        if (tipo === "recorte") return "#38bdf8"
        if (tipo === "salida") return "#a78bfa"
        return Theme.accent
    }
    function f2(v) { return Number(v).toFixed(2) }
    function hex(n) { return "#" + ("000000" + Number(n).toString(16)).slice(-6).toUpperCase() }
    function resumen(n) {
        const p = n.params || {}
        if (n.tipo === "entrada") return "La cámara (ya encuadrada por el PTZ)"
        if (n.tipo === "escena") return "Lo que ve la escena"
        if (n.tipo === "obs") return "Filtro de OBS · " + (p.filtro || "")
        if (n.tipo === "compositor") return ((n.entradas || []).length - 1) + " entradas · " + (p.ancho || 0) + "×" + (p.alto || 0)
        if (n.tipo === "color") return "brillo " + f2(p.brillo) + " · contraste " + f2(p.contraste) + " · saturación " + f2(p.saturacion)
        if (n.tipo === "croma") return "fondo " + hex(p.clave) + " · similitud " + f2(p.similitud)
        if (n.tipo === "recorte") {
            const partes = []
            if (p.izquierda > 0 || p.arriba > 0 || p.derecha > 0 || p.abajo > 0) partes.push("bordes recortados")
            if (p.espejoH) partes.push("espejo ↔")
            if (p.espejoV) partes.push("espejo ↕")
            return partes.length ? partes.join(" · ") : "sin cambios"
        }
        if (n.tipo === "salida") return "«" + (p.nombre || "") + "»" + (n.publicada ? " · en escena" : "")
        return ""
    }
    // Los nodos de afuera (otra entrada, el estudio) en una columna a la
    // derecha: su posición es la de su propia vista.
    readonly property var nodosVista: {
        const l = page.g.nodos || []
        let maxX = 0
        for (const n of l) if (!n.externo) maxX = Math.max(maxX, n.x)
        const out = []
        let yExt = 0
        for (let i = 0; i < l.length; ++i) {
            const n = Object.assign({}, l[i], { resumen: page.resumen(l[i]) })
            if (n.externo) { n.x = maxX + 260; n.y = yExt; yExt += 140 }
            out.push(n)
        }
        return out
    }
    // La vista Estudio: todos los nodos, cada entrada en su franja.
    onEstChanged: console.log("DEPURACION estudio:", JSON.stringify(Object.keys(page.est)), (page.est.nodos || []).length, "vista", page.vista)
    readonly property var nodosEstudio: {
        const l = page.est.nodos || []
        const out = []
        for (let i = 0; i < l.length; ++i) out.push(Object.assign({}, l[i], { resumen: page.resumen(l[i]), externo: false }))
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
            if (k !== "") out.push({ titulo: k, y: porGrupo[k].min - 26, alto: porGrupo[k].max - porGrupo[k].min + 40 })
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
        const orden = ["Cámaras", "Red", "NDI"]
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
            ListView {
                anchors.fill: parent
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
            Flow {
                Layout.fillWidth: true
                spacing: 8
                Repeater {
                    model: page.vg ? page.vg.catalogo : []
                    delegate: GlassButton {
                        required property var modelData
                        compact: true
                        text: "+ " + modelData.titulo
                        enabled: page.vg && page.vg.entrada !== ""
                        // Después de la tarjeta elegida (o antes de la escena).
                        onClicked: page.avisar(page.vg.agregar(modelData.tipo, page.nodo && page.nodo.tipo !== "escena" && !page.nodo.externo ? page.sel : ""))
                        ToolTip.visible: hovered
                        ToolTip.delay: 500
                        ToolTip.text: modelData.descripcion + (page.nodo && page.nodo.tipo !== "escena" && modelData.ambito !== "estudio" ? " Va después de la tarjeta elegida." : "")
                    }
                }
                GlassButton {
                    compact: true
                    text: "+ Filtros…"
                    icon: "⌕"
                    enabled: page.vg && page.vg.entrada !== ""
                    onClicked: { filtrosPopup.lista = page.vg.filtrosObs(); filtrosPopup.open(); buscaFiltro.forceActiveFocus() }
                    ToolTip.visible: hovered; ToolTip.delay: 400
                    ToolTip.text: "Cualquier filtro de video de OBS como tarjeta: corrección de color, LUT (.cube), llaves, nitidez, recorte, escala, máscaras, Mejorar imagen, PTZ digital…"
                    Popup {
                        id: filtrosPopup
                        property var lista: []
                        y: parent.height + 4
                        width: 340
                        height: Math.min(460, 70 + lista.length * 34)
                        padding: 8
                        background: Rectangle { color: Theme.panel; radius: Theme.radiusSmall; border.color: Theme.glassBorder }
                        ColumnLayout {
                            anchors.fill: parent
                            spacing: 6
                            TextField { id: buscaFiltro; Layout.fillWidth: true; placeholderText: "Buscar filtro…" }
                            ListView {
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                clip: true
                                spacing: 2
                                model: filtrosPopup.lista.filter(f => buscaFiltro.text === "" ||
                                    (f.nombre + " " + f.id).toLowerCase().indexOf(buscaFiltro.text.toLowerCase()) >= 0)
                                delegate: GlassButton {
                                    required property var modelData
                                    width: ListView.view.width
                                    compact: true
                                    text: modelData.nombre + (modelData.propio ? "  ·  de la app" : "")
                                    onClicked: {
                                        page.avisar(page.vg.agregarFiltro(modelData.id, page.nodo && page.nodo.tipo !== "escena" && !page.nodo.externo ? page.sel : ""))
                                        filtrosPopup.close()
                                    }
                                }
                            }
                        }
                    }
                }
                Item { width: 12; height: 1 }
                GlassButton { compact: true; text: "Racks"; active: page.vista === "racks"; onClicked: page.vg.vista = "racks"
                              ToolTip.visible: hovered; ToolTip.delay: 400; ToolTip.text: "El pipeline de la entrada elegida" }
                GlassButton { compact: true; text: "Estudio"; icon: "▦"; active: page.vista === "estudio"; onClicked: page.vg.vista = "estudio"
                              ToolTip.visible: hovered; ToolTip.delay: 400; ToolTip.text: "Todas las entradas, cada una en su franja, y los cables entre ellas" }
                Item { width: 12; height: 1 }
                GlassButton {
                    compact: true
                    text: "Volver a como vino"
                    visible: page.g.trivial === false
                    onClicked: ApplicationWindow.window.confirm({
                        title: "¿Sacar todas las tarjetas de «" + page.vg.entrada + "»?",
                        text: "La entrada vuelve a ir directo a la escena. Las salidas que estén en escenas quedan en negro.",
                        accept: "Sacar todo", reject: "Cancelar",
                        action: function() { page.avisar(page.vg.limpiar()); page.sel = "" }
                    })
                }
            }
            Patchbay {
                Layout.fillWidth: true
                Layout.fillHeight: true
                nodos: page.vista === "estudio" ? page.nodosEstudio : page.nodosVista
                cables: (page.vista === "estudio" ? page.est.cables : page.g.cables) || []
                franjas: page.vista === "estudio" ? page.franjasEstudio : []
                seleccionado: page.sel
                colorDe: page.colorDe
                onElegir: (id) => page.elegir(id)
                onMover: (id, x, y) => page.avisar(page.vg.mover(id, x, y))
                onConectar: (desde, salida, hacia, entrada) => page.avisar(page.vg.conectar(desde, salida, hacia, entrada))
                onDesconectar: (desde, salida, hacia, entrada) => page.avisar(page.vg.desconectar(desde, salida, hacia, entrada))
            }
            Text {
                visible: page.msg !== ""
                Layout.fillWidth: true
                text: page.msg; color: Theme.warn; font.pixelSize: 12; wrapMode: Text.WordWrap
            }
        }

        // --- Vista previa y la tarjeta elegida ------------------------------------------
        Panel {
            Layout.preferredWidth: 320
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
                        anchors.fill: parent
                        columns: Math.ceil(width / 12)
                        Repeater {
                            model: Math.ceil(parent.width / 12) * Math.ceil(parent.height / 12)
                            delegate: Rectangle {
                                required property int index
                                width: 12; height: 12
                                color: (Math.floor(index / Math.ceil(parent.width / 12)) + index) % 2 ? "#2a2a2a" : "#1a1a1a"
                            }
                        }
                    }
                    VideoItem {
                        id: vista
                        anchors.fill: parent
                        source: page.vg && page.vg.entrada !== "" ? "entrada:" + page.vg.entrada : ""
                        fillMode: VideoItem.Fit
                    }
                    Text {
                        visible: !vista.hasFrame
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
                                  : "Del estudio: junta varias entradas. Cada cable a su entrada «+» crea un puerto nuevo. (Todavía no se ejecuta: es el paso siguiente.)"
                            color: Theme.textDim; font.pixelSize: 12
                        }

                        // Deslizadores de los parámetros numéricos.
                        Repeater {
                            model: {
                                if (!page.nodo) return []
                                const t = page.nodo.tipo
                                if (t === "color") return [
                                    { k: "brillo", t: "Brillo", min: -1, max: 1 },
                                    { k: "contraste", t: "Contraste", min: 0, max: 2 },
                                    { k: "saturacion", t: "Saturación", min: 0, max: 2 },
                                    { k: "gamma", t: "Gamma", min: 0.2, max: 3 }]
                                if (t === "croma") return [
                                    { k: "similitud", t: "Similitud (cuánto se va)", min: 0, max: 1 },
                                    { k: "suavidad", t: "Suavidad del borde", min: 0, max: 1 },
                                    { k: "derrame", t: "Quitar el reflejo de color", min: 0, max: 1 }]
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
                                    Text { text: page.f2(desl.value); color: Theme.textFaint; font.pixelSize: 11 }
                                }
                                Slider {
                                    id: desl
                                    Layout.fillWidth: true
                                    from: modelData.min; to: modelData.max
                                    value: page.nodo ? page.nodo.params[modelData.k] : 0
                                    onMoved: page.ajustar(page.sel, modelData.k, value)
                                }
                            }
                        }

                        // Una tarjeta de OBS: sus controles, de las propiedades del filtro.
                        Repeater {
                            model: page.nodo && page.nodo.tipo === "obs" ? page.vg.propiedades(page.sel) : []
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

                        // Croma: el color de fondo.
                        RowLayout {
                            visible: !!page.nodo && page.nodo.tipo === "croma"
                            spacing: 6
                            Text { text: "Fondo"; color: Theme.textDim; font.pixelSize: 12 }
                            Rectangle { width: 22; height: 22; radius: 4; color: page.nodo && page.nodo.params.clave !== undefined ? page.hex(page.nodo.params.clave) : "transparent"; border.color: Qt.rgba(1, 1, 1, 0.3) }
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

                        // Recorte: espejo.
                        RowLayout {
                            visible: !!page.nodo && page.nodo.tipo === "recorte"
                            CheckBox {
                                text: "Espejo ↔"
                                checked: !!page.nodo && !!page.nodo.params.espejoH
                                onToggled: page.avisar(page.vg.ajustar(page.sel, { espejoH: checked }))
                            }
                            CheckBox {
                                text: "Espejo ↕"
                                checked: !!page.nodo && !!page.nodo.params.espejoV
                                onToggled: page.avisar(page.vg.ajustar(page.sel, { espejoV: checked }))
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

                        GlassButton {
                            visible: !!page.nodo && page.nodo.tipo !== "entrada" && page.nodo.tipo !== "escena"
                            compact: true
                            text: "Quitar la tarjeta"
                            onClicked: { page.avisar(page.vg.quitar(page.sel)); page.sel = "" }
                        }
                    }
                }
            }
        }
    }
}
