// Marcador «$estilo»: local, visitante, los goles y el tiempo, arriba a la izquierda.
// Generado por tools/generar/plantillas.py para «$plantilla» (molde:
// tools/generar/graficos-plantilla/marcador.qml). Se actualiza sin volver a entrar
// (grafico_mostrar con los valores nuevos lo vuelve a dibujar; el gol lo marca la cue).
import QtQuick
import QtQuick.Effects

Item {
    id: raiz
    width: 760
    height: 110

    // --- Parámetros -----------------------------------------------------------
    property string local: "‹LOC›"
    property string visitante: "‹VIS›"
    property int golesLocal: 0
    property int golesVisitante: 0
    property string tiempo: "1T 00:00"
    property color acento: "$acento"
    property int salidaEn: 0

    readonly property color panel: "$panel"
    readonly property color acento2: "$acento2"
    readonly property color texto: "$texto"
    readonly property color texto2: "$texto2"
    readonly property string familiaTitulo: "$familiaTitulo"

    Item {
        id: contenido
        width: raiz.width
        height: raiz.height
        visible: false
        Row {
            y: 15
            height: 80
            spacing: 0
            Rectangle {
                width: 270; height: 80; color: raiz.panel
                Text { anchors.centerIn: parent; text: raiz.local; color: raiz.texto
                       font { family: raiz.familiaTitulo; pixelSize: 40; weight: Font.Black; capitalization: Font.AllUppercase } }
            }
            Rectangle {
                width: 170; height: 80; color: raiz.acento
                Text { anchors.centerIn: parent; text: raiz.golesLocal + " - " + raiz.golesVisitante; color: "white"
                       font { family: raiz.familiaTitulo; pixelSize: 48; weight: Font.Black } }
            }
            Rectangle {
                width: 270; height: 80; color: raiz.panel
                Text { anchors.centerIn: parent; text: raiz.visitante; color: raiz.texto
                       font { family: raiz.familiaTitulo; pixelSize: 40; weight: Font.Black; capitalization: Font.AllUppercase } }
            }
        }
        Rectangle {
            x: 270; y: 95; width: 170; height: 0
            id: reloj
            color: raiz.acento2
            Text { anchors.centerIn: parent; text: raiz.tiempo; color: "#111111"; visible: reloj.height > 10
                   font { family: raiz.familiaTitulo; pixelSize: 18; weight: Font.Bold } }
        }
    }

    MultiEffect {
        id: efecto
        source: contenido
        width: raiz.width
        height: raiz.height
        shadowEnabled: true
        shadowOpacity: 0.5
        shadowBlur: 0.5
        shadowVerticalOffset: 3
    }

    SequentialAnimation {
        running: true
        ParallelAnimation {
            NumberAnimation { target: efecto; property: "opacity"; from: 0; to: 1; duration: 300 }
            NumberAnimation { target: efecto; property: "x"; from: -200; to: 0; duration: 420; easing.type: Easing.OutCubic }
        }
        NumberAnimation { target: reloj; property: "height"; to: 26; duration: 220; easing.type: Easing.OutCubic }
    }
    SequentialAnimation {
        running: raiz.salidaEn > 0
        PauseAnimation { duration: Math.max(0, raiz.salidaEn) }
        NumberAnimation { target: efecto; property: "opacity"; to: 0; duration: 400 }
    }
}
