// La pestaña «Sincronía» de la vista de Latencias (la vista única de sincronización; Video
// lleva acá con su botón «Sincronía y red…»). Lo que el asistente hace con estudio_latencia,
// sincronia_clase, video_reloj, video_entrada_red, video_sincronia y video_desfase, para un humano:
//   - La latencia del estudio: el número, quién la marca y cada entrada contra esa línea
//     (sincronizadas con la sala o con su propia demora; se cambia con un toque).
//   - El reloj del estudio y el colchón de las entradas de red.
//   - Las entradas de red y de plugins, con su diagnóstico; comprobar dos cámaras; el audio
//     de cada canal con su video; una entrada de red nueva.
// Todo lo que se ve sale de las propiedades de Studio.video (la vista del grafo de video,
// que llega con la foto del motor): se rearma solo cuando cambia, sin sondear.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import BA

Item {
    id: pop
    property var vg: null
    /// Pide ver el mapa (la otra pestaña).
    signal verMapa()
    /// La edad con que sale el programa, medida (el mapa de tiempos); -1 si no se sabe.
    property real programaMs: -1

    readonly property var reloj: pop.vg ? pop.vg.reloj : ({})
    readonly property var red: pop.vg ? pop.vg.estadoRed : []
    readonly property var diag: pop.vg ? pop.vg.diagnostico : []
    property string medicionTexto: ""
    property bool midiendo: false

    function avisar(e, ok) { Studio.notify(e !== "" ? e : ok, e !== "" ? "error" : "info") }
    function colorGravedad(g) { return g === "error" ? Theme.live : g === "aviso" ? Theme.warn : Theme.textDim }
    function entradasTodas() { return pop.vg ? pop.vg.entradas : [] }

    function alMostrarse() { tipoReloj.currentIndex = Math.max(0, ["sistema", "ntp", "ptp", "red"].indexOf(pop.reloj.tipo || "sistema")) }
    onVisibleChanged: if (visible) alMostrarse()
    Component.onCompleted: if (visible) alMostrarse()
    Connections {
        target: pop.vg
        function onMedicion(r) {
            pop.midiendo = false
            if (r.error) { pop.medicionTexto = "✕ " + r.error; return }
            let t = (r.significa ? r.significa + ": " : "") + qsTr("«%1» va %2 ms detrás de «%3»").arg(r.b).arg(r.desfase_ms).arg(r.a)
            if (r.confianza !== undefined) t += " " + qsTr("(confianza %1, ±%2 ms)").arg(Number(r.confianza).toFixed(2)).arg(r.resolucion_ms || 0)
            if (r.aplicado) t += "\n" + qsTr("Corregido. Medido de nuevo: %1").arg(r.despues ? r.despues.desfase_ms + " ms — " + r.despues.significa : "—")
            if (r.no_aplicado) t += "\n" + qsTr("No se pudo corregir: %1").arg(r.no_aplicado)
            pop.medicionTexto = t
        }
    }

    ScrollView {
        id: sv
        anchors.fill: parent
        contentWidth: availableWidth
        clip: true
        ColumnLayout {
            width: sv.availableWidth
            spacing: 14

            // --- La latencia del estudio: el número, quién la marca y cada entrada ------------------
            Panel {
                id: latPanel
                Layout.fillWidth: true
                padding: 16
                implicitHeight: latCol.implicitHeight + 2 * padding
                readonly property var lat: pop.vg && pop.vg.latencia.latencia_ms !== undefined
                                           ? pop.vg.latencia
                                           : ({ latencia_ms: 0, objetivo_ms: 0, margen_ms: 30, tope_ms: 0, automatico: true, tarde: [], entradas: [] })
                // Las de video con señal (las de solo audio y las que no reciben nada, aparte: no suman a la vista).
                readonly property var conSenal: (lat.entradas || []).filter(e => e.video && ((e.pide_ms || 0) > 0 || (e.edad_p50_ms || 0) > 0))
                readonly property int sinSenal: (lat.entradas || []).length - conSenal.length
                readonly property var sala: conSenal.filter(e => e.clase === "sala").sort((a, b) => b.pide_ms - a.pide_ms)
                readonly property var propias: conSenal.filter(e => e.clase !== "sala").sort((a, b) => b.pide_ms - a.pide_ms)
                // La escala de las barras: lo más grande que haya, con aire.
                readonly property real escala: {
                    let m = Math.max(50, lat.latencia_ms || 0)
                    for (const e of (lat.entradas || [])) m = Math.max(m, (e.pide_ms || 0) + (e.retraso_propio_ms || 0))
                    return m * 1.15
                }
                function ms(v) { return Math.round(v || 0) + " ms" }
                /// Cambia la clase: la latencia nueva llega con la vista (la entrada se rearma).
                function cambiarClase(entrada, clase) {
                    pop.avisar(pop.vg.claseSincroniaUi(entrada, clase),
                               (clase === "sala" ? qsTr("«%1» se sincroniza con la sala") : qsTr("«%1» va con su propia demora")).arg(entrada))
                }

                // Una entrada: su barra contra la línea de la latencia del estudio, y su clase.
                component FilaSinc: Rectangle {
                    id: fila
                    required property var modelData
                    readonly property bool sala: modelData.clase === "sala"
                    readonly property color tono: modelData.marca ? Theme.accent : modelData.llega_tarde ? Theme.live
                                                : sala ? "#4fd8ff" : "#c38bff"
                    Layout.fillWidth: true
                    implicitHeight: 46
                    radius: 10
                    color: modelData.marca ? Qt.rgba(1, 0.42, 0.17, 0.10) : Qt.rgba(1, 1, 1, 0.03)
                    border.color: modelData.marca ? Theme.accent : "transparent"
                    RowLayout {
                        anchors { fill: parent; leftMargin: 10; rightMargin: 10 }
                        spacing: 10
                        Column {
                            Layout.preferredWidth: 170
                            Text {
                                width: parent.width
                                text: fila.modelData.entrada
                                color: Theme.text; font { pixelSize: 12; weight: Font.DemiBold }
                                elide: Text.ElideRight
                            }
                            Text {
                                text: fila.modelData.marca ? qsTr("marca la latencia") : fila.modelData.llega_tarde ? qsTr("pasa el tope: va con su demora")
                                    : !fila.modelData.en_escena ? qsTr("no está en ninguna escena") : (fila.sala ? qsTr("sincronizada") : qsTr("con su propia demora"))
                                color: fila.modelData.marca ? Theme.accent : Theme.textFaint; font.pixelSize: 9
                            }
                        }
                        // La barra: lo que pide (edad p90 + 10), la línea de la latencia del estudio y,
                        // si va con su propia demora, lo que se le corre la hora.
                        Item {
                            id: pista
                            Layout.fillWidth: true
                            Layout.preferredHeight: 18
                            Rectangle { anchors.fill: parent; radius: 9; color: Qt.rgba(1, 1, 1, 0.05) }
                            Rectangle {
                                height: parent.height; radius: 9
                                width: Math.max(4, pista.width * Math.min(1, (fila.modelData.pide_ms || 0) / latPanel.escala))
                                gradient: Gradient {
                                    orientation: Gradient.Horizontal
                                    GradientStop { position: 0.0; color: Qt.rgba(fila.tono.r, fila.tono.g, fila.tono.b, 0.35) }
                                    GradientStop { position: 1.0; color: fila.tono }
                                }
                            }
                            Rectangle {
                                visible: (fila.modelData.retraso_propio_ms || 0) > 0
                                x: pista.width * Math.min(1, (fila.modelData.pide_ms || 0) / latPanel.escala)
                                height: parent.height; radius: 4
                                width: pista.width * Math.min(1, (fila.modelData.retraso_propio_ms || 0) / latPanel.escala)
                                color: Qt.rgba(0.76, 0.55, 1, 0.25)
                                border.color: "#c38bff"
                            }
                            // La línea de la latencia del estudio.
                            Rectangle {
                                x: pista.width * Math.min(1, (latPanel.lat.latencia_ms || 0) / latPanel.escala) - 1
                                y: -4; width: 2; height: parent.height + 8; radius: 1
                                color: Theme.text; opacity: 0.8
                            }
                        }
                        Column {
                            Layout.preferredWidth: 96
                            Text {
                                anchors.right: parent.right
                                text: latPanel.ms(fila.modelData.pide_ms)
                                color: fila.tono; font { pixelSize: 15; weight: Font.DemiBold }
                            }
                            Text {
                                anchors.right: parent.right
                                text: ((fila.modelData.retraso_propio_ms || 0) > 0 ? qsTr("+%1 propios").arg(latPanel.ms(fila.modelData.retraso_propio_ms))
                                                                                   : qsTr("tarda %1 · tiembla %2").arg(latPanel.ms(fila.modelData.edad_p50_ms))
                                                                                                                   .arg(latPanel.ms(fila.modelData.tiembla_ms))) +
                                      ((fila.modelData.cuadros_tarde_60s || 0) > 0 ? " · " + qsTr("%1 tarde").arg(fila.modelData.cuadros_tarde_60s) : "")
                                color: (fila.modelData.cuadros_tarde_60s || 0) > 0 ? Theme.live : Theme.textFaint
                                font.pixelSize: 9
                            }
                        }
                        // La clase, con un toque (rearma la entrada: un instante sin imagen).
                        Row {
                            spacing: 2
                            GlassButton {
                                compact: true; text: qsTr("Sala"); active: fila.sala
                                onClicked: if (!fila.sala) latPanel.cambiarClase(fila.modelData.entrada, "sala")
                                ToolTip.visible: hovered; ToolTip.delay: 500
                                ToolTip.text: qsTr("Sincronizada: sale alineada con las demás (marca la latencia si es la más lenta)")
                            }
                            GlassButton {
                                compact: true; text: qsTr("Propia"); active: !fila.sala
                                onClicked: if (fila.sala) latPanel.cambiarClase(fila.modelData.entrada, "propia")
                                ToolTip.visible: hovered; ToolTip.delay: 500
                                ToolTip.text: qsTr("Con su propia demora: no atrasa a las demás (invitados, ventanas, una cámara que toma otra cosa)")
                            }
                        }
                    }
                }

                ColumnLayout {
                    id: latCol
                    // Arriba y a lo ancho (no fill): el panel toma su alto de acá; con fill se armaba
                    // un bucle de acomodo («Layout polish loop») que dejaba los botones sin responder bien.
                    anchors { left: parent.left; right: parent.right; top: parent.top }
                    spacing: 12
                    // --- Los números ---
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 28
                        Column {
                            Text { text: qsTr("Latencia del estudio"); color: Theme.textDim; font.pixelSize: 12 }
                            Row {
                                spacing: 4
                                Text { id: grande; text: Math.round(latPanel.lat.latencia_ms || 0); color: Theme.text; font { pixelSize: 46; weight: Font.Light } }
                                Text { text: "ms"; color: Theme.textDim; font.pixelSize: 16; anchors.baseline: grande.baseline }
                            }
                        }
                        Column {
                            Text { text: qsTr("La marca"); color: Theme.textDim; font.pixelSize: 12 }
                            Text {
                                text: latPanel.lat.la_marca ? latPanel.lat.la_marca : qsTr("nadie")
                                color: Theme.accent; font { pixelSize: 20; weight: Font.DemiBold }
                            }
                        }
                        Column {
                            Text { text: qsTr("El programa sale con"); color: Theme.textDim; font.pixelSize: 12 }
                            Text {
                                text: pop.programaMs >= 0 ? latPanel.ms(pop.programaMs)
                                                          : "~" + latPanel.ms((latPanel.lat.latencia_ms || 0) + (latPanel.lat.margen_ms || 0))
                                color: Theme.text; font { pixelSize: 20; weight: Font.DemiBold }
                            }
                            Text {
                                text: pop.programaMs >= 0 ? qsTr("medido desde la captura") : qsTr("+%1 del compositor (estimado)").arg(latPanel.ms(latPanel.lat.margen_ms))
                                color: Theme.textFaint; font.pixelSize: 9
                            }
                        }
                        Item { Layout.fillWidth: true }
                        Column {
                            spacing: 6
                            StatusPill {
                                text: latPanel.lat.baja_sola ? qsTr("Sin nada al aire: se acomoda sola") : qsTr("Al aire: solo sube")
                                dot: latPanel.lat.baja_sola ? Theme.ok : Theme.amber
                            }
                            // Si el colchón alcanza: los cuadros que llegaron tarde al compositor.
                            StatusPill {
                                readonly property int n: latPanel.lat.cuadros_tarde_60s || 0
                                text: n === 0 ? qsTr("Ningún cuadro tarde (último minuto)") : qsTr("%n cuadro tarde en el último minuto", "", n)
                                dot: n === 0 ? Theme.ok : n < 10 ? Theme.amber : Theme.live
                            }
                            GlassButton {
                                visible: latPanel.lat.puede_bajarse === true
                                text: qsTr("Bajar a %1").arg(latPanel.ms(latPanel.lat.objetivo_ms))
                                icon: "↓"; active: true
                                onClicked: pop.avisar(pop.vg.bajarLatenciaUi(), qsTr("Latencia del estudio: %1").arg(latPanel.ms(latPanel.lat.objetivo_ms)))
                                ToolTip.visible: hovered; ToolTip.delay: 300
                                ToolTip.text: latPanel.lat.baja_sola ? qsTr("Baja a lo que piden las sincronizadas")
                                                                     : qsTr("Con algo al aire, bajarla es un salto en todas las salidas")
                            }
                        }
                    }
                    Text {
                        Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.textDim; font.pixelSize: 11
                        text: qsTr("Las entradas sincronizadas salen alineadas entre sí y con el audio: el estudio espera a la más lenta (la línea blanca). " +
                                   "No se mueve al cambiar de escena: en vivo solo sube, si una entrada lo pide sostenido. Para bajarla, mejorá la que la marca " +
                                   "(otro modo de cámara, menos procesamiento) o pasala a «Propia» si no tiene que alinearse con la sala.")
                    }
                    // --- Las sincronizadas ---
                    Text {
                        visible: latPanel.sala.length > 0
                        text: qsTr("Sincronizadas con la sala · %1").arg(latPanel.sala.length)
                        color: Theme.text; font { pixelSize: 13; weight: Font.DemiBold }
                    }
                    Repeater { model: latPanel.sala; delegate: FilaSinc {} }
                    // --- Con su propia demora ---
                    Text {
                        visible: latPanel.propias.length > 0
                        text: qsTr("Con su propia demora · %1   (no atrasan a las demás; si llegan tarde se les corre la hora, audio y video juntos)").arg(latPanel.propias.length)
                        color: Theme.text; font { pixelSize: 13; weight: Font.DemiBold }
                    }
                    Repeater { model: latPanel.propias; delegate: FilaSinc {} }
                    // Los oídos (retornos de los invitados, escucha en vivo) no esperan la latencia del estudio:
                    // lo que llega más tarde que su espera fija no se oye ahí.
                    Text {
                        readonly property var fuera: (latPanel.lat.oido && latPanel.lat.oido.no_entran) || []
                        visible: fuera.length > 0
                        Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.live; font.pixelSize: 10
                        text: qsTr("No llegan a tiempo a los retornos ni a la escucha en vivo (esperan %1 ms): %2")
                              .arg(Math.round((latPanel.lat.oido && latPanel.lat.oido.latencia_ms) || 0)).arg(fuera.join(", "))
                    }
                    Text {
                        visible: latPanel.sinSenal > 0
                        text: qsTr("%n entrada más sin video ni señal ahora (audio, invitados sin conectar): no marcan la latencia", "", latPanel.sinSenal)
                        color: Theme.textFaint; font.pixelSize: 10
                    }
                    // --- Ajustes ---
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8
                        Text { text: qsTr("Tope"); color: Theme.textDim; font.pixelSize: 12 }
                        SpinBox {
                            from: 0; to: 2000; stepSize: 10; editable: true
                            value: latPanel.lat.tope_ms || 0
                            onValueModified: pop.vg.ponerTopeEstudioUi(value)
                        }
                        GlassButton { compact: true; text: qsTr("Automático"); active: latPanel.lat.automatico === true
                                      onClicked: pop.vg.ponerTopeEstudioUi(-1) }
                        // El modo baja latencia del evento: el tope bajo; lo que lo pasa va con su demora.
                        GlassButton { compact: true; text: qsTr("Baja latencia (%1 ms)").arg(latPanel.lat.baja_latencia_ms || 110)
                                      active: latPanel.lat.baja_latencia === true
                                      onClicked: pop.vg.ponerTopeEstudioUi(latPanel.lat.baja_latencia_ms || 110) }
                        Item { Layout.fillWidth: true }
                        GlassButton { compact: true; text: qsTr("Ver en el mapa"); icon: "⤳"; onClicked: pop.verMapa() }
                    }
                    // En su propia fila, no adentro de la de arriba: un texto que se parte dentro de
                    // una fila le cuesta a la columna una vuelta más de acomodarse, y sin ancho (la
                    // pestaña escondida, el panel a -32) no terminaba nunca («polish loop»).
                    Text {
                        Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.textFaint; font.pixelSize: 10
                        text: qsTr("Una sincronizada que pide más que el tope (una cámara con superresolución pesada) no arrastra a las demás: va con su propia demora.")
                    }
                }
            }

            // --- Reloj ------------------------------------------------------------------
            Panel {
                Layout.fillWidth: true
                padding: 12
                implicitHeight: relojCol.implicitHeight + 2 * padding
                ColumnLayout {
                    id: relojCol
                    anchors.fill: parent
                    spacing: 8
                    Text { text: qsTr("Reloj del estudio"); color: Theme.text; font { pixelSize: 14; weight: Font.DemiBold } }
                    Text {
                        Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.textDim; font.pixelSize: 11
                        text: qsTr("Las entradas de red presentan cada cuadro a su hora de captura + el colchón. Con el mismo reloj en todos los aparatos " +
                                   "(NTP, PTP o el de uno publicado) las cámaras RTSP quedan alineadas solas; SRT y RTMP se alinean midiendo (abajo).")
                    }
                    RowLayout {
                        spacing: 8
                        ComboBox {
                            id: tipoReloj
                            Layout.preferredWidth: 220
                            model: [qsTr("Sistema (este aparato)"), "NTP", "PTP (IEEE 1588)", qsTr("El de otro aparato")]
                        }
                        TextField {
                            id: servidor
                            visible: tipoReloj.currentIndex === 1 || tipoReloj.currentIndex === 3
                            Layout.preferredWidth: 200
                            placeholderText: tipoReloj.currentIndex === 1 ? "pool.ntp.org" : qsTr("IP del otro aparato")
                            text: pop.reloj.servidor || ""
                        }
                        SpinBox {
                            id: puertoReloj
                            visible: tipoReloj.currentIndex === 3
                            from: 1; to: 65535; editable: true
                            value: pop.reloj.puerto || 8554
                        }
                        SpinBox {
                            id: dominio
                            visible: tipoReloj.currentIndex === 2
                            from: 0; to: 127; editable: true
                            value: pop.reloj.dominio || 0
                        }
                        Text {
                            text: qsTr("Colchón de las entradas de red"); color: Theme.textDim; font.pixelSize: 12
                            HoverHandler { id: colHov }
                            ToolTip.visible: colHov.hovered; ToolTip.delay: 400
                            ToolTip.text: qsTr("Lo que espera cada cámara o entrada de red (RTSP, SRT…) para absorber los saltos de la red. No es la latencia del estudio: con una entrada de red, el estudio se atrasa al menos esto para que todo salga alineado.")
                        }
                        SpinBox { id: latencia; from: 0; to: 5000; stepSize: 50; editable: true; value: pop.reloj.latencia_ms || 200 }
                        Text { text: "ms"; color: Theme.textDim; font.pixelSize: 12 }
                        GlassButton {
                            compact: true; text: qsTr("Aplicar"); active: true
                            onClicked: {
                                const tipo = ["sistema", "ntp", "ptp", "red"][tipoReloj.currentIndex]
                                const c = { tipo: tipo, latencia_ms: latencia.value }
                                if (tipo === "ntp" || tipo === "red") c.servidor = servidor.text
                                if (tipo === "red") c.puerto = puertoReloj.value
                                if (tipo === "ptp") c.dominio = dominio.value
                                pop.avisar(pop.vg.configurarRelojUi(c), qsTr("Reloj del estudio: %1").arg(tipo))
                            }
                        }
                    }
                    RowLayout {
                        spacing: 8
                        Toggle {
                            checked: (pop.reloj.publicar_puerto || 0) > 0
                            onToggled: (w) => pop.avisar(pop.vg.configurarRelojUi({ publicar_puerto: w ? publicar.value : 0 }),
                                                         w ? qsTr("Publicando el reloj en el puerto %1").arg(publicar.value) : qsTr("Ya no se publica el reloj"))
                        }
                        Text { text: qsTr("Publicar el reloj de este aparato (para que otros lo usen) en el puerto"); color: Theme.textDim; font.pixelSize: 12 }
                        SpinBox { id: publicar; from: 1; to: 65535; editable: true; value: (pop.reloj.publicar_puerto || 0) > 0 ? pop.reloj.publicar_puerto : 8554 }
                    }
                }
            }

            // --- Entradas de red --------------------------------------------------------
            Panel {
                Layout.fillWidth: true
                padding: 12
                implicitHeight: redCol.implicitHeight + 2 * padding
                ColumnLayout {
                    id: redCol
                    anchors.fill: parent
                    spacing: 6
                    Text { text: qsTr("Entradas de red y de plugins"); color: Theme.text; font { pixelSize: 14; weight: Font.DemiBold } }
                    Text {
                        visible: pop.red.length === 0
                        text: qsTr("No hay. Agregá una abajo, o buscá cámaras en la red (Entradas → «Buscar cámaras en la red…»).")
                        color: Theme.textFaint; font.pixelSize: 11
                    }
                    Repeater {
                        model: pop.red
                        delegate: ColumnLayout {
                            id: fila
                            required property var modelData
                            readonly property var g: modelData.gstreamer || {}
                            Layout.fillWidth: true
                            spacing: 2
                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 10
                                Text { text: fila.modelData.entrada; color: Theme.text; font { pixelSize: 13; weight: Font.DemiBold } Layout.preferredWidth: 160; elide: Text.ElideRight }
                                StatusPill {
                                    text: fila.g.error ? qsTr("sin señal") : (fila.modelData.ancho > 0 ? fila.modelData.ancho + "x" + fila.modelData.alto + " · " + Number((fila.g.origen || {}).fps || 0).toFixed(1) + " fps" : qsTr("conectando…"))
                                    dot: fila.g.error ? Theme.live : fila.modelData.ancho > 0 ? Theme.ok : Theme.amber
                                    medidaDe: "1920x1080 · 30.0 fps"
                                }
                                Text {
                                    Layout.fillWidth: true
                                    text: qsTr("colchón %1 ms · desfase %2 ms · tarda %3–%4 ms").arg(fila.g.latencia_ms || 0).arg(fila.g.desfase_ms || 0)
                                              .arg(Math.round(fila.g.edad_p50_ms || 0)).arg(Math.round(fila.g.edad_p95_ms || 0)) +
                                          (fila.g.ntp_sync ? " · " + qsTr("con su hora") : "") + (fila.g.llega_tarde ? " · " + qsTr("llega tarde") : "") +
                                          (fila.g.normalizado ? " · " + qsTr("normalizada") : "") + (fila.g.superres ? " · " + qsTr("superres x2") : "")
                                    color: Theme.textDim; font.pixelSize: 11; elide: Text.ElideRight
                                }
                                SpinBox {
                                    id: desf
                                    from: -5000; to: 5000; stepSize: 10; editable: true
                                    value: fila.g.desfase_ms || 0
                                    ToolTip.visible: hovered; ToolTip.delay: 400; ToolTip.text: qsTr("Desfase (ms): positivo la atrasa")
                                }
                                GlassButton { compact: true; text: qsTr("Desfasar"); onClicked: pop.avisar(pop.vg.desfaseUi(fila.modelData.entrada, desf.value), qsTr("Desfase aplicado")) }
                                // Cómo sale a horario: el colchón propio o el appsink de GStreamer
                                // sincronizado con la latencia del estudio (audio y video juntos en los dos).
                                ComboBox {
                                    implicitWidth: 130
                                    model: [ { t: qsTr("Colchón"), v: "colchon" }, { t: "GStreamer", v: "gstreamer" } ]
                                    textRole: "t"
                                    currentIndex: fila.g.sincronia === "gstreamer" ? 1 : 0
                                    onActivated: (i) => pop.avisar(pop.vg.sincroniaUi(fila.modelData.entrada, model[i].v), qsTr("Sincronía: %1").arg(model[i].t))
                                    ToolTip.visible: hovered; ToolTip.delay: 400
                                    ToolTip.text: qsTr("Colchón: cola propia con la hora de captura (menos latencia con cámaras de hora inestable). " +
                                                       "GStreamer: appsink al reloj con la latencia del estudio") +
                                                  (fila.g.sincronia === "gstreamer" ? " " + qsTr("(ahora %1 ms)").arg(fila.g.latencia_estudio_ms || 0) : "")
                                }
                                // Superresolución x2 (macOS 26): para cámaras chicas (el tope lo pone el sistema).
                                Text { visible: pop.vg.haySuperres; text: qsTr("Superres x2"); color: Theme.textDim; font.pixelSize: 11 }
                                Toggle {
                                    visible: pop.vg.haySuperres
                                    checked: fila.g.superres === true
                                    onToggled: (w) => pop.avisar(pop.vg.superresUi(fila.modelData.entrada, w), w ? qsTr("Superresolución prendida") : qsTr("Superresolución apagada"))
                                    ToolTip.visible: hov.hovered; ToolTip.delay: 400
                                    ToolTip.text: qsTr("Duplica la resolución con la superresolución de Apple (para cámaras chicas: hasta el tamaño máximo que acepte el sistema, hoy 960x960; las más grandes pasan sin tocar)") +
                                                  (fila.g.superres_ms > 0 ? ". " + qsTr("Ahora: %1 ms por cuadro").arg(Number(fila.g.superres_ms).toFixed(1)) : "")
                                    HoverHandler { id: hov }
                                }
                            }
                            // Lo que corre en su cadena de GStreamer (las tarjetas del grafo y el audio).
                            Text {
                                visible: (fila.g.filtros || []).length > 0
                                Layout.fillWidth: true
                                Layout.leftMargin: 170
                                text: qsTr("En GStreamer: %1").arg((fila.g.filtros || []).map(f => (f.audio ? "🔊 " : "") + f.elemento +
                                          (f.ms > 0 ? " " + Number(f.ms).toFixed(0) + " ms" : "") + (f.andando === false ? " " + qsTr("(no está)") : "")).join(" → "))
                                color: Theme.textFaint; font.pixelSize: 11; wrapMode: Text.WordWrap
                            }
                            // Los filtros de audio de la entrada: tocar uno lo edita; ‹ › lo mueve; ✕ lo saca.
                            Flow {
                                id: filaAudio
                                Layout.fillWidth: true
                                Layout.leftMargin: 170
                                spacing: 6
                                readonly property var lista: pop.vg.filtrosAudio[fila.modelData.entrada] || []
                                function mover(i, d) {
                                    const l = lista.slice(), j = i + d
                                    if (j < 0 || j >= l.length) return
                                    const x = l[i]; l[i] = l[j]; l[j] = x
                                    pop.avisar(pop.vg.ponerFiltrosAudioUi(fila.modelData.entrada, l), qsTr("Orden cambiado"))
                                }
                                Text { text: qsTr("Audio:"); color: Theme.textDim; font.pixelSize: 11; height: 26; verticalAlignment: Text.AlignVCenter }
                                Repeater {
                                    model: filaAudio.lista
                                    delegate: Row {
                                        id: chip
                                        required property var modelData
                                        required property int index
                                        spacing: 1
                                        GlassButton {
                                            compact: true; text: "‹"; visible: chip.index > 0
                                            onClicked: filaAudio.mover(chip.index, -1)
                                        }
                                        GlassButton {
                                            compact: true
                                            text: chip.modelData.elemento === "baplugin"
                                                  ? "🐍 " + (chip.modelData.propiedades.plugin || "") + " · " + (chip.modelData.propiedades.tarjeta || "")
                                                  : chip.modelData.elemento
                                            ToolTip.visible: hovered; ToolTip.delay: 400; ToolTip.text: qsTr("Editar este filtro")
                                            onClicked: editorAudio.abrirPara(fila.modelData.entrada, filaAudio, chip.index)
                                        }
                                        GlassButton {
                                            compact: true; text: "›"; visible: chip.index < filaAudio.lista.length - 1
                                            onClicked: filaAudio.mover(chip.index, 1)
                                        }
                                        GlassButton {
                                            compact: true; text: "✕"
                                            ToolTip.visible: hovered; ToolTip.delay: 400; ToolTip.text: qsTr("Sacar este filtro")
                                            onClicked: pop.avisar(pop.vg.quitarFiltroAudioUi(fila.modelData.entrada, chip.index), qsTr("Filtro sacado"))
                                        }
                                    }
                                }
                                GlassButton {
                                    compact: true; text: qsTr("+ filtro…")
                                    ToolTip.visible: hovered; ToolTip.delay: 400
                                    ToolTip.text: qsTr("Un módulo de audio de GStreamer o una tarjeta de audio de un plugin de Python")
                                    onClicked: editorAudio.abrirPara(fila.modelData.entrada, filaAudio, -1)
                                }
                                ComboBox {
                                    model: [ { t: qsTr("Rápidos…"), v: "" }, { t: qsTr("Compresor"), v: "compresor" }, { t: qsTr("Pasa altos (80 Hz)"), v: "pasa_altos" },
                                             { t: qsTr("Eco"), v: "eco" }, { t: qsTr("Volumen x1,5"), v: "volumen" }, { t: qsTr("Automixer (mesa)"), v: "automixer" } ]
                                    textRole: "t"
                                    implicitWidth: 140
                                    onActivated: (i) => {
                                        const v = model[i].v
                                        if (v) pop.avisar(pop.vg.agregarFiltroAudioUi(fila.modelData.entrada, v), qsTr("Filtro agregado (la demora se compensa sola)"))
                                        currentIndex = 0
                                    }
                                }
                            }
                            Repeater {
                                model: fila.modelData.diagnostico || []
                                delegate: Text {
                                    required property var modelData
                                    Layout.fillWidth: true
                                    Layout.leftMargin: 170
                                    text: "• " + modelData.problema + (modelData.arreglo ? "  →  " + modelData.arreglo : "")
                                    color: pop.colorGravedad(modelData.gravedad); font.pixelSize: 11; wrapMode: Text.WordWrap
                                }
                            }
                        }
                    }
                }
            }

            // --- Medir sincronía --------------------------------------------------------
            Panel {
                Layout.fillWidth: true
                padding: 12
                implicitHeight: sincCol.implicitHeight + 2 * padding
                ColumnLayout {
                    id: sincCol
                    anchors.fill: parent
                    spacing: 8
                    Text { text: qsTr("Comprobar dos cámaras"); color: Theme.text; font { pixelSize: 14; weight: Font.DemiBold } }
                    Text {
                        Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.textDim; font.pixelSize: 11
                        text: qsTr("Para saber si dos cámaras que toman lo mismo salen alineadas: poné frente a las dos algo que cambie a la vez (un reloj " +
                                   "con milisegundos en un teléfono, un aplauso, un flash), elegilas y «Medir». Tiene que dar menos de un cuadro. " +
                                   "«Medir y corregir» atrasa la que va adelantada y vuelve a medir.")
                    }
                    RowLayout {
                        spacing: 8
                        ComboBox { id: entA; Layout.preferredWidth: 220; model: pop.entradasTodas(); displayText: currentIndex < 0 ? qsTr("Referencia…") : currentText; currentIndex: -1 }
                        Text { text: qsTr("contra"); color: Theme.textDim; font.pixelSize: 12 }
                        ComboBox { id: entB; Layout.preferredWidth: 220; model: pop.entradasTodas(); displayText: currentIndex < 0 ? qsTr("La otra…") : currentText; currentIndex: -1 }
                        GlassButton {
                            compact: true; text: pop.midiendo ? qsTr("Midiendo…") : qsTr("Medir")
                            enabled: !pop.midiendo && entA.currentIndex >= 0 && entB.currentIndex >= 0 && entA.currentIndex !== entB.currentIndex
                            onClicked: { pop.midiendo = true; pop.medicionTexto = ""; pop.vg.medirUi(entA.currentText, entB.currentText, false) }
                        }
                        GlassButton {
                            compact: true; text: qsTr("Medir y corregir"); active: true
                            enabled: !pop.midiendo && entA.currentIndex >= 0 && entB.currentIndex >= 0 && entA.currentIndex !== entB.currentIndex
                            onClicked: { pop.midiendo = true; pop.medicionTexto = ""; pop.vg.medirUi(entA.currentText, entB.currentText, true) }
                        }
                    }
                    Text {
                        visible: pop.medicionTexto !== ""
                        Layout.fillWidth: true
                        text: pop.medicionTexto
                        color: pop.medicionTexto.startsWith("✕") ? Theme.live : Theme.text
                        font.pixelSize: 12; wrapMode: Text.WordWrap
                    }
                }
            }

            // --- Audio de cada canal (sincro labios) -------------------------------------
            Panel {
                Layout.fillWidth: true
                padding: 12
                implicitHeight: audioSincCol.implicitHeight + 2 * padding
                ColumnLayout {
                    id: audioSincCol
                    anchors.fill: parent
                    spacing: 6
                    Text { text: qsTr("Audio de cada canal con su video"); color: Theme.text; font { pixelSize: 14; weight: Font.DemiBold } }
                    Text {
                        Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.textDim; font.pixelSize: 11
                        text: qsTr("El retardo de cada audio respecto de su imagen (sincro labios). Positivo atrasa el audio: lo normal cuando la cámara " +
                                   "llega tarde. Se ajusta a mano o midiendo con aplausos; es el mismo botón SINC del Mezclador.")
                    }
                    Repeater {
                        model: Studio.mixer
                        delegate: RowLayout {
                            id: filaSinc
                            required property string name
                            required property int retardoMs
                            readonly property int ms: retardoMs
                            Layout.fillWidth: true
                            spacing: 8
                            Text { Layout.fillWidth: true; text: filaSinc.name; color: Theme.text; font.pixelSize: 12; elide: Text.ElideRight }
                            Text { text: (filaSinc.ms > 0 ? "+" : "") + filaSinc.ms + " ms"
                                   color: filaSinc.ms !== 0 ? Theme.accent : Theme.textFaint; font.pixelSize: 12 }
                            GlassButton { compact: true; text: qsTr("Sincronizar…"); onClicked: retardoPop.abrir(filaSinc.name) }
                        }
                    }
                }
            }

            // --- Entrada de red nueva ---------------------------------------------------
            Panel {
                Layout.fillWidth: true
                padding: 12
                implicitHeight: nuevaCol.implicitHeight + 2 * padding
                ColumnLayout {
                    id: nuevaCol
                    anchors.fill: parent
                    spacing: 8
                    Text { text: qsTr("Entrada de red nueva"); color: Theme.text; font { pixelSize: 14; weight: Font.DemiBold } }
                    Text {
                        Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.textDim; font.pixelSize: 11
                        text: qsTr("SRT (srt://:9000?mode=listener para recibir), RTMP, RTSP, UDP o HTTP. Entra sincronizada con el reloj del estudio. " +
                                   "«Normalizar» la deja como las demás (desentrelaza, píxel cuadrado, los cuadros del estudio, color BT.709).")
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8
                        TextField { id: nNombre; Layout.preferredWidth: 160; placeholderText: qsTr("Nombre") }
                        TextField { id: nUri; Layout.fillWidth: true; placeholderText: "srt://… / rtmp://… / rtsp://…" }
                        ComboBox { id: nEscena; Layout.preferredWidth: 180; editable: true; model: pop.vg ? pop.vg.escenas : []; editText: "" }
                        SpinBox { id: nLat; from: 0; to: 5000; stepSize: 50; editable: true; value: pop.reloj.latencia_ms || 200 }
                        CheckBox { id: nNorm; text: qsTr("Normalizar"); checked: true }
                        GlassButton {
                            compact: true; text: qsTr("Agregar"); active: true
                            enabled: nNombre.text.trim() !== "" && nUri.text.trim() !== "" && nEscena.editText.trim() !== ""
                            onClicked: {
                                const e = pop.vg.nuevaEntradaRed(nNombre.text, nUri.text, nEscena.editText, nLat.value, nNorm.checked)
                                pop.avisar(e, qsTr("Entrada «%1» en «%2»").arg(nNombre.text).arg(nEscena.editText))
                                if (e === "") { nNombre.text = ""; nUri.text = "" }
                            }
                        }
                    }
                }
            }

            // --- Lo que dice el diagnóstico de las demás ---------------------------------
            Panel {
                Layout.fillWidth: true
                visible: pop.diag.length > 0
                padding: 12
                implicitHeight: diagCol.implicitHeight + 2 * padding
                ColumnLayout {
                    id: diagCol
                    anchors.fill: parent
                    spacing: 4
                    Text { text: qsTr("Diagnóstico de todas las entradas"); color: Theme.text; font { pixelSize: 14; weight: Font.DemiBold } }
                    Repeater {
                        model: pop.diag
                        delegate: ColumnLayout {
                            id: dfila
                            required property var modelData
                            Layout.fillWidth: true
                            spacing: 1
                            Text { text: dfila.modelData.entrada; color: Theme.text; font { pixelSize: 12; weight: Font.DemiBold } }
                            Repeater {
                                model: dfila.modelData.diagnostico
                                delegate: Text {
                                    required property var modelData
                                    Layout.fillWidth: true
                                    Layout.leftMargin: 12
                                    text: "• " + modelData.problema + (modelData.arreglo ? "  →  " + modelData.arreglo : "")
                                    color: pop.colorGravedad(modelData.gravedad); font.pixelSize: 11; wrapMode: Text.WordWrap
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    // El editor de un filtro de audio (agregar o editar).
    EditorFiltroAudio {
        id: editorAudio
        vg: pop.vg
        property var filaQueAbrio: null
        function abrirPara(entrada, fila, i) {
            filaQueAbrio = fila
            editorAudio.entrada = entrada
            abrir(fila.lista, i)
        }
    }

    // Sincronizar un canal de audio con su video (el mismo de la tarjeta y la ventana del Mezclador).
    RetardoCanal { id: retardoPop }
}
