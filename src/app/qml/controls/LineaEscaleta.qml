// La escaleta compacta de la línea de tiempo (PLAN-LINEA-DE-TIEMPO 3.5 B): los pasos de
// la salida que se mira, por bloque, con número, miniatura, nombre, cómo entra, front
// time, prevista y real, back time y los chips de sus piezas. Al aire en rojo, el
// próximo en amarillo. Con `storyboard`, solo lo que viene y con las miniaturas grandes.
//
// Solo dibuja: todo sale de Studio.linea (escaleta, storyboard), que lo calcula con el
// resolvedor. Doble clic en un paso lo pone al aire (Ir a). Lo que viene se arrastra
// por su agarradera (≡) a otro lugar: queda pendiente, como en la Línea, hasta
// «Confirmar». Arriba: importar y exportar la escaleta (CSV o MOS, L10).
import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
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
    function hacer(e) { if (e !== "") Studio.notify(e, "error") }
    /// Se puede arrastrar lo que viene (lo emitido y lo que está al aire, no).
    function arrastrable(p) { return p.editable !== false && (p.estado === "proximo" || p.estado === "futuro" || p.estado === "salteado") }
    /// Se suelta detrás de `q` (un paso) o, con `bloque` y sin `q`, al principio del bloque.
    function soltar(paso, q, bloque) {
        if (!esc.lv || !paso || (q && q.id === paso)) return
        esc.hacer(esc.lv.reordenar(paso, q ? q.bloque : bloque, q ? q.id : ""))
    }

    // Lo que se arrastra (una copia de la fila, que sigue al puntero).
    Rectangle {
        id: fantasma
        property string paso: ""
        property string nombre: ""
        z: 10
        visible: paso !== ""
        width: 240
        height: 30
        radius: 5
        color: Qt.rgba(0.96, 0.72, 0.24, 0.30)
        border.color: Theme.amber
        Drag.active: paso !== ""
        Drag.keys: ["paso-linea"]
        Drag.hotSpot.x: 12
        Drag.hotSpot.y: 15
        Text {
            anchors { fill: parent; leftMargin: 10 }
            verticalAlignment: Text.AlignVCenter
            text: fantasma.nombre
            elide: Text.ElideRight
            color: Theme.text
            font.pixelSize: 12
        }
    }

    FileDialog {
        id: importarDlg
        title: qsTr("Escaleta para importar (CSV o MOS)")
        nameFilters: [qsTr("Escaletas (*.csv *.xml *.mos)"), qsTr("Todos (*)")]
        onAccepted: {
            // Primero se prueba: la persona ve cuánto entra y confirma.
            const r = Studio.evento.importarEscaleta(selectedFile, true)
            if (!r.ok) { Studio.notify(r.error, "error"); return }
            const pasos = (r.diff || []).filter(d => d.tipo === "paso" && d.que === "agregado").length
            ApplicationWindow.window.confirm({
                title: qsTr("¿Importar la escaleta?"),
                text: qsTr("Se suman %n paso(s) al final de la línea.", "", pasos)
                      + ((r.avisos || []).length > 0 ? "\n\n" + r.avisos.join("\n") : ""),
                accept: qsTr("Importar"), reject: qsTr("Cancelar"),
                action: function() {
                    const r2 = Studio.evento.importarEscaleta(importarDlg.selectedFile, false)
                    if (!r2.ok) Studio.notify(r2.error, "error")
                }
            })
        }
    }
    FileDialog {
        id: exportarDlg
        property string formato: "csv"
        title: qsTr("Exportar la escaleta")
        fileMode: FileDialog.SaveFile
        nameFilters: formato === "mos" ? [qsTr("MOS (*.xml)")] : [qsTr("CSV (*.csv)")]
        onAccepted: {
            const e = Studio.evento.exportarEscaleta(formato, selectedFile)
            if (e !== "") Studio.notify(e, "error")
            else Studio.notify(qsTr("Escaleta exportada"), "info")
        }
    }

    // Una línea de encabezado con la salida (si hay varias): el filtro de la escaleta.
    RowLayout {
        id: filtro
        anchors { left: parent.left; right: parent.right; top: parent.top }
        spacing: 6
        Text { visible: esc.lv && esc.lv.salidas.length > 1; text: qsTr("Salida"); color: Theme.textFaint; font.pixelSize: 11 }
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
                visible: esc.lv.salidas.length > 1
            }
        }
        Item { Layout.fillWidth: true }
        // Lo arrastrado espera acá hasta confirmarlo (lo mismo que en la Línea).
        Text {
            visible: esc.lv && esc.lv.pendientes.length > 0
            text: esc.lv ? qsTr("%n cambio(s) sin confirmar", "", esc.lv.pendientes.length) : ""
            color: Theme.amber
            font { pixelSize: 12; weight: Font.DemiBold }
            ToolTip.visible: pendEscHover.hovered; ToolTip.delay: 300
            ToolTip.text: esc.lv ? esc.lv.pendientes.map(p => p.texto).join("\n") : ""
            HoverHandler { id: pendEscHover }
        }
        GlassButton {
            compact: true
            visible: esc.lv && esc.lv.pendientes.length > 0
            text: qsTr("Confirmar"); active: true
            onClicked: esc.hacer(esc.lv.confirmar())
            ToolTip.visible: hovered; ToolTip.delay: 400
            ToolTip.text: qsTr("Aplica los cambios a la línea (queda una revisión para deshacer)")
        }
        GlassButton {
            compact: true
            visible: esc.lv && esc.lv.pendientes.length > 0
            text: qsTr("Descartar")
            onClicked: esc.lv.descartar()
        }
        GlassButton {
            compact: true
            text: qsTr("Importar…")
            onClicked: importarDlg.open()
            ToolTip.visible: hovered; ToolTip.delay: 400
            ToolTip.text: qsTr("Trae una escaleta de una planilla (CSV) o de un sistema de redacción (MOS)")
        }
        GlassButton {
            compact: true
            text: qsTr("Exportar…")
            onClicked: exportarMenu.popup()
            Menu {
                id: exportarMenu
                MenuItem { text: qsTr("CSV (planilla)"); onTriggered: { exportarDlg.formato = "csv"; exportarDlg.open() } }
                MenuItem { text: qsTr("MOS (running order)"); onTriggered: { exportarDlg.formato = "mos"; exportarDlg.open() } }
            }
        }
    }

    // --- La lista, por bloque -----------------------------------------------------------
    ListView {
        id: lista
        visible: !esc.storyboard
        anchors { left: parent.left; right: parent.right; top: filtro.bottom; bottom: parent.bottom; topMargin: 6 }
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
                color: soltarBloque.containsDrag ? Qt.rgba(0.96, 0.72, 0.24, 0.18) : Qt.rgba(1, 1, 1, 0.06)
                border.color: bloque.modelData.pendiente ? Theme.amber : "transparent"
                // Soltado acá: al principio del bloque.
                DropArea {
                    id: soltarBloque
                    anchors.fill: parent
                    keys: ["paso-linea"]
                    onDropped: esc.soltar(fantasma.paso, null, bloque.modelData.id)
                }
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

                    // Soltado sobre esta fila: el arrastrado va detrás de ella.
                    DropArea {
                        id: soltarFila
                        anchors.fill: parent
                        keys: ["paso-linea"]
                        enabled: fila.p.estado !== "pasado"
                        onDropped: esc.soltar(fantasma.paso, fila.p, "")
                    }
                    Rectangle {
                        anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
                        height: 2
                        visible: soltarFila.containsDrag && fantasma.paso !== fila.p.id
                        color: Theme.amber
                    }
                    RowLayout {
                        anchors { fill: parent; leftMargin: 6; rightMargin: 8 }
                        spacing: 8
                        // La agarradera: arrastrar lo que viene a otro lugar (solo el futuro).
                        Text {
                            id: agarradera
                            Layout.preferredWidth: 12
                            text: "≡"
                            visible: esc.arrastrable(fila.p)
                            color: arrastre.active ? Theme.amber : Theme.textFaint
                            font.pixelSize: 14
                            DragHandler {
                                id: arrastre
                                target: null
                                // Que la lista no se lo lleve para desplazarse.
                                grabPermissions: PointerHandler.CanTakeOverFromAnything
                                onActiveChanged: {
                                    if (active) {
                                        const pt = agarradera.mapToItem(esc, 0, 0)
                                        fantasma.x = pt.x
                                        fantasma.y = pt.y
                                        fantasma.nombre = fila.p.nombre
                                        fantasma.paso = fila.p.id
                                    } else {
                                        fantasma.Drag.drop()
                                        fantasma.paso = ""
                                    }
                                }
                                onTranslationChanged: {
                                    const pt = agarradera.mapToItem(esc, 0, 0)
                                    fantasma.x = pt.x + translation.x
                                    fantasma.y = pt.y + translation.y
                                }
                            }
                            ToolTip.visible: agarraHover.hovered && !arrastre.active; ToolTip.delay: 500
                            ToolTip.text: qsTr("Arrastrá para cambiarlo de lugar (queda sin confirmar)")
                            HoverHandler { id: agarraHover }
                        }
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
        anchors { left: parent.left; right: parent.right; top: filtro.bottom; bottom: parent.bottom; topMargin: 6 }
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
