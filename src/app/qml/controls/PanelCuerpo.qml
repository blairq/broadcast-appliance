// El cuerpo legislativo (legislatura/CuerpoViewModel): la fuente única de
// nombre, cargo, bloque y banca para los zócalos, las bancas de la fila de
// oradores y el PTZ por banca. Tabla editable, importar planilla, validar.
import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import BA

Popup {
    id: pop
    readonly property var cu: Studio.cuerpo
    anchors.centerIn: Overlay.overlay
    width: Math.min((parent ? parent.width : 1100) - 60, 1100)
    height: Math.min((parent ? parent.height : 760) - 60, 760)
    modal: true
    padding: 18
    background: Rectangle { color: Theme.panel; radius: Theme.radiusSmall; border.color: Theme.glassBorder }

    FileDialog {
        id: abrirPlanilla
        nameFilters: ["Planillas (*.csv *.txt)"]
        onAccepted: Studio.notify(pop.cu.importar(selectedFile, false), "info")
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 10
        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            Text { text: "Cuerpo"; color: Theme.text; font { pixelSize: 18; weight: Font.DemiBold } }
            TextField {
                Layout.preferredWidth: 320
                text: pop.cu.nombre
                placeholderText: "Nombre (p. ej. Legislatura 2025-2029)"
                onEditingFinished: if (text !== pop.cu.nombre) pop.cu.setNombre(text)
            }
            StatusPill {
                text: pop.cu.integrantes.length + " integrantes · " + pop.cu.integrantes.filter(x => !x.validado_por).length + " sin validar"
                dot: pop.cu.integrantes.some(x => !x.validado_por) ? Theme.warn : Theme.ok
            }
            Item { Layout.fillWidth: true }
            GlassButton { compact: true; text: "Importar planilla…"; onClicked: abrirPlanilla.open() }
            GlassButton { compact: true; text: "Planilla de ejemplo"
                          onClicked: { const r = pop.cu.plantilla(); Studio.notify(r !== "" ? "Escrita en " + r : "No se pudo escribir", r !== "" ? "info" : "error") } }
            GlassButton { compact: true; text: "Cerrar"; onClicked: pop.close() }
        }
        Text {
            Layout.fillWidth: true; wrapMode: Text.WordWrap
            color: Theme.textDim; font.pixelSize: 11
            text: "Lo que sale en los zócalos sale de acá. Un integrante «sin validar» (nuevo, o le cambiaron el nombre o el cargo) se marca " +
                  "hasta que alguien lo revise. Cámara y posición: el preset PTZ de su banca; micrófono: la fuente de audio de su banca."
        }
        // Encabezado
        RowLayout {
            Layout.fillWidth: true
            spacing: 6
            Repeater {
                model: [ { t: "Banca", w: 60 }, { t: "Nombre", w: 190 }, { t: "Cargo", w: 130 }, { t: "Bloque", w: 120 },
                         { t: "Distrito", w: 100 }, { t: "Cámara", w: 110 }, { t: "Posición", w: 100 }, { t: "Micrófono", w: 110 } ]
                delegate: Text { required property var modelData; text: modelData.t; color: Theme.textFaint
                                 font { pixelSize: 10; weight: Font.Bold }
                                 Layout.preferredWidth: modelData.w }
            }
            Item { Layout.fillWidth: true }
        }
        ListView {
            id: lista
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            spacing: 3
            model: pop.cu.integrantes.concat([{ id: "", nuevo: true }])
            delegate: RowLayout {
                id: fila
                required property var modelData
                readonly property var ptz: modelData.preset_ptz || ({})
                readonly property var mic: modelData.microfono || ({})
                width: ListView.view.width
                spacing: 6
                function poner(campo, valor) {
                    const c = {}
                    if (fila.modelData.id) c.id = fila.modelData.id
                    c[campo] = valor
                    if (campo === "camara") c.posicion = ptz.posicion || ""
                    if (campo === "posicion") c.camara = ptz.camara || ""
                    const e = pop.cu.guardar(c)
                    if (e !== "") Studio.notify(e, "error")
                }
                component Celda: TextField {
                    property string campo
                    property string valor
                    text: valor
                    font.pixelSize: 11
                    placeholderText: fila.modelData.nuevo ? (campo === "nombre" ? "+ nuevo integrante" : "") : ""
                    onEditingFinished: if (text !== valor && (fila.modelData.id || (campo === "nombre" && text !== ""))) fila.poner(campo, text)
                }
                Celda { campo: "banca"; valor: fila.modelData.banca || ""; Layout.preferredWidth: 60 }
                Celda { campo: "nombre"; valor: fila.modelData.nombre || ""; Layout.preferredWidth: 190 }
                Celda { campo: "cargo_oficial"; valor: fila.modelData.cargo_oficial || ""; Layout.preferredWidth: 130 }
                Celda { campo: "bloque"; valor: fila.modelData.bloque || ""; Layout.preferredWidth: 120 }
                Celda { campo: "distrito"; valor: fila.modelData.distrito || ""; Layout.preferredWidth: 100 }
                Celda { campo: "camara"; valor: fila.ptz.camara || ""; Layout.preferredWidth: 110 }
                Celda { campo: "posicion"; valor: fila.ptz.posicion || ""; Layout.preferredWidth: 100 }
                Celda { campo: "microfono"; valor: fila.mic.fuente || ""; Layout.preferredWidth: 110 }
                StatusPill {
                    visible: !fila.modelData.nuevo
                    text: fila.modelData.validado_por ? "validado" : "sin validar"
                    dot: fila.modelData.validado_por ? Theme.ok : Theme.warn
                }
                GlassButton { compact: true; visible: !fila.modelData.nuevo && !fila.modelData.validado_por; text: "Validar"
                              onClicked: pop.cu.validar(fila.modelData.id) }
                GlassButton { compact: true; visible: !fila.modelData.nuevo; text: "✕"; onClicked: pop.cu.borrar(fila.modelData.id) }
                Item { Layout.fillWidth: true }
            }
        }
    }
}
