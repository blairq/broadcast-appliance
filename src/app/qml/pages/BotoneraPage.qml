// Página Botonera: la superficie de control estilo Bitfocus Companion.
//
//   - Editor: perfiles (cada uno es un puesto, con sus herramientas
//     permitidas), páginas y la grilla de celdas; tocar una celda la edita
//     (tipo, texto, colores, acción, ligaduras al bus de eventos) y «Probar»
//     la dispara como si la apretaran.
//   - Aparatos: emparejar una tablet por QR (la aprobación es SOLO acá), los
//     emparejados y los de hardware de los plugins (Stream Deck, MIDI…), con
//     su perfil, y revocar.
//   - Eventos: lo que publica la app en el bus (para ligaduras y plugins).
//   - Auditoría: cada emparejamiento, acción, rechazo y edición.
// Lo mismo se hace por MCP (botonera_*, eventos_*). La fuente es Studio.botonera.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import BA
import "../controls"

Rectangle {
    id: page
    color: Theme.bg
    property real leftInset: 92
    readonly property var bot: Studio.botonera
    property string pestana: "editor"   // editor | aparatos | eventos | auditoria
    property string perfilId: ""
    property string paginaId: ""
    property var render: ({})
    property int selX: -1
    property int selY: -1
    property var emparejo: ({})
    property double emparejoHasta: 0
    property string filtroEventos: ""

    function perfilActual() {
        for (const p of page.bot.perfiles) if (p.id === page.perfilId) return p
        return null
    }
    function refrescar() {
        if (page.perfilId === "" && page.bot.perfiles.length > 0) page.perfilId = page.bot.perfiles[0].id
        const p = perfilActual()
        if (p && !p.paginas.some(g => g.id === page.paginaId)) page.paginaId = p.paginas.length ? p.paginas[0].id : ""
        page.render = page.perfilId !== "" ? page.bot.paginaUi(page.perfilId, page.paginaId) : ({})
    }
    function celdaRender(x, y) {
        for (const c of (page.render.celdas || []))
            if (x >= c.x && x < c.x + c.ancho && y >= c.y && y < c.y + c.alto) return c
        return null
    }
    function elegir(x, y) {
        const c = celdaRender(x, y)
        page.selX = c ? c.x : x
        page.selY = c ? c.y : y
        const txt = page.bot.celdaJsonUi(page.perfilId, page.paginaId, page.selX, page.selY)
        editor.cargar(txt)
    }
    Component.onCompleted: refrescar()
    onVisibleChanged: if (visible) refrescar()
    Connections {
        target: page.bot
        function onCambio() { page.refrescar() }
    }
    Connections {
        target: Studio
        function onEmparejamientoListo(r) {
            page.emparejo = r
            page.emparejoHasta = r.vence_ms || 0
            if (r.error && !r.url) Studio.notify(r.error, "error")
        }
    }
    // Lo vivo (tally, niveles) se ve en la grilla mientras se mira.
    Timer {
        interval: 250; repeat: true
        running: page.visible && page.pestana === "editor"
        onTriggered: page.render = page.perfilId !== "" ? page.bot.paginaUi(page.perfilId, page.paginaId) : ({})
    }
    Timer {
        id: reloj
        property double ahora: Date.now()
        interval: 500; repeat: true; running: page.visible
        onTriggered: ahora = Date.now()
    }

    ColumnLayout {
        anchors { fill: parent; leftMargin: page.leftInset; margins: 20 }
        spacing: 12

        // --- Encabezado ---------------------------------------------------------
        RowLayout {
            Layout.fillWidth: true
            spacing: 10
            Text {
                text: "Botonera"
                color: Theme.text
                font.pixelSize: 20
                font.weight: Font.DemiBold
            }
            StatusPill {
                text: page.bot.dispositivos.filter(d => d.conectado).length + " conectados de " + page.bot.dispositivos.length
                dot: page.bot.dispositivos.some(d => d.conectado) ? Theme.ok : Theme.textFaint
            }
            StatusPill {
                visible: page.bot.solicitudes.length > 0
                text: page.bot.solicitudes.length + " esperan aprobación"
                dot: Theme.amber
            }
            Item { Layout.fillWidth: true }
            Repeater {
                model: [ { id: "editor", t: "Editor" }, { id: "aparatos", t: "Aparatos" },
                         { id: "eventos", t: "Eventos" }, { id: "auditoria", t: "Auditoría" } ]
                delegate: GlassButton {
                    required property var modelData
                    compact: true
                    text: modelData.t
                    active: page.pestana === modelData.id
                    onClicked: page.pestana = modelData.id
                }
            }
        }
        Text {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            color: Theme.textDim
            font.pixelSize: 12
            text: "Una grilla de botones, faders y perillas en una tablet (emparejada por QR) o en un Stream Deck o controlador MIDI " +
                  "(por plugin). Cada celda dispara una herramienta del sistema y muestra estado vivo. Cada perfil es un puesto con " +
                  "sus herramientas permitidas: lo que no está en la lista, el aparato no lo puede hacer. Lo que cambia el aire, al " +
                  "aire, se confirma en el propio aparato."
        }

        // ========================= EDITOR =========================
        RowLayout {
            visible: page.pestana === "editor"
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 12

            // --- Perfiles y páginas ---
            Panel {
                Layout.preferredWidth: 250
                Layout.fillHeight: true
                padding: 12
                ColumnLayout {
                    anchors.fill: parent
                    spacing: 8
                    Text { text: "Perfiles"; color: Theme.text; font.pixelSize: 14; font.weight: Font.DemiBold }
                    ListView {
                        Layout.fillWidth: true
                        Layout.preferredHeight: Math.min(contentHeight, 180)
                        clip: true
                        model: page.bot.perfiles
                        delegate: GlassButton {
                            required property var modelData
                            width: ListView.view.width
                            compact: true
                            text: modelData.nombre + " · " + modelData.herramientas_permitidas.length + " herr."
                            active: page.perfilId === modelData.id
                            onClicked: { page.perfilId = modelData.id; page.paginaId = ""; page.selX = -1; page.refrescar() }
                        }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        ComboBox {
                            id: plantillaBox
                            Layout.fillWidth: true
                            model: [{ id: "", nombre: "Vacío" }].concat(page.bot.plantillasUi())
                            textRole: "nombre"
                        }
                        GlassButton {
                            compact: true; text: "＋"
                            ToolTip.visible: hovered; ToolTip.delay: 400; ToolTip.text: "Perfil nuevo desde la plantilla elegida"
                            onClicked: {
                                const r = page.bot.crearPerfilUi("", plantillaBox.model[plantillaBox.currentIndex].id)
                                if (r.startsWith("error")) Studio.notify(r, "error")
                                else { page.perfilId = r; page.paginaId = ""; page.refrescar() }
                            }
                        }
                    }
                    Rectangle { Layout.fillWidth: true; height: 1; color: Theme.glassBorder }
                    Text { text: "Páginas"; color: Theme.text; font.pixelSize: 14; font.weight: Font.DemiBold }
                    Flow {
                        Layout.fillWidth: true
                        spacing: 6
                        Repeater {
                            model: page.perfilActual() ? page.perfilActual().paginas : []
                            delegate: GlassButton {
                                required property var modelData
                                compact: true
                                text: modelData.nombre + " (" + modelData.columnas + "×" + modelData.filas + ")"
                                active: page.paginaId === modelData.id
                                onClicked: { page.paginaId = modelData.id; page.selX = -1; page.refrescar() }
                            }
                        }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        TextField { id: pagNombre; Layout.fillWidth: true; placeholderText: "Página nueva"; color: Theme.text }
                        SpinBox { id: pagCols; from: 1; to: 16; value: 5; implicitWidth: 80 }
                        SpinBox { id: pagFilas; from: 1; to: 16; value: 3; implicitWidth: 80 }
                    }
                    RowLayout {
                        GlassButton {
                            compact: true; text: "Crear página"
                            onClicked: {
                                const r = page.bot.crearPaginaUi(page.perfilId, pagNombre.text, pagCols.value, pagFilas.value)
                                if (r.startsWith("error")) Studio.notify(r, "error")
                                else { page.paginaId = r; pagNombre.text = ""; page.refrescar() }
                            }
                        }
                        GlassButton {
                            compact: true; text: "Grilla"
                            ToolTip.visible: hovered; ToolTip.delay: 400; ToolTip.text: "Cambiar columnas y filas de esta página a las de arriba"
                            onClicked: {
                                const e = page.bot.editarPaginaUi(page.perfilId, page.paginaId, { columnas: pagCols.value, filas: pagFilas.value })
                                if (e !== "") Studio.notify(e, "error")
                            }
                        }
                        GlassButton {
                            compact: true; text: "Borrar"
                            onClicked: if (!page.bot.borrarPaginaUi(page.perfilId, page.paginaId)) Studio.notify("Un perfil necesita al menos una página.", "aviso")
                        }
                    }
                    Rectangle { Layout.fillWidth: true; height: 1; color: Theme.glassBorder }
                    Text { text: "Permisos del perfil"; color: Theme.text; font.pixelSize: 14; font.weight: Font.DemiBold }
                    TextArea {
                        id: permisos
                        Layout.fillWidth: true
                        Layout.preferredHeight: 90
                        wrapMode: TextEdit.Wrap
                        color: Theme.text
                        font.pixelSize: 11
                        text: page.perfilActual() ? page.perfilActual().herramientas_permitidas.join(", ") : ""
                        placeholderText: "herramientas separadas por coma (* = todas)"
                    }
                    CheckBox {
                        id: puedeEditar
                        text: "Se puede editar desde el aparato"
                        checked: page.perfilActual() ? page.perfilActual().puede_editar : false
                    }
                    RowLayout {
                        GlassButton {
                            compact: true; text: "Guardar permisos"
                            onClicked: {
                                const l = permisos.text.split(",").map(s => s.trim()).filter(s => s !== "")
                                const e = page.bot.editarPerfilUi(page.perfilId, { herramientas_permitidas: l, puede_editar: puedeEditar.checked })
                                Studio.notify(e === "" ? "Permisos guardados" : e, e === "" ? "ok" : "error")
                            }
                        }
                        GlassButton {
                            compact: true; text: "Exportar"
                            onClicked: { editor.cargar(page.bot.exportarUi(page.perfilId)); editor.modo = "perfil" }
                        }
                    }
                    Item { Layout.fillHeight: true }
                    GlassButton {
                        compact: true; text: "Borrar el perfil"
                        onClicked: ApplicationWindow.window.confirm({
                            title: "¿Borrar el perfil?",
                            text: "Los aparatos que lo tenían quedan sin perfil hasta que les asignes otro.",
                            action: () => { page.bot.borrarPerfilUi(page.perfilId); page.perfilId = ""; page.refrescar() }
                        })
                    }
                }
            }

            // --- La grilla ---
            Panel {
                Layout.fillWidth: true
                Layout.fillHeight: true
                padding: 12
                Item {
                    id: lienzo
                    anchors.fill: parent
                    readonly property int cols: page.render.pagina ? page.render.pagina.columnas : 1
                    readonly property int filas: page.render.pagina ? page.render.pagina.filas : 1
                    readonly property real lado: Math.min(width / cols, height / filas)
                    readonly property real gap: 6
                    // Huecos (para poner una celda nueva).
                    Repeater {
                        model: lienzo.cols * lienzo.filas
                        delegate: Rectangle {
                            required property int index
                            readonly property int cx: index % lienzo.cols
                            readonly property int cy: Math.floor(index / lienzo.cols)
                            x: cx * lienzo.lado + lienzo.gap / 2
                            y: cy * lienzo.lado + lienzo.gap / 2
                            width: lienzo.lado - lienzo.gap
                            height: lienzo.lado - lienzo.gap
                            radius: 10
                            color: "transparent"
                            border.color: page.selX === cx && page.selY === cy ? Theme.accent : Theme.glassBorder
                            border.width: page.selX === cx && page.selY === cy ? 2 : 1
                            Text { anchors.centerIn: parent; text: "＋"; color: Theme.textFaint; font.pixelSize: 18 }
                            MouseArea { anchors.fill: parent; onClicked: page.elegir(parent.cx, parent.cy) }
                        }
                    }
                    // Las celdas, como las ve el aparato.
                    Repeater {
                        model: page.render.celdas || []
                        delegate: Rectangle {
                            required property var modelData
                            readonly property bool sel: page.selX === modelData.x && page.selY === modelData.y
                            x: modelData.x * lienzo.lado + lienzo.gap / 2
                            y: modelData.y * lienzo.lado + lienzo.gap / 2
                            width: modelData.ancho * lienzo.lado - lienzo.gap
                            height: modelData.alto * lienzo.lado - lienzo.gap
                            radius: 10
                            color: modelData.fondo || "#2b2f3a"
                            border.color: sel ? Theme.accent : (modelData.activo ? "white" : Qt.rgba(1, 1, 1, 0.12))
                            border.width: sel || modelData.activo ? 3 : 1
                            opacity: modelData.titilar && reloj.ahora % 1000 < 500 ? 0.45 : 1
                            Text {
                                anchors.fill: parent
                                anchors.margins: 6
                                text: (modelData.icono ? modelData.icono + "\n" : "") + (modelData.texto || "") +
                                      (modelData.tipo === "fader" || modelData.tipo === "perilla" || modelData.tipo === "medidor"
                                       ? "\n" + Number(modelData.valor).toFixed(1) : "")
                                color: modelData.color || "white"
                                font.pixelSize: Math.max(10, Math.min(parent.width / 7, 18))
                                font.weight: Font.DemiBold
                                wrapMode: Text.WordWrap
                                horizontalAlignment: Text.AlignHCenter
                                verticalAlignment: Text.AlignVCenter
                            }
                            Text {
                                anchors { left: parent.left; top: parent.top; margins: 4 }
                                text: modelData.tipo === "boton" ? "" : modelData.tipo
                                color: Qt.rgba(1, 1, 1, 0.5); font.pixelSize: 9
                            }
                            MouseArea {
                                anchors.fill: parent
                                onClicked: page.elegir(modelData.x, modelData.y)
                                onDoubleClicked: page.bot.probarUi(page.perfilId, page.paginaId, modelData.x, modelData.y)
                            }
                        }
                    }
                }
            }

            // --- La celda elegida ---
            Panel {
                Layout.preferredWidth: 360
                Layout.fillHeight: true
                padding: 12
                ColumnLayout {
                    id: editor
                    anchors.fill: parent
                    spacing: 8
                    property string modo: "celda"   // celda | perfil (importar/exportar)
                    property var actual: ({})
                    function cargar(txt) {
                        editor.modo = "celda"
                        let c = {}
                        try { c = txt ? JSON.parse(txt) : {} } catch (e) { c = {} }
                        editor.actual = c
                        const est = c.estilo || {}
                        tipoBox.currentIndex = Math.max(0, tipoBox.model.indexOf(c.tipo || "boton"))
                        textoF.text = est.texto || ""
                        fondoF.text = est.fondo || ""
                        colorF.text = est.color || ""
                        const a = c.accion || {}
                        herrF.editText = a.herramienta || ""
                        argsF.text = a.args ? JSON.stringify(a.args) : ""
                        jsonF.text = txt || ""
                    }
                    function armar() {
                        const c = Object.assign({}, editor.actual)
                        c.x = page.selX
                        c.y = page.selY
                        c.tipo = tipoBox.currentText
                        c.estilo = Object.assign({}, c.estilo || {}, { texto: textoF.text })
                        if (fondoF.text !== "") c.estilo.fondo = fondoF.text
                        if (colorF.text !== "") c.estilo.color = colorF.text
                        if (herrF.editText.trim() !== "") {
                            let args = {}
                            if (argsF.text.trim() !== "") args = JSON.parse(argsF.text)
                            c.accion = { tipo: "herramienta", herramienta: herrF.editText.trim(), args: args }
                        } else {
                            delete c.accion
                        }
                        return c
                    }
                    Text {
                        text: editor.modo === "perfil" ? "Perfil (JSON)" : page.selX < 0 ? "Tocá una celda" : "Celda " + page.selX + ", " + page.selY
                        color: Theme.text; font.pixelSize: 14; font.weight: Font.DemiBold
                    }
                    GridLayout {
                        visible: editor.modo === "celda" && page.selX >= 0
                        columns: 2
                        columnSpacing: 8
                        rowSpacing: 6
                        Layout.fillWidth: true
                        Text { text: "Tipo"; color: Theme.textDim; font.pixelSize: 12 }
                        ComboBox { id: tipoBox; Layout.fillWidth: true; model: ["boton", "fader", "perilla", "medidor", "texto", "reloj"] }
                        Text { text: "Texto"; color: Theme.textDim; font.pixelSize: 12 }
                        TextField { id: textoF; Layout.fillWidth: true; color: Theme.text }
                        Text { text: "Fondo"; color: Theme.textDim; font.pixelSize: 12 }
                        TextField { id: fondoF; Layout.fillWidth: true; color: Theme.text; placeholderText: "#2b2f3a" }
                        Text { text: "Letra"; color: Theme.textDim; font.pixelSize: 12 }
                        TextField { id: colorF; Layout.fillWidth: true; color: Theme.text; placeholderText: "#ffffff" }
                        Text { text: "Herramienta"; color: Theme.textDim; font.pixelSize: 12 }
                        ComboBox {
                            id: herrF
                            Layout.fillWidth: true
                            editable: true
                            model: page.bot.herramientasUi("")
                        }
                        Text { text: "Argumentos"; color: Theme.textDim; font.pixelSize: 12 }
                        TextField {
                            id: argsF
                            Layout.fillWidth: true
                            color: Theme.text
                            placeholderText: "{\"name\": \"Cámara 2\"}  ·  fader: \"{{valor}}\""
                        }
                    }
                    Text {
                        visible: editor.modo === "celda" && page.selX >= 0
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        color: Theme.textFaint
                        font.pixelSize: 11
                        text: "Ligaduras (estado vivo), valor del fader y acciones al soltar o mantener: en el JSON de abajo. " +
                              "Los temas están en la pestaña Eventos. Doble clic en una celda de la grilla: probar."
                    }
                    ScrollView {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        visible: page.selX >= 0 || editor.modo === "perfil"
                        TextArea {
                            id: jsonF
                            wrapMode: TextEdit.NoWrap
                            color: Theme.text
                            font.family: Theme.mono
                            font.pixelSize: 11
                            placeholderText: "{ \"tipo\": \"boton\", \"estado\": [ … ] }"
                        }
                    }
                    RowLayout {
                        visible: editor.modo === "celda" && page.selX >= 0
                        GlassButton {
                            compact: true; text: "Guardar"; active: true
                            onClicked: {
                                let c
                                try { c = editor.armar() } catch (e) { return Studio.notify("Los argumentos no son JSON válido", "error") }
                                const e = page.bot.setCeldaUi(page.perfilId, page.paginaId, JSON.stringify(c))
                                if (e !== "") Studio.notify(e, "error")
                                else editor.cargar(page.bot.celdaJsonUi(page.perfilId, page.paginaId, page.selX, page.selY))
                            }
                        }
                        GlassButton {
                            compact: true; text: "Guardar JSON"
                            onClicked: {
                                let c
                                try { c = JSON.parse(jsonF.text) } catch (e) { return Studio.notify("El JSON no se entiende", "error") }
                                c.x = page.selX
                                c.y = page.selY
                                const e = page.bot.setCeldaUi(page.perfilId, page.paginaId, JSON.stringify(c))
                                if (e !== "") Studio.notify(e, "error")
                            }
                        }
                        GlassButton { compact: true; text: "Probar"; onClicked: page.bot.probarUi(page.perfilId, page.paginaId, page.selX, page.selY) }
                        GlassButton {
                            compact: true; text: "Borrar"
                            onClicked: { page.bot.borrarCeldaUi(page.perfilId, page.paginaId, page.selX, page.selY); editor.cargar("") }
                        }
                    }
                    RowLayout {
                        visible: editor.modo === "perfil"
                        GlassButton {
                            compact: true; text: "Importar como perfil nuevo"
                            onClicked: {
                                const r = page.bot.importarUi(jsonF.text)
                                if (r.startsWith("error")) Studio.notify(r, "error")
                                else { page.perfilId = r; editor.modo = "celda"; page.refrescar(); Studio.notify("Importado (sin permisos: dalos a mano)", "info") }
                            }
                        }
                        GlassButton { compact: true; text: "Cerrar"; onClicked: editor.cargar("") }
                    }
                }
            }
        }

        // ========================= APARATOS =========================
        ScrollView {
            visible: page.pestana === "aparatos"
            Layout.fillWidth: true
            Layout.fillHeight: true
            contentWidth: availableWidth
            clip: true
            ColumnLayout {
                width: parent.width
                spacing: 12

                // Solicitudes que esperan a una persona.
                Repeater {
                    model: page.bot.solicitudes
                    delegate: Panel {
                        required property var modelData
                        Layout.fillWidth: true
                        padding: 12
                        implicitHeight: 56
                        RowLayout {
                            anchors.fill: parent
                            spacing: 10
                            Text {
                                Layout.fillWidth: true
                                text: "«" + modelData.nombre + "» (" + modelData.ip + ") pide emparejarse con el perfil «" + modelData.perfil + "»"
                                color: Theme.amber; font.pixelSize: 14; wrapMode: Text.WordWrap
                            }
                            GlassButton { compact: true; text: "Aprobar"; active: true; onClicked: Studio.aprobarTabletUi(modelData.id, true) }
                            GlassButton { compact: true; text: "Rechazar"; onClicked: Studio.aprobarTabletUi(modelData.id, false) }
                        }
                    }
                }

                Panel {
                    Layout.fillWidth: true
                    padding: 14
                    implicitHeight: empCol.implicitHeight + 2 * padding
                    ColumnLayout {
                        id: empCol
                        anchors.fill: parent
                        spacing: 10
                        Text { text: "Emparejar una tablet"; color: Theme.text; font.pixelSize: 15; font.weight: Font.DemiBold }
                        RowLayout {
                            spacing: 8
                            ComboBox {
                                id: empPerfil
                                Layout.preferredWidth: 220
                                model: page.bot.perfiles
                                textRole: "nombre"
                            }
                            TextField { id: empNombre; Layout.preferredWidth: 200; placeholderText: "Nombre (iPad Dirección)"; color: Theme.text }
                            Text { text: "Vence sin uso (días)"; color: Theme.textDim; font.pixelSize: 12 }
                            SpinBox { id: empDias; from: 0; to: 365; value: 30; implicitWidth: 100 }
                            GlassButton {
                                compact: true; text: "Generar QR"; active: true
                                enabled: empPerfil.currentIndex >= 0
                                onClicked: Studio.emparejarTabletUi(page.bot.perfiles[empPerfil.currentIndex].id, empNombre.text, empDias.value)
                            }
                        }
                        RowLayout {
                            visible: !!page.emparejo.url
                            spacing: 16
                            Rectangle {
                                width: 240; height: 240; radius: 10; color: "white"
                                Image { anchors.fill: parent; anchors.margins: 8; source: page.emparejo.qr || ""; fillMode: Image.PreserveAspectFit; smooth: false }
                            }
                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 6
                                Text {
                                    readonly property int resta: Math.max(0, Math.round((page.emparejoHasta - reloj.ahora) / 1000))
                                    text: resta > 0 ? "Escaneala con la tablet: vence en " + resta + " s" : "El código venció: generá otro"
                                    color: resta > 0 ? Theme.text : Theme.live; font.pixelSize: 14
                                }
                                TextField { Layout.fillWidth: true; readOnly: true; text: page.emparejo.url || ""; color: Theme.textDim; font.pixelSize: 11 }
                                Text {
                                    visible: !!page.emparejo.huella
                                    Layout.fillWidth: true; wrapMode: Text.WrapAnywhere
                                    text: "El certificado es propio: la tablet avisa la primera vez. Huella para comparar: " + (page.emparejo.huella || "")
                                    color: Theme.textFaint; font.pixelSize: 11
                                }
                                Text {
                                    visible: !!page.emparejo.aviso
                                    Layout.fillWidth: true; wrapMode: Text.WordWrap
                                    text: page.emparejo.aviso || ""
                                    color: Theme.amber; font.pixelSize: 12
                                }
                                Text {
                                    Layout.fillWidth: true; wrapMode: Text.WordWrap
                                    text: "Cuando la tablet lo pida, aparece arriba para aprobar. El código sirve una sola vez."
                                    color: Theme.textFaint; font.pixelSize: 11
                                }
                            }
                        }
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    Text { text: "Aparatos"; color: Theme.text; font.pixelSize: 15; font.weight: Font.DemiBold }
                    Item { Layout.fillWidth: true }
                    GlassButton { compact: true; text: "Revocar todas las tablets"; onClicked: ApplicationWindow.window.confirm({
                        title: "¿Revocar todas las tablets?",
                        text: "Dejan de funcionar ya y hay que volver a emparejarlas con un QR.",
                        action: () => Studio.notify("Revocadas: " + page.bot.revocarUi("todos"), "info")
                    }) }
                }
                Text {
                    visible: page.bot.dispositivos.length === 0
                    text: "Todavía no hay ninguno. Los Stream Deck y controladores MIDI aparecen solos cuando su plugin está prendido."
                    color: Theme.textFaint; font.pixelSize: 12
                }
                Repeater {
                    model: page.bot.dispositivos
                    delegate: Panel {
                        required property var modelData
                        Layout.fillWidth: true
                        padding: 12
                        implicitHeight: 60
                        RowLayout {
                            anchors.fill: parent
                            spacing: 10
                            Text { text: modelData.conectado ? "●" : "○"; color: modelData.conectado ? Theme.ok : Theme.textFaint; font.pixelSize: 16 }
                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 2
                                Text {
                                    text: modelData.nombre + "  ·  " + (modelData.origen === "plugin" ? "hardware (plugin)" : "tablet")
                                    color: Theme.text; font.pixelSize: 13
                                }
                                Text {
                                    text: (modelData.ip ? modelData.ip + " · " : "") + (modelData.ultimo_uso_ms ? "último uso " + new Date(modelData.ultimo_uso_ms).toLocaleString(Qt.locale(), "dd/MM HH:mm") : "") +
                                          (modelData.vence_dias === 0 ? " · no vence" : modelData.vence_dias ? " · vence a los " + modelData.vence_dias + " días sin uso" : "")
                                    color: Theme.textFaint; font.pixelSize: 11
                                }
                            }
                            ComboBox {
                                Layout.preferredWidth: 200
                                model: page.bot.perfiles
                                textRole: "nombre"
                                currentIndex: page.bot.perfiles.findIndex(p => p.id === modelData.perfil)
                                onActivated: (i) => {
                                    const e = page.bot.asignarUi(modelData.id, page.bot.perfiles[i].id, "")
                                    if (e !== "") Studio.notify(e, "error")
                                }
                            }
                            GlassButton {
                                compact: true; text: "Revocar"
                                onClicked: page.bot.revocarUi(modelData.id)
                            }
                        }
                    }
                }
            }
        }

        // ========================= EVENTOS =========================
        ColumnLayout {
            visible: page.pestana === "eventos"
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 8
            RowLayout {
                TextField { id: filtroEv; Layout.preferredWidth: 280; placeholderText: "Filtro: aire/#, audio/nivel/+, alarma…"; color: Theme.text }
                Text {
                    Layout.fillWidth: true; wrapMode: Text.WordWrap
                    text: "Lo que publica la app. Sirve para el estado de una celda (ligadura) y para @plugin.escuchar(\"tema\")."
                    color: Theme.textFaint; font.pixelSize: 11
                }
            }
            RowLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 12
                Panel {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    padding: 10
                    ListView {
                        id: temasLista
                        anchors.fill: parent
                        clip: true
                        model: []
                        delegate: Text {
                            required property var modelData
                            width: ListView.view.width
                            text: "<b>" + modelData.tema + "</b>  " + modelData.valor
                            textFormat: Text.StyledText
                            color: Theme.text; font.pixelSize: 11; font.family: Theme.mono
                            wrapMode: Text.WrapAnywhere
                        }
                    }
                }
                Panel {
                    Layout.preferredWidth: 420
                    Layout.fillHeight: true
                    padding: 10
                    ListView {
                        id: recientesLista
                        anchors.fill: parent
                        clip: true
                        model: []
                        delegate: Text {
                            required property var modelData
                            width: ListView.view.width
                            text: modelData.hora + "  " + modelData.tema + "  " + modelData.valor
                            color: Theme.textDim; font.pixelSize: 11; font.family: Theme.mono
                            elide: Text.ElideRight
                        }
                    }
                }
            }
            Timer {
                interval: 1000; repeat: true; triggeredOnStart: true
                running: page.visible && page.pestana === "eventos"
                onTriggered: {
                    temasLista.model = Studio.bus.temasUi(filtroEv.text)
                    recientesLista.model = Studio.bus.recientesUi(filtroEv.text)
                }
            }
        }

        // ========================= AUDITORÍA =========================
        Panel {
            visible: page.pestana === "auditoria"
            Layout.fillWidth: true
            Layout.fillHeight: true
            padding: 10
            ListView {
                id: audLista
                anchors.fill: parent
                clip: true
                model: []
                delegate: Text {
                    required property var modelData
                    width: ListView.view.width
                    text: modelData.hora + "  " + modelData.dispositivo + "  " + modelData.que + "  " + modelData.detalle
                    color: modelData.que === "rechazada" || modelData.que === "error" || modelData.que.indexOf("rechaz") >= 0 ? Theme.live : Theme.textDim
                    font.pixelSize: 11; font.family: Theme.mono
                    elide: Text.ElideRight
                }
            }
            Timer {
                interval: 1500; repeat: true; triggeredOnStart: true
                running: page.visible && page.pestana === "auditoria"
                onTriggered: audLista.model = page.bot.auditoriaUi()
            }
        }
    }
}
