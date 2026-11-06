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
    readonly property var rp: ps.reparto
    property bool copiado: false
    function revisar() { panel.ps.revisarMaquina(cuantos.value, conHinchada.checked, varias.checked ? otras.value : 0, estaGenera.checked) }
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
                onValueModified: panel.revisar()
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
            onToggled: panel.revisar()
        }
        // Cuánto decodifica el probador de lo que vuelve (de fábrica, una muestra).
        OpcionDecodificar {
            id: decodificar
            Layout.fillWidth: true
            visible: !panel.ps.corriendo
            pct: panel.ps.muestraDeFabrica
            pctMin: panel.ps.muestraMinPct
            pctMax: panel.ps.muestraMaxPct
        }
        // Repartir los N entre esta máquina y otras (hinchada coordina).
        CheckBox {
            id: varias
            visible: !panel.ps.corriendo && conHinchada.checked
            text: qsTr("Repartir entre varias máquinas")
            onToggled: panel.revisar()
        }
        RowLayout {
            Layout.fillWidth: true
            visible: !panel.ps.corriendo && varias.checked && varias.visible
            spacing: 8
            Text { text: qsTr("Otras máquinas"); color: Theme.textDim; font.pixelSize: 12 }
            SpinBox { id: otras; from: 1; to: panel.ps.maxOtrasMaquinas; value: 1; editable: true; onValueModified: panel.revisar() }
            CheckBox {
                id: estaGenera
                checked: true
                text: qsTr("Esta también genera")
                onToggled: panel.revisar()
                ToolTip.visible: hovered; ToolTip.delay: 400
                ToolTip.text: qsTr("Sin tildar, esta máquina solo coordina: la app medida no comparte la CPU con el generador.")
            }
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
                    const err = panel.ps.arrancar(cuantos.value, minutos.value, conHinchada.checked, hablan.value,
                                                  decodificar.modo, decodificar.pct,
                                                  varias.checked && varias.visible ? otras.value : 0, estaGenera.checked)
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
            Text { text: qsTr("CPU del probador (acá)"); color: Theme.textDim; font.pixelSize: 11 }
            Text {
                text: panel.pct(panel.u.cpuGenerador) + "  ·  " + qsTr("decodifica: %1").arg(
                          panel.ps.decodificacion.modo === "nada" ? qsTr("nada")
                          : panel.ps.decodificacion.modo === "todo" ? qsTr("todo")
                          : qsTr("muestra %1 %").arg(panel.ps.decodificacion.pctEfectivo))
                color: Theme.text; font { family: Theme.mono; pixelSize: 11 }
            }
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
        // Los dos lados en vivo: esta máquina (la medida) y las que generan.
        GraficasPrueba {
            Layout.fillWidth: true
            visible: panel.ps.corriendo && panel.ps.serie.length > 1
            serie: panel.ps.serie
            deSala: true
        }
        // Varias máquinas: el código para la otra app (o la línea de hinchada) y quién se sumó.
        ColumnLayout {
            Layout.fillWidth: true
            visible: panel.ps.corriendo && panel.rp.activo === true
            spacing: 4
            Text {
                Layout.fillWidth: true; wrapMode: Text.WordWrap; font.pixelSize: 12
                color: panel.rp.sumadasOtras >= panel.rp.otras ? Theme.ok : Theme.amber
                text: !panel.rp.escuchando ? qsTr("Preparando el coordinador (la primera vez graba su video unos minutos)…")
                      : qsTr("Se sumaron %1 de %2 máquinas · a cada una le tocan %3 participantes").arg(panel.rp.sumadasOtras).arg(panel.rp.otras).arg(panel.rp.porMaquina)
            }
            Text {
                Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.textDim; font.pixelSize: 11
                visible: (panel.rp.codigo || "") !== ""
                text: qsTr("En cada otra PC con BroadcastAppliance: Pruebas → «Generar para otro equipo», pegá este código. Sin el programa, corré la línea en la carpeta de hinchada (bin/64bit/hinchada)%1. Dejá pasar el puerto %2 en el firewall de esta máquina.")
                          .arg(panel.rp.procesosPorOtra > 1 ? qsTr(", %1 veces (una por terminal)").arg(panel.rp.procesosPorOtra) : "")
                          .arg((panel.rp.direccion || "").split(":").pop())
            }
            RowLayout {
                Layout.fillWidth: true
                visible: (panel.rp.codigo || "") !== ""
                spacing: 6
                TextField { Layout.fillWidth: true; readOnly: true; selectByMouse: true; echoMode: TextInput.Password; text: panel.rp.codigo || ""; font.family: Theme.mono; font.pixelSize: 11 }
                GlassButton { compact: true; text: panel.copiado ? qsTr("Copiado ✓") : qsTr("Copiar el código"); onClicked: { Studio.copiar(panel.rp.codigo); panel.copiado = true } }
            }
            TextField { Layout.fillWidth: true; visible: (panel.rp.comando || "") !== ""; readOnly: true; selectByMouse: true; text: panel.rp.comando || ""; font.family: Theme.mono; font.pixelSize: 11 }
            TextField { Layout.fillWidth: true; visible: (panel.rp.comandoWindows || "") !== ""; readOnly: true; selectByMouse: true; text: panel.rp.comandoWindows || ""; font.family: Theme.mono; font.pixelSize: 11 }
            Repeater {
                model: panel.rp.generadoras || []
                delegate: Text {
                    required property var modelData
                    color: modelData.cpu > 90 ? Theme.live : Theme.textDim; font.pixelSize: 11
                    text: qsTr("Generadora %1: CPU %2 %").arg(modelData.maquina).arg(Math.round(modelData.cpu))
                }
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
