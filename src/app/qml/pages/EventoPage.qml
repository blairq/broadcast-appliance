// Guion del evento (storyboard): el rundown a la izquierda y la vista de
// conduccion a la derecha. Solo presentacion: todo sale de Studio.evento.
//
// La idea es que durante el evento se mire la derecha (que cue va, cual sigue,
// cuanto nos desviamos) y que la izquierda sea para armarlo antes.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import BA
import "../controls"

Rectangle {
    id: page
    color: Theme.bg
    property real leftInset: 92
    readonly property var ev: Studio.evento

    // Cue seleccionado para editar (bloque, cue). -1 = ninguno.
    property var informe: ({})
    property int selB: -1
    property int selC: -1
    readonly property var selCue: {
        const bs = ev.guion.bloques || []
        if (selB < 0 || selB >= bs.length) return null
        const cs = bs[selB].cues || []
        return (selC < 0 || selC >= cs.length) ? null : cs[selC]
    }

    // --- La vista de armado -------------------------------------------------
    /// Si se está mostrando la vista de armar contándolo.
    property bool armando: false
    /// La corrida del agente Organizador, mientras dura.
    property string corridaId: ""
    readonly property var corrida: corridaId === "" ? ({})
                                                    : Studio.despachador.corridaQml(corridaId)
    readonly property bool corriendo: corridaId !== "" &&
                                      (corrida.estado === "esperando" || corrida.estado === "corriendo")

    /// Le encarga el evento al rol `organizador`.
    ///
    /// NO se lo manda al asistente general: es un rol con su propia doctrina y
    /// con SOLO las herramientas del evento, así que no puede sacar nada al
    /// aire ni tocar el audio aunque el texto se lo pida. Esa contención es la
    /// mitad de por qué existe el rol.
    function armar(texto) {
        const r = Studio.despachador.encargarDesdeUiConId("organizador", texto)
        if (r.error !== "") { ev.notify(r.error, "error"); return }
        corridaId = r.id
    }

    function descartarLoArmado() {
        const cual = ev.abierto
        if (cual === "") return
        ApplicationWindow.window.confirm({
            title: qsTr("Descartar el evento armado"),
            text: qsTr("Se borra «%1» con su guion y su padrón. No se puede deshacer.").arg(cual),
            accept: qsTr("Descartar"), reject: qsTr("Dejarlo"),
            // borrar cierra el abierto sin guardarlo antes de tirarlo
            action: () => { ev.borrar(cual); corridaId = ""; armando = false }
        })
    }

    /// Le pide algo al asistente. Los botones de acta, capítulos e
    /// invitaciones son de una sola vez y su resultado es TEXTO para leer o
    /// pegar, así que no vale la pena una vista propia: se lo pide y contesta
    /// donde ya se lee, que es la barra del asistente.
    function pedirAsistente(texto) {
        ApplicationWindow.window.pedirAlAsistente(texto)
    }

    function bytesLegibles(b) {
        if (b === undefined || b === null) return ""
        return b > 1e9 ? (b / 1e9).toFixed(1) + " GB" : b > 1e6 ? (b / 1e6).toFixed(1) + " MB" : Math.round(b / 1e3) + " kB"
    }
    function mmss(s) {
        const neg = s < 0
        const t = Math.abs(Math.round(s))
        return (neg ? "-" : "") + Math.floor(t / 60) + ":" + String(t % 60).padStart(2, "0")
    }
    // La hora prevista de un cue: la de arranque del evento mas su offset.
    /// El "cuando" para el boton del encabezado: corto, y diciendo lo que
    /// falta cuando falta.
    function cuandoCorto() {
        const f = ev.guion.fecha || ""
        const h = ev.guion.inicio || ""
        if (f === "" && h === "") return qsTr("Sin fecha")
        if (f === "") return qsTr("Sin fecha · %1").arg(h)
        if (h === "") return qsTr("%1 · sin hora").arg(f)
        return f + " · " + h
    }

    function colorDeTipo(tipo) {
        switch (tipo) {
        case "escena": return Theme.accent
        case "orador": return "#5aa9e6"
        case "grafico": return "#c792ea"
        case "musica": return "#b98cf0"
        case "video": return "#b98cf0"
        case "teleprompter": return "#4fc3a1"
        case "pantalla": return "#4fc3a1"
        case "audio": return Theme.amber
        case "espera": return Theme.live
        default: return Theme.textFaint
        }
    }

    ColumnLayout {
        anchors { fill: parent; leftMargin: page.leftInset; margins: 20 }
        spacing: 14

        // --- Encabezado: que evento esta abierto -----------------------------
        Panel {
            Layout.fillWidth: true
            Layout.preferredHeight: 76
            RowLayout {
                anchors.fill: parent
                spacing: 12

                // El evento abierto (con todo lo suyo: escenas, mezclador, medios, asamblea,
                // web). Uno nuevo o abrir otro: cada uno tiene lo suyo, nunca se pisan.
                // Lo que se achica cuando falta lugar (el título, el selector, el conteo):
                // así «Guardar» nunca queda afuera de la ventana.
                ColumnLayout {
                    spacing: 0
                    Layout.fillWidth: true
                    Layout.minimumWidth: 90
                    Layout.maximumWidth: 260
                    Text { text: qsTr("EVENTO"); color: Theme.textFaint; font { pixelSize: 10; weight: Font.DemiBold; letterSpacing: 1 } }
                    Text {
                        text: Studio.configuraciones ? Studio.configuraciones.activa : ""
                        color: Theme.text; font { pixelSize: 16; weight: Font.DemiBold }
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                    }
                }
                GlassButton {
                    icon: "+"; text: qsTr("Evento nuevo…")
                    onClicked: nuevoEventoDlg.open()
                    ToolTip.visible: hovered; ToolTip.delay: 400
                    ToolTip.text: qsTr("Un evento aparte, con sus propias escenas, mezclador, medios, asamblea y web: desde cero, copiando este, desde uno anterior o desde una plantilla")
                }
                GlassButton {
                    text: qsTr("Abrir otro…")
                    onClicked: abrirEventoDlg.open()
                    ToolTip.visible: hovered; ToolTip.delay: 400
                    ToolTip.text: qsTr("Pasar a otro evento (reinicia la app con todo lo suyo)")
                }
                GlassButton {
                    compact: true; text: qsTr("Plantillas…")
                    onClicked: plantillasDlg.open()
                    ToolTip.visible: hovered; ToolTip.delay: 400
                    ToolTip.text: qsTr("Las plantillas de evento: orden, nombre, visibilidad, editarlas, guardar este evento como plantilla")
                }
                Rectangle { width: 1; Layout.fillHeight: true; Layout.margins: 8; color: Qt.rgba(1, 1, 1, 0.1) }
                ComboBox {
                    id: selector
                    Layout.fillWidth: true
                    Layout.minimumWidth: 130
                    Layout.maximumWidth: 220
                    model: page.ev.eventos
                    textRole: "nombre"
                    displayText: page.ev.abierto === "" ? qsTr("Elegí un evento…") : (page.ev.guion.nombre || page.ev.abierto)
                    onActivated: (i) => page.ev.abrir(page.ev.eventos[i].carpeta)
                    Component.onCompleted: page.ev.refrescar()
                    // Con su carpeta adelante: con el histórico hay muchos y se agrupan.
                    delegate: ItemDelegate {
                        required property var modelData
                        required property int index
                        width: selector.width
                        text: (modelData.grupo ? modelData.grupo + " / " : "") + modelData.nombre +
                              (modelData.enCurso ? qsTr("  ● en curso") : "")
                        highlighted: selector.highlightedIndex === index
                    }
                }
                GlassButton {
                    icon: "+"; text: qsTr("Guion"); compact: true
                    onClicked: nuevoDlg.open()
                    ToolTip.visible: hovered; ToolTip.delay: 400
                    ToolTip.text: qsTr("Otro guion dentro de este mismo evento (comparte escenas y medios). Para un evento aparte: «Evento nuevo…»")
                }
                // Armar contándolo, en vez de llenando un formulario. No es un
                // motor aparte: es el asistente que ya existe, usando las
                // herramientas del evento (ver `evento_armar`).
                GlassButton {
                    icon: "✦"; text: qsTr("Armar contándolo")
                    active: page.armando
                    onClicked: { page.armando = true; page.corridaId = ""; }
                    ToolTip.visible: hovered
                    ToolTip.delay: 400
                    ToolTip.text: qsTr("Contale al asistente cómo va a ser el evento y lo deja escrito")
                }
                Item { Layout.fillWidth: true }

                Text {
                    visible: page.ev.abierto !== ""
                    text: qsTr("%1 cues · %2").arg(page.ev.guion.cues).arg(page.mmss(page.ev.guion.duracionSeg))
                    color: Theme.textDim
                    font.pixelSize: 12
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    Layout.maximumWidth: implicitWidth
                }
                // Cuando es el evento. Sin fecha no se lo puede convocar ni
                // archivar, asi que cuando falta se ve que falta en vez de no
                // mostrar nada.
                GlassButton {
                    visible: page.ev.abierto !== ""
                    compact: true
                    text: page.cuandoCorto()
                    active: page.ev.guion.fecha === ""
                    onClicked: cuandoDlg.open()
                    ToolTip.visible: hovered
                    ToolTip.delay: 400
                    ToolTip.text: page.ev.guion.cuando !== ""
                                  ? qsTr("Arranca %1").arg(page.ev.guion.cuando)
                                  : qsTr("Poner el dia y la hora del evento")
                }
                Text {
                    visible: page.ev.sucio
                    text: qsTr("sin guardar")
                    color: Theme.amber
                    font.pixelSize: 12
                }
                GlassButton {
                    visible: page.ev.abierto !== ""
                    text: qsTr("Guardar"); compact: true
                    active: page.ev.sucio
                    onClicked: page.ev.guardar()
                }
            }
        }

        // --- Copiando un evento (en otro hilo): cuánto va --------------------------
        Panel {
            id: panelCopia
            readonly property var c: (Studio.configuraciones && Studio.configuraciones.copia) || ({})
            visible: !!c.nombre
            Layout.fillWidth: true
            Layout.preferredHeight: 44
            RowLayout {
                anchors.fill: parent
                spacing: 10
                BusyIndicator { running: panelCopia.visible; Layout.preferredWidth: 22; Layout.preferredHeight: 22 }
                Text {
                    Layout.fillWidth: true
                    readonly property var c: panelCopia.c
                    color: Theme.text; font.pixelSize: 13
                    text: c.total > 0
                          ? qsTr("Copiando «%1»… %2 % (%3 de %4)").arg(c.nombre || "")
                                .arg(Math.min(100, Math.round(100 * c.hechos / c.total)))
                                .arg(page.bytesLegibles(c.hechos)).arg(page.bytesLegibles(c.total))
                          : qsTr("Copiando «%1»… %2").arg(c.nombre || "").arg(page.bytesLegibles(c.hechos))
                }
            }
        }

        // --- El evento EN CURSO: el que recibe todo lo que pasa (histórico) ------
        Panel {
            Layout.fillWidth: true
            Layout.preferredHeight: 58
            RowLayout {
                anchors.fill: parent
                spacing: 10
                Rectangle {
                    width: 10; height: 10; radius: 5
                    color: page.ev.enCurso.carpeta ? Theme.ok : Theme.textDim
                }
                Text {
                    Layout.fillWidth: true
                    elide: Text.ElideRight
                    color: Theme.text
                    font.pixelSize: 13
                    text: page.ev.enCurso.carpeta
                          ? qsTr("En curso: %1").arg(page.ev.enCurso.nombre) +
                            (page.ev.enCurso.grupo ? "  ·  " + page.ev.enCurso.grupo : "") +
                            (page.ev.enCurso.desdeMs ? qsTr("  ·  desde las %1").arg(Qt.formatTime(new Date(page.ev.enCurso.desdeMs), "HH:mm")) : "") +
                            (page.ev.enCurso.automatico ? qsTr("  (la jornada del día: se creó sola)") : "")
                          : qsTr("Ningún evento en curso: lo que pase no queda en ningún evento")
                }
                GlassButton {
                    compact: true
                    visible: page.ev.abierto !== "" && page.ev.enCurso.carpeta !== page.ev.abierto
                    text: qsTr("Poner en curso este")
                    onClicked: page.ev.iniciarEvento(page.ev.abierto)
                    ToolTip.visible: hovered
                    ToolTip.delay: 400
                    ToolTip.text: qsTr("Desde ahora, grabaciones, vivos y registro van a «%1». Correr el guion de verdad lo hace solo.")
                                  .arg(page.ev.guion.nombre || page.ev.abierto)
                }
                GlassButton {
                    compact: true
                    visible: !!page.ev.enCurso.carpeta
                    text: qsTr("Terminar")
                    onClicked: page.ev.terminarEvento()
                    ToolTip.visible: hovered
                    ToolTip.delay: 400
                    ToolTip.text: page.ev.eventoAutomatico
                                  ? qsTr("Queda en el histórico como terminado; lo que siga va a la jornada del día")
                                  : qsTr("Queda en el histórico como terminado")
                }
                GlassButton {
                    compact: true
                    visible: page.ev.abierto !== ""
                    text: qsTr("Carpeta…")
                    onClicked: moverDlg.open()
                    ToolTip.visible: hovered
                    ToolTip.delay: 400
                    ToolTip.text: qsTr("Llevar el evento abierto a otra carpeta (2026/Octubre, Consejo/Ordinarias…)")
                }
                CheckBox {
                    text: qsTr("Jornada automática")
                    checked: page.ev.eventoAutomatico
                    onToggled: page.ev.eventoAutomatico = checked
                    ToolTip.visible: hovered
                    ToolTip.delay: 400
                    ToolTip.text: qsTr("Si no hay ningún evento en curso, se crea solo el del día: nada queda sin evento")
                }
            }
        }

            // --- La vista de armado ------------------------------------------------
        // Tapa el resto de la página mientras dura: armar un evento es una cosa
        // por vez, y tener el guion viejo al lado invita a confundir cuál es
        // cuál. No es un modal porque el armado TARDA —el agente piensa y llama
        // herramientas— y un modal que no se puede cerrar mientras tanto es una
        // pantalla trabada.
        Panel {
            visible: page.armando
            Layout.fillWidth: true
            Layout.fillHeight: true
            title: qsTr("Armar el evento contándolo")

            ColumnLayout {
                anchors.fill: parent
                spacing: 10

                Text {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    text: qsTr("Contá cómo va a ser: qué pasa, en qué orden, cuánto dura, quién habla y " +
                          "quién viene. Lo arma el agente Organizador, que lo deja escrito y " +
                          "revisado. No sale nada al aire.")
                    color: Theme.textDim
                    font.pixelSize: 12
                }

                TextArea {
                    id: contalo
                    Layout.fillWidth: true
                    Layout.preferredHeight: 120
                    enabled: !page.corriendo
                    wrapMode: TextArea.Wrap
                    color: Theme.text
                    placeholderText: qsTr("Asamblea del consorcio, el jueves 8 de octubre a las 19, dos " +
                                     "horas, presencial en el salón y por videollamada. Hay que " +
                                     "votar tres puntos: balance, expensas y el portón. Son 40 " +
                                     "propietarios y todos votan.")
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8
                    GlassButton {
                        text: page.corriendo ? qsTr("Armando…") : qsTr("Armar")
                        enabled: !page.corriendo && contalo.text.trim() !== ""
                        onClicked: page.armar(contalo.text.trim())
                    }
                    GlassButton {
                        text: page.corriendo ? qsTr("Detener") : qsTr("Cerrar")
                        onClicked: {
                            if (page.corriendo) Studio.despachador.detener(page.corridaId)
                            else { page.armando = false; page.corridaId = "" }
                        }
                    }
                    Item { Layout.fillWidth: true }
                    Text {
                        visible: page.corriendo
                        text: page.corrida.herramientas !== undefined
                              ? qsTr("%n herramienta · %1 s", "", page.corrida.herramientas).arg(page.corrida.duracionS)
                              : ""
                        color: Theme.textDim
                        font.pixelSize: 11
                    }
                }

                // Lo que contestó el agente. Su resumen es lo que hay que leer:
                // dice qué quedó hecho y qué quedó pendiente.
                Rectangle {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    visible: page.corridaId !== ""
                    radius: Theme.radiusSmall
                    color: Qt.rgba(1, 1, 1, 0.04)
                    border.color: Theme.glassBorder
                    Flickable {
                        anchors.fill: parent
                        anchors.margins: 12
                        contentHeight: salida.implicitHeight
                        clip: true
                        Text {
                            id: salida
                            width: parent.width
                            wrapMode: Text.WordWrap
                            color: page.corrida.error ? Theme.live : Theme.text
                            font { family: Theme.font; pixelSize: 13 }
                            text: page.corrida.error ? page.corrida.error
                                : page.corrida.resumen ? page.corrida.resumen
                                : (page.corriendo ? qsTr("Pensando…") : "")
                        }
                    }
                }

                // Y lo que quedó ESCRITO, que es lo que hay que aceptar o tirar.
                RowLayout {
                    Layout.fillWidth: true
                    visible: !page.corriendo && page.corridaId !== "" && page.ev.abierto !== ""
                    spacing: 8
                    Text {
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        color: Theme.textDim
                        font.pixelSize: 12
                        text: qsTr("Quedó abierto «%1»: %n bloque, %2 cues, %3.", "", page.ev.guion.bloques.length)
                                  .arg(page.ev.abierto).arg(page.ev.guion.cues).arg(page.mmss(page.ev.guion.duracionSeg)) +
                              (page.ev.reparos.length > 0
                               ? " " + qsTr("%n cosa para mirar.", "", page.ev.reparos.length)
                               : "")
                    }
                    GlassButton {
                        text: qsTr("Verlo")
                        onClicked: { page.armando = false; page.ev.revisar() }
                    }
                    GlassButton {
                        text: qsTr("Descartarlo")
                        onClicked: page.descartarLoArmado()
                    }
                }
            }
        }

        // --- Sin evento abierto ----------------------------------------------
        Panel {
            visible: page.ev.abierto === "" && !page.armando
            Layout.fillWidth: true
            Layout.fillHeight: true
            ColumnLayout {
                anchors.centerIn: parent
                width: Math.min(parent.width - 60, 560)
                spacing: 10
                Text {
                    Layout.fillWidth: true
                    text: qsTr("Todavía no hay ningún evento abierto")
                    color: Theme.text
                    font { family: Theme.font; pixelSize: 17; weight: Font.DemiBold }
                    horizontalAlignment: Text.AlignHCenter
                }
                Text {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    horizontalAlignment: Text.AlignHCenter
                    color: Theme.textDim
                    font.pixelSize: 13
                    text: qsTr("Un evento es el guion de lo que va a pasar: bloques (apertura, discurso, cierre) " +
                          "y dentro de cada uno los cues concretos. Con los tiempos previstos, durante el " +
                          "evento se ve cuánto nos estamos desviando.")
                }
                Text {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    horizontalAlignment: Text.AlignHCenter
                    color: Theme.textFaint
                    font.pixelSize: 12
                    text: qsTr("Se guarda en su propia carpeta, con su música, sus videos y sus discursos adentro, " +
                          "para poder llevárselo entero a otra máquina.")
                }
            }
        }

        // --- Las pestañas del evento -----------------------------------------
        // Guion es lo de siempre; Padrón y Público son los MISMOS paneles que
        // usa la página de Público (controls/Panel*.qml) leyendo el mismo
        // modelo. Están en los dos lados a propósito: acá mientras se arma el
        // evento, y allá con el aire encendido, donde hay que poder mirar la
        // cola sin perder de vista el guion.
        // ui_abrir_panel {page: "guion", panel: "documentos"…}: la pestaña pedida.
        Connections {
            target: Studio
            function onAbrirPanel(pagina, panel) {
                if (pagina !== "guion") return
                const i = ["guion", "padron", "publico", "orden", "web", "documentos"].indexOf(panel)
                if (i >= 0) pestanas.currentIndex = i
            }
        }
        TabBar {
            id: pestanas
            visible: page.ev.abierto !== "" && !page.armando
            Layout.fillWidth: true
            TabButton { text: qsTr("Guion") }
            TabButton {
                text: page.ev.resumenPadron.total > 0
                      ? qsTr("Padrón (%1/%2)").arg(page.ev.resumenPadron.presentes).arg(page.ev.resumenPadron.total)
                      : qsTr("Padrón")
            }
            TabButton {
                text: {
                    const r = Studio.publico.recuento
                    return r && r.nuevas > 0 ? qsTr("Público (%1)").arg(r.nuevas) : qsTr("Público")
                }
            }
            TabButton { text: qsTr("Orden del día") }
            TabButton { text: qsTr("Página web") }
            TabButton { text: Studio.documentos && Studio.documentos.lista.length ? qsTr("Documentos (%1)").arg(Studio.documentos.lista.length) : qsTr("Documentos") }
        }

        // ---- Documentación del evento (PDF con carátula, por punto y sección) ----------
        Panel {
            visible: page.ev.abierto !== "" && !page.armando && pestanas.currentIndex === 5
            Layout.fillWidth: true
            Layout.fillHeight: true
            title: qsTr("Documentación del evento")
            PanelDocumentos { anchors.fill: parent; bloques: page.ev.guion.bloques || [] }
        }

        // ---- Página web del evento en vivo -----------------------------------------
        Panel {
            visible: page.ev.abierto !== "" && !page.armando && pestanas.currentIndex === 4
            Layout.fillWidth: true
            Layout.fillHeight: true
            title: qsTr("La página web del evento en vivo")
            PanelEventoWeb { anchors.fill: parent; onPedirAsistente: (t) => page.pedirAsistente(t) }
        }

        // ---- Orden del día ---------------------------------------------------
        Panel {
            visible: page.ev.abierto !== "" && !page.armando && pestanas.currentIndex === 3
            Layout.fillWidth: true
            Layout.fillHeight: true
            title: qsTr("Orden del día y uso de la palabra")
            PanelOrdenDelDia { anchors.fill: parent }
        }

        // ---- Padrón ----------------------------------------------------------
        Panel {
            visible: page.ev.abierto !== "" && !page.armando && pestanas.currentIndex === 1
            Layout.fillWidth: true
            Layout.fillHeight: true
            title: qsTr("Quiénes vienen y quiénes llegaron")
            ColumnLayout {
                anchors.fill: parent
                spacing: 10
                PanelPadron {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                }
                // Lo que se hace CON el padrón, que hasta ahora solo estaba en
                // el asistente: la invitación de cada uno, el acta y los
                // capítulos de la grabación.
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8
                    GlassButton {
                        text: qsTr("Acta del evento")
                        icon: "❑"
                        onClicked: page.pedirAsistente(
                            qsTr("Armá el acta de este evento con `evento_acta` y contame qué quedó."))
                        ToolTip.visible: hovered
                        ToolTip.delay: 400
                        ToolTip.text: qsTr("Quiénes asistieron, el quórum y qué se votó")
                    }
                    GlassButton {
                        text: qsTr("Capítulos")
                        icon: "▤"
                        onClicked: page.pedirAsistente(
                            qsTr("Dame los capítulos de la grabación con `evento_capitulos`, listos para " +
                            "pegar en la descripción del video."))
                        ToolTip.visible: hovered
                        ToolTip.delay: 400
                        ToolTip.text: qsTr("Las marcas de tiempo de cada bloque, para la descripción del video")
                    }
                    GlassButton {
                        text: qsTr("Invitaciones")
                        icon: "✉"
                        enabled: page.ev.resumenPadron.total > 0
                        onClicked: page.pedirAsistente(
                            qsTr("Armame las invitaciones de los del padrón con `invitacion`, una por " +
                            "persona, con su enlace y su código."))
                        ToolTip.visible: hovered
                        ToolTip.delay: 400
                        ToolTip.text: qsTr("El enlace personal de cada uno: lo acredita al llegar y le deja el código")
                    }
                    Item { Layout.fillWidth: true }
                }
            }
        }

        // ---- Público ---------------------------------------------------------
        RowLayout {
            visible: page.ev.abierto !== "" && !page.armando && pestanas.currentIndex === 2
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 14
            Panel {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.minimumWidth: 360
                title: qsTr("Preguntas")
                PanelPreguntas { anchors.fill: parent }
            }
            Panel {
                Layout.preferredWidth: 460
                Layout.fillHeight: true
                title: qsTr("Votación")
                PanelVotacion { anchors.fill: parent }
            }
        }

        // --- El guion + la conduccion ----------------------------------------
        RowLayout {
            visible: page.ev.abierto !== "" && !page.armando && pestanas.currentIndex === 0
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 14

            // ---- Rundown -----------------------------------------------------
            Panel {
                Layout.fillWidth: true
                Layout.fillHeight: true
                // Sin minimo, el ancho implicito de la columna de la derecha
                // (que tiene textos largos) se comia todo el guion.
                Layout.minimumWidth: 420
                title: qsTr("Guion")

                ColumnLayout {
                    anchors.fill: parent
                    spacing: 8

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8
                        GlassButton { text: qsTr("+ Bloque"); compact: true; onClicked: page.ev.agregarBloque("") }
                        Item { Layout.fillWidth: true }
                        GlassButton {
                            text: qsTr("Revisar"); compact: true
                            onClicked: page.ev.revisar()
                            ToolTip.visible: hovered
                            ToolTip.text: qsTr("Busca lo que impide correr el guion, sin tocar nada")
                        }
                        GlassButton {
                            text: qsTr("Ensayo rápido"); compact: true
                            onClicked: page.ev.ensayoAcelerado()
                            ToolTip.visible: hovered
                            ToolTip.text: qsTr("Recorre el guion entero de una, sin esperar las duraciones, " +
                                          "para ver si cada cue se podría ejecutar")
                        }
                    }

                    ScrollView {
                        id: rundownScroll
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        contentWidth: availableWidth
                        ScrollBar.horizontal.policy: ScrollBar.AlwaysOff

                        ColumnLayout {
                            width: rundownScroll.availableWidth
                            spacing: 10

                            Repeater {
                                model: page.ev.guion.bloques
                                delegate: ColumnLayout {
                                    required property var modelData
                                    required property int index
                                    Layout.fillWidth: true
                                    spacing: 2

                                    // Cabecera del bloque
                                    Rectangle {
                                        Layout.fillWidth: true
                                        Layout.preferredHeight: 34
                                        color: Qt.rgba(1, 1, 1, 0.05)
                                        radius: 6
                                        RowLayout {
                                            anchors { fill: parent; leftMargin: 10; rightMargin: 8 }
                                            spacing: 8
                                            TextField {
                                                Layout.fillWidth: true
                                                Layout.minimumWidth: 60
                                                implicitWidth: 0 // que no empuje el ancho del panel
                                                text: modelData.nombre
                                                placeholderText: qsTr("Nombre del bloque")
                                                color: Theme.text
                                                font { family: Theme.font; pixelSize: 13; weight: Font.DemiBold }
                                                background: Item {}
                                                onEditingFinished: page.ev.renombrarBloque(index, text)
                                            }
                                            Text {
                                                // La hora la calcula el modelo (la misma que lee el asistente).
                                                text: (modelData.horaPrevista || "") !== ""
                                                      ? modelData.horaPrevista + " · " + page.mmss(modelData.duracionSeg)
                                                      : page.mmss(modelData.duracionSeg)
                                                color: Theme.textDim
                                                font { family: Theme.mono; pixelSize: 12 }
                                            }
                                            GlassButton { text: qsTr("+ Cue"); compact: true; onClicked: tipoDlg.abrir(index) }
                                            GlassButton { icon: "↑"; compact: true; onClicked: page.ev.moverBloque(index, index - 1) }
                                            GlassButton { icon: "↓"; compact: true; onClicked: page.ev.moverBloque(index, index + 1) }
                                            GlassButton { icon: "✕"; compact: true; onClicked: page.ev.quitarBloque(index) }
                                        }
                                    }

                                    Text {
                                        visible: (modelData.cues || []).length === 0
                                        Layout.fillWidth: true
                                        Layout.leftMargin: 12
                                        Layout.topMargin: 4
                                        Layout.bottomMargin: 4
                                        text: qsTr("Bloque vacío: agregale un cue para que pase algo.")
                                        color: Theme.textFaint
                                        font.pixelSize: 12
                                    }

                                    // Los cues del bloque
                                    Repeater {
                                        model: modelData.cues
                                        delegate: Rectangle {
                                            required property var modelData
                                            required property int index
                                            readonly property bool esActual: page.ev.actual
                                                && page.ev.actual.bloque === modelData.bloque
                                                && page.ev.actual.cue === modelData.cue
                                            readonly property bool elegido: page.selB === modelData.bloque
                                                && page.selC === modelData.cue

                                            Layout.fillWidth: true
                                            Layout.preferredHeight: 38
                                            radius: 5
                                            color: esActual ? Qt.rgba(1, 0.42, 0.17, 0.22)
                                                            : (elegido ? Qt.rgba(1, 1, 1, 0.09) : "transparent")
                                            border.color: esActual ? Theme.accent : "transparent"

                                            // La franja del tipo: se lee de un vistazo cual es cual.
                                            Rectangle {
                                                anchors { left: parent.left; top: parent.top; bottom: parent.bottom; margins: 4 }
                                                width: 3
                                                radius: 2
                                                color: page.colorDeTipo(modelData.tipo)
                                            }

                                            MouseArea {
                                                anchors.fill: parent
                                                onClicked: { page.selB = modelData.bloque; page.selC = modelData.cue }
                                                onDoubleClicked: page.ev.irA(modelData.bloque, modelData.cue)
                                            }

                                            RowLayout {
                                                anchors { fill: parent; leftMargin: 14; rightMargin: 8 }
                                                spacing: 8

                                                Text {
                                                    Layout.preferredWidth: 26
                                                    text: String(modelData.nro).padStart(2, "0")
                                                    color: Theme.textFaint
                                                    font { family: Theme.mono; pixelSize: 11 }
                                                }
                                                Rectangle {
                                                    Layout.preferredWidth: tipoTxt.implicitWidth + 12
                                                    Layout.preferredHeight: 18
                                                    radius: 3
                                                    color: Qt.rgba(1, 1, 1, 0.07)
                                                    Text {
                                                        id: tipoTxt
                                                        anchors.centerIn: parent
                                                        text: modelData.tipo
                                                        color: page.colorDeTipo(modelData.tipo)
                                                        font { family: Theme.font; pixelSize: 10; weight: Font.DemiBold }
                                                    }
                                                }
                                                Text {
                                                    Layout.fillWidth: true
                                                    elide: Text.ElideRight
                                                    text: modelData.texto !== "" ? modelData.texto
                                                          : (modelData.datos.escena || modelData.datos.archivo
                                                             || modelData.datos.participante || qsTr("sin descripción"))
                                                    color: modelData.texto !== "" ? Theme.text : Theme.textFaint
                                                    font.pixelSize: 12
                                                }
                                                Text {
                                                    visible: modelData.falta !== ""
                                                    text: "⚠"
                                                    color: Theme.live
                                                    font.pixelSize: 13
                                                    ToolTip.visible: faltaMa.containsMouse
                                                    ToolTip.text: modelData.falta
                                                    MouseArea { id: faltaMa; anchors.fill: parent; hoverEnabled: true }
                                                }
                                                Text {
                                                    visible: modelData.automatico
                                                    text: qsTr("auto")
                                                    color: Theme.amber
                                                    font.pixelSize: 10
                                                }
                                                Text {
                                                    text: page.mmss(modelData.duracionSeg)
                                                    color: Theme.textDim
                                                    font { family: Theme.mono; pixelSize: 11 }
                                                }
                                                // Editarlo (lo mismo que tocar la fila): el editor sale a la derecha.
                                                GlassButton {
                                                    compact: true; text: "✎"
                                                    active: elegido
                                                    onClicked: { page.selB = modelData.bloque; page.selC = modelData.cue; cueDlg.abrir(modelData.bloque, modelData.cue) }
                                                    ToolTip.visible: hovered; ToolTip.delay: 400; ToolTip.text: qsTr("Editar este cue")
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                            Item { Layout.fillHeight: true }
                        }
                    }
                }
            }

            // ---- Conduccion ---------------------------------------------------
            ColumnLayout {
                Layout.preferredWidth: 380
                Layout.maximumWidth: 380
                Layout.minimumWidth: 300
                Layout.fillHeight: true
                spacing: 14

                // Tras un corte de luz (PLAN-LINEA-DE-TIEMPO 3.8): el aire volvió como
                // estaba pero la línea quedó en HOLD. Se pregunta qué está pasando y lo
                // que se cuenta va al asistente, que PROPONE cómo seguir; la persona
                // confirma y recién ahí se suelta. Todo sale de `retoma` (view model).
                Panel {
                    id: retomaPanel
                    readonly property var r: page.ev.retoma || ({})
                    visible: !!r.activa
                    Layout.fillWidth: true
                    implicitHeight: retomaCuerpo.implicitHeight + 46 + padding // el título y el margen de Panel
                    title: qsTr("Volvió la luz: la línea está en HOLD")

                    ColumnLayout {
                        id: retomaCuerpo
                        anchors.fill: parent
                        spacing: 8
                        Text {
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                            text: qsTr("El aire volvió como estaba, pero nada avanza solo. ¿Qué está pasando?")
                            color: Theme.text
                            font { family: Theme.font; pixelSize: 14; weight: Font.DemiBold }
                        }
                        TextArea {
                            id: retomaTexto
                            visible: !!retomaPanel.r.pideDescripcion
                            Layout.fillWidth: true
                            Layout.preferredHeight: 70
                            wrapMode: TextEdit.Wrap
                            placeholderText: qsTr("«El orador sigue hablando», «ya votaron, vamos al cierre», «vamos 10 minutos atrasados»…")
                        }
                        Text {
                            Layout.fillWidth: true
                            visible: !!retomaPanel.r.pideDescripcion && Studio.voz.disponible
                            wrapMode: Text.WordWrap
                            text: qsTr("También lo podés dictar con el micrófono de la barra del asistente.")
                            color: Theme.textDim
                            font.pixelSize: 11
                        }
                        Text {
                            Layout.fillWidth: true
                            visible: !retomaPanel.r.pideDescripcion
                            wrapMode: Text.WordWrap
                            text: qsTr("Contaste: «%1». El asistente propone cómo seguir; cuando confirmes, suelta el HOLD.")
                                  .arg(retomaPanel.r.descripcion || "")
                            color: Theme.textDim
                            font.pixelSize: 12
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 6
                            GlassButton {
                                text: qsTr("Contarle al asistente")
                                visible: !!retomaPanel.r.pideDescripcion
                                enabled: retomaTexto.text.trim() !== ""
                                active: true
                                Layout.fillWidth: true
                                onClicked: {
                                    const pedido = page.ev.describirRetoma(retomaTexto.text)
                                    if (pedido === "") return
                                    retomaTexto.text = ""
                                    Studio.agent.encargar(pedido)
                                }
                            }
                            GlassButton {
                                text: qsTr("Seguir como estaba")
                                Layout.fillWidth: true
                                onClicked: page.ev.seguirComoEstaba()
                                ToolTip.visible: hovered; ToolTip.delay: 400
                                ToolTip.text: qsTr("Suelta el HOLD sin cambiar nada: lo que ya se pasó de su hora entra en el acto.")
                            }
                        }
                    }
                }

                Panel {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 296
                    title: page.ev.ensayo ? qsTr("Conducción (ensayo)") : qsTr("Conducción")

                    // HOLD con la H: frena (o suelta) el avance automático del cue actual.
                    // Con el foco en un campo de texto la H se escribe: el campo se la queda.
                    Shortcut {
                        sequence: "H"
                        enabled: page.visible && (page.ev.estado === "corriendo" || page.ev.estado === "pausado")
                        onActivated: page.ev.alternarHold()
                    }

                    ColumnLayout {
                        anchors.fill: parent
                        spacing: 8

                        // El desvio, que es lo unico que hay que poder leer de lejos.
                        Text {
                            Layout.fillWidth: true
                            horizontalAlignment: Text.AlignHCenter
                            visible: page.ev.estado !== "parado"
                            text: {
                                const d = page.ev.reloj.desvioSeg
                                if (Math.abs(d) < 5) return qsTr("en hora")
                                return (d > 0 ? qsTr("%1 tarde") : qsTr("%1 adelantados")).arg(page.mmss(Math.abs(d)))
                            }
                            color: {
                                const d = Math.abs(page.ev.reloj.desvioSeg)
                                return d < 5 ? Theme.ok : (d < 60 ? Theme.amber : Theme.live)
                            }
                            font { family: Theme.font; pixelSize: 30; weight: Font.DemiBold }
                        }

                        Text {
                            Layout.fillWidth: true
                            horizontalAlignment: Text.AlignHCenter
                            visible: page.ev.estado === "parado"
                            text: qsTr("El guion no está corriendo")
                            color: Theme.textFaint
                            font.pixelSize: 13
                        }

                        // Cue actual
                        Rectangle {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 62
                            radius: 6
                            visible: page.ev.estado !== "parado"
                            color: Qt.rgba(1, 0.42, 0.17, 0.16)
                            border.color: Theme.accent
                            ColumnLayout {
                                anchors { fill: parent; margins: 9 }
                                spacing: 1
                                Text {
                                    text: (page.ev.actual.bloqueNombre || "") + " · " + (page.ev.actual.tipo || "")
                                    color: Theme.textDim
                                    font.pixelSize: 11
                                }
                                Text {
                                    Layout.fillWidth: true
                                    elide: Text.ElideRight
                                    text: page.ev.actual.texto || page.ev.actual.datos && page.ev.actual.datos.escena || "—"
                                    color: Theme.text
                                    font { family: Theme.font; pixelSize: 15; weight: Font.DemiBold }
                                }
                                Text {
                                    text: (page.ev.actual.duracionSeg > 0
                                           ? qsTr("va %1 de %2 · cue %3 de %4").arg(page.mmss(page.ev.reloj.enCueSeg))
                                                 .arg(page.mmss(page.ev.actual.duracionSeg))
                                                 .arg(page.ev.reloj.cueNro).arg(page.ev.reloj.cuesTotal)
                                           : qsTr("va %1 · cue %2 de %3").arg(page.mmss(page.ev.reloj.enCueSeg))
                                                 .arg(page.ev.reloj.cueNro).arg(page.ev.reloj.cuesTotal))
                                    color: Theme.textDim
                                    font { family: Theme.mono; pixelSize: 11 }
                                }
                            }
                        }

                        // El que sigue
                        Rectangle {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 44
                            radius: 6
                            visible: page.ev.estado !== "parado"
                            color: Qt.rgba(1, 1, 1, 0.05)
                            ColumnLayout {
                                anchors { fill: parent; margins: 8 }
                                spacing: 0
                                Text { text: qsTr("sigue"); color: Theme.textFaint; font.pixelSize: 10 }
                                Text {
                                    Layout.fillWidth: true
                                    elide: Text.ElideRight
                                    text: page.ev.siguiente.texto
                                          || (page.ev.siguiente.tipo ? page.ev.siguiente.tipo : qsTr("nada: es el último"))
                                    color: Theme.textDim
                                    font.pixelSize: 12
                                }
                            }
                        }

                        Item { Layout.fillHeight: true }

                        // El avance automático: el aviso de los últimos 5 s o, frenado, el HOLD.
                        // Los dos salen del view model (entraSoloEnSeg, hold).
                        Text {
                            Layout.fillWidth: true
                            horizontalAlignment: Text.AlignHCenter
                            visible: page.ev.hold || page.ev.entraSoloEnSeg > 0
                            text: page.ev.hold ? qsTr("HOLD: este paso no se va solo")
                                               : qsTr("entra solo en %1").arg(page.mmss(page.ev.entraSoloEnSeg))
                            color: page.ev.hold ? Theme.live : Theme.amber
                            font { family: Theme.font; pixelSize: 15; weight: Font.DemiBold }
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 6
                            GlassButton {
                                text: page.ev.estado === "parado" ? qsTr("Arrancar") : qsTr("Siguiente")
                                active: page.ev.estado !== "parado"
                                Layout.fillWidth: true
                                onClicked: {
                                    if (page.ev.estado !== "parado") { page.ev.avanzar(); return }
                                    // La revisión previa la hace el view model; si hay algo grave, se pregunta.
                                    const graves = page.ev.arrancarRevisando(false)
                                    if (graves === 0) return
                                    ApplicationWindow.window.confirm({
                                        title: qsTr("¿Correr el guion igual?"),
                                        text: qsTr("La revisión encontró %n problema grave. Mirá «Revisión» antes de salir.", "", graves),
                                        accept: qsTr("Correr igual"), reject: qsTr("Revisar"),
                                        action: function() { page.ev.arrancarRevisando(true) }
                                    })
                                }
                            }
                            GlassButton {
                                icon: "◀"; compact: true
                                visible: page.ev.estado !== "parado"
                                onClicked: page.ev.retroceder()
                            }
                            GlassButton {
                                icon: page.ev.estado === "pausado" ? "▶" : "❚❚"; compact: true
                                visible: page.ev.estado !== "parado"
                                onClicked: page.ev.estado === "pausado" ? page.ev.reanudar() : page.ev.pausar()
                            }
                            GlassButton {
                                text: "HOLD"; compact: true
                                visible: page.ev.estado === "corriendo" || page.ev.estado === "pausado"
                                active: page.ev.hold
                                onClicked: page.ev.alternarHold()
                                ToolTip.visible: hovered; ToolTip.delay: 400
                                ToolTip.text: qsTr("Que este paso no pase solo al siguiente (H). Otra vez lo suelta.")
                            }
                            GlassButton {
                                icon: "■"; compact: true
                                visible: page.ev.estado !== "parado"
                                onClicked: page.ev.parar()
                            }
                            GlassButton {
                                icon: "▤"; compact: true
                                visible: page.ev.estado !== "parado"
                                onClicked: { page.informe = page.ev.informe(); informeDlg.open() }
                                ToolTip.visible: hovered
                                ToolTip.text: qsTr("Ver cuánto duró de verdad cada bloque")
                            }
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 8
                            Toggle {
                                checked: page.ev.ensayo
                                enabled: page.ev.estado === "parado"
                                onToggled: (w) => page.ev.ensayo = w
                            }
                            Text {
                                Layout.fillWidth: true
                                wrapMode: Text.WordWrap
                                text: page.ev.ensayo ? qsTr("Ensayo: recorre el guion sin tocar lo que sale al aire")
                                                     : qsTr("Ensayo apagado: los cues salen al aire de verdad")
                                color: page.ev.ensayo ? Theme.amber : Theme.textDim
                                font.pixelSize: 11
                            }
                        }
                    }
                }

                // El cue elegido: lo que es (solo para leer). Se edita en el modal (DialogoCue),
                // con el lápiz de la fila o con «Editar…».
                Panel {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    title: page.selCue ? qsTr("Cue %1 del bloque %2").arg(page.selC + 1).arg(page.selB + 1) : ""
                    visible: !!page.selCue

                    GlassButton {
                        z: 2
                        anchors { right: parent.right; top: parent.top; topMargin: -40 }
                        compact: true; text: "✕"
                        onClicked: page.selC = -1
                        ToolTip.visible: hovered; ToolTip.delay: 400; ToolTip.text: qsTr("Volver al chequeo y la bitácora")
                    }

                    ScrollView {
                        id: cueScroll
                        anchors.fill: parent
                        clip: true
                        contentWidth: availableWidth
                        ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
                        ColumnLayout {
                            width: cueScroll.availableWidth
                            spacing: 6
                            RowLayout {
                                spacing: 8
                                Rectangle {
                                    Layout.preferredWidth: tipoInfo.implicitWidth + 12
                                    Layout.preferredHeight: 20
                                    radius: 3
                                    color: Qt.rgba(1, 1, 1, 0.07)
                                    Text {
                                        id: tipoInfo
                                        anchors.centerIn: parent
                                        text: page.selCue ? page.selCue.tipo : ""
                                        color: page.selCue ? page.colorDeTipo(page.selCue.tipo) : Theme.text
                                        font { pixelSize: 11; weight: Font.DemiBold }
                                    }
                                }
                                Text {
                                    text: page.selCue ? (page.selCue.automatico ? qsTr("%1 · encadena solo").arg(page.mmss(page.selCue.duracionSeg)) : page.mmss(page.selCue.duracionSeg)) : ""
                                    color: Theme.textDim; font { family: Theme.mono; pixelSize: 11 }
                                }
                            }
                            Text {
                                visible: !!page.selCue && page.selCue.falta !== ""
                                Layout.fillWidth: true
                                wrapMode: Text.WordWrap
                                text: page.selCue ? "⚠ " + page.selCue.falta : ""
                                color: Theme.live
                                font.pixelSize: 11
                            }
                            Text {
                                Layout.fillWidth: true
                                wrapMode: Text.WordWrap
                                text: page.selCue ? (page.selCue.texto || qsTr("(sin texto)")) : ""
                                color: Theme.text
                                font.pixelSize: 12
                            }
                            // Lo propio del tipo, como está guardado.
                            Repeater {
                                model: {
                                    if (!page.selCue) return []
                                    const out = []
                                    const d = page.selCue.datos || {}
                                    for (const k in d) {
                                        if (k === "valores" && typeof d[k] === "object") {
                                            for (const v in d[k]) out.push({ k: v, v: String(d[k][v]) })
                                        } else {
                                            out.push({ k: k, v: typeof d[k] === "object" ? JSON.stringify(d[k]) : String(d[k]) })
                                        }
                                    }
                                    return out
                                }
                                delegate: RowLayout {
                                    required property var modelData
                                    Layout.fillWidth: true
                                    spacing: 6
                                    Text { text: modelData.k; color: Theme.textFaint; font.pixelSize: 11; Layout.preferredWidth: 90; elide: Text.ElideRight }
                                    Text { text: modelData.v; color: Theme.textDim; font.pixelSize: 11; Layout.fillWidth: true; wrapMode: Text.WrapAnywhere }
                                }
                            }
                            RowLayout {
                                Layout.fillWidth: true
                                Layout.topMargin: 6
                                spacing: 6
                                GlassButton { text: qsTr("Editar…"); compact: true; active: true; onClicked: cueDlg.abrir(page.selB, page.selC) }
                                GlassButton { text: qsTr("Poner al aire"); compact: true; onClicked: page.ev.irA(page.selB, page.selC) }
                                GlassButton {
                                    readonly property string url: page.selCue ? page.ev.enlaceDeCue(page.selCue.tipo, page.selCue.datos || ({})) : ""
                                    visible: url !== ""
                                    text: qsTr("Ver el teleprompter ↗"); compact: true
                                    onClicked: Qt.openUrlExternally(url)
                                    ToolTip.visible: hovered; ToolTip.delay: 400; ToolTip.text: qsTr("La pantalla del orador en el navegador: para ver si carga y avanza (%1)").arg(url)
                                }
                            }
                        }
                    }
                }

                // Sin cue elegido: el chequeo, la revisión y la bitácora, con desplazamiento
                // (apilados excedían la ventana y empujaban la página hacia abajo).
                ScrollView {
                    id: extrasScroll
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    visible: !page.selCue
                    clip: true
                    contentWidth: availableWidth
                    ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
                    ColumnLayout {
                        width: extrasScroll.availableWidth
                        spacing: 14
                        Text {
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                            text: qsTr("Tocá un cue para ver qué hace; el lápiz lo edita. Doble clic lo pone al aire.")
                            color: Theme.textFaint
                            font.pixelSize: 11
                        }
                        // Antes de salir: la lista de chequeo manual (lo que no se mide solo).
                        Panel {
                            id: chequeoPanel
                            Layout.fillWidth: true
                            Layout.preferredHeight: Math.min(260, 48 + lista.length * 30)
                            title: qsTr("Antes de salir · %1/%2").arg(lista.filter(x => x.hecho).length).arg(lista.length)
                            property var lista: page.ev.chequeo()
                            Connections {
                                target: page.ev
                                function onChequeoChanged() { chequeoPanel.lista = page.ev.chequeo() }
                            }
                            ListView {
                                anchors.fill: parent
                                clip: true
                                spacing: 2
                                model: chequeoPanel.lista
                                delegate: RowLayout {
                                    required property var modelData
                                    width: ListView.view.width
                                    spacing: 6
                                    CheckBox {
                                        checked: modelData.hecho
                                        onToggled: page.ev.marcarChequeo(modelData.item, checked, modelData.nota)
                                    }
                                    Text {
                                        Layout.fillWidth: true; elide: Text.ElideRight
                                        text: modelData.texto + (modelData.cuando ? "  · " + modelData.cuando : "")
                                        color: modelData.hecho ? Theme.textFaint : Theme.text
                                        font { pixelSize: 11; strikeout: modelData.hecho }
                                    }
                                }
                            }
                        }

                        // Reparos de la revision previa
                        Panel {
                            Layout.fillWidth: true
                            Layout.preferredHeight: Math.min(150, 40 + page.ev.reparos.length * 34)
                            visible: page.ev.reparos.length > 0
                            title: qsTr("Revisión")
                            ListView {
                                anchors.fill: parent
                                clip: true
                                spacing: 4
                                model: page.ev.reparos
                                delegate: RowLayout {
                                    required property var modelData
                                    width: ListView.view.width
                                    spacing: 6
                                    Text {
                                        text: modelData.grave ? "●" : "○"
                                        color: modelData.grave ? Theme.live : Theme.amber
                                        font.pixelSize: 12
                                    }
                                    Text {
                                        Layout.fillWidth: true
                                        wrapMode: Text.WordWrap
                                        text: modelData.bloque < 0 ? modelData.texto
                                              : modelData.cue >= 0 ? qsTr("Bloque %1, cue %2: %3").arg(modelData.bloque + 1).arg(modelData.cue + 1).arg(modelData.texto)
                                              : qsTr("Bloque %1: %2").arg(modelData.bloque + 1).arg(modelData.texto)
                                        color: Theme.textDim
                                        font.pixelSize: 11
                                    }
                                    // Un handler y no un MouseArea: dentro del layout, un Item con anchors
                                    // es «undefined behavior» (y un aviso por fila en cada repintado).
                                    TapHandler {
                                        onTapped: { page.selB = modelData.bloque; page.selC = modelData.cue }
                                    }
                                }
                            }
                        }

                        // Bitacora: lo que fue pasando
                        Panel {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 130
                            visible: page.ev.bitacora.length > 0
                            title: qsTr("Lo que pasó")
                            ListView {
                                anchors.fill: parent
                                clip: true
                                spacing: 2
                                model: page.ev.bitacora
                                delegate: RowLayout {
                                    required property var modelData
                                    width: ListView.view.width
                                    spacing: 6
                                    Text {
                                        text: modelData.hora
                                        color: Theme.textFaint
                                        font { family: Theme.mono; pixelSize: 10 }
                                    }
                                    Text {
                                        Layout.fillWidth: true
                                        elide: Text.ElideRight
                                        text: modelData.texto
                                        color: modelData.tipo === "error" ? Theme.live
                                               : (modelData.tipo === "ok" ? Theme.ok : Theme.textDim)
                                        font.pixelSize: 11
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    // --- Informe: previsto contra real ---------------------------------------
    Dialog {
        id: informeDlg
        anchors.centerIn: parent
        width: Math.min(page.width - 80, 620)
        modal: true
        title: qsTr("Lo que duró de verdad")
        standardButtons: Dialog.Close

        ColumnLayout {
            anchors.fill: parent
            spacing: 8

            Text {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                color: Theme.textDim
                font.pixelSize: 12
                text: page.informe && page.informe.bloques
                      ? (page.informe.desvioSeg >= 0 ? qsTr("Previsto %1 · real %2 · nos pasamos %3")
                                                     : qsTr("Previsto %1 · real %2 · sobró %3"))
                            .arg(page.mmss(page.informe.previstoSeg)).arg(page.mmss(page.informe.realSeg))
                            .arg(page.mmss(Math.abs(page.informe.desvioSeg))) +
                        (page.informe.ensayo ? qsTr("  (fue un ensayo)") : "")
                      : qsTr("El guion todavía no se corrió.")
            }

            ListView {
                Layout.fillWidth: true
                Layout.preferredHeight: Math.min(340, 34 * ((page.informe.bloques || []).length + 1))
                clip: true
                spacing: 3
                model: page.informe.bloques || []
                delegate: RowLayout {
                    required property var modelData
                    width: ListView.view.width
                    spacing: 8
                    Text {
                        Layout.fillWidth: true
                        elide: Text.ElideRight
                        text: modelData.nombre
                        color: Theme.text
                        font.pixelSize: 13
                    }
                    Text {
                        text: page.mmss(modelData.previstoSeg) + " → " + page.mmss(modelData.realSeg)
                        color: Theme.textDim
                        font { family: Theme.mono; pixelSize: 12 }
                    }
                    Text {
                        Layout.preferredWidth: 70
                        horizontalAlignment: Text.AlignRight
                        text: (modelData.desvioSeg > 0 ? "+" : "") + page.mmss(modelData.desvioSeg)
                        color: Math.abs(modelData.desvioSeg) < 15 ? Theme.ok
                               : (Math.abs(modelData.desvioSeg) < 60 ? Theme.amber : Theme.live)
                        font { family: Theme.mono; pixelSize: 12; weight: Font.DemiBold }
                    }
                }
            }

            GlassButton {
                text: qsTr("Guardar en la carpeta del evento"); compact: true
                onClicked: page.ev.guardarInforme()
            }
        }
    }

    // --- Cuando es el evento ------------------------------------------------
    Dialog {
        id: cuandoDlg
        anchors.centerIn: parent
        width: 460
        modal: true
        title: qsTr("Cuándo es el evento")
        standardButtons: Dialog.Ok | Dialog.Cancel
        onAboutToShow: {
            fechaCampo.text = page.ev.guion.fecha || ""
            horaCampo.text = page.ev.guion.inicio || ""
            husoCampo.text = page.ev.guion.huso || ""
            idiomaCampo.model = page.ev.idiomas()
            idiomaCampo.currentIndex = Math.max(0, idiomaCampo.indexOfValue(page.ev.guion.idioma || ""))
        }
        onAccepted: {
            page.ev.setFecha(fechaCampo.text.trim())
            page.ev.setInicio(horaCampo.text.trim())
            page.ev.setHuso(husoCampo.text.trim())
            page.ev.setIdioma(idiomaCampo.currentValue || "")
        }
        ColumnLayout {
            anchors.fill: parent
            spacing: 8
            Text {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                text: qsTr("De la hora salen las horas previstas de cada bloque. De la fecha con el huso " +
                      "sale el momento exacto, que es lo único que le sirve al que se conecta desde " +
                      "otro huso horario.")
                color: Theme.textDim
                font.pixelSize: 12
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                TextField {
                    id: fechaCampo
                    Layout.fillWidth: true
                    placeholderText: "2026-05-25"
                    color: Theme.text
                    onAccepted: cuandoDlg.accept()
                }
                TextField {
                    id: horaCampo
                    Layout.preferredWidth: 90
                    placeholderText: "20:00"
                    color: Theme.text
                    onAccepted: cuandoDlg.accept()
                }
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                TextField {
                    id: husoCampo
                    Layout.fillWidth: true
                    placeholderText: "America/Argentina/Buenos_Aires"
                    color: Theme.text
                    onAccepted: cuandoDlg.accept()
                }
                // El del aparato es el valor bueno casi siempre: el que arma
                // el evento esta en el lugar del evento.
                GlassButton {
                    compact: true
                    text: qsTr("El de acá")
                    onClicked: husoCampo.text = page.ev.husoDelAparato()
                }
            }
            // El idioma del evento: el de lo que ven y escuchan el público y los invitados
            // (gráficos, tótems, voz, la web); puede no ser el del programa.
            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                Text { text: qsTr("Idioma del evento"); color: Theme.textDim; font.pixelSize: 12 }
                ComboBox {
                    id: idiomaCampo
                    Layout.fillWidth: true
                    textRole: "label"
                    valueRole: "value"
                    ToolTip.visible: hovered; ToolTip.delay: 400
                    ToolTip.text: qsTr("Lo que ven y escuchan el público y los invitados: gráficos, tótems, voz y la página web")
                }
            }
        }
    }

    Dialog {
        id: nuevoDlg
        anchors.centerIn: parent
        width: 420
        modal: true
        title: qsTr("Otro guion en este evento")
        standardButtons: Dialog.Ok | Dialog.Cancel
        onAboutToShow: { nombreCampo.text = ""; carpetaNueva.editText = "" }
        onAccepted: if (nombreCampo.text.trim() !== "") page.ev.crear(nombreCampo.text.trim(), carpetaNueva.editText.trim())
        ColumnLayout {
            anchors.fill: parent
            spacing: 8
            Text {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                text: qsTr("Un guion más dentro de este evento: usa sus mismas escenas, mezclador y medios. " +
                      "Para un evento aparte, con lo suyo, es «Evento nuevo…».")
                color: Theme.textDim
                font.pixelSize: 12
            }
            TextField {
                id: nombreCampo
                Layout.fillWidth: true
                placeholderText: qsTr("Acto del 25 de mayo")
                color: Theme.text
                onAccepted: nuevoDlg.accept()
            }
            Text { text: qsTr("En la carpeta (vacío = Eventos/; se crea si no está)"); color: Theme.textDim; font.pixelSize: 12 }
            ComboBox {
                id: carpetaNueva
                Layout.fillWidth: true
                editable: true
                model: page.ev.carpetas
            }
        }
    }

    // --- Evento nuevo: aparte, con todo lo suyo --------------------------------
    Dialog {
        id: nuevoEventoDlg
        readonly property var cf: Studio.configuraciones
        property string modo: "cero"
        anchors.centerIn: parent
        width: 500
        modal: true
        title: qsTr("Evento nuevo")
        onAboutToShow: { nombreEv.text = ""; modo = "cero"; abrirlo.checked = !(Studio.streaming || Studio.recording) }
        ColumnLayout {
            width: nuevoEventoDlg.availableWidth
            spacing: 8
            Text {
                Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.textDim; font.pixelSize: 12
                text: qsTr("Un evento tiene TODO lo suyo: escenas, mezclador, medios, gráficos, distribución de la asamblea, " +
                      "orden del día, votaciones, página web y guion. Copiar es copia total (también la web, el historial y " +
                      "las grabaciones). El evento abierto no se toca.")
            }
            // Lo recomendado: que el asistente lo prepare entero siguiendo la guía de su tipo
            // (pregunta lo que hace falta, arma, verifica y entrega).
            GlassButton {
                Layout.fillWidth: true
                icon: "✦"; text: qsTr("Prepararlo con el asistente (recomendado)"); active: true
                onClicked: { nuevoEventoDlg.close(); asistenteEvtDlg.open() }
                ToolTip.visible: hovered; ToolTip.delay: 400
                ToolTip.text: qsTr("Elegís el tipo de evento y el asistente te hace las preguntas, lo arma desde cero, lo verifica y te pasa los enlaces")
            }
            RowLayout {
                Layout.fillWidth: true
                Text { Layout.fillWidth: true; text: qsTr("O crealo a mano:"); color: Theme.textFaint; font.pixelSize: 11 }
                GlassButton { compact: true; text: qsTr("Administrar plantillas…"); onClicked: { nuevoEventoDlg.close(); plantillasDlg.open() } }
            }
            TextField { id: nombreEv; Layout.fillWidth: true; placeholderText: qsTr("Sesión ordinaria del 20 de octubre"); color: Theme.text }
            ButtonGroup { id: grupoDesde }
            RadioButton { ButtonGroup.group: grupoDesde; checked: nuevoEventoDlg.modo === "cero"; onClicked: nuevoEventoDlg.modo = "cero"
                          text: qsTr("Desde cero (de fábrica: escenas base, sin medios ni asamblea)") }
            RadioButton { ButtonGroup.group: grupoDesde; checked: nuevoEventoDlg.modo === "actual"; onClicked: nuevoEventoDlg.modo = "actual"
                          text: qsTr("Copiando este («%1»): copia total").arg(nuevoEventoDlg.cf ? nuevoEventoDlg.cf.activa : "") }
            RowLayout {
                RadioButton { ButtonGroup.group: grupoDesde; checked: nuevoEventoDlg.modo === "anterior"; onClicked: nuevoEventoDlg.modo = "anterior"
                              text: qsTr("Copiando otro evento") }
                ComboBox {
                    id: anterior
                    Layout.fillWidth: true
                    enabled: nuevoEventoDlg.modo === "anterior"
                    model: nuevoEventoDlg.cf ? nuevoEventoDlg.cf.lista.filter(e => !e.activa).map(e => e.nombre) : []
                }
            }
            RowLayout {
                RadioButton { ButtonGroup.group: grupoDesde; checked: nuevoEventoDlg.modo === "plantilla"; onClicked: nuevoEventoDlg.modo = "plantilla"
                              text: qsTr("Desde una plantilla") }
                ComboBox {
                    id: plantilla
                    Layout.fillWidth: true
                    enabled: nuevoEventoDlg.modo === "plantilla"
                    // El nombre que se ve; se crea con la carpeta (puede tener otro nombre visible).
                    model: nuevoEventoDlg.cf ? nuevoEventoDlg.cf.plantillasUi() : []
                    textRole: "nombre"
                    valueRole: "carpeta"
                }
            }
            CheckBox {
                id: abrirlo
                text: (Studio.streaming || Studio.recording) ? qsTr("Abrirlo ahora (no: hay algo al aire o grabando)") : qsTr("Abrirlo ahora (la app se reinicia con él)")
                enabled: !(Studio.streaming || Studio.recording)
            }
            RowLayout {
                Item { Layout.fillWidth: true }
                GlassButton { compact: true; text: qsTr("Cancelar"); onClicked: nuevoEventoDlg.close() }
                GlassButton {
                    compact: true; active: true; text: qsTr("Crear")
                    enabled: nombreEv.text.trim() !== "" && (nuevoEventoDlg.modo !== "anterior" || anterior.currentIndex >= 0)
                             && (nuevoEventoDlg.modo !== "plantilla" || plantilla.currentIndex >= 0)
                    onClicked: {
                        const m = nuevoEventoDlg.modo
                        const desde = m === "anterior" ? anterior.currentText : m === "plantilla" ? "plantilla:" + plantilla.currentValue : m
                        // En otro hilo (una copia puede tardar): el progreso arriba, el aviso al terminar.
                        const e = nuevoEventoDlg.cf.nuevoEventoUi(nombreEv.text.trim(), desde, abrirlo.checked && !(Studio.streaming || Studio.recording))
                        if (e !== "") { Studio.notify(e, "error"); return }
                        nuevoEventoDlg.close()
                    }
                }
            }
        }
    }

    // --- Administrar las plantillas de evento -----------------------------------------
    DialogoPlantillas { id: plantillasDlg }

    // --- Preparar un evento con el asistente (la guía de su tipo) ------------------
    Dialog {
        id: asistenteEvtDlg
        property string elegido: ""
        property string plantillaElegida: ""
        /// Los tipos de evento: las plantillas visibles que tienen escenario (en su orden y
        /// con su nombre, los de «Plantillas…»).
        property var tipos: []
        anchors.centerIn: parent
        width: Math.min(page.width - 80, 760)
        height: Math.min(page.height - 60, 680)
        modal: true
        title: qsTr("Preparar un evento con el asistente")
        onAboutToShow: {
            elegido = ""; plantillaElegida = ""; notasEvt.text = ""
            tipos = (Studio.configuraciones ? Studio.configuraciones.plantillasUi() : []).filter(p => p.escenario !== "")
        }
        ColumnLayout {
            anchors.fill: parent
            spacing: 8
            Text {
                Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.textDim; font.pixelSize: 12
                text: qsTr("¿Qué tipo de evento es? El asistente sigue la guía de ese tipo: te pregunta lo que hace falta " +
                      "(gente, invitados, registro, documentos, gráficos, teleprompter, música, logos, traducción, pantallas), " +
                      "lo arma desde cero, prueba la conexión y la cámara antes de invitar, te sugiere el ensayo y te pasa los enlaces.")
            }
            GridView {
                id: gridEsc
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                cellWidth: Math.floor(width / Math.max(1, Math.floor(width / 230)))
                cellHeight: 86
                model: asistenteEvtDlg.tipos
                delegate: Item {
                    required property var modelData
                    width: gridEsc.cellWidth
                    height: gridEsc.cellHeight
                    Rectangle {
                        anchors { fill: parent; margins: 4 }
                        radius: 8
                        readonly property bool sel: asistenteEvtDlg.plantillaElegida === modelData.carpeta
                        color: sel ? Qt.rgba(1, 0.42, 0.17, 0.18) : (escMa.containsMouse ? Qt.rgba(1, 1, 1, 0.08) : Qt.rgba(1, 1, 1, 0.04))
                        border.color: sel ? Theme.accent : Theme.glassBorder
                        RowLayout {
                            anchors { fill: parent; margins: 8 }
                            spacing: 8
                            Text { text: modelData.icono; color: Theme.accent; font.pixelSize: 22; Layout.alignment: Qt.AlignTop }
                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 2
                                Text { Layout.fillWidth: true; text: modelData.nombre; color: Theme.text; elide: Text.ElideRight
                                       font { pixelSize: 13; weight: Font.DemiBold } }
                                Text { Layout.fillWidth: true; Layout.fillHeight: true; text: modelData.descripcion; color: Theme.textFaint
                                       font.pixelSize: 10; wrapMode: Text.WordWrap; elide: Text.ElideRight; maximumLineCount: 3 }
                            }
                        }
                        MouseArea {
                            id: escMa
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: { asistenteEvtDlg.elegido = modelData.escenario; asistenteEvtDlg.plantillaElegida = modelData.carpeta }
                        }
                    }
                }
            }
            Text { text: qsTr("Lo que ya sabés (opcional): fecha, cuántos son, quién habla…"); color: Theme.textDim; font.pixelSize: 11 }
            TextArea {
                id: notasEvt
                Layout.fillWidth: true
                Layout.preferredHeight: 60
                wrapMode: TextEdit.Wrap
                color: Theme.text
                placeholderText: qsTr("Clase de historia el jueves 16 a las 16, 60 minutos, 30 alumnos (la mitad remotos)…")
            }
            RowLayout {
                Item { Layout.fillWidth: true }
                GlassButton { compact: true; text: qsTr("Cancelar"); onClicked: asistenteEvtDlg.close() }
                GlassButton {
                    compact: true; active: true; text: qsTr("Empezar con el asistente")
                    enabled: asistenteEvtDlg.elegido !== ""
                    onClicked: {
                        Studio.agent.encargar(page.ev.pedidoDeEscenario(asistenteEvtDlg.elegido, notasEvt.text, asistenteEvtDlg.plantillaElegida))
                        asistenteEvtDlg.close()
                    }
                }
            }
        }
    }

    // --- Abrir otro evento (reinicia con lo suyo) ---------------------------------
    Dialog {
        id: abrirEventoDlg
        readonly property var cf: Studio.configuraciones
        anchors.centerIn: parent
        width: 500
        modal: true
        title: qsTr("Abrir otro evento")
        ColumnLayout {
            width: abrirEventoDlg.availableWidth
            spacing: 6
            Text { Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.textDim; font.pixelSize: 12
                   text: qsTr("Cada evento tiene sus escenas, mezclador, medios, asamblea y web. Abrir otro reinicia la app.") }
            Repeater {
                model: abrirEventoDlg.cf ? abrirEventoDlg.cf.lista : []
                delegate: RowLayout {
                    required property var modelData
                    Layout.fillWidth: true
                    Text { Layout.fillWidth: true; elide: Text.ElideRight; color: Theme.text; font.pixelSize: 13
                           text: modelData.nombre + (modelData.activa ? qsTr("  ● abierto") : "") + (modelData.plantilla ? qsTr("  (plantilla)") : "") }
                    Text { color: Theme.textFaint; font.pixelSize: 11
                           text: modelData.midiendo ? qsTr("midiendo…") : page.bytesLegibles(modelData.bytes) }
                    GlassButton {
                        compact: true; text: qsTr("Abrir"); visible: !modelData.activa
                        enabled: !(Studio.streaming || Studio.recording)
                        onClicked: {
                            const n = modelData.nombre
                            abrirEventoDlg.close()
                            ApplicationWindow.window.confirm({
                                title: qsTr("¿Abrir «%1»?").arg(n),
                                text: qsTr("La app se reinicia con ese evento (sus escenas, mezclador, medios y asamblea)."),
                                accept: qsTr("Abrir"), reject: qsTr("Cancelar"),
                                action: function() { const e = abrirEventoDlg.cf.cambiarUi(n); if (e !== "") Studio.notify(e, "error") } })
                        }
                    }
                }
            }
            Text { visible: (Studio.streaming || Studio.recording); color: Theme.amber; font.pixelSize: 12
                   text: qsTr("Hay una transmisión o una grabación en curso: terminala para abrir otro evento.") }
            RowLayout { Item { Layout.fillWidth: true } GlassButton { compact: true; text: qsTr("Cerrar"); onClicked: abrirEventoDlg.close() } }
        }
    }

    // --- Mover el evento abierto a otra carpeta ---------------------------------
    Dialog {
        id: moverDlg
        anchors.centerIn: parent
        width: 420
        modal: true
        title: qsTr("Llevar a otra carpeta")
        standardButtons: Dialog.Ok | Dialog.Cancel
        onAboutToShow: {
            const c = page.ev.abierto
            const i = c.lastIndexOf("/")
            carpetaDestino.editText = i < 0 ? "" : c.substring(0, i)
        }
        onAccepted: page.ev.moverEvento(page.ev.abierto, carpetaDestino.editText.trim())
        ColumnLayout {
            anchors.fill: parent
            spacing: 8
            Text {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                text: qsTr("Las carpetas ordenan el histórico como quieras («2026/Octubre», «Consejo/Ordinarias»). " +
                      "Vacío es la raíz de Eventos/. Mover no cambia nada del evento.")
                color: Theme.textDim
                font.pixelSize: 12
            }
            ComboBox {
                id: carpetaDestino
                Layout.fillWidth: true
                editable: true
                model: page.ev.carpetas
            }
        }
    }

    // --- Editar un cue (el lápiz de la fila) -----------------------------------
    DialogoCue { id: cueDlg; ev: page.ev }

    // --- Elegir el tipo de cue ------------------------------------------------
    Dialog {
        id: tipoDlg
        anchors.centerIn: parent
        width: 460
        modal: true
        title: qsTr("¿Qué pasa en este cue?")
        standardButtons: Dialog.Cancel
        property int bloque: -1
        function abrir(b) { bloque = b; open() }

        ColumnLayout {
            anchors.fill: parent
            spacing: 4
            Repeater {
                model: page.ev.tiposDeCue()
                delegate: Rectangle {
                    required property var modelData
                    Layout.fillWidth: true
                    Layout.preferredHeight: 42
                    radius: 5
                    color: tipoMa.containsMouse ? Qt.rgba(1, 1, 1, 0.09) : "transparent"
                    Rectangle {
                        anchors { left: parent.left; top: parent.top; bottom: parent.bottom; margins: 5 }
                        width: 3
                        radius: 2
                        color: page.colorDeTipo(modelData.tipo)
                    }
                    ColumnLayout {
                        anchors { fill: parent; leftMargin: 14; rightMargin: 8; topMargin: 4; bottomMargin: 4 }
                        spacing: 0
                        Text {
                            text: modelData.titulo
                            color: Theme.text
                            font { family: Theme.font; pixelSize: 13; weight: Font.Medium }
                        }
                        Text {
                            Layout.fillWidth: true
                            elide: Text.ElideRight
                            text: modelData.ayuda
                            color: Theme.textFaint
                            font.pixelSize: 11
                        }
                    }
                    MouseArea {
                        id: tipoMa
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            page.ev.agregarCue(tipoDlg.bloque, modelData.tipo)
                            const cs = page.ev.guion.bloques[tipoDlg.bloque].cues
                            page.selB = tipoDlg.bloque
                            page.selC = cs.length - 1
                            tipoDlg.close()
                            cueDlg.abrir(page.selB, page.selC)
                        }
                    }
                }
            }
        }
    }
}
