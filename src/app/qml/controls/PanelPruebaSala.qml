// La prueba de sala: el simulacro con participantes de prueba antes del evento real.
// Solo presentación: todo sale de Studio.pruebaSala (ui/PruebaSalaViewModel), el mismo
// modelo que usan las herramientas prueba_sala_* del MCP. Ver docs/funciones/PRUEBA-SALA.md.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import BA

Panel {
    id: panel
    readonly property var ps: Studio.pruebaSala
    readonly property var v: ps.vivo
    readonly property var m: ps.maquina
    readonly property var ult: ps.ultimoInforme
    readonly property var u: v.ultima || ({})
    title: qsTr("Prueba de sala")
    Layout.preferredHeight: col.implicitHeight + 62

    function mbps(kbps) { return kbps === undefined || kbps < 0 ? "–" : (kbps / 1000).toFixed(1) + " Mbps" }
    function pct(x) { return x === undefined || x < 0 ? "–" : Math.round(x) + " %" }

    ColumnLayout {
        id: col
        anchors.fill: parent
        spacing: 8

        Text {
            Layout.fillWidth: true
            visible: !panel.ps.corriendo
            wrapMode: Text.WordWrap
            color: Theme.textFaint
            font.pixelSize: 11
            text: qsTr("Antes del evento: entran participantes de prueba por el camino real (con video y voz) para ver si la máquina y la red aguantan. No pasan por el lobby, quedan fuera del padrón, la votación y el quórum, y al terminar se van todos: del evento queda solo el informe.")
        }

        // --- Para arrancar ----------------------------------------------------------
        GridLayout {
            Layout.fillWidth: true
            visible: !panel.ps.corriendo
            columns: 2
            columnSpacing: 8
            rowSpacing: 6
            Text { text: qsTr("Participantes"); color: Theme.textDim; font.pixelSize: 12 }
            SpinBox {
                id: cuantos
                from: 1; to: panel.ps.maxParticipantes; value: 10; editable: true
                Layout.fillWidth: true
                onValueModified: panel.ps.revisarMaquina(value, conHinchada.checked)
            }
            Text { text: qsTr("Minutos"); color: Theme.textDim; font.pixelSize: 12 }
            SpinBox { id: minutos; from: 1; to: 120; value: 5; editable: true; Layout.fillWidth: true }
            Text {
                text: qsTr("Hablan a la vez (0: cada uno a su ritmo)")
                color: Theme.textDim; font.pixelSize: 12
                wrapMode: Text.WordWrap; Layout.maximumWidth: 150
            }
            SpinBox { id: hablan; from: 0; to: 10; value: 2; editable: true; Layout.fillWidth: true }
        }
        CheckBox {
            id: conHinchada
            visible: !panel.ps.corriendo
            checked: panel.ps.hinchadaDisponible
            enabled: panel.ps.hinchadaDisponible
            text: panel.ps.hinchadaDisponible ? qsTr("Los genera esta máquina (hinchada)")
                                              : qsTr("Sin probador en el programa: carga externa")
            onToggled: panel.ps.revisarMaquina(cuantos.value, checked)
        }
        // La guardia: lo que dice la máquina antes de arrancar.
        Text {
            Layout.fillWidth: true
            visible: !panel.ps.corriendo && panel.m.memLibreMb !== undefined
            wrapMode: Text.WordWrap
            font.pixelSize: 11
            color: panel.m.puede ? Theme.live : Theme.textFaint
            text: !visible ? ""
                  : panel.m.puede ? qsTr("No se puede arrancar: %1").arg(panel.m.puede)
                  : qsTr("Memoria disponible %1 MB · carga %2 · %3 hilos").arg(Math.round(panel.m.memLibreMb))
                        .arg(panel.m.carga >= 0 ? panel.m.carga.toFixed(1) : "–").arg(panel.m.hilosApp)
        }
        // El enlace (por el túnel) para los N: lo que dice la última medición.
        RowLayout {
            Layout.fillWidth: true
            visible: !panel.ps.corriendo
            spacing: 8
            Text {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                font.pixelSize: 11
                color: panel.ps.enlace.veredicto === "no_entra" ? Theme.live
                       : panel.ps.enlace.veredicto === "justo" ? Theme.amber : Theme.textFaint
                text: panel.ps.enlace.veredicto === "entra" ? qsTr("Enlace: alcanza para %n invitados (última medición).", "", cuantos.value)
                      : panel.ps.enlace.veredicto === "justo" ? qsTr("Enlace: justo para %n invitados.", "", cuantos.value)
                      : panel.ps.enlace.veredicto === "no_entra" ? qsTr("Enlace: NO alcanza para %n invitados (entran %1 con margen).", "", cuantos.value).arg(panel.ps.enlace.entranConMargen)
                      : qsTr("Enlace sin medir: medilo antes del evento real.")
            }
            GlassButton {
                visible: Studio.enlace.hayTunel
                enabled: !Studio.enlace.midiendo
                text: Studio.enlace.midiendo ? qsTr("Midiendo…") : qsTr("Medir el enlace")
                compact: true
                onClicked: {
                    const e = Studio.enlace.medir(false)
                    if (e) Studio.notify(e, "warn")
                }
            }
        }
        RowLayout {
            Layout.fillWidth: true
            visible: !panel.ps.corriendo
            spacing: 8
            GlassButton {
                text: qsTr("Revisar la máquina")
                compact: true
                onClicked: panel.ps.revisarMaquina(cuantos.value, conHinchada.checked)
            }
            GlassButton {
                text: panel.ps.estado === "terminando" ? qsTr("Terminando…") : qsTr("Arrancar la prueba")
                active: true
                enabled: panel.ps.estado === "apagada"
                onClicked: {
                    const err = panel.ps.arrancar(cuantos.value, minutos.value, conHinchada.checked, hablan.value)
                    if (err) Studio.notify(qsTr("Prueba de sala: %1").arg(err), "error")
                }
            }
        }

        // --- En curso -------------------------------------------------------------
        RowLayout {
            Layout.fillWidth: true
            visible: panel.ps.corriendo
            spacing: 6
            StatusPill { text: qsTr("%1/%2 con video").arg(panel.v.conVideo || 0).arg(panel.v.pedidos || 0); dot: Theme.ok }
            StatusPill { visible: (panel.v.cortes || 0) > 0; text: qsTr("%n cortes", "", panel.v.cortes || 0); dot: Theme.amber }
        }
        GridLayout {
            Layout.fillWidth: true
            visible: panel.ps.corriendo
            columns: 2
            columnSpacing: 10
            rowSpacing: 2
            Text { text: qsTr("Subida / bajada"); color: Theme.textDim; font.pixelSize: 11 }
            Text { text: panel.mbps(panel.u.kbpsSubida) + " / " + panel.mbps(panel.u.kbpsBajada); color: Theme.text; font { family: Theme.mono; pixelSize: 11 } }
            Text { text: qsTr("CPU app / gateway"); color: Theme.textDim; font.pixelSize: 11 }
            Text { text: panel.pct(panel.u.cpuApp) + " / " + panel.pct(panel.u.cpuGateway); color: Theme.text; font { family: Theme.mono; pixelSize: 11 } }
            Text { text: qsTr("Memoria disponible"); color: Theme.textDim; font.pixelSize: 11 }
            Text {
                text: panel.u.memLibreMb !== undefined ? Math.round(panel.u.memLibreMb) + " MB" : "–"
                color: (panel.u.memLibreMb || 99999) < 4500 ? Theme.live : Theme.text
                font { family: Theme.mono; pixelSize: 11 }
            }
            Text { text: qsTr("Entraron / fallidos"); color: Theme.textDim; font.pixelSize: 11 }
            Text { text: (panel.v.entraron || 0) + " / " + (panel.v.fallidos || 0); color: Theme.text; font { family: Theme.mono; pixelSize: 11 } }
            Text { text: qsTr("HD al dar la palabra"); color: Theme.textDim; font.pixelSize: 11 }
            Text {
                text: panel.v.hd && panel.v.hd.cuantos > 0 ? qsTr("%1 ms (p95 %2)").arg(panel.v.hd.p50).arg(panel.v.hd.p95) : "–"
                color: Theme.text; font { family: Theme.mono; pixelSize: 11 }
            }
        }
        // Para un generador externo: la URL y la credencial de prueba (vence al terminar).
        ColumnLayout {
            Layout.fillWidth: true
            visible: panel.ps.corriendo && panel.ps.generador === "externo" && panel.ps.credencial !== ""
            spacing: 4
            Text {
                Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.textDim; font.pixelSize: 11
                text: qsTr("Generador externo: entrar a %1 con la credencial de prueba (vale solo mientras dure la prueba):").arg(panel.ps.urlIngreso)
            }
            TextField { Layout.fillWidth: true; readOnly: true; selectByMouse: true; text: panel.ps.comandoExterno; font.family: Theme.mono; font.pixelSize: 11 }
        }

        // «Entrada directa para todos»: visible, con su advertencia, solo durante la prueba.
        Rectangle {
            Layout.fillWidth: true
            visible: panel.ps.corriendo
            implicitHeight: directaFila.implicitHeight + 16
            radius: 10
            color: panel.ps.entradaDirecta ? Qt.rgba(1, 0.25, 0.2, 0.18) : "transparent"
            border.color: panel.ps.entradaDirecta ? Theme.live : Theme.glassBorder
            RowLayout {
                id: directaFila
                anchors { fill: parent; margins: 8 }
                spacing: 8
                Toggle {
                    checked: panel.ps.entradaDirecta
                    onToggled: (quiere) => {
                        if (!quiere) { panel.ps.setEntradaDirecta(false); return }
                        ApplicationWindow.window.confirm({
                            title: qsTr("Entrada directa para todos"),
                            text: qsTr("Cualquiera que tenga el enlace y el PIN entra a la conferencia SIN pasar por el lobby, no solo los de prueba. Se apaga sola al terminar la prueba o al cerrar el programa. ¿La prendo?"),
                            accept: qsTr("Prender"), reject: qsTr("Cancelar"),
                            action: () => { const e = panel.ps.setEntradaDirecta(true); if (e) Studio.notify(e, "error") }
                        })
                    }
                }
                Text {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    font.pixelSize: 12
                    color: panel.ps.entradaDirecta ? Theme.live : Theme.text
                    text: panel.ps.entradaDirecta ? qsTr("ENTRADA DIRECTA PARA TODOS: cualquiera entra sin lobby")
                                                  : qsTr("Entrada directa para todos (apagada: los reales pasan por el lobby)")
                }
            }
        }
        GlassButton {
            visible: panel.ps.corriendo
            text: qsTr("Terminar la prueba")
            onClicked: panel.ps.parar()
        }

        // --- La última ------------------------------------------------------------------
        Text {
            Layout.fillWidth: true
            visible: !panel.ps.corriendo && panel.ult.nota !== undefined
            wrapMode: Text.WordWrap
            font.pixelSize: 11
            color: panel.ult.bien ? Theme.ok : Theme.amber
            text: panel.ult.nota !== undefined ? qsTr("Última: %1 (en el evento: %2)").arg(panel.ult.nota).arg(panel.ult.archivo || "–") : ""
        }
    }
}
