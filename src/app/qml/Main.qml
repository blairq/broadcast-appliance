// Ventana principal: riel de navegacion + paginas + avisos + dialogos globales.
// Solo presentacion: todo el estado y los comandos vienen del singleton Studio.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import BA
import "controls"
import "cards"
import "pages"

ApplicationWindow {
    id: win
    // Modo prueba (--test): el MCP no pide aprobaciones. Siempre a la vista.
    Rectangle {
        visible: Studio.modoTest
        z: 100000
        anchors { bottom: parent.bottom; horizontalCenter: parent.horizontalCenter; bottomMargin: 4 }
        width: avisoTest.implicitWidth + 24; height: 20; radius: 4
        color: "#c62828"
        Text { id: avisoTest; anchors.centerIn: parent; color: "white"; font.pixelSize: 11; font.bold: true
               text: "MODO PRUEBA (--test): el asistente actúa SIN pedir aprobación, ni siquiera en el aire" }
    }
    objectName: "ba:principal" // ver PageWindow.qml
    visible: true
    width: 1600
    height: 920
    minimumWidth: 1100
    minimumHeight: 700
    title: "Broadcast Appliance"
    color: Theme.bg
    font.family: Theme.font

    // Los controles de Qt, en oscuro. Los colores salen de Theme porque esta
    // misma paleta la necesitan las tres ventanas (ver Theme.qml).
    palette {
        window: Theme.panel
        windowText: Theme.text
        base: Theme.paletaBase
        alternateBase: Theme.paletaAlterna
        text: Theme.text
        button: Theme.paletaBoton
        buttonText: Theme.text
        highlight: Theme.accent
        highlightedText: "#ffffff"
        mid: Theme.paletaMedio
        dark: Theme.paletaOscura
        light: Theme.paletaClara
        placeholderText: Theme.textFaint
        toolTipBase: Theme.paletaBoton
        toolTipText: Theme.text
    }

    property int page: 0
    // Modo en vivo: las páginas protegidas piden escribir CONFIRMAR (con un motivo) para entrar.
    property int paginaPermitida: 0
    onPageChanged: {
        const id = Studio.riel.idDePagina(page)
        if (Studio.paginaBloqueada(id)) {
            desbloqueo.pagina = page
            desbloqueo.idPagina = id
            page = paginaPermitida
            desbloqueo.open()
        } else {
            paginaPermitida = page
        }
    }
    Dialog {
        id: desbloqueo
        property int pagina: 0
        property string idPagina: ""
        anchors.centerIn: parent
        width: 440
        modal: true
        title: "Estamos en vivo"
        onOpened: { confirmarF.text = ""; motivoF.text = ""; confirmarF.forceActiveFocus() }
        contentItem: ColumnLayout {
            spacing: 8
            Text {
                Layout.fillWidth: true; wrapMode: Text.WordWrap
                text: "«" + Studio.riel.tituloDePagina(desbloqueo.pagina) + "» tiene cosas que pueden cortar el aire. " +
                      "Para entrar escribí CONFIRMAR y por qué (queda en el registro)."
                color: Theme.text; font.pixelSize: 13
            }
            TextField { id: confirmarF; Layout.fillWidth: true; placeholderText: "CONFIRMAR"; color: Theme.text }
            TextField { id: motivoF; Layout.fillWidth: true; placeholderText: "Motivo (p. ej. cambiar la sala de los invitados)"; color: Theme.text }
            RowLayout {
                Item { Layout.fillWidth: true }
                GlassButton { compact: true; text: "Cancelar"; onClicked: desbloqueo.close() }
                GlassButton {
                    compact: true; text: "Entrar"; active: true
                    enabled: confirmarF.text.trim().toUpperCase() === "CONFIRMAR" && motivoF.text.trim().length >= 3
                    onClicked: {
                        Studio.desbloquearPagina(desbloqueo.idPagina, motivoF.text.trim())
                        desbloqueo.close()
                        win.page = desbloqueo.pagina
                    }
                }
            }
        }
    }

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
        function onPedirConfirmarGrabacion(horas) {
            confirm({
                title: "Queda muy poco disco",
                text: "Alcanza para ~" + Math.round(horas * 60) + " minutos de grabación a este bitrate. ¿Grabar igual?",
                accept: "Grabar igual", reject: "Cancelar",
                action: function() { Studio.grabarIgual() }
            })
        }
        // Ya al aire y se pidió otro destino: nunca se cambia sin preguntar.
        function onPedirCambioDestino(actual, nuevoServer, nuevoKey) {
            confirm({
                title: "Ya estás transmitiendo",
                text: "Sale a " + actual + ". ¿Cambiar a " + nuevoServer + "? La transmisión se corta un instante.",
                accept: "Cambiar de destino", reject: "Seguir como está",
                action: function() { Studio.cambiarDestinoStream(nuevoServer, nuevoKey) }
            })
        }
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
        // AL FINAL, y no donde quedaria mas prolijo: el indice de cada pagina
        // es su posicion en esta lista, y RielConfig los tiene escritos.
        // Meterla en el medio le cambia el numero a todas las de abajo.
        PersonasPage { leftInset: win.insetPagina }
        PublicoPage { leftInset: win.insetPagina }
        MediosPage { leftInset: win.insetPagina }
        PtzPage { leftInset: win.insetPagina }
        VideoPage { leftInset: win.insetPagina }
        PluginsPage { leftInset: win.insetPagina }
        EquiposPage { leftInset: win.insetPagina }
        BotoneraPage { leftInset: win.insetPagina }
        LegislaturaPage { leftInset: win.insetPagina }
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
    Component { id: cmpPersonas;   PersonasPage {} }
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
    Component { id: cmpPublico;    PublicoPage {} }
    Component { id: cmpMedios;     MediosPage {} }
    Component { id: cmpPtz;        PtzPage {} }
    Component { id: cmpVideo;      VideoPage {} }
    Component { id: cmpPlugins;    PluginsPage {} }
    Component { id: cmpEquipos;    EquiposPage {} }
    Component { id: cmpBotonera;   BotoneraPage {} }
    Component { id: cmpLegislatura; LegislaturaPage {} }

    function componenteDePagina(i) {
        return [cmpDashboard, cmpScenes, cmpConference, cmpYouTube, cmpTwitch, cmpFacebook,
                cmpSettings, cmpAssistant, cmpInputs, cmpAudio, cmpEvento, cmpGraficos,
                cmpRotulos, cmpRegistro, cmpAgentes, cmpPrompter, cmpPantallas, cmpQr,
                cmpPersonas, cmpPublico, cmpMedios, cmpPtz, cmpVideo, cmpPlugins, cmpEquipos, cmpBotonera, cmpLegislatura][i] || null
    }

    ListModel { id: sueltas }

    // --- Paneles sueltos -----------------------------------------------------
    // Mas fino que sacar una pagina: en un evento, la cola de preguntas en un
    // monitor y la urna en otro. Ver PanelWindow.qml.
    ListModel { id: panelesSueltos }

    Component { id: cmpPanelPadron;    PanelPadron {} }
    Component { id: cmpPanelPreguntas; PanelPreguntas {} }
    Component { id: cmpPanelVotacion;  PanelVotacion {} }
    // El control del carrusel afuera: en vivo se maneja con una mano mientras
    // la principal tiene las escenas. Es la misma tarjeta del dashboard.
    Component { id: cmpPanelCarrusel;  CarruselContent {} }
    // El PTZ de una cámara afuera, con su imagen para apuntar con el mouse.
    // Uno por cámara: el id es «ptz:<entrada>».
    Component { id: cmpPanelPtz;       PtzContent {} }

    function componenteDePanel(id) {
        return id === "padron" ? cmpPanelPadron
             : id === "preguntas" ? cmpPanelPreguntas
             : id === "votacion" ? cmpPanelVotacion
             : id === "carrusel" ? cmpPanelCarrusel
             : id.startsWith("ptz:") ? cmpPanelPtz : null
    }

    function sacarPanel(id, titulo) {
        // Si ya esta afuera, se la trae al frente en vez de abrir otra igual.
        for (let k = 0; k < panelesSueltos.count; ++k)
            if (panelesSueltos.get(k).panelId === id) {
                toasts.show("«" + titulo + "» ya está en su ventana", "info")
                return
            }
        panelesSueltos.append({ panelId: id, titulo: titulo })
    }

    Instantiator {
        model: panelesSueltos
        delegate: PanelWindow {
            required property int index
            panelId: panelesSueltos.get(index) ? panelesSueltos.get(index).panelId : ""
            titulo: panelesSueltos.get(index) ? panelesSueltos.get(index).titulo : ""
            contenido: win.componenteDePanel(panelId)
            onCerrada: (cual) => {
                for (let k = 0; k < panelesSueltos.count; ++k)
                    if (panelesSueltos.get(k).panelId === cual) { panelesSueltos.remove(k); return }
            }
        }
    }

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
    // Vive en controls/RielNavegacion.qml: ocupaba un tercio de este archivo, y
    // ahora que ademas se abre en arbol no entraba mas. Aca solo se lo coloca y
    // se lo escucha.
    //
    // Abierto FLOTA sobre la pagina y no la corre: el lugar que se le reserva
    // (`rielLugar`, mas arriba) sale del grosor CERRADO, a proposito.
    RielNavegacion {
        id: rail
        paginaActual: win.page
        onElegida: (pagina) => win.page = pagina
        onSacada: (pagina, titulo) => win.sacarAVentana(pagina, titulo)
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

    // --- Alarma crítica: franja roja que titila arriba hasta que se reconoce --------
    Rectangle {
        id: franjaAlarma
        property var activas: []
        property bool critica: false
        // Las críticas sin reconocer, suenen o no (silenciar corta el sonido, no la franja).
        readonly property var criticas: activas.filter(a => a.sonido === "critico")
        readonly property bool todasSilenciadas: criticas.length > 0 && criticas.every(a => !!a.silenciada)
        function refrescar() { activas = Studio.alarmasActivasUi(); critica = criticas.length > 0 }
        function silenciar(si) {
            const vistos = {}
            for (const a of criticas)
                if (!vistos[a.ruleId]) { vistos[a.ruleId] = true; Studio.silenciarAlarmaUi(a.ruleId, si) }
        }
        Connections { target: Studio; function onAlarmasCambio() { franjaAlarma.refrescar() } }
        Component.onCompleted: refrescar()
        visible: critica
        z: 70
        anchors { left: parent.left; right: parent.right; top: parent.top
                  leftMargin: win.libreIzq; rightMargin: win.libreDer; topMargin: win.libreArr }
        height: 34
        color: "#dc2626"
        SequentialAnimation on opacity {
            running: franjaAlarma.visible; loops: Animation.Infinite
            NumberAnimation { to: 0.55; duration: 450 }
            NumberAnimation { to: 1.0; duration: 450 }
        }
        RowLayout {
            anchors { fill: parent; leftMargin: 16; rightMargin: 10 }
            spacing: 10
            Text {
                Layout.fillWidth: true; elide: Text.ElideRight
                text: "⚠ ALARMA: " + (franjaAlarma.activas.length > 0 ? franjaAlarma.activas[franjaAlarma.activas.length - 1].text : "") +
                      (franjaAlarma.activas.length > 1 ? "  (+" + (franjaAlarma.activas.length - 1) + ")" : "")
                color: "white"; font { pixelSize: 14; weight: Font.Bold }
            }
            // Corta el sonido de estas alarmas (sus disparadores quedan silenciados: la
            // franja sigue hasta Reconocer). Apretado otra vez, les devuelve el sonido.
            GlassButton {
                compact: true
                text: franjaAlarma.todasSilenciadas ? "🔇 Silenciada" : "Silenciar"
                onClicked: franjaAlarma.silenciar(!franjaAlarma.todasSilenciadas)
            }
            GlassButton { compact: true; text: "Reconocer (R)"; onClicked: Studio.reconocerAlarmaUi(0) }
        }
    }
    Shortcut {
        sequence: "R"
        enabled: franjaAlarma.critica
        onActivated: Studio.reconocerAlarmaUi(0)
    }
    // Los atajos que configuró el operador (página Botonera → Atajos). Los
    // globales los atiende el sistema (funcionan sin foco): acá van los locales.
    Instantiator {
        model: Studio.acciones ? Studio.acciones.atajos.filter(a => !a.global) : []
        delegate: Shortcut {
            required property var modelData
            sequence: modelData.tecla
            context: Qt.ApplicationShortcut
            onActivated: {
                const e = Studio.acciones.ejecutarUi(modelData.accion)
                if (e !== "") Studio.notify(e, "error")
            }
        }
    }

    // --- Volvimos de una caída: la causa y cuánto tardó el aire en volver -----------
    Rectangle {
        id: cartelCaida
        readonly property var c: Studio.caidaReciente
        visible: !!(c && c.causa)
        anchors { horizontalCenter: parent.horizontalCenter; top: parent.top; topMargin: 70 }
        width: Math.min(parent.width - 80, 720)
        height: cartelCol.implicitHeight + 28
        radius: 14
        z: 70
        color: Qt.rgba(0.1, 0.1, 0.12, 0.96)
        border.color: Theme.amber
        border.width: 2
        Timer { running: cartelCaida.visible && !!cartelCaida.c.aire_vuelta_s; interval: 30000; onTriggered: Studio.cerrarCartelCaida() }
        ColumnLayout {
            id: cartelCol
            anchors { fill: parent; margins: 14 }
            spacing: 4
            Text {
                Layout.fillWidth: true; wrapMode: Text.WordWrap
                text: (cartelCaida.c.componente === "gateway" ? "El gateway (la sala)" : "El programa") + " se reinició: " + (cartelCaida.c.causa || "")
                color: Theme.text; font.pixelSize: 17; font.weight: Font.DemiBold
            }
            Text {
                Layout.fillWidth: true; wrapMode: Text.WordWrap
                text: cartelCaida.c.aire_vuelta_s !== undefined
                      ? "El aire volvió " + Number(cartelCaida.c.aire_vuelta_s).toFixed(1) + " s después de la caída."
                      : cartelCaida.c.habia_aire ? "Retomando lo que estaba al aire…" : "No había nada al aire: no hubo que retomar."
                color: cartelCaida.c.aire_vuelta_s !== undefined || !cartelCaida.c.habia_aire ? Theme.ok : Theme.amber; font.pixelSize: 14
            }
            RowLayout {
                Text { Layout.fillWidth: true; text: "El detalle queda en la página Registro."; color: Theme.textFaint; font.pixelSize: 11 }
                GlassButton { compact: true; text: "Entendido"; onClicked: Studio.cerrarCartelCaida() }
            }
        }
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
