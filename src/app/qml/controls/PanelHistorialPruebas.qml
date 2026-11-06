// El historial de pruebas de esta máquina (Pruebas → Historial): la lista, abrir una (sus
// parámetros, las fichas de las máquinas, los resultados y los gráficos), comparar dos y
// exportar (JSON o CSV). Solo presentación: todo sale de Studio.historialPruebas
// (ui/HistorialPruebasViewModel), lo mismo que las herramientas MCP pruebas_historial y pruebas_ver.
import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import BA

ScrollView {
    id: hp
    clip: true
    contentWidth: availableWidth
    readonly property var vm: Studio.historialPruebas
    readonly property var d: vm.detalle
    /// Los dos elegidos para comparar.
    property var elegidos: []

    function fecha(ms) { return ms ? new Date(ms).toLocaleString(Qt.locale(), Locale.ShortFormat) : "" }
    function nombreTipo(t) {
        return t === "sala" ? qsTr("prueba de sala") : t === "local" ? qsTr("capacidad (esta)")
             : t === "remoto" ? qsTr("capacidad (otra)") : t === "recibida" ? qsTr("recibida")
             : t === "internet" ? qsTr("conexión") : t === "asamblea" ? qsTr("asamblea") : t
    }
    function elegir(archivo, si) {
        let e = hp.elegidos.filter(a => a !== archivo)
        if (si) e.push(archivo)
        if (e.length > 2) e = e.slice(e.length - 2)
        hp.elegidos = e
    }
    /// La ficha de una máquina en una línea.
    function ficha(f) {
        if (!f || !f.nombre) return ""
        const red = (f.red || []).map(r => r.descripcion + " (" + (r.tipo === "wifi" ? qsTr("Wi-Fi") : r.tipo === "cable" ? qsTr("cable") : r.tipo)
                                          + (r.velocidadMbps > 0 ? " " + r.velocidadMbps + " Mbps" : "") + ")").join(", ")
        return qsTr("%1 · %2 · %3, %4 núcleos · %5 GB · %6 · %7")
            .arg(f.nombre).arg(f.sistema || "").arg(f.cpu ? f.cpu.modelo : "").arg(f.cpu ? f.cpu.nucleosLogicos : "")
            .arg(f.memoriaMb > 0 ? Math.round(f.memoriaMb / 1024) : "–").arg(f.gpu ? f.gpu.nombre : "").arg(red)
    }

    ColumnLayout {
        width: hp.availableWidth
        spacing: 12

        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            Text { text: qsTr("%n pruebas", "", hp.vm.lista.length); color: Theme.textDim; font.pixelSize: 12 }
            Item { Layout.fillWidth: true }
            GlassButton { compact: true; text: qsTr("Actualizar"); onClicked: hp.vm.recargar() }
            GlassButton {
                compact: true
                text: qsTr("Comparar las dos elegidas")
                enabled: hp.elegidos.length === 2
                onClicked: hp.vm.comparar(hp.elegidos[0], hp.elegidos[1])
            }
            GlassButton { compact: true; text: qsTr("Exportar (CSV)…"); onClicked: { exportar.json = false; exportar.open() } }
            GlassButton {
                compact: true
                text: hp.d.archivo ? qsTr("Exportar la abierta (JSON)…") : qsTr("Exportar (JSON)…")
                onClicked: { exportar.json = true; exportar.open() }
            }
        }

        // --- La comparación ----------------------------------------------------------------
        Seccion {
            visible: hp.vm.comparacion.length > 0
            Layout.fillWidth: true
            titulo: qsTr("Comparación")
            extra: GlassButton { compact: true; text: qsTr("Cerrar"); onClicked: hp.vm.cerrar() }
            Repeater {
                model: hp.vm.comparacion
                delegate: RowLayout {
                    required property var modelData
                    Layout.fillWidth: true
                    spacing: 10
                    Text { text: modelData.clave; color: Theme.textDim; font.pixelSize: 11; Layout.preferredWidth: 160 }
                    Text {
                        text: modelData.clave === "fecha" ? hp.fecha(modelData.a) : String(modelData.a === -1 ? "–" : modelData.a)
                        color: Theme.text; font.family: Theme.mono; font.pixelSize: 11; Layout.preferredWidth: 220; elide: Text.ElideRight
                    }
                    Text {
                        text: modelData.clave === "fecha" ? hp.fecha(modelData.b) : String(modelData.b === -1 ? "–" : modelData.b)
                        color: Theme.text; font.family: Theme.mono; font.pixelSize: 11; Layout.fillWidth: true; elide: Text.ElideRight
                    }
                }
            }
        }

        // --- La abierta ----------------------------------------------------------------------
        Seccion {
            visible: !!hp.d.archivo
            Layout.fillWidth: true
            titulo: hp.d.archivo ? qsTr("%1 · %2").arg(hp.nombreTipo(hp.d.tipo)).arg(hp.fecha(hp.d.fecha)) : ""
            subtitulo: hp.d.frase || hp.d.nota || ""
            extra: GlassButton { compact: true; text: qsTr("Cerrar"); onClicked: hp.vm.cerrar() }
            Text {
                Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.textDim; font.pixelSize: 11
                visible: !!(hp.d.fichas && hp.d.fichas.local && hp.d.fichas.local.nombre)
                text: qsTr("Esta máquina: %1").arg(hp.ficha(hp.d.fichas ? hp.d.fichas.local : null))
            }
            Text {
                Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.textDim; font.pixelSize: 11
                visible: !!(hp.d.fichas && hp.d.fichas.remota && hp.d.fichas.remota.nombre)
                text: qsTr("La otra: %1").arg(hp.ficha(hp.d.fichas ? hp.d.fichas.remota : null))
            }
            Repeater {
                model: hp.d.fichas && hp.d.fichas.generadoras ? hp.d.fichas.generadoras : []
                delegate: Text {
                    required property var modelData
                    Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.textDim; font.pixelSize: 11
                    text: qsTr("Generadora: %1").arg(hp.ficha(modelData))
                }
            }
            Text {
                Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.textFaint; font.pixelSize: 11
                visible: !!hp.d.parametros || !!hp.d.decodificar
                text: qsTr("Parámetros: %1").arg(JSON.stringify(hp.d.parametros || { decodificar: hp.d.decodificar, tope: hp.d.tope }))
            }
            GraficasPrueba {
                Layout.fillWidth: true
                visible: (hp.d.serie || []).length > 1
                serie: hp.d.serie || []
                deSala: hp.d.tipo === "sala"
            }
        }

        // --- La lista -----------------------------------------------------------------------
        Repeater {
            model: hp.vm.lista
            delegate: RowLayout {
                required property var modelData
                Layout.fillWidth: true
                spacing: 10
                CheckBox {
                    checked: hp.elegidos.indexOf(modelData.archivo) >= 0
                    onToggled: hp.elegir(modelData.archivo, checked)
                    ToolTip.visible: hovered; ToolTip.delay: 400; ToolTip.text: qsTr("Elegir para comparar")
                }
                Text { text: hp.fecha(modelData.fecha); color: Theme.textFaint; font { family: Theme.mono; pixelSize: 11 } Layout.preferredWidth: 130 }
                Text { text: hp.nombreTipo(modelData.tipo); color: Theme.textDim; font.pixelSize: 11; Layout.preferredWidth: 110 }
                Text {
                    text: (modelData.maquina || "") + (modelData.remota ? " ↔ " + modelData.remota : "")
                    color: Theme.textDim; font.pixelSize: 11; elide: Text.ElideRight; Layout.preferredWidth: 200
                }
                Text {
                    text: modelData.participantes > 0 ? qsTr("%n participantes", "", modelData.participantes) : ""
                    color: Theme.textDim; font.pixelSize: 11; Layout.preferredWidth: 110
                }
                Text { text: modelData.frase || ""; color: Theme.text; font.pixelSize: 11; elide: Text.ElideRight; Layout.fillWidth: true }
                GlassButton { compact: true; text: qsTr("Abrir"); onClicked: hp.vm.abrir(modelData.archivo) }
            }
        }
        Text {
            visible: hp.vm.lista.length === 0 && !hp.vm.cargando
            color: Theme.textFaint; font.pixelSize: 12
            text: qsTr("Todavía no hay pruebas guardadas en esta máquina.")
        }
    }

    FileDialog {
        id: exportar
        property bool json: true
        fileMode: FileDialog.SaveFile
        nameFilters: json ? [qsTr("JSON (*.json)")] : [qsTr("CSV (*.csv)")]
        defaultSuffix: json ? "json" : "csv"
        onAccepted: {
            const e = hp.vm.exportar(selectedFile.toString())
            if (e) Studio.notify(e, "error"); else Studio.notify(qsTr("Exportado."), "ok")
        }
    }
}
