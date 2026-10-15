// El cuerpo deliberativo (asamblea/CuerpoViewModel): la fuente única de
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

    // La foto de un integrante (la cara de su botón en la botonera).
    property string fotoDe: ""
    FileDialog {
        id: abrirFoto
        nameFilters: ["Imágenes (*.jpg *.jpeg *.png *.webp *.heic)"]
        onAccepted: { const e = pop.cu.subirFoto(pop.fotoDe, selectedFile); Studio.notify(e !== "" ? e : "Foto cargada", e !== "" ? "error" : "info") }
    }
    Connections {
        target: pop.cu
        function onFotoLista(id, ok, texto) { Studio.notify(texto, ok ? "info" : "error") }
    }
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
                placeholderText: "Nombre (p. ej. Asamblea 2025-2029)"
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
                model: [ { t: "Foto", w: 36 }, { t: "Banca", w: 60 }, { t: "Nombre", w: 190 }, { t: "Cargo", w: 130 }, { t: "Bloque", w: 120 },
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
                Rectangle {
                    Layout.preferredWidth: 36; Layout.preferredHeight: 36; radius: 6; clip: true
                    color: Qt.rgba(1, 1, 1, 0.06)
                    visible: !fila.modelData.nuevo
                    Image { anchors.fill: parent; fillMode: Image.PreserveAspectCrop; asynchronous: true
                            source: fila.modelData.foto_url || "" }
                    Text { anchors.centerIn: parent; visible: !fila.modelData.foto_url; color: Theme.textFaint; font.pixelSize: 10
                           text: (fila.modelData.nombre || "?").split(" ").map(p => p.charAt(0)).join("").slice(0, 2).toUpperCase() }
                    MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: menuFoto.popup() }
                    Menu {
                        id: menuFoto
                        MenuItem { text: "Subir una imagen…"; onTriggered: { pop.fotoDe = fila.modelData.id; abrirFoto.open() } }
                        MenuItem { text: "Capturar con la cámara de su banca"; enabled: !!fila.ptz.camara
                                   onTriggered: { Studio.notify("Llevando la cámara a la banca " + (fila.modelData.banca || "") + "…", "info")
                                                  pop.cu.capturarFoto(fila.modelData.id) } }
                        MenuItem { text: "Quitar la foto"; enabled: !!fila.modelData.foto_url; onTriggered: pop.cu.borrarFoto(fila.modelData.id) }
                    }
                }
                Item { visible: !!fila.modelData.nuevo; Layout.preferredWidth: 36 }
                Celda { campo: "banca"; valor: fila.modelData.banca || ""; Layout.preferredWidth: 60 }
                Celda { campo: "nombre"; valor: fila.modelData.nombre || ""; Layout.preferredWidth: 190 }
                Celda { campo: "cargo_oficial"; valor: fila.modelData.cargo_oficial || ""; Layout.preferredWidth: 130 }
                Celda { campo: "bloque"; valor: fila.modelData.bloque || ""; Layout.preferredWidth: 120 }
                Celda { campo: "distrito"; valor: fila.modelData.distrito || ""; Layout.preferredWidth: 100 }
                // La toma es de SU BANCA (cámara y posición, de listas): queda en la banca aunque cambie la persona.
                SelectorToma {
                    enabled: !fila.modelData.nuevo && (fila.modelData.banca || "") !== ""
                    camara: fila.ptz.camara || ""
                    posicion: fila.ptz.posicion || ""
                    anchoCamara: 110; anchoPosicion: 100
                    onElegido: (c, p) => {
                        const e = pop.cu.guardar({ id: fila.modelData.id, camara: c, posicion: p })
                        if (e !== "") Studio.notify(e, "error")
                    }
                    ToolTip.visible: hovered && (fila.modelData.banca || "") === ""; ToolTip.text: "Primero su banca: la toma es de la banca"
                    property bool hovered: false
                    HoverHandler { onHoveredChanged: parent.hovered = hovered }
                }
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
