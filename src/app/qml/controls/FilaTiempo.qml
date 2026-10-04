// Una fila «título · barra · valor» para los tiempos y umbrales de audio
// (cortes de la transcripción, espera y máximo de una traducción): todos se
// ven y se comportan igual. El valor sigue a `valor` mientras no se arrastra
// (si lo cambia otro —el MCP, otra pantalla— la barra se mueve) y avisa
// `elegido` al soltar.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import BA

RowLayout {
    id: f
    property string titulo: ""
    property string ayuda: ""
    property real desde: 0
    property real hasta: 1
    property real paso: 0.05
    property real valor: 0
    property string unidad: " s"
    property int decimales: 2
    property int anchoTitulo: 150
    signal elegido(real v)
    spacing: 10

    Text {
        text: f.titulo; color: Theme.textDim; font.pixelSize: 12
        Layout.preferredWidth: f.anchoTitulo
        elide: Text.ElideRight
        ToolTip.visible: h.hovered && f.ayuda !== ""; ToolTip.text: f.ayuda; ToolTip.delay: 300
        HoverHandler { id: h }
    }
    Slider {
        id: sl
        Layout.fillWidth: true
        from: f.desde; to: f.hasta; stepSize: f.paso
        snapMode: Slider.SnapAlways
        Binding on value { when: !sl.pressed; value: f.valor }
        onPressedChanged: if (!pressed && Math.abs(value - f.valor) > f.paso / 2) f.elegido(value)
    }
    Text {
        Layout.preferredWidth: 56; horizontalAlignment: Text.AlignRight
        text: Number(sl.value).toFixed(f.decimales).replace(".", ",") + f.unidad
        color: Theme.text; font { pixelSize: 12; family: Theme.mono }
    }
}
