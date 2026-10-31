// La escaleta compacta de la línea de tiempo (PLAN-LINEA-DE-TIEMPO 3.5 B): los pasos de
// la salida que se mira, por bloque, con número, miniatura, nombre, cómo entra, front
// time, prevista y real, back time y los chips de sus piezas. Al aire en rojo, el
// próximo en amarillo. Con `storyboard`, solo lo que viene y con las miniaturas grandes.
//
// Solo dibuja: todo sale de Studio.linea (escaleta, storyboard), que lo calcula con el
// resolvedor. Doble clic en un paso lo pone al aire (Ir a).
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import BA

Item {
    id: esc
    property bool storyboard: false
    readonly property var lv: Studio.linea

    function colorDeEstado(e) {
        return e === "alAire" ? Qt.rgba(1, 0.23, 0.19, 0.22) : e === "proximo" ? Qt.rgba(0.96, 0.72, 0.24, 0.18)
             : e === "pasado" || e === "salteado" ? Qt.rgba(1, 1, 1, 0.02) : Qt.rgba(1, 1, 1, 0.05)
    }
    function textoEntrada(p) {
        switch (p.entra) {
        case "go": return qsTr("GO")
        case "auto": return qsTr("auto")
        case "hora": return p.entraHora || qsTr("hora")
        case "cuando": return qsTr("cuando")
        case "ancla": return qsTr("con %1").arg(p.entraDe || "")
        }
        return p.entra || ""
    }
    function irA(p) {
        if (!esc.lv) return
        const e = esc.lv.irA(p.id)
        if (e !== "") Studio.notify(e, "error")
    }

    // Una línea de encabezado con la salida (si hay varias): el filtro de la escaleta.
    RowLayout {
        id: filtro
        anchors { left: parent.left; right: parent.right; top: parent.top }
        visible: esc.lv && esc.lv.salidas.length > 1
        height: visible ? implicitHeight : 0
        spacing: 6
        Text { text: qsTr("Salida"); color: Theme.textFaint; font.pixelSize: 11 }
        Repeater {
            model: esc.lv ? esc.lv.salidas : []
            delegate: GlassButton {
                required property string modelData
                required property int index
                compact: true
                text: modelData
                active: esc.lv.salida === modelData
                onClicked: esc.lv.salida = modelData
                ToolTip.visible: hovered; ToolTip.delay: 400
                ToolTip.text: qsTr("Ver y manejar esta salida (Ctrl+%1)").arg(index + 1)
            }
        }
        Item { Layout.fillWidth: true }
    }

    // --- La lista, por bloque -----------------------------------------------------------
    ListView {
        id: lista
        visible: !esc.storyboard
        anchors { left: parent.left; right: parent.right; top: filtro.bottom; bottom: parent.bottom; topMargin: filtro.visible ? 6 : 0 }
        clip: true
        spacing: 8
        cacheBuffer: 200
        // El modelo es la CANTIDAD y cada fila lee lo suyo: la escaleta cambia cada segundo
        // en vivo (lo que lleva, lo que empuja) y un modelo nuevo rehace todas las filas
        // (las miniaturas vivas incluidas) y pierde dónde estaba el desplazamiento.
        model: esc.lv ? esc.lv.escaleta.length : 0
        ScrollBar.vertical: ScrollBar {}
        delegate: ColumnLayout {
            id: bloque
            required property int index
            readonly property var modelData: esc.lv.escaleta[index] || ({ pasos: [] })
            width: ListView.view.width
            spacing: 2

            // La cabecera del bloque: su hora, su presupuesto y el over/under.
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 28
                radius: 6
                color: Qt.rgba(1, 1, 1, 0.06)
                border.color: bloque.modelData.pendiente ? Theme.amber : "transparent"
                RowLayout {
                    anchors { fill: parent; leftMargin: 10; rightMargin: 10 }
                    spacing: 10
                    Text {
                        Layout.fillWidth: true
                        text: bloque.modelData.nombre
                        elide: Text.ElideRight
                        color: Theme.text
                        font { family: Theme.font; pixelSize: 13; weight: Font.DemiBold }
                    }
                    Text {
                        text: (bloque.modelData.desde || "") + " · " + Theme.mmss(bloque.modelData.presupuestoSeg || 0)
                        color: Theme.textDim
                        font { family: Theme.mono; pixelSize: 11 }
                    }
                    Text {
                        readonly property int ou: bloque.modelData.overUnderSeg || 0
                        visible: ou !== 0
                        text: (ou > 0 ? "+" : "") + Theme.mmss(ou)
                        color: ou > 0 ? Theme.live : Theme.ok
                        font { family: Theme.mono; pixelSize: 11; weight: Font.DemiBold }
                    }
                }
            }

            Repeater {
                model: bloque.modelData.pasos.length
                delegate: Rectangle {
                    id: fila
                    required property int index
                    readonly property var p: bloque.modelData.pasos[index] || ({})
                    Layout.fillWidth: true
                    Layout.preferredHeight: 48
                    radius: 5
                    color: esc.colorDeEstado(p.estado)
                    border.width: p.estado === "alAire" || p.estado === "proximo" || p.pendiente ? 1 : 0
                    border.color: p.estado === "alAire" ? Theme.live : p.estado === "proximo" ? Theme.amber : Theme.accentSoft
                    opacity: p.estado === "salteado" ? 0.45 : 1

                    RowLayout {
                        anchors { fill: parent; leftMargin: 6; rightMargin: 8 }
                        spacing: 8
                        Text {
                            Layout.preferredWidth: 22
                            horizontalAlignment: Text.AlignRight
                            text: fila.p.numero
                            color: Theme.textFaint
                            font { family: Theme.mono; pixelSize: 11 }
                        }
                        MiniaturaPaso {
                            Layout.preferredWidth: 64
                            Layout.preferredHeight: 36
                            mini: fila.p.mini
                        }
                        ColumnLayout {
                            Layout.fillWidth: true
                            Layout.minimumWidth: 80
                            spacing: 1
                            Text {
                                Layout.fillWidth: true
                                text: fila.p.nombre
                                elide: Text.ElideRight
                                color: fila.p.estado === "pasado" ? Theme.textDim : Theme.text
                                font { family: Theme.font; pixelSize: 13; weight: fila.p.estado === "alAire" ? Font.DemiBold : Font.Normal }
                            }
                            // Los chips de las piezas (el adlib ya disparado, tachado).
                            Row {
                                Layout.fillWidth: true
                                spacing: 4
                                clip: true
                                Repeater {
                                    model: fila.p.chips
                                    delegate: Rectangle {
                                        required property var modelData
                                        height: 15
                                        width: chipTxt.implicitWidth + 8
                                        radius: 3
                                        color: Qt.rgba(1, 1, 1, 0.06)
                                        border.width: modelData.manual ? 1 : 0
                                        border.color: Theme.colorDeTipo(modelData.tipo)
                                        Text {
                                            id: chipTxt
                                            anchors.centerIn: parent
                                            text: modelData.texto
                                            color: Theme.colorDeTipo(modelData.tipo)
                                            font { pixelSize: 9; strikeout: modelData.disparada }
                                        }
                                    }
                                }
                            }
                        }
                        // Cómo entra.
                        Rectangle {
                            Layout.preferredWidth: entraTxt.implicitWidth + 10
                            Layout.preferredHeight: 18
                            radius: 3
                            color: fila.p.entra === "go" ? Qt.rgba(1, 1, 1, 0.08) : Qt.rgba(0.96, 0.72, 0.24, 0.16)
                            Text {
                                id: entraTxt
                                anchors.centerIn: parent
                                text: esc.textoEntrada(fila.p)
                                color: fila.p.entra === "go" ? Theme.textDim : Theme.amber
                                font { family: Theme.mono; pixelSize: 10; weight: Font.DemiBold }
                            }
                        }
                        // Front, prevista / real, back.
                        Text {
                            Layout.preferredWidth: 62
                            text: fila.p.frente || ""
                            color: fila.p.tarde ? Theme.live : Theme.textDim
                            font { family: Theme.mono; pixelSize: 11; italic: fila.p.estimado }
                            ToolTip.visible: frenteHover.hovered; ToolTip.delay: 400
                            ToolTip.text: fila.p.estimado ? qsTr("Hora prevista (estimada: depende de algo sin hora)") : qsTr("Hora prevista de entrada")
                            HoverHandler { id: frenteHover }
                        }
                        Text {
                            Layout.preferredWidth: 78
                            horizontalAlignment: Text.AlignRight
                            text: fila.p.realSeg !== undefined ? Theme.mmss(fila.p.realSeg) + " / " + Theme.mmss(fila.p.previstoSeg)
                                                               : Theme.mmss(fila.p.previstoSeg || 0)
                            color: fila.p.overSeg > 0 ? Theme.live : Theme.textDim
                            font { family: Theme.mono; pixelSize: 11 }
                        }
                        Text {
                            Layout.preferredWidth: 62
                            visible: esc.width > 620
                            text: fila.p.back ? "◂ " + fila.p.back : ""
                            color: fila.p.tarde ? Theme.live : Theme.textFaint
                            font { family: Theme.mono; pixelSize: 10 }
                            ToolTip.visible: backHover.hovered && fila.p.back !== undefined; ToolTip.delay: 400
                            ToolTip.text: qsTr("Back time: lo último que puede entrar para llegar a horario")
                            HoverHandler { id: backHover }
                        }
                        Text {
                            visible: fila.p.hold
                            text: "HOLD"
                            color: Theme.live
                            font { pixelSize: 10; weight: Font.Bold }
                        }
                    }
                    TapHandler {
                        acceptedButtons: Qt.LeftButton
                        onDoubleTapped: esc.irA(fila.p)
                    }
                    ToolTip.visible: filaHover.hovered && (fila.p.nota || "") !== ""
                    ToolTip.delay: 500
                    ToolTip.text: fila.p.nota || ""
                    HoverHandler { id: filaHover }
                }
            }
        }
    }

    // --- El Storyboard: lo que viene, en grande -----------------------------------------------
    GridView {
        id: grilla
        visible: esc.storyboard
        anchors { left: parent.left; right: parent.right; top: filtro.bottom; bottom: parent.bottom; topMargin: filtro.visible ? 6 : 0 }
        clip: true
        cellWidth: Math.max(200, Math.floor(width / Math.max(1, Math.floor(width / 240))))
        cellHeight: cellWidth * 9 / 16 + 54
        cacheBuffer: 0
        model: esc.lv ? esc.lv.storyboard.length : 0 // la cantidad: ver la lista
        ScrollBar.vertical: ScrollBar {}
        delegate: Item {
            id: celda
            required property int index
            readonly property var modelData: esc.lv.storyboard[index] || ({})
            width: grilla.cellWidth
            height: grilla.cellHeight
            Rectangle {
                anchors { fill: parent; margins: 5 }
                radius: 6
                color: esc.colorDeEstado(celda.modelData.estado)
                border.width: celda.modelData.estado === "futuro" ? 0 : 2
                border.color: celda.modelData.estado === "alAire" ? Theme.live : Theme.amber
                ColumnLayout {
                    anchors { fill: parent; margins: 6 }
                    spacing: 3
                    MiniaturaPaso {
                        Layout.fillWidth: true
                        Layout.preferredHeight: width * 9 / 16
                        mini: celda.modelData.mini
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 6
                        Text {
                            text: celda.modelData.numero
                            color: Theme.textFaint
                            font { family: Theme.mono; pixelSize: 11 }
                        }
                        Text {
                            Layout.fillWidth: true
                            text: celda.modelData.nombre
                            elide: Text.ElideRight
                            color: Theme.text
                            font { family: Theme.font; pixelSize: 13; weight: Font.DemiBold }
                        }
                    }
                    Text {
                        Layout.fillWidth: true
                        text: esc.textoEntrada(celda.modelData) + " · " + (celda.modelData.frente || "") + " · " + Theme.mmss(celda.modelData.previstoSeg)
                        color: Theme.textDim
                        elide: Text.ElideRight
                        font { family: Theme.mono; pixelSize: 10 }
                    }
                }
                TapHandler { onDoubleTapped: esc.irA(celda.modelData) }
            }
        }
    }

    Text {
        anchors.centerIn: parent
        visible: esc.lv && (esc.storyboard ? esc.lv.storyboard.length === 0 : esc.lv.escaleta.length === 0)
        text: esc.storyboard ? qsTr("No viene nada más en esta salida") : qsTr("La línea no tiene pasos en esta salida")
        color: Theme.textFaint
        font.pixelSize: 13
    }
}
