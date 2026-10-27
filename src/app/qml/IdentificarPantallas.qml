// El cartel que dice cuál monitor es cuál.
//
// POR QUE HACE FALTA. Los menús ofrecen «eDP-1» y «DP-1», que no le dicen nada
// a nadie: para saber cuál es el de la izquierda había que mandar una ventana
// y ver adónde iba. Esto pone el número y el nombre, grandes, en cada pantalla
// a la vez, que es como lo resuelven los sistemas operativos.
//
// EL MISMO NÚMERO QUE LA PÁGINA. Los monitores salen de Studio.monitores (el
// orden físico de pantallas/Monitores, de izquierda a derecha): el «2» del
// cartel es el «2» de la página Pantallas, no el orden en que el sistema los
// lista, que puede ser cualquiera.
//
// CÓMO SE PONE, SEGÚN EL SISTEMA. En Linux (Wayland) un programa no puede
// colocar sus ventanas: lo único que el compositor respeta es PANTALLA
// COMPLETA EN UN MONITOR, y de paso eso prueba que mandar una ventana a un
// monitor va a andar. En macOS, en cambio, la pantalla completa es la nativa
// (un escritorio propio con animación) y no se puede pedir para varias
// ventanas a la vez: entraba UNA y las demás no. Ahí, y en Windows, el cartel
// es una ventana sin bordes, encima de todo, del tamaño del monitor.
import QtQuick
import QtQuick.Window
import BA

Item {
    id: raiz
    /// Cuántos milisegundos queda puesto.
    property int duracionMs: 4000
    /// Los monitores a identificar (ids de pantallas/Monitores); vacío = todos.
    property var ids: []
    readonly property bool completa: Qt.platform.os === "linux"

    function mostrar(soloEstos) {
        raiz.ids = soloEstos || []
        raiz.abierto = false // si ya estaba puesto, se rearma con los de ahora
        raiz.abierto = true
        cierre.restart()
    }

    property bool abierto: false
    Timer { id: cierre; interval: raiz.duracionMs; onTriggered: raiz.abierto = false }

    function pantallaDe(sistema) {
        const ss = Qt.application.screens
        for (let i = 0; i < ss.length; ++i)
            if (ss[i].name === sistema) return ss[i]
        return null
    }

    Instantiator {
        model: raiz.abierto ? Studio.monitores.filter(m => raiz.ids.length === 0 || raiz.ids.indexOf(m.id) >= 0) : []
        delegate: Window {
            id: cartel
            required property var modelData
            screen: raiz.pantallaDe(modelData.sistema)
            x: modelData.x; y: modelData.y
            width: modelData.ancho; height: modelData.alto
            visibility: raiz.completa ? Window.FullScreen : Window.Windowed
            visible: true
            color: "#0b0d12"
            flags: raiz.completa ? (Qt.Window | Qt.FramelessWindowHint)
                                 : (Qt.Window | Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint)

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
                        text: cartel.modelData.orden
                        color: "white"
                        font { family: Theme.font; pixelSize: Math.round(parent.parent.height * 0.42)
                               weight: Font.Bold }
                    }
                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: cartel.modelData.nombre
                        color: "white"
                        font { family: Theme.font; pixelSize: 26; weight: Font.DemiBold }
                    }
                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: cartel.modelData.principal
                              ? qsTr("%1 × %2   ·   principal").arg(cartel.modelData.ancho).arg(cartel.modelData.alto)
                              : cartel.modelData.ancho + " × " + cartel.modelData.alto
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
