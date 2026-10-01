// El dibujo de un filtro de audio: lo que hace, como gráfico. Generado acá
// (no son capturas de lsp-plug.in, que tienen todos los derechos reservados):
// cada tipo dibuja su curva característica con el color de su categoría.
import QtQuick
import BA

Canvas {
    id: c
    property string tipo: "eq"
    property color color: Theme.accent
    property bool grande: false
    onTipoChanged: requestPaint()
    onColorChanged: requestPaint()
    onWidthChanged: requestPaint()
    onHeightChanged: requestPaint()

    onPaint: {
        const g = getContext("2d")
        const w = width, h = height
        g.reset()
        // Fondo: grilla tenue, como la pantalla de un analizador.
        g.fillStyle = "#101014"
        g.fillRect(0, 0, w, h)
        g.strokeStyle = "rgba(255,255,255,0.06)"
        g.lineWidth = 1
        for (let i = 1; i < 6; ++i) { g.beginPath(); g.moveTo(w * i / 6, 0); g.lineTo(w * i / 6, h); g.stroke() }
        for (let j = 1; j < 4; ++j) { g.beginPath(); g.moveTo(0, h * j / 4); g.lineTo(w, h * j / 4); g.stroke() }

        const col = c.color
        const rgba = (a) => "rgba(" + Math.round(col.r * 255) + "," + Math.round(col.g * 255) + "," + Math.round(col.b * 255) + "," + a + ")"
        g.lineWidth = c.grande ? 3 : 2
        g.strokeStyle = rgba(1)
        g.fillStyle = rgba(0.18)
        const m = 8
        const X = (t) => m + t * (w - 2 * m)          // t en 0..1
        const Y = (v) => h - m - v * (h - 2 * m)      // v en 0..1

        function curva(f, relleno) {
            g.beginPath()
            for (let i = 0; i <= 80; ++i) { const t = i / 80; i === 0 ? g.moveTo(X(t), Y(f(t))) : g.lineTo(X(t), Y(f(t))) }
            if (relleno) { g.lineTo(X(1), Y(0)); g.lineTo(X(0), Y(0)); g.closePath(); g.fill(); }
            g.beginPath()
            for (let i = 0; i <= 80; ++i) { const t = i / 80; i === 0 ? g.moveTo(X(t), Y(f(t))) : g.lineTo(X(t), Y(f(t))) }
            g.stroke()
        }
        function diagonal() {
            g.save(); g.strokeStyle = "rgba(255,255,255,0.15)"; g.setLineDash([4, 4])
            g.beginPath(); g.moveTo(X(0), Y(0)); g.lineTo(X(1), Y(1)); g.stroke(); g.restore()
        }
        const gauss = (t, c0, s) => Math.exp(-((t - c0) * (t - c0)) / (2 * s * s))

        switch (c.tipo) {
        case "eq":   // pasa-altos + una campana + un estante
            curva(t => 0.5 - 0.45 * Math.exp(-t * 14) + 0.22 * gauss(t, 0.45, 0.08) - 0.12 * gauss(t, 0.72, 0.05) + 0.08 * t, true)
            for (const p of [[0.45, 0.5 + 0.22], [0.72, 0.5 - 0.12]]) { g.beginPath(); g.arc(X(p[0]), Y(p[1]), 4, 0, 2 * Math.PI); g.fillStyle = rgba(1); g.fill() }
            break
        case "hpf":
            curva(t => Math.min(0.62, 0.62 * Math.pow(Math.min(1, t / 0.28), 2.4)), true)
            break
        case "geq": {
            const v = [0.45, 0.55, 0.62, 0.5, 0.42, 0.48, 0.58, 0.66, 0.6, 0.5, 0.44, 0.52, 0.57, 0.5, 0.46, 0.4]
            const bw = (w - 2 * m) / v.length
            for (let i = 0; i < v.length; ++i) {
                g.fillStyle = rgba(0.75); g.fillRect(X(i / v.length) + 2, Y(v[i]) - 3, bw - 4, 6)
                g.fillStyle = rgba(0.15); g.fillRect(X(i / v.length) + bw / 2 - 1, Y(1), 2, Y(0) - Y(1))
            }
            break
        }
        case "comp": diagonal(); curva(t => t < 0.55 ? t : 0.55 + (t - 0.55) / 3.5, false); break
        case "lim":
            diagonal(); curva(t => Math.min(t, 0.72), false)
            g.save(); g.strokeStyle = "rgba(239,68,68,0.9)"; g.setLineDash([6, 4]); g.beginPath(); g.moveTo(X(0), Y(0.72)); g.lineTo(X(1), Y(0.72)); g.stroke(); g.restore()
            break
        case "gate": diagonal(); curva(t => t < 0.4 ? 0.02 : t, false); break
        case "exp":  diagonal(); curva(t => t < 0.45 ? Math.max(0, 0.45 - (0.45 - t) * 2.2) : t, false); break
        case "deess":
            curva(t => 0.55 - 0.08 * Math.sin(t * 9) * (1 - t) - 0.3 * gauss(t, 0.78, 0.06), true)
            break
        case "mb": {
            const bandas = [[0, 0.25, 0.55], [0.25, 0.5, 0.42], [0.5, 0.75, 0.62], [0.75, 1, 0.48]]
            const tonos = [1, 0.75, 0.55, 0.35]
            for (let i = 0; i < 4; ++i) {
                g.fillStyle = rgba(0.12 + 0.12 * tonos[i]); g.fillRect(X(bandas[i][0]) + 1, Y(bandas[i][2]), X(bandas[i][1]) - X(bandas[i][0]) - 2, Y(0) - Y(bandas[i][2]))
                g.strokeStyle = rgba(0.9); g.beginPath(); g.moveTo(X(bandas[i][0]) + 1, Y(bandas[i][2])); g.lineTo(X(bandas[i][1]) - 1, Y(bandas[i][2])); g.stroke()
            }
            break
        }
        case "loud": curva(t => 0.3 + 0.35 * (1 - Math.exp(-t * 4)) + 0.04 * Math.sin(t * 30) * Math.exp(-t * 3), true); break
        case "delay":
            for (let i = 0; i < 7; ++i) { const a = Math.pow(0.68, i); g.fillStyle = rgba(0.35 + 0.6 * a); g.fillRect(X(0.06 + i * 0.13), Y(a * 0.85), 6, Y(0) - Y(a * 0.85)) }
            break
        case "reverb":
            for (let i = 0; i < 90; ++i) { const t = i / 90; const a = Math.exp(-t * 4) * (0.6 + 0.4 * Math.random()); g.fillStyle = rgba(0.6); g.fillRect(X(t), Y(a * 0.85), Math.max(1, (w - 2 * m) / 90 - 1), Y(0) - Y(a * 0.85)) }
            break
        case "mod":
            curva(t => 0.5 + 0.3 * Math.sin(t * Math.PI * 4), false)
            g.strokeStyle = rgba(0.4); curva(t => 0.5 + 0.3 * Math.sin(t * Math.PI * 4 + 1.2), false)
            break
        case "spec":
            for (let i = 0; i < 40; ++i) { const t = i / 40; const a = Math.max(0.05, 0.75 - t * 0.5 + 0.12 * Math.sin(i * 1.7)); g.fillStyle = rgba(0.3 + 0.6 * a); g.fillRect(X(t) + 1, Y(a), (w - 2 * m) / 40 - 2, Y(0) - Y(a)) }
            break
        case "osc": curva(t => 0.5 + 0.32 * Math.sin(t * Math.PI * 6) * (0.6 + 0.4 * Math.sin(t * 3)), false); break
        case "gen":
            curva(t => 0.5 + 0.3 * Math.sin(t * Math.PI * 6), false)
            g.strokeStyle = rgba(0.45)
            curva(t => (Math.floor(t * 6) % 2 === 0) ? 0.78 : 0.22, false)
            break
        case "pads":
            for (let i = 0; i < 4; ++i) for (let j = 0; j < 2; ++j) {
                g.fillStyle = rgba((i + j) % 3 === 0 ? 0.85 : 0.3)
                const pw = (w - 2 * m - 30) / 4, ph = (h - 2 * m - 10) / 2
                g.fillRect(m + 6 + i * (pw + 6), m + 2 + j * (ph + 6), pw, ph)
            }
            break
        default: { // utilidades: un patchbay
            g.lineWidth = 2
            for (const p of [[0.1, 0.25, 0.9, 0.7], [0.1, 0.5, 0.9, 0.3], [0.1, 0.75, 0.9, 0.5]]) {
                g.strokeStyle = rgba(0.8); g.beginPath(); g.moveTo(X(p[0]), Y(p[1]))
                g.bezierCurveTo(X(0.5), Y(p[1]), X(0.5), Y(p[3]), X(p[2]), Y(p[3])); g.stroke()
                g.fillStyle = rgba(1); g.beginPath(); g.arc(X(p[0]), Y(p[1]), 4, 0, 2 * Math.PI); g.fill()
                g.beginPath(); g.arc(X(p[2]), Y(p[3]), 4, 0, 2 * Math.PI); g.fill()
            }
        }
        }
    }
}
