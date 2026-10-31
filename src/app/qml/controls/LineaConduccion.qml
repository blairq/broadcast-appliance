// La conducción de la línea de tiempo (PLAN-LINEA-DE-TIEMPO 3.5 C): el paso al aire en
// grande con su cuenta regresiva (y el «+» rojo si se pasa), el texto de su guion y sus
// adlibs; el próximo con su PVW y cuánto falta; GO, HOLD, Saltar, mover el próximo
// (F9/F10) e Insertar; el estante de adlibs como cue cart (F1…F8); y lo de correr
// (arrancar con la revisión, pausa, parar, informe, ensayo).
//
// Solo dibuja Studio.linea.conduccion y le pasa los botones a Studio.linea (que maneja la
// salida que se mira). Arrancar, pausar, parar y el ensayo siguen siendo del evento.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import BA

ColumnLayout {
    id: cond
    readonly property var lv: Studio.linea
    readonly property var ev: Studio.evento
    readonly property var c: lv ? lv.conduccion : ({})
    readonly property var aire: c.alAire || null
    readonly property var prox: c.proximo || null
    readonly property bool parado: !lv || !lv.corriendo
    /// Ver cuánto duró de verdad cada bloque (el diálogo es de la página).
    signal pedirInforme()
    spacing: 8

    function hacer(error) { if (error !== "") Studio.notify(error, "error") }
    function arrancar() {
        // La revisión previa la hace el view model; si hay algo grave, se pregunta.
        const graves = cond.ev.arrancarRevisando(false)
        if (graves === 0) return
        ApplicationWindow.window.confirm({
            title: qsTr("¿Correr el guion igual?"),
            text: qsTr("La revisión encontró %n problema grave. Mirá «Revisión» antes de salir.", "", graves),
            accept: qsTr("Correr igual"), reject: qsTr("Revisar"),
            action: function() { cond.ev.arrancarRevisando(true) }
        })
    }

    Text {
        Layout.fillWidth: true
        horizontalAlignment: Text.AlignHCenter
        visible: cond.parado
        text: cond.ev.estado === "terminado" ? qsTr("La línea terminó") : qsTr("La línea no está corriendo")
        color: Theme.textFaint
        font.pixelSize: 13
    }

    // --- Al aire, en grande ------------------------------------------------------------
    Rectangle {
        Layout.fillWidth: true
        Layout.preferredHeight: aireCol.implicitHeight + 16
        visible: !!cond.aire
        radius: 6
        color: Qt.rgba(1, 0.23, 0.19, 0.14)
        border.color: Theme.live
        ColumnLayout {
            id: aireCol
            anchors { fill: parent; margins: 8 }
            spacing: 4
            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                MiniaturaPaso {
                    Layout.preferredWidth: 112
                    Layout.preferredHeight: 63
                    mini: cond.aire ? cond.aire.mini : ({})
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 0
                    Text {
                        text: qsTr("AL AIRE") + (cond.c.bloque ? " · " + cond.c.bloque.nombre : "")
                        color: Theme.live
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                        font { pixelSize: 10; weight: Font.Bold; letterSpacing: 0.5 }
                    }
                    Text {
                        Layout.fillWidth: true
                        text: cond.aire ? cond.aire.nombre : ""
                        elide: Text.ElideRight
                        color: Theme.text
                        font { family: Theme.font; pixelSize: 18; weight: Font.DemiBold }
                    }
                    // La cuenta: lo que falta o, pasado, el «+» en rojo.
                    Text {
                        readonly property int r: cond.aire ? cond.aire.restanteSeg : 0
                        text: cond.aire && cond.aire.previstoSeg > 0 ? (r < 0 ? "+" + Theme.mmss(-r) : Theme.mmss(r))
                                                                     : (cond.aire ? Theme.mmss(cond.aire.llevaSeg) : "")
                        color: r < 0 ? Theme.live : r <= 10 ? Theme.amber : Theme.text
                        font { family: Theme.mono; pixelSize: 30; weight: Font.Bold }
                    }
                }
            }
            Text {
                Layout.fillWidth: true
                text: cond.aire ? qsTr("lleva %1 de %2").arg(Theme.mmss(cond.aire.llevaSeg)).arg(Theme.mmss(cond.aire.previstoSeg)) : ""
                color: Theme.textDim
                font { family: Theme.mono; pixelSize: 11 }
            }
            Text {
                Layout.fillWidth: true
                visible: cond.aire && (cond.aire.nota || "") !== ""
                text: cond.aire ? cond.aire.nota || "" : ""
                wrapMode: Text.WordWrap
                color: Theme.amber
                font.pixelSize: 12
            }
            // Los adlibs del paso (sus piezas manuales): ya disparados, apagados.
            Flow {
                Layout.fillWidth: true
                visible: cond.aire && cond.aire.adlibs && cond.aire.adlibs.length > 0
                spacing: 4
                Repeater {
                    model: cond.aire && cond.aire.adlibs ? cond.aire.adlibs : []
                    delegate: GlassButton {
                        required property var modelData
                        compact: true
                        text: modelData.texto
                        active: !modelData.disparada
                        activeColor: Theme.colorDeTipo(modelData.tipo)
                        opacity: modelData.disparada ? 0.5 : 1
                        onClicked: cond.hacer(cond.lv.adlib(modelData.id))
                    }
                }
            }
        }
    }

    // El guion del paso: lo que dice quien conduce.
    ScrollView {
        Layout.fillWidth: true
        Layout.preferredHeight: Math.min(110, guionTxt.implicitHeight + 8)
        visible: cond.aire && (cond.aire.guion || "") !== ""
        clip: true
        contentWidth: availableWidth
        Text {
            id: guionTxt
            width: parent.width
            text: cond.aire ? cond.aire.guion || "" : ""
            wrapMode: Text.WordWrap
            color: Theme.text
            font { family: Theme.font; pixelSize: 14 }
        }
    }

    // --- El próximo, con su PVW ---------------------------------------------------------
    Rectangle {
        Layout.fillWidth: true
        Layout.preferredHeight: 62
        visible: !!cond.prox
        radius: 6
        color: Qt.rgba(0.96, 0.72, 0.24, 0.10)
        border.color: Theme.amber
        RowLayout {
            anchors { fill: parent; margins: 6 }
            spacing: 8
            MiniaturaPaso {
                Layout.preferredWidth: 88
                Layout.preferredHeight: 50
                mini: cond.prox ? cond.prox.mini : ({})
            }
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 0
                Text {
                    text: cond.prox && cond.prox.movidoAMano ? qsTr("PRÓXIMO (movido a mano)") : qsTr("PRÓXIMO")
                    color: Theme.amber
                    font { pixelSize: 10; weight: Font.Bold; letterSpacing: 0.5 }
                }
                Text {
                    Layout.fillWidth: true
                    text: cond.prox ? cond.prox.nombre : ""
                    elide: Text.ElideRight
                    color: Theme.text
                    font { family: Theme.font; pixelSize: 14; weight: Font.DemiBold }
                }
                Text {
                    Layout.fillWidth: true
                    elide: Text.ElideRight
                    text: !cond.prox ? ""
                          : cond.prox.automatico ? qsTr("entra solo a las %1 (en %2)").arg(cond.prox.frente).arg(Theme.mmss(cond.prox.faltaSeg))
                          : qsTr("con GO · previsto %1").arg(cond.prox.frente)
                    color: Theme.textDim
                    font { family: Theme.mono; pixelSize: 10 }
                }
            }
        }
    }
    Text {
        Layout.fillWidth: true
        visible: !cond.parado && !cond.prox
        horizontalAlignment: Text.AlignHCenter
        text: qsTr("No viene nada más: con GO termina la salida")
        color: Theme.textFaint
        font.pixelSize: 11
    }

    // El aviso de los últimos 5 s o, frenado, el HOLD.
    Text {
        Layout.fillWidth: true
        horizontalAlignment: Text.AlignHCenter
        readonly property int seg: cond.prox ? (cond.prox.entraSoloEnSeg || 0) : 0
        visible: !!cond.c.hold || seg > 0
        text: cond.c.hold ? qsTr("HOLD: este paso no se va solo") : qsTr("entra solo en %1").arg(Theme.mmss(seg))
        color: cond.c.hold ? Theme.live : Theme.amber
        font { family: Theme.font; pixelSize: 15; weight: Font.DemiBold }
    }

    // --- Lo que propone el productor para llegar a horario (L9) --------------------------
    // Studio.linea.productor vigila el over/under y propone; acá una persona elige una
    // opción (se aplica con su revisión para deshacer) o la descarta. Nada se aplica solo.
    Repeater {
        model: cond.lv && cond.lv.productor ? cond.lv.productor.propuestas : []
        delegate: Rectangle {
            id: prop
            required property var modelData
            Layout.fillWidth: true
            Layout.preferredHeight: propCol.implicitHeight + 12
            radius: 6
            color: Qt.rgba(0.96, 0.72, 0.24, 0.08)
            border.color: Theme.amber
            ColumnLayout {
                id: propCol
                anchors { fill: parent; margins: 6 }
                spacing: 4
                RowLayout {
                    Layout.fillWidth: true
                    Text {
                        Layout.fillWidth: true
                        text: (cond.lv.salidas.length > 1 ? "«" + prop.modelData.salida + "» · " : "") + prop.modelData.texto
                        wrapMode: Text.WordWrap
                        color: Theme.amber
                        font { pixelSize: 12; weight: Font.DemiBold }
                    }
                    GlassButton {
                        text: qsTr("Descartar"); compact: true
                        onClicked: cond.lv.productor.descartar(prop.modelData.salida)
                    }
                }
                // Cada opción ya viene probada: cómo quedaría el fin.
                Repeater {
                    model: prop.modelData.opciones || []
                    delegate: RowLayout {
                        id: op
                        required property var modelData
                        Layout.fillWidth: true
                        spacing: 6
                        Text {
                            Layout.fillWidth: true
                            text: op.modelData.texto
                                  + (op.modelData.resultado && op.modelData.resultado.fin
                                     ? " · " + qsTr("termina %1").arg(op.modelData.resultado.fin) : "")
                            wrapMode: Text.WordWrap
                            color: Theme.text
                            font.pixelSize: 11
                        }
                        GlassButton {
                            text: qsTr("Aplicar"); compact: true
                            onClicked: cond.hacer(cond.lv.productor.aplicar(prop.modelData.salida, op.modelData.id))
                            ToolTip.visible: hovered; ToolTip.delay: 400
                            ToolTip.text: qsTr("Cambia la línea (queda una revisión para deshacer)")
                        }
                    }
                }
            }
        }
    }

    // La salida se cambió a mano fuera de la línea: volver a la escena del paso.
    Rectangle {
        Layout.fillWidth: true
        Layout.preferredHeight: aManoFila.implicitHeight + 10
        visible: !cond.parado && !!cond.c.aMano
        radius: 6
        color: Qt.rgba(1, 1, 1, 0.05)
        border.color: Theme.amber
        RowLayout {
            id: aManoFila
            anchors { fill: parent; margins: 5 }
            Text {
                Layout.fillWidth: true
                text: qsTr("A mano: «%1» en vez de «%2», la del paso").arg(cond.c.aMano ? cond.c.aMano.escena : "")
                                                                    .arg(cond.c.aMano ? cond.c.aMano.esperada : "")
                wrapMode: Text.WordWrap
                color: Theme.amber
                font.pixelSize: 11
            }
            GlassButton {
                text: qsTr("Reenganchar"); compact: true
                onClicked: cond.hacer(cond.ev.reenganchar(cond.lv.salida))
                ToolTip.visible: hovered; ToolTip.delay: 400
                ToolTip.text: qsTr("Vuelve a la escena del paso al aire y la salida sigue con la línea")
            }
        }
    }

    // Las PTZ que esperan que su cámara salga del aire.
    Repeater {
        model: cond.c.ptzPendientes || []
        delegate: RowLayout {
            id: ptz
            required property var modelData
            Layout.fillWidth: true
            spacing: 6
            Text {
                Layout.fillWidth: true
                text: qsTr("PTZ «%1» a «%2» espera: la cámara está al aire").arg(ptz.modelData.camara).arg(ptz.modelData.posicion)
                wrapMode: Text.WordWrap
                color: Theme.amber
                font.pixelSize: 11
            }
            GlassButton {
                text: qsTr("Mover igual"); compact: true
                onClicked: cond.hacer(cond.ev.decidirPtz(ptz.modelData.pieza, "mover"))
            }
            GlassButton {
                text: qsTr("No mover"); compact: true
                onClicked: cond.hacer(cond.ev.decidirPtz(ptz.modelData.pieza, "saltar"))
            }
        }
    }

    // --- Los botones de vivo -------------------------------------------------------------
    RowLayout {
        Layout.fillWidth: true
        spacing: 6
        GlassButton {
            Layout.fillWidth: true
            text: cond.parado ? qsTr("Arrancar") : qsTr("GO")
            active: true
            activeColor: cond.parado ? Theme.accent : Theme.live
            onClicked: cond.parado ? cond.arrancar() : cond.hacer(cond.lv.go())
            ToolTip.visible: hovered; ToolTip.delay: 400
            ToolTip.text: cond.parado ? qsTr("Arranca la línea (revisa antes)") : qsTr("Entra el próximo (Espacio o F12)")
        }
        GlassButton {
            text: "HOLD"; compact: true
            enabled: !cond.parado
            active: !!cond.c.hold
            onClicked: cond.hacer(cond.lv.hold())
            ToolTip.visible: hovered; ToolTip.delay: 400
            ToolTip.text: qsTr("Que este paso no pase solo al siguiente (H). Otra vez lo suelta.")
        }
        GlassButton {
            text: qsTr("Saltar"); compact: true
            enabled: !cond.parado && !!cond.prox
            onClicked: cond.hacer(cond.lv.saltar())
            ToolTip.visible: hovered; ToolTip.delay: 400
            ToolTip.text: qsTr("Saltea el próximo (Mayús+F9)")
        }
        GlassButton {
            icon: "◀"; compact: true
            enabled: !cond.parado
            onClicked: cond.hacer(cond.lv.anterior())
            ToolTip.visible: hovered; ToolTip.delay: 400
            ToolTip.text: qsTr("El próximo, uno para atrás, sin tocar el aire (F9)")
        }
        GlassButton {
            icon: "▶"; compact: true
            enabled: !cond.parado
            onClicked: cond.hacer(cond.lv.siguiente())
            ToolTip.visible: hovered; ToolTip.delay: 400
            ToolTip.text: qsTr("El próximo, uno para adelante, sin tocar el aire (F10)")
        }
    }
    RowLayout {
        Layout.fillWidth: true
        spacing: 6
        GlassButton {
            text: qsTr("Insertar…"); compact: true
            enabled: !cond.parado && insertarMenu.count > 0
            onClicked: insertarMenu.popup()
            ToolTip.visible: hovered; ToolTip.delay: 400
            ToolTip.text: qsTr("Un paso del estante va de próximo")
            Menu {
                id: insertarMenu
                Instantiator {
                    model: cond.lv ? cond.lv.estante.filter(x => x.esPaso) : []
                    delegate: MenuItem {
                        required property var modelData
                        text: modelData.texto
                        onTriggered: cond.hacer(cond.lv.insertar(modelData.id, false))
                    }
                    onObjectAdded: (i, o) => insertarMenu.insertItem(i, o)
                    onObjectRemoved: (i, o) => insertarMenu.removeItem(o)
                }
            }
        }
        GlassButton {
            text: qsTr("Repetir"); compact: true
            enabled: !!cond.aire
            onClicked: cond.hacer(cond.lv.repetir())
            ToolTip.visible: hovered; ToolTip.delay: 400
            ToolTip.text: qsTr("Vuelve a entrar el paso al aire")
        }
        Item { Layout.fillWidth: true }
        GlassButton {
            icon: cond.ev.estado === "pausado" ? "▶" : "❚❚"; compact: true
            visible: !cond.parado
            onClicked: cond.ev.estado === "pausado" ? cond.ev.reanudar() : cond.ev.pausar()
            ToolTip.visible: hovered; ToolTip.delay: 400
            ToolTip.text: qsTr("Pausa general: nada avanza solo y no cuenta como desvío")
        }
        GlassButton {
            icon: "■"; compact: true
            visible: !cond.parado
            onClicked: cond.ev.parar()
        }
        GlassButton {
            icon: "▤"; compact: true
            visible: cond.ev.estado !== "parado"
            onClicked: cond.pedirInforme()
            ToolTip.visible: hovered; ToolTip.delay: 400
            ToolTip.text: qsTr("Ver cuánto duró de verdad cada bloque")
        }
    }

    // --- El estante (cue cart) ---------------------------------------------------------
    Text {
        visible: cond.lv && cond.lv.estante.length > 0
        text: qsTr("ESTANTE")
        color: Theme.textFaint
        font { pixelSize: 10; weight: Font.DemiBold; letterSpacing: 1 }
    }
    GridLayout {
        Layout.fillWidth: true
        visible: cond.lv && cond.lv.estante.length > 0
        columns: 4
        columnSpacing: 4
        rowSpacing: 4
        Repeater {
            model: cond.lv ? cond.lv.estante : []
            delegate: Rectangle {
                required property var modelData
                Layout.fillWidth: true
                Layout.preferredHeight: 44
                radius: 5
                color: tap.pressed ? Qt.rgba(1, 1, 1, 0.16) : Qt.rgba(1, 1, 1, 0.06)
                border.color: modelData.color !== "" ? modelData.color : Theme.colorDeTipo(modelData.tipo)
                opacity: cond.parado ? 0.5 : 1
                ColumnLayout {
                    anchors { fill: parent; margins: 4 }
                    spacing: 0
                    Text {
                        Layout.fillWidth: true
                        text: modelData.texto
                        elide: Text.ElideRight
                        horizontalAlignment: Text.AlignHCenter
                        color: Theme.text
                        font { pixelSize: 11; weight: Font.DemiBold }
                    }
                    Text {
                        Layout.fillWidth: true
                        horizontalAlignment: Text.AlignHCenter
                        text: modelData.tecla + (modelData.esPaso ? " · " + qsTr("paso") : "")
                        color: Theme.textFaint
                        font { family: Theme.mono; pixelSize: 9 }
                    }
                }
                TapHandler {
                    id: tap
                    enabled: !cond.parado
                    onTapped: cond.hacer(cond.lv.adlib(modelData.id))
                }
            }
        }
    }

    // El ensayo: la misma línea sin tocar el aire.
    RowLayout {
        Layout.fillWidth: true
        spacing: 8
        Toggle {
            checked: cond.ev.ensayo
            enabled: cond.ev.estado === "parado"
            onToggled: (w) => cond.ev.ensayo = w
        }
        Text {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            text: cond.ev.ensayo ? qsTr("Ensayo: recorre el guion sin tocar lo que sale al aire")
                                 : qsTr("Ensayo apagado: los cues salen al aire de verdad")
            color: cond.ev.ensayo ? Theme.amber : Theme.textDim
            font.pixelSize: 11
        }
    }
    // Grabar la línea (L10): el ensayo corre con todo a GO y lo que dura cada paso queda
    // para guardarlo como su duración prevista.
    RowLayout {
        Layout.fillWidth: true
        spacing: 8
        Toggle {
            checked: cond.ev.grabarTiempos
            enabled: cond.ev.estado === "parado" || cond.ev.estado === "terminado"
            onToggled: (w) => cond.ev.grabarTiempos = w
        }
        Text {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            text: cond.ev.grabarTiempos ? qsTr("Grabando: cada paso entra con GO y lo que dure queda como su duración")
                                        : qsTr("Grabar los tiempos en el ensayo")
            color: cond.ev.grabarTiempos ? Theme.amber : Theme.textDim
            font.pixelSize: 11
        }
    }
    // Lo que grabó el ensayo: una persona lo guarda o lo descarta.
    Rectangle {
        id: grab
        readonly property var g: cond.ev.tiemposGrabados || ({})
        Layout.fillWidth: true
        Layout.preferredHeight: grabCol.implicitHeight + 12
        visible: !!(g.pasos && g.pasos.length > 0)
        radius: 6
        color: Qt.rgba(1, 1, 1, 0.05)
        border.color: Theme.accent
        ColumnLayout {
            id: grabCol
            anchors { fill: parent; margins: 6 }
            spacing: 2
            Text {
                Layout.fillWidth: true
                text: qsTr("El ensayo grabó %n paso(s): de %1 a %2 en total", "", grab.g.pasos ? grab.g.pasos.length : 0)
                      .arg(Theme.mmss(grab.g.totalAntesSeg || 0)).arg(Theme.mmss(grab.g.totalGrabadoSeg || 0))
                wrapMode: Text.WordWrap
                color: Theme.text
                font { pixelSize: 12; weight: Font.DemiBold }
            }
            Repeater {
                model: grab.g.pasos || []
                delegate: Text {
                    required property var modelData
                    Layout.fillWidth: true
                    text: modelData.nombre + ": " + Theme.mmss(modelData.antesSeg) + " → " + Theme.mmss(modelData.grabadoSeg)
                    elide: Text.ElideRight
                    color: Theme.textDim
                    font { family: Theme.mono; pixelSize: 10 }
                }
            }
            RowLayout {
                Item { Layout.fillWidth: true }
                GlassButton {
                    text: qsTr("Descartar"); compact: true
                    onClicked: cond.ev.descartarTiemposGrabados()
                }
                GlassButton {
                    text: qsTr("Guardar los tiempos"); compact: true
                    active: true
                    onClicked: cond.hacer(cond.ev.aplicarTiemposGrabados())
                    ToolTip.visible: hovered; ToolTip.delay: 400
                    ToolTip.text: qsTr("Quedan como duraciones previstas (con una revisión para deshacer)")
                }
            }
        }
    }
}
