// Página Plugins: los programas en Python que le suman al sistema lo que no
// trae (equipos raros, tarjetas de video y audio, automatizaciones).
//
// Lo mismo que hace el asistente por MCP, a mano:
//   - pedirle al asistente un plugin nuevo describiendo el equipo o la idea;
//   - copiar un ejemplo;
//   - prender, apagar, reiniciar, probar sin tocar el aire;
//   - ver el registro y el código;
//   - cambiar la configuración;
//   - llamar sus controles;
//   - poner sus tarjetas en una entrada o una fuente;
//   - ver qué equipos hay conectados y los eventos que publican.
// La fuente es una sola, Studio.plugins (plugins/Plugins.cpp).
import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import BA
import "../controls"

Rectangle {
    id: page
    color: Theme.bg
    property real leftInset: 92
    readonly property var pl: Studio.plugins
    property string abierto: ""          // id del plugin desplegado
    property string vista: "resumen"     // resumen | registro | codigo | config
    property string textoVista: ""
    property var respuestas: ({})        // "id/control" -> {texto, ok}
    property var configForm: ({})        // el "config" de plugin.json, para el formulario
    property bool configJson: false      // editarlo como JSON en vez de formulario

    function colorDeEstado(e) {
        switch (e) {
        case "corriendo": return Theme.ok
        case "arrancando": case "dependencias": case "reiniciando": return Theme.amber
        case "caido": return Theme.live
        default: return Theme.textFaint
        }
    }
    function nombreDeEstado(e) {
        switch (e) {
        case "corriendo": return "corriendo"
        case "arrancando": return "arrancando…"
        case "dependencias": return "bajando bibliotecas…"
        case "reiniciando": return "reiniciando…"
        case "caido": return "caído"
        default: return "apagado"
        }
    }
    function cargarVista() {
        if (page.abierto === "") return
        if (page.vista === "registro") page.textoVista = page.pl.registroUi(page.abierto)
        else if (page.vista === "codigo") page.textoVista = page.pl.codigo(page.abierto)
        else if (page.vista === "config") {
            page.textoVista = page.pl.configTexto(page.abierto)
            try { const c = JSON.parse(page.textoVista); page.configForm = (c && typeof c === "object" && !Array.isArray(c)) ? c : ({}) }
            catch (e) { page.configForm = ({}); page.configJson = true }
        }
    }
    function corriendo() {
        let n = 0
        for (const p of page.pl.lista) if (p.estado === "corriendo") ++n
        return n
    }
    FileDialog {
        id: importarDlg
        title: "Plugin exportado (.zip)"
        nameFilters: ["Plugin (*.zip)"]
        onAccepted: page.pl.importarUi(selectedFile)
    }
    Connections {
        target: page.pl
        function onAviso(texto, ok) { Studio.notify(texto, ok ? "info" : "error") }
        function onRespuesta(id, control, texto, ok) {
            const r = Object.assign({}, page.respuestas)
            r[id + "/" + control] = { texto: texto, ok: ok }
            page.respuestas = r
        }
    }
    // El registro se refresca solo mientras se mira.
    Timer {
        interval: 2000; repeat: true
        running: page.visible && page.abierto !== "" && page.vista === "registro"
        onTriggered: page.cargarVista()
    }

    ScrollView {
        id: scroll
        anchors { fill: parent; leftMargin: page.leftInset; margins: 20 }
        contentWidth: availableWidth
        clip: true

        ColumnLayout {
            width: scroll.availableWidth
            spacing: 12

            // --- Encabezado ---------------------------------------------------------
            RowLayout {
                Layout.fillWidth: true
                spacing: 10
                Text { text: "Plugins"; color: Theme.text; font { pixelSize: 20; weight: Font.DemiBold } }
                StatusPill {
                    text: page.corriendo() + " corriendo de " + page.pl.lista.length
                    dot: page.corriendo() > 0 ? Theme.ok : Theme.textFaint
                }
                StatusPill {
                    visible: !page.pl.hayPython
                    text: "falta Python en el programa"; dot: Theme.live
                }
                Item { Layout.fillWidth: true }
                GlassButton {
                    compact: true; text: "Importar…"; icon: "⇩"
                    onClicked: importarDlg.open()
                    ToolTip.visible: hovered; ToolTip.delay: 400; ToolTip.text: "Instalar un plugin exportado en otro aparato (.zip)"
                }
                GlassButton { compact: true; text: "Abrir la carpeta"; icon: "▤"; onClicked: page.pl.abrirCarpeta("") }
            }
            Text {
                Layout.fillWidth: true
                text: "Un plugin es un programa en Python que corre aparte (si se cae, la emisión sigue) y puede ser una entrada de video o audio, " +
                      "una tarjeta del grafo, el PTZ de una cámara que se maneja de forma rara, un canal de datos (quién habla) o una automatización. " +
                      "Viven en " + page.pl.carpetaTexto + " y no se pierden al actualizar."
                color: Theme.textDim; font.pixelSize: 12; wrapMode: Text.WordWrap
            }

            // --- Pedirle uno al asistente ------------------------------------------
            Panel {
                Layout.fillWidth: true
                padding: 12
                implicitHeight: pedirCol.implicitHeight + 2 * padding
                ColumnLayout {
                    id: pedirCol
                    anchors.fill: parent
                    spacing: 8
                    Text { text: "Pedirle un plugin al asistente ✦"; color: Theme.text; font { pixelSize: 14; weight: Font.DemiBold } }
                    Text {
                        Layout.fillWidth: true
                        text: "Contale qué equipo es o qué necesitás (por ejemplo: «tengo una Logitech Rally por USB y quiero manejar su PTZ», " +
                              "«juntá las dos Kandao y que se vea quien habla»). Lo escribe, lo prueba sin tocar el aire y lo deja andando."
                        color: Theme.textDim; font.pixelSize: 11; wrapMode: Text.WordWrap
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8
                        TextField {
                            id: pedido
                            Layout.fillWidth: true
                            placeholderText: Studio.agent.running ? "Qué tiene que hacer el plugin…" : "Abrí el asistente ✦ para poder pedírselo"
                            color: Theme.text; font.pixelSize: 12
                            onAccepted: mandarPedido.clicked()
                        }
                        GlassButton {
                            id: mandarPedido
                            compact: true; text: "Pedírselo"; active: true
                            enabled: Studio.agent.running && pedido.text.trim() !== ""
                            onClicked: {
                                Studio.agent.send("Hacé un plugin de Python para esto (help {\"topic\": \"plugins\"}): " + pedido.text.trim() +
                                                  ". Probalo con plugin_probar antes de activarlo y contame cómo se usa.")
                                pedido.text = ""
                                Studio.notify("Se lo pasé al asistente ✦", "info")
                            }
                        }
                    }
                }
            }

            // --- Los plugins ---------------------------------------------------------
            RowLayout {
                Layout.fillWidth: true
                Text { text: "Instalados"; color: Theme.text; font { pixelSize: 15; weight: Font.DemiBold } }
                Item { Layout.fillWidth: true }
                ComboBox {
                    id: ejemploBox
                    Layout.preferredWidth: 260
                    model: page.pl.ejemplos
                    textRole: "nombre"
                    displayText: currentIndex < 0 ? "Copiar un ejemplo…" : currentText
                    currentIndex: -1
                    ToolTip.visible: hovered && currentIndex >= 0
                    ToolTip.text: currentIndex >= 0 ? page.pl.ejemplos[currentIndex].descripcion : ""
                }
                TextField {
                    id: nuevoId
                    Layout.preferredWidth: 160
                    placeholderText: "id nuevo (mi-camara)"
                    color: Theme.text; font.pixelSize: 12
                    validator: RegularExpressionValidator { regularExpression: /[a-z0-9-]{0,40}/ }
                }
                GlassButton {
                    compact: true; text: "Copiar"; icon: "＋"
                    enabled: ejemploBox.currentIndex >= 0 && nuevoId.text !== ""
                    onClicked: {
                        const e = page.pl.copiarEjemplo(page.pl.ejemplos[ejemploBox.currentIndex].ejemplo, nuevoId.text)
                        if (e !== "") Studio.notify(e, "error")
                        else { page.abierto = nuevoId.text; page.vista = "resumen"; nuevoId.text = ""; ejemploBox.currentIndex = -1 }
                    }
                }
            }

            Text {
                visible: page.pl.lista.length === 0
                text: "Todavía no hay plugins. Pedile uno al asistente o copiá un ejemplo."
                color: Theme.textFaint; font.pixelSize: 12
            }

            Repeater {
                model: page.pl.lista
                delegate: Panel {
                    id: tarjeta
                    required property var modelData
                    readonly property bool abierta: page.abierto === modelData.id
                    readonly property var prueba: page.pl.pruebas[modelData.id]
                    Layout.fillWidth: true
                    padding: 12
                    implicitHeight: col.implicitHeight + 2 * padding

                    ColumnLayout {
                        id: col
                        anchors.fill: parent
                        spacing: 8

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 12
                            Toggle {
                                checked: tarjeta.modelData.estado !== "apagado" && tarjeta.modelData.estado !== "caido"
                                onToggled: (wanted) => page.pl.prender(tarjeta.modelData.id, wanted)
                            }
                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 1
                                RowLayout {
                                    spacing: 8
                                    Text { text: tarjeta.modelData.nombre; color: Theme.text; font { pixelSize: 14; weight: Font.DemiBold } }
                                    Text { text: tarjeta.modelData.id + (tarjeta.modelData.version ? " · v" + tarjeta.modelData.version : ""); color: Theme.textFaint; font.pixelSize: 11 }
                                }
                                Text {
                                    Layout.fillWidth: true
                                    text: tarjeta.modelData.descripcion || ""
                                    color: Theme.textDim; font.pixelSize: 11
                                    wrapMode: Text.WordWrap; maximumLineCount: tarjeta.abierta ? 20 : 2; elide: Text.ElideRight
                                }
                                Text {
                                    Layout.fillWidth: true
                                    visible: !!tarjeta.modelData.dice
                                    text: tarjeta.modelData.dice ? "dice: " + tarjeta.modelData.dice.texto : ""
                                    color: tarjeta.modelData.dice && tarjeta.modelData.dice.ok === false ? Theme.warn : Theme.textDim
                                    font.pixelSize: 11; wrapMode: Text.WordWrap
                                }
                                Text {
                                    Layout.fillWidth: true
                                    visible: !!tarjeta.modelData.error
                                    text: tarjeta.modelData.error || ""
                                    color: Theme.live; font.pixelSize: 11; wrapMode: Text.WordWrap
                                }
                            }
                            StatusPill {
                                text: page.nombreDeEstado(tarjeta.modelData.estado) + (tarjeta.modelData.reinicios ? " · " + tarjeta.modelData.reinicios + " reinicios" : "")
                                dot: page.colorDeEstado(tarjeta.modelData.estado)
                                pulse: tarjeta.modelData.estado === "arrancando" || tarjeta.modelData.estado === "dependencias"
                                medidaDe: "bajando bibliotecas…"
                            }
                            GlassButton {
                                compact: true; text: "Probar"; icon: "✓"
                                enabled: !(tarjeta.prueba && tarjeta.prueba.probando)
                                ToolTip.visible: hovered; ToolTip.text: "Lo corre aparte, sin tocar el aire, y dice si anda"
                                onClicked: { page.pl.probarUi(tarjeta.modelData.id); page.abierto = tarjeta.modelData.id; page.vista = "resumen" }
                            }
                            GlassButton {
                                compact: true; text: "Reiniciar"; icon: "↻"
                                enabled: tarjeta.modelData.estado !== "apagado"
                                onClicked: page.pl.reiniciarUi(tarjeta.modelData.id)
                            }
                            GlassButton {
                                compact: true; text: tarjeta.abierta ? "Cerrar" : "Ver"; icon: tarjeta.abierta ? "▴" : "▾"
                                onClicked: { page.abierto = tarjeta.abierta ? "" : tarjeta.modelData.id; page.vista = "resumen" }
                            }
                        }

                        // Lo que aporta, siempre a la vista.
                        Flow {
                            Layout.fillWidth: true
                            spacing: 6
                            Repeater {
                                model: tarjeta.modelData.entradas || []
                                delegate: StatusPill {
                                    required property var modelData
                                    text: "entrada: " + modelData.nombre + (modelData.video ? " 🎥" : "") + (modelData.audio ? " 🔊" : "")
                                    dot: Theme.accent
                                }
                            }
                            Repeater {
                                model: tarjeta.modelData.tarjetas || []
                                delegate: StatusPill {
                                    required property var modelData
                                    text: "tarjeta de " + (modelData.medio === "audio" ? "audio" : "video") + ": " + modelData.nombre
                                    dot: Theme.amber
                                }
                            }
                            Repeater {
                                model: tarjeta.modelData.ptz || []
                                delegate: StatusPill { required property var modelData; text: "PTZ de " + modelData.entrada; dot: Theme.ok }
                            }
                            Repeater {
                                model: tarjeta.modelData.escucha || []
                                delegate: StatusPill { required property var modelData; text: "escucha " + modelData; dot: Theme.textDim }
                            }
                        }

                        // --- Desplegado -------------------------------------------
                        ColumnLayout {
                            visible: tarjeta.abierta
                            Layout.fillWidth: true
                            spacing: 8

                            RowLayout {
                                spacing: 6
                                Repeater {
                                    model: [["resumen", "Usar"], ["registro", "Registro"], ["codigo", "Código"], ["config", "Configuración"]]
                                    delegate: GlassButton {
                                        required property var modelData
                                        compact: true; text: modelData[1]
                                        active: page.vista === modelData[0]
                                        onClicked: { page.vista = modelData[0]; page.cargarVista() }
                                    }
                                }
                                Item { Layout.fillWidth: true }
                                GlassButton { compact: true; text: "Carpeta"; icon: "▤"; onClicked: page.pl.abrirCarpeta(tarjeta.modelData.id) }
                                GlassButton {
                                    compact: true; text: "Exportar"; icon: "⇧"
                                    onClicked: page.pl.exportarUi(tarjeta.modelData.id)
                                    ToolTip.visible: hovered; ToolTip.delay: 400; ToolTip.text: "Un .zip para instalarlo en otro aparato (sin sus bibliotecas ni datos)"
                                }
                                GlassButton {
                                    compact: true; text: "Borrar"; icon: "✕"
                                    onClicked: {
                                        const e = page.pl.borrarUi(tarjeta.modelData.id)
                                        if (e !== "") Studio.notify(e, "error")
                                        else { Studio.notify("Se movió a Plugins/.papelera", "info"); page.abierto = "" }
                                    }
                                }
                            }

                            // Usar: prueba, controles y tarjetas.
                            ColumnLayout {
                                visible: page.vista === "resumen"
                                Layout.fillWidth: true
                                spacing: 8

                                Text {
                                    visible: !!tarjeta.prueba
                                    Layout.fillWidth: true
                                    wrapMode: Text.WordWrap; font.pixelSize: 12
                                    color: !tarjeta.prueba ? Theme.textDim : tarjeta.prueba.probando ? Theme.amber : tarjeta.prueba.ok ? Theme.ok : Theme.live
                                    text: {
                                        const r = tarjeta.prueba
                                        if (!r) return ""
                                        if (r.probando) return "Probando…"
                                        let t = (r.ok ? "✓ La prueba anduvo" : "✕ La prueba falló") + " (" + (r.cuando || "") + ")"
                                        if (r.error) t += ": " + r.error
                                        for (const x of (r.tarjetas || []))
                                            t += "\n  tarjeta " + x.tarjeta + ": " + (x.error ? x.error : (x.ms_promedio + " ms por cuadro" + (x.aviso_tiempo ? " — " + x.aviso_tiempo : "")))
                                        for (const x of (r.entradas || []))
                                            t += "\n  entrada " + x.entrada + (x.medio ? " (" + x.medio + ")" : "") + ": " +
                                                 (x.error ? x.error : x.gst ? "pipeline de GStreamer (se arma al prenderlo)" : x.por_segundo + " por segundo")
                                        return t
                                    }
                                }

                                // Su panel (plugin.panel): lo primero que se usa.
                                Panel {
                                    visible: !!tarjeta.modelData.panel
                                    Layout.fillWidth: true
                                    padding: 12
                                    implicitHeight: panelCol.implicitHeight + 2 * padding
                                    ColumnLayout {
                                        id: panelCol
                                        anchors.fill: parent
                                        spacing: 6
                                        Text {
                                            text: tarjeta.modelData.panel && tarjeta.modelData.panel.titulo ? tarjeta.modelData.panel.titulo : "Panel"
                                            color: Theme.text; font { pixelSize: 13; weight: Font.DemiBold }
                                        }
                                        PanelPlugin {
                                            Layout.fillWidth: true
                                            pluginId: tarjeta.modelData.id
                                            controles: tarjeta.modelData.panel ? tarjeta.modelData.panel.controles : []
                                            corriendo: tarjeta.modelData.estado === "corriendo"
                                        }
                                    }
                                }

                                Text {
                                    visible: (tarjeta.modelData.controles || []).length > 0
                                    text: tarjeta.modelData.panel ? "Más acciones" : "Acciones"; color: Theme.text; font { pixelSize: 13; weight: Font.DemiBold }
                                }
                                Repeater {
                                    model: tarjeta.modelData.controles || []
                                    delegate: ColumnLayout {
                                        id: ctl
                                        required property var modelData
                                        readonly property var resp: page.respuestas[tarjeta.modelData.id + "/" + modelData.nombre]
                                        Layout.fillWidth: true
                                        spacing: 4
                                        // Lo escrito en cada parámetro (clave -> texto); se arma el JSON al tocar.
                                        property var escrito: ({})
                                        readonly property var claves: Object.keys(modelData.parametros || {})
                                        function argumentos() {
                                            const o = {}
                                            for (const k of ctl.claves) {
                                                const t = (ctl.escrito[k] || "").trim()
                                                if (t === "") continue
                                                // Un número, una lista o sí/no se mandan como tales; lo demás, como texto.
                                                try { o[k] = JSON.parse(t) } catch (e) { o[k] = t }
                                            }
                                            return JSON.stringify(o)
                                        }
                                        RowLayout {
                                            Layout.fillWidth: true
                                            spacing: 8
                                            GlassButton {
                                                compact: true; text: ctl.modelData.nombre; icon: "▶"
                                                enabled: tarjeta.modelData.estado === "corriendo"
                                                onClicked: page.pl.controlUi(tarjeta.modelData.id, ctl.modelData.nombre, ctl.argumentos())
                                            }
                                            Text {
                                                Layout.fillWidth: true
                                                text: ctl.modelData.descripcion || ""
                                                color: Theme.textDim; font.pixelSize: 11; elide: Text.ElideRight
                                            }
                                        }
                                        Repeater {
                                            model: ctl.claves
                                            delegate: RowLayout {
                                                id: par
                                                required property string modelData
                                                readonly property var ejemplo: ctl.modelData.parametros[modelData]
                                                Layout.fillWidth: true
                                                Layout.leftMargin: 24
                                                spacing: 8
                                                Text { text: par.modelData; color: Theme.textDim; font.pixelSize: 11; Layout.preferredWidth: 120; elide: Text.ElideRight }
                                                TextField {
                                                    Layout.fillWidth: true
                                                    color: Theme.text; font.pixelSize: 11
                                                    // Un ejemplo numérico es el valor de fábrica; un texto, cómo se escribe.
                                                    placeholderText: typeof par.ejemplo === "string" ? par.ejemplo : JSON.stringify(par.ejemplo)
                                                    onTextEdited: { const e = ctl.escrito; e[par.modelData] = text; ctl.escrito = e }
                                                    onAccepted: page.pl.controlUi(tarjeta.modelData.id, ctl.modelData.nombre, ctl.argumentos())
                                                    background: Rectangle { color: Theme.field; radius: 6 }
                                                }
                                            }
                                        }
                                        Text {
                                            visible: !!ctl.resp
                                            Layout.fillWidth: true
                                            text: ctl.resp ? ctl.resp.texto : ""
                                            color: ctl.resp && !ctl.resp.ok ? Theme.live : Theme.textDim
                                            font { pixelSize: 11; family: "Menlo" }
                                            wrapMode: Text.WrapAnywhere; maximumLineCount: 12; elide: Text.ElideRight
                                        }
                                    }
                                }

                                Text {
                                    visible: (tarjeta.modelData.tarjetas || []).length > 0
                                    text: "Poner sus tarjetas"; color: Theme.text; font { pixelSize: 13; weight: Font.DemiBold }
                                }
                                Repeater {
                                    model: tarjeta.modelData.tarjetas || []
                                    delegate: RowLayout {
                                        id: tj
                                        required property var modelData
                                        readonly property bool audio: modelData.medio === "audio"
                                        Layout.fillWidth: true
                                        spacing: 8
                                        Text {
                                            text: tj.modelData.nombre; color: Theme.text; font.pixelSize: 12
                                            Layout.preferredWidth: 120; elide: Text.ElideRight
                                        }
                                        ComboBox {
                                            id: destino
                                            Layout.preferredWidth: 240
                                            model: tj.audio ? page.pl.fuentesConAudio() : page.pl.entradasVideo()
                                            displayText: currentIndex < 0 ? (tj.audio ? "¿en qué fuente?" : "¿en qué entrada?") : currentText
                                            currentIndex: -1
                                        }
                                        GlassButton {
                                            compact: true; text: tj.audio ? "Poner como filtro" : "Poner en el grafo"
                                            enabled: destino.currentIndex >= 0 && tarjeta.modelData.estado === "corriendo"
                                            onClicked: {
                                                const e = page.pl.tarjetaUi(tarjeta.modelData.id, tj.modelData.nombre, destino.currentText, tj.audio)
                                                Studio.notify(e !== "" ? e : "Listo: «" + tj.modelData.nombre + "» en " + destino.currentText, e !== "" ? "error" : "info")
                                            }
                                        }
                                        Text {
                                            Layout.fillWidth: true
                                            text: tj.modelData.descripcion || ""
                                            color: Theme.textDim; font.pixelSize: 11; elide: Text.ElideRight
                                        }
                                    }
                                }
                            }

                            // Registro y código: solo lectura.
                            ScrollView {
                                visible: page.vista === "registro" || page.vista === "codigo"
                                Layout.fillWidth: true
                                Layout.preferredHeight: 320
                                TextArea {
                                    readOnly: true
                                    text: page.textoVista
                                    color: Theme.text; font { pixelSize: 11; family: "Menlo" }
                                    wrapMode: TextEdit.NoWrap
                                    background: Rectangle { color: Theme.field; radius: 6 }
                                }
                            }
                            Text {
                                visible: page.vista === "codigo"
                                Layout.fillWidth: true
                                text: "Para cambiar el código, pedíselo al asistente (o editá plugin.py en su carpeta y reinicialo)."
                                color: Theme.textFaint; font.pixelSize: 11; wrapMode: Text.WordWrap
                            }

                            // Configuración: las claves (cifradas) y el "config" de plugin.json.
                            ColumnLayout {
                                visible: page.vista === "config"
                                Layout.fillWidth: true
                                spacing: 6
                                Text {
                                    visible: claves.count > 0
                                    text: "Claves (se guardan cifradas; el plugin las lee con plugin.secreto)"
                                    color: Theme.text; font { pixelSize: 13; weight: Font.DemiBold }
                                }
                                Repeater {
                                    id: claves
                                    model: page.vista === "config" && tarjeta.abierta ? page.pl.secretosUi(tarjeta.modelData.id) : []
                                    delegate: RowLayout {
                                        id: sec
                                        required property var modelData
                                        Layout.fillWidth: true
                                        spacing: 8
                                        Text { text: sec.modelData.etiqueta; color: Theme.text; font.pixelSize: 12; Layout.preferredWidth: 200; elide: Text.ElideRight }
                                        TextField {
                                            id: valorSec
                                            Layout.fillWidth: true
                                            echoMode: TextInput.Password
                                            placeholderText: sec.modelData.cargada ? "cargada ✓ (escribí para cambiarla)" : "sin cargar"
                                            color: Theme.text; font.pixelSize: 12
                                        }
                                        GlassButton {
                                            compact: true; text: "Guardar"
                                            enabled: valorSec.text !== ""
                                            onClicked: {
                                                const e = page.pl.guardarSecreto(tarjeta.modelData.id, sec.modelData.clave, valorSec.text)
                                                Studio.notify(e !== "" ? e : "Clave guardada (cifrada)", e !== "" ? "error" : "info")
                                                if (e === "") valorSec.text = ""
                                            }
                                        }
                                    }
                                }
                                RowLayout {
                                    Layout.fillWidth: true
                                    Text {
                                        Layout.fillWidth: true
                                        text: "Lo que el plugin lee en plugin.config (número de serie, IP, modo…). Al guardar se le pasa en vivo."
                                        color: Theme.textDim; font.pixelSize: 11; wrapMode: Text.WordWrap
                                    }
                                    GlassButton {
                                        compact: true; text: page.configJson ? "Como formulario" : "Como JSON"
                                        onClicked: {
                                            if (page.configJson) { try { page.configForm = JSON.parse(configArea.text) } catch (e) { Studio.notify("El JSON no es válido", "error"); return } }
                                            else configArea.text = JSON.stringify(page.configForm, null, 2)
                                            page.configJson = !page.configJson
                                        }
                                    }
                                }
                                // Un campo por clave, según lo que tenga: sí/no, número, texto (lo demás, en JSON).
                                Repeater {
                                    model: page.configJson ? [] : Object.keys(page.configForm)
                                    delegate: RowLayout {
                                        id: cf
                                        required property string modelData
                                        readonly property var v: page.configForm[modelData]
                                        readonly property string clase: typeof v === "boolean" ? "si_no" : typeof v === "number" ? "numero" : typeof v === "string" ? "texto" : "json"
                                        Layout.fillWidth: true
                                        spacing: 8
                                        Text { text: cf.modelData; color: Theme.textDim; font.pixelSize: 12; Layout.preferredWidth: 200; elide: Text.ElideRight }
                                        Toggle {
                                            visible: cf.clase === "si_no"
                                            checked: cf.v === true
                                            onToggled: (quiero) => { const c = page.configForm; c[cf.modelData] = quiero; page.configForm = Object.assign({}, c) }
                                        }
                                        TextField {
                                            visible: cf.clase !== "si_no"
                                            Layout.fillWidth: true
                                            color: Theme.text; font { pixelSize: 12; family: cf.clase === "json" ? Theme.mono : Theme.font }
                                            text: cf.clase === "json" ? JSON.stringify(cf.v) : String(cf.v)
                                            validator: cf.clase === "numero" ? numValido : null
                                            DoubleValidator { id: numValido; locale: "C" }
                                            onEditingFinished: {
                                                const c = page.configForm
                                                if (cf.clase === "numero") c[cf.modelData] = Number(text)
                                                else if (cf.clase === "json") { try { c[cf.modelData] = JSON.parse(text) } catch (e) { Studio.notify("«" + cf.modelData + "»: JSON no válido", "error"); return } }
                                                else c[cf.modelData] = text
                                                page.configForm = Object.assign({}, c)
                                            }
                                            background: Rectangle { color: Theme.field; radius: 6 }
                                        }
                                    }
                                }
                                Text {
                                    visible: !page.configJson && Object.keys(page.configForm).length === 0
                                    text: "Este plugin no tiene configuración."
                                    color: Theme.textFaint; font.pixelSize: 12
                                }
                                ScrollView {
                                    visible: page.configJson
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: 220
                                    TextArea {
                                        id: configArea
                                        text: page.vista === "config" ? page.textoVista : ""
                                        color: Theme.text; font { pixelSize: 12; family: "Menlo" }
                                        background: Rectangle { color: Theme.field; radius: 6 }
                                    }
                                }
                                RowLayout {
                                    Item { Layout.fillWidth: true }
                                    GlassButton {
                                        compact: true; text: "Guardar configuración"; active: true
                                        onClicked: {
                                            const e = page.pl.guardarConfig(tarjeta.modelData.id, page.configJson ? configArea.text : JSON.stringify(page.configForm, null, 2))
                                            Studio.notify(e !== "" ? e : "Configuración guardada", e !== "" ? "error" : "info")
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }

            // --- Equipos conectados y eventos -----------------------------------------
            RowLayout {
                Layout.fillWidth: true
                Layout.topMargin: 6
                spacing: 12

                Panel {
                    Layout.fillWidth: true
                    Layout.alignment: Qt.AlignTop
                    padding: 12
                    implicitHeight: equiposCol.implicitHeight + 2 * padding
                    ColumnLayout {
                        id: equiposCol
                        anchors.fill: parent
                        spacing: 6
                        RowLayout {
                            Layout.fillWidth: true
                            Text { text: "Equipos conectados"; color: Theme.text; font { pixelSize: 14; weight: Font.DemiBold } }
                            Item { Layout.fillWidth: true }
                            GlassButton {
                                compact: true; text: page.pl.buscandoEquipos ? "Buscando…" : "Buscar"; icon: "⌕"
                                enabled: !page.pl.buscandoEquipos
                                onClicked: page.pl.buscarEquipos()
                            }
                        }
                        Text {
                            visible: Object.keys(page.pl.equipos).length === 0
                            Layout.fillWidth: true
                            text: "Cámaras, micrófonos, USB, HID y puertos serie: el primer paso para saber qué es un equipo raro y por dónde se le habla."
                            color: Theme.textFaint; font.pixelSize: 11; wrapMode: Text.WordWrap
                        }
                        Repeater {
                            model: page.pl.equipos.camaras_y_microfonos || []
                            delegate: ColumnLayout {
                                required property var modelData
                                Layout.fillWidth: true
                                spacing: 0
                                Text {
                                    text: (modelData.clase.indexOf("Video") >= 0 ? "🎥 " : "🎙 ") + modelData.nombre
                                    color: Theme.text; font.pixelSize: 12
                                }
                                TextEdit {
                                    text: modelData.gst || ""; readOnly: true; selectByMouse: true
                                    color: Theme.textDim; font { pixelSize: 10; family: "Menlo" }
                                }
                            }
                        }
                        Repeater {
                            model: page.pl.equipos.usb || []
                            delegate: Text {
                                required property var modelData
                                text: "USB " + modelData.vid + ":" + modelData.pid + "  " + (modelData.fabricante || "") + " " + (modelData.producto || "") + (modelData.uvc ? "  (cámara UVC)" : "")
                                color: Theme.textDim; font.pixelSize: 11
                            }
                        }
                        Text {
                            visible: Object.keys(page.pl.equipos).length > 0
                            text: (page.pl.equipos.hid || []).length + " equipos HID · " + (page.pl.equipos.serie || []).length + " puertos serie" +
                                  ((page.pl.equipos.usb || []).length === 0 ? " · nada por USB" : "")
                            color: Theme.textFaint; font.pixelSize: 11
                        }
                    }
                }

                Panel {
                    Layout.fillWidth: true
                    Layout.alignment: Qt.AlignTop
                    padding: 12
                    implicitHeight: eventosCol.implicitHeight + 2 * padding
                    ColumnLayout {
                        id: eventosCol
                        anchors.fill: parent
                        spacing: 4
                        Text { text: "Lo que publican (eventos)"; color: Theme.text; font { pixelSize: 14; weight: Font.DemiBold } }
                        Text {
                            visible: page.pl.eventosRecientes.length === 0
                            text: "Nada todavía. Un plugin publica con plugin.evento(«hablando», {…})."
                            color: Theme.textFaint; font.pixelSize: 11
                        }
                        Repeater {
                            model: page.pl.eventosRecientes.slice(0, 12)
                            delegate: Text {
                                required property var modelData
                                Layout.fillWidth: true
                                text: (modelData.recibido || "").slice(11, 19) + "  " + modelData.de + "/" + modelData.canal + "  " + JSON.stringify(modelData.datos)
                                color: Theme.textDim; font { pixelSize: 11; family: "Menlo" }
                                elide: Text.ElideRight
                            }
                        }
                    }
                }
            }
        }
    }
}
