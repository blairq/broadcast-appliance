// Ventana principal: riel de navegacion + paginas + avisos + dialogos globales.
// Solo presentacion: todo el estado y los comandos vienen del singleton Studio.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import BA
import "controls"
import "pages"

ApplicationWindow {
    id: win
    objectName: "ba:principal" // ver PageWindow.qml
    visible: true
    width: 1600
    height: 920
    minimumWidth: 1100
    minimumHeight: 700
    title: "Broadcast Appliance"
    color: Theme.bg
    font.family: Theme.font

    palette {
        window: Theme.panel
        windowText: Theme.text
        base: "#1b1b1f"
        alternateBase: "#222226"
        text: Theme.text
        button: "#26262b"
        buttonText: Theme.text
        highlight: Theme.accent
        highlightedText: "#ffffff"
        mid: "#3a3a40"
        dark: "#101012"
        light: "#44444b"
        placeholderText: Theme.textFaint
        toolTipBase: "#26262b"
        toolTipText: Theme.text
    }

    property int page: 0

    // Cerrar la ventana principal cierra la aplicacion aunque haya ventanas de
    // salida abiertas (esas se recuerdan y vuelven al proximo arranque).
    // Con algo en curso (emision, grabacion, gente en la sala) antes se pregunta.
    property bool quitting: false // el cierre ya esta decidido: la guardia no pregunta
    // Todos los conectados: lobby, programa y fila de oradores (SettingsPage usa lo mismo).
    readonly property int peopleConnected: Studio.conference.peopleConnected
    onClosing: (close) => {
        if (quitting || !(Studio.streaming || Studio.recording || peopleConnected > 0)) { Qt.quit(); return }
        close.accepted = false
        // Diferido: QGuiApplication::quit() tambien cierra las ventanas, asi que
        // un cierre pedido por la app pasa por aca; su quitRequested llega en
        // la misma vuelta y askClose() ya no pregunta.
        Qt.callLater(askClose)
    }
    function askClose() {
        if (quitting) return
        const n = peopleConnected
        let what = Studio.streaming && Studio.recording ? "Estás AL AIRE y grabando"
                 : Studio.streaming ? "Estás AL AIRE" : Studio.recording ? "Estás grabando" : ""
        if (n > 0) {
            const g = n === 1 ? "hay 1 persona en la sala" : "hay " + n + " personas en la sala"
            what = what === "" ? "Hay" + g.substring(3)
                               : what + (Studio.streaming && Studio.recording ? ", y " : " y ") + g
        }
        const lost = []
        if (Studio.streaming) lost.push("se corta la emisión")
        if (Studio.recording) lost.push("se detiene la grabación")
        if (n > 0) lost.push(n === 1 ? "esa persona queda afuera" : "esas personas quedan afuera")
        const last = lost.pop()
        confirm({
            title: "¿Cerrar Broadcast Appliance?",
            text: what + ". Si cerrás, " + (lost.length > 0 ? lost.join(", ") + " y " : "") + last + ".",
            accept: "Cerrar",
            reject: "Cancelar",
            action: function() { win.quitting = true; Qt.quit() }
        })
    }

    // --- API para las tarjetas y paginas ------------------------------------------
    function showPage(i) { page = i }
    function toast(message, level) { toasts.show(message, level) }

    /// Le manda un pedido al asistente con el contexto de la pagina que esta a
    /// la vista ya puesto. Es lo que usan los botones de las paginas (por
    /// ejemplo los de un grafico) para no tener que armar el contexto cada uno.
    function pedirAlAsistente(texto) { barraIA.mandar(texto) }
    function openStreamDialog() {
        if (Studio.streamServer && Studio.streamKeyAvailable) Studio.startStreaming("", "")
        else streamDlg.open()
    }
    function confirm(opts) { confirmDlg.ask(opts) }
    function confirmStopStream() {
        confirm({
            title: "¿Cortar la transmisión?",
            text: "La emisión en vivo se va a detener.",
            accept: "Cortar",
            reject: "Cancelar",
            action: function() { Studio.stopStreaming() }
        })
    }

    Connections {
        target: Studio
        function onNotify(message, level) { toasts.show(message, level) }
        // Cierre pedido por la app (launcher por el canal de control,
        // actualizacion, MCP): nunca se pregunta. Si la guardia llego a frenar
        // ese quit, se repite ya sin guardia.
        function onQuitRequested() {
            win.quitting = true
            confirmDlg.close()
            Qt.quit()
        }
    }

    // --- Paginas ------------------------------------------------------------------
    // --- El lugar que ocupa la barra de navegacion -----------------------------
    // Todo lo que flota sobre las paginas (el zocalo del asistente, la barra
    // del operador, los avisos, las pildoras de estado) tiene que esquivarla.
    // Antes cada uno lo resolvia por su cuenta —uno anclaba a `rail.right`,
    // otro tenia un 92 escrito a mano— y eso solo funcionaba con la barra a la
    // izquierda y de 64 px: al moverla, el zocalo del asistente se aplastaba
    // hasta desaparecer. Ahora salen todos de estas cuatro cuentas.
    readonly property real rielLugar: Studio.riel.grosor + 28
    readonly property real libreIzq: Studio.riel.posicion === "izquierda" ? rielLugar : 0
    readonly property real libreDer: Studio.riel.posicion === "derecha" ? rielLugar : 0
    readonly property real libreArr: Studio.riel.posicion === "arriba" ? rielLugar : 0
    readonly property real libreAba: Studio.riel.posicion === "abajo" ? rielLugar : 0

    /// Cuanto margen tiene que dejar cada pagina a la izquierda. Con el riel a
    /// la izquierda, el riel FLOTA sobre ese margen; en los otros lados el
    /// lugar ya lo reserva el StackLayout y la pagina solo deja un aire.
    readonly property real insetPagina: win.libreIzq > 0 ? win.libreIzq : 14

    StackLayout {
        id: paginas
        anchors.fill: parent
        // Le deja lugar al zocalo del asistente: si no, la barra taparia el
        // ultimo control de cada pagina justo cuando hace falta apretarlo.
        anchors.bottomMargin: (barraIA.visible ? barraIA.height + 14 : 0) + win.libreAba
        // A la izquierda el riel FLOTA sobre el margen que cada pagina ya deja
        // (su `leftInset`); en los otros tres lados hay que reservarle lugar de
        // verdad, porque ahi las paginas no dejan nada.
        anchors.topMargin: win.libreArr
        anchors.rightMargin: win.libreDer
        currentIndex: win.page
        DashboardPage { leftInset: win.insetPagina }
        ScenesPage { leftInset: win.insetPagina }
        ConferencePage { leftInset: win.insetPagina }
        YouTubePage { leftInset: win.insetPagina }
        TwitchPage { leftInset: win.insetPagina }
        FacebookPage { leftInset: win.insetPagina }
        SettingsPage { leftInset: win.insetPagina }
        AssistantPage { leftInset: win.insetPagina }
        InputsPage { leftInset: win.insetPagina }
        AudioPage { leftInset: win.insetPagina }
        EventoPage { leftInset: win.insetPagina }
        GraficosPage { leftInset: win.insetPagina }
        RotulosPage { leftInset: win.insetPagina }
        RegistroPage { leftInset: win.insetPagina }
        AgentesPage { leftInset: win.insetPagina }
        PrompterPage { leftInset: win.insetPagina }
        PantallasPage { leftInset: win.insetPagina }
        QrPage { leftInset: win.insetPagina }
    }

    // --- Pestañas sacadas a su propia ventana --------------------------------------
    // No se saca la pagina de aca: se abre OTRA VISTA de lo mismo (todas leen
    // su estado de los Studio.*, asi que se mantienen sincronizadas solas).
    // Se abre con clic derecho en el riel.
    Component { id: cmpDashboard;  DashboardPage {} }
    Component { id: cmpScenes;     ScenesPage {} }
    Component { id: cmpConference; ConferencePage {} }
    Component { id: cmpYouTube;    YouTubePage {} }
    Component { id: cmpTwitch;     TwitchPage {} }
    Component { id: cmpFacebook;   FacebookPage {} }
    Component { id: cmpSettings;   SettingsPage {} }
    Component { id: cmpAssistant;  AssistantPage {} }
    Component { id: cmpInputs;     InputsPage {} }
    Component { id: cmpAudio;      AudioPage {} }
    Component { id: cmpEvento;     EventoPage {} }
    Component { id: cmpGraficos;   GraficosPage {} }
    Component { id: cmpRotulos;    RotulosPage {} }
    Component { id: cmpRegistro;   RegistroPage {} }
    Component { id: cmpAgentes;    AgentesPage {} }
    Component { id: cmpPrompter;   PrompterPage {} }
    Component { id: cmpPantallas;  PantallasPage {} }
    Component { id: cmpQr;         QrPage {} }

    function componenteDePagina(i) {
        return [cmpDashboard, cmpScenes, cmpConference, cmpYouTube, cmpTwitch, cmpFacebook,
                cmpSettings, cmpAssistant, cmpInputs, cmpAudio, cmpEvento, cmpGraficos,
                cmpRotulos, cmpRegistro, cmpAgentes, cmpPrompter, cmpPantallas, cmpQr][i] || null
    }

    ListModel { id: sueltas }

    function sacarAVentana(i, titulo) {
        // Si ya esta afuera, se la trae al frente en vez de abrir otra igual.
        for (let k = 0; k < sueltas.count; ++k) {
            if (sueltas.get(k).page === i) { toasts.show("«" + titulo + "» ya está en su ventana", "info"); return }
        }
        sueltas.append({ page: i, titulo: titulo, idPagina: Studio.riel.idDePagina(i) })
    }

    Instantiator {
        model: sueltas
        delegate: PageWindow {
            required property int index
            page: sueltas.get(index) ? sueltas.get(index).page : 0
            titulo: sueltas.get(index) ? sueltas.get(index).titulo : ""
            idPagina: sueltas.get(index) ? sueltas.get(index).idPagina : ""
            contenido: win.componenteDePagina(page)
            onCerrada: (p) => {
                for (let k = 0; k < sueltas.count; ++k)
                    if (sueltas.get(k).page === p) { sueltas.remove(k); return }
            }
        }
    }

    // Los carteles que dicen cuál monitor es cuál (Studio.identificarPantallas).
    IdentificarPantallas { id: identificar }
    Connections {
        target: Studio
        function onIdentificarPedido() { identificar.mostrar() }
    }

    // --- Ventanas de salida (una por entrada del modelo) ---------------------------
    Instantiator {
        model: Studio.dashboard.windows
        delegate: OutputWindow {}
    }

    // --- Riel de navegacion (vidrio flotante) ---------------------------------------
    // SIN ANCHORS CONDICIONALES, y es la lección cara de este archivo: poner
    // `anchors.right: cond ? parent.right : undefined` parece que alterna, pero
    // al volver de horizontal a vertical el ancla vieja no se suelta, el item
    // queda atado a dos lados opuestos y se estira hasta ocupar la ventana
    // entera (se vio: el botón del asistente convertido en una mancha naranja
    // de pared a pared). Para cambiar anclas en vivo hay que usar estados con
    // AnchorChanges; acá directamente no hacen falta: con x/y/ancho/alto
    // calculados, cada posición es una cuenta y no hay estado que limpiar.
    //
    // Adentro, un solo GridLayout que fluye en un sentido o en el otro: el
    // logo, la tira de páginas, el asistente y los indicadores son los mismos
    // cuatro hijos en las cuatro posiciones.
    Rectangle {
        id: rail
        // Por encima de TODO lo que flota sobre las paginas (avisos, barra del
        // operador, pildoras de estado): es como se navega, y una barra tapada
        // deja al operador sin salida justo cuando necesita cambiar de
        // pantalla. Lo unico que puede ponerse arriba es un dialogo modal, que
        // es una interrupcion deliberada y se cierra.
        z: 100
        readonly property var cfg: Studio.riel
        readonly property bool vert: cfg.vertical
        readonly property int margen: 14

        x: cfg.posicion === "derecha" ? parent.width - width - margen : margen
        y: cfg.posicion === "abajo" ? parent.height - height - margen : margen
        width: vert ? cfg.grosor : parent.width - margen * 2
        height: vert ? parent.height - margen * 2 : cfg.grosor
        radius: cfg.grosor / 2
        color: Qt.rgba(0.06, 0.06, 0.07, 0.72)
        border.color: Theme.glassBorder

        // El catalogo de paginas (cual, que icono, en que grupo) vive en C++
        // —ver ui/RielConfig.hpp— porque Configuracion tambien lo necesita para
        // ofrecer que mostrar. Repetido en dos lados se desincroniza.
        readonly property var grupos: rail.cfg.grupos

        GridLayout {
            anchors.fill: parent
            anchors.margins: 11
            flow: rail.vert ? GridLayout.TopToBottom : GridLayout.LeftToRight
            columns: rail.vert ? 1 : 4
            rows: rail.vert ? 4 : 1
            rowSpacing: 12
            columnSpacing: 12

            // 1) El logotipo.
            Rectangle {
                Layout.preferredWidth: rail.cfg.tamano
                Layout.preferredHeight: rail.cfg.tamano
                Layout.alignment: Qt.AlignCenter
                radius: width / 2
                color: Theme.accent
                Text {
                    anchors.centerIn: parent
                    text: "BA"
                    color: "white"
                    font { pixelSize: Math.round(rail.cfg.tamano * 0.35); weight: Font.Black }
                }
            }

            // 2) Las paginas, agrupadas y desplazables. Con catorce ya no
            // entran en una pantalla de portatil: grupos con un separador fino
            // —para poder apuntar a lo que uno busca sin leer los catorce— y la
            // tira que se desplaza cuando no entra, con un degradado que avisa.
            Flickable {
                id: tira
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                contentHeight: rail.vert ? filas.implicitHeight : height
                contentWidth: rail.vert ? width : filas.implicitWidth
                boundsBehavior: Flickable.StopAtBounds
                // Sin barra de desplazamiento: el riel es angosto y se comeria
                // el lugar del icono. Los degradados ya dicen que hay mas.
                interactive: rail.vert ? contentHeight > height : contentWidth > width

                GridLayout {
                    id: filas
                    flow: rail.vert ? GridLayout.TopToBottom : GridLayout.LeftToRight
                    rowSpacing: 8
                    columnSpacing: 8
                    // El layout se mide solo, y la tira le fija UNICAMENTE el
                    // lado cruzado. Escribirlo como `width: vert ? tira.width :
                    // implicitWidth` arma un bucle de bindings (el ancho
                    // dependia de su propio ancho implicito, que depende de
                    // como quedaron los hijos, que dependen del ancho). Con
                    // `Binding on ... when` solo hay una atadura activa por vez
                    // y al soltarla vuelve sola a medirse.
                    width: implicitWidth
                    height: implicitHeight
                    Binding on width { when: rail.vert; value: tira.width }
                    Binding on height { when: !rail.vert; value: tira.height }

                    Repeater {
                        model: rail.grupos
                        delegate: GridLayout {
                            id: grupo
                            required property var modelData
                            required property int index
                            flow: rail.vert ? GridLayout.TopToBottom : GridLayout.LeftToRight
                            rowSpacing: 8
                            columnSpacing: 8
                            Layout.fillWidth: rail.vert
                            Layout.fillHeight: !rail.vert

                            // Separador entre grupos (en el primero no va).
                            Item {
                                visible: grupo.index !== 0 && rail.cfg.separadores
                                Layout.preferredWidth: rail.vert ? rail.cfg.tamano : (visible ? 9 : 0)
                                Layout.preferredHeight: rail.vert ? (visible ? 9 : 0) : rail.cfg.tamano
                                Rectangle {
                                    anchors.centerIn: parent
                                    width: rail.vert ? 22 : 1
                                    height: rail.vert ? 1 : 22
                                    color: Qt.rgba(1, 1, 1, 0.14)
                                }
                            }

                            Repeater {
                                model: grupo.modelData.items
                                // La CELDA mide lo mismo que el boton y el
                                // boton se centra adentro. Todas las celdas de
                                // todos los grupos miden igual, asi que las
                                // columnas quedan alineadas: el primer grupo se
                                // veia corrido porque, al ser el unico sin
                                // separador visible, su columna medía distinto.
                                delegate: Item {
                                    id: celda
                                    required property var modelData
                                    Layout.preferredWidth: rail.cfg.tamano
                                    Layout.preferredHeight: rail.cfg.tamano
                                    Layout.alignment: Qt.AlignCenter

                                    Rectangle {
                                        anchors.fill: parent
                                        radius: width / 2
                                        color: win.page === celda.modelData.page ? "white"
                                             : (navMa.containsMouse ? Qt.rgba(1, 1, 1, 0.12) : "transparent")
                                        border.color: Qt.rgba(1, 1, 1, 0.16)
                                        Behavior on color { ColorAnimation { duration: 150 } }
                                        Text {
                                            anchors.centerIn: parent
                                            text: celda.modelData.icon
                                            color: win.page === celda.modelData.page ? "#111" : Theme.text
                                            // El icono acompaña al tamaño del
                                            // boton: si no, agrandar la barra
                                            // deja un icono chico en el medio.
                                            font.pixelSize: Math.round(rail.cfg.tamano * 0.43)
                                        }
                                        MouseArea {
                                            id: navMa
                                            anchors.fill: parent
                                            hoverEnabled: true
                                            cursorShape: Qt.PointingHandCursor
                                            acceptedButtons: Qt.LeftButton | Qt.RightButton
                                            onClicked: (m) => {
                                                if (m.button === Qt.RightButton)
                                                    win.sacarAVentana(celda.modelData.page, celda.modelData.titulo)
                                                else win.page = celda.modelData.page
                                            }
                                        }
                                        ToolTip.visible: navMa.containsMouse
                                        ToolTip.text: celda.modelData.tip + "\n(clic derecho: sacar a su propia ventana)"
                                        ToolTip.delay: 400
                                    }
                                }
                            }
                        }
                    }
                }

                // Los degradados que avisan que la tira sigue. Solo cuando hay
                // algo mas para ese lado: si no, son manchas que no dicen nada.
                Rectangle {
                    parent: tira
                    x: 0; y: 0
                    width: rail.vert ? tira.width : 18
                    height: rail.vert ? 18 : tira.height
                    visible: rail.vert ? tira.contentY > 2 : tira.contentX > 2
                    gradient: Gradient {
                        orientation: rail.vert ? Gradient.Vertical : Gradient.Horizontal
                        GradientStop { position: 0; color: Qt.rgba(0.06, 0.06, 0.07, 0.95) }
                        GradientStop { position: 1; color: "transparent" }
                    }
                }
                Rectangle {
                    parent: tira
                    x: rail.vert ? 0 : tira.width - width
                    y: rail.vert ? tira.height - height : 0
                    width: rail.vert ? tira.width : 18
                    height: rail.vert ? 18 : tira.height
                    visible: rail.vert ? tira.contentHeight - tira.contentY - tira.height > 2
                                       : tira.contentWidth - tira.contentX - tira.width > 2
                    gradient: Gradient {
                        orientation: rail.vert ? Gradient.Vertical : Gradient.Horizontal
                        GradientStop { position: 0; color: "transparent" }
                        GradientStop { position: 1; color: Qt.rgba(0.06, 0.06, 0.07, 0.95) }
                    }
                }
            }

            // 3) El asistente. Fijo, fuera de la tira: es el que se busca a las
            // apuradas y no puede moverse con el desplazamiento.
            Rectangle {
                id: aiBtn
                Layout.preferredWidth: rail.cfg.tamano
                Layout.preferredHeight: rail.cfg.tamano
                Layout.alignment: Qt.AlignCenter
                radius: width / 2
                gradient: Gradient {
                    GradientStop { position: 0; color: win.page === 7 ? "#ffffff" : "#ff8a3d" }
                    GradientStop { position: 1; color: win.page === 7 ? "#ffe2cf" : "#c2410c" }
                }
                border.color: Qt.rgba(1, 1, 1, 0.3)
                Text {
                    anchors.centerIn: parent
                    text: "✦"
                    color: win.page === 7 ? "#c2410c" : "white"
                    font.pixelSize: Math.round(rail.cfg.tamano * 0.48)
                }
                Rectangle { // corriendo
                    visible: Studio.agent.running
                    width: 10; height: 10; radius: 5; color: Theme.ok; border.color: "#111"
                    anchors { right: parent.right; top: parent.top }
                }
                MouseArea {
                    id: aiMa
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: win.page = 7
                }
                ToolTip.visible: aiMa.containsMouse
                ToolTip.text: "Asistente (chat con la IA que maneja la app)"
                ToolTip.delay: 400
            }

            // 4) Indicadores rapidos: al aire, grabando, sala.
            Grid {
                columns: rail.vert ? 1 : 3
                spacing: 10
                Layout.alignment: Qt.AlignCenter
                Repeater {
                    model: [
                        { on: Studio.streaming, c: Theme.live,   si: "Al aire",      no: "Fuera del aire" },
                        { on: Studio.recording, c: Theme.accent, si: "Grabando",     no: "Sin grabar" },
                        { on: Studio.gatewayAlive, c: Theme.ok,  si: "Sala en línea", no: "Sala sin conexión" }
                    ]
                    delegate: Rectangle {
                        id: punto
                        required property var modelData
                        width: 10; height: 10; radius: 5
                        color: modelData.on ? modelData.c : Theme.textFaint
                        ToolTip.visible: puntoMa.containsMouse
                        ToolTip.text: punto.modelData.on ? punto.modelData.si : punto.modelData.no
                        MouseArea {
                            id: puntoMa
                            anchors.fill: parent
                            anchors.margins: -6
                            hoverEnabled: true
                        }
                    }
                }
            }
        }
    }

    // --- El zocalo para hablarle al asistente, en todas las paginas ------------------
    // El contexto se arma ACA porque es el unico lugar que sabe que pagina
    // esta a la vista y puede leer de los Studio.* que esta mirando el
    // operador. Sin contexto, «achicalo un poco» no quiere decir nada.
    function contextoDePagina(i) {
        switch (i) {
        case 0: return "Estoy en el Dashboard, mirando el programa y las salidas."
        case 1: return "Estoy en Escenas y fuentes. La escena al aire es «" + Studio.currentScene + "»."
        case 2: return "Estoy en la Sala (lobby, invitados y chat)."
        case 3: return "Estoy en la pagina de YouTube (cuenta, videos y vivos)."
        case 4: return "Estoy en la pagina de Twitch."
        case 5: return "Estoy en la pagina de Facebook."
        case 6: return "Estoy en Configuracion."
        case 8: return "Estoy en Entradas de video (camaras, capturadoras y red)."
        case 9: return "Estoy en Audio, en el patchbay de modulos por fuente."
        case 10: return "Estoy en el Guion del evento" +
                        (Studio.evento.abierto ? " «" + Studio.evento.abierto + "»." : ", sin ningun evento abierto.")
        case 11: return Studio.grafico.abierto !== ""
                        ? "Estoy en el editor de Graficos, editando «" + Studio.grafico.abierto + "»" +
                          (Studio.grafico.declarativo ? " (declarativo: se edita su descripcion en grafico.json)."
                                                      : " (QML escrito a mano).")
                        : "Estoy en el editor de Graficos, sin ninguno abierto."
        case 12: return "Estoy en Rotulos. La escena al aire es «" + Studio.rotulador.escena + "»."
        case 13: return "Estoy en el visor del Registro."
        }
        return ""
    }

    // Los pedidos ya escritos de cada pagina: lo que uno va a querer pedir
    // estando ahi. Son los que antes vivian adentro de cada pagina.
    function sugerenciasDePagina(i) {
        switch (i) {
        case 0: return [ { t: "¿Como venimos?", p: "Deci en dos lineas como esta todo ahora: aire, grabacion, sala y alertas." },
                         { t: "Revisar antes de salir", p: "Revisa todo lo que conviene mirar antes de salir al aire y deci que esta mal." } ]
        case 1: return [ { t: "Escena nueva", p: "Arma una escena nueva parecida a la que esta al aire y deci como te quedo." },
                         { t: "¿Que hay en esta escena?", p: "Deci que fuentes tiene la escena al aire y para que sirve cada una." } ]
        case 2: return [ { t: "¿Quien esta?", p: "Deci quien esta conectado en la sala, quien espera y quien esta al aire." },
                         { t: "Pasar al que sigue", p: "Pone al aire al siguiente de la cola de oradores." } ]
        case 9: return [ { t: "Se escucha mal", p: "Mira los niveles de audio y deci que fuente esta mal y que le harias." } ]
        case 10: return [ { t: "Revisar el guion", p: "Revisa el guion y deci que le falta a cada cue para poder correrlo." },
                          { t: "¿Como vamos de tiempo?", p: "Deci como vamos de tiempo contra lo previsto." } ]
        case 11: return [ { t: "Que entre mas rapido", p: "Hace que entre mas rapido, alrededor de 300 ms." },
                          { t: "Mas chico", p: "Achicalo un 20 % sin que el texto quede apretado." },
                          { t: "Probar un nombre largo", p: "Probalo con un nombre bien largo y fijate si se sale de la caja." },
                          { t: "Proponer botones", p: "Propone los botones («acciones») de este grafico y escribilos en su grafico.json: cada uno con titulo corto, icono de un caracter y prompt redactado como se lo pediria el operador en vivo." } ]
        case 12: return [ { t: "Poner al que habla", p: "Pone en el zocalo de esta escena el nombre y el cargo del que esta al aire, y sacalo al aire." },
                          { t: "Que se mantenga solo", p: "Ata este rotulo al orador que este al aire, para que se actualice solo cuando cambie." } ]
        case 13: return [ { t: "¿Que se rompio?", p: "Mira el registro y deci que fallo, cuando y por que. Empeza por los avisos y errores." },
                          { t: "Mas detalle", p: "Baja el nivel de registro a depuracion asi queda mas detalle de lo que viene." } ]
        }
        return []
    }

    BarraAsistente {
        id: barraIA
        // En la pestaña del Asistente no va: ahi esta el chat de verdad, con
        // su configuracion, y dos cajas para lo mismo confunden.
        visible: win.page !== 7
        // A la VENTANA, no a la barra: anclarlo a `rail.right` funcionaba solo
        // con la barra a la izquierda; con la barra a la derecha o arriba,
        // `rail.right` es el borde de la ventana y el zocalo se aplastaba
        // hasta desaparecer.
        anchors {
            left: parent.left; right: parent.right; bottom: parent.bottom
            leftMargin: win.libreIzq + 20
            rightMargin: win.libreDer + 20
            bottomMargin: win.libreAba + 14
        }
        contexto: win.contextoDePagina(win.page)
        sugerencias: win.sugerenciasDePagina(win.page)
        onMandado: (texto) => toasts.show("Pedido mandado. La respuesta aparece en el Asistente.", "info")
    }

    // --- Estado de emision, visible en todas las paginas ------------------------------
    // Va en la franja libre de arriba (las paginas arrancan en y = 18 o mas): no
    // tapa ningun control. El Dashboard ya trae sus pildoras en el encabezado.
    Item {
        visible: win.page !== 0
        anchors { top: parent.top; horizontalCenter: parent.horizontalCenter
                  topMargin: win.libreArr + 1 }
        z: 50
        width: airState.width
        height: airState.height
        Row {
            id: airState
            spacing: 8
            // Reconectando gana sobre "al aire": para libobs la salida sigue
            // activa, pero no esta llegando nada y el operador tiene que saberlo.
            StatusPill {
                height: 16
                text: Studio.reconnecting ? "RECONECTANDO"
                      : Studio.streaming ? "AL AIRE  " + Studio.streamTime : "FUERA DEL AIRE"
                dot: Studio.reconnecting ? Theme.amber : Theme.live
                on: Studio.streaming || Studio.reconnecting
                pulse: true
                ToolTip.visible: pillMa.containsMouse && Studio.outputProblem !== ""
                ToolTip.text: Studio.outputProblem
                MouseArea { id: pillMa; anchors.fill: parent; hoverEnabled: true }
            }
            StatusPill { height: 16; visible: Studio.recording; text: "REC  " + Studio.recordTime; dot: Theme.accent; pulse: true }
        }
        MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: win.page = 0 }
    }

    // --- Reloj del orador al aire (modo oradores), visible en todas las paginas ------
    // Debajo de la franja del estado de emision, para no taparla.
    OnAirTimer {
        anchors { top: parent.top; horizontalCenter: parent.horizontalCenter
                  topMargin: win.libreArr + 20 }
        z: 50
        onClicked: win.page = 2
    }

    // --- Barra del asistente al operador (mensajes, preguntas, respuestas a invitados) ----
    OperatorBar {
        id: opBar
        anchors { left: parent.left; right: parent.right; bottom: parent.bottom
                  leftMargin: win.libreIzq + 12; rightMargin: win.libreDer + 18
                  bottomMargin: win.libreAba + 14 }
        z: 60
    }

    // --- Avisos (toasts) ------------------------------------------------------------
    Column {
        id: toasts
        anchors { right: parent.right; bottom: opBar.visible ? opBar.top : parent.bottom
                  rightMargin: win.libreDer + 22
                  bottomMargin: (opBar.visible ? 0 : win.libreAba) + 22 }
        z: 61
        spacing: 8
        width: 380
        function show(message, level) {
            // El mismo aviso que ya esta en pantalla no se repite (renueva su tiempo).
            for (let i = 0; i < children.length; ++i)
                if (children[i].message === message && children[i].opacity > 0) return children[i].renew()
            toastComp.createObject(toasts, { message: message, level: level || "info" })
        }
    }
    Component {
        id: toastComp
        Rectangle {
            id: t
            property string message
            property string level
            function renew() { life.restart() }
            width: 380
            height: tt.implicitHeight + 26
            radius: 16
            color: Qt.rgba(0.08, 0.08, 0.09, 0.92)
            border.color: level === "error" ? Theme.live : level === "warn" ? Theme.warn : level === "ok" ? Theme.ok : Theme.glassBorder
            opacity: 0
            Component.onCompleted: { opacity = 1; life.start() }
            Behavior on opacity { NumberAnimation { duration: 200 } }
            Text {
                id: tt
                anchors { fill: parent; margins: 13 }
                text: t.message; color: Theme.text; wrapMode: Text.WordWrap; font.pixelSize: 12
            }
            Timer { id: life; interval: t.level === "error" ? 7000 : 3500; onTriggered: { t.opacity = 0; die.start() } }
            Timer { id: die; interval: 250; onTriggered: t.destroy() }
            MouseArea { anchors.fill: parent; onClicked: t.destroy() }
        }
    }

    // --- Dialogos globales -----------------------------------------------------------
    Dialog {
        id: streamDlg
        title: "Salir al aire"
        anchors.centerIn: parent
        modal: true
        standardButtons: Dialog.Ok | Dialog.Cancel
        onOpened: { srvField.text = Studio.streamServer; keyField.text = "" }
        onAccepted: Studio.startStreaming(srvField.text, keyField.text)
        ColumnLayout {
            spacing: 10
            Label { text: "Servidor RTMP" }
            TextField { id: srvField; Layout.preferredWidth: 420; placeholderText: "rtmp://a.rtmp.youtube.com/live2" }
            Label { text: "Clave de transmisión (se guarda cifrada en Credenciales/)" }
            TextField {
                id: keyField; Layout.preferredWidth: 420; echoMode: TextInput.Password
                placeholderText: Studio.streamKeySaved ? "Vacío = la clave de la última vez" : ""
            }
        }
    }
    ConfirmDialog { id: confirmDlg }
}
