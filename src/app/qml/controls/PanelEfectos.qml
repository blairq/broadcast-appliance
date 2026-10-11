// Transparencia, borde difuso y sombra (o resplandor) de un elemento de la
// escena (SourcesModel::efectos / setEfectos; EfectosCapa en el motor). Todo
// cambia en vivo mientras se arrastra: el motor actualiza la capa en el lugar.
// Medidas en píxeles del lienzo; el ángulo, hacia dónde cae la sombra.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import BA

ColumnLayout {
    id: p
    property string fuente: ""
    property var ef: ({})
    readonly property var sombra: ef.sombra || ({})
    spacing: 6

    function leer() { p.ef = p.fuente ? Studio.sources.efectos(p.fuente) : ({}) }
    function poner(cambios) {
        if (!p.fuente) return
        if (Studio.sources.setEfectos(p.fuente, cambios)) leer()
    }
    function ponerSombra(cambios) { poner({ sombra: cambios }) }
    onFuenteChanged: leer()
    Component.onCompleted: leer()
    Connections {
        target: Studio.sources
        function onModelReset() { p.leer() }   // otra escena, o lo cambió el MCP
    }

    // Una fila «título · barra · valor» que aplica mientras se arrastra.
    component Fila: RowLayout {
        id: f
        property string titulo
        property string ayuda: ""
        property real desde: 0
        property real hasta: 100
        property real paso: 1
        property real valor: 0
        property string unidad: " px"
        property real escala: 1     // lo que se muestra = valor × escala
        signal cambio(real v)
        spacing: 8
        Text {
            text: f.titulo; color: Theme.textDim; font.pixelSize: 12
            Layout.preferredWidth: 110; elide: Text.ElideRight
            ToolTip.visible: hf.hovered && f.ayuda !== ""; ToolTip.text: f.ayuda; ToolTip.delay: 300
            HoverHandler { id: hf }
        }
        Slider {
            id: sl
            Layout.fillWidth: true
            from: f.desde; to: f.hasta; stepSize: f.paso; snapMode: Slider.SnapAlways
            Binding on value { when: !sl.pressed; value: f.valor }
            onMoved: f.cambio(value)
        }
        Text {
            Layout.preferredWidth: 52; horizontalAlignment: Text.AlignRight
            text: Math.round(sl.value * f.escala) + f.unidad
            color: Theme.text; font { pixelSize: 12; family: Theme.mono }
        }
    }

    Text {
        text: "EFECTOS"
        color: Theme.accentSoft
        font { family: Theme.font; pixelSize: 11; weight: Font.Bold; letterSpacing: 1.2 }
    }
    Fila {
        Layout.fillWidth: true
        titulo: "Opacidad"; ayuda: "Transparencia de todo el elemento (y de su sombra)"
        desde: 0; hasta: 1; paso: 0.01; escala: 100; unidad: " %"
        valor: p.ef.opacidad !== undefined ? p.ef.opacidad : 1
        onCambio: (v) => p.poner({ opacidad: v })
    }
    Fila {
        Layout.fillWidth: true
        titulo: "Borde difuso"; ayuda: "Cuántos píxeles del borde del elemento se funden a transparente"
        desde: 0; hasta: 200
        valor: p.ef.borde || 0
        onCambio: (v) => p.poner({ borde: v })
    }
    Fila {
        Layout.fillWidth: true
        titulo: "Esquinas"; ayuda: "Esquinas redondeadas: el radio (la sombra las sigue)"
        desde: 0; hasta: 300
        valor: p.ef.esquinas || 0
        onCambio: (v) => p.poner({ esquinas: v })
    }

    RowLayout {
        Layout.fillWidth: true
        Layout.topMargin: 4
        spacing: 8
        Text { text: "Sombra"; color: Theme.text; font.pixelSize: 13; Layout.preferredWidth: 110 }
        Toggle { checked: !!p.sombra.activa; onToggled: (w) => p.ponerSombra({ activa: w }) }
        Item { Layout.fillWidth: true }
        // Atajos: cambian todo de una (después se retoca).
        Repeater {
            model: [
                { t: "Suave", s: { activa: true, color: "#000000", alfa: 0.6, difuminado: 24, distancia: 10, angulo: 45, tamano: 0 } },
                { t: "Dura", s: { activa: true, color: "#000000", alfa: 0.85, difuminado: 2, distancia: 8, angulo: 45, tamano: 0 } },
                { t: "Resplandor", s: { activa: true, color: "#FFFFFF", alfa: 0.8, difuminado: 30, distancia: 0, angulo: 0, tamano: 4 } }
            ]
            delegate: GlassButton {
                required property var modelData
                compact: true
                text: modelData.t
                onClicked: p.ponerSombra(modelData.s)
            }
        }
    }
    ColumnLayout {
        Layout.fillWidth: true
        visible: !!p.sombra.activa
        spacing: 6
        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            Text { text: "Color"; color: Theme.textDim; font.pixelSize: 12; Layout.preferredWidth: 110 }
            Repeater {
                model: ["#000000", "#FFFFFF", "#202A44", "#FF6B2C", "#2C8CFF", "#FFD400"]
                delegate: Rectangle {
                    required property string modelData
                    width: 20; height: 20; radius: 4
                    color: modelData
                    border.width: (p.sombra.color || "").toUpperCase().endsWith(modelData.substring(1)) ? 2 : 1
                    border.color: border.width > 1 ? Theme.accent : Qt.rgba(1, 1, 1, 0.3)
                    MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: p.ponerSombra({ color: parent.modelData }) }
                }
            }
            TextField {
                Layout.preferredWidth: 84
                text: p.sombra.color || "#000000"
                font { family: Theme.mono; pixelSize: 12 }
                validator: RegularExpressionValidator { regularExpression: /#?[0-9A-Fa-f]{6}([0-9A-Fa-f]{2})?/ }
                onEditingFinished: p.ponerSombra({ color: text.startsWith("#") ? text : "#" + text })
            }
        }
        Fila {
            Layout.fillWidth: true
            titulo: "Opacidad"; ayuda: "Qué tan oscura (o fuerte) es la sombra"
            desde: 0; hasta: 1; paso: 0.01; escala: 100; unidad: " %"
            valor: p.sombra.alfa !== undefined ? p.sombra.alfa : 0.6
            onCambio: (v) => p.ponerSombra({ alfa: v })
        }
        Fila {
            Layout.fillWidth: true
            titulo: "Difuminado"; ayuda: "Radio del difuminado: 0 = sombra recortada, más = más suave"
            desde: 0; hasta: 120
            valor: p.sombra.difuminado !== undefined ? p.sombra.difuminado : 20
            onCambio: (v) => p.ponerSombra({ difuminado: v })
        }
        Fila {
            Layout.fillWidth: true
            titulo: "Distancia"; ayuda: "Cuánto se separa la sombra del elemento (0 = alrededor, como un resplandor)"
            desde: 0; hasta: 150
            valor: p.sombra.distancia !== undefined ? p.sombra.distancia : 10
            onCambio: (v) => p.ponerSombra({ distancia: v })
        }
        Fila {
            Layout.fillWidth: true
            titulo: "Ángulo"; ayuda: "Hacia dónde cae: 0° derecha, 90° abajo, 180° izquierda, 270° arriba"
            desde: 0; hasta: 359; unidad: "°"
            valor: (((p.sombra.angulo !== undefined ? p.sombra.angulo : 45) % 360) + 360) % 360
            onCambio: (v) => p.ponerSombra({ angulo: v })
        }
        Fila {
            Layout.fillWidth: true
            titulo: "Tamaño"; ayuda: "Cuánto más grande (o más chica) que el elemento es la sombra"
            desde: -50; hasta: 80
            valor: p.sombra.tamano || 0
            onCambio: (v) => p.ponerSombra({ tamano: v })
        }
    }
}
