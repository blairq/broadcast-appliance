// La barra de una ventana suelta: a qué monitor va, pantalla completa, cerrar.
//
// UNA SOLA, la de la ventana suelta (VentanaSuelta: video, páginas, paneles).
// Estaba solo en las de video; las pestañas no tenían nada y había que
// acomodarlas a mano desde el escritorio. Copiar la barra habría dejado dos
// que se desacomodan por separado, así que vive acá.
//
// SE ESCONDE SOLA. En un monitor de sala una barra permanente molesta; con el
// mouse encima vuelve. Mientras el menú está abierto NO se esconde: un menú
// cuyo ancestro se vuelve invisible se cierra con él, y el botón parecía roto.
//
// CAMBIAR DE MONITOR ESTANDO EN PANTALLA COMPLETA: hay que SALIR, cambiar y
// volver a ENTRAR, con una pausa. El compositor recibió «pantalla completa en
// el monitor A»; cambiarle la pantalla después no le vuelve a pedir nada.
import QtQuick
import QtQuick.Controls
import BA

Item {
    id: barra
    anchors { top: parent.top; horizontalCenter: parent.horizontalCenter; topMargin: 16 }
    width: caja.width
    height: caja.height

    /// La ventana que maneja (se le cambia `screen` y `visibility`).
    required property Window ventana
    /// Se llama para cerrar. Vacío = `ventana.close()`.
    property var alCerrar: null
    /// «Dejar fijo en este monitor» (pasa a Salidas → Pantallas): se llama con el
    /// id del monitor donde está la ventana. Vacío = no se ofrece.
    property var alFijar: null
    /// Botones propios de la ventana (distribución, qué multivista), antes de los de siempre.
    property alias extras: extra.data

    /// El id estable (Salidas → Pantallas) del monitor donde está la ventana.
    function idDeMonitor(s) {
        return s ? Studio.pantallasConectadas.idDePantalla(s.name, s.manufacturer, s.model, s.serialNumber) : ""
    }
    function pantallaDeSistema(nombre) {
        const ss = Qt.application.screens
        for (let i = 0; i < ss.length; ++i) if (ss[i].name === nombre) return ss[i]
        return null
    }

    readonly property bool completa: ventana.visibility === Window.FullScreen

    // CAMBIAR `screen` NO MUEVE LA VENTANA (en Mac, y en Windows según el
    // caso): queda donde estaba, con las coordenadas del monitor viejo, y el
    // botón parecía no hacer nada. Además de la pantalla hay que llevarla:
    // al centro del monitor elegido, achicada si no entra.
    property var destino: null
    property bool volver: false
    function aMonitor(pantalla) {
        destino = pantalla
        volver = completa || !Studio.escritorio.puedeUbicarVentanas
        if (completa) {
            // Salir de pantalla completa tiene animación (Mac): mover recién después.
            ventana.visibility = Window.Windowed
            llevar.interval = Qt.platform.os === "osx" ? 800 : 150
            llevar.restart()
        } else {
            llevarYa()
        }
    }
    function llevarYa() {
        const s = destino
        if (!s) return
        if (ventana.visibility === Window.Maximized) ventana.visibility = Window.Windowed
        ventana.screen = s
        const w = Math.min(ventana.width, s.width - 40), h = Math.min(ventana.height, s.height - 80)
        ventana.width = w
        ventana.height = h
        ventana.x = s.virtualX + Math.round((s.width - w) / 2)
        ventana.y = s.virtualY + Math.round((s.height - h) / 2)
        ventana.raise()
        ventana.requestActivate()
        if (volver) reentrar.restart()
    }
    Timer { id: llevar; onTriggered: barra.llevarYa() }
    Timer { id: reentrar; interval: 300; onTriggered: barra.ventana.visibility = Window.FullScreen }

    Timer { id: esconder; interval: 2200; onTriggered: caja.opacity = 0 }
    Component.onCompleted: esconder.start()

    Rectangle {
        id: caja
        width: fila.implicitWidth + 24
        height: 44
        radius: 22
        color: Qt.rgba(0.07, 0.07, 0.08, 0.82)
        border.color: Qt.rgba(1, 1, 1, 0.14)
        opacity: 1
        visible: opacity > 0
        Behavior on opacity { NumberAnimation { duration: 250 } }
        HoverHandler { onHoveredChanged: if (hovered) { caja.opacity = 1; esconder.stop() } else esconder.restart() }

        Row {
            id: fila
            anchors.centerIn: parent
            spacing: 8

            Row { id: extra; spacing: 8; visible: children.length > 0 }
            GlassButton {
                compact: true; text: qsTr("Monitor"); icon: "▭"
                onClicked: menuMonitores.popup()
                Menu {
                    id: menuMonitores
                    onAboutToShow: esconder.stop()
                    onClosed: esconder.restart()
                    MenuItem {
                        // «eDP-1» no le dice nada a nadie hasta que se lo ve
                        // puesto en la pantalla que es.
                        text: qsTr("Identificar pantallas")
                        onTriggered: Studio.escritorio.identificarPantallas()
                    }
                    MenuSeparator { id: sepMonitores }
                    // Los monitores de Salidas → Pantallas: en su orden físico y con
                    // el número que se ve en el dibujo y al identificarlos, no por
                    // el nombre del sistema.
                    ItemsDeMenu {
                        menu: menuMonitores
                        despuesDe: sepMonitores
                        model: Studio.pantallasConectadas.presentes
                        delegate: MenuItem {
                            required property string nombreSistema
                            required property string nombre
                            required property int orden
                            required property rect geometria
                            text: qsTr("Monitor %1 — %2  (%3×%4)").arg(orden).arg(nombre).arg(geometria.width).arg(geometria.height)
                            checkable: true
                            checked: barra.ventana.screen && barra.ventana.screen.name === nombreSistema
                            onTriggered: { const s = barra.pantallaDeSistema(nombreSistema); if (s) barra.aMonitor(s) }
                        }
                    }
                    MenuItem {
                        visible: !!barra.alFijar
                        height: visible ? implicitHeight : 0
                        text: qsTr("Dejar fijo en este monitor (Salidas → Pantallas)")
                        onTriggered: barra.alFijar(barra.idDeMonitor(barra.ventana.screen))
                    }
                    MenuItem {
                        enabled: false
                        visible: !Studio.escritorio.puedeUbicarVentanas
                        height: visible ? implicitHeight : 0
                        text: qsTr("(este escritorio solo deja mandarla a pantalla completa)")
                    }
                }
            }
            GlassButton {
                compact: true
                text: barra.completa ? qsTr("Ventana") : qsTr("Pantalla completa")
                icon: barra.completa ? "⧉" : "⛶"
                onClicked: barra.ventana.visibility =
                    barra.completa ? Window.Windowed : Window.FullScreen
            }
            GlassButton {
                compact: true; text: qsTr("Cerrar"); icon: "✕"
                onClicked: barra.alCerrar ? barra.alCerrar() : barra.ventana.close()
            }
        }
    }

    // El mouse sobre la ventana la trae de vuelta.
    MouseArea {
        parent: barra.ventana.contentItem
        anchors.fill: parent
        acceptedButtons: Qt.NoButton
        hoverEnabled: true
        z: -1
        onPositionChanged: { caja.opacity = 1; esconder.restart() }
    }
}
