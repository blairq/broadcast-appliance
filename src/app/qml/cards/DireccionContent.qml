// Tarjeta «Dirección»: lo único que un director mira todo el tiempo.
//
// QUE VA ACA Y QUE NO. Al aire, lo armado, si estamos emitiendo y hace cuánto,
// y —si hay guion corriendo— qué cue va y cuánto le queda. Nada más. El resto
// de la información del sistema ya tiene su tarjeta; esta existe para no tener
// que barrer la pantalla con la vista en el momento en que menos tiempo hay.
//
// LOS TIEMPOS EN GRANDE Y EL DESVIO CON COLOR: en vivo no se leen números
// chicos, se los mira de reojo. Atrasado es rojo, adelantado es ámbar, en hora
// no se marca —lo que está bien no tiene que llamar la atención—.
import QtQuick
import QtQuick.Layouts
import BA
import "../controls"

Item {
    id: tarjeta
    readonly property var ev: Studio.evento
    readonly property var reloj: ev ? ev.reloj : ({})
    readonly property bool corriendo: ev && ev.estado === "corriendo"

    function mmss(s) {
        const n = Math.max(0, Math.round(Math.abs(s)))
        return (s < 0 ? "−" : "") + Math.floor(n / 60) + ":" + String(n % 60).padStart(2, "0")
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 8

        // --- Los dos buses -----------------------------------------------------
        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            Rectangle {
                Layout.fillWidth: true; Layout.preferredWidth: 1
                implicitHeight: 46
                radius: 10
                color: Qt.rgba(1, 0.42, 0.17, 0.85)
                ColumnLayout {
                    anchors { fill: parent; leftMargin: 10; rightMargin: 10; topMargin: 5; bottomMargin: 5 }
                    spacing: 0
                    Text { text: "AL AIRE"; color: Qt.rgba(1, 1, 1, 0.75)
                           font { family: Theme.font; pixelSize: 10; weight: Font.DemiBold; letterSpacing: 1 } }
                    Text { Layout.fillWidth: true; text: Studio.currentScene || "—"; color: Theme.text
                           elide: Text.ElideRight
                           font { family: Theme.font; pixelSize: 14; weight: Font.DemiBold } }
                }
            }
            Rectangle {
                Layout.fillWidth: true; Layout.preferredWidth: 1
                implicitHeight: 46
                radius: 10
                color: Qt.rgba(1, 1, 1, 0.06)
                border.color: Studio.escenaArmada !== "" ? Theme.ok : Qt.rgba(1, 1, 1, 0.1)
                ColumnLayout {
                    anchors { fill: parent; leftMargin: 10; rightMargin: 10; topMargin: 5; bottomMargin: 5 }
                    spacing: 0
                    Text { text: "ARMADA"; color: Theme.textFaint
                           font { family: Theme.font; pixelSize: 10; weight: Font.DemiBold; letterSpacing: 1 } }
                    Text { Layout.fillWidth: true; text: Studio.escenaArmada || "nada"; elide: Text.ElideRight
                           color: Studio.escenaArmada !== "" ? Theme.text : Theme.textFaint
                           font { family: Theme.font; pixelSize: 14; weight: Font.DemiBold } }
                }
            }
            GlassButton {
                text: "TAKE"; active: true
                enabled: Studio.escenaArmada !== ""
                onClicked: Studio.take()
            }
        }

        // --- Aire y grabación ---------------------------------------------------
        RowLayout {
            Layout.fillWidth: true
            spacing: 14
            StatusPill {
                text: Studio.streaming ? "AL AIRE · " + Studio.streamTime : "fuera del aire"
                dot: Studio.streaming ? Theme.live : Theme.textFaint
                pulse: Studio.streaming
            }
            StatusPill {
                visible: Studio.recording
                text: "REC · " + Studio.recordTime
                dot: Theme.live; pulse: true
            }
            Item { Layout.fillWidth: true }
            Text {
                visible: Studio.droppedFrames > 0
                text: Studio.droppedFrames + " perdidos"
                color: Theme.amber; font.pixelSize: 11
            }
        }

        // --- El guion, si está corriendo ----------------------------------------
        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: tarjeta.corriendo
            spacing: 2
            RowLayout {
                Layout.fillWidth: true
                Text {
                    Layout.fillWidth: true
                    text: (tarjeta.ev.actual.texto || "—")
                    color: Theme.text; elide: Text.ElideRight
                    font { family: Theme.font; pixelSize: 15; weight: Font.DemiBold }
                }
                Text {
                    text: tarjeta.mmss(tarjeta.reloj.restanteSeg || 0)
                    color: (tarjeta.reloj.restanteSeg || 0) <= 10 ? Theme.live : Theme.text
                    font { family: "JetBrains Mono"; pixelSize: 22; weight: Font.DemiBold }
                }
            }
            RowLayout {
                Layout.fillWidth: true
                Text {
                    Layout.fillWidth: true
                    text: "sigue: " + (tarjeta.ev.siguiente.texto || "—")
                    color: Theme.textDim; elide: Text.ElideRight; font.pixelSize: 12
                }
                Text {
                    // El desvío es lo que decide si hay que apurar o estirar.
                    readonly property int d: tarjeta.reloj.desvioSeg || 0
                    text: d === 0 ? "en hora" : (d > 0 ? "+" + tarjeta.mmss(d) : tarjeta.mmss(d))
                    color: d > 20 ? Theme.live : (d < -20 ? Theme.amber : Theme.textFaint)
                    font { family: "JetBrains Mono"; pixelSize: 12 }
                }
            }
            Text {
                Layout.fillWidth: true
                text: "cue " + (tarjeta.reloj.cueNro || 0) + " de " + (tarjeta.reloj.cuesTotal || 0)
                color: Theme.textFaint; font.pixelSize: 11
            }
            Item { Layout.fillHeight: true }
        }
        Text {
            Layout.fillWidth: true
            visible: !tarjeta.corriendo
            text: "Sin guion corriendo. Abrí uno en Guion del evento para ver los tiempos acá."
            color: Theme.textFaint; font.pixelSize: 11; wrapMode: Text.WordWrap
        }
    }
}
