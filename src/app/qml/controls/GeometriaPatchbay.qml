// La geometría de un patchbay: dónde está cada tarjeta y cada puerto, a qué entrada va
// un cable que se suelta, por dónde pasa la curva y la grilla donde encajan las
// tarjetas. UNA sola para los dos patchbays (el genérico, Patchbay: el grafo de video y
// el estudio de audio; y el del rack de filtros de audio, AudioPage, con sus puertos
// estéreo de dos puntos y sus tarjetas que se agrandan): antes eran dos copias.
//
// Los puertos de un nodo son `entradas`/`salidas`: textos (el rack de audio, con la
// información de cada uno en `puertosEntrada`/`puertosSalida`) u objetos {id, tipo,
// pedido} (el grafo). Un puerto con `canales: 2` se dibuja como dos puntos (L arriba,
// R abajo, `separacion(id)` del centro); `ch`: -1 el puerto entero, 0 L, 1 R.
// Mientras se arrastra una tarjeta, `mover` la anota (los cables la siguen en vivo) y
// `soltar` la olvida; la posición de verdad es la del modelo.
import QtQuick

QtObject {
    id: geo
    property var nodos: []
    property real nodeW: 180
    property real headH: 34
    property real grid: 20
    /// El alto de una fila de puertos de un nodo (por id: las tarjetas del rack se agrandan).
    property var altoFila: function(id) { return 24 }
    /// Lo que va debajo de los puertos (el cuerpo de la tarjeta).
    property var altoCuerpo: function(n) { return 40 }
    /// Cuánto se separan del centro los puntos L/R de un puerto estéreo.
    property var separacion: function(id) { return 0 }
    /// Las posiciones mientras se arrastra (id -> punto).
    property var vivas: ({})

    function snap(v) { return Math.round(v / grid) * grid }
    function nodeOf(id) {
        for (let i = 0; i < nodos.length; ++i) if (nodos[i].id === id) return nodos[i]
        return null
    }
    function idDe(pt) { return typeof pt === "string" ? pt : pt.id }
    /// Los datos del puerto `i` (canales, tipo, pedido...).
    function infoDe(n, isIn, i) {
        const lista = (isIn ? n.entradas : n.salidas) || []
        const pt = lista[i]
        if (pt !== undefined && typeof pt !== "string") return pt
        return ((isIn ? n.puertosEntrada : n.puertosSalida) || [])[i] || ({})
    }
    function filas(n) { return Math.max((n.entradas || []).length, (n.salidas || []).length, 1) }
    function nodeH(n) { return headH + filas(n) * altoFila(n.id) + altoCuerpo(n) }
    function posOf(n) {
        const p = vivas[n.id]
        return p ? p : Qt.point(n.x, n.y)
    }
    function mover(id, x, y) { const v = Object.assign({}, vivas); v[id] = Qt.point(x, y); vivas = v }
    function soltar(id) { if (vivas[id] === undefined) return; const v = Object.assign({}, vivas); delete v[id]; vivas = v }
    function portPos(id, port, isIn, ch) {
        const n = nodeOf(id)
        if (!n) return Qt.point(0, 0)
        const lista = (isIn ? n.entradas : n.salidas) || []
        let i = 0
        for (let k = 0; k < lista.length; ++k) if (idDe(lista[k]) === port) i = k
        const p = posOf(n)
        const info = infoDe(n, isIn, i)
        const off = separacion(id)
        const dy = (info.canales === 2 && ch !== undefined && ch >= 0) ? (ch === 0 ? -off : off) : 0
        const h = altoFila(id)
        return Qt.point(p.x + (isIn ? 0 : nodeW), p.y + headH + i * h + h / 2 + dy)
    }
    /// La entrada a la que iría un cable soltado en (x, y): el puerto más cercano (a
    /// menos de `margen`; en uno estéreo, el canal más cercano) que `acepta(info)`, o,
    /// soltado sobre una tarjeta, su primera entrada que acepta (la última libre `pedido`,
    /// si hay). Así no hay que apuntarle justo al puntito. {node, port, ch} o null.
    function inputAt(x, y, acepta, margen) {
        acepta = acepta || function(info) { return true }
        let best = null, bd = margen || 40
        for (let i = 0; i < nodos.length; ++i) {
            const n = nodos[i]
            const lista = n.entradas || []
            for (let k = 0; k < lista.length; ++k) {
                const info = infoDe(n, true, k)
                if (!acepta(info)) continue
                for (const ch of (info.canales === 2 ? [-1, 0, 1] : [-1])) {
                    const q = portPos(n.id, idDe(lista[k]), true, ch)
                    const d = Math.hypot(q.x - x, q.y - y)
                    if (d < bd) { bd = d; best = { node: n.id, port: idDe(lista[k]), ch: ch } }
                }
            }
        }
        if (best) return best
        for (let i = 0; i < nodos.length; ++i) {
            const n = nodos[i]
            const lista = n.entradas || []
            let libre = -1
            for (let k = 0; k < lista.length; ++k) {
                const info = infoDe(n, true, k)
                if (!acepta(info)) continue
                if (libre < 0 || info.pedido) libre = k
            }
            if (libre < 0) continue
            const p = posOf(n)
            if (x >= p.x && x <= p.x + nodeW && y >= p.y && y <= p.y + nodeH(n))
                return { node: n.id, port: idDe(lista[libre]), ch: -1 }
        }
        return null
    }
    /// Cable curvo: sale hacia la derecha y entra por la izquierda. Los puntos de
    /// control se separan según la distancia, así la curva no se aplasta.
    function cableCtl(a, b) { return Math.max(40, Math.min(160, Math.abs(b.x - a.x) / 2 + 20)) }
    /// Punto medio REAL de la curva (Bézier cúbica en t = 0.5): la ✕ tiene que caer
    /// sobre el cable, no en el medio de una recta entre las dos puntas.
    function cableMid(a, b) {
        const d = cableCtl(a, b)
        return Qt.point((a.x + 3 * (a.x + d) + 3 * (b.x - d) + b.x) / 8, (a.y + 3 * a.y + 3 * b.y + b.y) / 8)
    }
}
