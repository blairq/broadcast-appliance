// La Línea multipista (PLAN-LINEA-DE-TIEMPO 3.5 A): arriba la regla en la hora del
// evento con los hard starts (bandera, gap u overlap) y el fin planeado; la línea de
// «ahora» fija a un tercio; los carriles agrupados por salida (el de escena con las cajas
// de los pasos y su miniatura; gráficos, DSK, audio y medios con sus piezas) y las
// globales abajo, colapsables. Lo emitido es sólido con el fantasma de lo previsto y el
// «+0:42»; las manuales son botoncitos de su caja; los conflictos, en rojo.
//
// Zoom de 10 s a 4 h: Ctrl+rueda, o 1–4 (el bloque, el evento, 5 min, 1 min). La rueda
// sola corre la ventana; «Ahora» vuelve a seguirlo.
//
// Editar es arrastrar SOLO el futuro: el cuerpo de una caja la cambia de lugar, su borde
// derecho le cambia el largo, una pieza se corre dentro de su paso. Nada sale al aire
// hasta «Confirmar» (los cambios quedan pendientes y se ven como quedarían). Lo emitido
// no se arrastra y dice por qué.
//
// Solo dibuja Studio.linea.multipista: las posiciones vienen como fracción del ancho
// (0 = el borde izquierdo) y lo que se arrastra vuelve como fracción.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import BA

