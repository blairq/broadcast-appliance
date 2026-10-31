// PanelMezclador.qml — el Mezclador, en su propia ventana (docs/planes/PLAN-MEZCLADOR.md, M1)
//
// Lo que maneja: lo que SALE de cada entrada (después de su rack: los racks siguen
// en la página Audio) y ADÓNDE va (la matriz de salida, desplegable abajo).
//   - Entradas: una tira por fuente con audio: vúmetro (pico, RMS y retención por
//     canal, dibujado en la GPU: Vumetro), fader, mute, escucha (PFL), AFV,
//     retardo, y los chips de sus envíos (clic: su fila en la matriz).
//   - Salidas: Programa (con el loudness EBU R128 contra el objetivo), las pistas
//     que reciben algo, los buses virtuales y las salidas de la placa.
//   - Matriz: la misma de la página Audio (MatrizAudio), a un botón.
// Los niveles salen de Studio.mezclador (un temporizador de 30 Hz que corre solo
// mientras esta vista se ve); las acciones son las mismas de la tarjeta.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import BA

Item {
    id: m
    // El tamaño con que abre su ventana (VentanaSuelta).
    readonly property int anchoVentana: 1440
    readonly property int altoVentana: 880
    property bool enVentana: false

    readonly property var mz: Studio.mezclador
    readonly property var entradas: mz ? mz.entradas : []
    readonly property var todasSalidas: mz ? mz.salidas : []
    readonly property var nE: mz ? mz.nivelesEntradas : []
    readonly property var nS: mz ? mz.nivelesSalidas : []
    readonly property var lu: mz ? mz.loudness : ({})
    property var tiras: ({})     // nombre -> {volumenDb, mute, pfl, envios}
    readonly property bool matrizAbierta: mz ? mz.matrizAbierta : false // del modelo (mezclador_pestana)
    property string elegida: ""
    // La pestaña es del modelo (el asistente la cambia: mezclador_pestana).
    readonly property string pestana: mz ? mz.pestana : "consola"
    property bool calibracionAbierta: pestana === "calibracion" // se crea la primera vez y queda
    onPestanaChanged: if (pestana === "calibracion") calibracionAbierta = true

    Component.onCompleted: if (mz) mz.usar(m, visible)
    onVisibleChanged: if (mz) mz.usar(m, visible)
    Component.onDestruction: if (mz) mz.usar(m, false)

    // El estado de las tiras (fader, mute, envíos) cambia poco: se relee seguido sin
    // pedir un aviso por cada cosa.
    Timer { interval: 400; running: m.visible; repeat: true; triggeredOnStart: true; onTriggered: m.leerTiras() }
    function leerTiras() {
        if (!mz) return
        const t = {}
        for (let i = 0; i < entradas.length; ++i) t[entradas[i]] = mz.tira(entradas[i])
        tiras = t
        // Los grupos (los mismos de la tarjeta, del modelo del mezclador): cuáles están
        // compactados y su volumen general (VCA).
        const gs = Studio.mixer ? Studio.mixer.grupos() : []
        const porClave = {}, deTira = {}
        for (let i = 0; i < gs.length; ++i) {
            porClave[gs[i].grupo] = gs[i]
            for (let k = 0; k < gs[i].tiras.length; ++k) deTira[gs[i].tiras[k]] = gs[i].grupo
        }
        grupos = porClave
        // La estructura (qué grupos, qué tiras, cuáles compactados) cambia poco: la vista se
        // rearma solo si cambió. Si se rearmara en cada lectura, titilaría y cortaría el
        // arrastre de un fader (el VCA cambia mientras se lo mueve: va por `grupos`).
        const est = JSON.stringify(gs.map((x) => [x.grupo, x.tiras, x.compacto]))
        if (est !== estructura) { estructura = est; grupoDe = deTira }
    }
    property var grupos: ({})  // clave -> {grupo, nombre, tiras, compacto, volumenDb}
    property var grupoDe: ({}) // tira -> clave de su grupo
    property string estructura: ""
    // Lo que muestra la consola, en orden: cada tira; al empezar un grupo desplegado, su VCA;
    // un grupo compactado, un solo bloque (sin faders, una casilla por tira).
    readonly property var vista: {
        const out = [], vistos = {}
        for (let i = 0; i < entradas.length; ++i) {
            const n = entradas[i], g = grupoDe[n]
            const compacto = g !== undefined && estructura.length > 0 && m.compactoEnEstructura(g)
            if (g !== undefined && !vistos[g]) {
                vistos[g] = true
                out.push({ tipo: compacto ? "bloque" : "vca", grupo: g })
            }
            if (!compacto) out.push({ tipo: "tira", nombre: n })
        }
        return out
    }
    // De la estructura (no de `grupos`, que cambia con el VCA): ¿ese grupo está compactado?
    function compactoEnEstructura(g) {
        const est = JSON.parse(estructura)
        for (let i = 0; i < est.length; ++i) if (est[i][0] === g) return est[i][2]
        return false
    }
    function indiceEnVista(nombre) {
        for (let i = 0; i < vista.length; ++i) if (vista[i].tipo === "tira" && vista[i].nombre === nombre) return i
        return -1
    }
    function nv(arr, i, k) { const v = arr[i * 6 + k]; return v === undefined ? -100 : v }
    // Las salidas que se muestran: Programa siempre; las pistas 2–6 solo si alguien les manda.
    readonly property var salidas: todasSalidas.filter((s) => {
        if (s.tipo !== "pista" || s.id === "pista1") return true
        for (const n in tiras) if ((tiras[n].envios || []).indexOf(s.id) >= 0) return true
        return false
    })
    function indiceSalida(id) { for (let i = 0; i < todasSalidas.length; ++i) if (todasSalidas[i].id === id) return i; return -1 }
    function colorDestino(s) {
        if (!s) return Theme.accent
        if (s.tipo === "pista") return s.id === "pista1" ? "#ef4444" : "#f59e0b"
        if (s.tipo === "salida") return "#a78bfa"
        if (s.tipo === "escucha") return "#22c55e"
        if (s.tipo === "fisica") return "#38bdf8"
        return "#14b8a6"
    }
    function salidaDe(id) { for (let i = 0; i < todasSalidas.length; ++i) if (todasSalidas[i].id === id) return todasSalidas[i]; return null }
    function abreviar(s) {
        if (!s) return "?"
        if (s.tipo === "pista") return s.id === "pista1" ? "PGM" : "P" + s.id.substring(5)
        if (s.tipo === "escucha") return qsTr("ESC")
        const w = (s.nombre || "").split(/\s+/)
        return w.length > 1 ? (w[0][0] + w[1][0]).toUpperCase() : (s.nombre || "?").substring(0, 3).toUpperCase()
    }
    function texto(db) { return db <= -99 ? "−∞" : (db > 0 ? "+" : "") + db.toFixed(1) }
    function abrirLsp(entrada, cual) {
        if (cual === "perfil") { confirmarPerfil.entrada = entrada; confirmarPerfil.open(); return }
        const e = mz.abrirAnalisisLsp(entrada, cual)
        if (e !== "") Studio.notify(e, "error")
    }
    function mostrarEnMatriz(fila, bus) {
        m.mz.matrizAbierta = true
        matriz.hFila = fila || ""
        matriz.hBus = bus || ""
    }

    // Sincronizar un canal con su video (el mismo de la tarjeta y de «Sincronía y red»).
    RetardoCanal { id: retardoPop; onAplicado: m.leerTiras() }

    // El Profiler emite un barrido por la salida que se elija en su interfaz: suena.
    // Un diálogo propio (en una ventana suelta no está el de la principal).
    Dialog {
        id: confirmarPerfil
        property string entrada: ""
        anchors.centerIn: parent
        modal: true
        width: 460   // ancho fijo: con el implícito del Label, el diálogo entraba en un ciclo
        title: qsTr("El Profiler de LSP hace sonar un barrido")
        Label {
            width: confirmarPerfil.availableWidth
            wrapMode: Text.WordWrap
            text: qsTr("Abre el Profiler sobre «%1», escuchando al costado: lo que él emite no va al aire. Para medir, en el rack de la entrada conectá su salida a una tarjeta Salida (el parlante); el barrido de 20 Hz a 20 kHz puede sonar fuerte. Nada sale hasta que aprietes «Medir» en su interfaz.").arg(confirmarPerfil.entrada)
        }
        standardButtons: Dialog.Ok | Dialog.Cancel
        onAccepted: { const e = m.mz.abrirAnalisisLsp(entrada, "perfil"); if (e !== "") Studio.notify(e, "error") }
    }

    // --- Una tira de entrada ---------------------------------------------------------
    component TiraEntrada: Rectangle {
        id: t
        property string nombre
        readonly property int index: m.entradas.indexOf(nombre) // su lugar en los niveles (nE)
        readonly property var st: m.tiras[nombre] || ({})
        readonly property real ret: Math.max(m.nv(m.nE, index, 4), m.nv(m.nE, index, 5))
        readonly property bool elegida: m.elegida === nombre
        width: 92
        height: parent ? parent.height : 400
        radius: 12
        color: elegida ? Qt.rgba(1, 1, 1, 0.09) : Qt.rgba(1, 1, 1, 0.035)
        border.color: elegida ? Theme.accent : Qt.rgba(1, 1, 1, 0.08)

        ColumnLayout {
            anchors { fill: parent; margins: 6 }
            spacing: 5
            // El nombre (clic: el análisis mira esta entrada).
            Text {
                Layout.fillWidth: true
                text: t.nombre
                color: t.st.mute ? Theme.textFaint : Theme.text
                font { family: Theme.font; pixelSize: 11; weight: Font.DemiBold }
                elide: Text.ElideRight
                horizontalAlignment: Text.AlignHCenter
                HoverHandler { id: nomHov }
                ToolTip.visible: nomHov.hovered; ToolTip.delay: 500; ToolTip.text: t.nombre
                TapHandler { onTapped: { m.elegida = t.elegida ? "" : t.nombre; lista.forceActiveFocus() } }
            }
            // La retención más alta (rojo si toca 0 dBFS).
            Text {
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignHCenter
                text: m.texto(t.ret)
                color: t.ret > -1 ? Theme.live : t.ret > -6 ? Theme.amber : Theme.textDim
                font { family: Theme.font; pixelSize: 10 }
            }
            // Vúmetro + escala + fader.
            Item {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Vumetro {
                    id: vu
                    anchors { left: parent.left; top: parent.top; bottom: parent.bottom }
                    width: 22
                    picoL: m.nv(m.nE, t.index, 0); picoR: m.nv(m.nE, t.index, 1)
                    rmsL: m.nv(m.nE, t.index, 2); rmsR: m.nv(m.nE, t.index, 3)
                    retencionL: m.nv(m.nE, t.index, 4); retencionR: m.nv(m.nE, t.index, 5)
                    apagado: !!t.st.mute
                }
                // La escala (cada 12 dB y el 0).
                Repeater {
                    model: [0, -6, -12, -24, -36, -48]
                    delegate: Text {
                        required property var modelData
                        x: vu.width + 2
                        y: vu.y + (1 - vu.fraccion(modelData)) * vu.height - height / 2
                        text: modelData === 0 ? "0" : String(modelData)
                        color: Theme.textFaint
                        font { family: Theme.font; pixelSize: 8 }
                    }
                }
                Fader {
                    id: fader
                    anchors { right: parent.right; top: parent.top; bottom: parent.bottom }
                    width: 30
                    db: t.st.volumenDb !== undefined ? t.st.volumenDb : 0
                    alMover: (v) => Studio.mixer.setVolumeDb(t.nombre, v)
                    alCero: () => { Studio.mixer.setVolumeDb(t.nombre, 0); m.leerTiras() }
                }
            }
            // El nivel del fader.
            Text {
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignHCenter
                text: (t.st.volumenDb === undefined || t.st.volumenDb <= -60) ? "−∞" : t.st.volumenDb.toFixed(1)
                color: Theme.textDim
                font { family: Theme.font; pixelSize: 10 }
            }
            // Mute, escucha, AFV.
            RowLayout {
                Layout.fillWidth: true
                spacing: 3
                Boton { texto: "M"; on: !!t.st.mute; colorOn: Theme.canalMute; ayuda: qsTr("Mute")
                        alTocar: () => { Studio.mixer.setMuted(t.nombre, !t.st.mute); m.leerTiras() } }
                Boton { texto: "PFL"; on: !!t.st.pfl; colorOn: Theme.canalPfl; textoOn: "#1a1a1a"; ayuda: qsTr("Escucharla en los auriculares (se suma a lo que ya va ahí, como la voz del asistente)")
                        alTocar: () => { const e = Studio.mezcla.pflUi(t.nombre); if (e !== "") Studio.notify(e, "error"); m.leerTiras() } }
                Boton { texto: "AFV"; on: Studio.afvFuentes.indexOf(t.nombre) >= 0; colorOn: Theme.canalAfv; ayuda: qsTr("Sigue al video: sube cuando su escena sale al aire")
                        alTocar: () => Studio.setAfv(t.nombre, Studio.afvFuentes.indexOf(t.nombre) < 0) }
            }
            // Sincronizar con el video (sincro labios): el retardo del canal.
            Boton {
                Layout.fillWidth: true
                readonly property int ms: t.st.retardoMs || 0
                texto: ms !== 0 ? qsTr("SINC %1 ms").arg((ms > 0 ? "+" : "") + ms) : qsTr("SINC")
                on: ms !== 0; colorOn: Theme.canalSinc
                ayuda: qsTr("Sincronizar con el video: atrasar este audio hasta que coincida con los labios (a mano o midiendo con aplausos)")
                alTocar: () => retardoPop.abrir(t.nombre)
            }
            // Los analizadores de LSP sobre esta entrada (su interfaz, en una ventana aparte).
            Boton {
                id: lspBtn
                Layout.fillWidth: true
                texto: "LSP ▾"
                ayuda: qsTr("Mirarla con los analizadores de LSP: espectro, fase, osciloscopio, latencia, perfil")
                alTocar: () => lspMenu.popup(lspBtn, 0, lspBtn.height)
                Menu {
                    id: lspMenu
                    ItemsDeMenu {
                        menu: lspMenu
                        model: m.mz ? m.mz.analisisLsp : []
                        delegate: MenuItem {
                            required property var modelData
                            text: modelData.nombre + " — " + modelData.para
                            onTriggered: m.abrirLsp(t.nombre, modelData.id)
                        }
                    }
                }
            }
            // Los envíos: a qué destinos va (clic: su fila en la matriz).
            Flow {
                Layout.fillWidth: true
                Layout.preferredHeight: 35
                clip: true
                spacing: 3
                Repeater {
                    model: t.st.envios || []
                    delegate: Rectangle {
                        required property string modelData
                        readonly property var s: m.salidaDe(modelData)
                        width: chipTxt.implicitWidth + 8; height: 16; radius: 8
                        color: Qt.alpha(m.colorDestino(s), 0.25)
                        border.color: m.colorDestino(s)
                        Text { id: chipTxt; anchors.centerIn: parent; text: m.abreviar(parent.s); color: Theme.text
                               font { family: Theme.font; pixelSize: 9; weight: Font.DemiBold } }
                        HoverHandler { id: chipHov }
                        ToolTip.visible: chipHov.hovered; ToolTip.delay: 300
                        ToolTip.text: qsTr("Va a %1 (clic: verlo en la matriz)").arg(s ? s.nombre : modelData)
                        TapHandler { onTapped: m.mostrarEnMatriz(t.nombre, "") }
                    }
                }
                Text { visible: (t.st.envios || []).length === 0; text: qsTr("sin salida"); color: Theme.amber
                       font { family: Theme.font; pixelSize: 9 } }
            }
        }
    }

    // El fader de una consola: ranura oscura, marca de 0 dB y perilla rectangular.
    // -60 dB (−∞) abajo, +12 dB arriba; la rueda ajusta de a 1 dB (Shift: 0,5).
    component Fader: Item {
        id: fd
        property real db: 0
        property var alMover: null
        property var alCero: null
        readonly property real desde: -60
        readonly property real hasta: 12
        property bool arrastrando: false
        property real dbArrastre: 0
        readonly property real mostrado: arrastrando ? dbArrastre : Math.max(desde, db)
        function aY(v) { return (1 - (v - desde) / (hasta - desde)) * (height - perilla.height) }
        function deY(y) { return Math.max(desde, Math.min(hasta, hasta - (y / (height - perilla.height)) * (hasta - desde))) }
        Rectangle { // la ranura
            anchors { horizontalCenter: parent.horizontalCenter; top: parent.top; bottom: parent.bottom
                      topMargin: perilla.height / 2; bottomMargin: perilla.height / 2 }
            width: 4; radius: 2
            color: Qt.rgba(0, 0, 0, 0.6)
            border.color: Qt.rgba(1, 1, 1, 0.08)
        }
        Rectangle { // la marca de 0 dB
            x: 2; width: parent.width - 4; height: 1
            y: fd.aY(0) + perilla.height / 2
            color: Qt.rgba(1, 1, 1, 0.35)
        }
        Rectangle {
            id: perilla
            width: parent.width; height: 16; radius: 3
            y: fd.aY(fd.mostrado)
            gradient: Gradient {
                GradientStop { position: 0.0; color: "#d4d4d8" }
                GradientStop { position: 0.45; color: "#a1a1aa" }
                GradientStop { position: 0.5; color: "#f4f4f5" }
                GradientStop { position: 1.0; color: "#71717a" }
            }
            border.color: Qt.rgba(0, 0, 0, 0.5)
        }
        MouseArea {
            anchors.fill: parent
            cursorShape: Qt.SizeVerCursor
            hoverEnabled: true
            preventStealing: true
            onPressed: (e) => { fd.arrastrando = true; fd.dbArrastre = fd.deY(e.y - perilla.height / 2) }
            onPositionChanged: (e) => {
                if (!fd.arrastrando) return
                fd.dbArrastre = Math.round(fd.deY(e.y - perilla.height / 2) * 2) / 2
                if (fd.alMover) fd.alMover(fd.dbArrastre)
            }
            onReleased: fd.arrastrando = false
            onDoubleClicked: if (fd.alCero) fd.alCero()
            onWheel: (w) => {
                // De costado (rueda inclinada, trackpad, Shift+rueda en macOS): desplaza la
                // consola, no el fader.
                if (Math.abs(w.angleDelta.x) > Math.abs(w.angleDelta.y)) { w.accepted = false; return }
                const paso = (w.modifiers & Qt.ShiftModifier) ? 0.5 : 1
                const v = Math.max(fd.desde, Math.min(fd.hasta, Math.max(fd.desde, fd.db) + (w.angleDelta.y > 0 ? paso : -paso)))
                if (fd.alMover) fd.alMover(v)
            }
            ToolTip.visible: containsMouse || fd.arrastrando; ToolTip.delay: 300
            ToolTip.text: qsTr("%1 dB (doble clic: 0 dB)").arg(fd.mostrado <= fd.desde ? "−∞" : fd.mostrado.toFixed(1))
        }
    }

    // Un botón chico de la tira (M, PFL, AFV).
    component Boton: Rectangle {
        id: b
        property string texto
        property bool on: false
        property color colorOn: Theme.accent
        property color textoOn: "white" // oscuro sobre colores claros (el amarillo del PFL)
        property string ayuda
        property var alTocar: null
        Layout.fillWidth: true
        height: 20; radius: 5
        color: on ? colorOn : Qt.rgba(1, 1, 1, 0.07)
        border.color: on ? Qt.lighter(colorOn, 1.3) : Qt.rgba(1, 1, 1, 0.12)
        Text { anchors.centerIn: parent; text: b.texto; color: b.on ? b.textoOn : Theme.textDim
               font { family: Theme.font; pixelSize: 9; weight: Font.Bold } }
        HoverHandler { id: bHov; cursorShape: Qt.PointingHandCursor }
        ToolTip.visible: bHov.hovered && b.ayuda !== ""; ToolTip.delay: 500; ToolTip.text: b.ayuda
        TapHandler { onTapped: if (b.alTocar) b.alTocar() }
    }

    // --- El volumen general de un grupo (VCA), al empezar el grupo desplegado ---------
    // Mueve a todos juntos sin tocar el fader de cada uno (las diferencias quedan); el «−»
    // lo compacta. Doble clic en «VCA» (o en el fader): a 0 dB.
    component TiraVca: Rectangle {
        id: v
        property string clave
        readonly property var g: m.grupos[clave] || ({})
        readonly property real db: g.volumenDb !== undefined ? Math.max(-60, g.volumenDb) : 0
        width: 64
        height: parent ? parent.height : 400
        radius: 12
        color: Qt.rgba(1, 0.42, 0.17, 0.06)
        border.color: Qt.rgba(1, 0.42, 0.17, 0.35)
        ColumnLayout {
            anchors { fill: parent; margins: 6 }
            spacing: 5
            RowLayout {
                Layout.fillWidth: true
                spacing: 2
                Text {
                    Layout.fillWidth: true
                    text: "VCA"; color: Theme.accentSoft
                    font { family: Theme.font; pixelSize: 10; weight: Font.Bold; letterSpacing: 1 }
                    TapHandler { onDoubleTapped: { Studio.mixer.setGananciaGrupoDb(v.clave, 0); m.leerTiras() } }
                    HoverHandler { cursorShape: Qt.PointingHandCursor }
                }
                Boton { texto: "−"; Layout.fillWidth: false; Layout.preferredWidth: 20
                        ayuda: qsTr("Compactar %1: sin faders, una casilla por tira").arg(v.g.nombre || "")
                        alTocar: () => { Studio.mixer.setCompacto(v.clave, true); m.leerTiras() } }
            }
            Item {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Fader {
                    anchors { horizontalCenter: parent.horizontalCenter; top: parent.top; bottom: parent.bottom }
                    width: 30
                    db: v.db
                    alMover: (x) => Studio.mixer.setGananciaGrupoDb(v.clave, x)
                    alCero: () => { Studio.mixer.setGananciaGrupoDb(v.clave, 0); m.leerTiras() }
                }
            }
            Text {
                Layout.fillWidth: true; horizontalAlignment: Text.AlignHCenter
                text: m.texto(v.db)
                color: Math.abs(v.db) > 0.05 ? Theme.accent : Theme.textDim
                font { family: Theme.font; pixelSize: 10 }
                TapHandler { onDoubleTapped: { Studio.mixer.setGananciaGrupoDb(v.clave, 0); m.leerTiras() } }
            }
            Text {
                Layout.fillWidth: true; horizontalAlignment: Text.AlignHCenter
                text: v.g.nombre || ""; color: Theme.accentSoft; elide: Text.ElideRight
                font { family: Theme.font; pixelSize: 10; weight: Font.DemiBold }
            }
        }
    }

    // --- Un grupo compactado: un bloque en vez de sus tiras ---------------------------
    // «M todos», su VCA acostado y una casilla por tira (su número, verde cuando habla, roja
    // muda; un clic la silencia). Las casillas llenan la altura y suman columnas.
    component BloqueGrupo: Rectangle {
        id: bq
        property string clave
        readonly property var g: m.grupos[clave] || ({})
        readonly property var miembros: g.tiras || []
        readonly property real db: g.volumenDb !== undefined ? Math.max(-60, g.volumenDb) : 0
        readonly property int filas: Math.max(1, Math.floor(grilla.height / 26))
        width: Math.max(110, 12 + 24 + Math.ceil(miembros.length / filas) * 38) // márgenes + el VCA parado + las casillas
        height: parent ? parent.height : 400
        radius: 12
        color: Qt.rgba(1, 1, 1, 0.035)
        border.color: Qt.rgba(1, 0.42, 0.17, 0.35)
        ColumnLayout {
            anchors { fill: parent; margins: 6 }
            spacing: 5
            RowLayout {
                Layout.fillWidth: true
                spacing: 4
                Boton { texto: "+"; Layout.fillWidth: false; Layout.preferredWidth: 20
                        ayuda: qsTr("Mostrar los faders de %1 (%n tira)", "", bq.miembros.length).arg(bq.g.nombre || "")
                        alTocar: () => { Studio.mixer.setCompacto(bq.clave, false); m.leerTiras() } }
                Text {
                    Layout.fillWidth: true
                    text: bq.g.nombre || ""; color: Theme.text; elide: Text.ElideRight
                    font { family: Theme.font; pixelSize: 11; weight: Font.DemiBold }
                }
            }
            Boton {
                Layout.fillWidth: true
                texto: qsTr("M todos"); ayuda: qsTr("Silenciar todo el grupo (si ya están todas mudas, las vuelve a abrir)")
                alTocar: () => {
                    const alguna = bq.miembros.some((n) => !(m.tiras[n] || {}).mute)
                    Studio.mixer.setMutedGrupo(bq.clave, alguna)
                    m.leerTiras()
                }
            }
            // El VCA parado al costado de las casillas (alto hay de sobra), y las casillas.
            RowLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 6
                // Fino (del ancho de la perilla): que no corra las casillas.
                ColumnLayout {
                    Layout.fillHeight: true
                    Layout.preferredWidth: 18
                    Layout.maximumWidth: 18
                    spacing: 4
                    Text {
                        Layout.alignment: Qt.AlignHCenter
                        text: "VCA"; color: Theme.accentSoft; font { family: Theme.font; pixelSize: 8; weight: Font.Bold }
                        TapHandler { onDoubleTapped: { Studio.mixer.setGananciaGrupoDb(bq.clave, 0); m.leerTiras() } }
                        HoverHandler { cursorShape: Qt.PointingHandCursor }
                    }
                    Item {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        Fader {
                            anchors { horizontalCenter: parent.horizontalCenter; top: parent.top; bottom: parent.bottom }
                            width: 14
                            db: bq.db
                            alMover: (x) => Studio.mixer.setGananciaGrupoDb(bq.clave, x)
                            alCero: () => { Studio.mixer.setGananciaGrupoDb(bq.clave, 0); m.leerTiras() }
                        }
                    }
                    Text {
                        Layout.alignment: Qt.AlignHCenter
                        text: m.texto(bq.db); color: Math.abs(bq.db) > 0.05 ? Theme.accent : Theme.textFaint
                        font { family: Theme.font; pixelSize: 8 }
                        TapHandler { onDoubleTapped: { Studio.mixer.setGananciaGrupoDb(bq.clave, 0); m.leerTiras() } }
                    }
                }
            Item {
                id: grilla
                Layout.fillHeight: true
                Layout.preferredWidth: Math.ceil(bq.miembros.length / bq.filas) * 38
                Grid {
                    flow: Grid.TopToBottom
                    rows: bq.filas
                    spacing: 4
                    Repeater {
                        model: bq.miembros
                        delegate: Rectangle {
                            id: cas
                            required property string modelData
                            required property int index
                            readonly property bool muda: !!(m.tiras[modelData] || {}).mute
                            readonly property real pico: m.nv(m.nE, m.entradas.indexOf(modelData), 0)
                            width: 34; height: 22; radius: 5
                            color: muda ? Theme.canalMute : Qt.rgba(0.24, 0.86, 0.59, Math.max(0.08, Math.min(0.85, (pico + 50) / 50)))
                            Text {
                                anchors.centerIn: parent
                                // Su número; si el nombre no lo tiene, su lugar en el grupo.
                                text: (cas.modelData.match(/\d+/) || [String(cas.index + 1)])[0]
                                color: Theme.text; font { family: Theme.font; pixelSize: 10; weight: Font.DemiBold }
                            }
                            HoverHandler { id: casHov; cursorShape: Qt.PointingHandCursor }
                            ToolTip.visible: casHov.hovered; ToolTip.delay: 400
                            ToolTip.text: (cas.muda ? qsTr("%1 · muda (un clic la silencia)") : qsTr("%1 (un clic la silencia)")).arg(cas.modelData)
                            TapHandler { onTapped: { Studio.mixer.setMuted(cas.modelData, !cas.muda); m.leerTiras() } }
                        }
                    }
                }
            }
            }
        }
    }

    // Una salida (bus, pista, placa).
    component TiraSalida: Rectangle {
        id: so
        required property var modelData
        readonly property int i: m.indiceSalida(modelData.id)
        readonly property real ret: Math.max(m.nv(m.nS, i, 4), m.nv(m.nS, i, 5))
        width: 62
        radius: 12
        color: Qt.rgba(1, 1, 1, 0.035)
        border.color: Qt.alpha(m.colorDestino(modelData), 0.6)
        ColumnLayout {
            anchors { fill: parent; margins: 6 }
            spacing: 5
            Text {
                Layout.fillWidth: true
                text: so.modelData.nombre
                color: m.colorDestino(so.modelData)
                font { family: Theme.font; pixelSize: 11; weight: Font.DemiBold }
                elide: Text.ElideRight
                horizontalAlignment: Text.AlignHCenter
                HoverHandler { id: soHov }
                ToolTip.visible: soHov.hovered; ToolTip.delay: 500; ToolTip.text: qsTr("%1 (clic: quién le manda, en la matriz)").arg(so.modelData.nombre)
                TapHandler { onTapped: m.mostrarEnMatriz("", so.modelData.id) }
            }
            Text {
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignHCenter
                text: m.texto(so.ret)
                color: so.ret > -1 ? Theme.live : so.ret > -6 ? Theme.amber : Theme.textDim
                font { family: Theme.font; pixelSize: 10 }
            }
            Vumetro {
                Layout.alignment: Qt.AlignHCenter
                Layout.fillHeight: true
                implicitWidth: 26
                picoL: m.nv(m.nS, so.i, 0); picoR: m.nv(m.nS, so.i, 1)
                rmsL: m.nv(m.nS, so.i, 2); rmsR: m.nv(m.nS, so.i, 3)
                retencionL: m.nv(m.nS, so.i, 4); retencionR: m.nv(m.nS, so.i, 5)
            }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 10

        // --- La barra de arriba --------------------------------------------------------
        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            GlassButton { compact: true; text: qsTr("Consola"); active: m.pestana === "consola"; onClicked: m.mz.pestana = "consola" }
            GlassButton {
                compact: true; text: qsTr("Calibración de sala"); active: m.pestana === "calibracion"; onClicked: m.mz.pestana = "calibracion"
                ToolTip.visible: hovered; ToolTip.delay: 400
                ToolTip.text: qsTr("Medir la sala con barridos (varios puntos, micrófonos sin calibrar incluidos), ver modos y reverberación, y ecualizar.")
            }
            Rectangle { width: 1; height: 22; color: Qt.rgba(1, 1, 1, 0.12); visible: m.pestana === "consola" }
            Text {
                visible: m.pestana === "consola"
                text: qsTr("%n entrada", "", m.entradas.length) + " · " + qsTr("%n salida", "", m.salidas.length)
                color: Theme.textFaint
                font { family: Theme.font; pixelSize: 12 }
            }
            Item { Layout.fillWidth: true }
            // Los analizadores de LSP sobre la entrada elegida (clic en el nombre de una tira).
            Text {
                visible: m.pestana === "consola"
                text: m.elegida === "" ? qsTr("Análisis LSP: elegí una entrada") : qsTr("Analizar «%1»:").arg(m.elegida)
                color: Theme.textFaint
                font { family: Theme.font; pixelSize: 11 }
                elide: Text.ElideMiddle
                Layout.maximumWidth: 280
            }
            Repeater {
                model: m.pestana === "consola" && m.mz ? m.mz.analisisLsp : []
                delegate: GlassButton {
                    required property var modelData
                    compact: true
                    text: modelData.nombre
                    enabled: m.elegida !== ""
                    onClicked: m.abrirLsp(m.elegida, modelData.id)
                    ToolTip.visible: hovered; ToolTip.delay: 400; ToolTip.text: modelData.para
                }
            }
            GlassButton {
                visible: m.pestana === "consola"
                compact: true
                text: m.matrizAbierta ? qsTr("Matriz ▴") : qsTr("Matriz ▾")
                active: m.matrizAbierta
                onClicked: { m.mz.matrizAbierta = !m.matrizAbierta; if (!m.mz.matrizAbierta) { matriz.hFila = ""; matriz.hBus = "" } }
                ToolTip.visible: hovered; ToolTip.delay: 400
                ToolTip.text: qsTr("La matriz de salida: qué entrada va a qué pista, bus o salida de la placa.")
            }
        }

        // --- De qué salida es la mezcla (con varias: cada una tiene la suya) --------------
        // Los faders, el mute y el AFV de las tiras mueven la mezcla de la elegida; el
        // rack, la matriz y las salidas de la placa son de las entradas: los mismos.
        SelectorSalida { Layout.fillWidth: true; visible: Studio.salidas.varias && m.pestana === "consola" }

        // --- Calibración de sala (se crea la primera vez que se abre) ------------------
        Loader {
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: m.pestana === "calibracion"
            active: m.calibracionAbierta
            sourceComponent: Component { PanelCalibracion {} }
        }

        // --- Entradas y salidas -------------------------------------------------------
        RowLayout {
            visible: m.pestana === "consola"
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 10
            ListView {
                id: lista
                Layout.fillWidth: true
                Layout.fillHeight: true
                orientation: ListView.Horizontal
                spacing: 6
                clip: true
                model: m.vista
                delegate: Loader {
                    required property var modelData
                    height: ListView.view ? ListView.view.height : 400
                    sourceComponent: modelData.tipo === "tira" ? tiraComp : modelData.tipo === "vca" ? vcaComp : bloqueComp
                    onLoaded: {
                        if (modelData.tipo === "tira") item.nombre = Qt.binding(() => modelData.nombre)
                        else item.clave = Qt.binding(() => modelData.grupo)
                    }
                }
                Component { id: tiraComp; TiraEntrada {} }
                Component { id: vcaComp; TiraVca {} }
                Component { id: bloqueComp; BloqueGrupo {} }
                ScrollBar.horizontal: ScrollBar { policy: ScrollBar.AsNeeded }
                // CON EL TECLADO (un clic en una tira o Tab le da el foco): ← → o Tab elige
                // la tira y la trae a la vista; ↑ ↓ el fader de a 1 dB (Shift: 3); M mute;
                // Inicio y Fin. Como la tarjeta del Mezclador.
                activeFocusOnTab: true
                keyNavigationEnabled: false // las flechas las maneja esto (eligen tira, no mueven el índice)
                function elegirPaso(paso) {
                    const n = m.entradas.length
                    if (n === 0) return
                    const i = m.entradas.indexOf(m.elegida)
                    const j = i < 0 ? (paso > 0 ? 0 : n - 1) : Math.max(0, Math.min(n - 1, i + paso))
                    m.elegida = m.entradas[j]
                    const v = m.indiceEnVista(m.elegida)
                    if (v >= 0) lista.positionViewAtIndex(v, ListView.Contain)
                }
                function ajustar(db) {
                    if (m.elegida === "") return
                    const t = m.tiras[m.elegida] || {}
                    const v = t.volumenDb !== undefined ? t.volumenDb : 0
                    Studio.mixer.setVolumeDb(m.elegida, Math.max(-60, Math.min(12, v + db)))
                    m.leerTiras()
                }
                Keys.onPressed: (e) => {
                    const grande = (e.modifiers & Qt.ShiftModifier) !== 0
                    switch (e.key) {
                        case Qt.Key_Left: lista.elegirPaso(-1); e.accepted = true; break
                        case Qt.Key_Right: lista.elegirPaso(1); e.accepted = true; break
                        case Qt.Key_Tab: lista.elegirPaso(1); e.accepted = true; break
                        case Qt.Key_Backtab: lista.elegirPaso(-1); e.accepted = true; break
                        case Qt.Key_Home: lista.elegirPaso(-m.entradas.length); e.accepted = true; break
                        case Qt.Key_End: lista.elegirPaso(m.entradas.length); e.accepted = true; break
                        case Qt.Key_Up: lista.ajustar(grande ? 3 : 1); e.accepted = true; break
                        case Qt.Key_Down: lista.ajustar(grande ? -3 : -1); e.accepted = true; break
                        case Qt.Key_M:
                            if (m.elegida !== "") { Studio.mixer.setMuted(m.elegida, !(m.tiras[m.elegida] || {}).mute); m.leerTiras() }
                            e.accepted = true; break
                    }
                }
                // Un clic en cualquier lado de la consola le da el foco del teclado (sin
                // quitarle el clic al fader o a los botones).
                TapHandler { gesturePolicy: TapHandler.DragThreshold; grabPermissions: PointerHandler.ApproveAnyGrab
                             onTapped: lista.forceActiveFocus() }
                // La rueda común (vertical) fuera de un fader desplaza los canales de costado; los
                // faders están encima y la toman antes (su volumen); de costado la maneja la lista.
                WheelHandler {
                    orientation: Qt.Vertical
                    acceptedDevices: PointerDevice.Mouse
                    onWheel: (ev) => {
                        const max = Math.max(lista.originX, lista.originX + lista.contentWidth - lista.width)
                        lista.contentX = Math.max(lista.originX, Math.min(max, lista.contentX - ev.angleDelta.y / 120 * 60))
                    }
                }
                // Arrastrar con el botón del medio (la rueda apretada) desplaza la consola.
                MouseArea {
                    anchors.fill: parent
                    z: 100
                    acceptedButtons: Qt.MiddleButton
                    cursorShape: pressed ? Qt.ClosedHandCursor : Qt.ArrowCursor
                    property real x0: 0
                    property real cx0: 0
                    onPressed: (e) => { x0 = e.x; cx0 = lista.contentX; lista.forceActiveFocus() }
                    onPositionChanged: (e) => {
                        const max = Math.max(lista.originX, lista.originX + lista.contentWidth - lista.width)
                        lista.contentX = Math.max(lista.originX, Math.min(max, cx0 - (e.x - x0)))
                    }
                }
                Text {
                    anchors.centerIn: parent
                    visible: m.entradas.length === 0
                    text: m.mz ? qsTr("No hay entradas con audio.") : qsTr("El audio no está disponible en esta compilación.")
                    color: Theme.textFaint
                }
            }
            Rectangle { Layout.fillHeight: true; width: 1; color: Qt.rgba(1, 1, 1, 0.1) }

            // Programa: la salida que importa, ancha y con su loudness.
            Rectangle {
                id: pgm
                readonly property int i: m.indiceSalida("pista1")
                readonly property var obj: m.lu.objetivo || ({})
                readonly property real objetivo: obj.lufs !== undefined ? obj.lufs : -23
                Layout.fillHeight: true
                Layout.preferredWidth: 196
                radius: 12
                color: Qt.rgba(1, 1, 1, 0.045)
                border.color: "#ef4444"
                ColumnLayout {
                    anchors { fill: parent; margins: 8 }
                    spacing: 6
                    Text { text: qsTr("PROGRAMA"); color: "#ef4444"; Layout.alignment: Qt.AlignHCenter
                           font { family: Theme.font; pixelSize: 12; weight: Font.Bold; letterSpacing: 1 } }
                    RowLayout {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        spacing: 8
                        Vumetro {
                            Layout.fillHeight: true
                            implicitWidth: 52
                            picoL: m.nv(m.nS, pgm.i, 0); picoR: m.nv(m.nS, pgm.i, 1)
                            rmsL: m.nv(m.nS, pgm.i, 2); rmsR: m.nv(m.nS, pgm.i, 3)
                            retencionL: m.nv(m.nS, pgm.i, 4); retencionR: m.nv(m.nS, pgm.i, 5)
                        }
                        // El loudness (EBU R128) contra el objetivo.
                        ColumnLayout {
                            Layout.fillWidth: true
                            Layout.alignment: Qt.AlignTop
                            spacing: 4
                            component Dato: Column {
                                property string etiqueta
                                property string valor
                                property color tinte: Theme.text
                                Text { text: parent.etiqueta; color: Theme.textFaint; font { family: Theme.font; pixelSize: 9 } }
                                Text { text: parent.valor; color: parent.tinte; font { family: Theme.font; pixelSize: 15; weight: Font.DemiBold } }
                            }
                            function lufs(v) { return v === undefined || v <= -70 ? "—" : v.toFixed(1) }
                            Dato { etiqueta: qsTr("MOMENTÁNEO"); valor: parent.lufs(m.lu.momentaneo) }
                            Dato { etiqueta: qsTr("CORTO (3 s)"); valor: parent.lufs(m.lu.corto)
                                   tinte: m.lu.corto !== undefined && m.lu.corto > pgm.objetivo + 2 ? Theme.amber : Theme.text }
                            Dato { etiqueta: qsTr("INTEGRADO"); valor: parent.lufs(m.lu.integrado)
                                   tinte: m.lu.integrado !== undefined && Math.abs(m.lu.integrado - pgm.objetivo) <= 1 ? Theme.ok : Theme.text }
                            Dato { etiqueta: "LRA"; valor: m.lu.lra !== undefined ? m.lu.lra.toFixed(1) : "—" }
                            Dato { etiqueta: "TRUE PEAK"; valor: m.lu.true_peak_db !== undefined && m.lu.true_peak_db > -70 ? m.lu.true_peak_db.toFixed(1) : "—"
                                   tinte: m.lu.true_peak_db !== undefined && m.lu.true_peak_db > -1 ? Theme.live : Theme.text }
                            Text { text: qsTr("Objetivo %1 LUFS").arg(pgm.objetivo.toFixed(0)); color: Theme.textFaint; font { family: Theme.font; pixelSize: 10 } }
                        }
                    }
                }
            }
            // El resto de las salidas: hasta un tercio del ancho (las entradas son lo que se
            // opera); si no entran, se corren.
            ListView {
                id: otrasSalidas
                Layout.fillHeight: true
                Layout.preferredWidth: Math.min(contentWidth, m.width * 0.32)
                orientation: ListView.Horizontal
                spacing: 6
                clip: true
                interactive: contentWidth > width
                model: m.salidas.filter((s) => s.id !== "pista1")
                delegate: TiraSalida { height: otrasSalidas.height }
                ScrollBar.horizontal: ScrollBar { policy: ScrollBar.AsNeeded }
            }
        }

        // --- La matriz (desplegable) ------------------------------------------------
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: m.matrizAbierta ? Math.max(300, m.height * 0.5) : 0
            visible: m.matrizAbierta && m.pestana === "consola"
            radius: 12
            color: Qt.rgba(1, 1, 1, 0.03)
            border.color: Qt.rgba(1, 1, 1, 0.08)
            clip: true
            Behavior on Layout.preferredHeight { NumberAnimation { duration: 160; easing.type: Easing.OutCubic } }
            MatrizAudio {
                id: matriz
                compacta: true
                anchors { fill: parent; margins: 8 }
                visible: m.matrizAbierta
            }
        }
    }
}
