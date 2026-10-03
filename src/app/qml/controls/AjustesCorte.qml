// Cuándo corta una frase la transcripción continua (Studio.voz.cortes): la pausa
// que la termina, lo más que dura y cuán seguro tiene que estar el detector de
// que es voz. Con el indicador en vivo de cada transcripción, para encontrar el
// punto justo hablando. Lo usan Configuración → Audio y voz y el panel de Traducción.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import BA

ColumnLayout {
    id: ac
    readonly property var vz: Studio.voz
    readonly property var c: vz ? vz.cortes : ({ pausa: 0.35, maximo: 6, umbral: 0.5 })
    spacing: 8

    function poner(pausa, maximo, umbral) {
        const e = vz.setCortes(pausa, maximo, umbral)
        if (e !== "") Studio.notify(e, "error")
    }
    function dos(x) { return Number(x).toFixed(2).replace(".", ",") }

    Text { text: "Cortes de la transcripción continua"; color: Theme.text; font { pixelSize: 14; weight: Font.DemiBold } }
    Text {
        Layout.fillWidth: true; wrapMode: Text.WordWrap
        color: Theme.textDim; font.pixelSize: 11
        text: "Cuándo se manda una frase a subtítulos y traducción. Menos pausa y menos largo máximo = sale antes pero más " +
              "cortada; más = frases enteras pero con más demora. Si la sala tiene ruido y no corta nunca, subí el umbral. " +
              "Hablá y mirá el indicador de abajo: se aplica al toque."
    }
    Repeater {
        model: [
            { k: "pausa", t: "Pausa que corta", min: 0.15, max: 1.5, paso: 0.05, u: " s",
              ayuda: "Silencio que termina una frase. 0,3 a 0,4 s anda para la mayoría de los oradores." },
            { k: "maximo", t: "Largo máximo de frase", min: 2, max: 15, paso: 0.5, u: " s",
              ayuda: "Si el orador no hace pausa, al pasar este tiempo se corta en la próxima pausa corta." },
            { k: "umbral", t: "Umbral de voz", min: 0.2, max: 0.9, paso: 0.05, u: "",
              ayuda: "Cuán seguro tiene que estar el detector de que es voz. Más alto ignora más el ruido de la sala." }
        ]
        delegate: RowLayout {
            id: fila
            required property var modelData
            Layout.fillWidth: true
            spacing: 10
            Text {
                text: fila.modelData.t; color: Theme.textDim; font.pixelSize: 12
                Layout.preferredWidth: 170
                ToolTip.visible: h.hovered; ToolTip.text: fila.modelData.ayuda; ToolTip.delay: 300
                HoverHandler { id: h }
            }
            Slider {
                id: sl
                Layout.fillWidth: true
                from: fila.modelData.min; to: fila.modelData.max; stepSize: fila.modelData.paso
                snapMode: Slider.SnapAlways
                Binding on value { when: !sl.pressed; value: ac.c[fila.modelData.k] }
                onPressedChanged: if (!pressed) {
                    const n = Object.assign({}, ac.c); n[fila.modelData.k] = value
                    ac.poner(n.pausa, n.maximo, n.umbral)
                }
            }
            Text {
                Layout.preferredWidth: 56; horizontalAlignment: Text.AlignRight
                text: ac.dos(sl.value) + fila.modelData.u
                color: Theme.text; font { pixelSize: 12; family: Theme.mono }
            }
        }
    }
    RowLayout {
        spacing: 6
        Text { text: "Valores típicos:"; color: Theme.textFaint; font.pixelSize: 11 }
        GlassButton { compact: true; text: "Rápido"; onClicked: ac.poner(0.25, 4, 0.5)
                      ToolTip.visible: hovered; ToolTip.delay: 400; ToolTip.text: "0,25 s · 4 s: sale enseguida, frases más cortadas" }
        GlassButton { compact: true; text: "Normal"; onClicked: ac.poner(0.35, 6, 0.5)
                      ToolTip.visible: hovered; ToolTip.delay: 400; ToolTip.text: "0,35 s · 6 s" }
        GlassButton { compact: true; text: "Sala ruidosa"; onClicked: ac.poner(0.35, 6, 0.7)
                      ToolTip.visible: hovered; ToolTip.delay: 400; ToolTip.text: "Umbral 0,7: el murmullo y el eco no cuentan como voz" }
        GlassButton { compact: true; text: "Frases enteras"; onClicked: ac.poner(0.6, 10, 0.5)
                      ToolTip.visible: hovered; ToolTip.delay: 400; ToolTip.text: "0,6 s · 10 s: mejor traducción, más demora" }
    }
    // Lo que oye ahora cada transcripción continua.
    Text {
        visible: ac.vz && ac.vz.oido.length === 0
        Layout.fillWidth: true; wrapMode: Text.WordWrap
        text: "No hay ninguna transcripción continua andando (Audio → rack → «+ Agregar» → «Voz a texto: transcripción continua»)."
        color: Theme.textFaint; font.pixelSize: 11
    }
    Repeater {
        model: ac.vz ? ac.vz.oido : []
        delegate: RowLayout {
            id: o
            required property var modelData
            Layout.fillWidth: true
            spacing: 8
            Rectangle {
                width: 10; height: 10; radius: 5
                color: o.modelData.hablando ? Theme.ok : Theme.textFaint
            }
            Text { text: o.modelData.flujo; color: Theme.text; font.pixelSize: 12; Layout.preferredWidth: 160; elide: Text.ElideRight }
            Rectangle {
                Layout.fillWidth: true
                height: 8; radius: 4
                color: Qt.rgba(1, 1, 1, 0.08)
                Rectangle {
                    height: parent.height; radius: 4
                    width: parent.width * Math.min(1, o.modelData.segundos / Math.max(1, ac.c.maximo))
                    color: o.modelData.segundos > ac.c.maximo * 0.8 ? Theme.amber : Theme.ok
                }
            }
            Text {
                Layout.preferredWidth: 120; horizontalAlignment: Text.AlignRight
                text: o.modelData.hablando ? "hablando · " + ac.dos(o.modelData.segundos) + " s" : "silencio"
                color: Theme.textDim; font { pixelSize: 11; family: Theme.mono }
            }
        }
    }
}
