// Pantallas → Pantallas locales → «Distribuciones»: cómo quedan las ventanas en los
// monitores de esta máquina, guardado con nombre (del evento) para aplicarlo de un clic.
//
// A la izquierda la lista: aplicar, guardar lo que hay abierto ahora, armar una desde
// una plantilla y la salida de emergencia (cerrar las ventanas de la distribución).
// A la derecha el editor de la elegida: un esquema de los monitores como están puestos,
// con un rectángulo por ventana que se arrastra (y se agranda desde la esquina), y la
// lista de sus ventanas con qué muestra cada una.
//
// Todo enlazado al modelo (Studio.escritorio.distribuciones, pantallas/Distribuciones):
// la elegida es `porId[elegida]`, que cambia sola cuando cambia algo (el MCP, otro
// operador, un monitor que se desconecta). Mientras se arrastra, el rectángulo se corre
// con un desplazamiento propio y al soltar se le dice al modelo dónde quedó: la posición
// sigue enlazada al modelo, no se pisa.
//
// Aplicada, va en UNA ventana madre sin marco por monitor (VentanaLienzo.qml): su lugar se cambia acá (si
// la distribución está aplicada, el cambio se ve en el acto) o por MCP.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import BA

Panel {
    id: sec
    title: qsTr("Distribuciones")
    readonly property var d: Studio.escritorio.distribuciones
    property string elegida: ""
    readonly property var actual: sec.elegida !== "" ? (sec.d.porId[sec.elegida] || null) : null

    // Cuántas hay y en cuántos monitores está puesta la aplicada, arriba a la derecha del título.
    Text {
        parent: sec
        anchors { right: parent.right; top: parent.top; rightMargin: 18; topMargin: 16 }
        text: qsTr("%n guardada(s)", "", sec.d.cantidad) +
              (sec.d.lienzos.cantidad > 0 ? " · " + qsTr("puesta en %n monitor(es)", "", sec.d.lienzos.cantidad) : "")
        color: Theme.textDim
        font.pixelSize: 11
    }

    RowLayout {
        anchors.fill: parent
        spacing: 14

        // --- La lista -------------------------------------------------------------
        ColumnLayout {
            Layout.preferredWidth: 300
            Layout.fillHeight: true
            spacing: 8

            Flow {
                Layout.fillWidth: true
                spacing: 6
                GlassButton {
                    compact: true; text: qsTr("Guardar la actual"); icon: "⧉"
                    onClicked: { nombreDialogo.modo = "capturar"; nombreDialogo.plantilla = ""; nombreDialogo.open() }
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("Guarda las ventanas que hay abiertas ahora, dónde están y qué muestran")
                }
                GlassButton {
                    compact: true; text: qsTr("Plantilla"); icon: "▦"
                    onClicked: menuPlantillas.popup()
                    Menu {
                        id: menuPlantillas
                        ItemsDeMenu {
                            menu: menuPlantillas
                            model: sec.d.plantillas
                            delegate: MenuItem {
                                required property var modelData
                                text: modelData.titulo
                                ToolTip.visible: hovered; ToolTip.text: modelData.ayuda
                                onTriggered: { nombreDialogo.modo = "plantilla"; nombreDialogo.plantilla = modelData.id; nombreDialogo.open() }
                            }
                        }
                    }
                }
                GlassButton {
                    compact: true; text: qsTr("Vacía"); icon: "+"
                    onClicked: { nombreDialogo.modo = "nueva"; nombreDialogo.plantilla = ""; nombreDialogo.open() }
                }
            }

            ListView {
                id: lista
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                spacing: 4
                model: sec.d
                ScrollBar.vertical: ScrollBar {}
                delegate: Rectangle {
                    id: fila
                    required property string distribucionId
                    required property string nombre
                    required property string resumen
                    required property bool aplicada
                    required property bool alAbrir
                    width: lista.width
                    height: 46
                    radius: 8
                    color: sec.elegida === distribucionId ? Qt.rgba(1, 1, 1, 0.07) : Qt.rgba(1, 1, 1, 0.025)
                    border.color: sec.elegida === distribucionId ? Theme.accent : "transparent"
                    MouseArea { anchors.fill: parent; onClicked: sec.elegida = fila.distribucionId; onDoubleClicked: sec.d.aplicar(fila.distribucionId) }
                    Column {
                        anchors { left: parent.left; right: aplicarBtn.left; verticalCenter: parent.verticalCenter; margins: 10 }
                        spacing: 2
                        Text {
                            width: parent.width; elide: Text.ElideRight
                            text: (fila.aplicada ? "● " : "") + fila.nombre + (fila.alAbrir ? "  ★" : "")
                            color: fila.aplicada ? Theme.accentSoft : Theme.text
                            font { pixelSize: 13; weight: Font.DemiBold }
                        }
                        Text { text: fila.resumen; color: Theme.textFaint; font.pixelSize: 10 }
                    }
                    GlassButton {
                        id: aplicarBtn
                        anchors { right: parent.right; verticalCenter: parent.verticalCenter; rightMargin: 8 }
                        compact: true; text: qsTr("Aplicar"); active: fila.aplicada
                        onClicked: sec.d.aplicar(fila.distribucionId)
                    }
                }
                Text {
                    anchors.centerIn: parent
                    width: parent.width - 20
                    visible: lista.count === 0
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.WordWrap
                    text: qsTr("Todavía no hay distribuciones. Abrí las ventanas como las querés y «Guardar la actual», o armá una desde una plantilla.")
                    color: Theme.textFaint; font.pixelSize: 12
                }
            }

            Text {
                Layout.fillWidth: true
                visible: sec.d.aviso !== ""
                text: "⚠ " + sec.d.aviso
                color: Theme.warn; font.pixelSize: 11; wrapMode: Text.WordWrap
            }

            // La salida de emergencia: las ventanas de una distribución no se cierran con el mouse.
            GlassButton {
                Layout.fillWidth: true
                text: qsTr("Cerrar las ventanas de la distribución")
                icon: "✕"
                activeColor: Theme.warn
                active: sec.d.lienzos.cantidad > 0
                enabled: sec.d.lienzos.cantidad > 0
                onClicked: sec.d.cerrarVentanas()
                ToolTip.visible: hovered
                ToolTip.text: qsTr("También con Ctrl+Shift+F12 desde cualquier ventana de la app")
            }
        }

        // --- El editor de la elegida ------------------------------------------------
        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 8
            visible: sec.actual !== null

            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                TextField {
                    id: nombreEd
                    Layout.preferredWidth: 220
                    color: Theme.text
                    text: sec.actual ? sec.actual.nombre : ""
                    onEditingFinished: if (sec.actual && text !== sec.actual.nombre) sec.d.renombrar(sec.elegida, text)
                }
                GlassButton { compact: true; text: qsTr("Aplicar"); active: true; onClicked: sec.d.aplicar(sec.elegida) }
                GlassButton {
                    compact: true; text: qsTr("Guardar la actual acá")
                    onClicked: sec.d.capturar("", sec.elegida)
                    ToolTip.visible: hovered; ToolTip.text: qsTr("Reemplaza sus ventanas por las que hay abiertas ahora")
                }
                GlassButton { compact: true; text: qsTr("Duplicar"); onClicked: { const id = sec.d.duplicar(sec.elegida, ""); if (id !== "") sec.elegida = id } }
                GlassButton { compact: true; text: qsTr("Borrar"); onClicked: { sec.d.borrar(sec.elegida); sec.elegida = "" } }
                Item { Layout.fillWidth: true }
                Text { text: qsTr("Al abrir el evento"); color: Theme.textDim; font.pixelSize: 11 }
                Toggle {
                    checked: sec.actual ? sec.actual.alAbrir : false
                    onToggled: (quiere) => sec.d.setAlAbrir(sec.elegida, quiere)
                }
            }

            // El esquema: los monitores como están puestos y un rectángulo por ventana.
            Item {
                id: esquema
                Layout.fillWidth: true
                Layout.preferredHeight: 220
                readonly property real prop: Math.max(0.2, sec.d.proporcion)
                readonly property real aw: Math.min(width, height * prop)
                readonly property real ah: aw / prop
                readonly property real ax: (width - aw) / 2
                readonly property real ay: (height - ah) / 2
                Repeater {
                    model: sec.d.monitores
                    delegate: Rectangle {
                        required property var modelData
                        x: esquema.ax + modelData.dibujo.x * esquema.aw + 1
                        y: esquema.ay + modelData.dibujo.y * esquema.ah + 1
                        width: Math.max(8, modelData.dibujo.width * esquema.aw - 2)
                        height: Math.max(8, modelData.dibujo.height * esquema.ah - 2)
                        radius: 4
                        color: Qt.rgba(1, 1, 1, 0.04)
                        border.color: Qt.rgba(1, 1, 1, 0.22)
                        Text {
                            anchors { left: parent.left; bottom: parent.bottom; margins: 4 }
                            text: modelData.orden + (modelData.principal ? " ★" : "") + "  " + modelData.ancho + "×" + modelData.alto
                            color: Theme.textFaint; font.pixelSize: 9
                        }
                    }
                }
                Repeater {
                    model: sec.actual ? sec.actual.ventanas : []
                    delegate: Rectangle {
                        id: caja
                        required property var modelData
                        required property int index
                        readonly property var dib: modelData.dibujo || null
                        // Lo que se corre mientras se arrastra (al soltar vuelve a 0).
                        property real dx: 0
                        property real dy: 0
                        property real dw: 0
                        property real dh: 0
                        visible: dib !== null
                        x: esquema.ax + (dib ? dib.x : 0) * esquema.aw + dx
                        y: esquema.ay + (dib ? dib.y : 0) * esquema.ah + dy
                        width: Math.max(10, (dib ? dib.width : 0) * esquema.aw + dw)
                        height: Math.max(10, (dib ? dib.height : 0) * esquema.ah + dh)
                        radius: 3
                        color: modelData.faltaMonitor ? Qt.rgba(1, 0.7, 0.2, 0.25) : Qt.rgba(1, 0.42, 0.17, 0.25)
                        border.color: Theme.accent
                        border.width: 1
                        function soltar() {
                            sec.d.moverEnDibujo(sec.elegida, caja.index, (caja.x - esquema.ax) / esquema.aw,
                                                (caja.y - esquema.ay) / esquema.ah, caja.width / esquema.aw, caja.height / esquema.ah)
                            caja.dx = 0; caja.dy = 0; caja.dw = 0; caja.dh = 0
                        }
                        Text {
                            anchors { fill: parent; margins: 3 }
                            text: (caja.index + 1) + ". " + caja.modelData.titulo + (caja.modelData.pantallaCompleta ? " ⛶" : "")
                            color: Theme.text; font.pixelSize: 10; elide: Text.ElideRight; wrapMode: Text.WordWrap
                        }
                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.SizeAllCursor
                            property point inicio
                            onPressed: (m) => inicio = mapToItem(esquema, m.x, m.y)
                            onPositionChanged: (m) => {
                                const p = mapToItem(esquema, m.x, m.y)
                                caja.dx = p.x - inicio.x
                                caja.dy = p.y - inicio.y
                            }
                            onReleased: caja.soltar()
                        }
                        // La esquina de abajo a la derecha: agrandar o achicar.
                        Rectangle {
                            anchors { right: parent.right; bottom: parent.bottom }
                            width: 10; height: 10
                            color: Theme.accent
                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.SizeFDiagCursor
                                property point inicio
                                onPressed: (m) => inicio = mapToItem(esquema, m.x, m.y)
                                onPositionChanged: (m) => {
                                    const p = mapToItem(esquema, m.x, m.y)
                                    caja.dw = p.x - inicio.x
                                    caja.dh = p.y - inicio.y
                                }
                                onReleased: caja.soltar()
                            }
                        }
                    }
                }
            }

            // Sus ventanas: qué muestra cada una y cómo.
            ListView {
                id: ventanasLista
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                spacing: 4
                model: sec.actual ? sec.actual.ventanas : []
                ScrollBar.vertical: ScrollBar {}
                delegate: Rectangle {
                    id: vf
                    required property var modelData
                    required property int index
                    width: ventanasLista.width
                    height: 40
                    radius: 6
                    color: Qt.rgba(1, 1, 1, 0.025)
                    Row {
                        anchors { left: parent.left; verticalCenter: parent.verticalCenter; leftMargin: 8 }
                        spacing: 6
                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            text: (vf.index + 1) + "."
                            color: Theme.textDim; font.pixelSize: 12
                        }
                        TextField {
                            width: 120
                            placeholderText: qsTr("Nombre")
                            text: vf.modelData.nombre
                            color: Theme.text
                            onEditingFinished: if (text !== vf.modelData.nombre) sec.d.setVentana(sec.elegida, vf.index, { nombre: text })
                        }
                        ComboBox {
                            width: 220
                            model: Studio.catalogo.locales
                            textRole: "etiqueta"
                            valueRole: "itemId"
                            Component.onCompleted: currentIndex = Math.max(0, indexOfValue(vf.modelData.cosas[0]))
                            onActivated: sec.d.setVentana(sec.elegida, vf.index, { cosas: [currentValue] })
                            ToolTip.visible: hovered && vf.modelData.cosas.length > 1
                            ToolTip.text: vf.modelData.queMuestra
                        }
                        ComboBox {
                            width: 150
                            model: sec.d.monitores
                            textRole: "nombre"
                            valueRole: "id"
                            displayText: vf.modelData.faltaMonitor ? qsTr("⚠ no está") : currentText
                            Component.onCompleted: currentIndex = Math.max(0, indexOfValue(vf.modelData.enMonitor))
                            onActivated: sec.d.setVentana(sec.elegida, vf.index, { monitor: currentValue })
                        }
                        Text { anchors.verticalCenter: parent.verticalCenter; text: qsTr("Completa"); color: Theme.textDim; font.pixelSize: 11 }
                        Toggle {
                            anchors.verticalCenter: parent.verticalCenter
                            checked: vf.modelData.pantallaCompleta
                            onToggled: (quiere) => sec.d.setVentana(sec.elegida, vf.index, { pantallaCompleta: quiere })
                        }
                        Text { anchors.verticalCenter: parent.verticalCenter; text: qsTr("Encima"); color: Theme.textDim; font.pixelSize: 11 }
                        Toggle {
                            anchors.verticalCenter: parent.verticalCenter
                            checked: vf.modelData.encima
                            onToggled: (quiere) => sec.d.setVentana(sec.elegida, vf.index, { encima: quiere })
                        }
                        GlassButton {
                            anchors.verticalCenter: parent.verticalCenter
                            compact: true; text: qsTr("Quitar")
                            onClicked: sec.d.quitarVentana(sec.elegida, vf.index)
                        }
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 6
                ComboBox {
                    id: nuevaCosa
                    Layout.preferredWidth: 280
                    model: Studio.catalogo.locales
                    textRole: "etiqueta"
                    valueRole: "itemId"
                }
                GlassButton {
                    compact: true; text: qsTr("Agregar ventana"); icon: "+"
                    enabled: nuevaCosa.currentValue !== undefined && nuevaCosa.currentValue !== ""
                    onClicked: sec.d.agregarVentana(sec.elegida, { cosas: [nuevaCosa.currentValue], x: 0.25, y: 0.25, ancho: 0.5, alto: 0.5 })
                }
                Item { Layout.fillWidth: true }
                Text {
                    text: qsTr("Arrastrá los rectángulos; la esquina los agranda. Aplicada, se ve en el acto.")
                    color: Theme.textFaint; font.pixelSize: 10
                }
            }
        }

        Text {
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: sec.actual === null
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            wrapMode: Text.WordWrap
            text: qsTr("Elegí una distribución para verla y editarla.")
            color: Theme.textFaint; font.pixelSize: 12
        }
    }

    Dialog {
        id: nombreDialogo
        parent: Overlay.overlay
        anchors.centerIn: parent
        modal: true
        title: modo === "capturar" ? qsTr("Guardar las ventanas de ahora") : qsTr("Distribución nueva")
        standardButtons: Dialog.Ok | Dialog.Cancel
        property string modo: "nueva"
        property string plantilla: ""
        onOpened: { nombreNuevo.text = ""; nombreNuevo.forceActiveFocus() }
        onAccepted: {
            const n = nombreNuevo.text
            const id = modo === "capturar" ? sec.d.capturar(n, "")
                     : modo === "plantilla" ? sec.d.crearDePlantilla(plantilla, n, "", [])
                     : sec.d.crear(n, [])
            if (id !== "") sec.elegida = id
        }
        ColumnLayout {
            spacing: 8
            Text {
                Layout.preferredWidth: 340
                wrapMode: Text.WordWrap
                color: Theme.textDim; font.pixelSize: 11
                text: nombreDialogo.modo === "plantilla"
                      ? qsTr("Vacío = el nombre de la plantilla. Después se edita como cualquier otra.")
                      : qsTr("Queda guardada con el evento. Aplicada, cada monitor lleva una sola ventana sin marco con sus ventanas adentro.")
            }
            TextField { id: nombreNuevo; Layout.preferredWidth: 340; placeholderText: qsTr("Remotos en la sala"); color: Theme.text
                        onAccepted: nombreDialogo.accept() }
        }
    }
}
