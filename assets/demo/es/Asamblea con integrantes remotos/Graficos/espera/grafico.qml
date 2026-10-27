// «Placa de espera» (Institucional petróleo): una tarjeta centrada con una etiqueta chica, el título y una
// bajada (la placa de espera y la de cierre salen de este molde). Generado por
// tools/generar/plantillas.py para «Asamblea con integrantes remotos» (molde: tools/generar/graficos-plantilla/
// tarjeta.qml). Va sobre el fondo de la escena: el lienzo es solo la tarjeta.
import QtQuick
import QtQuick.Effects

Item {
    id: raiz
    width: 1600
    height: 620

    // --- Parámetros -----------------------------------------------------------
    property string etiqueta: "En instantes"
    property string titulo: "‹Nombre del evento›"
    property string bajada: "Empezamos a las ‹hora›"
    property color acento: "#0e7490"
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
    readonly property int duracionEntrada: 800

    Item {
        id: contenido
        width: raiz.width
        height: raiz.height
        visible: false

        Rectangle {
            id: tarjeta
            anchors.fill: parent
            anchors.margins: 30
            radius: raiz.forma === "pildora" ? 60 : 0
            color: raiz.panel
            opacity: 0.95
            border.width: raiz.forma === "neon" ? 3 : raiz.forma === "filete" ? 1 : 0
            border.color: raiz.forma === "filete" ? raiz.acento2 : raiz.acento
            antialiasing: true
        }
        // Una franja del acento: arriba (barra, pildora), a la izquierda inclinada
        // (diagonal); en filete, un marco interior fino del segundo color.
        Rectangle {
            visible: raiz.forma === "barra" || raiz.forma === "pildora"
            x: tarjeta.x + (raiz.forma === "pildora" ? 60 : 0); y: tarjeta.y
            width: tarjeta.width - (raiz.forma === "pildora" ? 120 : 0); height: 10
            radius: raiz.forma === "pildora" ? 5 : 0
            color: raiz.acento
        }
        Rectangle {
            visible: raiz.forma === "diagonal"
            x: tarjeta.x + 40; y: tarjeta.y
            width: 60; height: tarjeta.height
            color: raiz.acento
            transform: Matrix4x4 { matrix: Qt.matrix4x4(1, -0.25, 0, tarjeta.height * 0.25, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1) }
        }
        Rectangle {
            visible: raiz.forma === "filete"
            anchors.fill: tarjeta; anchors.margins: 18
            color: "transparent"
            border.width: 1; border.color: raiz.acento2
            opacity: 0.6
        }
        Column {
            id: textos
            anchors.centerIn: tarjeta
            width: tarjeta.width - 200
            spacing: 18
            Text {
                id: txtEtiqueta
                width: parent.width
                horizontalAlignment: Text.AlignHCenter
                text: raiz.etiqueta
                visible: text !== ""
                color: raiz.forma === "filete" ? raiz.acento2 : raiz.acento
                font { family: raiz.familia; pixelSize: 30; weight: Font.DemiBold; letterSpacing: 6; capitalization: Font.AllUppercase }
                renderType: Text.QtRendering
                opacity: 0
            }
            Text {
                id: txtTitulo
                width: parent.width
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
                maximumLineCount: 2
                elide: Text.ElideRight
                text: raiz.titulo
                color: raiz.texto
                font { family: raiz.familiaTitulo; pixelSize: 92; weight: Font.Normal; italic: raiz.forma === "diagonal" }
                renderType: Text.QtRendering
                opacity: 0
            }
            Text {
                id: txtBajada
                width: parent.width
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
                maximumLineCount: 2
                text: raiz.bajada
                visible: text !== ""
                color: raiz.texto2
                font { family: raiz.familia; pixelSize: 40 }
                renderType: Text.QtRendering
                opacity: 0
            }
        }
    }

    MultiEffect {
        id: efecto
        source: contenido
        width: raiz.width
        height: raiz.height
        shadowEnabled: true
        shadowColor: raiz.forma === "neon" ? raiz.acento : "#000000"
        shadowBlur: 1.0
        shadowOpacity: raiz.forma === "neon" ? 0.9 : 0.45
        shadowVerticalOffset: raiz.forma === "neon" ? 0 : 10
        blurEnabled: blur > 0
        blurMax: 48
    }

    // --- La entrada ---------------------------------------------------------------
    SequentialAnimation {
        running: true
        ParallelAnimation {
            NumberAnimation { target: efecto; property: "opacity"; from: 0; to: 1; duration: Math.round(raiz.duracionEntrada * 0.7) }
            NumberAnimation {
                target: efecto; property: "x"; easing.type: Easing.OutCubic; duration: raiz.duracionEntrada
                from: raiz.entrada === "desliza" ? -160 : 0; to: 0
            }
            NumberAnimation {
                target: efecto; property: "y"; easing.type: Easing.OutCubic; duration: raiz.duracionEntrada
                from: raiz.entrada === "sube" ? 80 : 0; to: 0
            }
            NumberAnimation {
                target: efecto; property: "scale"; easing.type: Easing.OutBack; duration: raiz.duracionEntrada
                from: raiz.entrada === "crece" ? 0.86 : raiz.entrada === "revela" ? 0.97 : 1; to: 1
            }
            SequentialAnimation {
                PauseAnimation { duration: Math.round(raiz.duracionEntrada * 0.4) }
                NumberAnimation { target: txtEtiqueta; property: "opacity"; to: 1; duration: 300 }
            }
            SequentialAnimation {
                PauseAnimation { duration: Math.round(raiz.duracionEntrada * 0.55) }
                NumberAnimation { target: txtTitulo; property: "opacity"; to: 1; duration: 360 }
            }
            SequentialAnimation {
                PauseAnimation { duration: Math.round(raiz.duracionEntrada * 0.8) }
                NumberAnimation { target: txtBajada; property: "opacity"; to: 1; duration: 360 }
            }
        }
    }

    SequentialAnimation {
        running: raiz.salidaEn > 0
        PauseAnimation { duration: Math.max(0, raiz.salidaEn) }
        ParallelAnimation {
            NumberAnimation { target: efecto; property: "blur"; to: 1.0; duration: 400 }
            NumberAnimation { target: efecto; property: "opacity"; to: 0; duration: 400; easing.type: Easing.InQuad }
        }
    }
}
