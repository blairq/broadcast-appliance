// Configuración → Voz del sistema → «Voz del atendedor»: la voz con la que el
// asistente le habla a quien atiende en el lobby (la atención hablada, Sala →
// «Los atiende el asistente» → «Hablando»). Es otra que la del operador: esa
// suena en sus auriculares; esta le llega SOLO a la persona atendida.
//
// PROBAR NO SUENA: genera la frase, muestra cuánto dura y su forma de onda, y
// deja un .wav para escucharlo con auriculares. Por los parlantes del estudio
// no sale nunca (puede haber gente, o estar al aire). Fuente: Studio.atencion;
// por MCP, atencion_configurar {voz} y atencion_voz_probar.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import BA

ScrollView {
    id: pva
    clip: true
    contentWidth: availableWidth
    readonly property var at: Studio.atencion

    ColumnLayout {
        width: pva.availableWidth
        spacing: 14

        Text {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            visible: !pva.at
            color: Theme.amber; font.pixelSize: 13
            text: "Esta compilación no trae la atención hablada (falta el motor de voz)."
        }

        Text { visible: !!pva.at; text: "Voz del atendedor"; color: Theme.text; font { pixelSize: 15; weight: Font.DemiBold } }
        Text {
            visible: !!pva.at
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            color: Theme.textFaint; font.pixelSize: 11
            text: "Con la que el asistente le contesta a quien atiende hablando en el lobby. Le llega solo a esa " +
                  "persona: nunca al aire, ni a los demás que esperan, ni a tus auriculares. Las voces se bajan " +
                  "en Modelos locales (tipo «texto a voz»)."
        }
        Repeater {
            model: pva.at ? pva.at.voces : []
            delegate: Rectangle {
                required property var modelData
                Layout.fillWidth: true
                implicitHeight: filaVoz.implicitHeight + 16
                radius: Theme.radiusSmall
                color: modelData.elegida ? Qt.rgba(1, 0.42, 0.17, 0.12) : Theme.field
                border.color: modelData.elegida ? Theme.accent : "transparent"
                RowLayout {
                    id: filaVoz
                    anchors { fill: parent; margins: 8 }
                    spacing: 10
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 1
                        Text { text: modelData.nombre; color: Theme.text; font.pixelSize: 13 }
                        Text {
                            text: "calidad " + modelData.calidad + " · " + Math.round(modelData.tamano_gb * 1024) + " MB"
                            color: Theme.textDim; font.pixelSize: 11
                        }
                    }
                    GlassButton {
                        compact: true
                        visible: !modelData.instalado
                        enabled: Studio.modelos.enCola.indexOf(modelData.repo) < 0
                        text: enabled ? "Bajar" : "En la cola"; icon: "⇩"
                        onClicked: Studio.notify(modelData.repo + ": " + Studio.modelos.descargar(modelData.repo), "info")
                    }
                    GlassButton {
                        compact: true
                        visible: modelData.instalado
                        text: modelData.elegida ? "Elegida" : "Elegir"
                        active: modelData.elegida
                        onClicked: pva.at.elegirVoz(modelData.repo)
                    }
                }
            }
        }
        RowLayout {
            Layout.fillWidth: true
            visible: !!pva.at && pva.at.vozLista
            spacing: 10
            Repeater {
                model: pva.at && pva.at.hablantes > 1 ? pva.at.hablantes : 0
                delegate: GlassButton {
                    required property int index
                    compact: true
                    text: "Voz " + (index + 1)
                    active: pva.at.hablante === index
                    onClicked: pva.at.hablante = index
                }
            }
            Text { text: "más lento"; color: Theme.textDim; font.pixelSize: 11 }
            Slider {
                Layout.fillWidth: true
                from: 0.7; to: 1.5; stepSize: 0.05
                value: pva.at ? pva.at.velocidad : 1
                onMoved: pva.at.velocidad = value
            }
            Text { text: "más rápido"; color: Theme.textDim; font.pixelSize: 11 }
        }

        Rectangle { visible: !!pva.at; Layout.fillWidth: true; height: 1; color: Theme.glassBorder }

        // --- Probar, sin que suene -------------------------------------------------------
        Text { visible: !!pva.at; text: "Probar (no suena)"; color: Theme.text; font { pixelSize: 15; weight: Font.DemiBold } }
        RowLayout {
            Layout.fillWidth: true
            visible: !!pva.at
            TextField {
                id: frase
                Layout.fillWidth: true
                placeholderText: "escribí una frase (o dejalo vacío para una de ejemplo)"
                onAccepted: pva.at.probarVoz(text)
            }
            GlassButton {
                compact: true; text: "Generar"; active: true
                enabled: pva.at && pva.at.vozLista && !pva.at.prueba.probando
                onClicked: pva.at.probarVoz(frase.text)
            }
        }
        Text {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            visible: !!pva.at && (pva.at.prueba.probando || !!pva.at.prueba.error || pva.at.prueba.segundos !== undefined)
            color: pva.at && pva.at.prueba.error ? Theme.amber : Theme.textDim
            font.pixelSize: 12
            text: !pva.at ? ""
                  : pva.at.prueba.probando ? "Generando…"
                  : pva.at.prueba.error ? pva.at.prueba.error
                  : "Dura " + Number(pva.at.prueba.segundos || 0).toFixed(1).replace(".", ",") + " s." +
                    (pva.at.prueba.archivo ? " Quedó en " + pva.at.prueba.archivo + " para escucharlo con auriculares." : "")
        }
        // La forma de onda: el pico de cada tramo, de 0 a 1.
        Row {
            Layout.fillWidth: true
            Layout.preferredHeight: 48
            visible: !!pva.at && !!pva.at.prueba.forma && pva.at.prueba.forma.length > 0
            spacing: 1
            Repeater {
                model: pva.at && pva.at.prueba.forma ? pva.at.prueba.forma : []
                delegate: Rectangle {
                    required property var modelData
                    width: Math.max(1, (pva.availableWidth - 120) / 120)
                    height: Math.max(1, 48 * modelData)
                    anchors.verticalCenter: parent.verticalCenter
                    color: Theme.accent
                    radius: 1
                }
            }
        }
        RowLayout {
            visible: !!pva.at && !!pva.at.prueba.archivo
            GlassButton {
                compact: true; text: "Copiar la ruta del .wav"
                onClicked: { rutaWav.text = pva.at.prueba.archivo; rutaWav.selectAll(); rutaWav.copy(); Studio.notify("Ruta copiada", "ok") }
            }
            TextInput { id: rutaWav; visible: false }
        }
    }
}
