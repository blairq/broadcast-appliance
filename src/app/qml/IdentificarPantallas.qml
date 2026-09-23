// El cartel que dice cuál monitor es cuál.
//
// POR QUE HACE FALTA. Los menús ofrecen «eDP-1» y «DP-1», que no le dicen nada
// a nadie: para saber cuál es el de la izquierda había que mandar una ventana
// y ver adónde iba. Esto pone el número y el nombre, grandes, en cada pantalla
// a la vez, que es como lo resuelven los sistemas operativos.
//
// Y DE PASO PRUEBA SI SE PUEDE. En un escritorio que no deja que un programa
// coloque sus ventanas, esto es lo único que igual funciona: no pide una
// posición, pide PANTALLA COMPLETA EN UN MONITOR, que es una petición
// distinta y los compositores sí la respetan. Si aparecen los dos carteles en
// los dos monitores, mandar una ventana a un monitor también va a andar.
import QtQuick
import QtQuick.Window
import BA

Item {
    id: raiz
    /// Cuántos milisegundos queda puesto.
    property int duracionMs: 4000

    function mostrar() {
        raiz.hasta = Date.now() + raiz.duracionMs
        raiz.abierto = true
        cierre.restart()
    }

    property bool abierto: false
    property double hasta: 0
    Timer { id: cierre; interval: raiz.duracionMs; onTriggered: raiz.abierto = false }

    Instantiator {
        model: raiz.abierto ? Qt.application.screens : []
        delegate: Window {
            required property var modelData
            required property int index
            screen: modelData
            // Pantalla completa y no una geometría: en Wayland pedir una
            // posición no hace nada, pero pantalla completa en un monitor sí.
            visibility: Window.FullScreen
            visible: true
            color: "#0b0d12"
            flags: Qt.Window | Qt.FramelessWindowHint

            Rectangle {
                anchors.centerIn: parent
                width: Math.min(parent.width * 0.5, 520)
                height: width * 0.62
                radius: 28
                color: Qt.rgba(1, 0.42, 0.17, 0.92)
                Column {
                    anchors.centerIn: parent
                    spacing: 6
                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: index + 1
                        color: "white"
                        font { family: Theme.font; pixelSize: Math.round(parent.parent.height * 0.42)
                               weight: Font.Bold }
                    }
                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: modelData.name
                        color: "white"
                        font { family: Theme.font; pixelSize: 26; weight: Font.DemiBold }
                    }
                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: modelData.width + " × " + modelData.height +
                              (modelData === Qt.application.screens[0] ? "   ·   principal" : "")
                        color: Qt.rgba(1, 1, 1, 0.8)
                        font { family: Theme.font; pixelSize: 15 }
                    }
                }
            }
            // Un clic o Esc lo saca antes: si tapó algo justo cuando hacía
            // falta, no hay que esperarlo.
            MouseArea { anchors.fill: parent; onClicked: raiz.abierto = false }
            Shortcut { sequence: "Esc"; onActivated: raiz.abierto = false }
        }
    }
}
