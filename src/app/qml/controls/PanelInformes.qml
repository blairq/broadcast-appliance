// Los informes del resultado de la sesión en PDF (asamblea/InformesResultado). Dos
// partes: GENERAR (se elige la plantilla, se ve cómo queda con lo de ahora y se pasa
// a PDF en la carpeta del evento) y PLANTILLAS (ver y cambiar el texto de cada una,
// crear otra, pedírsela al asistente). Una plantilla es Markdown con campos
// ({{cuerpo}}, {{#puntos}}…{{/puntos}}): la lista está al costado del editor.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import BA

ColumnLayout {
    id: inf
    spacing: 8
    readonly property var m: Studio.informes
    readonly property var lista: inf.m ? inf.m.plantillas : []
    property string elegida: lista.length > 0 ? lista[0].id : ""
    property string ultimo: ""
    function nombreDe(id) { const p = inf.lista.find(x => x.id === id); return p ? p.nombre : id }
    // Si la elegida desaparece (se quitó), la primera.
    onListaChanged: if (!inf.lista.some(p => p.id === inf.elegida)) inf.elegida = inf.lista.length > 0 ? inf.lista[0].id : ""

    TabBar {
        id: partes
        Layout.fillWidth: true
        TabButton { text: qsTr("Generar") }
        TabButton { text: qsTr("Plantillas") }
    }

    StackLayout {
        Layout.fillWidth: true
        Layout.fillHeight: true
        currentIndex: partes.currentIndex

        // --- Generar --------------------------------------------------------------
        ColumnLayout {
            spacing: 8
            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                Text { text: qsTr("Plantilla"); color: Theme.textDim; font.pixelSize: 12 }
                ComboBox {
                    id: selGenerar
                    implicitWidth: 260
                    model: inf.lista
                    textRole: "nombre"
                    currentIndex: Math.max(0, inf.lista.findIndex(p => p.id === inf.elegida))
                    onActivated: (i) => inf.elegida = inf.lista[i].id
                }
                GlassButton {
                    compact: true; text: qsTr("Actualizar la vista")
                    onClicked: vista.text = inf.m.vistaUi(inf.elegida)
                }
                Item { Layout.fillWidth: true }
                GlassButton {
                    compact: true; active: true; text: qsTr("Generar PDF")
                    enabled: inf.elegida !== ""
                    onClicked: {
                        const r = inf.m.generarUi(inf.elegida)
                        if (r.error) { Studio.notify(r.error, "error"); return }
                        inf.ultimo = r.archivo
                        Studio.notify(qsTr("Informe guardado: %1").arg(r.archivo), "info")
                    }
                    ToolTip.visible: hovered; ToolTip.delay: 400
                    ToolTip.text: qsTr("Rellena la plantilla con lo de ahora y la guarda en PDF en la carpeta del evento (informes/), con el texto al lado")
                }
                GlassButton {
                    compact: true; text: qsTr("Abrir")
                    visible: inf.ultimo !== ""
                    onClicked: Qt.openUrlExternally("file://" + inf.ultimo)
                    ToolTip.visible: hovered; ToolTip.delay: 400; ToolTip.text: inf.ultimo
                }
            }
            Text {
                Layout.fillWidth: true; wrapMode: Text.WordWrap
                text: qsTr("Así queda con lo de ahora (el quórum, los puntos con su estado y resultado, las votaciones oficiales y el uso de la palabra):")
                color: Theme.textFaint; font.pixelSize: 11
            }
            ScrollView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                TextArea {
                    id: vista
                    readOnly: true
                    textFormat: TextEdit.MarkdownText
                    wrapMode: TextEdit.Wrap
                    color: Theme.text
                    text: inf.m && inf.elegida !== "" ? inf.m.vistaUi(inf.elegida) : ""
                    background: Rectangle { color: Qt.rgba(1, 1, 1, 0.03); radius: 8; border.color: Theme.glassBorder }
                }
            }
        }

        // --- Plantillas -----------------------------------------------------------
        ColumnLayout {
            spacing: 8
            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                ComboBox {
                    implicitWidth: 240
                    model: inf.lista
                    textRole: "nombre"
                    currentIndex: Math.max(0, inf.lista.findIndex(p => p.id === inf.elegida))
                    onActivated: (i) => inf.elegida = inf.lista[i].id
                }
                TextField {
                    id: nombre
                    Layout.preferredWidth: 220
                    placeholderText: qsTr("Nombre")
                    text: inf.nombreDe(inf.elegida)
                    color: Theme.text
                }
                GlassButton {
                    compact: true; active: editor.text !== editor.guardado || nombre.text !== inf.nombreDe(inf.elegida)
                    text: qsTr("Guardar")
                    onClicked: {
                        const e = inf.m.guardarUi(inf.elegida, editor.text, nombre.text)
                        if (e !== "") Studio.notify(e, "error"); else editor.guardado = editor.text
                    }
                }
                GlassButton {
                    compact: true; text: qsTr("Nueva…")
                    onClicked: nueva.open()
                }
                GlassButton {
                    compact: true; text: qsTr("Quitar")
                    enabled: inf.lista.length > 1
                    onClicked: ApplicationWindow.window.confirm({
                        title: qsTr("¿Quitar la plantilla «%1»?").arg(inf.nombreDe(inf.elegida)),
                        text: qsTr("Los informes ya generados quedan."), accept: qsTr("Quitar"), reject: qsTr("Cancelar"),
                        action: () => { const e = inf.m.quitarUi(inf.elegida); if (e !== "") Studio.notify(e, "error") }
                    })
                }
                Item { Layout.fillWidth: true }
                GlassButton {
                    compact: true; text: qsTr("✨ Pedírsela al asistente")
                    onClicked: ApplicationWindow.window.pedirAlAsistente(
                        qsTr("Armame una plantilla para el informe del resultado de la sesión (informe_plantilla_guardar). "
                             + "Contame qué datos querés que tenga y cómo, y la escribo: encabezado, quórum, puntos del "
                             + "orden del día con su resultado, votaciones, uso de la palabra, firmas…"))
                    ToolTip.visible: hovered; ToolTip.delay: 400
                    ToolTip.text: qsTr("El asistente escribe la plantilla con los campos; después la ves y la cambiás acá")
                }
            }
            RowLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 10
                ScrollView {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    TextArea {
                        id: editor
                        property string guardado: ""
                        font.family: "Menlo"
                        font.pixelSize: 12
                        wrapMode: TextEdit.Wrap
                        color: Theme.text
                        selectByMouse: true
                        background: Rectangle { color: Qt.rgba(1, 1, 1, 0.03); radius: 8; border.color: Theme.glassBorder }
                        // Lo guardado de la elegida; al cambiar de plantilla (o si la cambió el asistente) se recarga.
                        function cargar() { guardado = inf.m && inf.elegida !== "" ? inf.m.textoUi(inf.elegida) : ""; text = guardado }
                        Component.onCompleted: cargar()
                        Connections { target: inf; function onElegidaChanged() { editor.cargar() } }
                        Connections {
                            target: inf.m
                            // Lo que cambia otro (el asistente) se ve, salvo que haya algo a medio editar.
                            function onCambio() { if (editor.text === editor.guardado) editor.cargar() }
                        }
                    }
                }
                // Los campos que se pueden usar.
                ColumnLayout {
                    Layout.preferredWidth: 300
                    Layout.fillHeight: true
                    spacing: 4
                    Text { text: qsTr("Campos"); color: Theme.textDim; font { pixelSize: 12; weight: Font.DemiBold } }
                    ListView {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        spacing: 6
                        model: inf.m ? inf.m.campos : []
                        delegate: ColumnLayout {
                            required property var modelData
                            width: ListView.view.width
                            spacing: 1
                            Text { Layout.fillWidth: true; wrapMode: Text.WrapAnywhere; text: modelData.campo
                                   color: Theme.accent; font { family: "Menlo"; pixelSize: 11 } }
                            Text { Layout.fillWidth: true; wrapMode: Text.WordWrap; text: modelData.que
                                   color: Theme.textFaint; font.pixelSize: 11 }
                        }
                    }
                }
            }
        }
    }

    Dialog {
        id: nueva
        title: qsTr("Plantilla nueva")
        anchors.centerIn: Overlay.overlay
        modal: true
        standardButtons: Dialog.Ok | Dialog.Cancel
        ColumnLayout {
            spacing: 8
            Text { text: qsTr("Empieza con el texto de la de fábrica; después la cambiás."); color: Theme.textDim; font.pixelSize: 11 }
            TextField { id: nombreNueva; Layout.preferredWidth: 300; placeholderText: qsTr("Nombre (p. ej. «Acta para la prensa»)"); color: Theme.text }
        }
        onAccepted: {
            const r = inf.m.crearUi(nombreNueva.text)
            if (r.error) { Studio.notify(r.error, "error"); return }
            inf.elegida = r.id
            nombreNueva.text = ""
        }
    }
}
