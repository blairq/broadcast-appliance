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
    property string sel: ""
    property string msg: ""
    readonly property var nodo: {
        const l = page.g.nodos || []
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
        if (tipo === "in" || tipo === "out") return "#64748b"
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
        if (n.tipo === "in") return "La cámara (ya encuadrada por el PTZ)"
        if (n.tipo === "out") return "Lo que ve la escena"
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
    readonly property var nodosVista: {
        const l = page.g.nodos || []
        const out = []
        for (let i = 0; i < l.length; ++i) out.push(Object.assign({}, l[i], { resumen: page.resumen(l[i]) }))
        return out
    }
    function avisar(e) { page.msg = e || "" }
    // Los deslizadores aplican mientras se mueven, sin pedir un cambio por píxel.
    property var pendiente: null
    Timer { id: aplicar; interval: 80; onTriggered: if (page.pendiente) { page.avisar(page.vg.ajustar(page.pendiente.id, page.pendiente.p)); page.pendiente = null } }
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
                model: page.vg ? page.vg.entradas : []
                delegate: Rectangle {
                    required property string modelData
                    width: ListView.view.width
                    height: 40
                    radius: 10
                    color: page.vg && page.vg.entrada === modelData ? Qt.rgba(1, 0.42, 0.17, 0.18) : Qt.rgba(1, 1, 1, 0.04)
                    border.color: page.vg && page.vg.entrada === modelData ? Theme.accent : "transparent"
                    Text {
                        anchors { left: parent.left; right: parent.right; verticalCenter: parent.verticalCenter; margins: 12 }
                        text: modelData; elide: Text.ElideRight
                        color: Theme.text; font.pixelSize: 13
                    }
                    MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: { page.vg.entrada = modelData; page.sel = "" } }
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
                        onClicked: page.avisar(page.vg.agregar(modelData.tipo, page.nodo && page.nodo.tipo !== "out" ? page.sel : ""))
                        ToolTip.visible: hovered
                        ToolTip.delay: 500
                        ToolTip.text: modelData.descripcion + (page.nodo && page.nodo.tipo !== "out" ? " Va después de la tarjeta elegida." : "")
                    }
                }
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
                nodos: page.nodosVista
                cables: page.g.cables || []
                seleccionado: page.sel
                colorDe: page.colorDe
                onElegir: (id) => page.sel = id
                onMover: (id, x, y) => page.avisar(page.vg.mover(id, x, y))
                onConectar: (desde, hacia) => page.avisar(page.vg.conectar(desde, hacia))
                onDesconectar: (desde, hacia) => page.avisar(page.vg.desconectar(desde, hacia))
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
                            visible: !!page.nodo && (page.nodo.tipo === "in" || page.nodo.tipo === "out")
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                            text: page.nodo && page.nodo.tipo === "in"
                                  ? "La cámara tal como sale del PTZ (Mejorar imagen y el encuadre digital, en «Cámaras PTZ»)."
                                  : "Lo que llega acá es lo que ven las escenas que usan esta entrada. Sin nada conectado, no se ve."
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
                            visible: !!page.nodo && page.nodo.tipo !== "in" && page.nodo.tipo !== "out"
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
