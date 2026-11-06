// Un gráfico en vivo de las pruebas (Pruebas: prueba de sala, capacidad, recibir): varias
// líneas sobre la misma escala, un punto por segundo, con su leyenda y el último valor.
// Solo dibuja lo que le pasan: `serie` es la lista de puntos del modelo ({t, clave: valor…});
// los valores negativos (no se sabe) cortan la línea.
import QtQuick
import QtQuick.Layouts
import BA

ColumnLayout {
    id: g
    property string titulo: ""
    property string unidad: ""
    /// [{clave, titulo, color}]
    property var lineas: []
    property var serie: []
    /// Techo fijo (100 para %); 0 = el máximo de lo que hay (redondeado).
    property real tope: 0
    property int alto: 110
    spacing: 4

    readonly property real techo: {
        if (g.tope > 0) return g.tope
        let m = 1
        for (const p of g.serie) for (const l of g.lineas) { const v = p[l.clave]; if (v !== undefined && v > m) m = v }
        const paso = m <= 10 ? 2 : m <= 50 ? 10 : m <= 200 ? 50 : 100
        return Math.ceil(m * 1.1 / paso) * paso
    }
    function ultimo(clave) {
        for (let i = g.serie.length - 1; i >= 0; --i) { const v = g.serie[i][clave]; if (v !== undefined && v >= 0) return v }
        return -1
    }

    Text { text: g.titulo; color: Theme.textDim; font.pixelSize: 11 }
    Rectangle {
        Layout.fillWidth: true
        Layout.preferredHeight: g.alto
        color: "transparent"
        border.color: Theme.glassBorder
        radius: 6
        Canvas {
            id: lienzo
            anchors { fill: parent; margins: 6 }
            onPaint: {
                const c = getContext("2d")
                c.reset()
                const n = g.serie.length
                if (n < 2) return
                const w = width, h = height
                c.lineWidth = 1.5
                for (const l of g.lineas) {
                    c.strokeStyle = l.color
                    c.beginPath()
                    let abierto = false
                    for (let i = 0; i < n; ++i) {
                        const v = g.serie[i][l.clave]
                        if (v === undefined || v < 0) { abierto = false; continue }
                        const x = i * w / (n - 1)
                        const y = h - Math.min(1, v / g.techo) * h
                        if (abierto) c.lineTo(x, y); else { c.moveTo(x, y); abierto = true }
                    }
                    c.stroke()
                }
            }
            Connections { target: g; function onSerieChanged() { lienzo.requestPaint() } }
            onWidthChanged: requestPaint()
        }
        Text {
            anchors { right: parent.right; top: parent.top; margins: 4 }
            text: g.techo + " " + g.unidad
            color: Theme.textFaint; font.pixelSize: 10
        }
    }
    Flow {
        Layout.fillWidth: true
        spacing: 10
        Repeater {
            model: g.lineas
            delegate: Row {
                required property var modelData
                spacing: 4
                Rectangle { width: 10; height: 3; radius: 1; color: modelData.color; anchors.verticalCenter: parent.verticalCenter }
                Text {
                    readonly property real v: g.ultimo(modelData.clave)
                    text: modelData.titulo + " " + (v >= 0 ? (v < 10 ? v.toFixed(1) : Math.round(v)) : "–") + " " + g.unidad
                    color: Theme.textDim; font.pixelSize: 10
                }
            }
        }
    }
}
