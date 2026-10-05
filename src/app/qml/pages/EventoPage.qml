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
        const err = Studio.despachador.encargarDesdeUi("organizador", texto)
        if (err !== "") { ev.notify(err, "error"); return }
        // `encargarDesdeUi` devuelve el error, no el id: el de la corrida sale
        // de la más nueva de este rol, que es la que se acaba de encargar.
        const cs = Studio.despachador.corridas
        for (let i = 0; i < cs.length; ++i)
            if (cs[i].rol === "organizador") { corridaId = cs[i].id; break }
    }

    function descartarLoArmado() {
        const cual = ev.abierto
        if (cual === "") return
        ApplicationWindow.window.confirm({
            title: "Descartar el evento armado",
            text: "Se borra «" + cual + "» con su guion y su padrón. No se puede deshacer.",
            accept: "Descartar", reject: "Dejarlo",
            action: () => { ev.cerrar(); ev.borrar(cual); corridaId = ""; armando = false }
        })
    }

    /// Le pide algo al asistente. Los botones de acta, capítulos e
    /// invitaciones son de una sola vez y su resultado es TEXTO para leer o
    /// pegar, así que no vale la pena una vista propia: se lo pide y contesta
    /// donde ya se lee, que es la barra del asistente.
    function pedirAsistente(texto) {
        ApplicationWindow.window.pedirAlAsistente(texto)
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
        if (f === "" && h === "") return "Sin fecha"
        if (f === "") return "Sin fecha · " + h
        if (h === "") return f + " · sin hora"
        return f + " · " + h
    }

    function horaDe(offsetSeg) {
        const inicio = ev.guion.inicio || ""
        if (!/^\d{1,2}:\d{2}$/.test(inicio) || offsetSeg < 0) return ""
        const p = inicio.split(":")
        const total = parseInt(p[0]) * 60 + parseInt(p[1]) + Math.floor(offsetSeg / 60)
        return String(Math.floor(total / 60) % 24).padStart(2, "0") + ":" + String(total % 60).padStart(2, "0")
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

                ComboBox {
                    id: selector
                    Layout.preferredWidth: 260
                    model: page.ev.eventos
                    textRole: "nombre"
                    displayText: page.ev.abierto === "" ? "Elegí un evento…" : currentText
                    onActivated: (i) => page.ev.abrir(page.ev.eventos[i].carpeta)
                    Component.onCompleted: page.ev.refrescar()
                }
                GlassButton {
                    icon: "+"; text: "Evento"
                    onClicked: nuevoDlg.open()
                }
                // Armar contándolo, en vez de llenando un formulario. No es un
                // motor aparte: es el asistente que ya existe, usando las
                // herramientas del evento (ver `evento_armar`).
                GlassButton {
                    icon: "✦"; text: "Armar contándolo"
                    active: page.armando
                    onClicked: { page.armando = true; page.corridaId = ""; }
                    ToolTip.visible: hovered
                    ToolTip.delay: 400
                    ToolTip.text: "Contale al asistente cómo va a ser el evento y lo deja escrito"
                }
                Item { Layout.fillWidth: true }

                Text {
                    visible: page.ev.abierto !== ""
                    text: page.ev.guion.cues + " cues · " + page.mmss(page.ev.guion.duracionSeg)
                    color: Theme.textDim
                    font.pixelSize: 12
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
                                  ? "Arranca " + page.ev.guion.cuando
                                  : "Poner el dia y la hora del evento"
                }
                Text {
                    visible: page.ev.sucio
                    text: "sin guardar"
                    color: Theme.amber
                    font.pixelSize: 12
                }
                GlassButton {
                    visible: page.ev.abierto !== ""
                    text: "Guardar"; compact: true
                    active: page.ev.sucio
                    onClicked: page.ev.guardar()
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
            title: "Armar el evento contándolo"

            ColumnLayout {
                anchors.fill: parent
                spacing: 10

                Text {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    text: "Contá cómo va a ser: qué pasa, en qué orden, cuánto dura, quién habla y " +
                          "quién viene. Lo arma el agente Organizador, que lo deja escrito y " +
                          "revisado. No sale nada al aire."
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
                    placeholderText: "Asamblea del consorcio, el jueves 8 de octubre a las 19, dos " +
                                     "horas, presencial en el salón y por videollamada. Hay que " +
                                     "votar tres puntos: balance, expensas y el portón. Son 40 " +
                                     "propietarios y todos votan."
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8
                    GlassButton {
                        text: page.corriendo ? "Armando…" : "Armar"
                        enabled: !page.corriendo && contalo.text.trim() !== ""
                        onClicked: page.armar(contalo.text.trim())
                    }
                    GlassButton {
                        text: page.corriendo ? "Detener" : "Cerrar"
                        onClicked: {
                            if (page.corriendo) Studio.despachador.detener(page.corridaId)
                            else { page.armando = false; page.corridaId = "" }
                        }
                    }
                    Item { Layout.fillWidth: true }
                    Text {
                        visible: page.corriendo
                        text: page.corrida.herramientas !== undefined
                              ? page.corrida.herramientas + " herramientas · " + page.corrida.duracionS + " s"
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
                                : (page.corriendo ? "Pensando…" : "")
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
                        text: "Quedó abierto «" + page.ev.abierto + "»: " +
                              page.ev.guion.bloques.length + " bloques, " + page.ev.guion.cues +
                              " cues, " + page.mmss(page.ev.guion.duracionSeg) + "." +
                              (page.ev.reparos.length > 0
                               ? " " + page.ev.reparos.length + " cosa(s) para mirar."
                               : "")
                    }
                    GlassButton {
                        text: "Verlo"
                        onClicked: { page.armando = false; page.ev.revisar() }
                    }
                    GlassButton {
                        text: "Descartarlo"
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
                    text: "Todavía no hay ningún evento abierto"
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
                    text: "Un evento es el guion de lo que va a pasar: bloques (apertura, discurso, cierre) " +
                          "y dentro de cada uno los cues concretos. Con los tiempos previstos, durante el " +
                          "evento se ve cuánto nos estamos desviando."
                }
                Text {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    horizontalAlignment: Text.AlignHCenter
                    color: Theme.textFaint
                    font.pixelSize: 12
                    text: "Se guarda en su propia carpeta, con su música, sus videos y sus discursos adentro, " +
                          "para poder llevárselo entero a otra máquina."
                }
            }
        }

        // --- Las pestañas del evento -----------------------------------------
        // Guion es lo de siempre; Padrón y Público son los MISMOS paneles que
        // usa la página de Público (controls/Panel*.qml) leyendo el mismo
        // modelo. Están en los dos lados a propósito: acá mientras se arma el
        // evento, y allá con el aire encendido, donde hay que poder mirar la
        // cola sin perder de vista el guion.
        TabBar {
            id: pestanas
            visible: page.ev.abierto !== "" && !page.armando
            Layout.fillWidth: true
            TabButton { text: "Guion" }
            TabButton {
                text: page.ev.resumenPadron.total > 0
                      ? "Padrón (" + page.ev.resumenPadron.presentes + "/" +
                        page.ev.resumenPadron.total + ")"
                      : "Padrón"
            }
            TabButton {
                text: {
                    const r = Studio.publico.recuento
                    return r && r.nuevas > 0 ? "Público (" + r.nuevas + ")" : "Público"
                }
            }
        }

        // ---- Padrón ----------------------------------------------------------
        Panel {
            visible: page.ev.abierto !== "" && !page.armando && pestanas.currentIndex === 1
            Layout.fillWidth: true
            Layout.fillHeight: true
            title: "Quiénes vienen y quiénes llegaron"
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
                        text: "Acta del evento"
                        icon: "❑"
                        onClicked: page.pedirAsistente(
                            "Armá el acta de este evento con `evento_acta` y contame qué quedó.")
                        ToolTip.visible: hovered
                        ToolTip.delay: 400
                        ToolTip.text: "Quiénes asistieron, el quórum y qué se votó"
                    }
                    GlassButton {
                        text: "Capítulos"
                        icon: "▤"
                        onClicked: page.pedirAsistente(
                            "Dame los capítulos de la grabación con `evento_capitulos`, listos para " +
                            "pegar en la descripción del video.")
                        ToolTip.visible: hovered
                        ToolTip.delay: 400
                        ToolTip.text: "Las marcas de tiempo de cada bloque, para la descripción del video"
                    }
                    GlassButton {
                        text: "Invitaciones"
                        icon: "✉"
                        enabled: page.ev.resumenPadron.total > 0
                        onClicked: page.pedirAsistente(
                            "Armame las invitaciones de los del padrón con `invitacion`, una por " +
                            "persona, con su enlace y su código.")
                        ToolTip.visible: hovered
                        ToolTip.delay: 400
                        ToolTip.text: "El enlace personal de cada uno: lo acredita al llegar y le deja el código"
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
                title: "Preguntas"
                PanelPreguntas { anchors.fill: parent }
            }
            Panel {
                Layout.preferredWidth: 460
                Layout.fillHeight: true
                title: "Votación"
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
                title: "Guion"

                ColumnLayout {
                    anchors.fill: parent
                    spacing: 8

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8
                        GlassButton { text: "+ Bloque"; compact: true; onClicked: page.ev.agregarBloque("") }
                        Item { Layout.fillWidth: true }
                        GlassButton {
                            text: "Revisar"; compact: true
                            onClicked: page.ev.revisar()
                            ToolTip.visible: hovered
                            ToolTip.text: "Busca lo que impide correr el guion, sin tocar nada"
                        }
                        GlassButton {
                            text: "Ensayo rápido"; compact: true
                            onClicked: page.ev.ensayoAcelerado()
                            ToolTip.visible: hovered
                            ToolTip.text: "Recorre el guion entero de una, sin esperar las duraciones, " +
                                          "para ver si cada cue se podría ejecutar"
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
                                                placeholderText: "Nombre del bloque"
                                                color: Theme.text
                                                font { family: Theme.font; pixelSize: 13; weight: Font.DemiBold }
                                                background: Item {}
                                                onEditingFinished: page.ev.renombrarBloque(index, text)
                                            }
                                            Text {
                                                text: page.horaDe(modelData.offsetSeg) !== ""
                                                      ? page.horaDe(modelData.offsetSeg) + " · " + page.mmss(modelData.duracionSeg)
                                                      : page.mmss(modelData.duracionSeg)
                                                color: Theme.textDim
                                                font { family: Theme.mono; pixelSize: 12 }
                                            }
                                            GlassButton { text: "+ Cue"; compact: true; onClicked: tipoDlg.abrir(index) }
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
                                        text: "Bloque vacío: agregale un cue para que pase algo."
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
                                                             || modelData.datos.participante || "sin descripción")
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
                                                    text: "auto"
                                                    color: Theme.amber
                                                    font.pixelSize: 10
                                                }
                                                Text {
                                                    text: page.mmss(modelData.duracionSeg)
                                                    color: Theme.textDim
                                                    font { family: Theme.mono; pixelSize: 11 }
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

                Panel {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 268
                    title: page.ev.ensayo ? "Conducción (ensayo)" : "Conducción"

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
                                if (Math.abs(d) < 5) return "en hora"
                                return page.mmss(Math.abs(d)) + (d > 0 ? " tarde" : " adelantados")
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
                            text: "El guion no está corriendo"
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
                                    text: "va " + page.mmss(page.ev.reloj.enCueSeg) +
                                          (page.ev.actual.duracionSeg > 0
                                           ? " de " + page.mmss(page.ev.actual.duracionSeg) : "") +
                                          " · cue " + page.ev.reloj.cueNro + " de " + page.ev.reloj.cuesTotal
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
                                Text { text: "sigue"; color: Theme.textFaint; font.pixelSize: 10 }
                                Text {
                                    Layout.fillWidth: true
                                    elide: Text.ElideRight
                                    text: page.ev.siguiente.texto
                                          || (page.ev.siguiente.tipo ? page.ev.siguiente.tipo : "nada: es el último")
                                    color: Theme.textDim
                                    font.pixelSize: 12
                                }
                            }
                        }

                        Item { Layout.fillHeight: true }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 6
                            GlassButton {
                                text: page.ev.estado === "parado" ? "Arrancar" : "Siguiente"
                                active: page.ev.estado !== "parado"
                                Layout.fillWidth: true
                                onClicked: {
                                    if (page.ev.estado !== "parado") { page.ev.avanzar(); return }
                                    // Antes de arrancar: si la revisión encontró algo grave, se pregunta.
                                    page.ev.revisar()
                                    const graves = page.ev.reparos.filter(r => r.grave).length
                                    if (graves === 0) { page.ev.arrancar(); return }
                                    ApplicationWindow.window.confirm({
                                        title: "¿Correr el guion igual?",
                                        text: "La revisión encontró " + graves + " problema(s) grave(s). Mirá «Revisión» antes de salir.",
                                        accept: "Correr igual", reject: "Revisar",
                                        action: function() { page.ev.arrancar() }
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
                                icon: "■"; compact: true
                                visible: page.ev.estado !== "parado"
                                onClicked: page.ev.parar()
                            }
                            GlassButton {
                                icon: "▤"; compact: true
                                visible: page.ev.estado !== "parado"
                                onClicked: { page.informe = page.ev.informe(); informeDlg.open() }
                                ToolTip.visible: hovered
                                ToolTip.text: "Ver cuánto duró de verdad cada bloque"
                            }
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 8
                            Toggle {
                                checked: page.ev.ensayo
                                enabled: page.ev.estado === "parado"
                                onToggled: (w) => page.ev.setEnsayo(w)
                            }
                            Text {
                                Layout.fillWidth: true
                                wrapMode: Text.WordWrap
                                text: page.ev.ensayo ? "Ensayo: recorre el guion sin tocar lo que sale al aire"
                                                     : "Ensayo apagado: los cues salen al aire de verdad"
                                color: page.ev.ensayo ? Theme.amber : Theme.textDim
                                font.pixelSize: 11
                            }
                        }
                    }
                }

                // Antes de salir: la lista de chequeo manual (lo que no se mide solo).
                Panel {
                    id: chequeoPanel
                    Layout.fillWidth: true
                    Layout.preferredHeight: Math.min(260, 48 + lista.length * 30)
                    title: "Antes de salir · " + lista.filter(x => x.hecho).length + "/" + lista.length
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
                    title: "Revisión"
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
                                text: (modelData.bloque >= 0 ? "Bloque " + (modelData.bloque + 1) +
                                       (modelData.cue >= 0 ? ", cue " + (modelData.cue + 1) : "") + ": " : "") +
                                      modelData.texto
                                color: Theme.textDim
                                font.pixelSize: 11
                            }
                            MouseArea {
                                anchors.fill: parent
                                onClicked: { page.selB = modelData.bloque; page.selC = modelData.cue }
                            }
                        }
                    }
                }

                // El cue elegido: su formulario
                Panel {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    title: page.selCue ? "Cue" : ""
                    visible: true

                    Text {
                        anchors.centerIn: parent
                        visible: !page.selCue
                        width: parent.width - 20
                        wrapMode: Text.WordWrap
                        horizontalAlignment: Text.AlignHCenter
                        text: "Tocá un cue del guion para editarlo.\nDoble clic lo pone al aire."
                        color: Theme.textFaint
                        font.pixelSize: 12
                    }

                    ScrollView {
                        id: cueScroll
                        anchors.fill: parent
                        visible: !!page.selCue
                        clip: true
                        contentWidth: availableWidth
                        ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
                        ColumnLayout {
                            width: cueScroll.availableWidth
                            spacing: 7

                            Text {
                                visible: page.selCue && page.selCue.falta !== ""
                                Layout.fillWidth: true
                                wrapMode: Text.WordWrap
                                text: page.selCue ? "⚠ " + page.selCue.falta : ""
                                color: Theme.live
                                font.pixelSize: 11
                            }

                            Text { text: "Qué dice"; color: Theme.textDim; font.pixelSize: 11 }
                            TextField {
                                Layout.fillWidth: true
                                text: page.selCue ? page.selCue.texto : ""
                                placeholderText: "Lo que vas a leer cuando toque"
                                color: Theme.text
                                font.pixelSize: 12
                                onEditingFinished: page.ev.editarCue(page.selB, page.selC, { texto: text })
                            }

                            // Los campos propios del tipo.
                            Repeater {
                                model: {
                                    if (!page.selCue) return []
                                    const ts = page.ev.tiposDeCue()
                                    for (let i = 0; i < ts.length; i++)
                                        if (ts[i].tipo === page.selCue.tipo) return ts[i].campos
                                    return []
                                }
                                delegate: ColumnLayout {
                                    required property string modelData
                                    Layout.fillWidth: true
                                    spacing: 2
                                    Text { text: modelData; color: Theme.textDim; font.pixelSize: 11 }
                                    TextField {
                                        Layout.fillWidth: true
                                        text: page.selCue && page.selCue.datos[modelData] !== undefined
                                              ? String(page.selCue.datos[modelData]) : ""
                                        color: Theme.text
                                        font.pixelSize: 12
                                        onEditingFinished: {
                                            let d = {}
                                            d[modelData] = text
                                            page.ev.editarCue(page.selB, page.selC, { datos: d })
                                        }
                                    }
                                }
                            }

                            // Un cue de gráfico lleva ADEMÁS los parámetros del
                            // gráfico elegido. Se leen de su ficha, así que un
                            // gráfico nuevo aparece acá sin tocar esta pantalla.
                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 6
                                visible: page.selCue && page.selCue.tipo === "grafico"

                                Text {
                                    Layout.fillWidth: true
                                    wrapMode: Text.WordWrap
                                    color: Theme.textDim
                                    font.pixelSize: 10
                                    text: {
                                        const fs = page.ev.fuentesDeGrafico()
                                        const gs = page.ev.graficosDisponibles()
                                        return "Fuentes en las escenas: " + (fs.length ? fs.join(" · ") : "ninguna todavía") +
                                               "\nGráficos guardados: " + (gs.length ? gs.join(" · ") : "ninguno")
                                    }
                                }

                                Repeater {
                                    model: page.selCue && page.selCue.tipo === "grafico"
                                           ? page.ev.parametrosDeGrafico(page.selCue.datos.grafico || "") : []
                                    delegate: ColumnLayout {
                                        required property var modelData
                                        Layout.fillWidth: true
                                        spacing: 2
                                        Text {
                                            text: modelData.titulo || modelData.clave
                                            color: Theme.textDim; font.pixelSize: 11
                                        }
                                        TextField {
                                            Layout.fillWidth: true
                                            color: Theme.text
                                            font.pixelSize: 12
                                            // Vacío = queda el valor por defecto del gráfico, y se
                                            // muestra como marca de agua para que se sepa cuál es.
                                            placeholderText: String(modelData.porDefecto !== undefined
                                                                    ? modelData.porDefecto : "")
                                            text: {
                                                const v = page.selCue && page.selCue.datos.valores
                                                if (!v || v[modelData.clave] === undefined) return ""
                                                return String(v[modelData.clave])
                                            }
                                            onEditingFinished: page.ev.setValorDeCue(page.selB, page.selC,
                                                                                     modelData.clave, text)
                                        }
                                    }
                                }
                            }

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 8
                                ColumnLayout {
                                    spacing: 2
                                    Text { text: "Dura (seg)"; color: Theme.textDim; font.pixelSize: 11 }
                                    TextField {
                                        Layout.preferredWidth: 90
                                        text: page.selCue ? String(page.selCue.duracionSeg) : "0"
                                        color: Theme.text
                                        font.pixelSize: 12
                                        validator: IntValidator { bottom: 0; top: 86400 }
                                        onEditingFinished: page.ev.editarCue(page.selB, page.selC,
                                                                             { duracionSeg: parseInt(text) || 0 })
                                    }
                                }
                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 2
                                    Text { text: "Encadena solo"; color: Theme.textDim; font.pixelSize: 11 }
                                    Toggle {
                                        checked: page.selCue ? page.selCue.automatico : false
                                        onToggled: (w) => page.ev.editarCue(page.selB, page.selC, { automatico: w })
                                    }
                                }
                            }

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 6
                                GlassButton {
                                    text: "Poner al aire"; compact: true
                                    onClicked: page.ev.irA(page.selB, page.selC)
                                }
                                Item { Layout.fillWidth: true }
                                GlassButton {
                                    icon: "✕"; text: "Quitar"; compact: true
                                    onClicked: { page.ev.quitarCue(page.selB, page.selC); page.selC = -1 }
                                }
                            }
                        }
                    }
                }

                // Bitacora: lo que fue pasando
                Panel {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 130
                    visible: page.ev.bitacora.length > 0
                    title: "Lo que pasó"
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

    // --- Informe: previsto contra real ---------------------------------------
    Dialog {
        id: informeDlg
        anchors.centerIn: parent
        width: Math.min(page.width - 80, 620)
        modal: true
        title: "Lo que duró de verdad"
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
                      ? "Previsto " + page.mmss(page.informe.previstoSeg) + " · real " +
                        page.mmss(page.informe.realSeg) + " · " +
                        (page.informe.desvioSeg >= 0 ? "nos pasamos " : "sobró ") +
                        page.mmss(Math.abs(page.informe.desvioSeg)) +
                        (page.informe.ensayo ? "  (fue un ensayo)" : "")
                      : "El guion todavía no se corrió."
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
                text: "Guardar en la carpeta del evento"; compact: true
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
        title: "Cuándo es el evento"
        standardButtons: Dialog.Ok | Dialog.Cancel
        onAboutToShow: {
            fechaCampo.text = page.ev.guion.fecha || ""
            horaCampo.text = page.ev.guion.inicio || ""
            husoCampo.text = page.ev.guion.huso || ""
        }
        onAccepted: {
            page.ev.setFecha(fechaCampo.text.trim())
            page.ev.setInicio(horaCampo.text.trim())
            page.ev.setHuso(husoCampo.text.trim())
        }
        ColumnLayout {
            anchors.fill: parent
            spacing: 8
            Text {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                text: "De la hora salen las horas previstas de cada bloque. De la fecha con el huso " +
                      "sale el momento exacto, que es lo único que le sirve al que se conecta desde " +
                      "otro huso horario."
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
                    text: "El de acá"
                    onClicked: husoCampo.text = page.ev.husoDelAparato()
                }
            }
        }
    }

    Dialog {
        id: nuevoDlg
        anchors.centerIn: parent
        width: 420
        modal: true
        title: "Evento nuevo"
        standardButtons: Dialog.Ok | Dialog.Cancel
        onAboutToShow: nombreCampo.text = ""
        onAccepted: if (nombreCampo.text.trim() !== "") page.ev.crear(nombreCampo.text.trim())
        ColumnLayout {
            anchors.fill: parent
            spacing: 8
            Text {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                text: "Se crea una carpeta con este nombre adentro de Eventos/, con lugar para su música, " +
                      "sus videos y sus discursos."
                color: Theme.textDim
                font.pixelSize: 12
            }
            TextField {
                id: nombreCampo
                Layout.fillWidth: true
                placeholderText: "Acto del 25 de mayo"
                color: Theme.text
                onAccepted: nuevoDlg.accept()
            }
        }
    }

    // --- Elegir el tipo de cue ------------------------------------------------
    Dialog {
        id: tipoDlg
        anchors.centerIn: parent
        width: 460
        modal: true
        title: "¿Qué pasa en este cue?"
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
                        }
                    }
                }
            }
        }
    }
}
