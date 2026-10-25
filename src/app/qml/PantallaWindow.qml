// Lo que va en un monitor de la máquina (Salidas → Pantallas → Pantallas
// conectadas): una cosa del catálogo, o varias en grilla, en SU monitor.
//
// Una por monitor con algo asignado y conectado (Main.qml la crea desde
// Studio.pantallasConectadas.activos). El monitor se busca por su id estable
// (fabricante, modelo, serie), no por el índice: si se desenchufa, esta
// ventana se va —su contenido NO se muda a otro monitor— y cuando vuelve,
// vuelve ella sola.
//
// Cerrarla (Cmd+W, el menú del sistema) es lo mismo que «Quitar» en la página.
import QtQuick
import QtQuick.Controls
import QtQuick.Window
import BA
import "controls"

Window {
    id: w
    required property string monitorId
    required property var cosas
    required property bool pantallaCompleta
    required property int orden
    required property string titulo
    /// La ventana principal (le da los componentes a VistaCatalogo).
    required property var principal

    // Para ventana_listar / ventana_captura (MCP).
    objectName: "ba:monitor:" + monitorId
    title: "Broadcast Appliance — Monitor " + orden + ": " + titulo
    color: "black"
    visibility: Window.Hidden

    /// El QScreen de este monitor, o null si (todavía) no está.
    readonly property var pantalla: {
        const ss = Qt.application.screens
        for (let i = 0; i < ss.length; ++i)
            if (Studio.pantallasConectadas.idDePantalla(ss[i].name, ss[i].manufacturer, ss[i].model,
                                                         ss[i].serialNumber) === w.monitorId)
                return ss[i]
        return null
    }

    function ubicar() {
        // Sin su monitor no se muestra en ningún otro (ver arriba).
        if (!w.pantalla) { w.visibility = Window.Hidden; return }
        w.screen = w.pantalla
        w.x = w.pantalla.virtualX
        w.y = w.pantalla.virtualY
        w.width = w.pantalla.width
        w.height = w.pantalla.height
        // Donde el escritorio no deja ubicar ventanas (Wayland), lo único que
        // respeta es pantalla completa EN un monitor.
        w.visibility = w.pantallaCompleta || !Studio.puedeUbicarVentanas ? Window.FullScreen : Window.Maximized
    }
    onPantallaChanged: ubicar()
    onPantallaCompletaChanged: ubicar()
    Component.onCompleted: ubicar()
    onClosing: (close) => Studio.pantallasConectadas.quitar(w.monitorId)

    // --- Lo que muestra: una cosa, o varias en grilla --------------------------
    // La misma vista que las ventanas sueltas (VistaCosas).
    VistaCosas {
        anchors.fill: parent
        cosas: w.cosas
        principal: w.principal
        // Una sola cosa en un monitor: a pantalla entera, sin marco.
        marcoSolo: false
    }

    // --- Con el mouse, qué es este monitor y cómo cambiarlo ---------------------
    // HoverHandler y no MouseArea: lo de adentro (una página, un panel) sigue
    // recibiendo los clics.
    HoverHandler {
        id: encima
        onPointChanged: { aviso.opacity = 1; esconder.restart() }
    }
    Timer { id: esconder; interval: 2500; onTriggered: aviso.opacity = 0 }
    Rectangle {
        id: aviso
        z: 1000
        anchors { bottom: parent.bottom; horizontalCenter: parent.horizontalCenter; bottomMargin: 18 }
        width: fila.implicitWidth + 24
        height: 40
        radius: 20
        color: Qt.rgba(0.07, 0.07, 0.08, 0.86)
        border.color: Qt.rgba(1, 1, 1, 0.14)
        opacity: 0
        visible: opacity > 0
        Behavior on opacity { NumberAnimation { duration: 250 } }
        HoverHandler { onHoveredChanged: if (hovered) { aviso.opacity = 1; esconder.stop() } else esconder.restart() }
        Row {
            id: fila
            anchors.centerIn: parent
            spacing: 8
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: "Monitor " + w.orden + " · se cambia en Salidas → Pantallas"
                color: Theme.textDim; font.pixelSize: 12
            }
            GlassButton {
                compact: true
                text: w.pantallaCompleta ? "Salir de pantalla completa" : "Pantalla completa"
                active: w.pantallaCompleta
                onClicked: Studio.pantallasConectadas.setPantallaCompleta(w.monitorId, !w.pantallaCompleta)
            }
            GlassButton {
                compact: true; text: "Quitar"; icon: "✕"
                onClicked: Studio.pantallasConectadas.quitar(w.monitorId)
            }
        }
    }
}
