// El panel de una tarjeta «Traducir» del rack de audio (AudioRack, tipo
// "traducir"): qué salida de texto traduce, de qué idioma a cuál, dónde publica
// y cómo va. Los cambios van por Studio.traductor (agent/Traductor), que guarda
// en la tarjeta. `id`: «<fuente>#<nodo>».
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import BA

ColumnLayout {
    id: panel
    property string traduccionId: ""
    signal quitar()
    signal abrirGlosario()
    readonly property var tr: Studio.traductor
    readonly property var ml: Studio.modelos
    readonly property var t: (tr ? tr.traducciones : []).find(x => x.id === traduccionId) || ({})
    readonly property var idiomas: tr ? tr.idiomas : []
    property string resultado: ""
    property bool resultadoOk: true
    spacing: 6

    function cambiar(c) { const e = tr.ajustarUi(traduccionId, c); if (e !== "") Studio.notify(e, "error") }
    function nombreIdioma(c) { const x = idiomas.find(i => i.codigo === c); return x ? x.nombre : c }

    Connections {
        target: panel.tr
        function onProbado(a, texto, ok) { if (a === panel.t.a) { panel.resultado = texto; panel.resultadoOk = ok } }
    }

    RowLayout {
        Layout.fillWidth: true
        Toggle { checked: panel.t.activa !== false; onToggled: (q) => panel.cambiar({ activa: q }) }
        Text {
            Layout.fillWidth: true; elide: Text.ElideRight
            text: panel.t.activa === false ? "Pausada" : "Traduciendo"
            color: Theme.textDim; font.pixelSize: 12
        }
        Text {
            text: panel.t.estado || ""
            color: panel.t.estado === "lista" ? Theme.ok : Theme.amber; font.pixelSize: 11
        }
    }
    RowLayout {
        Layout.fillWidth: true
        Text { text: "Traduce"; color: Theme.textDim; font.pixelSize: 12; Layout.preferredWidth: 70 }
        ComboBox {
            Layout.fillWidth: true
            // Las salidas de texto que hay, menos la que publica esta misma tarjeta.
            readonly property var lista: ["(elegí una salida de texto)"].concat(
                (Studio.flujos ? Studio.flujos.flujos : []).map(f => f.nombre).filter(n => n !== panel.t.nombre))
            model: lista
            currentIndex: Math.max(0, lista.indexOf(panel.t.desde || ""))
            onActivated: if (currentIndex > 0) panel.cambiar({ desde: currentText })
            ToolTip.visible: hovered; ToolTip.delay: 400
            ToolTip.text: "Lo que traduce: lo que publica una «Voz a texto» (o lo que ya publica otra traducción)."
        }
    }
    RowLayout {
        Layout.fillWidth: true
        Text { text: "De"; color: Theme.textDim; font.pixelSize: 12; Layout.preferredWidth: 70 }
        ComboBox {
            Layout.fillWidth: true
            model: panel.idiomas.map(x => x.nombre)
            currentIndex: Math.max(0, panel.idiomas.findIndex(x => x.codigo === (panel.t.idioma_desde || "es")))
            onActivated: panel.cambiar({ idioma_desde: panel.idiomas[currentIndex].codigo })
        }
        Text { text: "al"; color: Theme.textDim; font.pixelSize: 12 }
        ComboBox {
            Layout.fillWidth: true
            model: panel.idiomas.map(x => x.nombre)
            currentIndex: Math.max(0, panel.idiomas.findIndex(x => x.codigo === (panel.t.a || "en")))
            onActivated: panel.cambiar({ a: panel.idiomas[currentIndex].codigo })
        }
    }
    GlassButton {
        visible: (panel.t.faltan || []).length > 0
        enabled: panel.ml && !panel.ml.descargando
        compact: true; icon: "⇩"
        text: "Bajar " + panel.nombreIdioma(panel.t.idioma_desde) + " → " + panel.nombreIdioma(panel.t.a) +
              " (" + (panel.t.faltan || []).length + " paquete" + ((panel.t.faltan || []).length === 1 ? "" : "s") + ")"
        onClicked: { const e = panel.tr.bajarUi(panel.t.idioma_desde, panel.t.a); if (e !== "") Studio.notify(e, "error") }
    }
    Text {
        visible: panel.ml && panel.ml.descargando
        Layout.fillWidth: true; wrapMode: Text.WordWrap
        text: panel.ml ? panel.ml.textoDescarga : ""
        color: Theme.amber; font.pixelSize: 11
    }
    RowLayout {
        Layout.fillWidth: true
        Text { text: "Publica en"; color: Theme.textDim; font.pixelSize: 12; Layout.preferredWidth: 70 }
        TextField {
            Layout.fillWidth: true
            text: panel.t.nombre || ""
            placeholderText: "nombre de la salida traducida"
            color: Theme.text; font.pixelSize: 12
            background: Rectangle { color: Theme.field; radius: 6 }
            onEditingFinished: if (text !== (panel.t.nombre || "")) panel.cambiar({ nombre: text })
            ToolTip.visible: hovered; ToolTip.delay: 400
            ToolTip.text: "La salida de texto con la traducción: la leen una tarjeta «Texto a voz», un cuadro «Subtítulos» de una escena o un rótulo. Vacío = «<lo que traduce> · <idioma>»."
        }
    }
    RowLayout {
        Layout.fillWidth: true
        Text { text: "Rótulo"; color: Theme.textDim; font.pixelSize: 12; Layout.preferredWidth: 70 }
        TextField {
            Layout.fillWidth: true
            text: panel.t.rotulo || ""
            placeholderText: "fuente de texto donde escribir (opcional)"
            color: Theme.text; font.pixelSize: 12
            background: Rectangle { color: Theme.field; radius: 6 }
            onEditingFinished: if (text !== (panel.t.rotulo || "")) panel.cambiar({ rotulo: text })
        }
    }
    RowLayout {
        Layout.fillWidth: true
        Text { text: "Archivo"; color: Theme.textDim; font.pixelSize: 12; Layout.preferredWidth: 70 }
        Toggle { checked: panel.t.archivo === true; onToggled: (q) => panel.cambiar({ archivo: q }) }
        Text { text: ".txt y .srt"; color: Theme.textFaint; font.pixelSize: 11; Layout.fillWidth: true }
    }
    TiemposTraduccion {
        Layout.fillWidth: true
        t: panel.t
        onCambiar: (c) => panel.cambiar(c)
    }
    Text {
        visible: !!panel.t.ultima
        Layout.fillWidth: true; wrapMode: Text.WordWrap
        text: !panel.t.ultima ? "" : panel.t.ultima.error ? "✕ " + panel.t.ultima.error
              : panel.t.ultima.hora + " · " + panel.t.ultima.ms + " ms · " + panel.t.ultima.texto
        color: panel.t.ultima && panel.t.ultima.error ? Theme.live : Theme.textFaint; font.pixelSize: 11
    }
    RowLayout {
        Layout.fillWidth: true
        TextField {
            id: prueba
            Layout.fillWidth: true
            placeholderText: "Probar: escribí una frase"
            color: Theme.text; font.pixelSize: 12
            background: Rectangle { color: Theme.field; radius: 6 }
            onAccepted: probar.clicked()
        }
        GlassButton {
            id: probar
            compact: true; text: "Traducir"
            enabled: prueba.text.trim() !== ""
            onClicked: { panel.resultado = "Traduciendo…"; panel.resultadoOk = true; panel.tr.probarUi(prueba.text, panel.t.a, panel.t.idioma_desde || "es") }
        }
    }
    Text {
        visible: panel.resultado !== ""
        Layout.fillWidth: true; wrapMode: Text.WordWrap
        text: panel.resultado
        color: panel.resultadoOk ? Theme.text : Theme.live; font.pixelSize: 12
    }
    RowLayout {
        Layout.fillWidth: true
        GlassButton { compact: true; text: "Glosario y ajustes…"; onClicked: panel.abrirGlosario() }
        Item { Layout.fillWidth: true }
        GlassButton { compact: true; text: "Quitar"; onClicked: panel.quitar() }
    }
}
