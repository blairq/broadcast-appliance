// El tablero web (/control/app/): la ventana del navegador. Adentro van las MISMAS tarjetas de
// la app (src/app/qml: DashboardPage y cards/), pero el tablero es propio de este aparato: su
// disposición se guarda en el registro del aparato en la app (Studio.dashboard acá es local,
// ver main.cpp) y nunca toca la del tablero de la app.
//
// Arriba: qué salida maneja el aparato (un botón por salida, con su color) y, plegables, el
// preview, el programa y el TAKE de esa salida. Todo el tablero trabaja sobre la salida elegida:
// la app responde por este aparato como si su salida activa fuera esa (ReplicaPublicador).
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
    // Solo lo que tiene sentido en otro aparato: abrir páginas o ventanas de la app no está
    // (los botones que lo hacen no se muestran en la web).
    function confirm(opts) { confirmDlg.ask(opts) }
    function toast(message, level) { avisos.mostrar(message, level) }
    function abrirPestana(vista) { Pagina.abrirPestana(vista) }
    function openStreamDialog() { Studio.emision.startStreaming("", "") }
    function confirmStopStream() {
        confirm({
            title: qsTr("¿Cortar la transmisión?"),
            text: qsTr("La emisión en vivo se va a detener."),
            accept: qsTr("Cortar"),
            reject: qsTr("Cancelar"),
            action: function() { Studio.emision.stopStreaming() }
        })
    }

    Connections {
        target: Replica
        // Lo que la app le avisa al operador (Studio.notify) y por qué no se hizo un botón.
        function onAviso(texto, nivel) { avisos.mostrar(texto, nivel) }
        // Toca el aire y hay aire: la misma pregunta que el control web.
        function onConfirmar(id, texto) {
            win.confirm({
                title: qsTr("Cambia el aire"),
                text: texto,
                accept: qsTr("Sí, hacelo"),
                reject: qsTr("Cancelar"),
                action: function() { Replica.confirmarLlamada(id) }
            })
        }
    }

    readonly property bool angosta: width < 760
    // Plegado se recuerda en este navegador (es de la pantalla, no del aparato).
    property bool monitoresPlegados: Pagina.preferencia("monitoresPlegados") === "1"
    onMonitoresPlegadosChanged: Pagina.guardarPreferencia("monitoresPlegados", monitoresPlegados ? "1" : "0")

    // --- Barra: salidas y plegar -------------------------------------------------------
    Rectangle {
        id: barra
        anchors { left: parent.left; right: parent.right; top: parent.top }
        height: 34
        color: Theme.bg
        RowLayout {
            anchors { fill: parent; leftMargin: 10; rightMargin: 10 }
            spacing: 6
            // Un botón por salida (con su color), la elegida marcada. Con una sola, igual pero
            // discreto. La elige ESTE aparato (se recuerda en su registro de la app): la app
            // no cambia su salida activa.
            Repeater {
                model: Studio.salidas.filasLista
                delegate: Rectangle {
                    required property var modelData
                    readonly property bool elegida: Studio.salidas.activa === modelData.id
                    readonly property bool sola: !Studio.salidas.varias
                    implicitHeight: 24
                    implicitWidth: nombreSalida.implicitWidth + 26
                    radius: 5
                    opacity: sola ? 0.6 : 1
                    color: elegida && !sola ? Qt.rgba(1, 1, 1, 0.14) : Qt.rgba(1, 1, 1, 0.04)
                    border { width: elegida && !sola ? 2 : 1; color: modelData.color || Theme.glassBorder }
                    Rectangle { width: 8; height: 8; radius: 4; color: modelData.color || Theme.textFaint
                                anchors { left: parent.left; leftMargin: 8; verticalCenter: parent.verticalCenter } }
                    Text { id: nombreSalida; anchors { left: parent.left; leftMargin: 20; verticalCenter: parent.verticalCenter }
                           text: modelData.nombre; color: Theme.text; font.pixelSize: 12 }
                    MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; enabled: !parent.sola
                                onClicked: Studio.salidas.activa = modelData.id }
                }
            }
            Item { Layout.fillWidth: true }
            BotonBarra {
                text: win.monitoresPlegados ? qsTr("▾ Mostrar monitores") : qsTr("▴ Ocultar monitores")
                onClicked: win.monitoresPlegados = !win.monitoresPlegados
            }
            BotonBarra {
                text: qsTr("▦ Disposición")
                onClicked: menuDisposicion.popup()
                Menu {
                    id: menuDisposicion
                    MenuItem {
                        text: qsTr("Restablecer la disposición de este aparato")
                        onTriggered: win.confirm({
                            title: qsTr("¿Restablecer la disposición?"),
                            text: qsTr("Las tarjetas de este aparato vuelven a como vinieron de fábrica. El tablero de la app no cambia."),
                            accept: qsTr("Restablecer"),
                            reject: qsTr("Cancelar"),
                            action: function() { Studio.dashboard.resetLayout() }
                        })
                    }
                }
            }
            BotonBarra {
                text: Pagina.pantallaCompleta ? qsTr("⤡ Salir de pantalla completa") : qsTr("⤢ Pantalla completa")
                onClicked: {
                    const e = Pagina.alternarPantallaCompleta()
                    if (e !== "") avisos.mostrar(e, "info")
                }
            }
        }
    }

    component BotonBarra: Text {
        signal clicked()
        color: zona.containsMouse ? Theme.text : Theme.textDim
        font.pixelSize: 12
        MouseArea { id: zona; anchors.fill: parent; anchors.margins: -6; hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor; onClicked: parent.clicked() }
    }

    // --- Preview, programa y TAKE de la salida elegida (plegable) ---------------------
    Rectangle {
        id: arriba
        anchors { left: parent.left; right: parent.right; top: barra.bottom }
        // Plegado no está: sus VideoItem dejan de verse y la app deja de mandar esos cuadros.
        visible: !win.monitoresPlegados
        height: !visible ? 0
                : win.angosta ? Math.min(win.height * 0.62, (width - 24) * 9 / 16 * 2 + 90)
                : Math.min(win.height * 0.34, ((width - 200) / 2) * 9 / 16 + 20)
        color: Theme.bg

        GridLayout {
            anchors { fill: parent; leftMargin: 10; rightMargin: 10; bottomMargin: 6 }
            columns: win.angosta ? 1 : 3
            columnSpacing: 8
            rowSpacing: 6

            Monitor {
                Layout.fillWidth: true; Layout.fillHeight: true
                clave: Studio.salidas.claveArmadaActiva || "armada"
                etiqueta: qsTr("PREVIEW · %1").arg(Studio.escenaArmada || qsTr("nada"))
                colorBorde: Theme.ok
            }
            ColumnLayout {
                Layout.preferredWidth: win.angosta ? -1 : 150
                Layout.fillWidth: win.angosta
                Layout.fillHeight: !win.angosta
                spacing: 6
                Item { Layout.fillHeight: true; visible: !win.angosta }
                Button {
                    id: take
                    Layout.fillWidth: true
                    Layout.preferredHeight: 60
                    enabled: Studio.escenaArmada !== "" && Replica.escribe
                    font { pixelSize: 22; weight: Font.Bold; family: Theme.font }
                    text: qsTr("TAKE")
                    onClicked: Studio.escenas.take()
                    background: Rectangle {
                        radius: 6
                        color: !take.enabled ? Qt.rgba(1, 1, 1, 0.06) : take.down ? Qt.darker(Theme.live, 1.3) : Theme.live
                        border.color: Qt.rgba(1, 1, 1, 0.2)
                    }
                    contentItem: Text {
                        text: take.text; font: take.font; color: take.enabled ? "white" : Theme.textFaint
                        horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter
                    }
                }
                Button {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 36
                    enabled: Studio.escenaArmada !== "" && Replica.escribe
                    text: qsTr("CUT")
                    onClicked: Studio.escenas.takeCon("cut", 0)
                }
                Item { Layout.fillHeight: true; visible: !win.angosta }
            }
            Monitor {
                Layout.fillWidth: true; Layout.fillHeight: true
                clave: Studio.salidas.claveProgramaActiva || "program"
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
        radius: 6
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
            anchors { left: parent.left; top: parent.top; margins: 6 }
            radius: 4; color: Qt.rgba(0, 0, 0, 0.6)
            width: rotulo.implicitWidth + 12; height: rotulo.implicitHeight + 4
            Text { id: rotulo; anchors.centerIn: parent; text: mon.etiqueta; color: Theme.text; font { pixelSize: 11; weight: Font.DemiBold } }
        }
        // Cuánto tarda en verse (desde que el cuadro llegó a la app): ?hud=1.
        Text {
            visible: HudWeb
            anchors { right: parent.right; bottom: parent.bottom; margins: 6 }
            text: video.retardoMs >= 0 ? video.retardoMs + " ms" : ""
            color: "#9be59b"; font { pixelSize: 12; family: Theme.mono }
        }
    }

    // --- El tablero (las tarjetas de la app, con la disposición de este aparato) ----------
    // En una pantalla chica (un teléfono) se arma a su tamaño mínimo y se recorre deslizando.
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
            leftInset: 6
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
            cmpAviso.createObject(avisos, { texto: texto, nivel: nivel || "info" })
        }
    }
    Component {
        id: cmpAviso
        Rectangle {
            property string texto
            property string nivel
            width: Math.min(420, win.width - 36); height: t.implicitHeight + 20
            radius: 6
            color: nivel === "error" ? "#7a2020" : nivel === "warn" ? "#6b5410" : Theme.paletaBoton
            border.color: Qt.rgba(1, 1, 1, 0.15)
            Text { id: t; anchors { fill: parent; margins: 10 } text: parent.texto; wrapMode: Text.WordWrap; color: Theme.text; font.pixelSize: 13 }
            Timer { running: true; interval: 4500; onTriggered: parent.destroy() }
        }
    }
}
