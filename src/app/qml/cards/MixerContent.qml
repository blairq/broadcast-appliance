// Tarjeta "Mezclador": nivel, volumen y silencio de cada fuente con audio.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import BA
import "../controls"

// CON EL TECLADO. Con la tarjeta enfocada (un clic en cualquier parte de ella):
//   ← →   elegir canal
//   ↑ ↓   subir o bajar 1 dB   ·   con Shift, 3 dB
//   M o Espacio   silenciar
//   Inicio / Fin  ir al primero o al ultimo
//
// POR QUE IMPORTA ACA MAS QUE EN NINGUNA OTRA. «Hay acople» se arregla bajando
// un fader YA, y buscar un control de 70 px con el mouse mientras suena el
// pitido es justo lo que no se puede hacer. De a 1 dB porque un salto grande
// se escucha como un escalon al aire; Shift para cuando hay que ir en serio.
Item {
    id: tarjeta
    focus: true

    /// Que canal manda el teclado. -1 = ninguno todavia.
    property int elegido: -1

    function cuantos() { return Studio.mixer ? Studio.mixer.rowCount() : 0 }

    function mover(paso) {
        const n = tarjeta.cuantos()
        if (n === 0) return
        tarjeta.elegido = tarjeta.elegido < 0 ? 0 : Math.max(0, Math.min(n - 1, tarjeta.elegido + paso))
    }
    function ajustar(db) {
        const c = Studio.mixer.canal(tarjeta.elegido)
        if (!c || c.nombre === undefined) return
        // Los mismos topes que el fader: de -60 a +12 dB.
        Studio.mixer.setVolumeDb(c.nombre, Math.max(-60, Math.min(12, c.volumenDb + db)))
    }
    function silenciar() {
        const c = Studio.mixer.canal(tarjeta.elegido)
        if (!c || c.nombre === undefined) return
        Studio.mixer.setMuted(c.nombre, !c.mudo)
    }

    Keys.onPressed: (e) => {
        const grande = (e.modifiers & Qt.ShiftModifier) !== 0
        switch (e.key) {
            case Qt.Key_Left:  tarjeta.mover(-1); e.accepted = true; break
            case Qt.Key_Right: tarjeta.mover(1);  e.accepted = true; break
            case Qt.Key_Up:    tarjeta.ajustar(grande ? 3 : 1);  e.accepted = true; break
            case Qt.Key_Down:  tarjeta.ajustar(grande ? -3 : -1); e.accepted = true; break
            case Qt.Key_Home:  tarjeta.elegido = 0; e.accepted = true; break
            case Qt.Key_End:   tarjeta.elegido = tarjeta.cuantos() - 1; e.accepted = true; break
            case Qt.Key_M:
            case Qt.Key_Space: tarjeta.silenciar(); e.accepted = true; break
        }
    }

    RowLayout {
        anchors.fill: parent
        spacing: 14

        // Programa (mezcla final)
        ColumnLayout {
            Layout.fillHeight: true
            spacing: 6
            RowLayout {
                Layout.fillHeight: true
                spacing: 4
                LevelMeter { vertical: true; db: Studio.programPeakL; Layout.fillHeight: true }
                LevelMeter { vertical: true; db: Studio.programPeakR; Layout.fillHeight: true }
            }
            Text { text: "Programa"; color: Theme.accentSoft; font { pixelSize: 11; weight: Font.DemiBold } }
        }
        Rectangle { width: 1; Layout.fillHeight: true; color: Qt.rgba(1, 1, 1, 0.1) }

        ListView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            orientation: ListView.Horizontal
            spacing: 14
            clip: true
            model: Studio.mixer
            delegate: ColumnLayout {
                required property int index
                required property string name
                required property real peakDb
                required property real volumeDb
                required property bool muted
                height: ListView.view.height
                width: 70
                spacing: 6
                RowLayout {
                    Layout.fillHeight: true
                    spacing: 6
                    LevelMeter { vertical: true; db: muted ? -100 : peakDb; Layout.fillHeight: true }
                    Slider {
                        orientation: Qt.Vertical
                        Layout.fillHeight: true
                        from: -60; to: 12
                        value: volumeDb
                        onMoved: Studio.mixer.setVolumeDb(name, value)
                        ToolTip.visible: pressed
                        ToolTip.text: value.toFixed(1) + " dB"
                    }
                }
                Rectangle {
                    Layout.fillWidth: true
                    height: 22; radius: 8
                    color: muted ? Qt.rgba(1, 0.23, 0.19, 0.7) : Qt.rgba(1, 1, 1, 0.08)
                    Text { anchors.centerIn: parent; text: muted ? "MUDO" : "M"; color: Theme.text; font { pixelSize: 10; weight: Font.Bold } }
                    MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: Studio.mixer.setMuted(name, !muted) }
                }
                // El canal que manda el teclado se marca: sin esto, subir el
                // volumen con las flechas es una apuesta.
                Rectangle {
                    Layout.fillWidth: true
                    height: 2
                    radius: 1
                    color: Theme.accent
                    visible: tarjeta.activeFocus && index === tarjeta.elegido
                }
                Text { Layout.fillWidth: true; text: name; color: Theme.textDim; elide: Text.ElideRight
                       horizontalAlignment: Text.AlignHCenter; font.pixelSize: 10
                       font.bold: tarjeta.activeFocus && index === tarjeta.elegido }
            }
        }
    }
}
