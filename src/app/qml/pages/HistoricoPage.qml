// El Histórico: todos los eventos y todo lo de cada uno (docs/planes/PLAN-EVENTOS-HISTORICO.md).
// La página Evento es preparar y correr; esta es el archivo: qué pasó, con qué
// configuración, qué se grabó y transmitió. Solo presentación: lee de
// Studio.historico, que dice lo mismo que el MCP (evento_* e historico_*).
//
//   carpetas | eventos | detalle (Resumen · Línea de tiempo · Grabaciones y vivos ·
//                                 Configuración · Archivos)
//   arriba: buscar en todos los eventos, ajustes de qué se registra.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import BA
import "../controls"

Rectangle {
    id: page
    color: Theme.bg
    property real leftInset: 92
    readonly property var h: Studio.historico

    property string grupo: ""
    property string sel: ""
    property string tab: "resumen"
    property int version: 0     // se sube para releer (los métodos no avisan solos)
    readonly property var lista: { page.version; return page.h ? page.h.eventos(page.grupo, filtro.text) : [] }
    readonly property var resumen: { page.version; return page.h && page.sel ? page.h.resumen(page.sel) : ({}) }
    readonly property var actual: lista.find(e => e.carpeta === page.sel) || null

    function toast(t, n) { ApplicationWindow.window.toast(t, n || "info") }
    function bytes(b) {
        if (b === undefined || b === null) return ""
        if (b > 1e9) return (b / 1e9).toFixed(1) + " GB"
        if (b > 1e6) return (b / 1e6).toFixed(1) + " MB"
        if (b > 1e3) return Math.round(b / 1e3) + " kB"
        return b + " B"
    }
    function colorEstado(e) { return e === "en_curso" ? Theme.live : e === "terminado" ? Theme.ok : Theme.textFaint }
    function textoEstado(e) { return e === "en_curso" ? qsTr("EN CURSO") : e === "terminado" ? qsTr("terminado") : qsTr("planificado") }
    function colorTipo(t) {
        return ({ bus: "#5aa9ff", mcp: "#b48cff", botonera: "#ffb02e", guion: "#35c46b",
                  aviso: "#ff6a6a", evento: Theme.accent, config: "#4dd0c8" })[t] || Theme.textDim
    }

    onVisibleChanged: if (visible) version++
    // ui_show_page {page: "historico", seccion: "<evento>[/<pestaña>]"} (resumen, tiempo,
    // grabaciones, config, archivos): ese evento en esa pestaña.
    Connections {
        target: Studio
        function onMostrarSeccion(pagina, seccion) {
            if (pagina !== "historico" || seccion === "") return
            const tabs = ["resumen", "tiempo", "grabaciones", "config", "archivos"]
            const partes = seccion.split("/")
            if (partes.length > 1 && tabs.indexOf(partes[partes.length - 1]) >= 0) page.tab = partes.pop()
            page.grupo = ""
            page.sel = partes.join("/")
            page.version++
        }
        // ui_abrir_panel {page: "historico", panel: "registro"}: el de «Qué se registra…».
        function onAbrirPanel(pagina, panel) {
            if (pagina !== "historico") return
            if (panel === "cerrar") ajustes.close()
            else if (panel === "registro") ajustes.open()
        }
    }
    Connections {
        target: page.h
        function onCambio() { page.version++ }
        function onResultados(texto, l) { resultados.lista = l; resultados.texto = texto; resultados.open() }
        function onVerificado(carpeta, r) {
            if (r.ok) page.toast(qsTr("«%1»: intacto (%n archivo sellado)", "", r.archivos).arg(carpeta), "ok")
            else if (r.error) page.toast(r.error, "warn")
            else page.toast(qsTr("«%1»: %2 cambiados, %3 faltan, %4 nuevos").arg(carpeta).arg(r.cambiados.length).arg(r.faltan.length)
                            .arg(r.nuevos.length) + (r.aviso ? " — " + r.aviso : ""), r.aviso ? "info" : "warn")
        }
        function onExportado(carpeta, error, copia) {
            if (error) page.toast(qsTr("No se pudo exportar: %1").arg(error), "error")
            else page.toast(qsTr("Exportado en %1").arg(copia), "ok")
        }
    }

    ColumnLayout {
        anchors { fill: parent; leftMargin: page.leftInset; margins: 20 }
        spacing: 12

        // --- Encabezado ----------------------------------------------------------
        RowLayout {
            Layout.fillWidth: true
            spacing: 10
            Text { text: qsTr("Histórico"); color: Theme.text; font { pixelSize: 20; weight: Font.DemiBold } }
            Text { text: qsTr("%n evento", "", page.lista.length); color: Theme.textDim; font.pixelSize: 11 }
            Item { Layout.fillWidth: true }
            TextField {
                id: buscarTodo
                Layout.preferredWidth: 320
                placeholderText: qsTr("Buscar en todos los eventos (bitácora, transcripciones, actas…)")
                color: Theme.text
                font.pixelSize: 12
                onAccepted: if (text.trim().length >= 2) page.h.buscar(text.trim())
            }
            BusyIndicator { running: !!page.h && page.h.buscando; visible: running; implicitWidth: 22; implicitHeight: 22 }
            GlassButton { compact: true; text: qsTr("Carpeta Eventos"); onClicked: page.h.abrirCarpeta("") }
            GlassButton { compact: true; text: qsTr("Qué se registra…"); onClicked: ajustes.open() }
        }

        // Una configuración preparada para el próximo arranque.
        Rectangle {
            visible: !!page.h && !!page.h.restauracion && !!page.h.restauracion.desde
            Layout.fillWidth: true
            implicitHeight: 38
            radius: 8
            color: Qt.rgba(0.96, 0.72, 0.24, 0.15)
            border.color: Theme.warn
            RowLayout {
                anchors { fill: parent; leftMargin: 12; rightMargin: 8 }
                Text {
                    Layout.fillWidth: true
                    elide: Text.ElideRight
                    color: Theme.text; font.pixelSize: 12
                    text: page.h && page.h.restauracion && page.h.restauracion.desde
                          ? qsTr("Al próximo arranque se vuelve a la configuración de «%1» (%n archivo). Lo de ahora queda respaldado.", "",
                                 (page.h.restauracion.archivos || []).length).arg(page.h.restauracion.desde)
                          : ""
                }
                GlassButton { compact: true; text: qsTr("Descartar"); onClicked: page.h.cancelarRestauracion() }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 12

            // --- Carpetas ----------------------------------------------------------
            Rectangle {
                Layout.preferredWidth: 190
                Layout.fillHeight: true
                radius: 10; color: Theme.panel; border.color: Theme.glassBorder
                ColumnLayout {
                    anchors { fill: parent; margins: 10 }
                    spacing: 4
                    // Los eventos guardados (cada uno con todo lo suyo) y cuánto ocupa cada uno;
                    // debajo del abierto, sus subcarpetas (jornadas por año y mes).
                    Text { text: qsTr("Eventos"); color: Theme.textDim; font.pixelSize: 11 }
                    ListView {
                        id: listaEventos
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        readonly property var cf: Studio.configuraciones
                        readonly property string abierto: cf ? cf.activa : ""
                        model: {
                            const l = [{ grupo: "", texto: qsTr("Todos"), nivel: 0 }]
                            for (const e of (cf ? cf.lista : [])) {
                                l.push({ grupo: "@" + e.nombre, texto: e.nombre, nivel: 0, ev: e })
                                if (e.activa)
                                    for (const c of (page.h ? page.h.carpetas : []))
                                        l.push({ grupo: "@" + e.nombre + "/" + c, texto: "▸ " + c.split("/").pop(), nivel: c.split("/").length })
                            }
                            return l
                        }
                        delegate: Rectangle {
                            required property var modelData
                            width: ListView.view.width
                            height: modelData.ev ? 40 : 26
                            radius: 6
                            color: page.grupo === modelData.grupo ? Qt.rgba(1, 0.42, 0.17, 0.18) : "transparent"
                            Column {
                                anchors.verticalCenter: parent.verticalCenter
                                x: 6 + modelData.nivel * 12
                                width: parent.width - x - 6
                                Text {
                                    width: parent.width; elide: Text.ElideRight
                                    text: modelData.texto + (modelData.ev && modelData.ev.activa ? "  ●" : "")
                                    color: page.grupo === modelData.grupo ? Theme.text : Theme.textDim
                                    font.pixelSize: 12; font.weight: modelData.ev ? Font.DemiBold : Font.Normal
                                }
                                Text {
                                    visible: !!modelData.ev
                                    width: parent.width; elide: Text.ElideRight
                                    color: Theme.textFaint; font.pixelSize: 10
                                    text: !modelData.ev ? "" : modelData.ev.midiendo ? qsTr("midiendo…")
                                          : modelData.ev.bytes_grabaciones > 0
                                                ? qsTr("%1 · %2 de grabaciones").arg(page.bytes(modelData.ev.bytes)).arg(page.bytes(modelData.ev.bytes_grabaciones))
                                                : page.bytes(modelData.ev.bytes)
                                }
                            }
                            MouseArea { anchors.fill: parent; onClicked: page.grupo = modelData.grupo }
                            ToolTip.visible: hov.hovered && !!modelData.ev; ToolTip.delay: 500
                            ToolTip.text: modelData.ev ? qsTr("«%1»: todo lo suyo (escenas, medios, grabaciones…) ocupa %2")
                                                         .arg(modelData.ev.nombre).arg(page.bytes(modelData.ev.bytes)) : ""
                            HoverHandler { id: hov }
                        }
                    }
                    GlassButton { compact: true; text: qsTr("Volver a medir"); Layout.fillWidth: true
                                  onClicked: if (listaEventos.cf) listaEventos.cf.medir() }
                }
            }

            // --- Eventos -----------------------------------------------------------
            Rectangle {
                Layout.preferredWidth: 340
                Layout.fillHeight: true
                radius: 10; color: Theme.panel; border.color: Theme.glassBorder
                ColumnLayout {
                    anchors { fill: parent; margins: 10 }
                    spacing: 6
                    TextField {
                        id: filtro
                        Layout.fillWidth: true
                        placeholderText: qsTr("Filtrar por nombre…")
                        color: Theme.text; font.pixelSize: 12
                    }
                    ListView {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        spacing: 4
                        model: page.lista
                        delegate: Rectangle {
                            required property var modelData
                            width: ListView.view.width
                            height: 54
                            radius: 8
                            color: page.sel === modelData.carpeta ? Qt.rgba(1, 0.42, 0.17, 0.16) : Qt.rgba(1, 1, 1, 0.03)
                            border.color: page.sel === modelData.carpeta ? Theme.accent : "transparent"
                            ColumnLayout {
                                anchors { fill: parent; margins: 8 }
                                spacing: 2
                                RowLayout {
                                    spacing: 6
                                    Rectangle { width: 8; height: 8; radius: 4; color: page.colorEstado(modelData.estado) }
                                    Text {
                                        Layout.fillWidth: true
                                        text: modelData.nombre + (modelData.abierto === false ? "  ·  " + modelData.evento : "")
                                        elide: Text.ElideRight
                                        color: Theme.text; font { pixelSize: 13; weight: Font.Medium }
                                    }
                                    Text { text: page.textoEstado(modelData.estado); color: page.colorEstado(modelData.estado); font.pixelSize: 10 }
                                }
                                Text {
                                    Layout.fillWidth: true
                                    elide: Text.ElideRight
                                    color: Theme.textDim; font.pixelSize: 11
                                    text: (modelData.desde || modelData.fecha || qsTr("sin fecha"))
                                          + (modelData.duracionMin ? qsTr(" · %1 h %2 min").arg(Math.floor(modelData.duracionMin / 60)).arg(modelData.duracionMin % 60) : "")
                                          + (modelData.resumen && modelData.resumen.grabaciones ? " · ⏺ " + modelData.resumen.grabaciones : "")
                                          + (modelData.resumen && modelData.resumen.transmisiones ? " · 📡 " + modelData.resumen.transmisiones : "")
                                          + (modelData.resumen && modelData.resumen.sellado ? qsTr(" · sellado ✓") : "")
                                          + (modelData.resumen && modelData.resumen.acta ? qsTr(" · acta") : "")
                                          + (modelData.resumen ? " · " + page.bytes((modelData.resumen.bytes || 0) + (modelData.resumen.bytes_grabaciones || 0)) : "")
                                }
                            }
                            MouseArea { anchors.fill: parent; onClicked: page.sel = modelData.carpeta }
                        }
                        Text {
                            anchors.centerIn: parent
                            visible: page.lista.length === 0
                            text: qsTr("No hay eventos acá")
                            color: Theme.textFaint
                        }
                    }
                }
            }

            // --- Detalle -----------------------------------------------------------
            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                radius: 10; color: Theme.panel; border.color: Theme.glassBorder
                Text {
                    anchors.centerIn: parent
                    visible: !page.sel
                    text: qsTr("Elegí un evento a la izquierda")
                    color: Theme.textFaint
                }
                ColumnLayout {
                    visible: !!page.sel
                    anchors { fill: parent; margins: 12 }
                    spacing: 8
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 2
                            Text {
                                Layout.fillWidth: true
                                elide: Text.ElideRight
                                text: page.actual ? page.actual.nombre : page.sel
                                color: Theme.text; font { pixelSize: 16; weight: Font.DemiBold }
                            }
                            Text {
                                Layout.fillWidth: true
                                elide: Text.ElideRight
                                color: Theme.textDim; font.pixelSize: 11
                                text: page.sel + (page.resumen.desde ? " · " + page.resumen.desde : "")
                                      + (page.resumen.hasta ? " → " + page.resumen.hasta : (page.resumen.enCurso ? qsTr(" → (en curso)") : ""))
                            }
                        }
                        GlassButton { compact: true; text: qsTr("Abrir carpeta"); onClicked: page.h.abrirCarpeta(page.sel) }
                        GlassButton {
                            compact: true; text: qsTr("Verificar sellos")
                            enabled: !!page.resumen.sellado
                            onClicked: { page.toast(qsTr("Verificando… (con grabaciones grandes tarda)")); page.h.verificar(page.sel) }
                        }
                        GlassButton { compact: true; text: qsTr("Exportar…"); onClicked: exportar.open() }
                    }
                    // Pestañas
                    RowLayout {
                        spacing: 6
                        Repeater {
                            model: [{ id: "resumen", t: qsTr("Resumen") }, { id: "tiempo", t: qsTr("Línea de tiempo") },
                                    { id: "grabaciones", t: qsTr("Grabaciones y vivos") }, { id: "config", t: qsTr("Configuración") },
                                    { id: "archivos", t: qsTr("Archivos") }]
                            delegate: GlassButton {
                                required property var modelData
                                compact: true
                                text: modelData.t
                                active: page.tab === modelData.id
                                onClicked: page.tab = modelData.id
                            }
                        }
                    }
                    StackLayout {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        currentIndex: ["resumen", "tiempo", "grabaciones", "config", "archivos"].indexOf(page.tab)

                        // Resumen
                        ScrollView {
                            clip: true
                            contentWidth: availableWidth
                            ColumnLayout {
                                width: parent.width
                                spacing: 8
                                GridLayout {
                                    columns: 2
                                    columnSpacing: 16
                                    rowSpacing: 6
                                    Repeater {
                                        model: [
                                            [qsTr("Estado"), page.textoEstado(page.resumen.estado) + (page.resumen.automatico ? qsTr(" (jornada automática)") : "")],
                                            [qsTr("Lugar"), page.resumen.lugar || "—"],
                                            [qsTr("Etiquetas"), (page.resumen.etiquetas || []).join(", ") || "—"],
                                            [qsTr("Grabaciones"), page.resumen.grabaciones || 0],
                                            [qsTr("Transmisiones"), page.resumen.transmisiones || 0],
                                            [qsTr("Transcripciones"), page.resumen.transcripciones || 0],
                                            [qsTr("Configuración"), page.resumen.config ? (page.resumen.sellado ? qsTr("foto al empezar y al cerrar") : qsTr("foto al empezar")) : "—"],
                                            [qsTr("Sellado"), page.resumen.sellado ? qsTr("sí (Verificar sellos)") : (page.resumen.enCurso ? qsTr("se sella al terminar") : qsTr("no"))],
                                            [qsTr("Acta"), page.resumen.acta ? qsTr("sí") : qsTr("no")],
                                            [qsTr("Evento"), (page.resumen.evento || "—") + (page.resumen.abierto === false ? qsTr(" (no es el abierto)") : "")],
                                            [qsTr("Ocupa"), page.bytes((page.resumen.bytes || 0) + (page.resumen.bytes_grabaciones || 0)) +
                                                      (page.resumen.bytes_grabaciones ? qsTr(" (%1 de grabaciones)").arg(page.bytes(page.resumen.bytes_grabaciones)) : "")]
                                        ]
                                        delegate: RowLayout {
                                            required property var modelData
                                            Layout.columnSpan: 2
                                            Text { Layout.preferredWidth: 130; text: modelData[0]; color: Theme.textDim; font.pixelSize: 12 }
                                            Text { text: String(modelData[1]); color: Theme.text; font.pixelSize: 12 }
                                        }
                                    }
                                }
                                Text {
                                    visible: (page.resumen.documentos || []).length > 0
                                    text: qsTr("Acta, informe y capítulos"); color: Theme.textDim; font.pixelSize: 11; Layout.topMargin: 6
                                }
                                Repeater {
                                    model: page.resumen.documentos || []
                                    delegate: RowLayout {
                                        required property var modelData
                                        Text { text: modelData.archivo; color: Theme.text; font.pixelSize: 12 }
                                        GlassButton { compact: true; text: qsTr("Abrir"); onClicked: page.h.abrirArchivo(modelData.ruta) }
                                    }
                                }
                                Text { text: qsTr("En la bitácora"); color: Theme.textDim; font.pixelSize: 11; Layout.topMargin: 6 }
                                Flow {
                                    Layout.fillWidth: true
                                    spacing: 6
                                    Repeater {
                                        model: Object.keys(page.resumen.porTipo || {})
                                        delegate: Rectangle {
                                            required property var modelData
                                            radius: 10
                                            height: 22
                                            width: chip.implicitWidth + 16
                                            color: Qt.rgba(1, 1, 1, 0.05)
                                            border.color: page.colorTipo(modelData)
                                            Text {
                                                id: chip
                                                anchors.centerIn: parent
                                                text: modelData + " " + page.resumen.porTipo[modelData]
                                                color: page.colorTipo(modelData); font.pixelSize: 11
                                            }
                                            MouseArea {
                                                anchors.fill: parent
                                                onClicked: { tiempo.tipos = [modelData]; page.tab = "tiempo" }
                                            }
                                        }
                                    }
                                }
                                Text {
                                    visible: !!page.resumen.notas
                                    Layout.fillWidth: true
                                    wrapMode: Text.WordWrap
                                    text: page.resumen.notas || ""
                                    color: Theme.textDim; font.pixelSize: 12
                                }
                            }
                        }

                        // Línea de tiempo
                        ColumnLayout {
                            id: tiempo
                            property var tipos: []
                            readonly property var lineas: { page.version; return page.sel && page.tab === "tiempo"
                                ? page.h.bitacora(page.sel, tiempo.tipos, filtroTiempo.text, 1500) : [] }
                            spacing: 6
                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 4
                                GlassButton { compact: true; text: qsTr("Todo"); active: tiempo.tipos.length === 0; onClicked: tiempo.tipos = [] }
                                Repeater {
                                    model: ["bus", "mcp", "botonera", "guion", "aviso", "evento", "config"]
                                    delegate: GlassButton {
                                        required property var modelData
                                        compact: true
                                        text: modelData
                                        active: tiempo.tipos.indexOf(modelData) >= 0
                                        activeColor: page.colorTipo(modelData)
                                        onClicked: {
                                            const l = tiempo.tipos.slice()
                                            const i = l.indexOf(modelData)
                                            if (i >= 0) l.splice(i, 1); else l.push(modelData)
                                            tiempo.tipos = l
                                        }
                                    }
                                }
                                Item { Layout.fillWidth: true }
                                TextField {
                                    id: filtroTiempo
                                    Layout.preferredWidth: 200
                                    placeholderText: qsTr("Buscar…")
                                    color: Theme.text; font.pixelSize: 12
                                }
                                GlassButton { compact: true; text: "↻"; onClicked: page.version++ }
                            }
                            ListView {
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                clip: true
                                model: tiempo.lineas
                                ScrollBar.vertical: ScrollBar {}
                                delegate: RowLayout {
                                    required property var modelData
                                    width: ListView.view.width
                                    spacing: 8
                                    Text { Layout.preferredWidth: 58; text: modelData.hora; color: Theme.textFaint; font { pixelSize: 11; family: "Menlo" } }
                                    Text { Layout.preferredWidth: 58; text: modelData.tipo; color: page.colorTipo(modelData.tipo); font.pixelSize: 11 }
                                    Text { Layout.preferredWidth: 130; text: modelData.origen; elide: Text.ElideRight; color: Theme.textDim; font.pixelSize: 11 }
                                    Text {
                                        Layout.fillWidth: true
                                        text: modelData.texto
                                        wrapMode: Text.WrapAnywhere
                                        maximumLineCount: 3
                                        elide: Text.ElideRight
                                        color: modelData.nivel === "error" ? Theme.live : modelData.nivel === "aviso" ? Theme.warn : Theme.text
                                        font.pixelSize: 11
                                    }
                                }
                                Text {
                                    anchors.centerIn: parent
                                    visible: tiempo.lineas.length === 0
                                    text: qsTr("Nada en la bitácora con ese filtro")
                                    color: Theme.textFaint
                                }
                            }
                        }

                        // Grabaciones y vivos
                        ScrollView {
                            clip: true
                            contentWidth: availableWidth
                            ColumnLayout {
                                id: gv
                                readonly property var d: { page.version; return page.sel && page.tab === "grabaciones" ? page.h.grabaciones(page.sel) : ({}) }
                                width: parent.width
                                spacing: 6
                                RowLayout {
                                    Text { text: qsTr("Grabaciones"); color: Theme.textDim; font.pixelSize: 11; Layout.fillWidth: true }
                                    CheckBox { id: moverlas; text: qsTr("moverlas a la carpeta del evento"); font.pixelSize: 11 }
                                    GlassButton {
                                        compact: true; text: qsTr("Traer lo del día")
                                        onClicked: {
                                            const r = page.h.traerDelDia(page.sel, moverlas.checked)
                                            if (r.error) page.toast(r.error, "error")
                                            else page.toast(qsTr("%1 y %2 del %3").arg(qsTr("%n grabación", "", (r.grabaciones || []).length))
                                                            .arg(qsTr("%n transcripción", "", (r.transcripciones || []).length))
                                                            .arg(r.desde.split(" ")[0]), "ok")
                                            page.version++
                                        }
                                    }
                                }
                                Text { visible: (gv.d.grabaciones || []).length === 0; text: qsTr("Ninguna"); color: Theme.textFaint; font.pixelSize: 12 }
                                Repeater {
                                    model: gv.d.grabaciones || []
                                    delegate: Rectangle {
                                        required property var modelData
                                        Layout.fillWidth: true
                                        implicitHeight: colG.implicitHeight + 14
                                        radius: 8; color: Qt.rgba(1, 1, 1, 0.04)
                                        ColumnLayout {
                                            id: colG
                                            anchors { left: parent.left; right: parent.right; top: parent.top; margins: 7 }
                                            spacing: 3
                                            RowLayout {
                                                Text {
                                                    Layout.fillWidth: true
                                                    elide: Text.ElideMiddle
                                                    text: (modelData.archivo || "").split("/").pop()
                                                    color: modelData.existe ? Theme.text : Theme.textFaint; font.pixelSize: 12
                                                }
                                                Text {
                                                    text: modelData.desde + (modelData.duracion_seg !== undefined
                                                          ? " · " + Math.floor(modelData.duracion_seg / 60) + ":" + String(modelData.duracion_seg % 60).padStart(2, "0")
                                                          : qsTr(" · grabando")) + " · " + page.bytes(modelData.bytes)
                                                    color: Theme.textDim; font.pixelSize: 11
                                                }
                                                GlassButton { compact: true; text: qsTr("Abrir"); enabled: !!modelData.existe; onClicked: page.h.abrirArchivo(modelData.archivo) }
                                            }
                                            Text {
                                                Layout.fillWidth: true
                                                wrapMode: Text.WordWrap
                                                visible: (modelData.capitulos || []).length > 0
                                                color: Theme.textDim; font.pixelSize: 11
                                                text: (modelData.capitulos || []).map(c => Math.floor(c.seg / 60) + ":" + String(c.seg % 60).padStart(2, "0") + " " + c.escena).join("  ·  ")
                                            }
                                        }
                                    }
                                }
                                Text { text: qsTr("Transmisiones"); color: Theme.textDim; font.pixelSize: 11; Layout.topMargin: 8 }
                                Text { visible: (gv.d.transmisiones || []).length === 0; text: qsTr("Ninguna"); color: Theme.textFaint; font.pixelSize: 12 }
                                Repeater {
                                    model: gv.d.transmisiones || []
                                    delegate: RowLayout {
                                        required property var modelData
                                        Layout.fillWidth: true
                                        Text { text: modelData.plataforma || qsTr("transmisión"); color: Theme.text; font.pixelSize: 12; Layout.preferredWidth: 90 }
                                        Text {
                                            Layout.fillWidth: true
                                            elide: Text.ElideRight
                                            text: (modelData.url || modelData.servidor || "") + "  " + modelData.desde + (modelData.hasta ? " → " + modelData.hasta : qsTr(" → (al aire)"))
                                                  + (modelData.caidas ? qsTr(" · %n caída", "", modelData.caidas) : "")
                                            color: Theme.textDim; font.pixelSize: 11
                                        }
                                        GlassButton { compact: true; text: qsTr("Ver"); visible: !!modelData.url; onClicked: Qt.openUrlExternally(modelData.url) }
                                        GlassButton { compact: true; text: qsTr("Estudio"); visible: !!modelData.estudio; onClicked: Qt.openUrlExternally(modelData.estudio) }
                                    }
                                }
                                Text { text: qsTr("Transcripciones"); color: Theme.textDim; font.pixelSize: 11; Layout.topMargin: 8 }
                                Text { visible: (gv.d.transcripciones || []).length === 0; text: qsTr("Ninguna"); color: Theme.textFaint; font.pixelSize: 12 }
                                Repeater {
                                    model: gv.d.transcripciones || []
                                    delegate: RowLayout {
                                        required property var modelData
                                        Text { Layout.fillWidth: true; text: modelData.archivo; color: Theme.text; font.pixelSize: 12 }
                                        Text { text: page.bytes(modelData.bytes); color: Theme.textDim; font.pixelSize: 11 }
                                        GlassButton { compact: true; text: qsTr("Abrir"); onClicked: page.h.abrirArchivo(modelData.ruta) }
                                    }
                                }
                            }
                        }

                        // Configuración
                        ColumnLayout {
                            id: cfg
                            property string foto: "inicio"
                            property string archivo: ""
                            property var cambios: null
                            readonly property var archivos: { page.version; return page.sel && page.tab === "config" ? page.h.config(page.sel, cfg.foto) : [] }
                            spacing: 6
                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 4
                                Repeater {
                                    model: [{ id: "inicio", t: qsTr("Al empezar") }, { id: "cierre", t: qsTr("Al cerrar") }, { id: "ultima", t: qsTr("La última") }]
                                    delegate: GlassButton {
                                        required property var modelData
                                        compact: true
                                        text: modelData.t
                                        active: cfg.foto === modelData.id
                                        onClicked: { cfg.foto = modelData.id; cfg.archivo = ""; cfg.cambios = null }
                                    }
                                }
                                Item { Layout.fillWidth: true }
                                GlassButton {
                                    compact: true; text: qsTr("Comparar con ahora")
                                    enabled: cfg.archivos.length > 0
                                    onClicked: cfg.cambios = page.h.comparar(page.sel, cfg.foto, "", "actual")
                                }
                                GlassButton {
                                    compact: true; text: qsTr("Volver a esta configuración…")
                                    enabled: cfg.archivos.length > 0
                                    onClicked: ApplicationWindow.window.confirm({
                                        title: qsTr("Volver a la configuración de este evento"),
                                        text: qsTr("Se prepara %1 como estaba %2 «%3», y se aplica la próxima vez que arranque la app. Lo de ahora queda respaldado. Las claves no cambian.")
                                              .arg(cfg.archivo ? qsTr("«%1»").arg(cfg.archivo) : qsTr("toda la configuración (%n archivo)", "", cfg.archivos.length))
                                              .arg(cfg.foto === "inicio" ? qsTr("al empezar") : cfg.foto === "cierre" ? qsTr("al cerrar") : qsTr("la última vez"))
                                              .arg(page.sel),
                                        accept: qsTr("Preparar"),
                                        reject: qsTr("Cancelar"),
                                        action: () => {
                                            const e = page.h.prepararRestauracion(page.sel, cfg.foto, cfg.archivo ? [cfg.archivo] : [])
                                            page.toast(e ? e : qsTr("Listo: se aplica al reiniciar la app"), e ? "error" : "ok")
                                        }
                                    })
                                }
                            }
                            Text {
                                visible: cfg.archivos.length === 0
                                text: qsTr("Este evento no tiene esa foto (las fotos empezaron a sacarse con el histórico).")
                                color: Theme.textFaint; font.pixelSize: 12
                            }
                            RowLayout {
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                spacing: 8
                                ListView {
                                    Layout.preferredWidth: 220
                                    Layout.fillHeight: true
                                    clip: true
                                    model: cfg.archivos
                                    delegate: Rectangle {
                                        required property var modelData
                                        width: ListView.view.width
                                        height: 24
                                        radius: 5
                                        color: cfg.archivo === modelData.archivo ? Qt.rgba(1, 0.42, 0.17, 0.18) : "transparent"
                                        Text {
                                            anchors { verticalCenter: parent.verticalCenter; left: parent.left; leftMargin: 6; right: parent.right }
                                            elide: Text.ElideRight
                                            text: modelData.archivo
                                            color: Theme.text; font.pixelSize: 11
                                        }
                                        MouseArea { anchors.fill: parent; onClicked: { cfg.archivo = modelData.archivo; cfg.cambios = null } }
                                    }
                                }
                                ScrollView {
                                    Layout.fillWidth: true
                                    Layout.fillHeight: true
                                    clip: true
                                    TextArea {
                                        readOnly: true
                                        wrapMode: TextEdit.NoWrap
                                        color: Theme.text
                                        font { pixelSize: 11; family: "Menlo" }
                                        background: Rectangle { color: Qt.rgba(0, 0, 0, 0.25); radius: 6 }
                                        text: {
                                            if (cfg.cambios !== null) {
                                                if (cfg.cambios.length === 0) return qsTr("Igual que ahora: no cambió nada.")
                                                return cfg.cambios.map(c => c.archivo + " — " + c.que + (c.total ? qsTr(" (%n cambio)", "", c.total) : "")
                                                    + (c.cambios ? "\n" + c.cambios.map(o => "   " + o.op + " " + o.path + (o.value !== undefined ? " = " + JSON.stringify(o.value) : "")).join("\n") : "")).join("\n\n")
                                            }
                                            return cfg.archivo ? page.h.contenidoConfig(page.sel, cfg.foto, cfg.archivo) : qsTr("Elegí un archivo")
                                        }
                                    }
                                }
                            }
                        }

                        // Archivos
                        ListView {
                            id: arch
                            clip: true
                            model: { page.version; return page.sel && page.tab === "archivos" ? page.h.archivos(page.sel) : [] }
                            ScrollBar.vertical: ScrollBar {}
                            delegate: RowLayout {
                                required property var modelData
                                width: ListView.view.width
                                Text { Layout.fillWidth: true; text: modelData.archivo; elide: Text.ElideMiddle; color: Theme.text; font.pixelSize: 12 }
                                Text { text: page.bytes(modelData.bytes); color: Theme.textDim; font.pixelSize: 11 }
                                GlassButton {
                                    compact: true; text: qsTr("Abrir")
                                    onClicked: page.h.abrirArchivo(page.resumen.ruta + "/" + modelData.archivo)
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    // --- Resultados de buscar en todo ----------------------------------------------
    Popup {
        id: resultados
        property var lista: []
        property string texto: ""
        anchors.centerIn: parent
        width: Math.min(page.width - 120, 900)
        height: Math.min(page.height - 120, 600)
        modal: true
        background: Rectangle { color: Theme.panel; radius: 12; border.color: Theme.glassBorder }
        ColumnLayout {
            anchors { fill: parent; margins: 12 }
            spacing: 8
            RowLayout {
                Text {
                    Layout.fillWidth: true
                    text: qsTr("%n resultado para «%1»", "", resultados.lista.length).arg(resultados.texto)
                    color: Theme.text; font { pixelSize: 14; weight: Font.DemiBold }
                }
                GlassButton { compact: true; text: qsTr("Cerrar"); onClicked: resultados.close() }
            }
            ListView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                spacing: 4
                model: resultados.lista
                ScrollBar.vertical: ScrollBar {}
                delegate: Rectangle {
                    required property var modelData
                    width: ListView.view.width
                    implicitHeight: colR.implicitHeight + 10
                    radius: 6
                    color: Qt.rgba(1, 1, 1, 0.04)
                    ColumnLayout {
                        id: colR
                        anchors { left: parent.left; right: parent.right; top: parent.top; margins: 5 }
                        spacing: 2
                        Text {
                            text: modelData.evento + " · " + modelData.archivo + (modelData.hora ? " · " + modelData.hora : qsTr(" · línea %1").arg(modelData.linea))
                            color: Theme.textDim; font.pixelSize: 11
                        }
                        Text {
                            Layout.fillWidth: true
                            text: modelData.texto
                            wrapMode: Text.WrapAnywhere
                            maximumLineCount: 3
                            elide: Text.ElideRight
                            color: Theme.text; font.pixelSize: 12
                        }
                    }
                    MouseArea {
                        anchors.fill: parent
                        onClicked: {
                            page.grupo = ""
                            page.sel = modelData.evento
                            if (modelData.archivo === "bitacora.jsonl") {
                                tiempo.tipos = []
                                filtroTiempo.text = resultados.texto
                                page.tab = "tiempo"
                            } else {
                                page.tab = "archivos"
                            }
                            resultados.close()
                        }
                    }
                }
            }
        }
    }

    // --- Exportar --------------------------------------------------------------------
    Popup {
        id: exportar
        anchors.centerIn: parent
        width: 460
        modal: true
        background: Rectangle { color: Theme.panel; radius: 12; border.color: Theme.glassBorder }
        onOpened: destinoExp.text = page.h.carpetaDeExportar()
        ColumnLayout {
            anchors { fill: parent; margins: 14 }
            spacing: 10
            Text { text: qsTr("Exportar «%1»").arg(page.sel); color: Theme.text; font { pixelSize: 14; weight: Font.DemiBold } }
            Text {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                text: qsTr("Se copia la carpeta del evento entera (bitácora, configuración, asamblea, registro, transcripciones) y lo que nombran sus fotos de configuración: se entiende sola en otra máquina.")
                color: Theme.textDim; font.pixelSize: 12
            }
            TextField { id: destinoExp; Layout.fillWidth: true; color: Theme.text; font.pixelSize: 12 }
            CheckBox { id: conGrab; checked: true; text: qsTr("Con las grabaciones") }
            RowLayout {
                Item { Layout.fillWidth: true }
                GlassButton { text: qsTr("Cancelar"); onClicked: exportar.close() }
                GlassButton {
                    text: qsTr("Exportar")
                    active: true
                    onClicked: { page.h.exportar(page.sel, destinoExp.text, conGrab.checked); page.toast(qsTr("Exportando…")); exportar.close() }
                }
            }
        }
    }

    // --- Qué se registra -------------------------------------------------------------
    Popup {
        id: ajustes
        anchors.centerIn: parent
        // El alto lo da el contenido (antes iba con anchors.fill y el modal lo cortaba
        // abajo); el ancho, la ventana.
        width: Math.min(600, page.width - 40)
        height: Math.min(implicitHeight, page.height - 40)
        padding: 18
        modal: true
        background: Rectangle { color: Theme.panel; radius: 12; border.color: Theme.glassBorder }
        readonly property var a: page.h ? page.h.ajustes : ({})
        contentItem: ScrollView {
            id: ajustesScroll
            contentWidth: availableWidth
            clip: true
            implicitHeight: ajustesCol.implicitHeight
        ColumnLayout {
            id: ajustesCol
            width: ajustesScroll.availableWidth
            spacing: 8
            Text { text: qsTr("Qué se registra en cada evento"); color: Theme.text; font { pixelSize: 14; weight: Font.DemiBold } }
            Repeater {
                model: [
                    { k: "bus", t: qsTr("Lo que pasa en el estudio (escena al aire, grabación, transmisión, oradores, votaciones, alarmas)") },
                    { k: "mcp", t: qsTr("Cada herramienta del asistente o del MCP que cambia algo") },
                    { k: "botonera", t: qsTr("Cada botón de la botonera y los aparatos que se conectan") },
                    { k: "guion", t: qsTr("Cada cue del guion (no el ensayo)") },
                    { k: "aviso", t: qsTr("Avisos y errores de cualquier parte") },
                    { k: "config", t: qsTr("Cada cambio de configuración (con lo que cambió)") },
                    { k: "evento", t: qsTr("Cuándo arranca y se cierra la app, cuándo empieza y termina el evento") }
                ]
                delegate: RowLayout {
                    required property var modelData
                    Layout.fillWidth: true
                    Toggle {
                        checked: !!ajustes.a.registrar && ajustes.a.registrar[modelData.k] !== false
                        onToggled: (wanted) => {
                            const r = {}
                            r[modelData.k] = wanted
                            const e = page.h.guardarAjustes({ registrar: r })
                            if (e) page.toast(e, "error")
                        }
                    }
                    Text { Layout.fillWidth: true; wrapMode: Text.WordWrap; text: modelData.t; color: Theme.text; font.pixelSize: 12 }
                }
            }
            RowLayout {
                Layout.topMargin: 6
                Toggle {
                    checked: !!ajustes.a.evento_automatico
                    onToggled: (wanted) => page.h.guardarAjustes({ evento_automatico: wanted })
                }
                Text {
                    Layout.fillWidth: true; wrapMode: Text.WordWrap
                    text: qsTr("Jornada automática: sin un evento elegido, todo va a «Jornada del día» (nada queda sin evento)")
                    color: Theme.text; font.pixelSize: 12
                }
            }
            Text { text: qsTr("Del bus no se anota (se repite muchas veces por segundo):"); color: Theme.textDim; font.pixelSize: 11; Layout.topMargin: 6 }
            TextField {
                Layout.fillWidth: true
                color: Theme.text; font.pixelSize: 12
                text: (ajustes.a.bus_excluir || []).join(", ")
                onEditingFinished: {
                    const l = text.split(",").map(s => s.trim()).filter(s => s.length > 0)
                    const e = page.h.guardarAjustes({ bus_excluir: l })
                    if (e) page.toast(e, "error")
                }
            }
            Text { text: qsTr("Disco y retención"); color: Theme.textDim; font.pixelSize: 11; Layout.topMargin: 6 }
            // Cada ajuste: el texto arriba (se corta en renglones) y el número abajo, así
            // nada se sale del marco por la derecha.
            Text { Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.text; font.pixelSize: 12
                   text: qsTr("Borrar las grabaciones de eventos terminados hace más de") }
            RowLayout {
                SpinBox {
                    from: 0; to: 3650
                    editable: true
                    value: ajustes.a.retencion ? ajustes.a.retencion.grabaciones_dias : 0
                    onValueModified: page.h.guardarAjustes({ retencion: { grabaciones_dias: value } })
                }
                Text { text: qsTr("días (0 = nunca)"); color: Theme.textDim; font.pixelSize: 12 }
            }
            Text { Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.text; font.pixelSize: 12
                   text: qsTr("Avisar cuando el disco pase del") }
            RowLayout {
                Layout.fillWidth: true
                SpinBox {
                    from: 0; to: 100
                    editable: true
                    value: ajustes.a.retencion ? ajustes.a.retencion.aviso_disco_pct : 90
                    onValueModified: page.h.guardarAjustes({ retencion: { aviso_disco_pct: value } })
                }
                Text {
                    Layout.fillWidth: true; wrapMode: Text.WordWrap
                    readonly property var d: { page.version; return ajustes.visible ? page.h.discoUi() : ({}) }
                    text: qsTr("% (ahora %1 %, quedan %2 GB)").arg(d.usado_pct || 0).arg(d.libre_gb || 0)
                    color: Theme.textDim; font.pixelSize: 12
                }
            }
            RowLayout {
                Layout.fillWidth: true
                Layout.topMargin: 4
                GlassButton {
                    compact: true; text: qsTr("¿Qué borraría?")
                    onClicked: {
                        const r = page.h.retencionUi(true)
                        page.toast(r.nota ? r.nota : qsTr("%n evento, %1 MB de grabaciones", "", r.eventos.length).arg(Math.round(r.bytes / 1e6)), "info")
                    }
                }
                Item { Layout.fillWidth: true }
                GlassButton { text: qsTr("Listo"); onClicked: ajustes.close() }
            }
        }
        }
    }
}
