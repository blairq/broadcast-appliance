// El menú del programador de superresolución (VideoGrafos.programadorUi /
// configurarProgramadorUi): qué cámaras usan la red según el programa y el
// preview, cuánto aguanta cada acelerador (medido) y lo que se puede configurar.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import BA

Popup {
    id: pop
    property var vg: null
    property var p: ({})
    readonly property var entradas: {
        const e = p.entradas || {}
        return Object.keys(e).sort().map(k => Object.assign({ entrada: k }, e[k]))
    }
    /// Las entradas cuya red no llega a tiempo (alarma, y bypass si está prendido).
    readonly property var tardes: {
        const out = []
        const ent = pop.p.entradas || {}
        for (const k in ent) if (ent[k].tarde) out.push({ entrada: k, ms: ent[k].ms, bypass_s: ent[k].bypass_s || 0 })
        return out
    }
    readonly property var presupuesto: {
        const b = p.presupuesto || {}
        return Object.keys(b).map(k => Object.assign({ acelerador: k }, b[k]))
    }

    anchors.centerIn: Overlay.overlay
    width: Math.min((parent ? parent.width : 1000) - 60, 900)
    height: Math.min((parent ? parent.height : 760) - 60, 720)
    modal: true
    padding: 18
    background: Rectangle { color: Theme.panel; radius: Theme.radiusSmall; border.color: Theme.glassBorder }

    function refrescar() { p = vg ? vg.programadorUi() : ({}) }
    function poner(cambios, ok) {
        const e = vg.configurarProgramadorUi(cambios)
        if (e !== "") Studio.notify(e, "error"); else if (ok) Studio.notify(ok, "info")
        refrescar()
    }
    function nombreAcelerador(a) {
        return ({ "neural-engine": "Neural Engine", gpu: "GPU", cpu: "Procesador", cuda: "NVIDIA (CUDA/TensorRT)", dml: "DirectML",
                  migraphx: "AMD (MIGraphX)", rocm: "AMD (ROCm)", openvino: "Intel (OpenVINO)", videotoolbox: "VideoToolbox" })[a] || a
    }
    Timer { interval: 1000; running: pop.opened; repeat: true; triggeredOnStart: true; onTriggered: pop.refrescar() }

    ColumnLayout {
        anchors.fill: parent
        spacing: 10
        RowLayout {
            Layout.fillWidth: true
            Text { text: "Programador de superresolución"; color: Theme.text; font { pixelSize: 18; weight: Font.DemiBold } Layout.fillWidth: true }
            Text { text: pop.p.activo !== false ? "Prendido" : "Apagado: todas las redes trabajan"; color: Theme.textDim; font.pixelSize: 12 }
            Toggle { checked: pop.p.activo !== false; onToggled: (w) => pop.poner({ activo: w }, w ? "Programador prendido" : "Programador apagado") }
            GlassButton { compact: true; text: "Cerrar"; onClicked: pop.close() }
        }
        Text {
            Layout.fillWidth: true; wrapMode: Text.WordWrap
            color: Theme.textDim; font.pixelSize: 12
            text: "Las redes de todas las cámaras quedan cargadas y calientes; trabajan las que se ven grandes en el programa o en el preview " +
                  "(la escena armada), hasta lo que aguante cada acelerador. Al pasar una cámara a preview la red entra de a poco: sale al aire ya " +
                  "mejorada, sin saltos ni cuadros negros."
        }

        ScrollView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            contentWidth: availableWidth
            clip: true
            ColumnLayout {
                width: parent ? parent.width : 800
                spacing: 12

                // --- Capacidad --------------------------------------------------------
                Text { text: "Capacidad"; color: Theme.text; font { pixelSize: 14; weight: Font.DemiBold } }
                RowLayout {
                    spacing: 8
                    GlassButton { compact: true; active: pop.p.capacidad !== "fija"; text: "Medida (automática)"; onClicked: pop.poner({ capacidad: "auto" }, "") }
                    GlassButton { compact: true; active: pop.p.capacidad === "fija"; text: "Fija"; onClicked: pop.poner({ capacidad: "fija" }, "") }
                    Text { text: pop.p.capacidad === "fija" ? "Redes a la vez" : "Tope de redes a la vez"; color: Theme.textDim; font.pixelSize: 12 }
                    SpinBox {
                        from: 0; to: 16; editable: true
                        value: pop.p.max_activas !== undefined ? pop.p.max_activas : 3
                        onValueModified: pop.poner({ max_activas: value }, "")
                    }
                }
                RowLayout {
                    visible: pop.p.capacidad !== "fija"
                    Layout.fillWidth: true
                    spacing: 8
                    Text { text: "Uso de cada acelerador"; color: Theme.textDim; font.pixelSize: 12 }
                    Slider {
                        id: uso
                        Layout.preferredWidth: 220
                        from: 30; to: 100; stepSize: 5
                        value: pop.p.uso_pct !== undefined ? pop.p.uso_pct : 85
                        onPressedChanged: if (!pressed) pop.poner({ uso_pct: value }, "")
                    }
                    Text { text: Math.round(uso.value) + " % de cada cuadro"; color: Theme.textFaint; font.pixelSize: 11 }
                    Item { Layout.fillWidth: true }
                }
                Text {
                    visible: pop.p.capacidad !== "fija"
                    Layout.fillWidth: true; wrapMode: Text.WordWrap
                    color: Theme.textFaint; font.pixelSize: 11
                    text: "Cada acelerador corre de a una red por vez: en un cuadro (" + (pop.p.cuadro_ms || 33.3) + " ms) le entran las redes cuyo " +
                          "tiempo medido sume hasta ese porcentaje. Lo que ya está prendido tiene un margen para no titilar; lo nuevo espera 2 s a que la medición se asiente."
                }
                Repeater {
                    model: pop.presupuesto
                    delegate: RowLayout {
                        id: pr
                        required property var modelData
                        spacing: 8
                        Text { text: pop.nombreAcelerador(pr.modelData.acelerador); color: Theme.text; font.pixelSize: 12; Layout.preferredWidth: 180 }
                        ProgressBar {
                            Layout.preferredWidth: 260
                            from: 0; to: pr.modelData.disponible_ms || 1
                            value: Math.min(pr.modelData.usado_ms || 0, pr.modelData.disponible_ms || 1)
                        }
                        Text {
                            text: Number(pr.modelData.usado_ms || 0).toFixed(1) + " de " + Number(pr.modelData.disponible_ms || 0).toFixed(1) + " ms"
                            color: (pr.modelData.usado_ms || 0) > (pr.modelData.disponible_ms || 0) ? Theme.amber : Theme.textDim
                            font.pixelSize: 11
                        }
                    }
                }

                // --- Si no llega ---------------------------------------------------------
                Rectangle {
                    Layout.fillWidth: true
                    visible: pop.tardes.length > 0
                    implicitHeight: tardeTxt.implicitHeight + 16
                    radius: 8
                    color: Qt.rgba(0.85, 0.15, 0.15, 0.25)
                    border.color: Theme.live
                    Text {
                        id: tardeTxt
                        anchors { left: parent.left; right: parent.right; verticalCenter: parent.verticalCenter; margins: 10 }
                        wrapMode: Text.WordWrap; color: Theme.text; font.pixelSize: 13; font.weight: Font.DemiBold
                        text: "No llega: " + pop.tardes.map(t => "«" + t.entrada + "» (" + t.ms + " ms" +
                              (t.bypass_s ? ", pasada a barato " + t.bypass_s + " s" : "") + ")").join(", ")
                    }
                }
                RowLayout {
                    spacing: 8
                    Switch { checked: pop.p.bypass_auto !== false; onToggled: pop.poner({ bypass_auto: checked }, "") }
                    Text { text: "Bypass automático"; color: Theme.text; font.pixelSize: 12 }
                    Text { text: "cuando tarda más del"; color: Theme.textDim; font.pixelSize: 12 }
                    SpinBox { from: 30; to: 200; stepSize: 5; editable: true
                              value: pop.p.umbral_pct !== undefined ? pop.p.umbral_pct : 80
                              onValueModified: pop.poner({ umbral_pct: value }, "") }
                    Text { text: "% del cuadro durante 2 s: pasa sola a barato y vuelve a probar (15 s, 30 s, 1 min…)"
                           color: Theme.textFaint; font.pixelSize: 11; Layout.fillWidth: true; wrapMode: Text.WordWrap }
                }

                // --- Cuándo ------------------------------------------------------------
                Rectangle { Layout.fillWidth: true; height: 1; color: Theme.glassBorder }
                Text { text: "Cuándo y cómo"; color: Theme.text; font { pixelSize: 14; weight: Font.DemiBold } }
                CheckBox {
                    text: "Prender la red al pasar la cámara a preview (si no, recién al salir al aire)"
                    checked: pop.p.en_preview !== false
                    onToggled: pop.poner({ en_preview: checked }, "")
                }
                RowLayout {
                    spacing: 8
                    Text { text: "Lo que se ve pero no tiene turno"; color: Theme.textDim; font.pixelSize: 12 }
                    GlassButton { compact: true; active: pop.p.sin_turno !== "reposo"; text: "Afilado"; onClicked: pop.poner({ sin_turno: "barato" }, "") }
                    GlassButton { compact: true; active: pop.p.sin_turno === "reposo"; text: "Agrandado común"; onClicked: pop.poner({ sin_turno: "reposo" }, "") }
                }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8
                    Text { text: "Prender si se ve agrandada más de"; color: Theme.textDim; font.pixelSize: 12 }
                    Slider {
                        id: umb
                        Layout.preferredWidth: 200
                        from: 0.5; to: 3.0; stepSize: 0.05
                        value: pop.p.umbral_aumento !== undefined ? pop.p.umbral_aumento : 1.1
                        onPressedChanged: if (!pressed) pop.poner({ umbral_aumento: value }, "")
                    }
                    Text { text: "x" + Number(umb.value).toFixed(2) + " de la cámara"; color: Theme.textFaint; font.pixelSize: 11 }
                    Item { Layout.fillWidth: true }
                }
                RowLayout {
                    spacing: 8
                    Text { text: "Transición"; color: Theme.textDim; font.pixelSize: 12 }
                    SpinBox {
                        from: 0; to: 3000; stepSize: 50; editable: true
                        value: pop.p.transicion_ms !== undefined ? pop.p.transicion_ms : 400
                        onValueModified: pop.poner({ transicion_ms: value }, "")
                    }
                    Text { text: "ms para que la red entre o salga de a poco"; color: Theme.textFaint; font.pixelSize: 11 }
                }

                // --- Cada cámara ----------------------------------------------------------
                Rectangle { Layout.fillWidth: true; height: 1; color: Theme.glassBorder }
                Text { text: "Cámaras"; color: Theme.text; font { pixelSize: 14; weight: Font.DemiBold } }
                Text {
                    Layout.fillWidth: true; wrapMode: Text.WordWrap
                    color: Theme.textFaint; font.pixelSize: 11
                    text: "programa: " + (pop.p.programa || "—") + " · preview: " + (pop.p.preview || "—") +
                          (pop.entradas.length === 0 ? " · Ninguna entrada tiene superresolución (tarjeta «Superresolución» en su grafo)." : "")
                }
                Repeater {
                    model: pop.entradas
                    delegate: RowLayout {
                        id: ent
                        required property var modelData
                        Layout.fillWidth: true
                        spacing: 10
                        Text { text: ent.modelData.entrada; color: Theme.text; font.pixelSize: 12; Layout.preferredWidth: 170; elide: Text.ElideRight }
                        StatusPill {
                            text: ent.modelData.modo === "activo" ? "red" : ent.modelData.modo === "barato" ? "afilado" : "reposo"
                            dot: ent.modelData.modo === "activo" ? Theme.ok : ent.modelData.modo === "barato" ? Theme.amber : Theme.textFaint
                            medidaDe: "afilado"
                        }
                        Text {
                            Layout.fillWidth: true; elide: Text.ElideRight
                            text: (ent.modelData.programa ? "al aire · " : "") + (ent.modelData.preview ? "en preview · " : "") +
                                  "x" + Number(ent.modelData.aumento || 0).toFixed(2) + " · " +
                                  (ent.modelData.ms > 0 ? Number(ent.modelData.ms).toFixed(1) + " ms" : "sin medir") +
                                  " en " + pop.nombreAcelerador(ent.modelData.acelerador || "?")
                            color: Theme.textDim; font.pixelSize: 11
                        }
                        ComboBox {
                            implicitWidth: 170
                            model: [ { t: "Automático", v: "auto" }, { t: "Siempre con red", v: "siempre" }, { t: "Nunca con red", v: "nunca" } ]
                            textRole: "t"
                            currentIndex: Math.max(0, model.findIndex(o => o.v === (ent.modelData.forzado || "auto")))
                            onActivated: (i) => {
                                const f = {}
                                f[ent.modelData.entrada] = model[i].v
                                pop.poner({ forzar: f }, "")
                            }
                        }
                    }
                }
            }
        }
    }
}
