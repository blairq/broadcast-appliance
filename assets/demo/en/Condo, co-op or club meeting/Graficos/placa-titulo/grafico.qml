// Placa de título «Vecinal»: la apertura de un bloque, una banda a media altura con el
// título y una bajada. Generado por tools/generar/plantillas.py para «Condo, co-op or club meeting» (el
// molde: tools/generar/graficos-plantilla/placa-titulo.qml). Lienzo de 1920×360 y no de
// pantalla completa: cada cuadro de 1080p son 8 MB mientras está en escena.
import QtQuick
import QtQuick.Effects

Item {
    id: raiz
    width: 1920
    height: 360

    // --- Parámetros -----------------------------------------------------------
    property string titulo: "‹Title›"
    property string bajada: "‹Subtitle›"
    property color acento: "#16a34a"
    /// Cuándo empieza a irse, en ms desde que entró. 0 = se queda.
    property int salidaEn: 0

    // --- Estilo de la plantilla -------------------------------------------------
    readonly property string forma: "pildora"
    readonly property string entrada: "sube"
    readonly property color panel: "#102a1c"
    readonly property color acento2: "#bbf7d0"
    readonly property color texto: "#ffffff"
    readonly property color texto2: "#d1fae5"
    readonly property string familia: "Rubik"
    readonly property string familiaTitulo: "Rubik"
    readonly property int duracionEntrada: 600
    readonly property bool centrado: forma === "filete" || forma === "neon"
    // La banda: de lado a lado; en «pildora», una tarjeta con margen.
    readonly property real margen: forma === "pildora" ? 150 : 0

    Item {
        id: contenido
        width: raiz.width
        height: raiz.height
        visible: false

        Rectangle {
            id: banda
            x: raiz.margen
            y: 40
            width: raiz.width - 2 * raiz.margen
            height: raiz.height - 80
            radius: raiz.forma === "pildora" ? 40 : 4
            color: raiz.panel
            opacity: 0.94
            antialiasing: true
            transform: Matrix4x4 {
                matrix: raiz.forma === "diagonal" ? Qt.matrix4x4(1, -0.12, 0, banda.height * 0.06, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1)
                                                  : Qt.matrix4x4()
            }
        }
        // Acentos: la línea de arriba (barra), franja inclinada (diagonal), filetes arriba y
        // abajo (filete), líneas que brillan (neon), un punto (pildora).
        Rectangle {
            id: lineaArriba
            visible: raiz.forma !== "pildora"
            x: raiz.centrado ? (raiz.width - width) / 2 : (raiz.forma === "diagonal" ? 120 : 0)
            y: banda.y + (raiz.forma === "filete" ? 26 : 0)
            width: 0
            height: raiz.forma === "filete" ? 2 : raiz.forma === "neon" ? 3 : 8
            color: raiz.forma === "filete" ? raiz.acento2 : raiz.acento
        }
        Rectangle {
            id: lineaAbajo
            visible: raiz.forma === "filete" || raiz.forma === "neon"
            x: (raiz.width - width) / 2
            y: banda.y + banda.height - height - (raiz.forma === "filete" ? 26 : 0)
            width: lineaArriba.width
            height: lineaArriba.height
            color: lineaArriba.color
        }
        Rectangle {
            visible: raiz.forma === "pildora"
            x: banda.x + 70; y: banda.y + 70
            width: 26; height: 26; radius: 13
            color: raiz.acento
        }
        Text {
            id: txtTitulo
            x: raiz.centrado ? (raiz.width - width) / 2 : banda.x + (raiz.forma === "pildora" ? 120 : 150)
            y: banda.y + (raiz.forma === "filete" ? 52 : 46)
            width: Math.min(implicitWidth, raiz.width - 300)
            elide: Text.ElideRight
            text: raiz.titulo
            color: raiz.texto
            font { family: raiz.familiaTitulo; pixelSize: 88; weight: Font.DemiBold; italic: raiz.forma === "diagonal" }
            renderType: Text.QtRendering
            opacity: 0
        }
        Text {
            id: txtBajada
            x: raiz.centrado ? (raiz.width - width) / 2 : txtTitulo.x + 4
            y: txtTitulo.y + txtTitulo.height + 10
            width: Math.min(implicitWidth, raiz.width - 300)
            elide: Text.ElideRight
            text: raiz.bajada
            color: raiz.texto2
            font { family: raiz.familia; pixelSize: 36; letterSpacing: raiz.forma === "filete" ? 3 : 0;
                   capitalization: raiz.forma === "filete" ? Font.AllUppercase : Font.MixedCase }
            renderType: Text.QtRendering
            opacity: 0
        }
    }

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
            shadowBlur: raiz.forma === "neon" ? 1.0 : 0.6
            shadowOpacity: raiz.forma === "neon" ? 0.9 : 0.4
            shadowVerticalOffset: raiz.forma === "neon" ? 0 : 6
            blurEnabled: blur > 0
            blurMax: 48
        }
    }

    // --- La entrada ---------------------------------------------------------------
    SequentialAnimation {
        running: true
        ParallelAnimation {
            NumberAnimation { target: efecto; property: "opacity"; from: 0; to: 1; duration: Math.round(raiz.duracionEntrada * 0.6) }
            NumberAnimation {
                target: marco; property: "x"; easing.type: Easing.OutCubic; duration: raiz.duracionEntrada
                from: raiz.entrada === "desliza" ? -raiz.width * 0.25 : 0; to: 0
            }
            NumberAnimation {
                target: marco; property: "y"; easing.type: Easing.OutCubic; duration: raiz.duracionEntrada
                from: raiz.entrada === "sube" ? 90 : 0; to: 0
            }
            NumberAnimation {
                target: efecto; property: "scale"; easing.type: Easing.OutBack; duration: raiz.duracionEntrada
                from: raiz.entrada === "crece" ? 0.88 : 1; to: 1
            }
            NumberAnimation {
                target: marco; property: "revelado"; easing.type: Easing.InOutCubic; duration: raiz.duracionEntrada
                from: raiz.entrada === "revela" ? 0 : 1; to: 1
            }
            SequentialAnimation {
                PauseAnimation { duration: Math.round(raiz.duracionEntrada * 0.3) }
                NumberAnimation {
                    target: lineaArriba; property: "width"; duration: 600; easing.type: Easing.OutCubic
                    to: raiz.centrado ? 900 : raiz.width - (raiz.forma === "diagonal" ? 240 : 0)
                }
            }
            SequentialAnimation {
                PauseAnimation { duration: Math.round(raiz.duracionEntrada * 0.5) }
                NumberAnimation { target: txtTitulo; property: "opacity"; to: 1; duration: 320 }
            }
            SequentialAnimation {
                PauseAnimation { duration: Math.round(raiz.duracionEntrada * 0.8) }
                NumberAnimation { target: txtBajada; property: "opacity"; to: 1; duration: 320 }
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
