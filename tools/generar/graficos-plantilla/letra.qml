// Letra «$estilo»: una estrofa para cantar, centrada abajo. Generado por
// tools/generar/plantillas.py para «$plantilla» (molde: tools/generar/graficos-plantilla/letra.qml).
import QtQuick
import QtQuick.Effects

Item {
    id: raiz
    width: 1700
    height: 300

    // --- Parámetros -----------------------------------------------------------
    property string estrofa: "‹Primer verso›\n‹Segundo verso›"
    property color acento: "$acento"
    property int salidaEn: 0

    readonly property color panel: "$panel"
    readonly property color acento2: "$acento2"
    readonly property color texto: "$texto"
    readonly property string familiaTitulo: "$familiaTitulo"

    Item {
        id: contenido
        width: raiz.width
        height: raiz.height
        visible: false
        Rectangle {
            anchors.fill: parent
            anchors.margins: 20
            radius: $radio
            color: raiz.panel
            opacity: 0.88
        }
        Rectangle { x: (raiz.width - 300) / 2; y: 34; width: 300; height: 2; color: raiz.acento2 }
        Text {
            anchors.centerIn: parent
            width: raiz.width - 160
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
            maximumLineCount: 3
            text: raiz.estrofa
            color: raiz.texto
            lineHeight: 1.15
            font { family: raiz.familiaTitulo; pixelSize: 58; italic: true }
            renderType: Text.QtRendering
        }
        Rectangle { x: (raiz.width - 300) / 2; y: raiz.height - 36; width: 300; height: 2; color: raiz.acento2 }
    }

    MultiEffect {
        id: efecto
        source: contenido
        width: raiz.width
        height: raiz.height
        shadowEnabled: true
        shadowOpacity: 0.5
        shadowBlur: 0.8
        blurEnabled: blur > 0
        blurMax: 32
    }

    // Una entrada lenta, sin sobresaltos: aparece desde un leve desenfoque.
    ParallelAnimation {
        running: true
        NumberAnimation { target: efecto; property: "opacity"; from: 0; to: 1; duration: 900; easing.type: Easing.InOutQuad }
        NumberAnimation { target: efecto; property: "blur"; from: 0.6; to: 0; duration: 900; easing.type: Easing.OutQuad }
    }
    SequentialAnimation {
        running: raiz.salidaEn > 0
        PauseAnimation { duration: Math.max(0, raiz.salidaEn) }
        NumberAnimation { target: efecto; property: "opacity"; to: 0; duration: 700 }
    }
}
