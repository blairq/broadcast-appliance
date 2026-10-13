// PanelCalibracion.qml — la pestaña Calibración del Mezclador (docs/planes/PLAN-CALIBRACION-SALA.md)
//
// Solo presenta y llama: todo lo hace Studio.mezclador.calibracion (audio::Calibracion),
// que es también lo que maneja el asistente (calibracion_*).
//   - Izquierda: la sesión (sala y medidas), los parlantes, los micrófonos (con su
//     calibración) y la curva objetivo.
//   - Centro: el croquis (clic en la sala: un punto nuevo; arrastrar: mover; cada
//     punto dice si ya está medido para el parlante elegido) y el control de la
//     medición (parlante, micrófono, punto, etapa, nivel, duración).
//   - Derecha: las curvas (cada punto, el promedio, el objetivo, lo previsto con el
//     EQ y la verificación), la respuesta impulsiva y la curva energía-tiempo, el RT60
//     por banda y la propuesta con el motivo de cada filtro (Aplicar / Revertir).
//   - Abajo: el asistente con pedidos ya escritos.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import BA

Item {
    id: c
    readonly property var cal: Studio.mezclador ? Studio.mezclador.calibracion : null
    readonly property var ses: cal ? cal.sesion : ({})
    readonly property bool hay: ses.id !== undefined
    readonly property var sala: ses.sala || ({ largo: 8, ancho: 6, alto: 3 })
    readonly property var parlantes: ses.parlantes || []
    readonly property var microfonos: ses.microfonos || []
    readonly property var puntos: ses.puntos || []
    readonly property var mediciones: ses.mediciones || []
    readonly property bool ocupado: cal ? cal.estado !== "" : false

    property string parlante: parlantes.length ? parlantes[0].id : ""
    property string microfono: microfonos.length ? microfonos[0].id : ""
    property string punto: ""
    property string etapa: "antes"
    property real nivelDb: -20
    property real duracion: 4
    readonly property string vista: cal ? cal.vista : "respuesta" // del modelo (mezclador_pestana)
    property string tipoPunto: "escucha"
    property bool confirmado: false     // ya se avisó que suena (por sesión)
    property var curvas: ({})

    onParlanteChanged: releerCurvas()
    Connections {
        target: c.cal
        function onAnalisisCambio() { c.releerCurvas() }
        function onSesionCambio() {
            if (c.parlante === "" || !c.buscar(c.parlantes, c.parlante)) c.parlante = c.parlantes.length ? c.parlantes[0].id : ""
            if (c.microfono === "" || !c.buscar(c.microfonos, c.microfono)) c.microfono = c.microfonos.length ? c.microfonos[0].id : ""
        }
        function onMedicionTerminada(id, error) {
            if (error !== "" || id === "") return
            // Al siguiente punto sin medir: el micrófono se mueve a mano.
            const sig = c.siguienteSinMedir()
            if (sig !== "") c.punto = sig
        }
    }
    Component.onCompleted: releerCurvas()
    function releerCurvas() { curvas = cal && parlante !== "" ? cal.curvas(parlante) : ({}) }
    function buscar(lista, id) { for (let i = 0; i < lista.length; ++i) if (lista[i].id === id) return lista[i]; return null }
    function nombreDe(lista, id) { const o = buscar(lista, id); return o ? o.nombre : "—" }
    function medido(pt, et) {
        for (let i = 0; i < mediciones.length; ++i) {
            const m = mediciones[i]
            if (m.punto === pt && m.parlante === parlante && (m.etapa || "antes") === et) return true
        }
        return false
    }
    function siguienteSinMedir() {
        for (let i = 0; i < puntos.length; ++i)
            if (puntos[i].tipo === "escucha" && !medido(puntos[i].id, etapa)) return puntos[i].id
        return ""
    }
    function aviso(e) { if (e !== "") Studio.notify(e, "error") }
    function simulada() {
        const p = buscar(parlantes, parlante)
        return p && p.salida && p.salida.dispositivo === "sala-simulada"
    }
    function medir() {
        if (punto === "") { aviso("Elegí un punto en el croquis"); return }
        if (!simulada() && !confirmado) { confirmar.open(); return }
        aviso(cal.medir(parlante, punto, microfono, etapa, duracion, nivelDb))
    }
    function fmt(v, d) { return v === undefined || v === null || v < 0 ? "—" : Number(v).toFixed(d) }

    // --- Antes del primer barrido de verdad -------------------------------------------
    Dialog {
        id: confirmar
        anchors.centerIn: parent
        modal: true
        title: "Va a sonar un barrido de 20 Hz a 20 kHz"
        property int falta: 3
        onOpened: { falta = 3; cuenta.start() }
        Timer { id: cuenta; interval: 1000; repeat: true; onTriggered: { confirmar.falta--; if (confirmar.falta <= 0) stop() } }
        ColumnLayout {
            width: 460
            spacing: 8
            Label { Layout.fillWidth: true; wrapMode: Text.WordWrap
                    text: "Por «" + c.nombreDe(c.parlantes, c.parlante) + "», a " + c.nivelDb.toFixed(0) + " dBFS. Antes de medir:" }
            Label { Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.textDim
                    text: "• Que no haya público ni nadie con auriculares.\n• Los micrófonos en vivo cerrados (si no, acopla).\n• El volumen del equipo bajo: se sube de a poco hasta que la relación señal/ruido pase los 40 dB.\n• El micrófono en el punto, apuntado al parlante y a más de 1 m." }
        }
        footer: DialogButtonBox {
            Button { text: confirmar.falta > 0 ? "Medir (" + confirmar.falta + ")" : "Medir"; enabled: confirmar.falta <= 0
                     DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole }
            Button { text: "Cancelar"; DialogButtonBox.buttonRole: DialogButtonBox.RejectRole }
        }
        onAccepted: { c.confirmado = true; c.medir() }
    }

    // --- Piezas ---------------------------------------------------------------------
    component Titulo: Text {
        color: Theme.accentSoft
        font { family: Theme.font; pixelSize: 11; weight: Font.Bold; letterSpacing: 1.1 }
    }
    component Nota: Text {
        color: Theme.textFaint
        wrapMode: Text.WordWrap
        font { family: Theme.font; pixelSize: 11 }
    }
    component Caja: Rectangle {
        radius: 12
        color: Qt.rgba(1, 1, 1, 0.035)
        border.color: Qt.rgba(1, 1, 1, 0.08)
    }
    component Numero: SpinBox {
        id: nb
        property real real: value / 100
        property real desdeR: 0
        property real hastaR: 100
        from: desdeR * 100; to: hastaR * 100; stepSize: 10
        editable: true
        Layout.preferredWidth: 96
        textFromValue: (v) => (v / 100).toFixed(1)
        valueFromText: (t) => Math.round(parseFloat(t.replace(",", ".")) * 100)
        validator: DoubleValidator { bottom: nb.desdeR; top: nb.hastaR }
    }

    // Un gráfico en frecuencia (log, 20 Hz–20 kHz): curvas [{datos, color, ancho, guiones}].
    component GraficoFrecuencia: Canvas {
        id: gf
        property var grilla: c.cal ? c.cal.grilla : []
        property var series: []
        property var marcas: []      // filtros: [{f, ganancia_db}]
        property real fs: 0          // frecuencia de Schroeder (se sombrea lo de abajo)
        property real rango: 36      // dB visibles
        onSeriesChanged: requestPaint()
        onMarcasChanged: requestPaint()
        onWidthChanged: requestPaint()
        onHeightChanged: requestPaint()
        function xDe(f) { return 44 + (width - 56) * Math.log(f / 20) / Math.log(1000) }
        onPaint: {
            const g = getContext("2d")
            g.reset()
            g.clearRect(0, 0, width, height)
            const izq = 44, der = width - 12, arr = 10, aba = height - 22
            // La escala de dB: centrada en la mediana del promedio (o de la primera serie).
            let ref = 0, hay = false
            for (let s = 0; s < series.length && !hay; ++s) {
                const d = series[s].datos
                if (d && d.length === grilla.length) {
                    const v = d.slice(60, 200).slice().sort((a, b) => a - b)
                    ref = v[Math.floor(v.length / 2)]
                    hay = true
                }
            }
            // El rango: lo que ocupa la curva más gruesa (la última), entre 36 y 60 dB.
            let mn = 1e9, mx = -1e9
            const ult = series.length ? series[series.length - 1].datos : null
            if (ult && ult.length === grilla.length) for (let i = 10; i < ult.length; ++i) { mn = Math.min(mn, ult[i]); mx = Math.max(mx, ult[i]) }
            const r = mx > mn ? Math.min(60, Math.max(rango, Math.ceil((mx - mn + 12) / 6) * 6)) : rango
            if (mx > mn) ref = (mx + mn) / 2
            const top = Math.ceil((ref + r / 2) / 6) * 6, bot = top - r
            const yDe = (db) => arr + (aba - arr) * (top - db) / (top - bot)
            // Lo de abajo de fs: modos (corrección fina posible).
            if (fs > 20) {
                g.fillStyle = Qt.rgba(1, 0.42, 0.17, 0.06)
                g.fillRect(izq, arr, xDe(fs) - izq, aba - arr)
                g.fillStyle = Theme.textFaint
                g.font = "10px sans-serif"
                g.fillText("modos (bajo fs = " + fs.toFixed(0) + " Hz)", izq + 4, aba - 6)
            }
            g.lineWidth = 1
            g.font = "10px sans-serif"
            for (const f of [31.5, 63, 125, 250, 500, 1000, 2000, 4000, 8000, 16000]) {
                const x = xDe(f)
                g.strokeStyle = Qt.rgba(1, 1, 1, 0.08)
                g.beginPath(); g.moveTo(x, arr); g.lineTo(x, aba); g.stroke()
                g.fillStyle = Theme.textFaint
                g.fillText(f >= 1000 ? (f / 1000) + "k" : String(f), x - 8, height - 6)
            }
            for (let db = bot; db <= top; db += (r > 42 ? 12 : 6)) {
                const y = yDe(db)
                g.strokeStyle = Qt.rgba(1, 1, 1, 0.07)
                g.beginPath(); g.moveTo(izq, y); g.lineTo(der, y); g.stroke()
                g.fillStyle = Theme.textFaint
                g.fillText(db.toFixed(0) + " dB", 4, y + 3)
            }
            for (const s of series) {
                const d = s.datos
                if (!d || d.length !== grilla.length) continue
                g.strokeStyle = s.color
                g.lineWidth = s.ancho || 1
                g.globalAlpha = s.alfa !== undefined ? s.alfa : 1
                g.setLineDash ? g.setLineDash(s.guiones ? [6, 4] : []) : 0
                g.beginPath()
                for (let i = 0; i < d.length; ++i) {
                    const x = xDe(grilla[i]), y = Math.max(arr, Math.min(aba, yDe(d[i])))
                    if (i === 0) g.moveTo(x, y); else g.lineTo(x, y)
                }
                g.stroke()
                g.globalAlpha = 1
            }
            g.setLineDash ? g.setLineDash([]) : 0
            // Los filtros de la propuesta: un triángulo en su frecuencia (abajo: corte).
            for (const f of marcas) {
                const x = xDe(f.f), y = aba - 4
                g.fillStyle = f.ganancia_db < 0 ? "#38bdf8" : "#f5b83d"
                g.beginPath(); g.moveTo(x - 5, y); g.lineTo(x + 5, y); g.lineTo(x, y - 8); g.closePath(); g.fill()
            }
        }
    }

    // --- Sin ba-lspdsp o sin sesión ---------------------------------------------------
    Text {
        anchors.centerIn: parent
        visible: !c.cal || !c.cal.disponible
        width: 520
        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.WordWrap
        color: Theme.textDim
        text: "La calibración necesita ba-lspdsp (el DSP de LSP) y no está en esta compilación."
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 10
        visible: c.cal && c.cal.disponible

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 10

            // ============ Izquierda: sesión, parlantes, micrófonos ============
            Caja {
                Layout.fillHeight: true
                Layout.preferredWidth: 330
                Layout.maximumWidth: 330
                ScrollView {
                    anchors { fill: parent; margins: 12 }
                    contentWidth: availableWidth
                    clip: true
                    ColumnLayout {
                        width: parent.width
                        spacing: 8

                        Titulo { text: "SESIÓN" }
                        RowLayout {
                            Layout.fillWidth: true
                            ComboBox {
                                id: selSesion
                                Layout.fillWidth: true
                                model: c.cal ? c.cal.sesiones : []
                                textRole: "nombre"
                                displayText: c.hay ? c.ses.nombre : "Elegí una guardada…"
                                onActivated: (i) => c.aviso(c.cal.abrir(model[i].id))
                            }
                            GlassButton { compact: true; text: "Nueva"; onClicked: nuevaSesion.visible = !nuevaSesion.visible }
                        }
                        ColumnLayout {
                            id: nuevaSesion
                            visible: !c.hay
                            Layout.fillWidth: true
                            spacing: 6
                            TextField { id: nomSala; Layout.fillWidth: true; placeholderText: "Nombre (p. ej. Recinto principal)" }
                            RowLayout {
                                Nota { text: "L × A × H (m)" }
                                Numero { id: nL; desdeR: 1; hastaR: 100; value: 800 }
                                Numero { id: nA; desdeR: 1; hastaR: 100; value: 600 }
                                Numero { id: nH; desdeR: 1; hastaR: 30; value: 300 }
                            }
                            GlassButton {
                                text: "Crear sesión"
                                onClicked: { c.aviso(c.cal.nueva(nomSala.text, nL.real, nA.real, nH.real)); if (c.hay) nuevaSesion.visible = false }
                            }
                        }
                        Nota {
                            visible: c.hay
                            Layout.fillWidth: true
                            text: "Sala de " + c.sala.largo + " × " + c.sala.ancho + " × " + c.sala.alto + " m (" +
                                  (c.sala.largo * c.sala.ancho * c.sala.alto).toFixed(0) + " m³). " +
                                  (c.cal.analisis.schroeder_hz ? "Frecuencia de Schroeder ≈ " + c.cal.analisis.schroeder_hz + " Hz: abajo mandan los modos." : "")
                        }

                        // --- Parlantes
                        Titulo { text: "PARLANTES"; visible: c.hay; Layout.topMargin: 6 }
                        Repeater {
                            model: c.hay ? c.parlantes : []
                            delegate: Rectangle {
                                required property var modelData
                                Layout.fillWidth: true
                                height: 34; radius: 8
                                color: c.parlante === modelData.id ? Qt.rgba(1, 0.42, 0.17, 0.16) : Qt.rgba(1, 1, 1, 0.04)
                                border.color: c.parlante === modelData.id ? Theme.accent : "transparent"
                                RowLayout {
                                    anchors { fill: parent; leftMargin: 10; rightMargin: 4 }
                                    Text { text: "▲"; color: "#a78bfa"; font.pixelSize: 11 }
                                    Text { Layout.fillWidth: true; text: modelData.nombre; color: Theme.text; elide: Text.ElideRight
                                           font { family: Theme.font; pixelSize: 12 } }
                                    Text { text: "canal " + (modelData.salida.canal < 0 ? "todos" : modelData.salida.canal + 1); color: Theme.textFaint
                                           font { family: Theme.font; pixelSize: 10 } }
                                    ToolButton { text: "✕"; onClicked: c.aviso(c.cal.quitar(modelData.id))
                                                 ToolTip.visible: hovered; ToolTip.text: "Quitar el parlante (sus mediciones quedan)" }
                                }
                                TapHandler { onTapped: c.parlante = modelData.id }
                            }
                        }
                        GridLayout {
                            visible: c.hay
                            Layout.fillWidth: true
                            columns: 2
                            columnSpacing: 6; rowSpacing: 6
                            TextField { id: nomPar; Layout.columnSpan: 2; Layout.fillWidth: true; placeholderText: "Nombre del parlante" }
                            ComboBox { id: salPar; Layout.fillWidth: true; model: c.cal ? c.cal.salidas : []; textRole: "nombre"; valueRole: "id" }
                            ComboBox { id: canPar; Layout.preferredWidth: 96; model: ["Izq.", "Der.", "Todos"] }
                            GlassButton {
                                Layout.columnSpan: 2
                                text: "+ Parlante"
                                compact: true
                                onClicked: c.aviso(c.cal.agregarParlante(nomPar.text, salPar.currentValue, canPar.currentIndex === 2 ? -1 : canPar.currentIndex,
                                                                         0.5, c.sala.ancho * (c.parlantes.length % 2 ? 0.75 : 0.25)))
                            }
                        }

                        // --- Micrófonos
                        Titulo { text: "MICRÓFONOS"; visible: c.hay; Layout.topMargin: 6 }
                        Repeater {
                            model: c.hay ? c.microfonos : []
                            delegate: Rectangle {
                                required property var modelData
                                readonly property var calb: modelData.calibracion || ({})
                                Layout.fillWidth: true
                                implicitHeight: micCol.implicitHeight + 10
                                radius: 8
                                color: c.microfono === modelData.id ? Qt.rgba(1, 0.42, 0.17, 0.16) : Qt.rgba(1, 1, 1, 0.04)
                                border.color: c.microfono === modelData.id ? Theme.accent : "transparent"
                                ColumnLayout {
                                    id: micCol
                                    anchors { left: parent.left; right: parent.right; verticalCenter: parent.verticalCenter; leftMargin: 10; rightMargin: 4 }
                                    spacing: 0
                                    RowLayout {
                                        Text { text: "●"; color: "#22c55e"; font.pixelSize: 10 }
                                        Text { Layout.fillWidth: true; text: modelData.nombre; color: Theme.text; elide: Text.ElideRight
                                               font { family: Theme.font; pixelSize: 12 } }
                                        ToolButton { text: "✕"; onClicked: c.aviso(c.cal.quitar(modelData.id)) }
                                    }
                                    Text {
                                        Layout.fillWidth: true
                                        Layout.bottomMargin: 4
                                        wrapMode: Text.WordWrap
                                        color: calb.tipo === "archivo" || calb.tipo === "sustitucion" ? Theme.ok : Theme.amber
                                        font { family: Theme.font; pixelSize: 10 }
                                        text: calb.tipo === "archivo" ? "Con archivo de calibración (" + (calb.archivo || "") + ")"
                                            : calb.tipo === "sustitucion" ? "Calibrado por sustitución contra " + (calb.referencia || "la referencia")
                                            : calb.tipo === "plano" ? "De medición, sin archivo (plano)"
                                            : "Curva del modelo (" + (calb.modelo || "?") + ", ±3 dB): agudos solo tendencias"
                                    }
                                }
                                TapHandler { onTapped: c.microfono = modelData.id }
                            }
                        }
                        GridLayout {
                            visible: c.hay
                            Layout.fillWidth: true
                            columns: 2
                            columnSpacing: 6; rowSpacing: 6
                            TextField { id: nomMic; Layout.columnSpan: 2; Layout.fillWidth: true; placeholderText: "Nombre del micrófono" }
                            ComboBox { id: entMic; Layout.fillWidth: true; model: c.cal ? c.cal.entradas : []; textRole: "nombre"; valueRole: "id" }
                            SpinBox { id: canMic; Layout.preferredWidth: 96; from: 1; to: 8; value: 1
                                      ToolTip.visible: hovered; ToolTip.text: "Canal de la entrada" }
                            ComboBox {
                                id: modMic
                                Layout.columnSpan: 2
                                Layout.fillWidth: true
                                model: c.cal ? c.cal.modelosMicrofono : []
                                textRole: "nombre"; valueRole: "id"
                                ToolTip.visible: hovered && currentIndex >= 0; ToolTip.delay: 400
                                ToolTip.text: currentIndex >= 0 ? model[currentIndex].nota : ""
                            }
                            TextField { id: archMic; Layout.columnSpan: 2; Layout.fillWidth: true
                                        placeholderText: "Archivo de calibración (opcional: ruta al .txt)" }
                            GlassButton {
                                Layout.columnSpan: 2
                                text: "+ Micrófono"
                                compact: true
                                onClicked: {
                                    const arch = archMic.text.trim()
                                    const id = modMic.currentValue
                                    c.aviso(c.cal.agregarMicrofono(nomMic.text, entMic.currentValue, canMic.value - 1,
                                                                   arch !== "" ? "archivo" : (id === "plano" ? "plano" : "modelo"), id, arch))
                                }
                            }
                        }

                        // --- Objetivo
                        Titulo { text: "CURVA OBJETIVO"; visible: c.hay; Layout.topMargin: 6 }
                        RowLayout {
                            visible: c.hay
                            Layout.fillWidth: true
                            Slider {
                                id: pend
                                Layout.fillWidth: true
                                from: -2; to: 0; stepSize: 0.1
                                value: c.ses.objetivo ? c.ses.objetivo.pendiente_db_oct : -0.8
                                onPressedChanged: if (!pressed) c.aviso(c.cal.setObjetivo(value))
                            }
                            Text { text: pend.value.toFixed(1) + " dB/oct"; color: Theme.textDim; font { family: Theme.font; pixelSize: 11 } }
                        }
                        Nota { visible: c.hay; Layout.fillWidth: true
                               text: "0 = plana (monitoreo). −0,8 = palabra en sala (Toole: en régimen permanente la sala cae hacia los agudos)." }
                    }
                }
            }

            // ============ Centro: croquis y medición ============
            ColumnLayout {
                Layout.fillHeight: true
                Layout.fillWidth: false
                Layout.preferredWidth: 400
                Layout.maximumWidth: 400
                spacing: 10

                Caja {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    ColumnLayout {
                        anchors { fill: parent; margins: 12 }
                        spacing: 6
                        RowLayout {
                            Layout.fillWidth: true
                            Titulo { text: "CROQUIS"; Layout.fillWidth: true }
                            Nota { text: "clic: punto nuevo de" }
                            ComboBox {
                                Layout.preferredWidth: 130
                                model: [{ t: "escucha", n: "escucha" }, { t: "cerca", n: "cerca (1 m)" }, { t: "pared", n: "pared" }, { t: "microfono", n: "micrófono" }]
                                textRole: "n"; valueRole: "t"
                                onActivated: c.tipoPunto = currentValue
                            }
                        }
                        // La sala a escala (x a lo largo, hacia abajo; los parlantes adelante, arriba).
                        Item {
                            id: plano
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            readonly property real esc: Math.min(width / Math.max(1, c.sala.ancho), height / Math.max(1, c.sala.largo))
                            Rectangle {
                                id: sala
                                width: c.sala.ancho * plano.esc
                                height: c.sala.largo * plano.esc
                                anchors.centerIn: parent
                                color: Qt.rgba(1, 1, 1, 0.03)
                                border.color: Qt.rgba(1, 1, 1, 0.25)
                                visible: c.hay
                                Text { anchors { top: parent.top; horizontalCenter: parent.horizontalCenter; topMargin: 2 }
                                       text: "frente"; color: Theme.textFaint; font { family: Theme.font; pixelSize: 9 } }
                                // Cuadrícula de 1 m.
                                Repeater {
                                    model: Math.floor(c.sala.largo)
                                    Rectangle { required property int index; x: 0; y: (index + 1) * plano.esc; width: sala.width; height: 1; color: Qt.rgba(1, 1, 1, 0.04) }
                                }
                                Repeater {
                                    model: Math.floor(c.sala.ancho)
                                    Rectangle { required property int index; y: 0; x: (index + 1) * plano.esc; height: sala.height; width: 1; color: Qt.rgba(1, 1, 1, 0.04) }
                                }
                                TapHandler {
                                    enabled: c.hay
                                    onTapped: (ev) => c.aviso(c.cal.agregarPunto("", c.tipoPunto, ev.position.y / plano.esc, ev.position.x / plano.esc))
                                }
                                // Puntos.
                                Repeater {
                                    model: c.puntos
                                    delegate: Rectangle {
                                        id: pt
                                        required property var modelData
                                        required property int index
                                        readonly property bool hecho: c.medido(modelData.id, c.etapa)
                                        width: 26; height: 26; radius: modelData.tipo === "pared" ? 4 : 13
                                        x: modelData.y * plano.esc - width / 2
                                        y: modelData.x * plano.esc - height / 2
                                        color: hecho ? Qt.rgba(0.21, 0.77, 0.42, 0.85) : Qt.rgba(1, 1, 1, 0.12)
                                        border.width: c.punto === modelData.id ? 3 : 1
                                        border.color: c.punto === modelData.id ? Theme.accent : Qt.rgba(1, 1, 1, 0.5)
                                        Text { anchors.centerIn: parent; text: modelData.id.substring(2); color: "white"
                                               font { family: Theme.font; pixelSize: 11; weight: Font.Bold } }
                                        HoverHandler { id: ptHov }
                                        ToolTip.visible: ptHov.hovered; ToolTip.delay: 300
                                        ToolTip.text: modelData.nombre + " (" + modelData.tipo + ")" + (hecho ? " — medido" : " — sin medir") + ". Arrastrá para moverlo."
                                        TapHandler { onTapped: c.punto = pt.modelData.id }
                                        DragHandler {
                                            onActiveChanged: if (!active) c.aviso(c.cal.mover(pt.modelData.id, (pt.y + pt.height / 2) / plano.esc, (pt.x + pt.width / 2) / plano.esc))
                                        }
                                    }
                                }
                                // Parlantes.
                                Repeater {
                                    model: c.parlantes
                                    delegate: Rectangle {
                                        id: pa
                                        required property var modelData
                                        width: 30; height: 22; radius: 4
                                        x: modelData.y * plano.esc - width / 2
                                        y: modelData.x * plano.esc - height / 2
                                        color: c.parlante === modelData.id ? "#a78bfa" : Qt.rgba(0.65, 0.55, 0.98, 0.4)
                                        border.color: "white"
                                        Text { anchors.centerIn: parent; text: "◢◣"; color: "white"; font.pixelSize: 9 }
                                        HoverHandler { id: paHov }
                                        ToolTip.visible: paHov.hovered; ToolTip.text: modelData.nombre + " (arrastrá para ubicarlo)"
                                        TapHandler { onTapped: c.parlante = pa.modelData.id }
                                        DragHandler {
                                            onActiveChanged: if (!active) c.aviso(c.cal.mover(pa.modelData.id, (pa.y + pa.height / 2) / plano.esc, (pa.x + pa.width / 2) / plano.esc))
                                        }
                                    }
                                }
                            }
                            Text {
                                anchors.centerIn: parent
                                visible: !c.hay
                                width: parent.width - 40
                                horizontalAlignment: Text.AlignHCenter
                                wrapMode: Text.WordWrap
                                color: Theme.textFaint
                                text: "Creá una sesión con las medidas de la sala (a la izquierda) o pedile al asistente que te guíe."
                            }
                        }
                        Nota {
                            Layout.fillWidth: true
                            visible: c.hay
                            text: "Puntos de escucha: 5 a 9, a la altura de los oídos, a más de 1 m de las paredes y no simétricos. Verde: medido para «" +
                                  c.nombreDe(c.parlantes, c.parlante) + "» (" + c.etapa + ")."
                        }
                    }
                }

                // La medición.
                Caja {
                    Layout.fillWidth: true
                    Layout.preferredHeight: medCol.implicitHeight + 24
                    visible: c.hay
                    ColumnLayout {
                        id: medCol
                        anchors { left: parent.left; right: parent.right; top: parent.top; margins: 12 }
                        spacing: 8
                        RowLayout {
                            Layout.fillWidth: true
                            Titulo { text: "MEDIR"; Layout.fillWidth: true }
                            Repeater {
                                model: [{ t: "antes", n: "Antes" }, { t: "verificacion", n: "Verificación" }, { t: "sustitucion", n: "Sustitución" }]
                                delegate: GlassButton {
                                    required property var modelData
                                    compact: true
                                    text: modelData.n
                                    active: c.etapa === modelData.t
                                    onClicked: c.etapa = modelData.t
                                }
                            }
                        }
                        Text {
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                            color: Theme.text
                            font { family: Theme.font; pixelSize: 13 }
                            text: c.punto === "" ? "Elegí un punto en el croquis."
                                : "«" + c.nombreDe(c.parlantes, c.parlante) + "» con «" + c.nombreDe(c.microfonos, c.microfono) + "» en «" + c.nombreDe(c.puntos, c.punto) + "»"
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 8
                            Nota { text: "Nivel" }
                            Slider { id: nivel; Layout.fillWidth: true; from: -40; to: -6; stepSize: 1; value: c.nivelDb; onMoved: c.nivelDb = value }
                            Text { text: c.nivelDb.toFixed(0) + " dBFS"; color: c.nivelDb > -12 ? Theme.amber : Theme.textDim; font { family: Theme.font; pixelSize: 11 } }
                            Nota { text: "Barrido" }
                            ComboBox { Layout.preferredWidth: 76; model: ["2 s", "4 s", "8 s"]; currentIndex: 1
                                       onActivated: (i) => c.duracion = [2, 4, 8][i]
                                       ToolTip.visible: hovered; ToolTip.text: "Más largo: más relación señal/ruido (+3 dB cada vez que se duplica)" }
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            GlassButton {
                                text: c.ocupado ? "Midiendo…" : (c.simulada() ? "Medir (sala simulada)" : "Medir este punto")
                                active: !c.ocupado
                                enabled: !c.ocupado && c.parlante !== "" && c.microfono !== "" && c.punto !== ""
                                onClicked: c.medir()
                            }
                            GlassButton { compact: true; text: "Cancelar"; visible: c.ocupado; onClicked: c.cal.cancelar() }
                            BusyIndicator { running: c.ocupado; visible: running; implicitWidth: 26; implicitHeight: 26 }
                            Item { Layout.fillWidth: true }
                        }
                        Text {
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                            visible: c.cal && c.cal.mensaje !== ""
                            text: c.cal ? c.cal.mensaje : ""
                            color: text.indexOf("No se pudo") === 0 || text.indexOf("baja") >= 0 ? Theme.amber : Theme.textDim
                            font { family: Theme.font; pixelSize: 11 }
                        }
                    }
                }
            }

            // ============ Derecha: resultados ============
            Caja {
                Layout.fillWidth: true
                Layout.fillHeight: true
                ColumnLayout {
                    anchors { fill: parent; margins: 12 }
                    spacing: 8
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 6
                        Repeater {
                            model: [{ t: "respuesta", n: "Respuesta" }, { t: "impulso", n: "Impulso y ETC" }, { t: "rt", n: "RT60" }, { t: "propuesta", n: "Propuesta" }]
                            delegate: GlassButton {
                                required property var modelData
                                compact: true
                                text: modelData.n
                                active: c.vista === modelData.t
                                onClicked: c.cal.vista = modelData.t
                            }
                        }
                        Item { Layout.fillWidth: true }
                        Text { text: c.nombreDe(c.parlantes, c.parlante); color: "#a78bfa"; font { family: Theme.font; pixelSize: 12; weight: Font.DemiBold } }
                    }

                    // --- Respuesta en frecuencia
                    ColumnLayout {
                        visible: c.vista === "respuesta"
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        GraficoFrecuencia {
                            id: grafResp
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            fs: c.cal && c.cal.analisis.schroeder_hz ? c.cal.analisis.schroeder_hz : 0
                            marcas: c.curvas.filtros || []
                            series: {
                                const s = []
                                const pts = c.curvas.puntos || []
                                for (let i = 0; i < pts.length; ++i) s.push({ datos: pts[i].curva, color: "#9ca3af", ancho: 1, alfa: 0.35 })
                                if (c.curvas.objetivo) s.push({ datos: c.curvas.objetivo, color: "#f4f4f6", ancho: 1.5, guiones: true })
                                if (c.curvas.previsto && verPrevisto.checked) s.push({ datos: c.curvas.previsto, color: "#38bdf8", ancho: 2 })
                                if (c.curvas.despues) s.push({ datos: c.curvas.despues, color: "#35c46b", ancho: 2.5 })
                                if (c.curvas.promedio) s.push({ datos: c.curvas.promedio, color: "#ff6a2b", ancho: 2.5 })
                                return s
                            }
                            Text {
                                anchors.centerIn: parent
                                visible: !(c.curvas.puntos && c.curvas.puntos.length)
                                color: Theme.textFaint
                                text: "Sin mediciones en la zona de escucha para este parlante."
                            }
                        }
                        Flow {
                            Layout.fillWidth: true
                            spacing: 14
                            component Ref: Row {
                                property color tinte
                                property string texto
                                property bool guiones: false
                                spacing: 5
                                Rectangle { width: 18; height: guiones ? 1 : 3; color: parent.tinte; anchors.verticalCenter: parent.verticalCenter }
                                Text { text: parent.texto; color: Theme.textDim; font { family: Theme.font; pixelSize: 11 } }
                            }
                            Ref { tinte: "#9ca3af"; texto: "cada punto (1/6 oct)" }
                            Ref { tinte: "#ff6a2b"; texto: "promedio de la zona" }
                            Ref { tinte: "#f4f4f6"; texto: "objetivo"; guiones: true }
                            Ref { tinte: "#38bdf8"; texto: "previsto con el EQ"; visible: verPrevisto.checked }
                            Ref { tinte: "#35c46b"; texto: "verificación"; visible: !!c.curvas.despues }
                            CheckBox { id: verPrevisto; text: "previsto"; checked: true; padding: 0 }
                        }
                    }

                    // --- Impulso y curva energía-tiempo (de la última medición)
                    ColumnLayout {
                        visible: c.vista === "impulso"
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        // La del parlante y el punto elegidos (la más nueva de la etapa); si no hay, la última.
                        readonly property var u: {
                            if (!c.cal) return ({})
                            let id = ""
                            for (let i = 0; i < c.mediciones.length; ++i) {
                                const m = c.mediciones[i]
                                if (m.parlante === c.parlante && m.punto === c.punto && (m.etapa || "antes") === c.etapa) id = m.id
                            }
                            return id !== "" ? c.cal.medicion(id) : c.cal.ultima
                        }
                        Text {
                            Layout.fillWidth: true
                            color: Theme.text
                            font { family: Theme.font; pixelSize: 12 }
                            text: parent.u.id ? "Medición " + parent.u.id + ": " + c.nombreDe(c.parlantes, parent.u.parlante) + " → " + c.nombreDe(c.puntos, parent.u.punto) +
                                  " · directo " + c.fmt(parent.u.directo_ms, 2) + " ms · SNR " + c.fmt(parent.u.snr_db, 0) + " dB · C50 " +
                                  c.fmt(parent.u.c50_db, 1) + " dB · polaridad " + (parent.u.polaridad < 0 ? "INVERTIDA" : "normal")
                                : "Todavía no hay mediciones."
                        }
                        Canvas {
                            id: cvIr
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            property var d: parent.u.ir_vista || []
                            onDChanged: requestPaint()
                            onWidthChanged: requestPaint()
                            onPaint: {
                                const g = getContext("2d"); g.reset()
                                g.fillStyle = Theme.textFaint; g.font = "10px sans-serif"
                                g.fillText("respuesta impulsiva (primeros 100 ms; el primer pico es el directo)", 8, 12)
                                const mid = height / 2
                                g.strokeStyle = Qt.rgba(1, 1, 1, 0.1); g.beginPath(); g.moveTo(0, mid); g.lineTo(width, mid); g.stroke()
                                for (let t = 0; t <= 100; t += 10) { const x = width * t / 100; g.fillText(t + " ms", x + 2, height - 4) }
                                g.strokeStyle = "#ff9a5c"; g.lineWidth = 1; g.beginPath()
                                for (let i = 0; i < d.length; ++i) {
                                    const x = width * i / Math.max(1, d.length - 1), y = mid - d[i] * (mid - 14)
                                    if (i === 0) g.moveTo(x, y); else g.lineTo(x, y)
                                }
                                g.stroke()
                            }
                        }
                        Canvas {
                            id: cvEtc
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            property var d: parent.u.etc || []
                            onDChanged: requestPaint()
                            onWidthChanged: requestPaint()
                            onPaint: {
                                const g = getContext("2d"); g.reset()
                                g.fillStyle = Theme.textFaint; g.font = "10px sans-serif"
                                g.fillText("energía-tiempo (dB, 300 ms): de qué pared vienen las reflexiones, por el retardo", 8, 12)
                                for (let db = 0; db >= -60; db -= 20) {
                                    const y = 20 + (height - 36) * (-db / 60)
                                    g.strokeStyle = Qt.rgba(1, 1, 1, 0.08); g.beginPath(); g.moveTo(0, y); g.lineTo(width, y); g.stroke()
                                    g.fillText(db + " dB", width - 40, y - 3)
                                }
                                for (let t = 0; t <= 300; t += 50) g.fillText(t + " ms", width * t / 300 + 2, height - 2)
                                g.fillStyle = Qt.rgba(1, 0.6, 0.36, 0.7)
                                for (let i = 0; i < d.length; ++i) {
                                    const v = Math.max(-60, d[i]), x = width * i / d.length
                                    const y = 20 + (height - 36) * (-v / 60)
                                    g.fillRect(x, y, Math.max(1, width / d.length), height - 16 - y)
                                }
                            }
                        }
                    }

                    // --- RT60 por banda
                    ColumnLayout {
                        visible: c.vista === "rt"
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        readonly property var bandas: c.cal && c.cal.analisis.rt_bandas ? c.cal.analisis.rt_bandas : []
                        Text {
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                            color: Theme.text
                            font { family: Theme.font; pixelSize: 12 }
                            text: "RT60 medio (500 Hz–1 kHz): " + c.fmt(c.cal ? c.cal.analisis.rt60_medio_s : -1, 2) + " s — palabra: 0,4–0,8 s en salas chicas; más largo se entiende peor (eso es tratamiento acústico, no EQ)."
                        }
                        Canvas {
                            id: cvRt
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            property var b: parent.bandas
                            onBChanged: requestPaint()
                            onWidthChanged: requestPaint()
                            onHeightChanged: requestPaint()
                            onPaint: {
                                const g = getContext("2d"); g.reset()
                                const n = b.length; if (!n) return
                                let max = 1.0
                                for (const x of b) max = Math.max(max, x.t20_s, x.edt_s)
                                max = Math.ceil(max * 2) / 2
                                const izq = 36, aba = height - 22, w = (width - izq - 8) / n
                                g.font = "10px sans-serif"
                                for (let s = 0; s <= max; s += 0.5) {
                                    const y = aba - (aba - 10) * s / max
                                    g.strokeStyle = Qt.rgba(1, 1, 1, 0.08); g.beginPath(); g.moveTo(izq, y); g.lineTo(width, y); g.stroke()
                                    g.fillStyle = Theme.textFaint; g.fillText(s.toFixed(1) + " s", 2, y + 3)
                                }
                                for (let i = 0; i < n; ++i) {
                                    const x0 = izq + i * w
                                    const t = b[i].t20_s, e = b[i].edt_s
                                    if (t > 0) { g.fillStyle = "#ff6a2b"; const h = (aba - 10) * t / max; g.fillRect(x0 + w * 0.15, aba - h, w * 0.32, h) }
                                    if (e > 0) { g.fillStyle = "#38bdf8"; const h = (aba - 10) * e / max; g.fillRect(x0 + w * 0.52, aba - h, w * 0.32, h) }
                                    g.fillStyle = Theme.textDim
                                    g.fillText(b[i].hz >= 1000 ? (b[i].hz / 1000) + "k" : String(b[i].hz), x0 + w * 0.35, height - 6)
                                    if (t <= 0 && e <= 0) { g.fillStyle = Theme.textFaint; g.fillText("—", x0 + w * 0.42, aba - 6) }
                                }
                            }
                        }
                        Nota { Layout.fillWidth: true
                               text: "Naranja: T20 · celeste: EDT (lo que se percibe). «—»: la banda no sobresale del ruido lo bastante (ISO 3382); subí el nivel o alargá el barrido." }
                    }

                    // --- Propuesta
                    ScrollView {
                        id: prop
                        visible: c.vista === "propuesta"
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        contentWidth: availableWidth
                        clip: true
                        readonly property var p: c.cal && c.cal.propuesta.parlantes ? (c.cal.propuesta.parlantes[c.parlante] || ({})) : ({})
                        readonly property var v: c.cal && c.cal.verificacion ? (c.cal.verificacion[c.parlante] || ({})) : ({})
                        ColumnLayout {
                            width: parent.width
                            spacing: 8
                            Text {
                                Layout.fillWidth: true
                                wrapMode: Text.WordWrap
                                visible: !!prop.p.falta
                                color: Theme.amber
                                text: "Falta: " + (prop.p.falta || "")
                            }
                            Titulo { text: "FILTROS PROPUESTOS"; visible: !prop.p.falta }
                            Repeater {
                                model: prop.p.filtros || []
                                delegate: Rectangle {
                                    required property var modelData
                                    Layout.fillWidth: true
                                    implicitHeight: fCol.implicitHeight + 12
                                    radius: 8
                                    color: Qt.rgba(1, 1, 1, 0.04)
                                    ColumnLayout {
                                        id: fCol
                                        anchors { left: parent.left; right: parent.right; verticalCenter: parent.verticalCenter; margins: 10 }
                                        spacing: 2
                                        Text {
                                            text: Number(modelData.f).toFixed(0) + " Hz   " + (modelData.ganancia_db > 0 ? "+" : "") + Number(modelData.ganancia_db).toFixed(1) + " dB   Q " + Number(modelData.q).toFixed(1)
                                            color: modelData.ganancia_db < 0 ? "#38bdf8" : "#f5b83d"
                                            font { family: Theme.mono; pixelSize: 13; weight: Font.DemiBold }
                                        }
                                        Text { Layout.fillWidth: true; wrapMode: Text.WordWrap; text: modelData.motivo; color: Theme.textDim
                                               font { family: Theme.font; pixelSize: 11 } }
                                    }
                                }
                            }
                            Titulo { text: "LO QUE NO SE CORRIGE CON EQ"; visible: (prop.p.no_se_corrige || []).length > 0 }
                            Repeater {
                                model: prop.p.no_se_corrige || []
                                delegate: Text {
                                    required property var modelData
                                    Layout.fillWidth: true
                                    wrapMode: Text.WordWrap
                                    text: "• " + modelData.motivo
                                    color: Theme.textFaint
                                    font { family: Theme.font; pixelSize: 11 }
                                }
                            }
                            Titulo { text: "RETARDO Y NIVEL"; visible: c.parlantes.length > 1 && prop.p.retardo_ms !== undefined }
                            Text {
                                visible: c.parlantes.length > 1 && prop.p.retardo_ms !== undefined
                                Layout.fillWidth: true
                                wrapMode: Text.WordWrap
                                color: Theme.textDim
                                text: "Retardar " + c.fmt(prop.p.retardo_ms, 2) + " ms y " + (prop.p.ganancia_db >= 0 ? "dejar" : "bajar " + Math.abs(prop.p.ganancia_db).toFixed(1) + " dB") +
                                      " para que llegue junto y parejo con los demás." + (prop.p.polaridad ? " Polaridad: " + prop.p.polaridad : "")
                            }
                            Titulo { text: "APLICAR"; visible: (prop.p.filtros || []).length > 0; Layout.topMargin: 6 }
                            RowLayout {
                                visible: (prop.p.filtros || []).length > 0
                                Layout.fillWidth: true
                                Nota { text: "En el rack de" }
                                ComboBox { id: fuente; Layout.fillWidth: true; model: Studio.mezclador ? Studio.mezclador.entradas : []
                                           ToolTip.visible: hovered; ToolTip.text: "La entrada que alimenta este parlante (se le agrega un EQ de LSP «EQ de sala»)" }
                                GlassButton { compact: true; text: "Aplicar"; active: true; onClicked: c.aviso(c.cal.aplicar(c.parlante, fuente.currentText)) }
                                GlassButton { compact: true; text: "Revertir"; onClicked: c.aviso(c.cal.revertir(fuente.currentText)) }
                            }
                            Titulo { text: "VERIFICACIÓN"; Layout.topMargin: 6; visible: !!prop.v.veredicto }
                            Text {
                                visible: !!prop.v.veredicto
                                Layout.fillWidth: true
                                wrapMode: Text.WordWrap
                                color: prop.v.veredicto === "mejoró" ? Theme.ok : prop.v.veredicto && prop.v.veredicto.indexOf("empeoró") === 0 ? Theme.live : Theme.textDim
                                text: (prop.v.desvio_antes_db >= 0 ? "Desvío contra el objetivo: antes " + prop.v.desvio_antes_db + " dB" : "") +
                                      (prop.v.desvio_despues_db >= 0 ? ", después " + prop.v.desvio_despues_db + " dB. " : ". ") +
                                      (prop.v.veredicto || "")
                            }
                        }
                    }
                }
            }
        }

        // --- El asistente -----------------------------------------------------------
        BarraAsistente {
            Layout.fillWidth: true
            contexto: "[Ventana del Mezclador, pestaña Calibración de sala; todo está en calibracion_ver y se maneja con calibracion_*. Es de noche si no te digo lo contrario: preferí la sala simulada.]"
            sugerencias: [
                { t: "Guiame paso a paso", p: "Guiame paso a paso para calibrar esta sala: qué cargar, dónde poner el micrófono y cuándo medir. Esperá mi confirmación antes de cada barrido." },
                { t: "Explicame la sala", p: "Mirá el análisis de la calibración y explicame en criollo qué tiene esta sala: modos, reverberación, qué se nota al hablar." },
                { t: "¿Qué conviene corregir?", p: "Revisá la propuesta de la calibración: ¿los filtros tienen sentido? ¿hay algo que convenga resolver moviendo parlantes en vez de ecualizar?" },
                { t: "Revisá la verificación", p: "Compará antes y después de la calibración. Si algo empeoró, decime qué revertir o ajustar." },
                { t: "Calibrar mi SM58", p: "Quiero usar un SM58 para medir. Explicame cómo calibrarlo por sustitución si consigo un micrófono de medición, y qué límites tiene si no." }
            ]
        }
    }
}
