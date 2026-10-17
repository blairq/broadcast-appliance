// Zócalo «Institucional petróleo»: el nombre y el cargo de quien habla, abajo a la izquierda.
// Generado por tools/generar/plantillas.py para la plantilla «Asamblea con integrantes remotos» (no editar acá:
// se regenera; para cambiarlo, el molde tools/generar/graficos-plantilla/zocalo.qml).
import QtQuick
import QtQuick.Effects

Item {
    id: raiz
    width: 900
    height: 220

    // --- Parámetros -----------------------------------------------------------
    property string nombre: "‹Nombre›"
    property string cargo: "‹Cargo›"
    property color acento: "#0e7490"
    property int duracionEntrada: 800
    /// Cuándo empieza a irse, en ms desde que entró. 0 = se queda.
    property int salidaEn: 0

    // --- Estilo de la plantilla -------------------------------------------------
    readonly property string forma: "filete"
    readonly property string entrada: "revela"
    readonly property color panel: "#062a33"
    readonly property color acento2: "#a5f3fc"
    readonly property color texto: "#ffffff"
    readonly property color texto2: "#cffafe"
    readonly property string familia: "Linux Biolinum G"
    readonly property string familiaTitulo: "Linux Libertine G"
    readonly property real alto: 124
    readonly property real ancho: Math.min(width - 40, Math.max(txtNombre.implicitWidth, txtCargo.implicitWidth) + 96)

    Item {
        id: contenido
        width: raiz.width
        height: raiz.height
        visible: false

        // El panel. En «diagonal» va inclinado (el texto no).
        Rectangle {
            id: fondo
            x: raiz.forma === "diagonal" ? 18 : 0
            y: raiz.height - raiz.alto - 30
            width: raiz.ancho
            height: raiz.alto
            radius: raiz.forma === "pildora" ? height / 2 : 0
            color: raiz.panel
            opacity: 0.94
            border.width: raiz.forma === "neon" ? 2 : 0
            border.color: raiz.acento
            antialiasing: true
            transform: Matrix4x4 {
                matrix: raiz.forma === "diagonal" ? Qt.matrix4x4(1, -0.22, 0, raiz.alto * 0.22, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1)
                                                  : Qt.matrix4x4()
            }
        }
        // El acento: barra a la izquierda (barra, neon), punto (pildora), franja inclinada (diagonal).
        Rectangle {
            visible: raiz.forma === "barra" || raiz.forma === "diagonal"
            x: raiz.forma === "diagonal" ? fondo.x - 4 : 0
            y: fondo.y
            width: raiz.forma === "diagonal" ? 14 : 10
            height: raiz.alto
            color: raiz.acento
            antialiasing: true
            transform: Matrix4x4 {
                matrix: raiz.forma === "diagonal" ? Qt.matrix4x4(1, -0.22, 0, raiz.alto * 0.22, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1)
                                                  : Qt.matrix4x4()
            }
        }
        Rectangle {
            visible: raiz.forma === "pildora"
            x: fondo.x + 26
            y: fondo.y + fondo.height / 2 - 11
            width: 22; height: 22; radius: 11
            color: raiz.acento
        }
        Text {
            id: txtNombre
            x: fondo.x + (raiz.forma === "pildora" ? 64 : raiz.forma === "diagonal" ? 52 : 34)
            y: fondo.y + 18
            text: raiz.nombre
            color: raiz.texto
            font { family: raiz.familiaTitulo; pixelSize: 44; weight: Font.Normal; italic: raiz.forma === "diagonal" }
            renderType: Text.QtRendering
            opacity: 0
        }
        // «filete»: una línea fina del segundo color entre el nombre y el cargo.
        Rectangle {
            id: filete
            visible: raiz.forma === "filete"
            x: txtNombre.x
            y: txtNombre.y + txtNombre.height + 2
            width: 0
            height: 2
            color: raiz.acento2
        }
        Text {
            id: txtCargo
            x: txtNombre.x
            y: txtNombre.y + txtNombre.height + (raiz.forma === "filete" ? 8 : 0)
            text: raiz.cargo
            color: raiz.texto2
            font { family: raiz.familia; pixelSize: 24; letterSpacing: raiz.forma === "filete" ? 1.5 : 0;
                   capitalization: raiz.forma === "filete" ? Font.AllUppercase : Font.MixedCase }
            renderType: Text.QtRendering
            opacity: 0
        }
    }

    // Lo que se ve: el contenido por el efecto (sombra; en «neon», un halo del acento).
    Item {
        id: marco
        width: raiz.width
        height: raiz.height
        clip: raiz.entrada === "revela"
        property real revelado: 1
        MultiEffect {
            id: efecto
            source: contenido
            width: raiz.width
            height: raiz.height
            x: raiz.entrada === "revela" ? -raiz.width * (1 - marco.revelado) : 0
            shadowEnabled: true
            shadowColor: raiz.forma === "neon" ? raiz.acento : "#000000"
            shadowBlur: raiz.forma === "neon" ? 0.9 : 0.5
            shadowOpacity: raiz.forma === "neon" ? 0.9 : 0.45
            shadowVerticalOffset: raiz.forma === "neon" ? 0 : 4
            blurEnabled: blur > 0
            blurMax: 40
            transformOrigin: Item.BottomLeft
        }
    }

    // --- La entrada ---------------------------------------------------------------
    SequentialAnimation {
        running: true
        ParallelAnimation {
            NumberAnimation { target: efecto; property: "opacity"; from: 0; to: 1; duration: Math.round(raiz.duracionEntrada * 0.6) }
            NumberAnimation {
                target: marco; property: "x"; easing.type: Easing.OutCubic; duration: raiz.duracionEntrada
                from: raiz.entrada === "desliza" ? -raiz.width * 0.4 : 0; to: 0
            }
            NumberAnimation {
                target: marco; property: "y"; easing.type: Easing.OutCubic; duration: raiz.duracionEntrada
                from: raiz.entrada === "sube" ? 70 : 0; to: 0
            }
            NumberAnimation {
                target: efecto; property: "scale"; easing.type: Easing.OutBack; duration: raiz.duracionEntrada
                from: raiz.entrada === "crece" ? 0.82 : 1; to: 1
            }
            NumberAnimation {
                target: marco; property: "revelado"; easing.type: Easing.InOutCubic; duration: raiz.duracionEntrada
                from: raiz.entrada === "revela" ? 0 : 1; to: 1
            }
            SequentialAnimation {
                PauseAnimation { duration: Math.round(raiz.duracionEntrada * 0.45) }
                NumberAnimation { target: txtNombre; property: "opacity"; to: 1; duration: 240 }
            }
            SequentialAnimation {
                PauseAnimation { duration: Math.round(raiz.duracionEntrada * 0.6) }
                NumberAnimation { target: filete; property: "width"; to: txtNombre.implicitWidth; duration: 360; easing.type: Easing.OutCubic }
            }
            SequentialAnimation {
                PauseAnimation { duration: Math.round(raiz.duracionEntrada * 0.75) }
                NumberAnimation { target: txtCargo; property: "opacity"; to: 1; duration: 240 }
            }
        }
    }

    // --- La salida ------------------------------------------------------------------
    SequentialAnimation {
        running: raiz.salidaEn > 0
        PauseAnimation { duration: Math.max(0, raiz.salidaEn) }
        ParallelAnimation {
            NumberAnimation { target: efecto; property: "blur"; to: 1.0; duration: 400 }
            NumberAnimation { target: efecto; property: "opacity"; to: 0; duration: 400; easing.type: Easing.InQuad }
        }
    }
}
