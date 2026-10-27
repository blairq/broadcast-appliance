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
    // El canal elegido con el teclado se trae a la vista (si no, con muchos quedaba oculto).
    onElegidoChanged: if (elegido >= 0) canales.positionViewAtIndex(elegido, ListView.Contain)

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
            case Qt.Key_Tab:     tarjeta.mover(1);  e.accepted = true; break
            case Qt.Key_Backtab: tarjeta.mover(-1); e.accepted = true; break
            case Qt.Key_Up:    tarjeta.ajustar(grande ? 3 : 1);  e.accepted = true; break
            case Qt.Key_Down:  tarjeta.ajustar(grande ? -3 : -1); e.accepted = true; break
            case Qt.Key_Home:  tarjeta.elegido = 0; e.accepted = true; break
            case Qt.Key_End:   tarjeta.elegido = tarjeta.cuantos() - 1; e.accepted = true; break
            case Qt.Key_M:
            case Qt.Key_Space: tarjeta.silenciar(); e.accepted = true; break
        }
    }

    // ⧉ El Mezclador entero en su propia ventana (entradas, salidas, matriz): para
    // otro monitor. Ahí se ve lo que sale de cada entrada y adónde va.
    // Audio ›: la pestaña donde se configura cada entrada (su rack, filtros, la matriz).
    GlassButton {
        anchors { top: parent.top; right: mezBtn.left; rightMargin: 6 }
        z: 10
        compact: true
        text: "Audio ›"
        onClicked: ApplicationWindow.window.showPage(Studio.riel.paginaDeId("audio"))
        ToolTip.visible: hovered; ToolTip.delay: 400
        ToolTip.text: "La pestaña Audio: el rack y los filtros de cada entrada, la matriz y las salidas."
    }
    GlassButton {
        id: mezBtn
        anchors { top: parent.top; right: parent.right }
        z: 10
        compact: true
        text: "⧉ Mezclador"
        onClicked: ApplicationWindow.window.sacarPanel("mezclador", "Mezclador")
        ToolTip.visible: hovered; ToolTip.delay: 400
        ToolTip.text: "Abre el Mezclador en su propia ventana: lo que sale de cada entrada, las salidas y la matriz."
    }

    RowLayout {
        // Debajo del botón ⧉ (si no, tapaba el fader de la última tira).
        anchors { fill: parent; topMargin: 34 }
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
            // El espacio entre tiras lo pone cada una (una tira oculta del grupo compactado no
            // ocupa nada, ni el espacio).
            readonly property int espacio: 14
            spacing: 0
            clip: true
            model: Studio.mixer
            // Si no entran todos, que se vea que hay más (y se puedan arrastrar).
            ScrollBar.horizontal: ScrollBar { policy: canales.contentWidth > canales.width ? ScrollBar.AlwaysOn : ScrollBar.AlwaysOff }
            // La rueda común (vertical) fuera de un fader desplaza los canales de costado; los
            // faders están encima y la toman antes (su volumen); de costado la maneja la lista.
            WheelHandler {
                orientation: Qt.Vertical
                acceptedDevices: PointerDevice.Mouse
                onWheel: (ev) => {
                    const max = Math.max(canales.originX, canales.originX + canales.contentWidth - canales.width)
                    canales.contentX = Math.max(canales.originX, Math.min(max, canales.contentX - ev.angleDelta.y / 120 * 60))
                }
            }
            // Arrastrar con el botón del medio (la rueda apretada) desplaza los canales.
            MouseArea {
                anchors.fill: parent
                z: 100
                acceptedButtons: Qt.MiddleButton
                cursorShape: pressed ? Qt.ClosedHandCursor : Qt.ArrowCursor
                property real x0: 0
                property real cx0: 0
                onPressed: (e) => { x0 = e.x; cx0 = canales.contentX }
                onPositionChanged: (e) => {
                    const max = Math.max(canales.originX, canales.originX + canales.contentWidth - canales.width)
                    canales.contentX = Math.max(canales.originX, Math.min(max, cx0 - (e.x - x0)))
                }
            }
            delegate: Item {
                id: tira
                required property int index
                required property string name
                required property real peakDb
                required property real volumeDb
                required property bool muted
                required property int retardoMs
                // Tiras seguidas del mismo tipo («Invitado 1…N (audio)») son un grupo: la
                // primera lleva el botón para compactarlo (sin faders, solo los botones),
                // que es lo que deja entrar a muchos invitados.
                required property string grupo
                required property bool primeroDeGrupo
                required property int tamGrupo
                required property bool compacto
                required property string nombreGrupo
                required property real gananciaGrupoDb
                // Del grupo compactado, la primera tira es el bloque entero y las demás no ocupan lugar.
                readonly property bool oculta: compacto && !primeroDeGrupo
                readonly property bool bloque: compacto && primeroDeGrupo
                // Desplegado, el grupo empieza con su fader general (VCA), como en una consola.
                readonly property bool conVca: primeroDeGrupo && !compacto
                readonly property int anchoVca: 46
                height: ListView.view.height
                width: oculta ? 0 : (bloque ? bloqueCol.width : 70 + (conVca ? anchoVca + 8 : 0)) + canales.espacio
                visible: !oculta

                // El volumen general del grupo (VCA): mueve a todos juntos sin tocar el fader de
                // cada uno, así las diferencias entre ellos quedan. Doble clic: a 0 dB.
                ColumnLayout {
                    visible: tira.conVca
                    anchors { left: parent.left; top: parent.top; bottom: parent.bottom }
                    width: tira.anchoVca
                    spacing: 6
                    Text {
                        Layout.fillWidth: true; Layout.preferredHeight: 16
                        horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter
                        text: "VCA"; color: Theme.accentSoft; font { pixelSize: 9; weight: Font.Bold; letterSpacing: 1 }
                    }
                    Slider {
                        id: vcaV
                        Layout.fillHeight: true
                        Layout.alignment: Qt.AlignHCenter
                        orientation: Qt.Vertical
                        from: -60; to: 12
                        value: Math.max(-60, tira.gananciaGrupoDb)
                        onMoved: Studio.mixer.setGananciaGrupoDb(tira.grupo, value)
                        ToolTip.visible: pressed || vcaVMa.containsMouse; ToolTip.delay: pressed ? 0 : 500
                        ToolTip.text: "Volumen general de " + tira.nombreGrupo + ": " + value.toFixed(1)
                                      + " dB (mueve a todos sin tocar el fader de cada uno; doble clic: 0 dB)"
                        MouseArea { id: vcaVMa; anchors.fill: parent; hoverEnabled: true; acceptedButtons: Qt.NoButton }
                        TapHandler { onDoubleTapped: Studio.mixer.setGananciaGrupoDb(tira.grupo, 0) }
                    }
                    Text {
                        Layout.fillWidth: true; horizontalAlignment: Text.AlignHCenter
                        text: (tira.gananciaGrupoDb > 0 ? "+" : "") + Math.max(-60, tira.gananciaGrupoDb).toFixed(1)
                        color: Math.abs(tira.gananciaGrupoDb) > 0.05 ? Theme.accent : Theme.textFaint
                        font { pixelSize: 10; weight: Font.DemiBold }
                    }
                    Text {
                        Layout.fillWidth: true; horizontalAlignment: Text.AlignHCenter
                        text: tira.nombreGrupo; color: Theme.accentSoft; elide: Text.ElideRight; font.pixelSize: 9
                    }
                }

                ColumnLayout {
                    visible: !tira.compacto
                    anchors { left: parent.left; leftMargin: tira.conVca ? tira.anchoVca + 8 : 0; top: parent.top; bottom: parent.bottom }
                    width: 70
                    spacing: 6
                    Item {
                        Layout.fillWidth: true
                        implicitHeight: 16
                        // La raya del grupo: une sus tiras (sobre el espacio entre tiras también).
                        Rectangle {
                            visible: tira.grupo !== ""
                            anchors { left: parent.left; right: parent.right; rightMargin: -canales.espacio; verticalCenter: parent.verticalCenter }
                            height: 2; radius: 1
                            color: Theme.accentSoft; opacity: 0.45
                        }
                        Rectangle {
                            visible: tira.primeroDeGrupo
                            width: 22; height: 16; radius: 5
                            color: grMa.containsMouse ? Theme.accent : Qt.rgba(0.12, 0.13, 0.16, 1)
                            border { width: 1; color: Theme.accentSoft }
                            Text { anchors.centerIn: parent; text: tira.compacto ? "+" : "−"; color: Theme.text; font { pixelSize: 12; weight: Font.Bold } }
                            MouseArea {
                                id: grMa
                                anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                                onClicked: Studio.mixer.setCompacto(tira.grupo, !tira.compacto)
                            }
                            ToolTip.visible: grMa.containsMouse; ToolTip.delay: 400
                            ToolTip.text: "Compactar " + tira.nombreGrupo + ": sin faders, una casilla por tira (" + tira.tamGrupo + " tiras)"
                        }
                    }
                    RowLayout {
                        Layout.fillHeight: true
                        Layout.alignment: Qt.AlignHCenter
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
                        color: muted ? Theme.canalMute : Qt.rgba(1, 1, 1, 0.08)
                        Text { anchors.centerIn: parent; text: muted ? "MUDO" : "M"; color: Theme.text; font { pixelSize: 10; weight: Font.Bold } }
                        MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: Studio.mixer.setMuted(name, !muted) }
                    }
                    // PFL: escucharla por el monitor sin tocar el aire (uno a la vez, amarillo).
                    Rectangle {
                        readonly property bool on: Studio.pfl === name
                        Layout.fillWidth: true
                        height: 18; radius: 6
                        color: on ? Theme.canalPfl : Qt.rgba(1, 1, 1, 0.05)
                        Text { anchors.centerIn: parent; text: "PFL"; color: parent.on ? "#1a1a1a" : Theme.textFaint; font { pixelSize: 9; weight: Font.Bold } }
                        MouseArea {
                            id: pflMa
                            anchors.fill: parent; cursorShape: Qt.PointingHandCursor; hoverEnabled: true
                            onClicked: { const e = Studio.pflUi(name); if (e !== "") Studio.notify(e, "error") }
                        }
                        ToolTip.visible: pflMa.containsMouse; ToolTip.delay: 500
                        ToolTip.text: "Escucharla en los auriculares sin tocar lo que sale al aire (se suma a lo que ya va ahí, como la voz del asistente). Si está MUDA no suena ni acá: bajale el volumen en vez de silenciarla."
                    }
                    // AFV: el audio sigue al video (sube y baja con su escena).
                    Rectangle {
                        readonly property bool on: Studio.afvFuentes.indexOf(name) >= 0
                        Layout.fillWidth: true
                        height: 18; radius: 6
                        color: on ? Theme.canalAfv : Qt.rgba(1, 1, 1, 0.05)
                        Text { anchors.centerIn: parent; text: "AFV"; color: parent.on ? Theme.text : Theme.textFaint; font { pixelSize: 9; weight: Font.Bold } }
                        MouseArea {
                            anchors.fill: parent; cursorShape: Qt.PointingHandCursor
                            onClicked: Studio.setAfv(name, !parent.on)
                        }
                        ToolTip.visible: afvMa.containsMouse; ToolTip.delay: 500
                        ToolTip.text: "Audio que sigue al video: sube cuando entra al aire una escena con esta fuente y baja cuando sale"
                        MouseArea { id: afvMa; anchors.fill: parent; hoverEnabled: true; acceptedButtons: Qt.NoButton }
                    }
                    // Sincronizar con el video (sincro labios): el retardo del canal, a mano o con aplausos.
                    Rectangle {
                        id: sincBtn
                        readonly property int ms: retardoMs
                        Layout.fillWidth: true
                        height: 18; radius: 6
                        color: ms !== 0 ? Theme.canalSinc : Qt.rgba(1, 1, 1, 0.05)
                        Text { anchors.centerIn: parent; text: sincBtn.ms !== 0 ? (sincBtn.ms > 0 ? "+" : "") + sincBtn.ms + " ms" : "SINC"
                               color: sincBtn.ms !== 0 ? Theme.text : Theme.textFaint; font { pixelSize: 9; weight: Font.Bold } }
                        MouseArea { id: sincMa; anchors.fill: parent; cursorShape: Qt.PointingHandCursor; hoverEnabled: true
                                    onClicked: retardoPop.abrir(name) }
                        ToolTip.visible: sincMa.containsMouse; ToolTip.delay: 500
                        ToolTip.text: "Sincronizar con el video: atrasar este audio hasta que coincida con los labios (a mano o midiendo con aplausos)"
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
                           // Un clic: sincronizarla con su video (como el botón de abajo).
                           MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor
                                       onClicked: retardoPop.abrir(name) } }
                }

                // El grupo compactado: un botón para silenciarlo entero y una casilla por tira
                // (su número, verde cuando habla, roja muda; un clic la silencia). La grilla
                // llena la altura y suma columnas: 64 invitados entran en unas pocas.
                ColumnLayout {
                    id: bloqueCol
                    visible: tira.bloque
                    anchors { left: parent.left; top: parent.top; bottom: parent.bottom }
                    // Lo que ocupe más: la grilla o el nombre del grupo entero.
                    width: Math.max(70, grilla.ancho, cabecera.implicitWidth)
                    spacing: 6
                    RowLayout {
                        id: cabecera
                        Layout.fillWidth: true
                        Layout.preferredHeight: 16
                        spacing: 4
                        Rectangle {
                            width: 22; height: 16; radius: 5
                            color: abrMa.containsMouse ? Theme.accent : Qt.rgba(0.12, 0.13, 0.16, 1)
                            border { width: 1; color: Theme.accentSoft }
                            Text { anchors.centerIn: parent; text: "+"; color: Theme.text; font { pixelSize: 12; weight: Font.Bold } }
                            MouseArea { id: abrMa; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                                        onClicked: Studio.mixer.setCompacto(tira.grupo, false) }
                            ToolTip.visible: abrMa.containsMouse; ToolTip.delay: 400
                            ToolTip.text: "Mostrar los faders de " + tira.nombreGrupo + " (" + tira.tamGrupo + " tiras)"
                        }
                        Text {
                            Layout.fillWidth: true
                            text: tira.nombreGrupo
                            color: Theme.textDim; font.pixelSize: 10
                        }
                    }
                    Rectangle {
                        Layout.fillWidth: true
                        height: 22; radius: 8
                        color: Qt.rgba(1, 1, 1, 0.08)
                        Text { anchors.centerIn: parent; text: "M todos"; color: Theme.text; font { pixelSize: 10; weight: Font.Bold } }
                        MouseArea {
                            id: todosMa
                            anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                            // Con alguna sonando, se silencian todas; todas mudas, vuelven todas.
                            onClicked: {
                                let alguna = false
                                for (let k = 0; k < tira.tamGrupo; ++k) {
                                    const c = Studio.mixer.canal(tira.index + k)
                                    if (c.nombre !== undefined && !c.mudo) alguna = true
                                }
                                Studio.mixer.setMutedGrupo(tira.grupo, alguna)
                            }
                        }
                        ToolTip.visible: todosMa.containsMouse; ToolTip.delay: 400
                        ToolTip.text: "Silenciar todo el grupo (si ya están todas mudas, las vuelve a abrir)"
                    }
                    // El volumen general del grupo (VCA), acostado: compactado no hay lugar para uno parado.
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 2
                        Slider {
                            id: vcaH
                            Layout.fillWidth: true
                            from: -60; to: 12
                            value: Math.max(-60, tira.gananciaGrupoDb)
                            onMoved: Studio.mixer.setGananciaGrupoDb(tira.grupo, value)
                            ToolTip.visible: pressed; ToolTip.text: value.toFixed(1) + " dB"
                            TapHandler { onDoubleTapped: Studio.mixer.setGananciaGrupoDb(tira.grupo, 0) }
                        }
                        Text {
                            text: (tira.gananciaGrupoDb > 0 ? "+" : "") + Math.max(-60, tira.gananciaGrupoDb).toFixed(0)
                            color: Math.abs(tira.gananciaGrupoDb) > 0.05 ? Theme.accent : Theme.textFaint
                            font { pixelSize: 9; weight: Font.DemiBold }
                        }
                    }
                    GridView {
                        id: grilla
                        Layout.fillHeight: true
                        Layout.preferredWidth: ancho
                        readonly property int filas: Math.max(1, Math.floor(height / cellHeight))
                        readonly property int ancho: Math.ceil(count / filas) * cellWidth
                        flow: GridView.FlowTopToBottom
                        cellWidth: 34; cellHeight: 24
                        interactive: false
                        model: tira.bloque ? Studio.mixer.delGrupo(tira.grupo) : null
                        delegate: Rectangle {
                            id: casilla
                            required property int index
                            required property string name
                            required property real peakDb
                            required property bool muted
                            width: 30; height: 20; radius: 5
                            color: muted ? Theme.canalMute
                                         : Qt.rgba(0.24, 0.86, 0.59, Math.max(0.08, Math.min(0.85, (peakDb + 50) / 50)))
                            Text {
                                anchors.centerIn: parent
                                // Su número; si el nombre no lo tiene («Remoto con la palabra»), su lugar en el grupo.
                                text: (casilla.name.match(/\d+/) || [String(casilla.index + 1)])[0]
                                color: Theme.text; font { pixelSize: 10; weight: Font.DemiBold }
                            }
                            MouseArea {
                                id: casMa
                                anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                                onClicked: Studio.mixer.setMuted(casilla.name, !casilla.muted)
                            }
                            ToolTip.visible: casMa.containsMouse; ToolTip.delay: 400
                            ToolTip.text: casilla.name + (casilla.muted ? " · muda" : "") + " (un clic la silencia)"
                        }
                    }
                }
            }
        }
    }

    // Sincronizar un canal con su video (sincro labios), como audio_sincro.
    RetardoCanal { id: retardoPop }
}
