// Bandera argentina flameando: una mosca chica, con transparencia, en bucle.
//
// CÓMO FLAMEA SIN SHADERS. La tela (franjas celeste-blanco-celeste y el Sol de
// Mayo) se dibuja una vez, oculta, y se muestra cortada en franjas verticales
// angostas (ShaderEffectSource con sourceRect: no pide shaders compilados). Cada
// franja baja o sube según una onda que viaja del asta hacia la punta y crece con
// la distancia al asta (donde está atada no se mueve), y lleva una sombra o un
// brillo según la pendiente de la onda en ese punto: eso es lo que hace que se
// lea como tela y no como una imagen que ondula.
//
// EL BUCLE. La fase da una vuelta entera en `periodo` ms, igual a duracionMs de la
// ficha: el cuadro siguiente al último es el primero, sin salto.
import QtQuick

Item {
    id: raiz
    width: 240
    height: 160

    // --- Parámetros -----------------------------------------------------------
    property real opacidad: 0.82
    property real ondas: 1

    readonly property int periodo: 2000
    readonly property int franjas: 80
    // La tela, dentro del lienzo, con lugar para que la onda no se corte.
    readonly property real telaAncho: width * 0.84
    readonly property real telaAlto: telaAncho * 0.625 // proporción 1:1,6 (Ley 23.208)
    readonly property real amplitud: telaAlto * 0.07 * ondas

    property real fase: 0
    NumberAnimation on fase { from: 0; to: 1; duration: raiz.periodo; loops: Animation.Infinite }

    // --- La tela, oculta: se muestra por franjas --------------------------------
    Item {
        id: tela
        width: raiz.telaAncho
        height: raiz.telaAlto
        visible: false
        readonly property color celeste: "#74ACDF"
        Rectangle { width: parent.width; height: parent.height / 3; color: tela.celeste }
        Rectangle { y: parent.height / 3; width: parent.width; height: parent.height / 3; color: "white" }
        Rectangle { y: 2 * parent.height / 3; width: parent.width; height: parent.height / 3 + 1; color: tela.celeste }

        // El Sol de Mayo: 32 rayos alternados (rectos y largos / cortos) y el disco.
        Item {
            id: sol
            readonly property real r: tela.height * 0.12
            anchors.centerIn: parent
            width: r * 2; height: r * 2
            Repeater {
                model: 32
                Rectangle {
                    required property int index
                    readonly property bool largo: index % 2 === 0
                    width: sol.r * (largo ? 0.22 : 0.14)
                    height: sol.r * (largo ? 1.0 : 0.8)
                    x: sol.width / 2 - width / 2
                    y: sol.height / 2 - sol.r * (largo ? 1.75 : 1.55)
                    color: "#F6B40E"
                    antialiasing: true
                    transform: Rotation { origin.x: width / 2; origin.y: sol.r * (largo ? 1.75 : 1.55); angle: index * 360 / 32 }
                }
            }
            Rectangle {
                anchors.centerIn: parent
                width: sol.r * 1.5; height: width; radius: width / 2
                color: "#F6B40E"
                border.color: "#85340A"; border.width: Math.max(1, sol.r * 0.06)
                antialiasing: true
            }
        }
    }

    // --- Las franjas que flamean ------------------------------------------------
    Item {
        id: flameo
        // La transparencia, al CONJUNTO (una capa): puesta a cada franja, el píxel en
        // que se solapan dos quedaba más opaco y se veían rayitas verticales.
        layer.enabled: true
        layer.smooth: true
        opacity: raiz.opacidad
        // Todo el lienzo (no solo la tela): la capa recorta a su rectángulo, y la onda
        // se sale de la tela para arriba y para abajo.
        anchors.fill: parent
        readonly property real x0: (raiz.width - raiz.telaAncho) / 2
        readonly property real y0: (raiz.height - raiz.telaAlto) / 2

        Repeater {
            model: raiz.franjas
            Item {
                id: franja
                required property int index
                readonly property real ancho: raiz.telaAncho / raiz.franjas
                // 0 en el asta, 1 en la punta: atada de un lado, suelta del otro.
                readonly property real u: index / (raiz.franjas - 1)
                readonly property real angulo: 2 * Math.PI * (raiz.fase * 1 - u * 1.25)
                readonly property real crece: 0.25 + 0.75 * u
                readonly property real enTela: index * ancho
                x: flameo.x0 + enTela
                y: flameo.y0 + Math.sin(angulo) * raiz.amplitud * crece
                width: Math.ceil(ancho) + 1 // un píxel de solape: sin rayas entre franjas
                height: raiz.telaAlto

                ShaderEffectSource {
                    anchors.fill: parent
                    sourceItem: tela
                    sourceRect: Qt.rect(franja.enTela, 0, franja.width, raiz.telaAlto)
                    smooth: true
                }
                // Sombra o brillo según la pendiente: la luz viene de arriba a la izquierda.
                Rectangle {
                    width: franja.ancho // sin el solape: dos sombras encimadas se ven como una raya
                    height: parent.height
                    readonly property real p: Math.cos(franja.angulo) * franja.crece * Math.min(1, raiz.ondas)
                    color: p > 0 ? "black" : "white"
                    opacity: Math.abs(p) * (p > 0 ? 0.22 : 0.16)
                }
            }
        }
    }
}
