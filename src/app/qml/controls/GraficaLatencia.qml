// La línea de tiempo de Latencias del pipeline: la latencia de varias tarjetas a la vez
// (cada una con su color), superpuestas en una escala o apiladas en franjas (cada una
// con la suya), y abajo la franja de eventos (conexiones, escenas, salidas, notas).
// Un click en un punto pide marcar ese momento (pedirNota); la cruz que sigue al
// puntero muestra el valor de cada serie en ese instante. La ventana termina en `fin`
// (la página la congela al pausar).
import QtQuick
import BA

Item {
    id: g
    /// [{id, titulo, color, datos: [{t, ms, acumulado}]}]
    property var series: []
    property string modo: "superpuestas" ///< superpuestas | apiladas
    property bool verAcumulado: false     ///< en vez de lo que agrega, lo acumulado desde la captura
    property real ventanaMs: 60000
    property real fin: Date.now()
    property var eventos: []
    property real referencia: 0           ///< la latencia del estudio (con verAcumulado)
    signal pedirNota(real t)

    readonly property int izq: 52
    readonly property int der: 12
    readonly property int altoEje: 18
    readonly property int altoEventos: 30
    readonly property real anchoPlot: Math.max(10, width - izq - der)
    readonly property real altoPlot: Math.max(10, height - altoEje - altoEventos)
    readonly property int franjas: modo === "apiladas" ? Math.max(1, series.length) : 1

    function valor(p) { return g.verAcumulado ? p.acumulado : p.ms }
    function xDe(t) { return g.izq + (1 - (g.fin - t) / g.ventanaMs) * g.anchoPlot }
    function tDe(x) { return g.fin - (1 - (x - g.izq) / g.anchoPlot) * g.ventanaMs }
    function techo(m) {
        const paso = m <= 10 ? 2 : m <= 50 ? 10 : m <= 200 ? 50 : m <= 1000 ? 200 : 1000
        return Math.max(paso, Math.ceil(m * 1.1 / paso) * paso)
    }
    /// El techo de cada franja (apiladas: cada serie el suyo; superpuestas: uno para todas).
    readonly property var techos: {
        const out = []
        let comun = g.verAcumulado ? g.referencia : 0
        for (const s of g.series) {
            let m = 0
            for (const p of (s.datos || [])) if (p.t >= g.fin - g.ventanaMs) m = Math.max(m, g.valor(p))
            out.push(g.techo(Math.max(1, m)))
            comun = Math.max(comun, m)
        }
        return g.modo === "apiladas" ? out : g.series.map(() => g.techo(Math.max(1, comun)))
    }
    function franjaDe(i) { return g.modo === "apiladas" ? i : 0 }
    function yDe(v, i) {
        const alto = g.altoPlot / g.franjas, y0 = g.franjaDe(i) * alto
        return y0 + 4 + (1 - v / (g.techos[i] || 1)) * (alto - 8)
    }
    function colorEvento(tipo) {
        return ({ conexion: "#35c46b", desconexion: "#ff3b30", escena: "#4fd8ff", salida: "#c38bff",
                  latencia: "#f5b83d", tarde: "#ff9a5c", rearme: "#f5b83d", nota: "#ffffff" })[tipo] || "#a2a2aa"
    }

    onSeriesChanged: lienzo.requestPaint()
    onModoChanged: lienzo.requestPaint()
    onVerAcumuladoChanged: lienzo.requestPaint()
    onFinChanged: lienzo.requestPaint()
    onVentanaMsChanged: lienzo.requestPaint()
    onEventosChanged: lienzo.requestPaint()
    onWidthChanged: lienzo.requestPaint()
    onHeightChanged: lienzo.requestPaint()

    Canvas {
        id: lienzo
        anchors.fill: parent
        onPaint: {
            const c = getContext("2d")
            c.reset()
            c.font = "9px sans-serif"
            c.lineWidth = 1
            // Las franjas con su grilla y su escala.
            const alto = g.altoPlot / g.franjas
            for (let f = 0; f < g.franjas; ++f) {
                const i = g.modo === "apiladas" ? f : 0
                const techo = g.techos[i] || 1
                for (let k = 0; k <= 2; ++k) {
                    const v = techo * k / 2, y = g.yDe(v, i)
                    c.strokeStyle = Qt.rgba(1, 1, 1, k === 0 ? 0.16 : 0.05)
                    c.beginPath(); c.moveTo(g.izq, y); c.lineTo(width - g.der, y); c.stroke()
                    c.fillStyle = Qt.rgba(1, 1, 1, 0.42)
                    c.textAlign = "right"
                    c.fillText(v >= 1000 ? (v / 1000).toFixed(1) + " s" : Math.round(v) + " ms", g.izq - 6, y + 3)
                }
                if (g.modo === "apiladas" && g.series[f]) {
                    c.fillStyle = g.series[f].color
                    c.textAlign = "left"
                    c.fillText(g.series[f].titulo, g.izq + 6, f * alto + 12)
                }
            }
            // El eje del tiempo.
            c.textAlign = "center"
            const pasos = 6
            for (let k = 0; k <= pasos; ++k) {
                const x = g.izq + k / pasos * g.anchoPlot
                const s = Math.round((1 - k / pasos) * g.ventanaMs / 1000)
                c.strokeStyle = Qt.rgba(1, 1, 1, 0.05)
                c.beginPath(); c.moveTo(x, 0); c.lineTo(x, g.altoPlot); c.stroke()
                c.fillStyle = Qt.rgba(1, 1, 1, 0.4)
                c.fillText(s === 0 ? "fin" : "−" + (s >= 120 ? Math.round(s / 60) + " min" : s + " s"), x, g.altoPlot + 12)
            }
            // La referencia del estudio (lo que espera la mezcla), con lo acumulado.
            if (g.verAcumulado && g.referencia > 0)
                for (let f = 0; f < g.franjas; ++f) {
                    const i = g.modo === "apiladas" ? f : 0
                    const y = g.yDe(g.referencia, i)
                    c.strokeStyle = Qt.rgba(1, 0.23, 0.19, 0.55)
                    c.setLineDash([4, 4])
                    c.beginPath(); c.moveTo(g.izq, y); c.lineTo(width - g.der, y); c.stroke()
                    c.setLineDash([])
                }
            // Los eventos: una línea tenue que cruza las franjas.
            for (const e of g.eventos) {
                if (e.t < g.fin - g.ventanaMs || e.t > g.fin) continue
                const x = g.xDe(e.t)
                const col = g.colorEvento(e.tipo)
                c.strokeStyle = Qt.rgba(col.r, col.g, col.b, 0.25)
                c.setLineDash([2, 3])
                c.beginPath(); c.moveTo(x, 0); c.lineTo(x, g.altoPlot); c.stroke()
                c.setLineDash([])
            }
            // Las series.
            g.series.forEach((s, i) => {
                const d = (s.datos || []).filter(p => p.t >= g.fin - g.ventanaMs && p.t <= g.fin)
                if (d.length < 2) return
                c.beginPath()
                d.forEach((p, k) => { const x = g.xDe(p.t), y = g.yDe(g.valor(p), i); if (k === 0) c.moveTo(x, y); else c.lineTo(x, y) })
                c.strokeStyle = s.color
                c.lineWidth = 1.7
                c.stroke()
                // Relleno suave (en las apiladas, o si es una sola).
                if (g.modo === "apiladas" || g.series.length === 1) {
                    const base = g.yDe(0, i)
                    c.lineTo(g.xDe(d[d.length - 1].t), base)
                    c.lineTo(g.xDe(d[0].t), base)
                    c.closePath()
                    const gr = c.createLinearGradient(0, base - g.altoPlot / g.franjas, 0, base)
                    gr.addColorStop(0, Qt.rgba(s.color.r, s.color.g, s.color.b, 0.30))
                    gr.addColorStop(1, Qt.rgba(s.color.r, s.color.g, s.color.b, 0.02))
                    c.fillStyle = gr
                    c.fill()
                }
            })
            if (!g.series.length) {
                c.fillStyle = Qt.rgba(1, 1, 1, 0.35)
                c.textAlign = "center"
                c.font = "11px sans-serif"
                c.fillText("Tocá tarjetas del mapa para sumarlas a la línea de tiempo", width / 2, g.altoPlot / 2)
            }
            // La franja de eventos.
            const y0 = g.altoPlot + g.altoEje
            c.fillStyle = Qt.rgba(1, 1, 1, 0.03)
            c.fillRect(g.izq, y0, g.anchoPlot, g.altoEventos - 4)
            c.fillStyle = Qt.rgba(1, 1, 1, 0.4)
            c.textAlign = "right"
            c.fillText("eventos", g.izq - 6, y0 + 15)
        }
    }

    // Los marcadores de eventos (con su texto al pasar por encima).
    Repeater {
        model: g.eventos.filter(e => e.t >= g.fin - g.ventanaMs && e.t <= g.fin)
        delegate: Item {
            id: marca
            required property var modelData
            x: g.xDe(modelData.t) - 7
            y: g.altoPlot + g.altoEje + 4
            width: 14; height: 18
            Rectangle {
                anchors.centerIn: parent
                width: 9; height: 9
                rotation: 45
                color: g.colorEvento(marca.modelData.tipo)
                border.color: Qt.rgba(0, 0, 0, 0.5)
            }
            HoverHandler { id: hvm }
            Rectangle {
                visible: hvm.hovered
                z: 20
                x: Math.min(g.width - marca.x - width - 4, 10)
                y: -height - 6
                width: tm.implicitWidth + 14
                height: tm.implicitHeight + 8
                radius: 6
                color: Qt.rgba(0.08, 0.08, 0.1, 0.95)
                border.color: g.colorEvento(marca.modelData.tipo)
                Text {
                    id: tm
                    anchors.centerIn: parent
                    text: new Date(marca.modelData.t).toLocaleTimeString(Qt.locale(), "HH:mm:ss") + "  " + marca.modelData.texto
                    color: Theme.text; font.pixelSize: 10
                }
            }
        }
    }

    // La cruz que sigue al puntero, con el valor de cada serie en ese instante.
    HoverHandler { id: hv }
    TapHandler {
        // Un click en la línea de tiempo (no en un evento): marcar ese momento.
        onTapped: (ev) => { if (ev.position.x >= g.izq && ev.position.y < g.altoPlot + g.altoEje + g.altoEventos) g.pedirNota(g.tDe(ev.position.x)) }
    }
    readonly property real tCursor: hv.hovered && hv.point.position.x >= g.izq ? g.tDe(hv.point.position.x) : -1
    readonly property var lectura: {
        if (g.tCursor < 0) return []
        const out = []
        g.series.forEach((s, i) => {
            let mejor = null, dist = 1e18
            for (const p of (s.datos || [])) { const d = Math.abs(p.t - g.tCursor); if (d < dist) { dist = d; mejor = p } }
            if (mejor && dist < g.ventanaMs / 50) out.push({ titulo: s.titulo, color: s.color, v: g.valor(mejor), i: i })
        })
        return out
    }
    Rectangle {
        visible: g.tCursor >= 0 && hv.point.position.y < g.altoPlot
        x: hv.point.position.x
        y: 0
        width: 1
        height: g.altoPlot
        color: Qt.rgba(1, 1, 1, 0.3)
    }
    Repeater {
        model: g.tCursor >= 0 && hv.point.position.y < g.altoPlot ? g.lectura : []
        delegate: Rectangle {
            required property var modelData
            x: hv.point.position.x - 4
            y: g.yDe(modelData.v, modelData.i) - 4
            width: 8; height: 8; radius: 4
            color: modelData.color
            border.color: "white"
        }
    }
    Rectangle {
        visible: g.tCursor >= 0 && g.lectura.length > 0 && hv.point.position.y < g.altoPlot
        x: Math.min(g.width - width - 4, hv.point.position.x + 10)
        y: 6
        width: col.implicitWidth + 14
        height: col.implicitHeight + 10
        radius: 6
        color: Qt.rgba(0.08, 0.08, 0.1, 0.94)
        border.color: Qt.rgba(1, 1, 1, 0.15)
        Column {
            id: col
            anchors.centerIn: parent
            spacing: 1
            Text {
                text: g.tCursor > 0 ? new Date(g.tCursor).toLocaleTimeString(Qt.locale(), "HH:mm:ss") + " · " + (g.verAcumulado ? "desde la captura" : "agrega") : ""
                color: Theme.textDim; font.pixelSize: 9
            }
            Repeater {
                model: g.lectura
                delegate: Text {
                    required property var modelData
                    text: "● " + modelData.titulo + "  " + modelData.v.toFixed(1) + " ms"
                    color: modelData.color; font.pixelSize: 10
                }
            }
            Text { text: "click: marcar este momento"; color: Theme.textFaint; font.pixelSize: 8 }
        }
    }
}