Item {
    id: mp
    readonly property var lv: Studio.linea
    readonly property var d: lv ? lv.multipista : ({})
    property bool activa: visible
    property bool globalesAbiertas: true
    readonly property int etiquetas: 92 // la columna con el nombre de cada carril

    function hacer(error) { if (error !== "") Studio.notify(error, "error") }

    Shortcut { sequence: "1"; enabled: mp.activa; onActivated: mp.lv.zoom(1) }
    Shortcut { sequence: "2"; enabled: mp.activa; onActivated: mp.lv.zoom(2) }
    Shortcut { sequence: "3"; enabled: mp.activa; onActivated: mp.lv.zoom(3) }
    Shortcut { sequence: "4"; enabled: mp.activa; onActivated: mp.lv.zoom(4) }

    ColumnLayout {
        anchors.fill: parent
        spacing: 6

        // --- Zoom, «ahora» y lo pendiente ----------------------------------------------------
        RowLayout {
            Layout.fillWidth: true
            spacing: 6
            Repeater {
                model: [qsTr("Bloque"), qsTr("Evento"), qsTr("5 min"), qsTr("1 min")]
                delegate: GlassButton {
                    required property string modelData
                    required property int index
                    compact: true
                    text: (index + 1) + " · " + modelData
                    onClicked: mp.lv.zoom(index + 1)
                }
            }
            Text {
                text: qsTr("ancho: %1").arg(Theme.mmss(mp.lv ? mp.lv.zoomMs / 1000 : 0))
                color: Theme.textFaint
                font { family: Theme.mono; pixelSize: 10 }
            }
            GlassButton {
                compact: true
                visible: mp.lv && !mp.lv.siguiendoAhora
                text: qsTr("Ahora")
                onClicked: mp.lv.alAhora()
            }
            Item { Layout.fillWidth: true }
            // Silent Edition: lo arrastrado espera acá hasta confirmarlo.
            Text {
                visible: mp.lv && mp.lv.pendientes.length > 0
                text: mp.lv ? qsTr("%n cambio(s) sin confirmar", "", mp.lv.pendientes.length) : ""
                color: Theme.amber
                font { pixelSize: 12; weight: Font.DemiBold }
                ToolTip.visible: pendHover.hovered; ToolTip.delay: 300
                ToolTip.text: mp.lv ? mp.lv.pendientes.map(p => p.texto).join("\n") : ""
                HoverHandler { id: pendHover }
            }
            GlassButton {
                compact: true
                visible: mp.lv && mp.lv.pendientes.length > 0
                text: qsTr("Confirmar"); active: true
                onClicked: mp.hacer(mp.lv.confirmar())
                ToolTip.visible: hovered; ToolTip.delay: 400
                ToolTip.text: qsTr("Aplica los cambios a la línea (queda una revisión para deshacer)")
            }
            GlassButton {
                compact: true
                visible: mp.lv && mp.lv.pendientes.length > 0
                text: qsTr("Descartar")
                onClicked: mp.lv.descartar()
            }
        }

        // --- La regla ---------------------------------------------------------------------
        Item {
            id: regla
            Layout.fillWidth: true
            Layout.preferredHeight: 38
            Item {
                id: reglaArea
                anchors { fill: parent; leftMargin: mp.etiquetas }
                clip: true
                Repeater {
                    model: mp.d.regla || []
                    delegate: Item {
                        required property var modelData
                        x: modelData.x * reglaArea.width
                        height: reglaArea.height
                        Rectangle {
                            anchors.bottom: parent.bottom
                            width: 1
                            height: modelData.mayor ? 10 : 5
                            color: Theme.textFaint
                        }
                        Text {
                            visible: !!modelData.texto
                            anchors { bottom: parent.bottom; bottomMargin: 11; horizontalCenter: parent.left }
                            text: modelData.texto || ""
                            color: Theme.textDim
                            font { family: Theme.mono; pixelSize: 10 }
                        }
                    }
                }
                // Los hard starts: bandera con su hora y el gap (verde), justo (ámbar) u overlap (rojo).
                Repeater {
                    model: mp.d.hardStarts || []
                    delegate: Rectangle {
                        required property var modelData
                        readonly property color tinta: modelData.nivel === "overlap" ? Theme.live : modelData.nivel === "justo" ? Theme.amber : Theme.ok
                        x: modelData.x * reglaArea.width
                        y: 0
                        width: banderaTxt.implicitWidth + 8
                        height: 15
                        color: Qt.rgba(tinta.r, tinta.g, tinta.b, 0.25)
                        border.color: tinta
                        Text {
                            id: banderaTxt
                            anchors.centerIn: parent
                            text: "⚑ " + modelData.hora + " " + (modelData.gapSeg >= 0 ? "+" : "") + Theme.mmss(modelData.gapSeg)
                            color: parent.tinta
                            font { family: Theme.mono; pixelSize: 9; weight: Font.DemiBold }
                        }
                        ToolTip.visible: hsHover.hovered; ToolTip.delay: 300
                        ToolTip.text: qsTr("«%1» entra a las %2 (%3): %4").arg(modelData.nombre).arg(modelData.hora).arg(modelData.politica)
                                      .arg(modelData.gapSeg >= 0 ? qsTr("sobran %1").arg(Theme.mmss(modelData.gapSeg))
                                                                 : qsTr("faltan %1").arg(Theme.mmss(-modelData.gapSeg)))
                        HoverHandler { id: hsHover }
                    }
                }
            }
            // La rueda sobre la regla corre la ventana; con Ctrl, el zoom.
            WheelHandler {
                onWheel: (ev) => mp.rueda(ev)
            }
        }

        // --- Los carriles -------------------------------------------------------------------
        Flickable {
            id: carriles
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            contentWidth: width
            contentHeight: columna.implicitHeight
            boundsBehavior: Flickable.StopAtBounds
            interactive: false // el arrastre es para editar; se desplaza con la barra
            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded; interactive: true }
            WheelHandler {
                onWheel: (ev) => {
                    if ((ev.modifiers & Qt.ControlModifier) || Math.abs(ev.angleDelta.x) > Math.abs(ev.angleDelta.y) || (ev.modifiers & Qt.ShiftModifier))
                        mp.rueda(ev)
                    else
                        carriles.contentY = Math.max(0, Math.min(carriles.contentHeight - carriles.height, carriles.contentY - ev.angleDelta.y / 2))
                }
            }

            Column {
                id: columna
                width: carriles.width
                spacing: 4

                // Los modelos son CANTIDADES y cada uno lee lo suyo: la línea cambia cada
                // segundo («ahora» avanza) y un modelo nuevo rehace todas las cajas (con sus
                // miniaturas vivas).
                Repeater {
                    model: (mp.d.salidas || []).length
                    delegate: Column {
                        id: grupo
                        required property int index
                        readonly property var modelData: mp.d.salidas[index] || ({ carriles: [] })
                        width: columna.width
                        spacing: 2
                        Text {
                            visible: (mp.d.salidas || []).length > 1
                            text: qsTr("Salida «%1»").arg(grupo.modelData.salida)
                            color: Theme.textDim
                            font { pixelSize: 11; weight: Font.DemiBold }
                        }
                        Repeater {
                            model: grupo.modelData.carriles.length
                            delegate: Carril {
                                required property int index
                                raiz: mp
                                ancho: columna.width
                                carril: grupo.modelData.carriles[index] || ({})
                            }
                        }
                    }
                }

                // Las globales (asamblea, sala, prompter, PTZ, asistente, notas), colapsables.
                Rectangle {
                    visible: (mp.d.globales || []).length > 0
                    width: columna.width
                    height: 20
                    color: "transparent"
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: (mp.globalesAbiertas ? "▾ " : "▸ ") + qsTr("Globales")
                        color: Theme.textDim
                        font { pixelSize: 11; weight: Font.DemiBold }
                    }
                    TapHandler { onTapped: mp.globalesAbiertas = !mp.globalesAbiertas }
                }
                Repeater {
                    model: mp.globalesAbiertas ? (mp.d.globales || []).length : 0
                    delegate: Carril {
                        required property int index
                        raiz: mp
                        ancho: columna.width
                        carril: mp.d.globales[index] || ({})
                    }
                }
            }
        }

        LineaBarra { Layout.fillWidth: true }
    }

    // La línea de «ahora» y el fin planeado, sobre todos los carriles.
    Rectangle {
        visible: mp.d.ahora !== undefined && mp.d.ahora >= 0 && mp.d.ahora <= 1
        x: regla.x + mp.etiquetas + (mp.d.ahora || 0) * reglaArea.width
        y: regla.y + 30
        width: 2
        height: carriles.y + carriles.height - y
        color: Theme.live
    }
    Rectangle {
        visible: mp.d.finPlaneado !== undefined
        x: regla.x + mp.etiquetas + (mp.d.finPlaneado || 0) * reglaArea.width
        y: regla.y + 16
        width: 1
        height: carriles.y + carriles.height - y
        color: Theme.textDim
        opacity: 0.6
    }

    function rueda(ev) {
        const delta = Math.abs(ev.angleDelta.y) >= Math.abs(ev.angleDelta.x) ? ev.angleDelta.y : ev.angleDelta.x
        if (ev.modifiers & Qt.ControlModifier) mp.lv.acercar(delta > 0 ? 0.8 : 1.25)
        else mp.lv.correr(delta > 0 ? -0.1 : 0.1)
        ev.accepted = true
    }

    // --- Un carril -----------------------------------------------------------------------------
    component Carril: Item {
        id: c
        /// La raíz (para el view model y la columna de nombres) y el ancho: un componente
        /// en línea no ve los ids del archivo.
        property var raiz
        property real ancho
        property var carril: ({})
        readonly property bool deEscena: !!carril.escena
        width: c.ancho
        height: deEscena ? 70 : 22

        Text {
            width: c.raiz.etiquetas - 6
            anchors.verticalCenter: parent.verticalCenter
            text: c.carril.nombre || ""
            elide: Text.ElideRight
            color: c.carril.color ? c.carril.color : Theme.colorDeTipo(c.carril.tipo)
            font { pixelSize: 10; weight: Font.DemiBold }
        }
        Rectangle {
            id: area
            anchors { fill: parent; leftMargin: c.raiz.etiquetas }
            color: Qt.rgba(1, 1, 1, 0.03)
            radius: 3
            clip: true
            Repeater {
                model: (c.carril.items || []).length
                delegate: Loader {
                    required property int index
                    sourceComponent: c.deEscena ? cajaPaso : bloquePieza
                    property var dato: c.carril.items[index] || ({})
                }
            }
        }

        // La caja de un paso: miniatura, nombre, entrada, prevista y real; el fantasma de
        // lo previsto debajo de lo emitido; los adlibs como botoncitos a la derecha.
        Component {
            id: cajaPaso
            Item {
                id: caja
                readonly property var p: parent && parent.dato ? parent.dato : ({})
                readonly property real x0: p.x0 * area.width
                readonly property real ancho: Math.max(3, (p.x1 - p.x0) * area.width)
                readonly property bool arrastrable: p.editable && !p.emitido
                x: x0 + (cuerpo.active ? cuerpo.activeTranslation.x : 0)
                width: ancho + (borde.active ? borde.activeTranslation.x : 0)
                height: area.height

                // El fantasma: el largo previsto, abajo, para comparar con lo real.
                Rectangle {
                    visible: caja.p.xPrevisto !== undefined
                    x: 0
                    y: parent.height - 6
                    width: Math.max(2, ((caja.p.xPrevisto || 0) - caja.p.x0) * area.width)
                    height: 4
                    radius: 2
                    color: "transparent"
                    border.color: Theme.textDim
                    opacity: 0.8
                }
                Rectangle {
                    anchors { fill: parent; bottomMargin: 8 }
                    radius: 4
                    color: caja.p.estado === "alAire" ? Qt.rgba(1, 0.23, 0.19, 0.30)
                         : caja.p.estado === "proximo" ? Qt.rgba(0.96, 0.72, 0.24, 0.22)
                         : caja.p.emitido ? Qt.rgba(1, 1, 1, 0.14) : Qt.rgba(1, 1, 1, 0.07)
                    border.width: caja.p.conflicto || caja.p.pendiente || caja.p.estado === "alAire" || caja.p.estado === "proximo" ? 1.5 : 1
                    border.color: caja.p.conflicto ? Theme.live : caja.p.pendiente ? Theme.accentSoft
                                : caja.p.estado === "alAire" ? Theme.live : caja.p.estado === "proximo" ? Theme.amber : Qt.rgba(1, 1, 1, 0.18)
                    clip: true
                    RowLayout {
                        anchors { fill: parent; margins: 3 }
                        spacing: 4
                        MiniaturaPaso {
                            visible: caja.width > 90
                            Layout.preferredWidth: 64
                            Layout.preferredHeight: 36
                            mini: caja.p.mini
                        }
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 0
                            Text {
                                Layout.fillWidth: true
                                text: caja.p.nombre
                                elide: Text.ElideRight
                                color: Theme.text
                                font { pixelSize: 11; weight: Font.DemiBold }
                            }
                            Text {
                                Layout.fillWidth: true
                                elide: Text.ElideRight
                                text: (caja.p.entra === "hora" ? "⚑ " + (caja.p.entraHora || "") : caja.p.entra === "go" ? "GO" : caja.p.entra)
                                      + " · " + (caja.p.realSeg !== undefined ? Theme.mmss(caja.p.realSeg) + "/" : "") + Theme.mmss(caja.p.previstoSeg)
                                color: caja.p.entra === "go" ? Theme.textDim : Theme.amber
                                font { family: Theme.mono; pixelSize: 9 }
                            }
                            Text {
                                visible: !!caja.p.over
                                text: caja.p.over || ""
                                color: Theme.live
                                font { family: Theme.mono; pixelSize: 11; weight: Font.Bold }
                            }
                        }
                        // Los adlibs del paso (manuales): se disparan con el paso al aire.
                        Column {
                            spacing: 2
                            visible: caja.width > 140
                            Repeater {
                                model: caja.p.manuales || []
                                delegate: Rectangle {
                                    required property var modelData
                                    width: 46; height: 13; radius: 3
                                    color: modelData.disparada ? Qt.rgba(1, 1, 1, 0.05) : Qt.rgba(1, 1, 1, 0.12)
                                    border.color: Theme.colorDeTipo(modelData.tipo)
                                    Text {
                                        anchors.centerIn: parent
                                        width: parent.width - 4
                                        text: modelData.texto
                                        elide: Text.ElideRight
                                        horizontalAlignment: Text.AlignHCenter
                                        color: Theme.text
                                        font.pixelSize: 8
                                    }
                                    TapHandler {
                                        enabled: caja.p.estado === "alAire" && !modelData.disparada
                                        onTapped: c.raiz.hacer(c.raiz.lv.adlib(modelData.id))
                                    }
                                }
                            }
                        }
                    }
                }
                // Arrastrar el cuerpo: cambia de lugar (detrás del que arranca antes de donde se suelta).
                DragHandler {
                    id: cuerpo
                    enabled: caja.arrastrable
                    target: null
                    xAxis.enabled: true
                    yAxis.enabled: false
                    onActiveChanged: if (!active) c.raiz.hacer(c.raiz.lv.mover(caja.p.paso, (caja.x0 + activeTranslation.x) / area.width))
                }
                // El borde derecho: el largo.
                Rectangle {
                    anchors { right: parent.right; top: parent.top; bottom: parent.bottom; bottomMargin: 8 }
                    width: 6
                    visible: caja.arrastrable
                    color: bordeHover.hovered || borde.active ? Qt.rgba(1, 1, 1, 0.3) : "transparent"
                    HoverHandler { id: bordeHover; cursorShape: Qt.SizeHorCursor }
                    DragHandler {
                        id: borde
                        target: null
                        yAxis.enabled: false
                        onActiveChanged: if (!active) c.raiz.hacer(c.raiz.lv.estirar(caja.p.paso, activeTranslation.x / area.width))
                    }
                }
                HoverHandler { id: cajaHover; cursorShape: caja.arrastrable ? Qt.OpenHandCursor : Qt.ArrowCursor }
                ToolTip.visible: cajaHover.hovered && !cuerpo.active
                ToolTip.delay: 500
                ToolTip.text: caja.p.nombre + " · " + (caja.p.frente || "") + (caja.p.escena ? " · " + caja.p.escena : "")
                              + (caja.p.porque ? "\n" + caja.p.porque : "")
                              + (caja.p.conflicto ? "\n" + qsTr("Conflicto con otra salida (cámara o gráfico compartido)") : "")
                TapHandler { onDoubleTapped: c.raiz.hacer(c.raiz.lv.irA(caja.p.paso)) }
            }
        }

        // Una pieza en su carril: el color de su tipo; con borde claro si vive más allá de
        // su paso («sigue»), rojo si choca con otra salida, punteada si es un pronóstico.
        Component {
            id: bloquePieza
            Rectangle {
                id: bp
                readonly property var z: parent && parent.dato ? parent.dato : ({})
                readonly property real x0: z.x0 * area.width
                readonly property bool arrastrable: z.editable && !z.emitida
                x: x0 + (mueve.active ? mueve.activeTranslation.x : 0)
                y: 3
                width: Math.max(3, (z.x1 - z.x0) * area.width)
                height: area.height - 6
                radius: 3
                readonly property color tinta: Theme.colorDeTipo(z.tipo)
                color: Qt.rgba(tinta.r, tinta.g, tinta.b, z.emitida ? 0.55 : 0.30)
                border.width: z.conflicto || z.cruza || z.pendiente ? 1.5 : 0
                border.color: z.conflicto ? Theme.live : z.pendiente ? Theme.accentSoft : Theme.text
                opacity: z.estimado ? 0.7 : 1
                Text {
                    anchors { fill: parent; leftMargin: 4; rightMargin: 4 }
                    verticalAlignment: Text.AlignVCenter
                    text: bp.z.texto + (bp.z.abierta ? " ▸" : "")
                    elide: Text.ElideRight
                    color: Theme.text
                    font.pixelSize: 9
                }
                DragHandler {
                    id: mueve
                    enabled: bp.arrastrable
                    target: null
                    yAxis.enabled: false
                    onActiveChanged: if (!active) c.raiz.hacer(c.raiz.lv.moverPieza(bp.z.id, activeTranslation.x / area.width))
                }
                HoverHandler { id: bpHover }
                ToolTip.visible: bpHover.hovered && !mueve.active
                ToolTip.delay: 500
                ToolTip.text: bp.z.texto + " · " + bp.z.tipo + " · " + qsTr("vida: %1").arg(bp.z.vida)
                              + (bp.z.porque ? "\n" + bp.z.porque : "")
                              + (bp.z.conflicto ? "\n" + qsTr("Conflicto con otra salida") : "")
            }
        }
    }
}
