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

        // Programa (mezcla final). Ancho FIJO: el botón de balística de abajo
        // ocupa el ancho de la columna (fillWidth) y, sin tope, la columna se
        // estiraba a toda la tarjeta y empujaba los canales afuera por la derecha.
        ColumnLayout {
            Layout.fillHeight: true
            Layout.fillWidth: false
            Layout.preferredWidth: 58
            Layout.maximumWidth: 58
            spacing: 6
            RowLayout {
                Layout.fillHeight: true
                spacing: 4
                LevelMeter { vertical: true; db: Studio.programPeakL; Layout.fillHeight: true }
                LevelMeter { vertical: true; db: Studio.programPeakR; Layout.fillHeight: true }
            }
            Text { text: "Programa"; color: Theme.accentSoft; font { pixelSize: 11; weight: Font.DemiBold } }
            // La balística de todos los medidores: pico → PPM (EBU IIb) → VU.
            Rectangle {
                Layout.fillWidth: true
                height: 16; radius: 5
                color: Qt.rgba(1, 1, 1, 0.06)
                Text { anchors.centerIn: parent; text: Studio.balistica.toUpperCase(); color: Theme.textDim; font { pixelSize: 9; weight: Font.Bold } }
                MouseArea {
                    id: balMa
                    anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                    onClicked: Studio.balistica = Studio.balistica === "pico" ? "ppm" : Studio.balistica === "ppm" ? "vu" : "pico"
                }
                ToolTip.visible: balMa.containsMouse; ToolTip.delay: 500
                ToolTip.text: "Balística de los medidores: PICO (instantáneo), PPM (EBU IIb: cae 24 dB en 2,8 s) o VU (integra 300 ms)"
            }
        }
        // Loudness de lo que sale (EBU R128): M y S en barras, el integrado en grande,
        // true peak (rojo arriba del objetivo), LRA y el objetivo.
        ColumnLayout {
            id: ld
            property var l: Studio.loudnessUi()
            Timer { interval: 300; running: ld.visible; repeat: true; onTriggered: ld.l = Studio.loudnessUi() }
            readonly property var ob: l.objetivo || ({ lufs: -23, tp: -1, perfil: "r128" })
            Layout.fillHeight: true
            Layout.fillWidth: false
            Layout.preferredWidth: 96
            Layout.maximumWidth: 110
            clip: true
            spacing: 2
            Text { text: "LOUDNESS"; color: Theme.textFaint; font { pixelSize: 9; weight: Font.Bold; letterSpacing: 1 } }
            RowLayout {
                Layout.fillHeight: true
                spacing: 3
                LevelMeter { vertical: true; db: (ld.l.m || -120) + 18; Layout.fillHeight: true }   // escala: -18 LUFS ≈ 0 dBFS del medidor
                LevelMeter { vertical: true; db: (ld.l.s || -120) + 18; Layout.fillHeight: true }
                ColumnLayout {
                    spacing: 0
                    Text { text: ld.l.i > -70 ? Number(ld.l.i).toFixed(1) : "—"; font { pixelSize: 20; weight: Font.DemiBold }
                           color: ld.l.i > -70 && Math.abs(ld.l.i - ld.ob.lufs) > 2 ? Theme.amber : Theme.text }
                    Text { text: "LUFS I"; color: Theme.textFaint; font.pixelSize: 9 }
                    Text { text: "TP " + (ld.l.tp > -100 ? Number(ld.l.tp).toFixed(1) : "—"); font.pixelSize: 11
                           color: ld.l.tp > ld.ob.tp ? Theme.live : Theme.textDim }
                    Text { text: "LRA " + Number(ld.l.lra || 0).toFixed(1); color: Theme.textDim; font.pixelSize: 11 }
                    Text { text: "obj. " + ld.ob.lufs + " / " + ld.ob.tp; color: Theme.textFaint; font.pixelSize: 9 }
                    Text { visible: (ld.l.gr_db || 0) < -0.1; text: "LIM " + Number(ld.l.gr_db).toFixed(1); color: Theme.amber; font.pixelSize: 10 }
                }
            }
            GlassButton { compact: true; text: "Reset"; onClicked: Studio.resetLoudnessUi()
                          ToolTip.visible: hovered; ToolTip.delay: 400; ToolTip.text: "Vuelve a cero el integrado y el LRA (arranca solo al transmitir o grabar)" }
        }
        Rectangle { width: 1; Layout.fillHeight: true; color: Qt.rgba(1, 1, 1, 0.1) }

        ListView {
            id: canales
            Layout.fillWidth: true
            Layout.fillHeight: true
            // Al menos un canal entero a la vista; los demás, desplazando.
            Layout.minimumWidth: 84
            orientation: ListView.Horizontal
            spacing: 14
            clip: true
            model: Studio.mixer
            // Si no entran todos, que se vea que hay más (y se puedan arrastrar).
            ScrollBar.horizontal: ScrollBar { policy: canales.contentWidth > canales.width ? ScrollBar.AlwaysOn : ScrollBar.AlwaysOff }
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
                // PFL: escucharla por el monitor sin tocar el aire (exclusivo, amarillo).
                Rectangle {
                    readonly property bool on: Studio.pfl === name
                    Layout.fillWidth: true
                    height: 18; radius: 6
                    color: on ? Qt.rgba(1, 0.8, 0.1, 0.85) : Qt.rgba(1, 1, 1, 0.05)
                    Text { anchors.centerIn: parent; text: "PFL"; color: parent.on ? "#1a1a1a" : Theme.textFaint; font { pixelSize: 9; weight: Font.Bold } }
                    MouseArea {
                        id: pflMa
                        anchors.fill: parent; cursorShape: Qt.PointingHandCursor; hoverEnabled: true
                        onClicked: { const e = Studio.pflUi(name); if (e !== "") Studio.notify(e, "error") }
                    }
                    ToolTip.visible: pflMa.containsMouse; ToolTip.delay: 500
                    ToolTip.text: "Escucharla por el monitor sin tocar lo que sale al aire. Si está MUDA no suena ni acá: bajale el volumen en vez de silenciarla."
                }
                // AFV: el audio sigue al video (sube y baja con su escena).
                Rectangle {
                    property bool on: Studio.afv(name)
                    Layout.fillWidth: true
                    height: 18; radius: 6
                    color: on ? Qt.rgba(0.18, 0.44, 0.87, 0.7) : Qt.rgba(1, 1, 1, 0.05)
                    Text { anchors.centerIn: parent; text: "AFV"; color: parent.on ? Theme.text : Theme.textFaint; font { pixelSize: 9; weight: Font.Bold } }
                    MouseArea {
                        anchors.fill: parent; cursorShape: Qt.PointingHandCursor
                        onClicked: { Studio.setAfv(name, !parent.on); parent.on = Studio.afv(name) }
                    }
                    ToolTip.visible: afvMa.containsMouse; ToolTip.delay: 500
                    ToolTip.text: "Audio que sigue al video: sube cuando entra al aire una escena con esta fuente y baja cuando sale"
                    MouseArea { id: afvMa; anchors.fill: parent; hoverEnabled: true; acceptedButtons: Qt.NoButton }
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
                       font.bold: tarjeta.activeFocus && index === tarjeta.elegido
                       // Un clic: el retardo de este audio respecto de su video (sincro labios).
                       MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor
                                   onClicked: { retardoPop.fuente = name; retardoPop.ms = Studio.retardoUi(name); retardoPop.open() } } }
            }
        }
    }

    // Retardo de un canal (sincro labios), como audio_sincro.
    Popup {
        id: retardoPop
        property string fuente: ""
        property int ms: 0
        property bool midiendo: false
        property string resultado: ""
        parent: Overlay.overlay
        anchors.centerIn: parent
        modal: true
        padding: 16
        width: 380
        background: Rectangle { color: Theme.panel; radius: Theme.radiusSmall; border.color: Theme.glassBorder }
        ColumnLayout {
            anchors.fill: parent
            spacing: 8
            Text { text: "Retardo de «" + retardoPop.fuente + "»"; color: Theme.text; font.pixelSize: 15; font.weight: Font.DemiBold }
            Text { Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.textDim; font.pixelSize: 11
                   text: "Positivo ATRASA el audio (lo normal con una cámara IP, que llega tarde). Mirá a alguien hablando y corregí hasta que coincidan los labios." }
            RowLayout {
                SpinBox { id: retMs; from: -5000; to: 5000; stepSize: 10; editable: true; value: retardoPop.ms; Layout.preferredWidth: 160 }
                Text { text: "ms"; color: Theme.textDim }
                Item { Layout.fillWidth: true }
                GlassButton { compact: true; text: "−40"; onClicked: retMs.value -= 40 }
                GlassButton { compact: true; text: "+40"; onClicked: retMs.value += 40 }
            }
            // Medir con aplauso: mira lo que sale y propone el valor.
            RowLayout {
                Layout.fillWidth: true
                GlassButton {
                    compact: true; text: retardoPop.midiendo ? "Midiendo… aplaudí" : "Medir con aplauso"
                    enabled: !retardoPop.midiendo
                    onClicked: {
                        const e = Studio.medirSincroUi(10)
                        if (e !== "") { Studio.notify(e, "error"); return }
                        retardoPop.midiendo = true; retardoPop.resultado = ""
                    }
                }
                Text { Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.textFaint; font.pixelSize: 10
                       text: retardoPop.resultado !== "" ? retardoPop.resultado
                             : "Con la cámara en el programa: 10 s para aplaudir 4 o 5 veces frente a ella, separadas." }
            }
            Connections {
                target: Studio
                function onMedicionSincroLista(r) {
                    if (!retardoPop.midiendo) return
                    retardoPop.midiendo = false
                    if (r.ok) {
                        retMs.value = retardoPop.ms + r.retardo_ms
                        retardoPop.resultado = (r.retardo_ms >= 0 ? "+" : "") + r.retardo_ms + " ms (" + r.aplausos + " aplausos, ±" +
                                               r.dispersion_ms + " ms)" + (r.confiable ? "" : " — poco confiable: repetí")
                    } else {
                        retardoPop.resultado = "No se pudo: " + (r.problema || "no encontré aplausos")
                    }
                }
            }
            RowLayout {
                Item { Layout.fillWidth: true }
                GlassButton { compact: true; text: "Cancelar"; onClicked: retardoPop.close() }
                GlassButton { compact: true; active: true; text: "Aplicar"
                              onClicked: { const e = Studio.setRetardoUi(retardoPop.fuente, retMs.value); if (e !== "") Studio.notify(e, "error"); else retardoPop.close() } }
            }
        }
    }
}
