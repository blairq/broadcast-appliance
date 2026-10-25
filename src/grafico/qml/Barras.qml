// Barras con datos, para los gráficos animados.
//
//   Barras { width: 900; height: 360; datos: raiz.votos; orientacion: "horizontal" }
//
// ENTRAN creciendo de a una (`escalonado` ms entre una y la siguiente), con el
// número contando a la par. CAMBIAN: con `anteriores` (los datos de antes) no
// entran: arrancan con esos valores y crecen o se achican hasta los nuevos.
// Como la torta, el tiempo es el del renderizador (determinista).
import QtQuick
import BA.Grafico

Item {
    id: b

    property var datos: []
    property var anteriores: null
    /// vertical (columnas) u horizontal (filas).
    property string orientacion: "vertical"
    /// El valor de la barra llena. 0 = el mayor de los datos (y de los de antes).
    property real maximo: 0
    property int aparece: 0
    property int duracion: 700
    property int escalonado: 120
    property int duracionCambio: 700
    /// valor, porcentaje, ambos o nada.
    property string mostrar: "valor"
    property int decimales: 0
    property string sufijo: ""
    property color colorTexto: "white"
    property color colorEtiquetas: "#c9d1d9"
    property int tamanoTexto: 22
    /// Horizontal: qué parte del ancho es para los nombres.
    property real anchoEtiquetas: 0.3
    /// Lo que se deja entre barra y barra, como parte del lugar de cada una.
    property real separacion: 0.28
    property var paleta: ["#1f6fb2", "#e8a33d", "#2e7d32", "#c62828", "#7b4fb8", "#00897b", "#d81b60", "#6d7a86"]

    readonly property var lista: Datos.normalizar(datos)
    readonly property var antes: anteriores ? Datos.normalizar(anteriores) : null
    property real mezcla: antes ? 0 : 1
    NumberAnimation {
        running: !!b.antes
        target: b; property: "mezcla"; from: 0; to: 1; duration: b.duracionCambio; easing.type: Easing.InOutCubic
    }

    function valorAntes(i) {
        if (!antes) return lista[i].valor
        for (let k = 0; k < antes.length; ++k)
            if (antes[k].etiqueta === lista[i].etiqueta) return antes[k].valor
        return 0
    }
    readonly property var valores: {
        const out = []
        for (let i = 0; i < lista.length; ++i) {
            const a = valorAntes(i)
            out.push(a + (lista[i].valor - a) * mezcla)
        }
        return out
    }
    readonly property real tope: {
        if (maximo > 0) return maximo
        let m = 0
        for (let i = 0; i < lista.length; ++i) m = Math.max(m, lista[i].valor, valorAntes(i))
        return m > 0 ? m : 1
    }
    readonly property real total: {
        let s = 0
        for (let i = 0; i < valores.length; ++i) s += Math.max(0, valores[i])
        return s
    }
    readonly property bool vertical: orientacion !== "horizontal"
    readonly property int n: Math.max(1, lista.length)
    // El lugar de las etiquetas y los números, fuera de la barra.
    readonly property real renglon: tamanoTexto * 1.5
    readonly property real zonaEtiquetas: vertical ? renglon : width * anchoEtiquetas
    readonly property real zonaNumero: vertical ? renglon : tamanoTexto * 4
    readonly property real largo: vertical ? height - zonaEtiquetas - zonaNumero : width - zonaEtiquetas - zonaNumero
    readonly property real paso: (vertical ? width : height) / n

    function colorDe(i) { return lista[i].color ? lista[i].color : paleta[i % paleta.length] }
    function texto(v) {
        const pc = total > 0 ? (100 * Math.max(0, v) / total).toFixed(decimales) + "%" : ""
        const val = v.toFixed(decimales) + sufijo
        if (mostrar === "porcentaje") return pc
        if (mostrar === "ambos") return val + "  " + pc
        if (mostrar === "nada") return ""
        return val
    }

    Repeater {
        model: b.lista.length
        delegate: Item {
            id: fila
            required property int index
            /// 0..1: cuánto creció al entrar (1 de entrada si es un cambio).
            property real p: b.antes ? 1 : 0
            SequentialAnimation {
                running: !b.antes
                PauseAnimation { duration: Math.max(0, b.aparece + fila.index * b.escalonado) }
                NumberAnimation { target: fila; property: "p"; from: 0; to: 1; duration: b.duracion; easing.type: Easing.OutCubic }
            }
            readonly property real v: b.valores[index] * p
            readonly property real l: Math.max(0, Math.min(1, v / b.tope)) * b.largo
            readonly property real grueso: b.paso * (1 - b.separacion)

            x: b.vertical ? b.paso * index + (b.paso - grueso) / 2 : 0
            y: b.vertical ? 0 : b.paso * index + (b.paso - grueso) / 2
            width: b.vertical ? grueso : b.width
            height: b.vertical ? b.height : grueso

            Rectangle {
                color: b.colorDe(fila.index)
                radius: 3
                x: b.vertical ? 0 : b.zonaEtiquetas
                y: b.vertical ? b.height - b.zonaEtiquetas - fila.l : 0
                width: b.vertical ? fila.grueso : fila.l
                height: b.vertical ? fila.l : fila.grueso
            }
            Text {
                // El nombre: abajo de la columna o a la izquierda de la fila.
                text: b.lista[fila.index].etiqueta
                color: b.colorEtiquetas
                elide: Text.ElideRight
                font { family: "Noto Sans"; pixelSize: b.tamanoTexto * 0.8 }
                renderType: Text.QtRendering
                horizontalAlignment: b.vertical ? Text.AlignHCenter : Text.AlignRight
                verticalAlignment: Text.AlignVCenter
                width: b.vertical ? b.paso : b.zonaEtiquetas - 14
                height: b.vertical ? b.zonaEtiquetas : fila.grueso
                x: b.vertical ? (fila.grueso - b.paso) / 2 : 0
                y: b.vertical ? b.height - b.zonaEtiquetas : 0
                opacity: Math.min(1, fila.p * 3)
            }
            Text {
                // El número, en la punta: cuenta mientras la barra crece.
                text: b.texto(fila.v)
                visible: b.mostrar !== "nada"
                color: b.colorTexto
                font { family: "Noto Sans"; pixelSize: b.tamanoTexto; weight: Font.DemiBold }
                renderType: Text.QtRendering
                horizontalAlignment: b.vertical ? Text.AlignHCenter : Text.AlignLeft
                verticalAlignment: Text.AlignVCenter
                width: b.vertical ? b.paso : b.zonaNumero
                height: b.vertical ? b.zonaNumero : fila.grueso
                x: b.vertical ? (fila.grueso - b.paso) / 2 : b.zonaEtiquetas + fila.l + 10
                y: b.vertical ? b.height - b.zonaEtiquetas - fila.l - b.zonaNumero : 0
                opacity: Math.min(1, fila.p * 3)
            }
        }
    }
}
