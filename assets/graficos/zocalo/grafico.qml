// Zócalo: el nombre y el cargo de quien habla.
//
// Entra deslizándose de izquierda a derecha, el texto aparece escalonado
// detrás, y con `salidaEn` se va con desenfoque.
//
// Todo lo que el operador puede cambiar está declarado como propiedad acá
// arriba, y repetido en grafico.json: de ahí salen el formulario de la
// interfaz, los campos del cue del guion y el esquema de la herramienta MCP.
import QtQuick
import QtQuick.Effects
import QtQuick.Shapes

Item {
    id: raiz
    width: 900
    height: 220

    // --- Parámetros -----------------------------------------------------------
    property string nombre: "Nombre Apellido"
    property string cargo: "Cargo o procedencia"
    property color acento: "#ff6a2b"
    property int duracionEntrada: 450
    /// Cuándo empieza a irse, en ms desde el principio. 0 = se queda.
    property int salidaEn: 0

    // El contenido se dibuja a través del efecto, así que este item no se
    // mueve nunca: quien se desliza es el MultiEffect. Mover el ORIGEN de un
    // efecto no mueve nada, porque el efecto lo muestrea en su propio
    // rectángulo — cuesta un rato darse cuenta.
    Item {
        id: contenido
        width: raiz.width
        height: raiz.height
        visible: false

        // La barra de acento, en vector (Shape): escala sin pixelarse y se le
        // puede animar la forma.
        Shape {
            id: barra
            x: 0
            anchors { bottom: parent.bottom; bottomMargin: 30 }
            width: 12
            height: 120
            ShapePath {
                fillColor: raiz.acento
                strokeWidth: 0
                startX: 0; startY: 0
                PathLine { x: 12; y: 0 }
                PathLine { x: 12; y: 120 }
                PathLine { x: 0; y: 120 }
                PathLine { x: 0; y: 0 }
            }
        }

        Rectangle {
            id: fondo
            anchors { left: barra.right; bottom: parent.bottom; bottomMargin: 30 }
            width: Math.max(txtNombre.implicitWidth, txtCargo.implicitWidth) + 56
            height: 120
            color: "#12151a"
            opacity: 0.93
        }

        Text {
            id: txtNombre
            anchors { left: fondo.left; leftMargin: 28; top: fondo.top; topMargin: 20 }
            text: raiz.nombre
            color: "white"
            font { family: "Noto Sans"; pixelSize: 44; weight: Font.DemiBold }
            // Sobre transparencia el suavizado subpíxel deja flecos de color.
            renderType: Text.QtRendering
            opacity: 0
        }

        Text {
            id: txtCargo
            anchors { left: fondo.left; leftMargin: 28; top: txtNombre.bottom; topMargin: 2 }
            text: raiz.cargo
            color: "#b9c0c6"
            font { family: "Noto Sans"; pixelSize: 24 }
            renderType: Text.QtRendering
            opacity: 0
        }
    }

    // Esto es lo que se ve: el contenido pasado por el efecto. Se desliza y se
    // desvanece ACÁ.
    MultiEffect {
        id: efecto
        source: contenido
        width: raiz.width
        height: raiz.height
        blurEnabled: blur > 0
        blurMax: 48
    }

    // --- La animación -----------------------------------------------------------
    // Arranca sola al crearse: el motor dibuja adelantando el reloj, así que
    // el instante 0 es el principio de esto.
    SequentialAnimation {
        running: true

        ParallelAnimation {
            // El conjunto entra desde fuera de cuadro, hacia la derecha.
            NumberAnimation {
                target: efecto; property: "x"
                from: -raiz.width * 0.5; to: 0
                duration: raiz.duracionEntrada
                easing.type: Easing.OutCubic
            }
            NumberAnimation {
                target: efecto; property: "opacity"
                from: 0; to: 1
                duration: Math.round(raiz.duracionEntrada * 0.6)
            }
            SequentialAnimation {
                PauseAnimation { duration: Math.round(raiz.duracionEntrada * 0.45) }
                NumberAnimation {
                    target: txtNombre; property: "opacity"
                    to: 1; duration: 220; easing.type: Easing.OutQuad
                }
            }
            SequentialAnimation {
                PauseAnimation { duration: Math.round(raiz.duracionEntrada * 0.7) }
                NumberAnimation {
                    target: txtCargo; property: "opacity"
                    to: 1; duration: 220; easing.type: Easing.OutQuad
                }
            }
        }
    }

    // La salida es su propia animación: corre solo si se pidió.
    SequentialAnimation {
        running: raiz.salidaEn > 0
        PauseAnimation { duration: Math.max(0, raiz.salidaEn) }
        ParallelAnimation {
            NumberAnimation { target: efecto; property: "blur"; to: 1.0; duration: 320 }
            NumberAnimation {
                target: efecto; property: "opacity"
                to: 0; duration: 320; easing.type: Easing.InQuad
            }
        }
    }
}
