// Las 4 capas DSK (encima del programa, no cambian con la escena): un toque
// sube o baja, iluminadas cuando están arriba. «✎» edita la capa (contenido,
// recuadro, fija) y tiene la plantilla «Intérprete LSA». Al aire, subir pide
// confirmación; bajar una fija (LSA) siempre la pide. Las capas suben y bajan en
// la salida que maneja la interfaz (el modelo las muestra de ella).
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import BA

Flow {
    id: fila
    spacing: 6
    readonly property var dsk: Studio.dsk

    function tocar(c) {
        const subir = !c.visible
        const hacer = function(conf) {
            const e = fila.dsk.mostrarUi(c.capa, subir, conf)
            if (e !== "") Studio.notify(e, "error")
        }
        if (!subir && c.fijo)
            ApplicationWindow.window.confirm({
                title: qsTr("¿Bajar «%1»?").arg(c.nombre),
                text: qsTr("Es una capa fija (por ejemplo, el intérprete de lengua de señas): sale de la transmisión."),
                accept: qsTr("Bajarla"), reject: qsTr("Cancelar"), action: () => hacer(true)
            })
        else if (subir && Studio.salidas.aire.alAire)
            ApplicationWindow.window.confirm({
                title: qsTr("¿Subir «%1»?").arg(c.nombre), text: qsTr("Se ve en la transmisión y en la grabación."),
                accept: qsTr("Subir"), reject: qsTr("Cancelar"), action: () => hacer(false)
            })
        else
            hacer(false)
    }

    Repeater {
        model: fila.dsk ? fila.dsk.capas : []
        delegate: RowLayout {
            required property var modelData
            spacing: 0
            GlassButton {
                compact: true
                text: (modelData.fijo ? "🔒 " : "") + modelData.nombre
                active: modelData.visible
                activeColor: "#c62828"
                opacity: (modelData.contenido || []).length > 0 ? 1 : 0.45
                onClicked: fila.tocar(modelData)
                ToolTip.visible: hovered; ToolTip.delay: 400
                ToolTip.text: (modelData.contenido || []).length > 0
                              ? (modelData.visible ? qsTr("Capa %1: %2 (arriba)") : qsTr("Capa %1: %2")).arg(modelData.capa).arg(modelData.contenido.join(", "))
                              : qsTr("Capa %1 vacía: tocá ✎ para armarla").arg(modelData.capa)
            }
            GlassButton {
                compact: true; text: "✎"
                onClicked: { editor.capa = modelData; editor.open() }
            }
        }
    }

    Popup {
        id: editor
        property var capa: ({})
        modal: true
        width: 420
        padding: 16
        x: Math.max(0, (fila.width - width) / 2)
        onOpened: {
            nombreF.text = capa.nombre || ""
            contenidoF.currentIndex = -1
            contenidoF.editText = (capa.contenido || []).join(", ")
            fijoF.checked = !!capa.fijo
            posF.currentIndex = Math.max(0, posF.model.indexOf((capa.recuadro || {}).posicion || "pantalla completa"))
        }
        background: Rectangle { color: Theme.bg; border.color: Theme.glassBorder; radius: 12 }
        contentItem: ColumnLayout {
            spacing: 8
            Text {
                text: qsTr("Capa %1").arg(editor.capa.capa)
                color: Theme.text
                font.pixelSize: 15
                font.weight: Font.DemiBold
            }
            TextField { id: nombreF; Layout.fillWidth: true; placeholderText: qsTr("Nombre (Logo, Intérprete LSA…)"); color: Theme.text }
            Text { text: qsTr("Contenido (escenas o fuentes, separadas por coma)"); color: Theme.textDim; font.pixelSize: 11 }
            ComboBox {
                id: contenidoF
                Layout.fillWidth: true
                editable: true
                model: Studio.control.nombresParaDsk
                onActivated: (i) => {
                    const actual = editText.split(",").map(s => s.trim()).filter(s => s !== "" && s !== model[i])
                    editText = actual.concat([model[i]]).join(", ")
                }
            }
            RowLayout {
                Text { text: qsTr("Recuadro"); color: Theme.textDim; font.pixelSize: 12 }
                ComboBox {
                    id: posF
                    Layout.fillWidth: true
                    model: ["pantalla completa", "abajo_derecha", "abajo_izquierda", "arriba_derecha", "arriba_izquierda"]
                }
            }
            CheckBox { id: fijoF; text: qsTr("Fija (no se baja sin confirmar; para el intérprete de LSA)") }
            RowLayout {
                GlassButton {
                    compact: true; text: qsTr("Guardar"); active: true
                    onClicked: {
                        const def = { nombre: nombreF.text, fijo: fijoF.checked,
                                      contenido: contenidoF.editText.split(",").map(s => s.trim()).filter(s => s !== ""),
                                      recuadro: posF.currentIndex === 0 ? {} : { posicion: posF.currentText, tamano: 0.28, margen: 0.03 } }
                        const e = fila.dsk.definirUi(editor.capa.capa, def)
                        if (e !== "") Studio.notify(e, "error"); else editor.close()
                    }
                }
                GlassButton {
                    compact: true; text: qsTr("Plantilla «Intérprete LSA»")
                    ToolTip.visible: hovered; ToolTip.delay: 400
                    ToolTip.text: qsTr("La primera fuente del contenido (la cámara del intérprete) en un recuadro abajo a la derecha, fija")
                    onClicked: {
                        const primera = contenidoF.editText.split(",").map(s => s.trim()).filter(s => s !== "")[0] || ""
                        const e = fila.dsk.plantillaLsaUi(editor.capa.capa, primera)
                        if (e !== "") Studio.notify(e, "error"); else editor.close()
                    }
                }
                Item { Layout.fillWidth: true }
                GlassButton { compact: true; text: qsTr("Cerrar"); onClicked: editor.close() }
            }
        }
    }
}
