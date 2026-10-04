// Superresolución de las entradas de red (Studio.superres, video/SuperRes): el
// motor de cada entrada, entrenar la red chica con un botón y los modelos.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import BA

ColumnLayout {
    id: panel
    property var vg: null
    readonly property var sr: Studio.superres
    readonly property var tr: sr ? sr.trabajo : ({})
    property var red: []
    spacing: 14

    function refrescar() { red = vg ? vg.estadoRed().filter(e => e.gstreamer !== undefined) : [] }
    function avisar(e, ok) { if (e !== "") Studio.notify(e, "error"); else if (ok) Studio.notify(ok, "info") }
    Timer { interval: 2000; running: panel.visible; repeat: true; triggeredOnStart: true; onTriggered: panel.refrescar() }

    // Las opciones de motor: los que hay y cada modelo ONNX.
    readonly property var opciones: {
        const l = [{ t: "Ninguna", motor: "", modelo: "" }]
        const m = sr ? sr.motores : []
        if (m.indexOf("apple") >= 0) l.push({ t: "Apple (VideoToolbox) · ~3 ms", motor: "apple", modelo: "" })
        if (m.indexOf("fsr") >= 0) l.push({ t: "FSR 1 (AMD, cualquier GPU)", motor: "fsr", modelo: "" })
        if (m.indexOf("onnx") >= 0)
            for (const x of (sr ? sr.modelos : [])) l.push({ t: "Red: " + x.nombre, motor: "onnx", modelo: x.ruta })
        return l
    }
    function indiceDe(g) {
        const motor = g.superres_motor || "", modelo = g.superres_modelo || ""
        for (let i = 0; i < opciones.length; ++i)
            if (opciones[i].motor === motor && (motor !== "onnx" || opciones[i].modelo === modelo)) return i
        return 0
    }

    Text {
        Layout.fillWidth: true; wrapMode: Text.WordWrap
        color: Theme.textDim; font.pixelSize: 12
        text: "Duplica la resolución de una cámara de red (una PAL de 768x576 sale en 1536x1152) antes de que entre al estudio. " +
              "El audio no pasa por acá: la sincronía se mantiene mientras lo que tarda entre en el colchón de la entrada."
    }

    // --- Cada entrada -------------------------------------------------------------
    Text { text: "Entradas de red"; color: Theme.text; font { pixelSize: 14; weight: Font.DemiBold } }
    Text {
        visible: panel.red.length === 0
        text: "No hay entradas de red (la superresolución va en la cadena de GStreamer de cada una)."
        color: Theme.textFaint; font.pixelSize: 12
    }
    Repeater {
        model: panel.red
        delegate: RowLayout {
            id: fila
            required property var modelData
            readonly property var g: modelData.gstreamer || {}
            Layout.fillWidth: true
            spacing: 10
            Text { text: fila.modelData.entrada; color: Theme.text; font.pixelSize: 13; Layout.preferredWidth: 160; elide: Text.ElideRight }
            Text {
                text: fila.modelData.ancho > 0 ? fila.modelData.ancho + "x" + fila.modelData.alto : "…"
                color: Theme.textDim; font { pixelSize: 11; family: Theme.mono }
                Layout.preferredWidth: 80
            }
            ComboBox {
                Layout.preferredWidth: 300
                model: panel.opciones.map(o => o.t)
                currentIndex: panel.indiceDe(fila.g)
                onActivated: (i) => {
                    const o = panel.opciones[i]
                    panel.avisar(panel.sr.aplicarUi(fila.modelData.entrada, o.motor, o.modelo), "Aplicado: se rearma la entrada")
                }
            }
            Text {
                Layout.fillWidth: true
                text: fila.g.superres_error ? "✕ " + fila.g.superres_error
                      : fila.g.superres_motor ? (fila.g.superres_ms > 0 ? Number(fila.g.superres_ms).toFixed(1) + " ms por cuadro" : "") +
                                                (fila.g.superres_proveedor ? " · " + fila.g.superres_proveedor : "")
                      : ""
                color: fila.g.superres_error ? Theme.live : fila.g.superres_ms > 25 ? Theme.amber : Theme.textDim
                font.pixelSize: 11; elide: Text.ElideRight
            }
        }
    }

    // --- Entrenar ---------------------------------------------------------------------
    Rectangle { Layout.fillWidth: true; height: 1; color: Theme.glassBorder }
    Text { text: "Entrenar con lo que ve una cámara"; color: Theme.text; font { pixelSize: 14; weight: Font.DemiBold } }
    Text {
        Layout.fillWidth: true; wrapMode: Text.WordWrap
        color: Theme.textDim; font.pixelSize: 11
        text: "Graba un minuto de la cámara (sin tocar lo que sale al aire), arma pares «la imagen achicada y comprimida → la " +
              "original» y entrena una red chica que aprende a deshacer lo que hace ESA cámara. Al terminar la pone en la entrada " +
              "y dice cuánto mejora contra el agrandado común. Volver a entrenar sigue desde la anterior." +
              (panel.sr && !panel.sr.laboratorioListo ? " La primera vez baja el laboratorio (PyTorch, ~800 MB, una sola vez)." : "")
    }
    RowLayout {
        Layout.fillWidth: true
        spacing: 8
        ComboBox {
            id: entradaEnt
            Layout.preferredWidth: 220
            model: panel.red.map(e => e.entrada)
        }
        Text { text: "minutos"; color: Theme.textDim; font.pixelSize: 12 }
        SpinBox { id: minutos; from: 1; to: 60; value: 5; editable: true }
        GlassButton {
            compact: true; active: true
            text: panel.tr.corriendo ? "Trabajando…" : "Entrenar con lo que ve"
            enabled: !panel.tr.corriendo && entradaEnt.currentText !== ""
            onClicked: panel.avisar(panel.sr.entrenarUi(entradaEnt.currentText, minutos.value, true), "")
        }
        GlassButton { compact: true; text: "Cancelar"; visible: !!panel.tr.corriendo; onClicked: panel.sr.cancelarUi() }
        Item { Layout.fillWidth: true }
    }
    ColumnLayout {
        visible: !!panel.tr.que
        Layout.fillWidth: true
        spacing: 4
        Text { text: panel.tr.que || ""; color: Theme.text; font.pixelSize: 12 }
        ProgressBar { Layout.fillWidth: true; from: 0; to: 100; value: panel.tr.pct || 0; visible: !!panel.tr.corriendo }
        Text {
            Layout.fillWidth: true; wrapMode: Text.WordWrap
            text: panel.tr.error ? "✕ " + panel.tr.error : (panel.tr.texto || "")
            color: panel.tr.error ? Theme.live : Theme.textDim; font.pixelSize: 11
        }
        Text {
            visible: panel.tr.psnr_red !== undefined
            text: "Calidad: " + Number(panel.tr.psnr_red || 0).toFixed(2) + " dB contra " + Number(panel.tr.psnr_bicubico || 0).toFixed(2) +
                  " dB del agrandado común (" + ((panel.tr.psnr_red - panel.tr.psnr_bicubico) >= 0 ? "+" : "") +
                  Number((panel.tr.psnr_red || 0) - (panel.tr.psnr_bicubico || 0)).toFixed(2) + " dB)"
            color: (panel.tr.psnr_red || 0) > (panel.tr.psnr_bicubico || 0) ? Theme.ok : Theme.amber; font.pixelSize: 12
        }
    }

    // --- Modelos ------------------------------------------------------------------------
    Rectangle { Layout.fillWidth: true; height: 1; color: Theme.glassBorder }
    RowLayout {
        Layout.fillWidth: true
        Text { text: "Modelos"; color: Theme.text; font { pixelSize: 14; weight: Font.DemiBold } Layout.fillWidth: true }
        GlassButton {
            compact: true; text: "Bajar SPAN x2"; enabled: !panel.tr.corriendo
            onClicked: panel.avisar(panel.sr.convertirUi("span"), "")
            ToolTip.visible: hovered; ToolTip.delay: 400; ToolTip.text: "SPAN (Apache-2.0): liviano y fiel. En una Mac ~140 ms por cuadro: para probar"
        }
        GlassButton {
            compact: true; text: "Bajar Real-ESRGAN"; enabled: !panel.tr.corriendo
            onClicked: panel.avisar(panel.sr.convertirUi("esrgan"), "")
            ToolTip.visible: hovered; ToolTip.delay: 400; ToolTip.text: "Real-ESRGAN compacto x4 (BSD-3): limpia ruido y compresión; pesado (~290 ms en una Mac)"
        }
    }
    Text {
        visible: panel.sr && panel.sr.modelos.length === 0
        text: "Todavía no hay modelos: entrená uno con una cámara o bajá SPAN / Real-ESRGAN."
        color: Theme.textFaint; font.pixelSize: 12
    }
    Repeater {
        model: panel.sr ? panel.sr.modelos : []
        delegate: RowLayout {
            id: mod
            required property var modelData
            Layout.fillWidth: true
            spacing: 8
            Text { text: mod.modelData.nombre; color: Theme.text; font.pixelSize: 12; Layout.preferredWidth: 220; elide: Text.ElideRight }
            Text {
                Layout.fillWidth: true; elide: Text.ElideRight
                text: (mod.modelData.arquitectura || "") + " · x" + (mod.modelData.escala || 2) + " · " +
                      (mod.modelData.canales === 1 ? "luma" : "RGB") + " · " + Number(mod.modelData.tamano_mb || 0).toFixed(1) + " MB" +
                      (mod.modelData.psnr_red !== undefined ? " · " + (mod.modelData.psnr_red - mod.modelData.psnr_bicubico >= 0 ? "+" : "") +
                                                             Number(mod.modelData.psnr_red - mod.modelData.psnr_bicubico).toFixed(2) + " dB" : "") +
                      (mod.modelData.licencia ? " · " + mod.modelData.licencia : "")
                color: Theme.textDim; font.pixelSize: 11
            }
            GlassButton { compact: true; text: "✕"; onClicked: panel.avisar(panel.sr.borrarModelo(mod.modelData.archivo), "Modelo borrado") }
        }
    }
}
