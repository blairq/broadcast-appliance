// Administrar las plantillas de evento: el orden, el nombre que se ve, la descripción y
// si aparecen en «Evento nuevo» y en «Prepararlo con el asistente»; abrir una para
// editarla (la app se reinicia con ella), restablecer una del programa que se editó,
// borrar una propia y guardar el evento abierto como plantilla nueva.
// Las del programa (assets/demo, generadas por tools/generar/plantillas.py) no se tocan:
// editarla hace una copia propia que le gana. Modelo: Studio.configuraciones
// (plantillasAdminUi, plantillaAjustarUi, plantillaMoverUi, plantillaEditarUi…).
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import BA

Dialog {
    id: dlg
    readonly property var cf: Studio.configuraciones
    property var lista: []
    function refrescar() { dlg.lista = dlg.cf ? dlg.cf.plantillasAdminUi() : [] }
    function ok(e) { if (e !== "") Studio.notify(e, "error"); refrescar() }

    anchors.centerIn: parent
    width: Math.min(parent ? parent.width - 60 : 900, 900)
    height: Math.min(parent ? parent.height - 60 : 720, 720)
    modal: true
    title: "Plantillas de evento"
    onAboutToShow: refrescar()
    Connections { target: dlg.cf; function onCambio() { dlg.refrescar() } }

    ColumnLayout {
        anchors.fill: parent
        spacing: 8
        Text {
            Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.textDim; font.pixelSize: 12
            text: "Cada plantilla es un evento entero de un tipo (escenas, gráficos, guion con los textos para completar, " +
                  "discursos, padrón de ejemplo). Las visibles aparecen en «Evento nuevo» y en «Prepararlo con el asistente», " +
                  "en este orden. Las del programa no se tocan: editarla hace una copia tuya que le gana (y se puede restablecer)."
        }
        ListView {
            id: lv
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            spacing: 4
            model: dlg.lista
            delegate: Rectangle {
                required property var modelData
                required property int index
                width: lv.width
                height: 74
                radius: 8
                color: modelData.visible ? Qt.rgba(1, 1, 1, 0.05) : Qt.rgba(1, 1, 1, 0.02)
                border.color: modelData.activa ? Theme.accent : Theme.glassBorder
                opacity: modelData.visible ? 1 : 0.6
                RowLayout {
                    anchors { fill: parent; margins: 8 }
                    spacing: 8
                    ColumnLayout {
                        spacing: 0
                        GlassButton { compact: true; text: "▲"; enabled: index > 0; onClicked: dlg.ok(dlg.cf.plantillaMoverUi(modelData.id, -1)) }
                        GlassButton { compact: true; text: "▼"; enabled: index < dlg.lista.length - 1; onClicked: dlg.ok(dlg.cf.plantillaMoverUi(modelData.id, 1)) }
                    }
                    Text { text: modelData.icono; color: Theme.accent; font.pixelSize: 24; Layout.preferredWidth: 30 }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 3
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 6
                            TextField {
                                Layout.fillWidth: true
                                text: modelData.nombre
                                color: Theme.text
                                font { pixelSize: 13; weight: Font.DemiBold }
                                onEditingFinished: if (text !== modelData.nombre) dlg.ok(dlg.cf.plantillaAjustarUi(modelData.id, { nombre: text }))
                            }
                            Repeater {
                                model: [modelData.programa && !modelData.editada ? "del programa" : "",
                                        modelData.editada ? "editada" : "", modelData.propia && !modelData.programa ? "propia" : "",
                                        modelData.activa ? "abierta" : ""].filter(x => x !== "")
                                delegate: Rectangle {
                                    required property string modelData
                                    implicitWidth: et.implicitWidth + 10; implicitHeight: 18; radius: 4
                                    color: Qt.rgba(1, 1, 1, 0.08)
                                    Text { id: et; anchors.centerIn: parent; text: modelData; color: Theme.textDim; font.pixelSize: 10 }
                                }
                            }
                        }
                        TextField {
                            Layout.fillWidth: true
                            text: modelData.descripcion
                            color: Theme.textDim
                            font.pixelSize: 11
                            placeholderText: "Para qué sirve"
                            onEditingFinished: if (text !== modelData.descripcion) dlg.ok(dlg.cf.plantillaAjustarUi(modelData.id, { descripcion: text }))
                        }
                    }
                    ColumnLayout {
                        spacing: 2
                        Text { text: "Visible"; color: Theme.textFaint; font.pixelSize: 10; Layout.alignment: Qt.AlignHCenter }
                        Toggle { checked: modelData.visible; onToggled: (w) => dlg.ok(dlg.cf.plantillaAjustarUi(modelData.id, { visible: w })) }
                    }
                    ColumnLayout {
                        spacing: 3
                        GlassButton {
                            compact: true; text: "Editar"
                            enabled: !modelData.activa && !(Studio.streaming || Studio.recording)
                            onClicked: {
                                const id = modelData.id, n = modelData.nombre
                                ApplicationWindow.window.confirm({
                                    title: "¿Editar «" + n + "»?",
                                    text: "La app se reinicia con la plantilla abierta (sus escenas, gráficos y guion). Lo que cambies queda en la plantilla. Para volver, «Abrir otro…».",
                                    accept: "Editarla", reject: "Cancelar",
                                    action: function() { dlg.ok(dlg.cf.plantillaEditarUi(id)) } })
                            }
                        }
                        GlassButton {
                            compact: true; text: modelData.editada ? "Restablecer" : "Borrar"
                            visible: modelData.editada || (modelData.propia && !modelData.programa)
                            onClicked: {
                                const id = modelData.id, n = modelData.nombre, rest = modelData.editada
                                ApplicationWindow.window.confirm({
                                    title: rest ? "¿Restablecer «" + n + "»?" : "¿Borrar «" + n + "»?",
                                    text: rest ? "Se saca tu copia (va a la papelera) y vuelve la del programa." : "Va a la papelera de eventos.",
                                    accept: rest ? "Restablecer" : "Borrar", reject: "Cancelar",
                                    action: function() { dlg.ok(rest ? dlg.cf.plantillaRestablecerUi(id) : dlg.cf.plantillaBorrarUi(id)) } })
                            }
                        }
                    }
                }
            }
        }
        // El evento abierto, como plantilla nueva.
        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            Text { text: "Guardar el evento abierto como plantilla:"; color: Theme.textDim; font.pixelSize: 12 }
            TextField { id: nuevaN; Layout.fillWidth: true; placeholderText: "Nombre de la plantilla"; color: Theme.text }
            ComboBox {
                id: nuevaEsc
                Layout.preferredWidth: 220
                model: [{ titulo: "Sin tipo", id: "" }].concat(Studio.evento ? Studio.evento.escenarios() : [])
                textRole: "titulo"
                valueRole: "id"
                ToolTip.visible: hovered; ToolTip.delay: 400; ToolTip.text: "El tipo de evento (su guía, para el asistente)"
            }
            GlassButton {
                compact: true; active: true; text: "Guardar"
                enabled: nuevaN.text.trim() !== ""
                onClicked: { dlg.ok(dlg.cf.plantillaDesdeActualUi(nuevaN.text, nuevaEsc.currentValue)); nuevaN.text = "" }
            }
        }
    }

    footer: RowLayout {
        Item { Layout.fillWidth: true }
        GlassButton { text: "Cerrar"; onClicked: dlg.close() }
        Item { Layout.preferredWidth: 12; Layout.preferredHeight: 50 }
    }
}
