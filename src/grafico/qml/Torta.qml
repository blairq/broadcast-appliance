// Torta (o anillo, con grosor > 0) con datos, para los gráficos animados.
//
//   Torta { width: 420; height: 420; datos: raiz.votos }
//
// ENTRA girando: las porciones crecen juntas desde arriba, en sentido horario.
// CAMBIA: con `anteriores` (los datos de antes) no entra, arranca dibujada con
// esos valores y las porciones se mueven a los nuevos. El sistema le pasa
// `anteriores` al gráfico cuando le cambia los valores al aire (ver
// docs/funciones/GRAFICOS.md, «Cambiar al aire»).
//
// Todo el tiempo sale de animaciones de Qt, o sea del reloj del renderizador:
// el cuadro de los 380 ms es siempre el mismo.
import QtQuick
import QtQuick.Shapes
import BA.Grafico

Item {
    id: t

    /// [{etiqueta, valor, color}] o cualquier cosa que entienda Datos.normalizar.
    property var datos: []
    /// Los datos de antes de un cambio (null = entra de cero).
    property var anteriores: null
    /// ms hasta que empieza a girar, y lo que tarda.
    property int aparece: 0
    property int duracion: 900
    /// Lo que tarda en moverse de los valores de antes a los nuevos.
    property int duracionCambio: 700
    /// 0 = torta llena; más = anillo de ese grosor en píxeles.
    property real grosor: 0
    property bool etiquetas: true
    /// porcentaje, valor, ambos o nada.
    property string mostrar: "porcentaje"
    property int decimales: 0
    property string sufijo: ""
    property color colorTexto: "white"
    property int tamanoTexto: 22
    property var paleta: ["#1f6fb2", "#e8a33d", "#2e7d32", "#c62828", "#7b4fb8", "#00897b", "#d81b60", "#6d7a86"]

    readonly property var lista: Datos.normalizar(datos)
    readonly property var antes: anteriores ? Datos.normalizar(anteriores) : null

    /// 0..1: cuánto de la vuelta se ve (la entrada).
    property real progreso: antes ? 1 : 0
    /// 0..1: de los valores de antes a los nuevos.
    property real mezcla: antes ? 0 : 1

    SequentialAnimation {
        running: !t.antes
        PauseAnimation { duration: Math.max(0, t.aparece) }
        NumberAnimation { target: t; property: "progreso"; from: 0; to: 1; duration: t.duracion; easing.type: Easing.OutCubic }
    }
    NumberAnimation {
        running: !!t.antes
        target: t; property: "mezcla"; from: 0; to: 1; duration: t.duracionCambio; easing.type: Easing.InOutCubic
    }

    /// El valor de antes de la porción i: la de la misma etiqueta (o la misma
    /// posición); una porción nueva arranca en cero.
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
            out.push(Math.max(0, a + (lista[i].valor - a) * mezcla))
        }
        return out
    }
    readonly property real total: {
        let s = 0
        for (let i = 0; i < valores.length; ++i) s += valores[i]
        return s
    }
    function colorDe(i) { return lista[i].color ? lista[i].color : paleta[i % paleta.length] }
    function inicioDe(i) {
        let s = 0
        for (let k = 0; k < i; ++k) s += valores[k]
        return total > 0 ? s / total : 0
    }
    function texto(i) {
        const v = valores[i]
        const pc = total > 0 ? (100 * v / total).toFixed(decimales) + "%" : ""
        const val = v.toFixed(decimales) + sufijo
        if (mostrar === "valor") return val
        if (mostrar === "ambos") return pc + "\n" + val
        if (mostrar === "nada") return ""
        return pc
    }

    readonly property real lado: Math.min(width, height)
    readonly property real radio: lado / 2
    readonly property real cx: width / 2
    readonly property real cy: height / 2

    Repeater {
        model: t.lista.length
        delegate: Shape {
            id: porcion
            required property int index
            anchors.fill: parent
            // Sin MSAA en el renderizador: el de curvas suaviza el borde solo.
            preferredRendererType: Shape.CurveRenderer
            readonly property real desde: -90 + 360 * t.progreso * t.inicioDe(index)
            readonly property real barrido: t.total > 0 ? 360 * t.progreso * t.valores[index] / t.total : 0
            visible: barrido > 0.05
            ShapePath {
                fillColor: t.grosor > 0 ? "transparent" : t.colorDe(porcion.index)
                strokeColor: t.grosor > 0 ? t.colorDe(porcion.index) : "transparent"
                strokeWidth: t.grosor > 0 ? t.grosor : 0
                capStyle: ShapePath.FlatCap
                startX: t.cx
                startY: t.cy
                PathAngleArc {
                    centerX: t.cx; centerY: t.cy
                    radiusX: t.grosor > 0 ? t.radio - t.grosor / 2 : t.radio
                    radiusY: radiusX
                    startAngle: porcion.desde
                    sweepAngle: porcion.barrido
                    moveToStart: t.grosor > 0
                }
            }
        }
    }

    // Los rótulos de cada porción, en el medio de su arco. Aparecen cuando la
    // vuelta ya se ve casi entera (antes se apilarían arriba).
    Repeater {
        model: t.etiquetas && t.mostrar !== "nada" ? t.lista.length : 0
        delegate: Text {
            required property int index
            readonly property real medio: (-90 + 360 * t.progreso * (t.inicioDe(index) + (t.total > 0 ? t.valores[index] / t.total / 2 : 0))) * Math.PI / 180
            readonly property real r: t.grosor > 0 ? t.radio - t.grosor / 2 : t.radio * 0.62
            x: t.cx + Math.cos(medio) * r - width / 2
            y: t.cy + Math.sin(medio) * r - height / 2
            visible: t.total > 0 && t.valores[index] / t.total > 0.04
            opacity: Math.max(0, (t.progreso - 0.6) / 0.4)
            text: t.texto(index)
            horizontalAlignment: Text.AlignHCenter
            color: t.colorTexto
            font { family: "Noto Sans"; pixelSize: t.tamanoTexto; weight: Font.DemiBold }
            renderType: Text.QtRendering
            style: Text.Raised
            styleColor: Qt.rgba(0, 0, 0, 0.35)
        }
    }
}
