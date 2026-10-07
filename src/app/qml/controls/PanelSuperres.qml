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
    property var gpu: ({})
    property var propios: []
    property var prog: ({})
    /// El plugin con el que abrir la sección «Destilar» (lo pone la tarjeta).
    property string maestroPedido: ""
    spacing: 14

    function refrescar() {
        red = vg ? vg.estadoRed().filter(e => e.gstreamer !== undefined) : []
        gpu = sr ? sr.runtimeGpuUi() : ({})
        propios = sr ? sr.pluginsElementoUi() : []
        prog = vg ? vg.programadorUi() : ({})
    }
    readonly property var progEntradas: {
        const e = prog.entradas || {}
        return Object.keys(e).map(k => Object.assign({ entrada: k }, e[k]))
    }
    readonly property var redesCatalogo: sr ? (sr.modelos, sr.pluginsUi()).filter(p => p.tipo === "red" && !p.propio) : []
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
        text: "Duplica la resolución de una cámara de red (sale al doble de lo que entra, sea cual sea su tamaño) antes de que entre al estudio. " +
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

    // --- Programador (su menú propio: PanelProgramador) -------------------------------
    Rectangle { Layout.fillWidth: true; height: 1; color: Theme.glassBorder }
    RowLayout {
        Layout.fillWidth: true
        Text { text: "Programador (programa y preview)"; color: Theme.text; font { pixelSize: 14; weight: Font.DemiBold } }
        Text {
            Layout.fillWidth: true; elide: Text.ElideRight
            text: panel.prog.activo === false ? "apagado: todas las redes trabajan"
                  : panel.progEntradas.filter(e => e.modo === "activo").length + " con red, " +
                    panel.progEntradas.filter(e => e.modo === "barato").length + " afiladas, " +
                    panel.progEntradas.filter(e => e.modo === "reposo").length + " en reposo · capacidad " +
                    (panel.prog.capacidad === "fija" ? "fija" : "medida")
            color: Theme.textDim; font.pixelSize: 12
        }
        GlassButton { compact: true; text: "Configurar…"; onClicked: progPop.open() }
    }
    PanelProgramador { id: progPop; vg: panel.vg }

    // --- Entrenar ---------------------------------------------------------------------
    Rectangle { Layout.fillWidth: true; height: 1; color: Theme.glassBorder }
    Text { text: "Entrenar con lo que ve una cámara"; color: Theme.text; font { pixelSize: 14; weight: Font.DemiBold } }
    Text {
        Layout.fillWidth: true; wrapMode: Text.WordWrap
        color: Theme.textDim; font.pixelSize: 11
        text: "Graba un minuto de la cámara (sin tocar lo que sale al aire: una de red, de su dirección; una local, cuadros de lo que sale de su grafo), arma pares «la imagen achicada y comprimida → la " +
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
            // Las de red graban de su dirección; las cámaras locales, cuadros de su grafo.
            model: panel.vg ? panel.vg.entradas : panel.red.map(e => e.entrada)
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

    // --- El banco de pruebas: qué puede usar esta máquina -----------------------------------
    ColumnLayout {
        id: banco
        Layout.fillWidth: true
        spacing: 6
        readonly property var b: Studio.bancoSr
        readonly property var est: b ? b.estado : ({})
        readonly property var res: b ? b.resultado : ({})
        readonly property var datos: (est.corriendo ? est.parciales : res.resultados) || []
        readonly property var tamanos: {
            const vistos = {}
            const l = []
            for (const r of datos) {
                const k = r.ancho + "x" + r.alto
                if (!vistos[k]) { vistos[k] = true; l.push({ k: k, px: r.ancho * r.alto, t: r.alto + "p" + (r.ancho * 9 !== r.alto * 16 ? " (" + r.ancho + "×" + r.alto + ")" : "") }) }
            }
            return l.sort((x, y) => x.px - y.px)
        }
        readonly property var filas: {
            const vistos = {}
            const l = []
            for (const r of datos) if (!vistos[r.id]) { vistos[r.id] = true; l.push({ id: r.id, nombre: r.nombre, acel: r.acelerador_nombre }) }
            return l
        }
        function celda(id, k) { return datos.find(r => r.id === id && (r.ancho + "x" + r.alto) === k) }
        function colorDe(v) { return v === "en_vivo" ? Theme.ok : v === "reducido" ? Theme.warn : v === "diferido" ? "#ff8a65" : Theme.textFaint }
        // Mientras mide, se refresca solo (la señal del banco ya avisa; esto es por la cuenta regresiva).
        Timer { interval: 1000; running: panel.visible && !!banco.est.corriendo; repeat: true; onTriggered: banco.estChanged() }

        RowLayout {
            Layout.fillWidth: true
            Text { text: "Esta máquina: qué superresolución puede usar"; color: Theme.text; font { pixelSize: 14; weight: Font.DemiBold } Layout.fillWidth: true }
            GlassButton {
                compact: true
                text: banco.est.corriendo ? "Cancelar" : (banco.res.fecha ? "Medir de nuevo" : "Medir esta máquina")
                onClicked: {
                    if (banco.est.corriendo) { banco.b.cancelar(); return }
                    const e = banco.b.correrUi({})
                    if (e !== "")
                        ApplicationWindow.window.confirm({
                            title: "Medir igual", text: e + ".", accept: "Medir igual", reject: "No",
                            action: () => panel.avisar(banco.b.correrUi({ forzar: true }), "Midiendo…")
                        })
                }
            }
        }
        Text {
            Layout.fillWidth: true; wrapMode: Text.WordWrap
            color: Theme.textDim; font.pixelSize: 12
            text: banco.est.corriendo
                  ? "Midiendo " + (banco.est.actual || "") + " · " + (banco.est.hechas || 0) + " de " + (banco.est.total || 0)
                    + (banco.est.eta_s ? " · quedan unos " + Math.ceil(banco.est.eta_s / 60) + " min" : "") + ". Calienta la máquina: mejor sin nada al aire."
                  : banco.res.fecha
                    ? "Medido el " + banco.res.fecha + " en " + ((banco.res.maquina || {}).sistema || "") + " a " + banco.res.fps + " fps (" + banco.res.cuadro_ms + " ms por cuadro)"
                      + (banco.res.completo === false ? " · incompleto (se canceló)" : "") + ". El programador usa estos números antes de prender una red."
                    : "Nunca se midió. Prueba cada método instalado con cada acelerador (Neural Engine, GPU, DirectML, CUDA, Vulkan…) a 540p, 720p, 1080p y al tamaño de cada cámara; tarda unos minutos."
        }
        // La tabla: un método y acelerador por fila; ms por cuadro por tamaño, en colores.
        GridLayout {
            visible: banco.filas.length > 0
            Layout.fillWidth: true
            columns: banco.tamanos.length + 1
            columnSpacing: 12
            rowSpacing: 3
            Text { text: "Método · acelerador"; color: Theme.textDim; font.pixelSize: 11 }
            Repeater {
                model: banco.tamanos
                delegate: Text { required property var modelData; text: modelData.t; color: Theme.textDim; font.pixelSize: 11 }
            }
            Repeater {
                model: banco.filas
                delegate: Item {
                    // (una fila del GridLayout: el nombre y una celda por tamaño)
                    required property var modelData
                    Layout.columnSpan: banco.tamanos.length + 1
                    Layout.fillWidth: true
                    implicitHeight: filaB.implicitHeight
                    RowLayout {
                        id: filaB
                        anchors { left: parent.left; right: parent.right }
                        spacing: 12
                        Text {
                            Layout.preferredWidth: 220
                            elide: Text.ElideRight
                            text: modelData.nombre + " · " + modelData.acel
                            color: Theme.text; font.pixelSize: 12
                        }
                        Repeater {
                            model: banco.tamanos
                            delegate: Text {
                                required property var modelData
                                readonly property var c: banco.celda(filaB.parent.modelData.id, modelData.k)
                                Layout.preferredWidth: 90
                                text: !c ? "—" : c.error ? "no corre" : c.ms + " ms" + (c.simultaneas > 1 ? " ×" + c.simultaneas : "")
                                color: c ? banco.colorDe(c.veredicto) : Theme.textFaint
                                font.pixelSize: 12
                                ToolTip.visible: maB.containsMouse && !!c
                                ToolTip.text: !c ? "" : c.error ? c.error
                                              : (c.veredicto === "en_vivo" ? "En vivo" + (c.simultaneas > 1 ? " (" + c.simultaneas + " a la vez en este acelerador)" : "")
                                                 : c.veredicto === "reducido" ? "Corre a unos " + c.fps + " cuadros por segundo"
                                                 : "Solo para lo grabado") + (c.abrir_s > 2 ? " · abrir la red: " + c.abrir_s + " s" : "")
                                MouseArea { id: maB; anchors.fill: parent; hoverEnabled: true }
                            }
                        }
                    }
                }
            }
        }
        Text {
            visible: banco.filas.length > 0
            Layout.fillWidth: true; wrapMode: Text.WordWrap
            font.pixelSize: 11
            color: Theme.textFaint
            text: "Verde: en vivo (×N: cuántas a la vez). Ámbar: corre a menos cuadros. Naranja: solo para lo grabado."
        }
        Repeater {
            model: banco.res.recomendado ? Object.keys(banco.res.recomendado) : []
            delegate: Text {
                required property var modelData
                readonly property var r: banco.res.recomendado[modelData]
                Layout.fillWidth: true; wrapMode: Text.WordWrap
                font.pixelSize: 12
                color: banco.colorDe(r.veredicto)
                text: "Para " + modelData.replace("x", "×") + ": " + r.nombre + " en " + r.acelerador + " (" + r.ms + " ms"
                      + (r.veredicto === "en_vivo" ? ", en vivo)" : r.veredicto === "reducido" ? ", a menos cuadros)" : ", solo grabado)")
            }
        }
    }

    // --- Placa de video ------------------------------------------------------------------
    Rectangle { Layout.fillWidth: true; height: 1; color: Theme.glassBorder }
    Text { text: "Placa de video y aceleración"; color: Theme.text; font { pixelSize: 14; weight: Font.DemiBold } }
    Text {
        Layout.fillWidth: true; wrapMode: Text.WordWrap
        color: Theme.textDim; font.pixelSize: 12
        text: "Placa detectada: " + (panel.gpu.placa ? panel.gpu.placa.toUpperCase() : "ninguna conocida") +
              (panel.gpu.instalado ? " · runtime de GPU bajado: " + panel.gpu.instalado.tipo + (panel.gpu.activo ? " (en uso)" : " (reiniciá el programa para usarlo)")
                                   : " · sin runtime de GPU bajado") +
              (panel.gpu.nota ? ". " + panel.gpu.nota : "")
    }
    RowLayout {
        visible: panel.gpu.sugerido !== undefined && Qt.platform.os !== "osx"
        Layout.fillWidth: true
        spacing: 8
        ComboBox {
            id: tipoGpu
            Layout.preferredWidth: 240
            model: [ { t: "NVIDIA (CUDA + TensorRT)", v: "nvidia" }, { t: "AMD en Linux (ROCm/MIGraphX)", v: "amd" }, { t: "Intel (OpenVINO)", v: "intel" } ]
            textRole: "t"
            currentIndex: Math.max(0, model.findIndex(o => o.v === panel.gpu.sugerido))
        }
        CheckBox {
            id: conTrt
            visible: tipoGpu.currentIndex === 0
            text: "Con TensorRT (compila la red para la placa; ~2 GB más)"
            checked: true
        }
        GlassButton {
            compact: true; active: true
            text: panel.tr.corriendo ? "Trabajando…" : "Bajar el runtime de GPU"
            enabled: !panel.tr.corriendo
            onClicked: panel.avisar(panel.sr.instalarRuntimeGpuUi(tipoGpu.model[tipoGpu.currentIndex].v, conTrt.checked), "Bajando: al terminar, reiniciá el programa")
        }
        Item { Layout.fillWidth: true }
    }

    // --- Destilar ---------------------------------------------------------------------------
    Rectangle { Layout.fillWidth: true; height: 1; color: Theme.glassBorder }
    Text { text: "Destilar una red chica de una grande"; color: Theme.text; font { pixelSize: 14; weight: Font.DemiBold } }
    Text {
        Layout.fillWidth: true; wrapMode: Text.WordWrap
        color: Theme.textDim; font.pixelSize: 11
        text: "Una red propia chica (corre en vivo hasta en la CPU) aprende a imitar a una red grande (el maestro) mirando lo que ve " +
              "la cámara: sirve en máquinas donde el maestro no da en vivo. En la CPU tarda más pero no calienta la GPU."
    }
    RowLayout {
        Layout.fillWidth: true
        spacing: 8
        ComboBox { id: entradaDest; Layout.preferredWidth: 200; model: panel.vg ? panel.vg.entradas : panel.red.map(e => e.entrada) }
        Text { text: "maestro"; color: Theme.textDim; font.pixelSize: 12 }
        ComboBox {
            id: maestro
            Layout.preferredWidth: 240
            model: panel.redesCatalogo
            textRole: "nombre"
            currentIndex: Math.max(0, panel.redesCatalogo.findIndex(p => p.id === (panel.maestroPedido || "realesr-general-x4v3")))
        }
        Text { text: "minutos"; color: Theme.textDim; font.pixelSize: 12 }
        SpinBox { id: minDest; from: 1; to: 240; value: 15; editable: true }
    }
    RowLayout {
        Layout.fillWidth: true
        spacing: 12
        CheckBox { id: destCpu; text: "En la CPU (no calienta la GPU)"; checked: true }
        CheckBox { id: destAplicar; text: "Ponerla en la entrada al terminar"; checked: true }
        GlassButton {
            compact: true; active: true
            text: panel.tr.corriendo ? "Trabajando…" : "Destilar"
            enabled: !panel.tr.corriendo && entradaDest.currentText !== "" && maestro.currentIndex >= 0
            onClicked: panel.avisar(panel.sr.destilarUi(entradaDest.currentText, panel.redesCatalogo[maestro.currentIndex].id,
                                                         minDest.value, destAplicar.checked, destCpu.checked), "")
        }
        Item { Layout.fillWidth: true }
    }

    // --- Variantes por resolución ------------------------------------------------------------
    Rectangle { Layout.fillWidth: true; height: 1; color: Theme.glassBorder }
    Text { text: "Variantes por resolución"; color: Theme.text; font { pixelSize: 14; weight: Font.DemiBold } }
    Text {
        Layout.fillWidth: true; wrapMode: Text.WordWrap
        color: Theme.textDim; font.pixelSize: 11
        text: "La misma red compilada para un tamaño de entrada fijo: el Neural Engine (CoreML) solo corre tamaños compilados, y TensorRT y " +
              "DirectML van mejor con la forma fija (ONNX). Cada variante guarda su ficha (variante.json: qué es, para qué cámara, tamaños de " +
              "entrada, red y salida, lo medido). La tarjeta usa sola la del tamaño que le llega. Se hacen acá mismo, sin bajar nada. " +
              "Los shaders GLSL no las necesitan: la placa los compila para cualquier tamaño."
    }
    ColumnLayout {
        id: varSec
        Layout.fillWidth: true
        spacing: 8
        readonly property var redes: panel.sr ? (panel.sr.modelos, panel.sr.pluginsUi()).filter(p => p.tipo === "red" && p.instalado) : []
        property string plugin: ""
        property var lista: []
        property var faltan: []
        readonly property string elegido: plugin !== "" ? plugin : (redes.length > 0 ? redes[0].id : "")
        function refrescar() {
            lista = panel.sr && elegido !== "" ? panel.sr.variantesUi(elegido) : []
            faltan = panel.sr && elegido !== "" ? panel.sr.tamanosSinVarianteUi(elegido) : []
        }
        onElegidoChanged: refrescar()
        Timer { interval: 3000; running: panel.visible; repeat: true; triggeredOnStart: true; onTriggered: varSec.refrescar() }
        Connections { target: panel.sr; function onCambio() { varSec.refrescar() } }
        Text {
            visible: varSec.redes.length === 0
            text: "No hay redes instaladas (Modelos, más abajo, o superres_instalar)."
            color: Theme.textFaint; font.pixelSize: 12
        }
        RowLayout {
            visible: varSec.redes.length > 0
            Layout.fillWidth: true
            spacing: 8
            Text { text: "Red"; color: Theme.textDim; font.pixelSize: 12 }
            ComboBox {
                Layout.preferredWidth: 280
                model: varSec.redes.map(p => p.nombre || p.id)
                currentIndex: Math.max(0, varSec.redes.findIndex(p => p.id === varSec.elegido))
                onActivated: (i) => varSec.plugin = varSec.redes[i].id
            }
            Text {
                Layout.fillWidth: true; elide: Text.ElideRight
                text: varSec.faltan.length > 0 ? "Cámaras sin variante: " + varSec.faltan.join(", ") : "Todas las cámaras del estudio tienen la suya."
                color: varSec.faltan.length > 0 ? Theme.amber : Theme.textFaint; font.pixelSize: 11
            }
            GlassButton {
                compact: true; text: "Crear las que faltan"
                enabled: varSec.faltan.length > 0 && !panel.tr.corriendo
                onClicked: panel.avisar(panel.sr.crearFaltantesUi(varSec.elegido), "Compilando " + varSec.faltan.join(", ") + "…")
            }
        }
        RowLayout {
            visible: varSec.redes.length > 0
            Layout.fillWidth: true
            spacing: 8
            Text { text: "Otro tamaño"; color: Theme.textDim; font.pixelSize: 12 }
            SpinBox { id: varAncho; from: 16; to: 4096; stepSize: 2; value: 1280; editable: true }
            Text { text: "×"; color: Theme.textDim }
            SpinBox { id: varAlto; from: 16; to: 4096; stepSize: 2; value: 720; editable: true }
            TextField { id: varPara; Layout.fillWidth: true; placeholderText: "Para qué (p. ej. «cámara del patio»)" }
            ComboBox {
                id: varMotores
                implicitWidth: 150
                model: [ { t: "Todo lo que tenga", v: "auto" }, { t: "Solo CoreML", v: "coreml" }, { t: "Solo ONNX", v: "onnx,onnx_nv12" } ]
                textRole: "t"
            }
            GlassButton {
                compact: true; active: true; text: "Crear"
                enabled: !panel.tr.corriendo
                onClicked: panel.avisar(panel.sr.crearVarianteUi(varSec.elegido, varAncho.value, varAlto.value, varPara.text,
                                                                 varMotores.model[varMotores.currentIndex].v),
                                        "Compilando " + varAncho.value + "x" + varAlto.value + "…")
            }
        }
        Text {
            visible: !!panel.tr.corriendo && String(panel.tr.que || "").indexOf("Variantes") === 0
            Layout.fillWidth: true; elide: Text.ElideRight
            text: (panel.tr.que || "") + " · " + (panel.tr.texto || "")
            color: Theme.accent; font.pixelSize: 11
        }
        Repeater {
            model: varSec.lista
            delegate: RowLayout {
                id: vr
                required property var modelData
                readonly property var en: modelData.entrada || {}
                readonly property var sa: modelData.salida || {}
                readonly property var med: (modelData.medido || {}).coreml || {}
                Layout.fillWidth: true
                spacing: 8
                Text { text: vr.en.ancho + "×" + vr.en.alto; color: Theme.text; font { pixelSize: 12; weight: Font.DemiBold } Layout.preferredWidth: 90 }
                Text {
                    Layout.preferredWidth: 300; elide: Text.ElideRight
                    text: "→ " + vr.sa.ancho + "×" + vr.sa.alto + " · " + Object.keys(vr.modelData.archivos || {}).join(", ") +
                          (vr.med.ms ? " · " + Number(vr.med.ms).toFixed(1) + " ms en " + (vr.med.acelerador || "?") : "") +
                          " · " + (vr.modelData.creada || "")
                    color: Theme.textDim; font.pixelSize: 11
                    ToolTip.visible: vhov.hovered; ToolTip.delay: 400; ToolTip.text: vr.modelData.descripcion || ""
                    HoverHandler { id: vhov }
                }
                TextField {
                    Layout.fillWidth: true
                    text: vr.modelData.para || ""
                    placeholderText: "Para qué"
                    onEditingFinished: if (text !== (vr.modelData.para || ""))
                        panel.avisar(panel.sr.editarVarianteUi(vr.modelData.plugin, vr.modelData.id, text, vr.modelData.descripcion || ""), "Guardado")
                }
                GlassButton {
                    compact: true; text: "✕"
                    onClicked: { panel.avisar(panel.sr.borrarVarianteUi(vr.modelData.plugin, vr.modelData.id), "Variante borrada"); varSec.refrescar() }
                }
            }
        }
    }

    // --- Plugins propios (elementos de GStreamer) ------------------------------------------
    Rectangle { Layout.fillWidth: true; height: 1; color: Theme.glassBorder }
    RowLayout {
        Layout.fillWidth: true
        Text { text: "Plugins propios (elementos de GStreamer)"; color: Theme.text; font { pixelSize: 14; weight: Font.DemiBold } Layout.fillWidth: true }
        GlassButton { compact: true; text: "Nuevo…"; onClicked: editorPropio.abrir(null) }
    }
    Text {
        Layout.fillWidth: true; wrapMode: Text.WordWrap
        color: Theme.textDim; font.pixelSize: 11
        text: "Cualquier elemento de GStreamer como método de superresolución: uno de GStreamer, o uno propio (por ejemplo una red " +
              "compilada con TensorRT para la placa) con su biblioteca en la carpeta gst/ del plugin. Si trabaja en memoria de la placa " +
              "(CUDA, OpenGL, Direct3D) la entrada se la da sin pasar por la CPU. Después se elige en la tarjeta como cualquier otro."
    }
    Text {
        visible: panel.propios.length === 0
        text: "No hay plugins propios."
        color: Theme.textFaint; font.pixelSize: 12
    }
    Repeater {
        model: panel.propios
        delegate: RowLayout {
            id: pp
            required property var modelData
            Layout.fillWidth: true
            spacing: 8
            Text { text: pp.modelData.nombre || pp.modelData.id; color: Theme.text; font.pixelSize: 12; Layout.preferredWidth: 220; elide: Text.ElideRight }
            Text {
                Layout.fillWidth: true; elide: Text.ElideRight
                text: pp.modelData.elemento + (pp.modelData.memoria ? " · " + pp.modelData.memoria : " · memoria de sistema") +
                      " · x" + (pp.modelData.escala || 2) + (pp.modelData.anda ? "" : " · ✕ " + (pp.modelData.error || "no anda acá"))
                color: pp.modelData.anda ? Theme.textDim : Theme.live; font.pixelSize: 11
            }
            GlassButton { compact: true; text: "Editar"; onClicked: editorPropio.abrir(pp.modelData) }
            GlassButton { compact: true; text: "✕"; onClicked: { panel.avisar(panel.sr.borrarPluginUi(pp.modelData.id), "Plugin borrado"); panel.refrescar() } }
        }
    }
    Popup {
        id: editorPropio
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(parent.width - 60, 720)
        height: Math.min(parent.height - 60, 680)
        modal: true
        padding: 16
        background: Rectangle { color: Theme.panel; radius: Theme.radiusSmall; border.color: Theme.glassBorder }
        property string pid: ""
        property string nombre: ""
        property string elemento: ""
        property string memoria: ""
        property bool saleSistema: false
        property int escala: 2
        property string descripcion: ""
        property var valores: ({})
        property string busca: ""
        readonly property var modulos: panel.vg ? panel.vg.modulosGst() : []
        readonly property var visibles: modulos.filter(m => busca === "" || (m.elemento + " " + (m.nombre || "")).toLowerCase().indexOf(busca.toLowerCase()) >= 0)
        function abrir(p) {
            pid = p ? p.id : ""; nombre = p ? (p.nombre || "") : ""; elemento = p ? p.elemento : ""
            memoria = p ? (p.memoria || "") : ""; saleSistema = p ? (p.memoria_salida === "") : false
            escala = p ? (p.escala || 2) : 2; descripcion = p ? (p.descripcion || "") : ""
            valores = p ? Object.assign({}, p.propiedades || {}) : ({}); busca = ""
            open()
        }
        ColumnLayout {
            anchors.fill: parent
            spacing: 8
            RowLayout {
                Layout.fillWidth: true
                Text { text: "Plugin propio"; color: Theme.text; font { pixelSize: 16; weight: Font.DemiBold } Layout.fillWidth: true }
                GlassButton { compact: true; text: "Cancelar"; onClicked: editorPropio.close() }
                GlassButton {
                    compact: true; active: true; text: "Guardar"
                    onClicked: {
                        const p = { id: editorPropio.pid, nombre: editorPropio.nombre, elemento: editorPropio.elemento,
                                    memoria: editorPropio.memoria, escala: editorPropio.escala, descripcion: editorPropio.descripcion,
                                    propiedades: editorPropio.valores }
                        if (editorPropio.saleSistema) p.memoria_salida = ""
                        const e = panel.sr.guardarPluginElementoUi(p)
                        if (e !== "" && e.indexOf("guardado") !== 0) { Studio.notify(e, "error"); return }
                        Studio.notify(e !== "" ? e : "Plugin guardado: ya se puede elegir en la tarjeta", e !== "" ? "error" : "info")
                        panel.refrescar()
                        editorPropio.close()
                    }
                }
            }
            GridLayout {
                columns: 2
                Layout.fillWidth: true
                columnSpacing: 8; rowSpacing: 6
                Text { text: "Id"; color: Theme.textDim; font.pixelSize: 12 }
                TextField { Layout.fillWidth: true; text: editorPropio.pid; placeholderText: "mi-red-trt (letras, números, guiones)"; onTextEdited: editorPropio.pid = text }
                Text { text: "Nombre"; color: Theme.textDim; font.pixelSize: 12 }
                TextField { Layout.fillWidth: true; text: editorPropio.nombre; onTextEdited: editorPropio.nombre = text }
                Text { text: "Elemento"; color: Theme.textDim; font.pixelSize: 12 }
                RowLayout {
                    Layout.fillWidth: true
                    TextField { Layout.preferredWidth: 150; placeholderText: "Buscar"; text: editorPropio.busca; onTextEdited: editorPropio.busca = text }
                    ComboBox {
                        Layout.fillWidth: true
                        model: editorPropio.visibles
                        textRole: "elemento"
                        displayText: editorPropio.elemento !== "" ? editorPropio.elemento : "— elegí un elemento —"
                        onActivated: (i) => { editorPropio.elemento = editorPropio.visibles[i].elemento; editorPropio.valores = ({}) }
                    }
                    // Uno propio que todavía no está registrado: el nombre a mano.
                    TextField { Layout.preferredWidth: 140; placeholderText: "o el nombre a mano"; onEditingFinished: if (text !== "") editorPropio.elemento = text }
                }
                Text { text: "Memoria"; color: Theme.textDim; font.pixelSize: 12 }
                RowLayout {
                    ComboBox {
                        model: [ { t: "Sistema (CPU)", v: "" }, { t: "CUDA (NVIDIA)", v: "CUDAMemory" }, { t: "OpenGL", v: "GLMemory" },
                                 { t: "Direct3D 11", v: "D3D11Memory" }, { t: "Direct3D 12", v: "D3D12Memory" } ]
                        textRole: "t"
                        currentIndex: Math.max(0, model.findIndex(o => o.v === editorPropio.memoria))
                        onActivated: (i) => editorPropio.memoria = model[i].v
                    }
                    CheckBox { visible: editorPropio.memoria !== ""; text: "Entrega en memoria de sistema"; checked: editorPropio.saleSistema; onToggled: editorPropio.saleSistema = checked }
                }
                Text { text: "Agranda"; color: Theme.textDim; font.pixelSize: 12 }
                SpinBox { from: 1; to: 4; value: editorPropio.escala; onValueModified: editorPropio.escala = value }
                Text { text: "Descripción"; color: Theme.textDim; font.pixelSize: 12 }
                TextField { Layout.fillWidth: true; text: editorPropio.descripcion; onTextEdited: editorPropio.descripcion = text }
            }
            Text {
                Layout.fillWidth: true; wrapMode: Text.WordWrap
                text: "Propiedades del elemento. En un texto, {dir} es la carpeta del plugin (para un modelo o un .engine que dejes ahí)."
                color: Theme.textFaint; font.pixelSize: 11
            }
            ScrollView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                contentWidth: availableWidth
                clip: true
                FormularioPropiedades {
                    width: parent ? parent.width : 400
                    props: editorPropio.elemento !== "" && panel.vg ? panel.vg.propiedadesModuloCon(editorPropio.elemento, editorPropio.valores) : []
                    onCambio: (n, v) => { const x = Object.assign({}, editorPropio.valores); x[n] = String(v); editorPropio.valores = x }
                }
            }
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
