// La barra de una ventana suelta: a qué monitor va, pantalla completa, cerrar.
//
// UNA SOLA, USADA POR LAS DOS CLASES DE VENTANA (las de video y las pestañas
// sacadas). Estaba solo en las de video; las pestañas no tenían nada y había
// que acomodarlas a mano desde el escritorio. Copiar la barra habría dejado
// dos que se desacomodan por separado —justo el problema que hubo que
// arreglar en media interfaz hoy—, así que vive acá.
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

    readonly property bool completa: ventana.visibility === Window.FullScreen

    function aMonitor(pantalla) {
        const volver = completa || !Studio.puedeUbicarVentanas
        if (completa) ventana.visibility = Window.Windowed
        ventana.screen = pantalla
        if (volver) reentrar.restart()
    }
    Timer { id: reentrar; interval: 180; onTriggered: barra.ventana.visibility = Window.FullScreen }

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

            GlassButton {
                compact: true; text: "Monitor"; icon: "▭"
                onClicked: menuMonitores.popup()
                Menu {
                    id: menuMonitores
                    onAboutToShow: esconder.stop()
                    onClosed: esconder.restart()
                    MenuItem {
                        // «eDP-1» no le dice nada a nadie hasta que se lo ve
                        // puesto en la pantalla que es.
                        text: "Identificar pantallas"
                        onTriggered: Studio.identificarPantallas()
                    }
                    MenuSeparator {}
                    Repeater {
                        model: Qt.application.screens
                        delegate: MenuItem {
                            required property var modelData
                            text: modelData.name + "  (" + modelData.width + "×" + modelData.height + ")"
                            checkable: true
                            checked: barra.ventana.screen && barra.ventana.screen.name === modelData.name
                            onTriggered: barra.aMonitor(modelData)
                        }
                    }
                    MenuItem {
                        enabled: false
                        visible: !Studio.puedeUbicarVentanas
                        height: visible ? implicitHeight : 0
                        text: "(este escritorio solo deja mandarla a pantalla completa)"
                    }
                }
            }
            GlassButton {
                compact: true
                text: barra.completa ? "Ventana" : "Pantalla completa"
                icon: barra.completa ? "⧉" : "⛶"
                onClicked: barra.ventana.visibility =
                    barra.completa ? Window.Windowed : Window.FullScreen
            }
            GlassButton {
                compact: true; text: "Cerrar"; icon: "✕"
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
