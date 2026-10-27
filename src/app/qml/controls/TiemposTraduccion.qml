// Los tiempos de una traducción (agent/Traductor): cuánto espera el fin de la
// oración y lo más que junta sin punto. Los usan la tarjeta «Traducir» y el
// panel de Traducción, iguales que los cortes de la transcripción (FilaTiempo).
import QtQuick
import QtQuick.Layouts
import BA

ColumnLayout {
    id: tt
    property var t: ({})
    signal cambiar(var c)
    readonly property var cortes: Studio.voz ? Studio.voz.cortes : ({ maximo: 6 })
    spacing: 4

    FilaTiempo {
        Layout.fillWidth: true
        titulo: qsTr("Espera el punto")
        ayuda: qsTr("Si una frase llega sin punto, cuánto espera la siguiente para juntarlas antes de traducir. Más = oraciones más enteras; menos = sale antes.")
        desde: 0.2; hasta: 3; paso: 0.1; decimales: 1
        valor: (tt.t.espera_ms || 600) / 1000
        onElegido: (v) => tt.cambiar({ espera_ms: Math.round(v * 1000) })
    }
    FilaTiempo {
        Layout.fillWidth: true
        titulo: qsTr("Máximo sin punto")
        ayuda: qsTr("Si el orador encadena frases sin terminar la oración, a este tiempo se traduce lo que hay (hasta la última coma). Menos = sale antes pero más cortado.")
        desde: 2; hasta: 20; paso: 0.5; decimales: 1
        valor: (tt.t.maximo_ms || 8000) / 1000
        onElegido: (v) => tt.cambiar({ maximo_ms: Math.round(v * 1000) })
    }
    Text {
        Layout.fillWidth: true; wrapMode: Text.WordWrap
        color: Theme.textFaint; font.pixelSize: 11
        text: qsTr("La transcripción manda frases de hasta %1 s (su «Largo máximo de frase», en la tarjeta Voz a texto).").arg(Number(tt.cortes.maximo).toFixed(1).replace(".", ",")) +
              ((tt.t.maximo_ms || 8000) / 1000 < tt.cortes.maximo ? " " + qsTr("Este máximo es menor: casi siempre va a traducir cada frase sola.") : "")
    }
}
