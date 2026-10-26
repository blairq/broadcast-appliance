// El oído de cada agente: qué reconocedor de voz a texto usa cada consumidor
// (pestaña Agentes, arriba de los roles).
//
// Dos motores, y ninguno reemplaza al otro (agent/Reconocedores): Parakeet (por
// frases, el de fábrica) y Nemotron (streaming: muchas voces a la vez, texto
// parcial, se lo puede interrumpir en el tótem). Cada uno se carga cuando
// alguno lo usa; si están los dos a la vez, son dos modelos en memoria y la
// alerta lo dice con quién usa cada uno.
//
// Todo sale de Studio.voz.reconocedores (propiedad con NOTIFY) y se cambia con
// elegirReconocedor / setInterrumpir; lo mismo por MCP: voz_reconocedores.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import BA

Panel {
    id: panel
    readonly property var voz: Studio.voz
    readonly property var r: panel.voz ? panel.voz.reconocedores : ({})
    readonly property var motores: [{ id: "parakeet", nombre: "Parakeet (por frases)" },
                                    { id: "nemotron", nombre: "Nemotron (streaming)" }]
    visible: !!panel.voz && panel.voz.disponible
    padding: 12
    implicitHeight: col.implicitHeight + 2 * padding

    function motor(id) {
        const ms = panel.r.motores || []
        for (let i = 0; i < ms.length; ++i) if (ms[i].id === id) return ms[i]
        return null
    }
    function estadoDe(m) {
        if (!m) return ""
        if (m.cargando) return "cargando…"
        if (m.cargado) return "en memoria · " + m.usos + (m.usos === 1 ? " uso" : " usos")
        if (m.error !== "") return "no cargó"
        return m.instalado ? "sin usar" : "sin bajar"
    }
    function elegir(consumidor, motor) {
        const err = panel.voz.elegirReconocedor(consumidor, motor)
        if (err !== "") Studio.notify(err, "error")
    }

    ColumnLayout {
        id: col
        anchors.fill: parent // el margen lo pone Panel (`padding`)
        spacing: 8

        RowLayout {
            Layout.fillWidth: true
            spacing: 10
            Text { text: "Oído (voz a texto)"; color: Theme.text; font { pixelSize: 14; weight: Font.DemiBold } }
            Repeater {
                model: panel.motores
                delegate: StatusPill {
                    required property var modelData
                    readonly property var m: panel.motor(modelData.id)
                    text: modelData.nombre.split(" ")[0] + ": " + panel.estadoDe(m)
                    dot: m && m.cargado ? Theme.ok : (m && m.cargando ? Theme.accent : Theme.textFaint)
                    pulse: !!m && m.cargando
                    ToolTip.visible: hov.hovered
                    ToolTip.text: (m && m.error !== "" ? m.error + "\n" : "") + "~" + (m ? m.memoria_mb : 0) +
                                  " MB en memoria cuando está cargado"
                    HoverHandler { id: hov }
                }
            }
            Item { Layout.fillWidth: true }
            Text {
                text: "Cada uno elige con qué escucha. Un motor se carga si alguien lo usa y se suelta a los " +
                      (panel.r.margen_s || 120) + " s sin uso."
                color: Theme.textFaint; font.pixelSize: 11
                elide: Text.ElideRight; Layout.maximumWidth: 520
            }
        }

        // Los dos a la vez: dos modelos en memoria. Se puede seguir así, pero conviene unificar.
        Text {
            Layout.fillWidth: true
            visible: (panel.r.alerta || "") !== ""
            text: "⚠ " + (panel.r.alerta || "")
            color: Theme.amber; font.pixelSize: 12
            wrapMode: Text.WordWrap
        }
        RowLayout {
            visible: (panel.r.alerta || "") !== ""
            spacing: 8
            GlassButton {
                compact: true; text: "Todos con Parakeet"
                onClicked: { for (const c of (panel.r.consumidores || [])) panel.elegir(c.id, "parakeet") }
            }
            GlassButton {
                compact: true; text: "Todos con Nemotron"
                enabled: !!panel.motor("nemotron") && panel.motor("nemotron").instalado
                onClicked: { for (const c of (panel.r.consumidores || [])) panel.elegir(c.id, "nemotron") }
            }
        }

        Repeater {
            model: panel.r.consumidores || []
            delegate: RowLayout {
                id: fila
                required property var modelData
                readonly property var m: panel.motor(fila.modelData.motor)
                Layout.fillWidth: true
                spacing: 12
                Text {
                    Layout.preferredWidth: 330
                    text: fila.modelData.titulo
                    color: Theme.text; font.pixelSize: 12
                    elide: Text.ElideRight
                    ToolTip.visible: filaHov.hovered
                    ToolTip.text: fila.modelData.descripcion
                    HoverHandler { id: filaHov }
                }
                ComboBox {
                    Layout.preferredWidth: 220
                    model: panel.motores
                    textRole: "nombre"; valueRole: "id"
                    currentIndex: fila.modelData.motor === "nemotron" ? 1 : 0
                    onActivated: (i) => panel.elegir(fila.modelData.id, panel.motores[i].id)
                }
                Text {
                    text: fila.modelData.usos > 0 ? (fila.modelData.usos + " escuchando") : "—"
                    color: fila.modelData.usos > 0 ? Theme.ok : Theme.textFaint
                    font.pixelSize: 11
                    Layout.preferredWidth: 90
                }
                Text {
                    visible: !!fila.m && !fila.m.instalado
                    text: "Falta bajarlo (Configuración → Modelos locales)"
                    color: Theme.warn; font.pixelSize: 11
                }
                // Solo donde hay cancelación de eco (el tótem) y con Nemotron, que sigue
                // oyendo mientras habla el asistente.
                Toggle {
                    visible: fila.modelData.interrumpible
                    enabled: fila.modelData.motor === "nemotron"
                    checked: fila.modelData.interrumpir
                    onToggled: (w) => {
                        const err = panel.voz.setInterrumpir(fila.modelData.id, w)
                        if (err !== "") Studio.notify(err, "error")
                    }
                }
                Text {
                    visible: fila.modelData.interrumpible
                    text: fila.modelData.motor === "nemotron" ? "La persona puede interrumpir al asistente"
                                                              : "Interrumpir al asistente: solo con Nemotron"
                    color: fila.modelData.motor === "nemotron" ? Theme.textDim : Theme.textFaint
                    font.pixelSize: 11
                }
                Item { Layout.fillWidth: true }
            }
        }
    }
}
