// Las descargas: TODO lo que el programa baja, en una sola cola (net/Descargas).
//
// Lo que se está bajando (avance, MB, velocidad), lo que espera, lo que terminó
// y las actualizaciones de lo instalado. Se abre desde la píldora «BAJANDO» de
// arriba o desde Configuración -> Modelos locales («Descargas…»).
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import BA
import "."

Dialog {
    id: dlg
    anchors.centerIn: parent
    modal: true
    width: Math.min(parent ? parent.width - 32 : 640, 640)
    height: Math.min(parent ? parent.height - 64 : 560, 560)
    title: qsTr("Descargas")
    standardButtons: Dialog.Close

    readonly property var dl: Studio.descargas

    function mb(b) { return (b / 1048576).toFixed(b >= 1073741824 ? 0 : 1) }
    function velocidad(kbps) { return kbps >= 1024 ? (kbps / 1024).toFixed(1) + " MB/s" : Math.round(kbps) + " KB/s" }
    function colorDe(e) {
        return e === "bajando" ? Theme.accent : e === "en_cola" ? Theme.textDim
             : e === "listo" ? Theme.ok : e === "error" ? Theme.live : Theme.textFaint
    }
    function nombreEstado(e) {
        return e === "bajando" ? qsTr("bajando") : e === "en_cola" ? qsTr("en la cola") : e === "listo" ? qsTr("listo")
             : e === "error" ? qsTr("falló") : qsTr("cancelada")
    }

    contentItem: ColumnLayout {
        spacing: 10

        // --- Actualizaciones ---------------------------------------------------------
        RowLayout {
            Layout.fillWidth: true
            Text {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                color: Theme.textDim; font.pixelSize: 12
                text: dlg.dl.buscando ? qsTr("Buscando actualizaciones…")
                      : dlg.dl.actualizaciones.length > 0 ? qsTr("Hay %n actualización.", "", dlg.dl.actualizaciones.length)
                      : qsTr("Una sola cola para todo, de a dos a la vez. Lo que ya está en la cola o bajado no se repite.")
            }
            GlassButton {
                compact: true; text: qsTr("Buscar actualizaciones")
                enabled: !dlg.dl.buscando
                onClicked: dlg.dl.buscarActualizaciones()
            }
        }
        Repeater {
            model: dlg.dl.actualizaciones
            delegate: RowLayout {
                required property var modelData
                Layout.fillWidth: true
                Text {
                    Layout.fillWidth: true
                    elide: Text.ElideRight
                    color: Theme.text; font.pixelSize: 12
                    text: modelData.titulo + "  ·  " + modelData.actual + " → " + modelData.version
                }
                GlassButton { compact: true; active: true; text: qsTr("Actualizar"); onClicked: dlg.dl.actualizar(modelData.id) }
            }
        }
        Rectangle { Layout.fillWidth: true; height: 1; color: Theme.glassBorder }

        // --- La cola y el historial ----------------------------------------------------
        ListView {
            id: lista
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            spacing: 8
            model: dlg.dl.lista
            ScrollBar.vertical: ScrollBar {}
            delegate: ColumnLayout {
                required property var modelData
                width: lista.width - 12
                spacing: 3
                RowLayout {
                    Layout.fillWidth: true
                    Rectangle { width: 8; height: 8; radius: 4; color: dlg.colorDe(modelData.estado) }
                    Text {
                        Layout.fillWidth: true
                        elide: Text.ElideRight
                        color: Theme.text; font.pixelSize: 13; font.weight: Font.DemiBold
                        text: modelData.titulo + (modelData.version !== "" ? "  ·  " + modelData.version : "")
                    }
                    Text { color: dlg.colorDe(modelData.estado); font.pixelSize: 11; text: dlg.nombreEstado(modelData.estado) }
                    GlassButton {
                        compact: true; text: qsTr("Cancelar")
                        visible: modelData.estado === "bajando" || modelData.estado === "en_cola"
                        onClicked: dlg.dl.cancelar(modelData.id)
                    }
                    GlassButton {
                        compact: true; text: qsTr("Reintentar")
                        visible: modelData.reintentable
                        onClicked: dlg.dl.reintentar(modelData.id)
                    }
                }
                ProgressBar {
                    Layout.fillWidth: true
                    visible: modelData.estado === "bajando"
                    from: 0; to: 1; value: modelData.progreso
                }
                Text {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    color: modelData.estado === "error" ? Theme.live : Theme.textDim
                    font.pixelSize: 11
                    text: modelData.estado === "bajando" && modelData.kbps > 0
                          ? modelData.texto + "  ·  " + dlg.velocidad(modelData.kbps)
                          : modelData.estado === "listo" && modelData.bytes > 0
                          ? qsTr("Listo  ·  %1 MB").arg(dlg.mb(modelData.bytes))
                          : modelData.texto
                }
            }
            Text {
                anchors.centerIn: parent
                visible: lista.count === 0
                color: Theme.textFaint; font.pixelSize: 12
                text: qsTr("No se bajó nada todavía.")
            }
        }
        RowLayout {
            Layout.fillWidth: true
            Item { Layout.fillWidth: true }
            GlassButton { compact: true; text: qsTr("Limpiar lo terminado"); onClicked: dlg.dl.limpiarTerminadas() }
        }
    }
}
