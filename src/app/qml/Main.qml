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
    onClosing: Qt.quit()

    // --- API para las tarjetas y paginas ------------------------------------------
    function showPage(i) { page = i }
    function toast(message, level) { toasts.show(message, level) }
    function openStreamDialog() {
        if (Studio.streamServer && Studio.streamKeyAvailable) Studio.startStreaming("", "")
        else streamDlg.open()
    }
    function confirmStopStream() { stopDlg.open() }

    Connections {
        target: Studio
        function onNotify(message, level) { toasts.show(message, level) }
    }

    // --- Paginas ------------------------------------------------------------------
    StackLayout {
        anchors.fill: parent
        currentIndex: win.page
        DashboardPage {}
        ScenesPage {}
        ConferencePage {}
        SettingsPage {}
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

        Column {
            anchors { top: parent.top; horizontalCenter: parent.horizontalCenter; topMargin: 14 }
            spacing: 14
            Rectangle {
                width: 44; height: 44; radius: 22
                color: Theme.accent
                Text { anchors.centerIn: parent; text: "BA"; color: "white"; font { pixelSize: 15; weight: Font.Black } }
            }
            Item { width: 1; height: 10 }
            Repeater {
                model: [ { icon: "⌂", tip: "Dashboard" }, { icon: "▦", tip: "Escenas y fuentes" }, { icon: "☎", tip: "Sala (lobby, invitados, chat)" }, { icon: "⚙", tip: "Configuracion" } ]
                delegate: Rectangle {
                    required property var modelData
                    required property int index
                    width: 44; height: 44; radius: 22
                    color: win.page === index ? "white" : (navMa.containsMouse ? Qt.rgba(1, 1, 1, 0.12) : "transparent")
                    border.color: Qt.rgba(1, 1, 1, 0.16)
                    Behavior on color { ColorAnimation { duration: 150 } }
                    Text { anchors.centerIn: parent; text: modelData.icon; color: win.page === index ? "#111" : Theme.text; font.pixelSize: 18 }
                    MouseArea { id: navMa; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: win.page = index }
                    ToolTip.visible: navMa.containsMouse
                    ToolTip.text: modelData.tip
                    ToolTip.delay: 400
                }
            }
        }
        // Indicadores rapidos abajo
        Column {
            anchors { bottom: parent.bottom; horizontalCenter: parent.horizontalCenter; bottomMargin: 18 }
            spacing: 10
            Rectangle { width: 10; height: 10; radius: 5; anchors.horizontalCenter: parent.horizontalCenter
                        color: Studio.streaming ? Theme.live : Theme.textFaint
                        ToolTip.visible: sMa.containsMouse; ToolTip.text: Studio.streaming ? "Al aire" : "Fuera del aire"
                        MouseArea { id: sMa; anchors.fill: parent; anchors.margins: -6; hoverEnabled: true } }
            Rectangle { width: 10; height: 10; radius: 5; anchors.horizontalCenter: parent.horizontalCenter
                        color: Studio.recording ? Theme.accent : Theme.textFaint }
            Rectangle { width: 10; height: 10; radius: 5; anchors.horizontalCenter: parent.horizontalCenter
                        color: Studio.gatewayAlive ? Theme.ok : Theme.live }
        }
    }

    // --- Avisos (toasts) ------------------------------------------------------------
    Column {
        id: toasts
        anchors { right: parent.right; bottom: parent.bottom; margins: 22 }
        spacing: 8
        width: 380
        function show(message, level) {
            toastComp.createObject(toasts, { message: message, level: level || "info" })
        }
    }
    Component {
        id: toastComp
        Rectangle {
            id: t
            property string message
            property string level
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
            Label { text: "Clave de transmision (no se guarda)" }
            TextField { id: keyField; Layout.preferredWidth: 420; echoMode: TextInput.Password }
        }
    }
    Dialog {
        id: stopDlg
        title: "¿Cortar la transmision?"
        anchors.centerIn: parent
        modal: true
        standardButtons: Dialog.Yes | Dialog.No
        onAccepted: Studio.stopStreaming()
        Label { text: "La emision en vivo se va a detener." }
    }
}
