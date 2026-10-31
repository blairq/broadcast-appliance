// Página «Salidas» (id «salidas»; docs/planes/PLAN-SALIDAS.md, etapa 5): las salidas
// del aparato. Cada una es un operador completo —su programa y su preview, su TAKE,
// su mezcla, sus destinos y su grabación—; lo que comparten son las entradas y las
// escenas.
//
// Acá se crean (hasta el tope, mirando el margen de la máquina: lo mismo que dice
// sistema_recursos), se les pone nombre y color (el del tally, el multiview y las
// superficies), se elige cuál maneja esta interfaz, se ven sus destinos y su
// grabación, y se borran. Todo sale de Studio.salidas (el modelo avisa); esta vista
// solo muestra y pide.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import BA
import "../controls"

Rectangle {
    id: page
    color: Theme.bg
    property real leftInset: 92
    /// Los colores para elegir (el de fábrica de cada salida es uno de estos).
    readonly property var paleta: ["#e53935", "#1e88e5", "#43a047", "#fdd835", "#8e24aa", "#fb8c00", "#00acc1", "#d81b60"]
    readonly property var nueva: Studio.nuevaSalida
    readonly property var rec: Studio.recursos

    // Elegir un color: una fila de puntos; el elegido, con borde.
    component Paleta: Row {
        id: pal
        property string elegido: ""
        signal elegir(string color)
        spacing: 6
        Repeater {
            model: page.paleta
            delegate: Rectangle {
                required property string modelData
                width: 22; height: 22; radius: 11
                color: modelData
                border { color: Theme.text; width: pal.elegido.toLowerCase() === modelData ? 2 : 0 }
                MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: pal.elegir(parent.modelData) }
            }
        }
    }

    DialogoDestino { id: nuevoDestino }

    ColumnLayout {
        anchors { fill: parent; leftMargin: page.leftInset; margins: 20 }
        spacing: 16

        // --- Una salida nueva ------------------------------------------------------
        Panel {
            Layout.fillWidth: true
            Layout.preferredHeight: 168
            title: qsTr("Salidas")
            ColumnLayout {
                anchors.fill: parent
                spacing: 8
                Text {
                    Layout.fillWidth: true; wrapMode: Text.WordWrap
                    text: qsTr("Cada salida es un operador completo: su programa y su preview, su TAKE, su mezcla, sus destinos y su grabación. Las entradas y las escenas son de todas.")
                    color: Theme.textDim; font.pixelSize: 12
                }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 10
                    TextField {
                        id: nombreNueva
                        Layout.preferredWidth: 240
                        placeholderText: qsTr("Nombre (p. ej. «Señal para la TV»)")
                        onAccepted: crearBtn.crear()
                    }
                    Paleta { id: colorNueva; elegido: ""; onElegir: (c) => colorNueva.elegido = c }
                    GlassButton {
                        id: crearBtn
                        active: true; text: qsTr("Crear salida")
                        enabled: page.nueva.puede === true && nombreNueva.text.trim() !== ""
                        function crear() {
                            if (!enabled) return
                            const e = Studio.crearSalidaUi(nombreNueva.text, colorNueva.elegido)
                            if (e !== "") { Studio.notify(e, "error"); return }
                            nombreNueva.text = ""; colorNueva.elegido = ""
                        }
                        onClicked: crear()
                    }
                    Item { Layout.fillWidth: true }
                    Text { text: qsTr("Tope"); color: Theme.textDim; font.pixelSize: 12 }
                    SpinBox {
                        from: 1; to: 16; value: Studio.topeSalidas
                        onValueModified: { const e = Studio.setTopeSalidasUi(value); if (e !== "") { Studio.notify(e, "error"); value = Studio.topeSalidas } }
                        ToolTip.visible: hovered; ToolTip.delay: 400
                        ToolTip.text: qsTr("Cuántas salidas se pueden tener a la vez (cada una es un programa, un preview y una mezcla más)")
                    }
                }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8
                    // El margen de la máquina (la tarjeta Sistema, sistema_recursos).
                    Rectangle {
                        width: 9; height: 9; radius: 5
                        color: !page.rec ? Theme.textFaint : page.rec.estado === "critico" ? Theme.live
                             : page.rec.estado === "aviso" ? Theme.amber : page.rec.estado === "normal" ? Theme.ok : Theme.textFaint
                    }
                    Text {
                        Layout.fillWidth: true; elide: Text.ElideRight
                        text: page.nueva.aviso
                              ? page.nueva.aviso
                              : (page.rec && page.rec.margen >= 0 ? qsTr("La máquina tiene margen: le queda %1 % a lo más cargado (%2).").arg(Math.round(page.rec.margen)).arg(page.rec.limita)
                                                                  : qsTr("Todavía no se sabe cuánto margen tiene la máquina."))
                        color: page.nueva.puede === false ? Theme.live : page.nueva.aviso ? Theme.amber : Theme.textFaint
                        font.pixelSize: 11
                    }
                    Text { text: qsTr("Codificador: %1 (el mismo para todas)").arg(Studio.encoder || "—"); color: Theme.textFaint; font.pixelSize: 11 }
                }
            }
        }

        // --- Las salidas -----------------------------------------------------------
        ScrollView {
            id: lista
            Layout.fillWidth: true
            Layout.fillHeight: true
            contentWidth: availableWidth
            clip: true
            Flow {
                width: lista.availableWidth
                spacing: 16
                Repeater {
                    model: Studio.salidas
                    delegate: Rectangle {
                        id: tarjeta
                        // Por `model`: los roles «id» y «color» chocan con los del Rectangle.
                        required property var model
                        readonly property string salida: model.id
                        readonly property bool porOmision: !!model.porOmision
                        readonly property bool elegida: salida === Studio.salidaActiva
                        readonly property var destinos: model.destinos || []
                        width: Math.max(360, Math.min(520, (lista.availableWidth - 16) / 2))
                        height: cuerpo.implicitHeight + 32
                        radius: Theme.radius
                        color: Qt.rgba(1, 1, 1, 0.04)
                        border { color: tarjeta.elegida ? tarjeta.model.color : Theme.glassBorder; width: tarjeta.elegida ? 2 : 1 }

                        ColumnLayout {
                            id: cuerpo
                            anchors { left: parent.left; right: parent.right; top: parent.top; margins: 16 }
                            spacing: 8
                            // Nombre (se edita en el lugar) y si es la que maneja esta interfaz.
                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 8
                                Rectangle { width: 14; height: 14; radius: 7; color: tarjeta.model.color }
                                TextField {
                                    Layout.fillWidth: true
                                    text: tarjeta.model.nombre
                                    font { pixelSize: 15; weight: Font.DemiBold }
                                    onEditingFinished: if (text.trim() !== "" && text !== tarjeta.model.nombre) {
                                        const e = Studio.editarSalida(tarjeta.salida, text.trim(), "")
                                        if (e !== "") Studio.notify(e, "error")
                                    }
                                }
                                Text { visible: tarjeta.porOmision; text: qsTr("por omisión"); color: Theme.textFaint; font.pixelSize: 11 }
                                GlassButton {
                                    compact: true
                                    text: tarjeta.elegida ? qsTr("La manejás vos") : qsTr("Manejar esta")
                                    active: tarjeta.elegida; activeColor: tarjeta.model.color
                                    onClicked: Studio.salidaActiva = tarjeta.salida
                                    ToolTip.visible: hovered; ToolTip.delay: 400
                                    ToolTip.text: qsTr("Dirección, Escenas y el Mezclador pasan a mostrar y manejar esta salida")
                                }
                            }
                            Paleta {
                                elegido: tarjeta.model.color
                                onElegir: (c) => { const e = Studio.editarSalida(tarjeta.salida, "", c); if (e !== "") Studio.notify(e, "error") }
                            }
                            // Su programa, en vivo (mientras se la mira).
                            Rectangle {
                                Layout.fillWidth: true
                                Layout.preferredHeight: width * 9 / 16
                                color: "black"; radius: 6; clip: true
                                VideoItem { anchors.fill: parent; source: tarjeta.model.programa; fillMode: VideoItem.Fit }
                            }
                            GridLayout {
                                Layout.fillWidth: true
                                columns: 2; columnSpacing: 10; rowSpacing: 2
                                Text { text: qsTr("AL AIRE"); color: Theme.textFaint; font { pixelSize: 10; weight: Font.DemiBold; letterSpacing: 1 } }
                                Text { Layout.fillWidth: true; elide: Text.ElideRight; text: tarjeta.model.alAire || "—"; color: Theme.text; font.pixelSize: 13 }
                                Text { text: qsTr("ARMADA"); color: Theme.textFaint; font { pixelSize: 10; weight: Font.DemiBold; letterSpacing: 1 } }
                                Text { Layout.fillWidth: true; elide: Text.ElideRight; text: tarjeta.model.armada || qsTr("nada"); color: Theme.textDim; font.pixelSize: 13 }
                            }
                            // Grabación de su programa.
                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 8
                                Rectangle { width: 9; height: 9; radius: 5; color: tarjeta.model.grabando ? Theme.live : Qt.rgba(1, 1, 1, 0.2) }
                                Text {
                                    Layout.fillWidth: true; elide: Text.ElideLeft
                                    text: tarjeta.model.grabando ? qsTr("Grabando · %1").arg(tarjeta.model.archivo || "")
                                                                 : qsTr("Sin grabar")
                                    color: tarjeta.model.grabando ? Theme.text : Theme.textFaint; font.pixelSize: 12
                                }
                                GlassButton {
                                    compact: true
                                    text: tarjeta.model.grabando ? qsTr("Detener") : qsTr("Grabar")
                                    active: !!tarjeta.model.grabando; activeColor: Theme.live
                                    onClicked: {
                                        if (!tarjeta.model.grabando) { const e = Studio.grabarSalida(tarjeta.salida, true); if (e !== "") Studio.notify(e, "error"); return }
                                        const id = tarjeta.salida, n = tarjeta.model.nombre
                                        ApplicationWindow.window.confirm({ title: qsTr("¿Detener la grabación de «%1»?").arg(n),
                                                                           text: qsTr("El archivo se cierra y lo que siga no queda grabado."),
                                                                           accept: qsTr("Detener"), reject: qsTr("Seguir grabando"),
                                                                           action: function() { Studio.grabarSalida(id, false) } })
                                    }
                                }
                            }
                            // Sus destinos (salen con su programa).
                            Text { text: qsTr("Destinos"); color: Theme.textDim; font { pixelSize: 12; weight: Font.DemiBold } }
                            Text {
                                visible: tarjeta.destinos.length === 0
                                Layout.fillWidth: true; wrapMode: Text.WordWrap
                                text: tarjeta.porOmision ? qsTr("Ninguno además de la transmisión principal.") : qsTr("Ninguno todavía: agregá uno para que esta salida transmita.")
                                color: Theme.textFaint; font.pixelSize: 11
                            }
                            Repeater {
                                model: tarjeta.destinos
                                delegate: RowLayout {
                                    required property var modelData
                                    Layout.fillWidth: true
                                    spacing: 6
                                    Rectangle { width: 8; height: 8; radius: 4
                                                color: modelData.activo ? (modelData.reconectando ? Theme.amber : Theme.live) : Theme.textFaint }
                                    Text { Layout.fillWidth: true; elide: Text.ElideRight
                                           text: modelData.respaldo ? qsTr("%1 · respaldo").arg(modelData.nombre) : modelData.nombre
                                           color: Theme.text; font.pixelSize: 12 }
                                    GlassButton {
                                        compact: true; text: modelData.activo ? qsTr("Cortar") : qsTr("Iniciar")
                                        active: modelData.activo; activeColor: Theme.live
                                        onClicked: {
                                            if (!modelData.activo) { const e = Studio.iniciarDestinoUi(modelData.id); if (e !== "") Studio.notify(e, "error"); return }
                                            const id = modelData.id, n = modelData.nombre
                                            ApplicationWindow.window.confirm({ title: qsTr("¿Cortar «%1»?").arg(n), text: qsTr("Los demás destinos siguen."),
                                                                               accept: qsTr("Cortar"), reject: qsTr("Cancelar"), action: function() { Studio.detenerDestinoUi(id) } })
                                        }
                                    }
                                    GlassButton { compact: true; text: "✕"; visible: !modelData.activo
                                                  onClicked: { const e = Studio.borrarDestinoUi(modelData.id); if (e !== "") Studio.notify(e, "error") } }
                                }
                            }
                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 6
                                GlassButton { compact: true; text: qsTr("+ Destino…"); onClicked: { nuevoDestino.salida = tarjeta.salida; nuevoDestino.open() } }
                                Item { Layout.fillWidth: true }
                                GlassButton {
                                    compact: true; visible: !tarjeta.porOmision
                                    text: qsTr("Borrar salida")
                                    onClicked: {
                                        const id = tarjeta.salida, n = tarjeta.model.nombre
                                        ApplicationWindow.window.confirm({
                                            title: qsTr("¿Borrar «%1»?").arg(n),
                                            text: qsTr("Se corta todo lo que sale por ella (transmisión, destinos, grabación) y desaparece. Sus destinos quedan guardados: vuelven si se crea otra salida con el mismo nombre."),
                                            accept: qsTr("Borrar"), reject: qsTr("Cancelar"),
                                            action: function() { const e = Studio.borrarSalida(id); if (e !== "") Studio.notify(e, "error") }
                                        })
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
