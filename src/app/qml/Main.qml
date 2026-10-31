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
    // Un idioma de derecha a izquierda (su perfil: "direccion": "rtl") espeja la interfaz entera:
    // filas, anclajes y alineaciones (arranque/TraductorQt pone la dirección de la aplicación).
    LayoutMirroring.enabled: Qt.application.layoutDirection === Qt.RightToLeft
    LayoutMirroring.childrenInherit: true
    // Modo prueba (--test): el MCP no pide aprobaciones. Siempre a la vista.
    Rectangle {
        visible: Studio.modoTest
        z: 100000
        anchors { bottom: parent.bottom; horizontalCenter: parent.horizontalCenter; bottomMargin: 4 }
        width: avisoTest.implicitWidth + 24; height: 20; radius: 4
        color: "#c62828"
        Text { id: avisoTest; anchors.centerIn: parent; color: "white"; font.pixelSize: 11; font.bold: true
               text: qsTr("MODO PRUEBA (--test): el asistente actúa SIN pedir aprobación, ni siquiera en el aire") }
    }
    // El disco en apuros (controls/AvisoDisco.qml): arriba y a la vista desde cualquier página.
    AvisoDisco {
        z: 99999
        anchors { top: parent.top; horizontalCenter: parent.horizontalCenter; topMargin: 64 }
        width: Math.min(760, parent.width - 160)
    }
    objectName: "ba:principal" // ver VentanaSuelta.qml
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
    /// Cuántas páginas (en orden) ya se pidieron en segundo plano (ver `paginas`).
    property int precargadas: 0
    Timer {
        // Arranca cuando la interfaz ya se ve; una página cada 150 ms.
        interval: 150
        repeat: true
        running: win.visible && win.precargadas < win.componentesDePagina.length
        onTriggered: win.precargadas++
    }
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
        // La palabra que hay que escribir sale en el idioma del operador (se compara con ella misma).
        readonly property string palabra: qsTr("CONFIRMAR", "palabra que se escribe para entrar a una página protegida")
        anchors.centerIn: parent
        width: 440
        modal: true
        title: qsTr("Estamos en vivo")
        onOpened: { confirmarF.text = ""; motivoF.text = ""; confirmarF.forceActiveFocus() }
        contentItem: ColumnLayout {
            spacing: 8
            Text {
                Layout.fillWidth: true; wrapMode: Text.WordWrap
                text: qsTr("«%1» tiene cosas que pueden cortar el aire. Para entrar escribí %2 y por qué (queda en el registro).")
                      .arg(Studio.riel.tituloDePagina(desbloqueo.pagina)).arg(desbloqueo.palabra)
                color: Theme.text; font.pixelSize: 13
            }
            TextField { id: confirmarF; Layout.fillWidth: true; placeholderText: desbloqueo.palabra; color: Theme.text }
            TextField { id: motivoF; Layout.fillWidth: true; placeholderText: qsTr("Motivo (p. ej. cambiar la conferencia de los invitados)"); color: Theme.text }
            RowLayout {
                Item { Layout.fillWidth: true }
                GlassButton { compact: true; text: qsTr("Cancelar"); onClicked: desbloqueo.close() }
                GlassButton {
                    compact: true; text: qsTr("Entrar"); active: true
                    enabled: confirmarF.text.trim().toUpperCase() === desbloqueo.palabra.toUpperCase() && motivoF.text.trim().length >= 3
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
    // Antes se pregunta siempre; con algo en curso (emision, grabacion, gente en la sala o
    // en la sesion remota), lo que se corta lo dice Studio (SettingsPage usa lo mismo).
    property bool quitting: false // el cierre ya esta decidido: la guardia no pregunta
    onClosing: (close) => {
        // Siempre se pregunta (un click en la ✕ por error cerraba todo): con algo en curso,
        // diciendo lo que se corta.
        if (quitting) { Qt.quit(); return }
        close.accepted = false
        // Diferido: QGuiApplication::quit() tambien cierra las ventanas, asi que
        // un cierre pedido por la app pasa por aca; su quitRequested llega en
        // la misma vuelta y askClose() ya no pregunta.
        Qt.callLater(askClose)
    }
    function askClose() {
        if (quitting) return
        confirm({
            title: qsTr("¿Cerrar Broadcast Appliance?"),
            text: Studio.impactoDeCerrar.hay ? Studio.avisoDeCerrar("")
                  : qsTr("Se cierran la conferencia, las salidas y los asistentes."),
            accept: qsTr("Cerrar"),
            reject: qsTr("Cancelar"),
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
    /// La cola de descargas (controls/PanelDescargas.qml).
    function abrirDescargas() { descargasDlg.open() }
    function confirmStopStream() {
        confirm({
            title: qsTr("¿Cortar la transmisión?"),
            text: qsTr("La emisión en vivo se va a detener."),
            accept: qsTr("Cortar"),
            reject: qsTr("Cancelar"),
            action: function() { Studio.stopStreaming() }
        })
    }

    Connections {
        target: Studio
        function onNotify(message, level) { toasts.show(message, level) }
        function onPedirConfirmarGrabacion(horas) {
            confirm({
                title: qsTr("Queda muy poco disco"),
                text: qsTr("Alcanza para ~%n minutos de grabación a este bitrate. ¿Grabar igual?", "", Math.round(horas * 60)),
                accept: qsTr("Grabar igual"), reject: qsTr("Cancelar"),
                action: function() { Studio.grabarIgual() }
            })
        }
        // Ya al aire y se pidió otro destino: nunca se cambia sin preguntar.
        function onPedirCambioDestino(actual, nuevoServer, nuevoKey) {
            confirm({
                title: qsTr("Ya estás transmitiendo"),
                text: qsTr("Sale a %1. ¿Cambiar a %2? La transmisión se corta un instante.").arg(actual).arg(nuevoServer),
                accept: qsTr("Cambiar de destino"), reject: qsTr("Seguir como está"),
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
    /// Con una alarma crítica, la franja roja ocupa su lugar arriba: las páginas
    /// se corren (si no, tapaba el título y la primera fila de botones de cada una).
    readonly property real libreAlarma: franjaAlarma.visible ? franjaAlarma.height : 0

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
        anchors.topMargin: win.libreArr + win.libreAlarma
        anchors.rightMargin: win.libreDer
        currentIndex: win.page
        // Las páginas, CUANDO HACEN FALTA. Antes se creaban las 27 al arrancar
        // (la mitad de lo que tardaba la interfaz en aparecer) y cada cambio de
        // datos repintaba también las que nadie miraba. Ahora se crea la que se
        // ve; las demás se van creando de a una, en segundo plano y de a pedazos
        // (asynchronous: no traban), y una que se abre antes de tiempo se crea
        // en el momento. Una vez creada queda: conserva lo que se escribió.
        // El orden es el de componenteDePagina (= el índice de cada página).
        Repeater {
            model: win.componentesDePagina.length
            delegate: Loader {
                id: lugar
                required property int index
                // El StackLayout (Qt 6.6) solo le da tamaño a la que se ve: las que se
                // precargan quedaban en 0×0, sus layouts se armaban con ancho 0 o negativo
                // («polish loop») y se rehacían enteras al mostrarlas.
                width: paginas.width
                height: paginas.height
                readonly property bool actual: paginas.currentIndex === index
                property bool pedida: actual
                onActualChanged: if (actual) pedida = true
                active: pedida || win.precargadas > index
                // La que se está mirando, ya (si estaba a medio crear, se termina ya).
                asynchronous: !actual
                sourceComponent: win.componenteDePagina(index)
                onLoaded: item.leftInset = Qt.binding(() => win.insetPagina)
            }
        }
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
    Component { id: cmpAsamblea; AsambleaPage {} }
    Component { id: cmpHistorico; HistoricoPage {} }
    Component { id: cmpMultiview;  MultiviewPage {} }
    Component { id: cmpEnlaces;    EnlacesPage {} }
    Component { id: cmpTiempos;    TiemposPage {} }
    Component { id: cmpEntradasAudio; EntradasAudioPage {} }
    Component { id: cmpManuales;   ManualesPage {} }

    /// Las páginas, por índice (el `page` del catálogo de RielConfig). Cuántas son
    /// sale de acá: una página nueva se agrega solo en esta lista y en el catálogo.
    readonly property var componentesDePagina: [
        cmpDashboard, cmpScenes, cmpConference, cmpYouTube, cmpTwitch, cmpFacebook,
        cmpSettings, cmpAssistant, cmpInputs, cmpAudio, cmpEvento, cmpGraficos,
        cmpRotulos, cmpRegistro, cmpAgentes, cmpPrompter, cmpPantallas, cmpQr,
        cmpPersonas, cmpPublico, cmpMedios, cmpPtz, cmpVideo, cmpPlugins, cmpEquipos, cmpBotonera, cmpAsamblea,
        cmpHistorico, cmpMultiview, cmpEnlaces, cmpTiempos, cmpEntradasAudio, cmpManuales]
    function componenteDePagina(i) { return win.componentesDePagina[i] || null }

    // --- Cosas sacadas a su propia ventana ---------------------------------------
    // Una página entera (clic derecho en el riel), un panel (el padrón, la cola de
    // preguntas, el Mezclador, la PTZ de una cámara), una tarjeta del tablero o
    // video (el programa, una escena, una multivista, un invitado, lo de fondo):
    // todo es una cosa del catálogo (Studio.catalogo) y va en una VentanaSuelta,
    // que se recuerda (Studio.ventanas: monitor, tamaño, pantalla completa).

    /// Lo dictado va derecho al asistente si se lo está mirando: su página en la
    /// principal, o su ventana propia activa (agent/Voz: vistaAsistente).
    property bool ventanaAsistenteActiva: false
    readonly property bool vistaAsistente: win.page === Studio.riel.paginaDeId("asistente") || ventanaAsistenteActiva
    onVistaAsistenteChanged: if (Studio.voz) Studio.voz.vistaAsistente = vistaAsistente

    /// Saca la cosa `itemId` del catálogo a su ventana. Si ya está afuera, la trae
    /// al frente en vez de abrir otra igual.
    function sacar(itemId) {
        const c = Studio.catalogo.porId[itemId]
        if (!c || !c.local) { toasts.show(qsTr("«%1» no se puede abrir en una ventana").arg(Studio.catalogo.tituloDe(itemId)), "warn"); return }
        if (afuera(itemId) && c.dibujo.modo !== "video" && c.dibujo.modo !== "multiview")
            toasts.show(qsTr("«%1» ya está en su ventana").arg(c.titulo), "info")
        Studio.ventanas.abrir([itemId], "")
    }
    /// ¿La cosa `itemId` está en su propia ventana? (Enlazado: el modelo avisa.)
    function afuera(itemId) { return Studio.ventanas.afuera.indexOf(itemId) >= 0 }
    /// Cierra su ventana propia (vuelve a estar solo en la principal).
    function traer(itemId) { Studio.ventanas.cerrarCosa(itemId) }
    // Los nombres de siempre (los llaman las tarjetas, las páginas y el MCP).
    function sacarAVentana(i, titulo) { sacar("pagina:" + Studio.riel.idDePagina(i)) }
    function paginaAfuera(id) { return afuera("pagina:" + id) }
    function traerPagina(id) { traer("pagina:" + id) }
    function sacarPanel(id, titulo) { sacar(id) }

    // Los paneles que se pueden sacar (el catálogo dice cuáles y con qué id;
    // acá está solo cómo se dibuja cada uno).
    Component { id: cmpPanelPadron;    PanelPadron {} }
    Component { id: cmpPanelPreguntas; PanelPreguntas {} }
    Component { id: cmpPanelVotacion;  PanelVotacion {} }
    // El control del carrusel afuera: en vivo se maneja con una mano mientras
    // la principal tiene las escenas. Es la misma tarjeta del dashboard.
    Component { id: cmpPanelCarrusel;  CarruselContent {} }
    // El PTZ de una cámara afuera, con su imagen para apuntar con el mouse.
    // Uno por cámara: el id es «ptz:<entrada>».
    Component { id: cmpPanelPtz;       PtzContent {} }
    // El Mezclador: entradas, salidas y la matriz (controls/PanelMezclador.qml).
    Component { id: cmpPanelMezclador; PanelMezclador {} }
    // Cualquier tarjeta del tablero en su ventana (la barra de tarjetas): «tarjeta:<kind>».
    Component { id: cmpPanelTarjeta;   TarjetaEnVentana {} }

    function componenteDePanel(id) {
        return id === "padron" ? cmpPanelPadron
             : id === "preguntas" ? cmpPanelPreguntas
             : id === "votacion" ? cmpPanelVotacion
             : id === "carrusel" ? cmpPanelCarrusel
             : id === "mezclador" ? cmpPanelMezclador
             : id.startsWith("ptz:") ? cmpPanelPtz
             : id.startsWith("tarjeta:") ? cmpPanelTarjeta : null
    }

    Instantiator {
        model: Studio.ventanas
        delegate: VentanaSuelta {
            principal: win
            onActiveChanged: if (idPagina === "asistente") win.ventanaAsistenteActiva = active
        }
    }

    // --- Los monitores de la máquina (Salidas → Pantallas) -------------------------
    // Una ventana por monitor conectado con algo asignado; si el monitor no está,
    // no hay ventana (se lo espera: ver PantallaWindow.qml).
    Instantiator {
        model: Studio.pantallasConectadas.activos
        delegate: PantallaWindow { principal: win }
    }

    // El asistente retomó la conversación de antes del reinicio: a su pestaña.
    Connections {
        target: Studio.agent
        function onMostrarseAlReanudar() {
            const p = Studio.riel.paginaDeId("asistente")
            if (p >= 0) win.showPage(p)
        }
        function onPideVerse() {
            const p = Studio.riel.paginaDeId("asistente")
            if (p >= 0) win.showPage(p)
        }
    }

    // Los carteles que dicen cuál monitor es cuál (Studio.identificarPantallas).
    IdentificarPantallas { id: identificar }
    Connections {
        target: Studio
        function onIdentificarPedido(ids) { identificar.mostrar(ids) }
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
        // Tras un corte de luz la línea de tiempo vuelve en HOLD y pregunta qué está
        // pasando: lo que se dicte o escriba acá es esa respuesta, esté donde esté.
        const r = Studio.evento.retoma
        const corte = r && r.activa
            ? qsTr("Volvió la luz en medio del evento y la línea de tiempo está en HOLD: lo que te cuento es qué está pasando. Pasalo con linea_retomar, proponé cómo seguir (linea_editar o linea_ajustar con probar) y soltá con linea_soltar cuando lo confirme. ")
            : ""
        return corte + contextoPropio(i)
    }
    function contextoPropio(i) {
        switch (i) {
        case 0: return qsTr("Estoy en el Dashboard, mirando el programa y las salidas.")
        case 1: return qsTr("Estoy en Escenas y fuentes. La escena al aire es «%1».").arg(Studio.currentScene)
        case 2: return qsTr("Estoy en la Conferencia (lobby, invitados y chat).")
        case 3: return qsTr("Estoy en la pagina de YouTube (cuenta, videos y vivos).")
        case 4: return qsTr("Estoy en la pagina de Twitch.")
        case 5: return qsTr("Estoy en la pagina de Facebook.")
        case 6: return qsTr("Estoy en Configuracion.")
        case 8: return qsTr("Estoy en Entradas de video (camaras, capturadoras y red).")
        case 9: return qsTr("Estoy en Audio, en el patchbay de modulos por fuente.")
        case 10: return Studio.evento.abierto
                        ? qsTr("Estoy en el Guion del evento «%1».").arg(Studio.evento.abierto)
                        : qsTr("Estoy en el Guion del evento, sin ningun evento abierto.")
        case 11: return Studio.grafico.abierto !== ""
                        ? (Studio.grafico.declarativo
                           ? qsTr("Estoy en el editor de Graficos, editando «%1» (declarativo: se edita su descripcion en grafico.json).").arg(Studio.grafico.abierto)
                           : qsTr("Estoy en el editor de Graficos, editando «%1» (QML escrito a mano).").arg(Studio.grafico.abierto))
                        : qsTr("Estoy en el editor de Graficos, sin ninguno abierto.")
        case 12: return qsTr("Estoy en Rotulos. La escena al aire es «%1».").arg(Studio.rotulador.escena)
        case 13: return qsTr("Estoy en el visor del Registro.")
        }
        return ""
    }

    // Los pedidos ya escritos de cada pagina: lo que uno va a querer pedir
    // estando ahi. Son los que antes vivian adentro de cada pagina.
    function sugerenciasDePagina(i) {
        switch (i) {
        case 0: return [ { t: qsTr("¿Como venimos?"), p: qsTr("Deci en dos lineas como esta todo ahora: aire, grabacion, conferencia y alertas.") },
                         { t: qsTr("Revisar antes de salir"), p: qsTr("Revisa todo lo que conviene mirar antes de salir al aire y deci que esta mal.") } ]
        case 1: return [ { t: qsTr("Escena nueva"), p: qsTr("Arma una escena nueva parecida a la que esta al aire y deci como te quedo.") },
                         { t: qsTr("¿Que hay en esta escena?"), p: qsTr("Deci que fuentes tiene la escena al aire y para que sirve cada una.") } ]
        case 2: return [ { t: qsTr("¿Quien esta?"), p: qsTr("Deci quien esta conectado en la conferencia, quien espera y quien esta al aire.") },
                         { t: qsTr("Pasar al que sigue"), p: qsTr("Pone al aire al siguiente de la cola de oradores.") } ]
        case 9: return [ { t: qsTr("Se escucha mal"), p: qsTr("Mira los niveles de audio y deci que fuente esta mal y que le harias.") } ]
        case 10: return [ { t: qsTr("Revisar el guion"), p: qsTr("Revisa el guion y deci que le falta a cada cue para poder correrlo.") },
                          { t: qsTr("¿Como vamos de tiempo?"), p: qsTr("Deci como vamos de tiempo contra lo previsto.") } ]
        case 11: return [ { t: qsTr("Que entre mas rapido"), p: qsTr("Hace que entre mas rapido, alrededor de 300 ms.") },
                          { t: qsTr("Mas chico"), p: qsTr("Achicalo un 20 % sin que el texto quede apretado.") },
                          { t: qsTr("Probar un nombre largo"), p: qsTr("Probalo con un nombre bien largo y fijate si se sale de la caja.") },
                          { t: qsTr("Proponer botones"), p: qsTr("Propone los botones («acciones») de este grafico y escribilos en su grafico.json: cada uno con titulo corto, icono de un caracter y prompt redactado como se lo pediria el operador en vivo.") } ]
        case 12: return [ { t: qsTr("Poner al que habla"), p: qsTr("Pone en el zocalo de esta escena el nombre y el cargo del que esta al aire, y sacalo al aire.") },
                          { t: qsTr("Que se mantenga solo"), p: qsTr("Ata este rotulo al orador que este al aire, para que se actualice solo cuando cambie.") } ]
        case 13: return [ { t: qsTr("¿Que se rompio?"), p: qsTr("Mira el registro y deci que fallo, cuando y por que. Empeza por los avisos y errores.") },
                          { t: qsTr("Mas detalle"), p: qsTr("Baja el nivel de registro a depuracion asi queda mas detalle de lo que viene.") } ]
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
        onMandado: (texto) => toasts.show(qsTr("Pedido mandado. La respuesta aparece en el Asistente."), "info")
    }

    // --- Estado de emision, visible en todas las paginas ------------------------------
    // Va en la franja libre de arriba (las paginas arrancan en y = 18 o mas): no
    // tapa ningun control. El Dashboard ya trae sus pildoras en el encabezado.
    Item {
        visible: win.page !== 0
        anchors { top: parent.top; horizontalCenter: parent.horizontalCenter
                  topMargin: win.libreArr + win.libreAlarma + 1 }
        z: 50
        width: airState.width
        height: airState.height
        // Un clic en el estado de emisión vuelve al Dashboard (debajo de la fila: la
        // pastilla IA tiene su propio clic).
        MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: win.page = 0 }
        Row {
            id: airState
            spacing: 8
            // Al aire = sale algo (Studio.alAire): lo mismo que miran la PTZ, las
            // alertas y los candados. Solo grabando también cuenta, y lo dice.
            // Reconectando gana: la salida sigue activa, pero no esta llegando nada
            // y el operador tiene que saberlo.
            StatusPill {
                height: 16
                text: Studio.reconnecting ? qsTr("RECONECTANDO")
                      : Studio.transmitiendo ? (Studio.streaming ? qsTr("AL AIRE  %1").arg(Studio.streamTime) : qsTr("AL AIRE"))
                      : Studio.alAire ? (Studio.recording ? qsTr("AL AIRE · GRABANDO  %1").arg(Studio.recordTime) : qsTr("AL AIRE · GRABANDO"))
                      : qsTr("FUERA DEL AIRE")
                dot: Studio.reconnecting ? Theme.amber : Theme.live
                on: Studio.alAire || Studio.reconnecting
                pulse: true
                ToolTip.visible: pillMa.containsMouse
                ToolTip.text: Studio.outputProblem !== "" ? Studio.outputProblem
                              : Studio.alAire ? qsTr("Sale el programa: la PTZ de una cámara que se ve pide confirmar y las páginas protegidas, %1.").arg(desbloqueo.palabra)
                              : qsTr("No sale nada (ni transmisión ni grabación): se puede mover y tocar todo sin avisos de aire.")
                MouseArea { id: pillMa; anchors.fill: parent; hoverEnabled: true; acceptedButtons: Qt.NoButton }
            }
            StatusPill { height: 16; visible: Studio.recording && Studio.transmitiendo; text: qsTr("REC  %1").arg(Studio.recordTime); dot: Theme.accent; pulse: true }
            // En las otras páginas, solo si algo de la IA no anda.
            PastillaIA { height: 16; visible: total > 0 }
        }
    }

    // --- Reloj del orador al aire (modo oradores), visible en todas las paginas ------
    // Debajo de la franja del estado de emision, para no taparla.
    OnAirTimer {
        anchors { top: parent.top; horizontalCenter: parent.horizontalCenter
                  topMargin: win.libreArr + win.libreAlarma + 20 }
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
                text: {
                    const n = franjaAlarma.activas.length
                    const ultima = n > 0 ? franjaAlarma.activas[n - 1].text : ""
                    return n > 1 ? qsTr("⚠ ALARMA: %1  (+%2)").arg(ultima).arg(n - 1) : qsTr("⚠ ALARMA: %1").arg(ultima)
                }
                color: "white"; font { pixelSize: 14; weight: Font.Bold }
            }
            // Corta el sonido de estas alarmas (sus disparadores quedan silenciados: la
            // franja sigue hasta Reconocer). Apretado otra vez, les devuelve el sonido.
            GlassButton {
                compact: true
                text: franjaAlarma.todasSilenciadas ? qsTr("🔇 Silenciada") : qsTr("Silenciar")
                onClicked: franjaAlarma.silenciar(!franjaAlarma.todasSilenciadas)
            }
            GlassButton { compact: true; text: qsTr("Reconocer (R)"); onClicked: Studio.reconocerAlarmaUi(0) }
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
        ColumnLayout {
            id: cartelCol
            anchors { fill: parent; margins: 14 }
            spacing: 4
            Text {
                Layout.fillWidth: true; wrapMode: Text.WordWrap
                text: (cartelCaida.c.componente === "gateway" ? qsTr("El servidor (gateway) se reinició: %1")
                                                              : qsTr("El programa se reinició: %1")).arg(cartelCaida.c.causa || "")
                color: Theme.text; font.pixelSize: 17; font.weight: Font.DemiBold
            }
            Text {
                Layout.fillWidth: true; wrapMode: Text.WordWrap
                text: cartelCaida.c.aire_vuelta_s !== undefined
                      ? qsTr("El aire volvió %1 s después de la caída.").arg(Number(cartelCaida.c.aire_vuelta_s).toFixed(1))
                      : cartelCaida.c.habia_aire ? qsTr("Retomando lo que estaba al aire…") : qsTr("No había nada al aire: no hubo que retomar.")
                color: cartelCaida.c.aire_vuelta_s !== undefined || !cartelCaida.c.habia_aire ? Theme.ok : Theme.amber; font.pixelSize: 14
            }
            RowLayout {
                Text { Layout.fillWidth: true; text: qsTr("El detalle queda en la página Registro."); color: Theme.textFaint; font.pixelSize: 11 }
                GlassButton { compact: true; text: qsTr("Entendido"); onClicked: Studio.cerrarCartelCaida() }
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
            // El mismo aviso (o uno del mismo tipo: mismo comienzo, como «La interfaz
            // estuvo trabada N s») no se apila: reemplaza al que está y renueva su tiempo.
            const tipo = message.substring(0, 24)
            for (let i = 0; i < children.length; ++i)
                if (children[i].opacity > 0 && children[i].message.substring(0, 24) === tipo) {
                    children[i].message = message
                    children[i].level = level || "info"
                    return children[i].renew()
                }
            // Como mucho cuatro a la vista: el más viejo se va.
            if (children.length >= 4) children[0].destroy()
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
        title: qsTr("Salir al aire")
        anchors.centerIn: parent
        modal: true
        standardButtons: Dialog.Ok | Dialog.Cancel
        onOpened: { srvField.text = Studio.streamServer; keyField.text = "" }
        onAccepted: Studio.startStreaming(srvField.text, keyField.text)
        ColumnLayout {
            spacing: 10
            Label { text: qsTr("Servidor RTMP") }
            TextField { id: srvField; Layout.preferredWidth: 420; placeholderText: "rtmp://a.rtmp.youtube.com/live2" }
            Label { text: qsTr("Clave de transmisión (se guarda cifrada en %1)").arg("Credenciales/") }
            TextField {
                id: keyField; Layout.preferredWidth: 420; echoMode: TextInput.Password
                placeholderText: Studio.streamKeySaved ? qsTr("Vacío = la clave de la última vez") : ""
            }
        }
    }
    ConfirmDialog { id: confirmDlg }
    // La app está por cerrarse para que el launcher la reabra (otro evento, actualización):
    // el aviso con la cuenta regresiva, encima de todo y sin poder cerrarlo (Studio.reinicio).
    Popup {
        id: reinicioDlg
        anchors.centerIn: parent
        width: Math.min(460, win.width - 32)
        modal: true
        closePolicy: Popup.NoAutoClose
        visible: !!Studio.reinicio.aviso
        padding: 24
        background: Rectangle { color: Theme.panel; radius: 12; border.color: Theme.glassBorder }
        contentItem: Column {
            spacing: 12
            Text {
                width: parent.width; wrapMode: Text.WordWrap; horizontalAlignment: Text.AlignHCenter
                text: Studio.reinicio.aviso || ""
                color: Theme.text; font.pixelSize: 16
            }
            Text {
                width: parent.width; horizontalAlignment: Text.AlignHCenter
                text: Studio.reinicio.segundos > 0 ? Studio.reinicio.segundos : "…"
                color: Theme.accent; font.pixelSize: 48; font.bold: true
            }
            Text {
                width: parent.width; wrapMode: Text.WordWrap; horizontalAlignment: Text.AlignHCenter
                text: qsTr("Vuelve a abrir sola en unos segundos.")
                color: Theme.textDim; font.pixelSize: 12
            }
        }
    }
    PanelDescargas { id: descargasDlg }
    // Los QR que manda el asistente (qr_mostrar): una ventana por QR, cada una de
    // su color («escaneá el QR verde»), escalonadas para que se vean todas.
    Component { id: qrMostrarComp; QrMostrar {} }
    property int qrAbiertos: 0
    Connections {
        target: Studio
        function onMostrarQr(texto, descripcion, mensaje, color) {
            const n = win.qrAbiertos
            const d = qrMostrarComp.createObject(win.contentItem, {
                texto: texto, descripcion: descripcion, mensaje: mensaje, colorNombre: color || "verde"
            })
            // Una al lado de la otra mientras entren; después, escalonadas encima.
            const w = d.width
            const cols = Math.max(1, Math.floor((win.width - 32 + 16) / (w + 16)))
            const x0 = Math.max(16, (win.width - cols * w - (cols - 1) * 16) / 2)
            d.x = x0 + (n % cols) * (w + 16)
            d.y = Math.max(16, 60 + Math.floor(n / cols) * 40)
            win.qrAbiertos = n + 1
            d.cerrada.connect(function() { win.qrAbiertos = Math.max(0, win.qrAbiertos - 1); d.destroy() })
            d.open()
        }
    }

    // --- Lo que se está bajando, visible en todas las páginas (abre las Descargas) ----
    StatusPill {
        visible: Studio.descargas.activas > 0
        anchors { top: parent.top; right: parent.right; rightMargin: 14
                  topMargin: win.libreArr + win.libreAlarma + 1 }
        z: 50
        height: 16
        dot: Theme.accent
        pulse: true
        text: {
            const l = Studio.descargas.lista
            const b = l.length > 0 && l[0].estado === "bajando" ? l[0] : null
            return b ? qsTr("BAJANDO %1  %2%").arg(Studio.descargas.activas).arg(Math.round(b.progreso * 100))
                     : qsTr("BAJANDO %1").arg(Studio.descargas.activas)
        }
        MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: win.abrirDescargas() }
    }
}
