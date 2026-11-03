// El tablero web (/control/app/): la ventana del navegador. Adentro va el MISMO DashboardPage
// de la app (src/app/qml/pages/DashboardPage.qml) con sus tarjetas; esto es solo el marco:
// arriba el preview, el programa y el TAKE (lo que se pidió para la superficie), y lo que en
// la app hace la ventana principal (confirmar, avisos) para las tarjetas que lo piden.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import BA
import "qrc:/qml/controls"
import "qrc:/qml/pages"

ApplicationWindow {
    id: win
    visible: true
    title: "Broadcast Appliance"
    color: Theme.bg
    font.family: Theme.font
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

    // --- Lo que las tarjetas le piden a la ventana (Main.qml en la app) ----------------
    function confirm(opts) { confirmDlg.ask(opts) }
    function toast(message, level) { avisos.mostrar(message, level) }
    function soloEnLaApp() { toast(qsTr("Eso se hace en la app."), "info") }
    function showPage(i) { soloEnLaApp() }
    function sacarPanel(id, titulo) { soloEnLaApp() }
    function sacarAVentana(i, titulo) { soloEnLaApp() }
    function pedirAlAsistente(texto) { soloEnLaApp() }
    function openStreamDialog() {
        if (Studio.streamServer) Studio.emision.startStreaming("", "")
        else soloEnLaApp()
    }
    function confirmStopStream() {
        confirm({
            title: qsTr("¿Cortar la transmisión?"),
            text: qsTr("La emisión en vivo se va a detener."),
            accept: qsTr("Cortar"),
            reject: qsTr("Cancelar"),
            action: function() { Studio.emision.stopStreaming() }
        })
    }

    // Lo que la app le avisa al operador (Studio.notify) también llega acá.
    Connections {
        target: Replica
        function onAviso(texto, nivel) { avisos.mostrar(texto, nivel) }
    }

    // --- Arriba: preview, programa y TAKE ----------------------------------------------
    // En un teléfono parado no entran los dos monitores lado a lado: van uno sobre otro.
    readonly property bool angosta: width < 760
    Rectangle {
        id: arriba
        anchors { left: parent.left; right: parent.right; top: parent.top }
        height: win.angosta ? Math.min(win.height * 0.62, (width - 24) * 9 / 16 * 2 + 90)
                            : Math.min(win.height * 0.36, ((width - 200) / 2) * 9 / 16 + 36)
        color: Theme.bg

        GridLayout {
            anchors { fill: parent; margins: 10 }
            columns: win.angosta ? 1 : 3
            columnSpacing: 10
            rowSpacing: 8

            Monitor {
                Layout.fillWidth: true; Layout.fillHeight: true
                clave: "armada"
                etiqueta: qsTr("PREVIEW · %1").arg(Studio.escenaArmada || qsTr("nada"))
                colorBorde: Theme.ok
            }
            ColumnLayout {
                Layout.preferredWidth: win.angosta ? -1 : 150
                Layout.fillWidth: win.angosta
                Layout.fillHeight: !win.angosta
                spacing: 8
                Item { Layout.fillHeight: true; visible: !win.angosta }
                Button {
                    id: take
                    Layout.fillWidth: true
                    Layout.preferredHeight: 64
                    enabled: Studio.escenaArmada !== "" && Replica.escribe
                    font { pixelSize: 22; weight: Font.Bold; family: Theme.font }
                    text: qsTr("TAKE")
                    onClicked: Studio.escenas.take()
                    background: Rectangle {
                        radius: 14
                        color: !take.enabled ? Qt.rgba(1, 1, 1, 0.06) : take.down ? Qt.darker(Theme.live, 1.3) : Theme.live
                        border.color: Qt.rgba(1, 1, 1, 0.2)
                    }
                    contentItem: Text {
                        text: take.text; font: take.font; color: take.enabled ? "white" : Theme.textFaint
                        horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter
                    }
                }
                Button {
                    id: cut
                    Layout.fillWidth: true
                    Layout.preferredHeight: 40
                    enabled: Studio.escenaArmada !== "" && Replica.escribe
                    text: qsTr("CUT")
                    onClicked: Studio.escenas.takeCon("cut", 0)
                }
                Item { Layout.fillHeight: true; visible: !win.angosta }
            }
            Monitor {
                Layout.fillWidth: true; Layout.fillHeight: true
                clave: "program"
                etiqueta: qsTr("PROGRAMA · %1").arg(Studio.currentScene)
                colorBorde: Theme.live
            }
        }
    }

    component Monitor: Rectangle {
        id: mon
        property string clave
        property string etiqueta
        property color colorBorde
        color: "black"
        radius: 10
        border { color: mon.colorBorde; width: 2 }
        clip: true
        VideoItem { id: video; anchors { fill: parent; margins: 2 } source: mon.clave; fillMode: VideoItem.Fit; topeCps: 30 }
        Text {
            visible: !video.hasFrame
            anchors.centerIn: parent
            text: qsTr("Sin señal")
            color: Theme.textFaint
            font.pixelSize: 13
        }
        Rectangle {
            anchors { left: parent.left; top: parent.top; margins: 8 }
            radius: 6; color: Qt.rgba(0, 0, 0, 0.6)
            width: rotulo.implicitWidth + 14; height: rotulo.implicitHeight + 6
            Text { id: rotulo; anchors.centerIn: parent; text: mon.etiqueta; color: Theme.text; font { pixelSize: 12; weight: Font.DemiBold } }
        }
        // Cuánto tarda en verse (desde que el cuadro llegó a la app): ?hud=1.
        Text {
            visible: HudWeb
            anchors { right: parent.right; bottom: parent.bottom; margins: 8 }
            text: video.retardoMs >= 0 ? video.retardoMs + " ms" : ""
            color: "#9be59b"; font { pixelSize: 12; family: Theme.mono }
        }
    }

    // --- El dashboard de la app, tal cual ------------------------------------------------
    // En una pantalla chica (un teléfono) el tablero se arma a su tamaño mínimo de la app y
    // se recorre deslizando, en vez de apretar las tarjetas hasta que no se leen.
    Flickable {
        id: recorrido
        anchors { left: parent.left; right: parent.right; top: arriba.bottom; bottom: parent.bottom }
        readonly property bool chica: width < 1000 || height < 560
        contentWidth: chica ? Math.max(width, 1100) : width
        contentHeight: chica ? Math.max(height, 640) : height
        interactive: chica
        boundsBehavior: Flickable.StopAtBounds
        clip: true
        DashboardPage {
            width: recorrido.contentWidth
            height: recorrido.contentHeight
            leftInset: 12
        }
    }

    // --- Avisos y confirmaciones -----------------------------------------------------------
    ConfirmDialog { id: confirmDlg }
    Column {
        id: avisos
        z: 1000
        anchors { right: parent.right; bottom: parent.bottom; margins: 18 }
        spacing: 8
        function mostrar(texto, nivel) {
            const c = cmpAviso.createObject(avisos, { texto: texto, nivel: nivel || "info" })
        }
    }
    Component {
        id: cmpAviso
        Rectangle {
            property string texto
            property string nivel
            width: Math.min(420, win.width - 36); height: t.implicitHeight + 20
            radius: 10
            color: nivel === "error" ? "#7a2020" : nivel === "warn" ? "#6b5410" : Theme.paletaBoton
            border.color: Qt.rgba(1, 1, 1, 0.15)
            Text { id: t; anchors { fill: parent; margins: 10 } text: parent.texto; wrapMode: Text.WordWrap; color: Theme.text; font.pixelSize: 13 }
            Timer { running: true; interval: 4500; onTriggered: parent.destroy() }
        }
    }
}
