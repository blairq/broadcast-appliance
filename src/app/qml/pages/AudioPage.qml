// Pagina Audio: patchbay tipo JACK por fuente. Cada fuente con audio (microfono,
// placa, video con sonido, invitado) tiene su rack: modulos LSP conectados con
// cables. Arrastrar de una salida (punto a la derecha) a una entrada (punto a la
// izquierda) conecta; la ✕ en el medio de un cable lo quita. A la derecha, los
// parametros del modulo elegido (y su interfaz LSP completa), sus medidores, o
// el analisis (LUFS, espectro, piso de ruido) de la entrada, la salida o un
// analizador. Todo esto tambien lo maneja el asistente por MCP (audio_*).
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import BA
import "../controls"

Rectangle {
    id: page
    color: Theme.bg
    property real leftInset: 92
    readonly property var au: Studio.audio
    property string msg: ""
    // Dos vistas: los racks de filtros de cada fuente, y la matriz de mezcla
    // (que rutea la SALIDA de esos filtros).
    property string vista: "racks"
    // ui_show_page {page: "audio", seccion: "estudio" | "racks" | "matriz" | "mesa" | <fuente>}
    Connections {
        target: Studio
        function onMostrarSeccion(pagina, seccion) {
            if (pagina !== "audio") return
            const v = seccion.toLowerCase()
            if (["racks", "estudio", "matriz", "mesa"].indexOf(v) >= 0) { page.vista = v; return }
            if (page.au) { page.au.selectSource(seccion); page.vista = "racks" }
        }
    }
    Suscripcion { objetivo: page.au; activa: page.visible }

    // --- El rack (el patchbay) --------------------------------------------------------------
    function kindColor(t) {
        if (t === "in" || t === "out") return "#64748b"
        if (t === "analyzer") return "#0ea5e9"
        if (t === "monitor") return "#a78bfa"
        if (t === "texto") return "#22c55e"
        if (t === "generator") return "#f59e0b"
        if (t === "entrada") return "#38bdf8"
        if (t === "habla") return "#f472b6"
        if (t === "traducir") return "#eab308"
        return Theme.accent
    }

    // Arbol de fuentes: un encabezado por grupo y debajo sus fuentes, en el
    // orden en que el motor las informa.
    property var collapsed: ({})
    function groupOpen(g) { return !collapsed[g] }
    function toggleGroup(g) { const c = Object.assign({}, collapsed); c[g] = !c[g]; collapsed = c }
    readonly property var sourceTree: {
        const src = au ? au.sources : []
        const orden = ["Conferencia", "Asamblea", "Micrófonos y líneas del equipo", "Entradas del equipo (placas)", "Voces (texto a audio)",
                       "Audio del equipo (lo que suena)", "Medios y red", "Otras fuentes"]
        const porGrupo = {}
        for (let i = 0; i < src.length; ++i) {
            const g = src[i].grupo || "Otras fuentes"
            if (!porGrupo[g]) porGrupo[g] = []
            porGrupo[g].push(src[i])
        }
        // Un grupo nuevo que no está en el orden también se muestra (al final):
        // antes se descartaba en silencio y la fuente no aparecía.
        for (const g in porGrupo)
            if (orden.indexOf(g) < 0) orden.push(g)
        const filas = []
        for (const g of orden) {
            if (!porGrupo[g]) continue
            filas.push({ fila: "grupo", grupo: g, cuantas: porGrupo[g].length })
            if (groupOpen(g))
                for (const s of porGrupo[g]) filas.push({ fila: "fuente", grupo: g, src: s })
        }
        return filas
    }

    Text {
        anchors.centerIn: parent
        visible: !page.au
        text: qsTr("Esta versión no trae los módulos de audio (CLAP/LSP).")
        color: Theme.textDim; font.pixelSize: 15
    }

    // Racks | Matriz, en su propia franja arriba (las vistas empiezan debajo).
    readonly property int franjaSelector: selectorVista.visible ? selectorVista.height + 14 + 10 : 18
    Row {
        id: selectorVista
        visible: !!page.au
        z: 10
        anchors { top: parent.top; right: parent.right; topMargin: 14; rightMargin: 22 }
        spacing: 4
        GlassButton { compact: true; text: qsTr("Racks"); active: page.vista === "racks"; onClicked: page.vista = "racks" }
        GlassButton { compact: true; text: qsTr("Estudio"); icon: "▦"; active: page.vista === "estudio"; onClicked: page.vista = "estudio"
                      ToolTip.visible: hovered; ToolTip.delay: 400
                      ToolTip.text: qsTr("Todas las fuentes, cada una con su cadena entera (origen → rack → filtros → mezcla) y los cables entre ellas") }
        GlassButton { compact: true; text: qsTr("Matriz"); icon: "▦"; visible: !!Studio.matriz; active: page.vista === "matriz"; onClicked: page.vista = "matriz" }
        GlassButton { compact: true; text: qsTr("Mesa"); active: page.vista === "mesa"; onClicked: page.vista = "mesa" }
    }
    EstudioAudio {
        visible: !!page.au && page.vista === "estudio" && page.visible
        anchors { fill: parent; leftMargin: page.leftInset; margins: 18; topMargin: page.franjaSelector }
        au: page.au
        onAbrirRack: (fuente, nodo) => {
            page.au.selectSource(fuente)
            if (nodo !== "") page.au.select(nodo)
            page.vista = "racks"
        }
    }
    MesaAutomixer {
        visible: !!page.au && page.vista === "mesa" && page.visible
        anchors { fill: parent; leftMargin: page.leftInset; margins: 18; topMargin: page.franjaSelector }
    }
    MatrizAudio {
        visible: !!page.au && !!Studio.matriz && page.vista === "matriz" && page.visible
        // Debajo de la franja del selector Racks | Matriz: nunca se pisan.
        anchors { fill: parent; leftMargin: page.leftInset; margins: 18; topMargin: page.franjaSelector }
    }

    RowLayout {
        visible: !!page.au && page.vista === "racks"
        anchors { fill: parent; leftMargin: page.leftInset; margins: 18; topMargin: page.franjaSelector }
        spacing: 14

        // --- Fuentes con audio + salida de escucha -------------------------------------------
        ColumnLayout {
            Layout.fillWidth: false  // ancho fijo aunque un texto largo empuje (ver EquiposPage)
            Layout.preferredWidth: 250
            Layout.maximumWidth: 250
            Layout.fillHeight: true
            spacing: 10

        Panel {
            Layout.fillWidth: true
            Layout.fillHeight: true
            title: qsTr("Fuentes con audio")
            ListView {
                id: srcList
                anchors.fill: parent
                clip: true
                spacing: 6
                model: page.sourceTree
                delegate: Item {
                    required property var modelData
                    width: srcList.width
                    height: modelData.fila === "grupo" ? 26 : 56

                    // Encabezado del grupo (se pliega con un clic).
                    Rectangle {
                        anchors.fill: parent
                        visible: modelData.fila === "grupo"
                        color: "transparent"
                        RowLayout {
                            anchors { fill: parent; leftMargin: 2; rightMargin: 4 }
                            spacing: 6
                            Text {
                                text: page.groupOpen(modelData.grupo) ? "▾" : "▸"
                                color: Theme.textFaint; font.pixelSize: 11
                            }
                            Text {
                                Layout.fillWidth: true
                                text: qsTranslate("audio", modelData.grupo || "")
                                color: Theme.textDim
                                font { pixelSize: 11; weight: Font.DemiBold; capitalization: Font.AllUppercase; letterSpacing: 0.4 }
                                elide: Text.ElideRight
                            }
                            Text { text: modelData.cuantas || 0; color: Theme.textFaint; font.pixelSize: 11 }
                        }
                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: page.toggleGroup(modelData.grupo)
                        }
                    }

                    // Fuente (sangrada, colgando del grupo).
                    Rectangle {
                        anchors { fill: parent; leftMargin: 12 }
                        visible: modelData.fila === "fuente"
                        radius: 12
                        readonly property var s: modelData.src || ({})
                        color: page.au && page.au.source === s.nombre ? Qt.rgba(1, 1, 1, 0.12) : Qt.rgba(1, 1, 1, 0.04)
                        border.color: page.au && page.au.source === s.nombre ? Theme.accent : "transparent"
                        ColumnLayout {
                            anchors { fill: parent; margins: 8 }
                            spacing: 3
                            RowLayout {
                                Text { text: parent.parent.parent.s.nombre || ""; color: Theme.text; font.pixelSize: 13; elide: Text.ElideRight; Layout.fillWidth: true }
                                Text { visible: !!parent.parent.parent.s.rack; text: "🎚 " + (parent.parent.parent.s.modulos || 0); color: Theme.accent; font.pixelSize: 12 }
                                // El rack corre en la cadena de GStreamer de la entrada: si ya procesa.
                                Text {
                                    readonly property var s: parent.parent.parent.s
                                    visible: !!s.rack
                                    text: s.procesando ? "GST" : "GST…"
                                    color: Theme.textDim
                                    font.pixelSize: 10
                                    MouseArea {
                                        id: motorHover; anchors.fill: parent; hoverEnabled: true
                                        ToolTip.visible: containsMouse; ToolTip.delay: 400
                                        ToolTip.text: parent.s.procesando ? qsTr("El rack corre en la cadena de GStreamer de la entrada")
                                                                          : qsTr("El rack va en GStreamer: esperando que la entrada pase audio")
                                    }
                                }
                            }
                            LevelMeter {
                                Layout.fillWidth: true
                                Layout.preferredHeight: 6
                                db: parent.parent.s.salida && parent.parent.s.salida.salidaDb !== undefined ? parent.parent.s.salida.salidaDb : -100
                            }
                        }
                        MouseArea { anchors.fill: parent; onClicked: page.au.selectSource(parent.s.nombre) }
                    }
                }
                Text {
                    anchors { centerIn: parent; margins: 10 }
                    width: parent.width - 20
                    visible: srcList.count === 0
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.WordWrap
                    text: qsTr("No hay fuentes con audio.\nAgregá un micrófono o una entrada del sistema.")
                    color: Theme.textFaint
                }
            }
        }

        // Micrófonos y entradas de audio del equipo (no aparecen solas: hay que
        // agregarlas). Con el MISMO componente que la página Entradas de audio:
        // una sola forma de crear una entrada de audio.
        GlassButton {
            Layout.fillWidth: true
            text: qsTr("+ Entrada de audio")
            onClicked: { Studio.inputs.refresh(); micPopup.open() }
        }
        Popup {
            id: micPopup
            parent: Overlay.overlay
            anchors.centerIn: parent
            width: 460
            modal: true
            padding: 18
            background: Rectangle { radius: 16; color: Theme.panel; border.color: Theme.glassBorder }
            contentItem: AgregarEntradaAudio {
                onAgregada: (nombre) => {
                    micPopup.close()
                    // La lista de fuentes se rehace sola cuando el runtime avisa (en la
                    // vuelta siguiente); para elegirla ya, se la pide ahora.
                    page.au.refresh()
                    page.au.selectSource(nombre)
                }
            }
        }

        }

        // --- Patchbay ---------------------------------------------------------------------
        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 10

            // Misma barra que el video: qué veo · qué agrego · cómo lo miro · ⋯
            BarraGrafo {
                Layout.fillWidth: true
                titulo: page.au ? page.au.source : ""
                agregar: [
                    { titulo: qsTr("Conectar"), items: [
                        { t: qsTr("Salida (auriculares, parlantes…)"), color: "#a78bfa", f: () => outPopup.open(),
                          tip: qsTr("Una tarjeta de salida de audio en este grafo: conectale lo que quieras oír, sin tocar lo que va al aire.") }
                    ].concat((page.au ? page.au.sources.filter(x => x.nombre !== page.au.source) : []).map(x => ({
                        t: qsTr("Entrada: %1").arg(x.nombre), color: "#38bdf8", f: () => page.msg = page.au.addEntrada(x.nombre),
                        tip: x.grupo ? qsTr("Trae el audio de «%1» (%2) como una tarjeta de este grafo.").arg(x.nombre).arg(qsTranslate("audio", x.grupo))
                                     : qsTr("Trae el audio de «%1» como una tarjeta de este grafo.").arg(x.nombre)
                    }))) },
                    { titulo: qsTr("Procesar"), items: (page.au ? page.au.catalog : []).map(c => ({
                        t: c.titulo, tip: c.ayuda, f: () => page.msg = page.au.addModule(c.clave, "")
                    })).concat([{ t: qsTr("Filtros LSP…"), tip: qsTr("Todos los filtros LSP con captura, búsqueda avanzada y para qué sirve cada uno."), f: () => catalogoFiltros.open() }]) },
                    { titulo: qsTr("Medir y generar"), items: [
                        { t: qsTr("Analizador"), tip: qsTr("Espectro y niveles de lo que le llega"), f: () => page.msg = page.au.addAnalyzer() },
                        { t: qsTr("Generador"), tip: qsTr("Tono, ruido o barrido para probar la cadena"), f: () => page.msg = page.au.addGenerator() }
                    ] },
                    { titulo: qsTr("Voz"), items: [].concat(
                        Studio.voces ? [{ t: qsTr("Texto a voz"), color: "#f472b6", f: () => page.msg = page.au.addHabla(),
                                          tip: qsTr("Dice en voz alta (con la voz y el idioma que elijas) lo que llega por una salida de texto.") }] : [],
                        Studio.traductor ? [{ t: qsTr("Traducir"), color: "#eab308", f: () => page.msg = page.au.addTraduccion(),
                                              tip: qsTr("Traduce lo que publica una «Voz a texto» a otro idioma y lo publica en otra salida de texto (subtítulos, archivo, texto a voz).") }] : [],
                        Studio.voz.disponible ? [
                            { t: qsTr("Voz a texto: dictado al asistente (F9)"), color: "#22c55e", f: () => page.msg = page.au.addTexto("dictado"),
                              tip: qsTr("Transcribe lo que le llega para dictarle al asistente.") },
                            { t: qsTr("Voz a texto: transcripción continua"), color: "#22c55e", f: () => page.msg = page.au.addTexto("continuo"),
                              tip: qsTr("Transcribe todo el tiempo: subtítulos y acta.") }
                        ] : []) }
                ]
                acciones: [
                    // Un rack armado a mano es media hora de trabajo: guardarlo con
                    // nombre permite ponérselo a otra fuente en un clic.
                    { t: qsTr("Plantillas"), icon: "▤", f: () => plantillasPopup.open(),
                      tip: qsTr("Guardar este rack con nombre, o ponerle a esta fuente uno guardado") },
                    { t: qsTr("Traducción…"), icon: "文", f: () => traduccionPopup.open(),
                      tip: qsTr("Traducir en vivo una salida de texto (lo que transcribe «Voz a texto») a otro idioma, y el glosario de términos") }
                ]
                // Si una ventana LSP no aparece (casi siempre: el zoom de su menú
                // la hizo más grande que la pantalla), esto la vuelve a la normalidad.
                mas: [{ t: qsTr("Restablecer interfaz LSP"), f: () => page.msg = page.au.restablecerInterfazLsp(),
                        tip: qsTr("Cierra las ventanas LSP y vuelve su zoom, escala y letra a lo de fábrica (guarda una copia). Para cuando la interfaz de un filtro no aparece.") },
                      // Cuando el rack quedó enredado (cables sacados, módulos que no se van),
                      // volver a cero en un clic: el audio pasa directo, como sin rack.
                      { t: qsTr("Restablecer entrada…"), tip: qsTr("Quita el rack entero de esta entrada: el audio vuelve a pasar directo, sin filtros"),
                        f: () => { const fuente = page.au.source
                                   ApplicationWindow.window.confirm({
                                       title: qsTr("¿Restablecer «%1»?").arg(fuente),
                                       text: qsTr("Se quitan todas las tarjetas y cables de su rack (filtros, medidores, voz a texto, traducción) y el audio vuelve a pasar directo. Si querés conservarlo, guardalo antes en Plantillas."),
                                       accept: qsTr("Restablecer"), reject: qsTr("Cancelar"),
                                       action: function() { if (page.au.source === fuente) page.au.removeRack() } }) } }]
                lienzo: lienzoAudio
            }
            Text {
                Layout.fillWidth: true
                text: page.msg !== "" ? page.msg
                      : (page.au && page.au.error !== "" ? page.au.error
                         : qsTr("Arrastrá desde una salida (●, derecha) a una entrada (●, izquierda) para conectar. La ✕ en el medio de un cable lo quita. Varios cables a una entrada se suman."))
                color: page.msg !== "" || (page.au && page.au.error !== "") ? Theme.live : Theme.textFaint
                font.pixelSize: 12; wrapMode: Text.WordWrap
            }

            // El rack: el patchbay de todos (controls/Patchbay) con lo del audio: puertos
            // estéreo de dos puntos, tarjetas que se agrandan, el sidechain y los medidores.
            Patchbay {
                id: lienzoAudio
                Layout.fillWidth: true
                Layout.fillHeight: true
                nodos: page.au && page.au.graph.nodos ? page.au.graph.nodos : []
                cables: page.au && page.au.graph.cables ? page.au.graph.cables : []
                seleccionado: page.au ? page.au.selected : ""
                grid: page.au ? page.au.gridStep : 20
                // La grilla se ve con el botón (o mientras se acomoda algo).
                grilla: false
                bodyH: 30
                soloMismoTipo: false
                margenSoltar: 70
                expandibles: true
                colorDe: page.kindColor
                // El sidechain se dibuja como un puerto de control (ámbar).
                tipoDe: function(info, id) { return id === "sc" ? "control" : "audio" }
                etiquetaPuerto: function(info, id, entrada, nodo) {
                    return !entrada ? qsTr("salida") : id === "sc" ? qsTr("sidechain") : qsTr("entrada")
                }
                cuerpo: medidorC
                onElegir: (id) => page.au.select(id)
                onMover: (id, x, y) => page.au.moveNode(id, x, y)
                onConectar: (desde, salida, hacia, entrada, chSalida, chEntrada) =>
                    page.msg = page.au.connectPorts(desde, salida, hacia, entrada, chSalida, chEntrada)
                onDesconectar: (desde, salida, hacia, entrada, chSalida, chEntrada) =>
                    page.au.disconnectPorts(desde, salida, hacia, entrada, chSalida, chEntrada)
                onOrdenar: page.au.tidy()
            }
            // El medidor de cada tarjeta: el nivel de salida y cuánto cambia el filtro.
            Component {
                id: medidorC
                Column {
                    property var nodo: ({})
                    readonly property var m: page.au && page.au.meters[nodo.id] ? page.au.meters[nodo.id] : ({})
                    spacing: 3
                    LevelMeter {
                        width: parent.width
                        height: 5
                        db: parent.m.salidaDb !== undefined ? parent.m.salidaDb : (parent.m.entradaDb !== undefined ? parent.m.entradaDb : -100)
                    }
                    Text {
                        width: parent.width
                        horizontalAlignment: Text.AlignRight
                        color: parent.m.cambioDb !== undefined && parent.m.cambioDb < -0.5 ? Theme.amber : Theme.textFaint
                        font.pixelSize: 10
                        text: parent.nodo.bypass ? qsTr("anulado")
                              : parent.m.cambioDb !== undefined ? (parent.m.cambioDb > 0 ? "+" : "") + Theme.db(parent.m.cambioDb) + " dB"
                              : Theme.db(parent.m.salidaDb !== undefined ? parent.m.salidaDb : parent.m.entradaDb) + " dBFS"
                    }
                }
            }
        }

        // --- Detalle del nodo -------------------------------------------------------------
        // Se arrastra para darle más lugar a las tarjetas o al panel (se recuerda).
        Divisor { id: divisorPanel; clave: "audio.panel"; ancho: 360; minimo: 240; maximo: 720 }
        Panel {
            Layout.preferredWidth: divisorPanel.ancho
            Layout.fillHeight: true
            title: page.au && page.au.detail.nodo ? (page.au.detail.etiqueta || page.au.detail.nombre || page.au.detail.nodo) : qsTr("Detalle")
            ScrollView {
                id: detScroll
                anchors.fill: parent
                // Ancho fijo (sin availableWidth): evita el ciclo barra <-> alto del contenido.
                contentWidth: width - 14
                clip: true
                ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
                ColumnLayout {
                    width: detScroll.width - 14
                    spacing: 8
                    readonly property var d: page.au ? page.au.detail : ({})

                    Text {
                        Layout.fillWidth: true
                        visible: !parent.d.nodo
                        text: qsTr("Elegí un nodo del patchbay. La entrada y la salida muestran el análisis del audio (LUFS, espectro, piso de ruido); los módulos, sus parámetros.")
                        color: Theme.textDim; font.pixelSize: 13; wrapMode: Text.WordWrap
                    }

                    // Modulo: acciones
                    Flow {
                        Layout.fillWidth: true
                        spacing: 6
                        visible: parent.d.tipo === "plugin"
                        // El vigía: lo que tarda por bloque, y si lo saltó por lento.
                        Text {
                            visible: parent.parent.d.ms_medio !== undefined
                            text: qsTr("%1 ms/bloque (máx %2 de %3)").arg(Number(parent.parent.d.ms_medio || 0).toFixed(2))
                                  .arg(Number(parent.parent.d.ms_max || 0).toFixed(1)).arg(Number(parent.parent.d.ms_bloque || 0).toFixed(1))
                            color: (parent.parent.d.ms_medio || 0) > (parent.parent.d.ms_bloque || 99) * 0.5 ? Theme.amber : Theme.textFaint
                            font.pixelSize: 11
                        }
                        StatusPill { visible: !!parent.parent.d.bypass_auto; text: qsTr("Saltado por lento"); dot: Theme.live }
                        GlassButton { compact: true; visible: !!parent.parent.d.bypass_auto; text: qsTr("Rearmar"); onClicked: page.au.rearmar(page.au.selected) }
                        GlassButton { compact: true; text: parent.parent.d.bypass ? qsTr("Activar") : qsTr("Anular"); onClicked: page.au.setBypass(page.au.selected, !parent.parent.d.bypass) }
                        GlassButton { compact: true; text: qsTr("Interfaz LSP"); enabled: !!parent.parent.d.tieneEditor; onClicked: page.msg = page.au.openEditor(page.au.selected) }
                        Repeater {
                            model: parent.parent.d.presets || []
                            delegate: GlassButton { required property string modelData; compact: true; text: qsTr("Preset: %1").arg(modelData); onClicked: page.msg = page.au.applyPreset(page.au.selected, modelData) }
                        }
                        GlassButton { compact: true; text: qsTr("Quitar"); onClicked: page.au.removeNode(page.au.selected) }
                    }
                    Flow {
                        Layout.fillWidth: true
                        spacing: 6
                        visible: parent.d.tipo === "analyzer" || parent.d.tipo === "generator"
                        GlassButton { compact: true; text: qsTr("Quitar"); onClicked: page.au.removeNode(page.au.selected) }
                    }
                    // Texto a voz: idioma, voz, qué salida lee, velocidad, probar
                    ColumnLayout {
                        id: hablaCol
                        Layout.fillWidth: true
                        visible: parent.d.tipo === "habla"
                        spacing: 6
                        readonly property var h: (parent.d && parent.d.habla) || ({})
                        readonly property var voces: Studio.voces ? Studio.voces.voces : []
                        readonly property var vozActual: voces.find(v => v.repo === h.voz)
                        property string idioma: vozActual ? vozActual.idioma : "es"
                        readonly property string estado: Studio.voces ? Studio.voces.estados[page.au.source + "/" + page.au.selected] || "" : ""
                        RowLayout {
                            Layout.fillWidth: true
                            Text { text: qsTr("Idioma"); color: Theme.textDim; font.pixelSize: 12; Layout.preferredWidth: 70 }
                            ComboBox {
                                Layout.fillWidth: true
                                model: Studio.voces ? Studio.voces.idiomas : []
                                textRole: "nombre"; valueRole: "codigo"
                                currentIndex: Math.max(0, (Studio.voces ? Studio.voces.idiomas : []).findIndex(x => x.codigo === hablaCol.idioma))
                                onActivated: hablaCol.idioma = currentValue
                            }
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            Text { text: qsTr("Voz"); color: Theme.textDim; font.pixelSize: 12; Layout.preferredWidth: 70 }
                            ComboBox {
                                id: vozBox
                                Layout.fillWidth: true
                                readonly property var lista: [{repo: "", nombre: qsTr("La del sistema"), instalada: true}].concat(hablaCol.voces.filter(v => v.idioma === hablaCol.idioma))
                                model: lista.map(v => v.instalada ? v.nombre : qsTr("%1  (bajar)").arg(v.nombre))
                                currentIndex: Math.max(0, lista.findIndex(v => v.repo === (hablaCol.h.voz || "")))
                                onActivated: {
                                    const v = lista[currentIndex]
                                    if (!v.instalada) Studio.voces.bajar(v.repo)
                                    page.msg = page.au.setHablaOpciones(page.au.selected, {voz: v.repo})
                                }
                            }
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            Text { text: qsTr("Lee"); color: Theme.textDim; font.pixelSize: 12; Layout.preferredWidth: 70 }
                            ComboBox {
                                Layout.fillWidth: true
                                readonly property var lista: [qsTr("(nada: solo lo que le pidan)")].concat((Studio.flujos ? Studio.flujos.flujos : []).map(f => f.nombre))
                                model: lista
                                currentIndex: Math.max(0, lista.indexOf(hablaCol.h.flujo || ""))
                                onActivated: page.msg = page.au.setHablaOpciones(page.au.selected, {flujo: currentIndex === 0 ? "" : currentText})
                                ToolTip.visible: hovered; ToolTip.delay: 400
                                ToolTip.text: qsTr("La salida de texto que dice en voz alta (por ejemplo, lo que transcribe una «Voz a texto»).")
                            }
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            Text { text: qsTr("Velocidad"); color: Theme.textDim; font.pixelSize: 12; Layout.preferredWidth: 70 }
                            Slider { Layout.fillWidth: true; from: 0.5; to: 2.0; stepSize: 0.05; value: hablaCol.h.velocidad || 1.0
                                     onMoved: page.au.setHablaOpciones(page.au.selected, {velocidad: value}) }
                            Text { text: (hablaCol.h.velocidad || 1.0).toFixed(2) + "×"; color: Theme.textFaint; font.pixelSize: 11 }
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            TextField { id: hablaPrueba; Layout.fillWidth: true; placeholderText: qsTr("Escribí algo para que lo diga") }
                            GlassButton { compact: true; text: qsTr("Decir"); enabled: hablaPrueba.text.trim() !== ""
                                          onClicked: page.msg = Studio.voces.decir(page.au.source, page.au.selected, hablaPrueba.text) }
                        }
                        Text { Layout.fillWidth: true; text: hablaCol.estado; color: Theme.textFaint; font.pixelSize: 11; wrapMode: Text.WordWrap }
                        GlassButton { compact: true; text: qsTr("Quitar"); onClicked: page.au.removeNode(page.au.selected) }
                    }
                    // Traducir: qué traduce, a qué idioma, dónde publica
                    TarjetaTraduccion {
                        Layout.fillWidth: true
                        visible: parent.d.tipo === "traducir"
                        traduccionId: parent.d.traduccion_id || ""
                        onQuitar: page.au.removeNode(page.au.selected)
                        onAbrirGlosario: traduccionPopup.open()
                    }
                    // Salida de audio: grabar todo lo que le llega
                    ColumnLayout {
                        id: grabCol
                        Layout.fillWidth: true
                        visible: parent.d.tipo === "monitor"
                        spacing: 4
                        readonly property var g: page.au.grabacion
                        Suscripcion { objetivo: page.au; que: "grabacion"; activa: grabCol.visible && page.visible }
                        RowLayout {
                            Layout.fillWidth: true
                            Toggle { checked: !!grabCol.g.grabar
                                     onToggled: (w) => page.msg = page.au.setGrabar(w) }
                            Text { Layout.fillWidth: true; text: qsTr("Grabar todo lo que le llega en un archivo (.wav)"); color: Theme.text; font.pixelSize: 12; wrapMode: Text.WordWrap }
                            Rectangle { visible: !!grabCol.g.grabando; width: 10; height: 10; radius: 5; color: Theme.live }
                        }
                        Text {
                            Layout.fillWidth: true; visible: !!grabCol.g.grabando; color: Theme.textDim; font.pixelSize: 11; wrapMode: Text.WrapAnywhere
                            text: {
                                const s = Math.floor(grabCol.g.segundos || 0)
                                const t = Math.floor(s / 3600) + ":" + ("0" + Math.floor(s / 60) % 60).slice(-2) + ":" + ("0" + s % 60).slice(-2)
                                return qsTr("Grabando %1 · %2 MB").arg(t).arg(((grabCol.g.bytes || 0) / 1048576).toFixed(1)) + "\n" + (grabCol.g.archivo || "")
                            }
                        }
                        Text { Layout.fillWidth: true; visible: !!grabCol.g.grabar && !grabCol.g.conCable; color: Theme.warn; font.pixelSize: 11; wrapMode: Text.WordWrap
                               text: qsTr("La tarjeta no tiene cable: conectale lo que quieras grabar.") }
                        GlassButton { compact: true; text: qsTr("Abrir la carpeta"); onClicked: Qt.openUrlExternally("file://" + grabCol.g.carpeta) }
                    }
                    // Entrada: de qué fuente trae el audio
                    RowLayout {
                        Layout.fillWidth: true
                        visible: parent.d.tipo === "entrada"
                        Text { text: qsTr("Fuente"); color: Theme.textDim; font.pixelSize: 12 }
                        ComboBox {
                            id: entradaFuente
                            Layout.fillWidth: true
                            readonly property var lista: page.au ? page.au.sources.filter(x => x.nombre !== page.au.source).map(x => x.nombre) : []
                            model: lista
                            currentIndex: lista.indexOf(parent.parent.d.fuente || "")
                            onActivated: page.msg = page.au.setEntradaFuente(page.au.selected, currentText)
                        }
                        GlassButton { compact: true; text: qsTr("Quitar"); onClicked: page.au.removeNode(page.au.selected) }
                    }
                    // Voz a texto: el nombre de su salida, el modo, las salidas, y lo que va transcribiendo
                    RowLayout {
                        Layout.fillWidth: true
                        visible: parent.d.tipo === "texto"
                        Text { text: qsTr("Salida"); color: Theme.textDim; font.pixelSize: 12 }
                        TextField {
                            id: flujoNombre
                            Layout.fillWidth: true
                            text: parent.parent.d.flujo || ""
                            placeholderText: qsTr("Nombre de la salida de texto")
                            onEditingFinished: if (text !== (parent.parent.d.flujo || ""))
                                                   page.msg = page.au.setTextoOpciones(page.au.selected, {flujo: text})
                            ToolTip.visible: hovered; ToolTip.delay: 400
                            ToolTip.text: qsTr("Con este nombre se usa en otras partes: disparadores, texto a voz, MCP.")
                        }
                    }
                    Flow {
                        Layout.fillWidth: true
                        spacing: 6
                        visible: parent.d.tipo === "texto"
                        GlassButton { compact: true; text: qsTr("Dictado"); active: parent.parent.d.modo === "dictado"; onClicked: page.msg = page.au.setTextoOpciones(page.au.selected, {modo: "dictado"}) }
                        GlassButton { compact: true; text: qsTr("Continuo"); active: parent.parent.d.modo === "continuo"; onClicked: page.msg = page.au.setTextoOpciones(page.au.selected, {modo: "continuo"}) }
                        GlassButton { compact: true; text: qsTr("Quitar"); onClicked: page.au.removeNode(page.au.selected) }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        visible: parent.d.tipo === "texto"
                        Toggle {
                            checked: !!(parent.parent.d.salidas && parent.parent.d.salidas.archivo)
                            onToggled: (w) => page.msg = page.au.setTextoOpciones(page.au.selected, {salidas: {archivo: w}})
                        }
                        Text { Layout.fillWidth: true; text: qsTr("Guardar en archivo (.txt con hora y .srt)"); color: Theme.text; font.pixelSize: 12; wrapMode: Text.WordWrap }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        visible: parent.d.tipo === "texto"
                        Text { text: qsTr("Subtítulos en"); color: Theme.textDim; font.pixelSize: 12 }
                        ComboBox {
                            Layout.fillWidth: true
                            readonly property var lista: [qsTr("(ninguno)")].concat(page.au ? page.au.fuentesDeTexto() : [])
                            model: lista
                            currentIndex: Math.max(0, lista.indexOf((parent.parent.d.salidas && parent.parent.d.salidas.rotulo) || qsTr("(ninguno)")))
                            onActivated: page.msg = page.au.setTextoOpciones(page.au.selected, {salidas: {rotulo: currentIndex === 0 ? "" : currentText}})
                            ToolTip.visible: hovered; ToolTip.delay: 400
                            ToolTip.text: qsTr("Una fuente de texto de la escena: su texto pasa a ser la última frase (subtítulo).")
                        }
                    }
                    Text {
                        Layout.fillWidth: true
                        visible: parent.d.tipo === "texto" && parent.d.modo === "dictado"
                        text: Studio.voz.ultimo !== "" ? qsTr("Último dictado: «%1»").arg(Studio.voz.ultimo) : Studio.voz.estado
                        color: Theme.textDim; font.pixelSize: 12; wrapMode: Text.WordWrap
                    }
                    // Cuándo corta una frase (pausa, largo máximo, umbral): es de toda
                    // la transcripción continua, también está en Configuración.
                    AjustesCorte {
                        Layout.fillWidth: true
                        visible: parent.d.tipo === "texto" && parent.d.modo === "continuo"
                        flujo: parent.d.flujo || ""
                    }
                    Repeater {
                        model: parent.d.tipo === "texto" && parent.d.modo === "continuo"
                               ? Studio.voz.transcripcion.filter(x => x.flujo === parent.d.flujo).slice(-12) : []
                        delegate: Text {
                            required property var modelData
                            Layout.fillWidth: true
                            text: "<font color='" + Theme.textFaint + "'>" + modelData.hora + "</font>  " + modelData.texto
                            textFormat: Text.StyledText
                            color: Theme.text; font.pixelSize: 12; wrapMode: Text.WordWrap
                        }
                    }
                    Text {
                        Layout.fillWidth: true
                        visible: !!parent.d.ayuda
                        text: parent.d.ayuda || ""
                        color: Theme.textFaint; font.pixelSize: 11; wrapMode: Text.WordWrap
                    }

                    // Medidores
                    GridLayout {
                        Layout.fillWidth: true
                        visible: !!parent.d.medidores && parent.d.tipo !== undefined
                        columns: 2
                        columnSpacing: 8
                        Text { text: qsTr("Entrada"); color: Theme.textDim; font.pixelSize: 12; visible: !!(parent.parent.d.medidores && parent.parent.d.medidores.entradaDb !== undefined) }
                        LevelMeter { Layout.fillWidth: true; Layout.preferredHeight: 8; visible: !!(parent.parent.d.medidores && parent.parent.d.medidores.entradaDb !== undefined)
                                     db: parent.parent.d.medidores && parent.parent.d.medidores.entradaDb !== undefined ? parent.parent.d.medidores.entradaDb : -100 }
                        Text { text: qsTr("Salida"); color: Theme.textDim; font.pixelSize: 12; visible: !!(parent.parent.d.medidores && parent.parent.d.medidores.salidaDb !== undefined) }
                        LevelMeter { Layout.fillWidth: true; Layout.preferredHeight: 8; visible: !!(parent.parent.d.medidores && parent.parent.d.medidores.salidaDb !== undefined)
                                     db: parent.parent.d.medidores && parent.parent.d.medidores.salidaDb !== undefined ? parent.parent.d.medidores.salidaDb : -100 }
                        Text { text: qsTr("Cambio"); color: Theme.textDim; font.pixelSize: 12; visible: !!(parent.parent.d.medidores && parent.parent.d.medidores.cambioDb !== undefined) }
                        Text { visible: !!(parent.parent.d.medidores && parent.parent.d.medidores.cambioDb !== undefined)
                               text: parent.parent.d.medidores ? Theme.db(parent.parent.d.medidores.cambioDb) + " dB" : ""
                               color: Theme.amber; font { pixelSize: 13; weight: Font.DemiBold } }
                    }

                    // Parametros del modulo (catalogo)
                    Repeater {
                        model: parent.d.parametros ? Object.keys(parent.d.parametros).filter(k => k !== "bandas" && k !== "opcionesBandas") : []
                        delegate: ColumnLayout {
                            id: pr
                            required property string modelData
                            readonly property var p: page.au.detail.parametros[modelData]
                            Layout.fillWidth: true
                            spacing: 2
                            RowLayout {
                                Layout.fillWidth: true
                                Text { text: pr.modelData.replace(/_/g, " "); color: Theme.text; font.pixelSize: 12; Layout.fillWidth: true
                                       ToolTip.visible: prMa.containsMouse; ToolTip.text: pr.p.ayuda || ""
                                       MouseArea { id: prMa; anchors.fill: parent; hoverEnabled: true } }
                                Text { text: pr.p.texto; color: Theme.accent; font.pixelSize: 12 }
                            }
                            ComboBox {
                                Layout.fillWidth: true
                                visible: !!pr.p.opciones
                                model: pr.p.opciones || []
                                currentIndex: pr.p.opciones ? pr.p.opciones.indexOf(pr.p.texto) : -1
                                onActivated: page.msg = page.au.setParam(page.au.selected, pr.modelData, currentText)
                            }
                            TextField {
                                Layout.fillWidth: true
                                visible: !pr.p.opciones
                                placeholderText: qsTr("nuevo valor (%1)").arg(pr.p.rango ? pr.p.rango.join(" … ") : "")
                                onAccepted: { page.msg = page.au.setParam(page.au.selected, pr.modelData, text); text = "" }
                            }
                        }
                    }

                    // Bandas (EQ / multibanda)
                    Repeater {
                        model: parent.d.parametros && parent.d.parametros.bandas ? parent.d.parametros.bandas : []
                        delegate: Rectangle {
                            id: bd
                            required property var modelData
                            Layout.fillWidth: true
                            implicitHeight: bandCol.implicitHeight + 14
                            radius: 10
                            color: Qt.rgba(1, 1, 1, 0.04)
                            readonly property var opts: page.au.detail.parametros.opcionesBandas || ({})
                            ColumnLayout {
                                id: bandCol
                                anchors { left: parent.left; right: parent.right; top: parent.top; margins: 7 }
                                spacing: 3
                                Text { text: qsTr("Banda %1").arg(bd.modelData.banda); color: Theme.text; font { pixelSize: 12; weight: Font.DemiBold } }
                                Repeater {
                                    model: Object.keys(bd.modelData).filter(k => k !== "banda")
                                    delegate: RowLayout {
                                        id: br
                                        required property string modelData
                                        Layout.fillWidth: true
                                        Text { text: br.modelData.replace(/_/g, " "); color: Theme.textDim; font.pixelSize: 11; Layout.preferredWidth: 90 }
                                        ComboBox {
                                            Layout.fillWidth: true
                                            visible: !!bd.opts[br.modelData]
                                            model: bd.opts[br.modelData] || []
                                            currentIndex: bd.opts[br.modelData] ? bd.opts[br.modelData].indexOf(bd.modelData[br.modelData]) : -1
                                            onActivated: page.msg = page.au.setParam(page.au.selected, br.modelData, currentText, bd.modelData.banda)
                                        }
                                        TextField {
                                            Layout.fillWidth: true
                                            visible: !bd.opts[br.modelData]
                                            placeholderText: bd.modelData[br.modelData]
                                            onAccepted: { page.msg = page.au.setParam(page.au.selected, br.modelData, text, bd.modelData.banda); text = "" }
                                        }
                                    }
                                }
                            }
                        }
                    }

                    // Generador
                    ColumnLayout {
                        Layout.fillWidth: true
                        visible: parent.d.tipo === "generator"
                        readonly property var s: parent.d.senal || ({})
                        ComboBox {
                            id: genKind
                            Layout.fillWidth: true
                            // La clave va al motor; la etiqueta se traduce.
                            model: [{ clave: "silencio", t: qsTr("silencio") }, { clave: "seno", t: qsTr("seno") },
                                    { clave: "rosa", t: qsTr("rosa") }, { clave: "blanco", t: qsTr("blanco") },
                                    { clave: "barrido", t: qsTr("barrido") }]
                            textRole: "t"; valueRole: "clave"
                            currentIndex: Math.max(0, model.findIndex(x => x.clave === (parent.s.tipo || "silencio")))
                            onActivated: page.au.setGenerator(page.au.selected, currentValue, Number(genFreq.text || 1000), Number(genLvl.text || -20))
                        }
                        RowLayout {
                            TextField { id: genFreq; Layout.fillWidth: true; placeholderText: "Hz (" + (parent.parent.s.frecuencia || 1000) + ")"
                                        onAccepted: page.au.setGenerator(page.au.selected, genKind.currentValue, Number(text), Number(genLvl.text || parent.parent.s.nivelDb || -20)) }
                            TextField { id: genLvl; Layout.fillWidth: true; placeholderText: "dBFS (" + (parent.parent.s.nivelDb || -20) + ")"
                                        onAccepted: page.au.setGenerator(page.au.selected, genKind.currentValue, Number(genFreq.text || parent.parent.s.frecuencia || 1000), Number(text)) }
                        }
                        Text { Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.textFaint; font.pixelSize: 11
                               text: qsTr("El generador queda suelto: conectalo a un analizador para medir. Si lo conectás a la cadena, se escucha al aire.") }
                    }

                    // Analisis (entrada, salida, analizador)
                    ColumnLayout {
                        Layout.fillWidth: true
                        visible: !!parent.d.analisis
                        readonly property var an: parent.d.analisis || ({})
                        spacing: 4
                        GridLayout {
                            columns: 2
                            columnSpacing: 12
                            Text { text: "LUFS M / S / I"; color: Theme.textDim; font.pixelSize: 12 }
                            Text { text: parent.parent.an.lufs ? Theme.db(parent.parent.an.lufs.momentaneo) + " / " + Theme.db(parent.parent.an.lufs.corto) + " / " + Theme.db(parent.parent.an.lufs.integrado) : "—"
                                   color: Theme.text; font.pixelSize: 12 }
                            Text { text: qsTr("Pico (máx.)"); color: Theme.textDim; font.pixelSize: 12 }
                            Text { text: Theme.db(parent.parent.an.picoDbfs) + " (" + Theme.db(parent.parent.an.picoMaximoDbfs) + ") dBFS"; color: Theme.text; font.pixelSize: 12 }
                            Text { text: qsTr("Piso de ruido"); color: Theme.textDim; font.pixelSize: 12 }
                            Text { text: Theme.db(parent.parent.an.pisoDeRuidoDbfs) + " dBFS"; color: Theme.text; font.pixelSize: 12 }
                            Text { text: qsTr("Nivel activo"); color: Theme.textDim; font.pixelSize: 12 }
                            Text { text: Theme.db(parent.parent.an.nivelActivoDbfs) + " dBFS"; color: Theme.text; font.pixelSize: 12 }
                        }
                        // Espectro en tercios de octava
                        Item {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 120
                            Row {
                                anchors.fill: parent
                                spacing: 1
                                Repeater {
                                    model: parent.parent.parent.an.bandas || []
                                    delegate: Rectangle {
                                        required property real modelData
                                        width: (parent.width - 29) / 30
                                        height: Math.max(1, parent.height * Math.max(0, (modelData + 90) / 90))
                                        anchors.bottom: parent.bottom
                                        radius: 2
                                        color: Qt.rgba(0.4, 0.7, 1, 0.85)
                                    }
                                }
                            }
                        }
                        RowLayout { Layout.fillWidth: true
                            Text { text: "25 Hz"; color: Theme.textFaint; font.pixelSize: 10; Layout.fillWidth: true }
                            Text { text: "1 kHz"; color: Theme.textFaint; font.pixelSize: 10; Layout.fillWidth: true; horizontalAlignment: Text.AlignHCenter }
                            Text { text: "20 kHz"; color: Theme.textFaint; font.pixelSize: 10 } }
                        Repeater {
                            model: parent.an.pistas || []
                            delegate: Text { required property string modelData; Layout.fillWidth: true; text: "• " + modelData; color: Theme.textDim; font.pixelSize: 11; wrapMode: Text.WordWrap }
                        }
                    }
                }
            }
        }
    }

    // Salidas de audio del equipo, agrupadas por dispositivo.
    Popup {
        id: outPopup
        anchors.centerIn: parent
        width: 520
        height: 460
        modal: true
        background: Rectangle { radius: 16; color: Theme.panel; border.color: Theme.glassBorder }
        ColumnLayout {
            anchors { fill: parent; margins: 16 }
            spacing: 10
            Text {
                text: qsTr("Mandar el audio de esta fuente a una salida")
                color: Theme.text; font { pixelSize: 16; weight: Font.DemiBold }
            }
            Text {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                color: Theme.textDim
                font.pixelSize: 12
                text: qsTr("Se agrega una tarjeta con el nombre de la salida al grafo de «%1». Conectale lo que quieras oír. No cambia lo que sale al aire. El motor reproduce por una salida a la vez: si elegís otra, se usa esa.").arg(page.au ? page.au.source : "")
            }
            ListView {
                id: outList
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                spacing: 4
                model: {
                    // Encabezado por dispositivo y debajo sus salidas.
                    const grupos = page.au ? page.au.outputDevices : []
                    const filas = []
                    for (let i = 0; i < grupos.length; ++i) {
                        filas.push({ fila: "disp", nombre: grupos[i].dispositivo })
                        const sal = grupos[i].salidas || []
                        for (let k = 0; k < sal.length; ++k)
                            filas.push({ fila: "salida", nombre: sal[k].nombre, id: sal[k].id })
                    }
                    return filas
                }
                delegate: Item {
                    required property var modelData
                    width: outList.width
                    height: modelData.fila === "disp" ? 26 : 34
                    Text {
                        visible: modelData.fila === "disp"
                        anchors { left: parent.left; verticalCenter: parent.verticalCenter }
                        text: modelData.nombre || ""
                        color: Theme.textDim
                        font { pixelSize: 11; weight: Font.DemiBold; capitalization: Font.AllUppercase; letterSpacing: 0.4 }
                        elide: Text.ElideRight
                        width: parent.width
                    }
                    Rectangle {
                        visible: modelData.fila === "salida"
                        anchors { fill: parent; leftMargin: 12 }
                        radius: 8
                        color: outMa.containsMouse ? Qt.rgba(1, 1, 1, 0.10) : "transparent"
                        Text {
                            anchors { left: parent.left; leftMargin: 8; right: parent.right; verticalCenter: parent.verticalCenter }
                            text: modelData.nombre || ""
                            color: Theme.text; font.pixelSize: 13; elide: Text.ElideRight
                        }
                        MouseArea {
                            id: outMa
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                page.msg = page.au.addOutput(modelData.id)
                                outPopup.close()
                            }
                        }
                    }
                }
                Text {
                    anchors.centerIn: parent
                    visible: outList.count === 0
                    text: qsTr("No se encontraron salidas de audio.")
                    color: Theme.textFaint
                }
            }
            GlassButton { Layout.alignment: Qt.AlignRight; compact: true; text: qsTr("Cerrar"); onClicked: outPopup.close() }
        }
    }

    // --- El catálogo de filtros (búsqueda avanzada) ------------------------------
    CatalogoFiltros {
        id: catalogoFiltros
        onElegido: (id) => page.msg = page.au.addModule(id, "")
    }

    // --- Plantillas de rack ---------------------------------------------------
    Popup {
        id: traduccionPopup
        anchors.centerIn: parent
        width: Math.min(parent.width - 80, 900)
        height: Math.min(parent.height - 80, 760)
        // El tamaño es fijo: sin esto el alto implícito salía del contenido, que a su
        // vez se estira al popup (bucle de bindings).
        implicitWidth: width
        implicitHeight: height
        modal: true
        padding: 18
        background: Rectangle { color: Theme.panel; radius: Theme.radiusSmall; border.color: Theme.glassBorder }
        ColumnLayout {
            anchors.fill: parent
            spacing: 10
            RowLayout {
                Layout.fillWidth: true
                Text { text: qsTr("Traducción en vivo"); color: Theme.text; font { pixelSize: 18; weight: Font.DemiBold } Layout.fillWidth: true }
                GlassButton { compact: true; text: qsTr("Cerrar"); onClicked: traduccionPopup.close() }
            }
            ScrollView {
                id: trScroll
                Layout.fillWidth: true
                Layout.fillHeight: true
                contentWidth: availableWidth
                clip: true
                PanelTraduccion { width: trScroll.availableWidth }
            }
        }
    }
    Popup {
        id: plantillasPopup
        anchors.centerIn: Overlay.overlay
        width: 520
        modal: true
        padding: 16
        background: Rectangle { color: Theme.panel; radius: Theme.radiusSmall; border.color: Theme.glassBorder }

        ColumnLayout {
            anchors.fill: parent
            spacing: 10

            Text {
                text: qsTr("Plantillas de rack")
                color: Theme.text
                font { family: Theme.font; pixelSize: 16; weight: Font.DemiBold }
            }
            Text {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                color: Theme.textDim
                font.pixelSize: 12
                text: qsTr("Guardá el rack de esta fuente con un nombre y poneselo a otra en un clic. Si la marcás como predeterminada de un tipo de fuente, las nuevas de ese tipo la toman solas al crearse — es lo que hace que un invitado ya suene bien sin que nadie le arme nada.")
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                TextField {
                    id: nuevoNombre
                    Layout.fillWidth: true
                    placeholderText: qsTr("Nombre para guardar este rack")
                    color: Theme.text
                    font.pixelSize: 13
                }
                GlassButton {
                    text: qsTr("Guardar"); compact: true
                    onClicked: {
                        page.msg = page.au.guardarPlantilla(nuevoNombre.text)
                        if (page.msg === "") nuevoNombre.text = ""
                    }
                }
            }

            ListView {
                Layout.fillWidth: true
                Layout.preferredHeight: Math.min(260, 46 * Math.max(1, (page.au ? page.au.plantillas.length : 0)))
                clip: true
                spacing: 4
                model: page.au ? page.au.plantillas : []
                delegate: RowLayout {
                    required property var modelData
                    width: ListView.view.width
                    spacing: 6
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 0
                        Text {
                            text: modelData.nombre
                            color: Theme.text
                            font { pixelSize: 13; weight: Font.Medium }
                        }
                        Text {
                            text: modelData.paraTipo !== ""
                                  ? qsTr("%n módulo  ·  predeterminada de %1", "", modelData.modulos).arg(modelData.paraTipo)
                                  : qsTr("%n módulo", "", modelData.modulos)
                            color: modelData.paraTipo !== "" ? Theme.accent : Theme.textFaint
                            font.pixelSize: 11
                        }
                    }
                    GlassButton {
                        text: qsTr("Aplicar"); compact: true
                        onClicked: { page.msg = page.au.aplicarPlantilla(modelData.nombre); plantillasPopup.close() }
                        ToolTip.visible: hovered
                        ToolTip.text: qsTr("Reemplaza el rack de « %1 » por este").arg(page.au ? page.au.source : "")
                    }
                    GlassButton {
                        text: modelData.paraTipo !== "" ? qsTr("Quitar defecto") : qsTr("Por defecto")
                        compact: true
                        onClicked: page.msg = page.au.plantillaPorDefecto(
                            modelData.nombre, modelData.paraTipo !== "" ? "" : page.au.tipoDeFuente())
                        ToolTip.visible: hovered
                        ToolTip.text: modelData.paraTipo !== ""
                            ? qsTr("Las fuentes nuevas dejan de tomarla")
                            : qsTr("Las fuentes nuevas del mismo tipo que « %1 » la toman solas").arg(page.au ? page.au.source : "")
                    }
                    GlassButton {
                        icon: "✕"; compact: true
                        onClicked: page.msg = page.au.borrarPlantilla(modelData.nombre)
                    }
                }
            }

            Text {
                visible: page.au && page.au.plantillas.length === 0
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignHCenter
                text: qsTr("Todavía no guardaste ninguna.")
                color: Theme.textFaint
                font.pixelSize: 12
            }

            GlassButton { text: qsTr("Cerrar"); compact: true; onClicked: plantillasPopup.close() }
        }
    }
}
