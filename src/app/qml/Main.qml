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
    StackLayout {
        anchors.fill: parent
        currentIndex: win.page
        DashboardPage {}
        ScenesPage {}
        ConferencePage {}
        YouTubePage {}
        TwitchPage {}
        FacebookPage {}
        SettingsPage {}
        AssistantPage {}
        InputsPage {}
        AudioPage {}
        EventoPage {}
        GraficosPage {}
        RotulosPage {}
        RegistroPage {}
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

    function componenteDePagina(i) {
        return [cmpDashboard, cmpScenes, cmpConference, cmpYouTube, cmpTwitch, cmpFacebook,
                cmpSettings, cmpAssistant, cmpInputs, cmpAudio, cmpEvento, cmpGraficos,
                cmpRotulos, cmpRegistro][i] || null
    }

    ListModel { id: sueltas }

    function sacarAVentana(i, titulo) {
        // Si ya esta afuera, se la trae al frente en vez de abrir otra igual.
        for (let k = 0; k < sueltas.count; ++k) {
            if (sueltas.get(k).page === i) { toasts.show("«" + titulo + "» ya está en su ventana", "info"); return }
        }
        sueltas.append({ page: i, titulo: titulo })
    }

    Instantiator {
        model: sueltas
        delegate: PageWindow {
            required property int index
            page: sueltas.get(index) ? sueltas.get(index).page : 0
            titulo: sueltas.get(index) ? sueltas.get(index).titulo : ""
            contenido: win.componenteDePagina(page)
            onCerrada: (p) => {
                for (let k = 0; k < sueltas.count; ++k)
                    if (sueltas.get(k).page === p) { sueltas.remove(k); return }
            }
        }
    }

    // --- Ventanas de salida (una por entrada del modelo) ---------------------------
    Instantiator {
        model: Studio.dashboard.windows
        delegate: OutputWindow {}
    }

    // --- Riel de navegacion (vidrio flotante) ---------------------------------------
    Rectangle {
        id: rail
        anchors { left: parent.left; top: parent.top; bottom: parent.bottom; margins: 14 }
        width: 64
        radius: 32
        color: Qt.rgba(0.06, 0.06, 0.07, 0.72)
        border.color: Theme.glassBorder

        // El logotipo queda fijo arriba: es el unico ancla que no se mueve.
        Rectangle {
            id: marca
            anchors { top: parent.top; horizontalCenter: parent.horizontalCenter; topMargin: 14 }
            width: 44; height: 44; radius: 22
            color: Theme.accent
            Text { anchors.centerIn: parent; text: "BA"; color: "white"; font { pixelSize: 15; weight: Font.Black } }
        }

        // --- Las paginas, AGRUPADAS y desplazables -----------------------------
        // Con catorce paginas ya no entran en una pantalla de portatil y los
        // iconos se encimaban. El patron es el de siempre para un riel que se
        // llena: grupos con un separador fino entre uno y otro —para poder
        // apuntar a lo que uno busca sin leer los catorce— y la tira que se
        // desplaza cuando no entra, con un degradado arriba y abajo que avisa
        // que hay mas. El asistente y los indicadores quedan FIJOS abajo:
        // son los que se buscan a las apuradas y no pueden moverse de lugar.
        //
        // `page` es el indice en el StackLayout, que no es el orden del riel.
        readonly property var grupos: [
            { titulo: "Producción", items: [
                { icon: "⌂", tip: "Dashboard", page: 0 },
                { icon: "▦", tip: "Escenas y fuentes", page: 1 },
                { icon: "◉", tip: "Entradas de video (camaras, capturadoras, red)", page: 8 },
                { icon: "🎚", tip: "Audio: patchbay de modulos LSP por fuente", page: 9 } ] },
            { titulo: "Contenido", items: [
                { icon: "☎", tip: "Sala (lobby, invitados, chat)", page: 2 },
                { icon: "≣", tip: "Guion del evento: bloques, cues y tiempos", page: 10 },
                { icon: "◈", tip: "Gráficos animados: zócalos, placas y tickers", page: 11 },
                { icon: "✎", tip: "Rótulos en vivo: los textos de la escena que está al aire", page: 12 } ] },
            { titulo: "Publicar", items: [
                { icon: "▶", tip: "YouTube (cuenta, videos, en vivo)", page: 3 },
                { icon: "◆", tip: "Twitch (canal, vivo, chat)", page: 4 },
                { icon: "f", tip: "Facebook (Paginas, vivos, videos)", page: 5 } ] },
            { titulo: "Sistema", items: [
                { icon: "❑", tip: "Registro: todo lo que pasa, de los tres procesos", page: 13 },
                { icon: "⚙", tip: "Configuracion", page: 6 } ] }
        ]

        Flickable {
            id: tira
            anchors {
                top: marca.bottom; topMargin: 12
                bottom: aiBtn.top; bottomMargin: 12
                left: parent.left; right: parent.right
            }
            clip: true
            contentHeight: columna.height
            boundsBehavior: Flickable.StopAtBounds
            // Sin barra: el riel tiene 64 px de ancho y una barra se comeria
            // el espacio del icono. Los degradados de los bordes ya dicen que
            // hay mas, y se desplaza con la rueda.
            interactive: contentHeight > height

            Column {
                id: columna
                width: tira.width
                spacing: 8

                Repeater {
                    model: rail.grupos
                    delegate: Column {
                        required property var modelData
                        required property int index
                        width: columna.width
                        spacing: 8

                        // Separador entre grupos (en el primero no va).
                        Item {
                            width: 1; height: index === 0 ? 0 : 9
                            visible: index !== 0
                            Rectangle {
                                anchors.centerIn: parent
                                width: 22; height: 1
                                color: Qt.rgba(1, 1, 1, 0.14)
                            }
                        }

                        Repeater {
                            model: modelData.items
                            delegate: Rectangle {
                                required property var modelData
                                anchors.horizontalCenter: parent.horizontalCenter
                                width: 42; height: 42; radius: 21
                                color: win.page === modelData.page ? "white"
                                     : (navMa.containsMouse ? Qt.rgba(1, 1, 1, 0.12) : "transparent")
                                border.color: Qt.rgba(1, 1, 1, 0.16)
                                Behavior on color { ColorAnimation { duration: 150 } }
                                Text {
                                    anchors.centerIn: parent
                                    text: modelData.icon
                                    color: win.page === modelData.page ? "#111" : Theme.text
                                    font.pixelSize: 18
                                }
                                MouseArea {
                                    id: navMa
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor
                                    acceptedButtons: Qt.LeftButton | Qt.RightButton
                                    onClicked: (m) => {
                                        if (m.button === Qt.RightButton)
                                            win.sacarAVentana(modelData.page, modelData.tip.split(":")[0])
                                        else win.page = modelData.page
                                    }
                                }
                                ToolTip.visible: navMa.containsMouse
                                ToolTip.text: modelData.tip + "\n(clic derecho: sacar a su propia ventana)"
                                ToolTip.delay: 400
                            }
                        }
                    }
                }
            }
        }

        // Los degradados que avisan que la tira sigue. Solo cuando hay algo
        // mas para ese lado: si no, son dos manchas que no dicen nada.
        Rectangle {
            anchors { top: tira.top; left: tira.left; right: tira.right }
            height: 18
            visible: tira.contentY > 2
            gradient: Gradient {
                GradientStop { position: 0; color: Qt.rgba(0.06, 0.06, 0.07, 0.95) }
                GradientStop { position: 1; color: "transparent" }
            }
        }
        Rectangle {
            anchors { bottom: tira.bottom; left: tira.left; right: tira.right }
            height: 18
            visible: tira.contentHeight - tira.contentY - tira.height > 2
            gradient: Gradient {
                GradientStop { position: 0; color: "transparent" }
                GradientStop { position: 1; color: Qt.rgba(0.06, 0.06, 0.07, 0.95) }
            }
        }

        // Asistente de IA: abajo del todo, separado de las paginas.
        Rectangle {
            id: aiBtn
            anchors { bottom: dots.top; horizontalCenter: parent.horizontalCenter; bottomMargin: 18 }
            width: 44; height: 44; radius: 22
            gradient: Gradient {
                GradientStop { position: 0; color: win.page === 7 ? "#ffffff" : "#ff8a3d" }
                GradientStop { position: 1; color: win.page === 7 ? "#ffe2cf" : "#c2410c" }
            }
            border.color: Qt.rgba(1, 1, 1, 0.3)
            Text { anchors.centerIn: parent; text: "✦"; color: win.page === 7 ? "#c2410c" : "white"; font.pixelSize: 20 }
            Rectangle { // corriendo
                visible: Studio.agent.running
                width: 10; height: 10; radius: 5; color: Theme.ok; border.color: "#111"
                anchors { right: parent.right; top: parent.top }
            }
            MouseArea { id: aiMa; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: win.page = 7 }
            ToolTip.visible: aiMa.containsMouse
            ToolTip.text: "Asistente (chat con la IA que maneja la app)"
            ToolTip.delay: 400
        }
        // Indicadores rapidos abajo
        Column {
            id: dots
            anchors { bottom: parent.bottom; horizontalCenter: parent.horizontalCenter; bottomMargin: 18 }
            spacing: 10
            Rectangle { width: 10; height: 10; radius: 5; anchors.horizontalCenter: parent.horizontalCenter
                        color: Studio.streaming ? Theme.live : Theme.textFaint
                        ToolTip.visible: sMa.containsMouse; ToolTip.text: Studio.streaming ? "Al aire" : "Fuera del aire"
                        MouseArea { id: sMa; anchors.fill: parent; anchors.margins: -6; hoverEnabled: true } }
            Rectangle { width: 10; height: 10; radius: 5; anchors.horizontalCenter: parent.horizontalCenter
                        color: Studio.recording ? Theme.accent : Theme.textFaint
                        ToolTip.visible: rMa.containsMouse; ToolTip.text: Studio.recording ? "Grabando" : "Sin grabar"
                        MouseArea { id: rMa; anchors.fill: parent; anchors.margins: -6; hoverEnabled: true } }
            Rectangle { width: 10; height: 10; radius: 5; anchors.horizontalCenter: parent.horizontalCenter
                        color: Studio.gatewayAlive ? Theme.ok : Theme.live
                        ToolTip.visible: gMa.containsMouse; ToolTip.text: Studio.gatewayAlive ? "Sala en línea" : "Sala sin conexión"
                        MouseArea { id: gMa; anchors.fill: parent; anchors.margins: -6; hoverEnabled: true } }
        }
    }

    // --- Estado de emision, visible en todas las paginas ------------------------------
    // Va en la franja libre de arriba (las paginas arrancan en y = 18 o mas): no
    // tapa ningun control. El Dashboard ya trae sus pildoras en el encabezado.
    Item {
        visible: win.page !== 0
        anchors { top: parent.top; horizontalCenter: parent.horizontalCenter; topMargin: 1 }
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
        anchors { top: parent.top; horizontalCenter: parent.horizontalCenter; topMargin: 20 }
        z: 50
        onClicked: win.page = 2
    }

    // --- Barra del asistente al operador (mensajes, preguntas, respuestas a invitados) ----
    OperatorBar {
        id: opBar
        anchors { left: parent.left; right: parent.right; bottom: parent.bottom; leftMargin: 92 + 12; rightMargin: 18; bottomMargin: 14 }
        z: 60
    }

    // --- Avisos (toasts) ------------------------------------------------------------
    Column {
        id: toasts
        anchors { right: parent.right; bottom: opBar.visible ? opBar.top : parent.bottom; margins: 22 }
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
