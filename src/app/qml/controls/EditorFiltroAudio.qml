// Agregar o editar un filtro de AUDIO de una entrada (su cadena de GStreamer):
// un módulo de GStreamer (compresor, ecualizador, eco…) con sus propiedades, o
// una tarjeta de audio de un plugin de Python (baplugin medio=audio).
// Guarda toda la lista con VideoGrafos.ponerFiltrosAudioUi (la demora que
// agregue se compensa sola).
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import BA

Popup {
    id: ed
    property var vg: null
    property string entrada: ""
    property var lista: []      // los filtros de la entrada (como los guarda)
    property int indice: -1     // -1: uno nuevo
    // Lo que se está armando:
    property string tipo: "modulo"        // modulo | plugin
    property string elemento: ""
    property var valores: ({})            // propiedad -> valor (lo que cambió)
    property string pluginId: ""
    property string tarjeta: ""
    property string parametros: "{}"
    property int esperaMs: 25
    property string filtroTexto: ""
    signal guardado()

    anchors.centerIn: Overlay.overlay
    width: Math.min((parent ? parent.width : 900) - 60, 720)
    height: Math.min((parent ? parent.height : 700) - 60, 640)
    modal: true
    padding: 16
    background: Rectangle { color: Theme.panel; radius: Theme.radiusSmall; border.color: Theme.glassBorder }

    readonly property var modulos: vg ? vg.modulosAudio() : []
    readonly property var modulosVisibles: modulos.filter(m => filtroTexto === "" ||
        (m.elemento + " " + (m.nombre || "") + " " + (m.descripcion || "")).toLowerCase().indexOf(filtroTexto.toLowerCase()) >= 0)
    // Las tarjetas de audio de los plugins de Python instalados.
    readonly property var tarjetasPlugins: {
        const out = []
        const l = Studio.plugins ? Studio.plugins.lista : []
        for (const p of l)
            for (const t of (p.tarjetas || []))
                if (t.medio === "audio") out.push({ plugin: p.id, tarjeta: t.nombre, texto: (p.nombre || p.id) + " · " + t.nombre,
                                                    parametros: t.parametros || {} })
        return out
    }
    readonly property var props: tipo === "modulo" && elemento !== "" && vg ? vg.propiedadesModuloCon(elemento, valores) : []

    /// Abre para editar el filtro `i` de `l` (o uno nuevo con i = -1).
    function abrir(l, i) {
        lista = l.slice()
        indice = i
        valores = ({}); elemento = ""; pluginId = ""; tarjeta = ""; parametros = "{}"; esperaMs = 25; filtroTexto = ""
        tipo = "modulo"
        if (i >= 0) {
            const f = l[i]
            const pr = f.propiedades || {}
            if (f.elemento === "baplugin") {
                tipo = "plugin"
                pluginId = pr.plugin || ""; tarjeta = pr.tarjeta || ""
                parametros = pr.parametros || "{}"; esperaMs = parseInt(pr["espera-ms"] || "25")
            } else {
                elemento = f.elemento
                valores = Object.assign({}, pr)
            }
        }
        open()
    }

    function guardar() {
        let f
        if (tipo === "modulo") {
            if (elemento === "") { Studio.notify("Elegí un módulo", "error"); return }
            f = { elemento: elemento, propiedades: valores }
        } else {
            if (pluginId === "" || tarjeta === "") { Studio.notify("Elegí una tarjeta de un plugin", "error"); return }
            try { JSON.parse(parametros) } catch (e) { Studio.notify("Los parámetros no son JSON válido", "error"); return }
            f = { elemento: "baplugin", propiedades: { plugin: pluginId, tarjeta: tarjeta, medio: "audio",
                                                        parametros: parametros, "espera-ms": String(esperaMs) } }
        }
        const l = lista.slice()
        if (indice >= 0) { f.id = l[indice].id; l[indice] = f } else { f.id = "a" + Date.now() % 100000; l.push(f) }
        const e = vg.ponerFiltrosAudioUi(entrada, l)
        if (e !== "") { Studio.notify(e, "error"); return }
        Studio.notify("Filtro de audio guardado (la demora se compensa sola)", "info")
        guardado()
        close()
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 10
        RowLayout {
            Layout.fillWidth: true
            Text {
                text: (ed.indice >= 0 ? "Editar" : "Agregar") + " un filtro de audio · " + ed.entrada
                color: Theme.text; font { pixelSize: 16; weight: Font.DemiBold } Layout.fillWidth: true; elide: Text.ElideRight
            }
            GlassButton { compact: true; text: "Cancelar"; onClicked: ed.close() }
            GlassButton { compact: true; active: true; text: "Guardar"; onClicked: ed.guardar() }
        }
        RowLayout {
            spacing: 6
            GlassButton { compact: true; active: ed.tipo === "modulo"; text: "Módulo de GStreamer"; onClicked: ed.tipo = "modulo" }
            GlassButton { compact: true; active: ed.tipo === "plugin"; text: "Tarjeta de un plugin de Python"; onClicked: ed.tipo = "plugin" }
        }

        // --- Un módulo de GStreamer ---------------------------------------------
        RowLayout {
            visible: ed.tipo === "modulo"
            Layout.fillWidth: true
            spacing: 8
            TextField {
                Layout.preferredWidth: 200
                placeholderText: "Buscar (compresor, eq, eco…)"
                text: ed.filtroTexto
                onTextEdited: ed.filtroTexto = text
            }
            ComboBox {
                Layout.fillWidth: true
                model: ed.modulosVisibles
                textRole: "elemento"
                displayText: ed.elemento !== "" ? ed.elemento : "— elegí un módulo —"
                delegate: ItemDelegate {
                    required property var modelData
                    width: ListView.view ? ListView.view.width : 300
                    text: modelData.elemento + "  —  " + (modelData.titulo || modelData.nombre || "")
                }
                onActivated: (i) => { ed.elemento = ed.modulosVisibles[i].elemento; ed.valores = ({}) }
            }
        }
        Text {
            visible: ed.tipo === "modulo" && ed.elemento !== ""
            Layout.fillWidth: true; wrapMode: Text.WordWrap
            text: { const m = ed.modulos.find(x => x.elemento === ed.elemento); return m ? (m.para || m.descripcion || "") : "" }
            color: Theme.textFaint; font.pixelSize: 11
        }
        ScrollView {
            visible: ed.tipo === "modulo"
            Layout.fillWidth: true
            Layout.fillHeight: true
            contentWidth: availableWidth
            clip: true
            FormularioPropiedades {
                width: parent ? parent.width : 400
                props: ed.props
                onCambio: (n, v) => { const x = Object.assign({}, ed.valores); x[n] = String(v); ed.valores = x }
            }
        }

        // --- Una tarjeta de audio de un plugin -----------------------------------
        ColumnLayout {
            visible: ed.tipo === "plugin"
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 8
            Text {
                visible: ed.tarjetasPlugins.length === 0
                Layout.fillWidth: true; wrapMode: Text.WordWrap
                text: "Ningún plugin de Python instalado tiene tarjetas de audio (@plugin.tarjeta(nombre, medio=\"audio\")). Ver Plugins."
                color: Theme.textFaint; font.pixelSize: 12
            }
            ComboBox {
                visible: ed.tarjetasPlugins.length > 0
                Layout.fillWidth: true
                model: ed.tarjetasPlugins
                textRole: "texto"
                displayText: ed.tarjeta !== "" ? ed.pluginId + " · " + ed.tarjeta : "— elegí una tarjeta —"
                onActivated: (i) => {
                    const t = ed.tarjetasPlugins[i]
                    ed.pluginId = t.plugin; ed.tarjeta = t.tarjeta
                    // Los parámetros de la tarjeta, con sus valores de fábrica.
                    const d = {}
                    for (const k in t.parametros) d[k] = (t.parametros[k] && t.parametros[k].defecto !== undefined) ? t.parametros[k].defecto : t.parametros[k]
                    ed.parametros = JSON.stringify(d)
                }
            }
            Text { text: "Parámetros de la tarjeta (JSON)"; color: Theme.textDim; font.pixelSize: 12 }
            TextArea {
                Layout.fillWidth: true
                Layout.preferredHeight: 110
                text: ed.parametros
                font.family: Theme.mono
                wrapMode: TextEdit.Wrap
                onEditingFinished: ed.parametros = text
                background: Rectangle { color: Qt.rgba(1, 1, 1, 0.05); radius: 4 }
            }
            RowLayout {
                spacing: 8
                Text { text: "Espera máxima por bloque (ms)"; color: Theme.textDim; font.pixelSize: 12 }
                SpinBox { from: 0; to: 200; value: ed.esperaMs; editable: true; onValueModified: ed.esperaMs = value }
                Text {
                    Layout.fillWidth: true; wrapMode: Text.WordWrap
                    text: "Si el plugin no devuelve el bloque a tiempo, sale el anterior: nunca traba la entrada. Esta espera se declara como demora y se compensa."
                    color: Theme.textFaint; font.pixelSize: 11
                }
            }
            Item { Layout.fillHeight: true }
        }
    }
}
