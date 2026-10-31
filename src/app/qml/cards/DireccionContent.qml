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
import QtQuick.Controls
import QtQuick.Layouts
import BA
import "../controls"

// CON EL TECLADO (un clic en la tarjeta y ya):
//   T o Enter   TAKE: saca al aire la escena armada
//
// Es la unica tarjeta donde una tecla saca algo al aire, y es a proposito:
// esta es la tarjeta del que dirige, dice AL AIRE y ARMADA en grande, y el
// TAKE es lo que se hace mirando eso. En las demas, las teclas eligen y miran;
// sacar al aire tiene su propio gesto.
//
// No hace nada si no hay nada armado: un TAKE al vacio no es una operacion.
Item {
    id: tarjeta
    focus: true
    Keys.onPressed: (e) => {
        if ((e.key === Qt.Key_T || e.key === Qt.Key_Return || e.key === Qt.Key_Enter) &&
            Studio.escenaArmada !== "" && !Studio.focoEnTexto()) {
            Studio.take()
            e.accepted = true
        }
    }
    readonly property var ev: Studio.evento
    readonly property var reloj: ev ? ev.reloj : ({})
    readonly property bool corriendo: ev && ev.estado === "corriendo"

    function mmss(s) {
        const n = Math.max(0, Math.round(Math.abs(s)))
        return (s < 0 ? "−" : "") + Math.floor(n / 60) + ":" + String(n % 60).padStart(2, "0")
    }

    QrControlEscenas { id: qrEscenas; parent: Overlay.overlay }

    ColumnLayout {
        anchors.fill: parent
        spacing: 8

        // --- De qué salida (con varias: el aire, la armada, el TAKE y la T-bar son de esa) ---
        SelectorSalida { Layout.fillWidth: true }

        // --- FTB y placa: tapar todo lo que sale a un toque ---------------------
        BotonesSeguridad { Layout.fillWidth: true }
        // --- Capas DSK encima del programa (logo, intérprete LSA) --------------
        FilaDsk { Layout.fillWidth: true }
        // --- El medio al aire: cuánto le queda (rojo en los últimos 10 s) ---------
        Repeater {
            model: Studio.mediosAlAire
            delegate: RowLayout {
                required property var modelData
                Layout.fillWidth: true
                Text { Layout.fillWidth: true; elide: Text.ElideRight; text: "▶ " + modelData.fuente; color: Theme.text; font.pixelSize: 12 }
                Text {
                    text: "−" + Math.floor(modelData.restante_s / 60) + ":" + String(modelData.restante_s % 60).padStart(2, "0")
                    color: modelData.restante_s <= 10 ? Theme.live : Theme.text
                    font.pixelSize: 26; font.weight: Font.Black; font.family: Theme.mono
                }
            }
        }
        // --- Back-timing del guion: lo que falta del bloque y a qué hora se termina ---
        Text {
            readonly property var r: Studio.evento ? Studio.evento.reloj : ({})
            visible: !!(r && r.horaFinPrevista && r.restanteGuionSeg > 0)
            Layout.fillWidth: true; wrapMode: Text.WordWrap
            text: {
                const faltan = Math.floor((r.restanteBloqueSeg || 0) / 60)
                const fin = r.horaFinPrevista || ""
                if (!r.horaFinPlaneada) return qsTr("Bloque: faltan %1 min · fin prevista %2").arg(faltan).arg(fin)
                const dif = Math.abs(Math.round(r.backTimingSeg / 60))
                return (r.backTimingSeg >= 0 ? qsTr("Bloque: faltan %1 min · fin prevista %2 (planeada %3, sobran %4 min)")
                                             : qsTr("Bloque: faltan %1 min · fin prevista %2 (planeada %3, hay que recortar %4 min)"))
                       .arg(faltan).arg(fin).arg(r.horaFinPlaneada).arg(dif)
            }
            color: r && r.backTimingSeg < 0 ? Theme.amber : Theme.textDim
            font.pixelSize: 11
        }
        // --- El punto del orden del día que se trata ------------------------------
        RowLayout {
            visible: Studio.ordenDelDia.puntos.length > 0
            Layout.fillWidth: true
            Text {
                Layout.fillWidth: true; elide: Text.ElideRight
                text: Studio.ordenDelDia.actual && Studio.ordenDelDia.actual.n
                      ? qsTr("Punto %1: %2").arg(Studio.ordenDelDia.actual.n).arg(Studio.ordenDelDia.actual.titulo)
                      : qsTr("Orden del día: ningún punto en tratamiento")
                color: Theme.text; font.pixelSize: 12
            }
            GlassButton {
                compact: true; text: qsTr("Siguiente punto ▶")
                enabled: Studio.ordenDelDia.puntos.some(p => p.estado === "pendiente")
                onClicked: { const e = Studio.ordenDelDia.siguienteUi(); if (e !== "") Studio.notify(e, "error") }
            }
        }

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
                    Text { text: qsTr("AL AIRE"); color: Qt.rgba(1, 1, 1, 0.75)
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
                    Text { text: qsTr("ARMADA"); color: Theme.textFaint
                           font { family: Theme.font; pixelSize: 10; weight: Font.DemiBold; letterSpacing: 1 } }
                    Text { Layout.fillWidth: true; text: Studio.escenaArmada || qsTr("nada"); elide: Text.ElideRight
                           color: Studio.escenaArmada !== "" ? Theme.text : Theme.textFaint
                           font { family: Theme.font; pixelSize: 14; weight: Font.DemiBold } }
                }
            }
            GlassButton {
                text: qsTr("CUT")
                enabled: Studio.escenaArmada !== ""
                onClicked: Studio.takeCon("cut", 0)
                ToolTip.visible: hovered; ToolTip.delay: 400; ToolTip.text: qsTr("Corte seco a la armada (sin transición)")
            }
            GlassButton {
                text: qsTr("AUTO"); active: true
                enabled: Studio.escenaArmada !== ""
                onClicked: Studio.take()
                ToolTip.visible: hovered; ToolTip.delay: 400; ToolTip.text: qsTr("TAKE con la transición (la de entrada de la escena o la global)")
            }
        }
        // --- T-bar: la transición a mano --------------------------------------------
        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            enabled: Studio.escenaArmada !== ""
            Text { text: qsTr("T-bar"); color: Theme.textDim; font.pixelSize: 11 }
            Slider {
                id: tbar
                Layout.fillWidth: true
                from: 0; to: 1; value: 0
                onMoved: Studio.tbar(value, false)
                onPressedChanged: if (!pressed) { Studio.tbar(value, true); value = 0 }
            }
            Text { text: qsTr("Entra con"); color: Theme.textDim; font.pixelSize: 11 }
            ComboBox {
                id: entrada
                implicitWidth: 120
                readonly property var actual: Studio.escenaArmada !== "" ? Studio.transicionEntrada(Studio.escenaArmada) : ({})
                model: [ { t: qsTr("la global"), v: "" }, { t: qsTr("corte"), v: "cut" }, { t: qsTr("fundido"), v: "fade" },
                         { t: qsTr("barrido"), v: "swipe" }, { t: qsTr("deslizar"), v: "slide" }, { t: qsTr("a color"), v: "fade_to_color" } ]
                textRole: "t"
                currentIndex: Math.max(0, model.findIndex(o => o.v === (actual.tipo || "")))
                onActivated: (i) => {
                    const e = Studio.setTransicionEntrada(Studio.escenaArmada, model[i].v, model[i].v === "cut" ? 0 : -1)
                    if (e !== "") Studio.notify(e, "error")
                }
                ToolTip.visible: hovered; ToolTip.delay: 400; ToolTip.text: qsTr("La transición con la que entra la escena armada")
            }
        }

        // --- Aire y grabación ---------------------------------------------------
        RowLayout {
            Layout.fillWidth: true
            spacing: 14
            // Los de la salida que maneja la interfaz (con una sola, la de siempre).
            StatusPill {
                text: Studio.salidas.aire.transmitiendo ? qsTr("AL AIRE · %1").arg(Studio.salidas.aire.tiempoTransmision) : qsTr("fuera del aire")
                dot: Studio.salidas.aire.transmitiendo ? Theme.live : Theme.textFaint
                pulse: !!Studio.salidas.aire.transmitiendo
            }
            StatusPill {
                visible: !!Studio.salidas.aire.grabando
                text: qsTr("REC · %1").arg(Studio.salidas.aire.tiempoGrabacion)
                dot: Theme.live; pulse: true
            }
            Item { Layout.fillWidth: true }
            // Manejar las escenas desde una tablet o un celular (la Botonera «Director»).
            GlassButton {
                compact: true; text: qsTr("QR de escenas")
                onClicked: qrEscenas.open()
                ToolTip.visible: hovered; ToolTip.delay: 400
                ToolTip.text: qsTr("El QR para manejar las escenas desde una tablet o un celular")
            }
            Text {
                visible: Studio.droppedFrames > 0
                text: qsTr("%n perdido", "", Studio.droppedFrames)
                color: Theme.amber; font.pixelSize: 11
            }
        }

        // --- El guion: los controles, el cue y los tiempos ----------------------------
        // Con un guion abierto (corriendo o no): arrancar, anterior / siguiente, pausa y
        // parar, el ensayo (recorre sin sacar nada al aire), y cuánto lleva contra lo
        // previsto y cuánto se va pasando.
        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: !!tarjeta.ev && tarjeta.ev.abierto !== ""
            spacing: 4
            readonly property string estado: tarjeta.ev ? tarjeta.ev.estado : "parado"
            readonly property bool enMarcha: estado === "corriendo" || estado === "pausado"

            RowLayout {
                Layout.fillWidth: true
                spacing: 6
                GlassButton {
                    compact: true; text: "◀"
                    enabled: parent.parent.enMarcha
                    onClicked: tarjeta.ev.retroceder()
                    ToolTip.visible: hovered; ToolTip.delay: 400; ToolTip.text: qsTr("Volver al cue anterior")
                }
                GlassButton {
                    Layout.fillWidth: true
                    active: true
                    text: parent.parent.enMarcha ? qsTr("Siguiente cue ▶") : (tarjeta.ev && tarjeta.ev.ensayo ? qsTr("Arrancar el ensayo") : qsTr("Arrancar el guion"))
                    onClicked: {
                        if (parent.parent.enMarcha) { tarjeta.ev.avanzar(); return }
                        // Con la revisión: si encontró algo grave no arranca y avisa.
                        const graves = tarjeta.ev.arrancarRevisando(false)
                        if (graves > 0) Studio.notify(qsTr("La revisión encontró %n problema grave: miralos en Guion del evento (ahí se puede arrancar igual).", "", graves), "error")
                    }
                }
                GlassButton {
                    compact: true
                    enabled: parent.parent.enMarcha
                    text: parent.parent.estado === "pausado" ? "⏵" : "⏸"
                    onClicked: parent.parent.estado === "pausado" ? tarjeta.ev.reanudar() : tarjeta.ev.pausar()
                    ToolTip.visible: hovered; ToolTip.delay: 400
                    ToolTip.text: parent.parent.estado === "pausado" ? qsTr("Seguir (lo pausado no cuenta como atraso)") : qsTr("Pausar el guion")
                }
                GlassButton {
                    compact: true; text: "■"
                    enabled: parent.parent.enMarcha
                    onClicked: tarjeta.ev.parar()
                    ToolTip.visible: hovered; ToolTip.delay: 400; ToolTip.text: qsTr("Terminar el guion (queda el informe de tiempos)")
                }
            }
            // Los tiempos del guion entero.
            GridLayout {
                Layout.fillWidth: true
                visible: parent.enMarcha
                columns: 3
                columnSpacing: 10
                rowSpacing: 0
                Text { text: qsTr("LLEVA"); color: Theme.textFaint; font { pixelSize: 9; weight: Font.DemiBold; letterSpacing: 1 } }
                Text { text: qsTr("VAS"); color: Theme.textFaint; font { pixelSize: 9; weight: Font.DemiBold; letterSpacing: 1 } }
                Text { text: qsTr("TERMINA"); color: Theme.textFaint; font { pixelSize: 9; weight: Font.DemiBold; letterSpacing: 1 } }
                Text {
                    text: tarjeta.mmss(tarjeta.reloj.transcurridoSeg || 0) + " / " + tarjeta.mmss(tarjeta.reloj.totalPrevistoSeg || 0)
                    color: Theme.text; font { family: "JetBrains Mono"; pixelSize: 14 }
                    ToolTip.visible: llevaMa.containsMouse; ToolTip.text: qsTr("Lo que lleva el guion (sin las pausas) de lo previsto en total")
                    MouseArea { id: llevaMa; anchors.fill: parent; hoverEnabled: true }
                }
                Text {
                    // El desvío: positivo = vas tarde, negativo = adelantado.
                    readonly property int d: tarjeta.reloj.desvioSeg || 0
                    text: Math.abs(d) < 5 ? qsTr("en hora") : (d > 0 ? qsTr("%1 tarde").arg(tarjeta.mmss(d)) : qsTr("%1 antes").arg(tarjeta.mmss(-d)))
                    color: d > 20 ? Theme.live : (d < -20 ? Theme.amber : Theme.ok)
                    font { family: "JetBrains Mono"; pixelSize: 14; weight: Font.DemiBold }
                }
                Text {
                    readonly property var r: tarjeta.reloj
                    text: r.horaFinPlaneada ? qsTr("%1 (plan %2)").arg(r.horaFinPrevista || "—").arg(r.horaFinPlaneada) : (r.horaFinPrevista || "—")
                    color: r && r.backTimingSeg < 0 ? Theme.amber : Theme.text
                    font { family: "JetBrains Mono"; pixelSize: 14 }
                }
            }
            // El cue en curso: qué es y cuánto le queda (negativo = ya se pasó).
            RowLayout {
                Layout.fillWidth: true
                visible: parent.enMarcha
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
            // Un cue con vista web (el teleprompter): abrirla para ver si carga y avanza.
            GlassButton {
                readonly property string url: parent.enMarcha && tarjeta.ev.actual.tipo ? tarjeta.ev.enlaceDeCue(tarjeta.ev.actual.tipo, tarjeta.ev.actual.datos || ({})) : ""
                visible: url !== ""
                compact: true
                text: qsTr("Ver el teleprompter ↗")
                onClicked: Qt.openUrlExternally(url)
                ToolTip.visible: hovered; ToolTip.delay: 400; ToolTip.text: qsTr("La pantalla del orador en el navegador (%1)").arg(url)
            }
            Text {
                Layout.fillWidth: true
                visible: parent.enMarcha
                text: qsTr("sigue: %1   ·   cue %2 de %3").arg(tarjeta.ev.siguiente.texto || "—").arg(tarjeta.reloj.cueNro || 0).arg(tarjeta.reloj.cuesTotal || 0)
                color: Theme.textDim; elide: Text.ElideRight; font.pixelSize: 12
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: 6
                Toggle {
                    checked: !!tarjeta.ev && tarjeta.ev.ensayo
                    enabled: parent.parent.estado === "parado" || parent.parent.estado === "terminado"
                    onToggled: (w) => tarjeta.ev.ensayo = w
                }
                Text {
                    Layout.fillWidth: true
                    elide: Text.ElideRight
                    text: tarjeta.ev && tarjeta.ev.ensayo ? qsTr("Ensayo: recorre el guion sin sacar nada al aire")
                                                          : qsTr("Ensayo apagado: los cues salen al aire de verdad")
                    color: tarjeta.ev && tarjeta.ev.ensayo ? Theme.amber : Theme.textFaint
                    font.pixelSize: 11
                }
                Text {
                    visible: parent.parent.estado === "pausado"
                    text: qsTr("EN PAUSA")
                    color: Theme.amber
                    font { pixelSize: 11; weight: Font.DemiBold }
                }
            }

            Item { Layout.fillHeight: true }
        }
        Text {
            Layout.fillWidth: true
            visible: !tarjeta.ev || tarjeta.ev.abierto === ""
            text: qsTr("Sin guion abierto. Abrí uno en Guion del evento para manejarlo y ver los tiempos acá.")
            color: Theme.textFaint; font.pixelSize: 11; wrapMode: Text.WordWrap
        }
    }
}
