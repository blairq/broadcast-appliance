// El Histórico: todos los eventos y todo lo de cada uno (docs/PLAN-EVENTOS-HISTORICO.md).
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
    function textoEstado(e) { return e === "en_curso" ? "EN CURSO" : e === "terminado" ? "terminado" : "planificado" }
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
    }
    Connections {
        target: page.h
        function onCambio() { page.version++ }
        function onResultados(texto, l) { resultados.lista = l; resultados.texto = texto; resultados.open() }
        function onVerificado(carpeta, r) {
            if (r.ok) page.toast("«" + carpeta + "»: intacto (" + r.archivos + " archivos sellados)", "ok")
            else if (r.error) page.toast(r.error, "warn")
            else page.toast("«" + carpeta + "»: " + (r.cambiados.length) + " cambiados, " + r.faltan.length + " faltan, "
                            + r.nuevos.length + " nuevos" + (r.aviso ? " — " + r.aviso : ""), r.aviso ? "info" : "warn")
        }
        function onExportado(carpeta, error, copia) {
            if (error) page.toast("No se pudo exportar: " + error, "error")
            else page.toast("Exportado en " + copia, "ok")
        }
    }

    ColumnLayout {
        anchors { fill: parent; leftMargin: page.leftInset; margins: 20 }
        spacing: 12

        // --- Encabezado ----------------------------------------------------------
        RowLayout {
            Layout.fillWidth: true
            spacing: 10
            Text { text: "Histórico"; color: Theme.text; font { pixelSize: 20; weight: Font.DemiBold } }
            Text { text: page.lista.length + " eventos"; color: Theme.textDim; font.pixelSize: 11 }
            Item { Layout.fillWidth: true }
            TextField {
                id: buscarTodo
                Layout.preferredWidth: 320
                placeholderText: "Buscar en todos los eventos (bitácora, transcripciones, actas…)"
                color: Theme.text
                font.pixelSize: 12
                onAccepted: if (text.trim().length >= 2) page.h.buscar(text.trim())
            }
            BusyIndicator { running: !!page.h && page.h.buscando; visible: running; implicitWidth: 22; implicitHeight: 22 }
            GlassButton { compact: true; text: "Carpeta Eventos"; onClicked: page.h.abrirCarpeta("") }
            GlassButton { compact: true; text: "Qué se registra…"; onClicked: ajustes.open() }
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
                          ? "Al próximo arranque se vuelve a la configuración de «" + page.h.restauracion.desde + "» ("
                            + (page.h.restauracion.archivos || []).length + " archivos). Lo de ahora queda respaldado."
                          : ""
                }
                GlassButton { compact: true; text: "Descartar"; onClicked: page.h.cancelarRestauracion() }
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
                    Text { text: "Carpetas"; color: Theme.textDim; font.pixelSize: 11 }
                    ListView {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        model: [""].concat(page.h ? page.h.carpetas : [])
                        delegate: Rectangle {
                            required property var modelData
                            width: ListView.view.width
                            height: 26
                            radius: 6
                            color: page.grupo === modelData ? Qt.rgba(1, 0.42, 0.17, 0.18) : "transparent"
                            Text {
                                anchors.verticalCenter: parent.verticalCenter
                                x: 6 + (modelData === "" ? 0 : (modelData.split("/").length - 1) * 12)
                                text: modelData === "" ? "Todos" : "▸ " + modelData.split("/").pop()
                                color: page.grupo === modelData ? Theme.text : Theme.textDim
                                font.pixelSize: 12
                            }
                            MouseArea { anchors.fill: parent; onClicked: page.grupo = modelData }
                        }
                    }
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
                        placeholderText: "Filtrar por nombre…"
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
                                        text: modelData.nombre
                                        elide: Text.ElideRight
                                        color: Theme.text; font { pixelSize: 13; weight: Font.Medium }
                                    }
                                    Text { text: page.textoEstado(modelData.estado); color: page.colorEstado(modelData.estado); font.pixelSize: 10 }
                                }
                                Text {
                                    Layout.fillWidth: true
                                    elide: Text.ElideRight
                                    color: Theme.textDim; font.pixelSize: 11
                                    text: (modelData.desde || modelData.fecha || "sin fecha")
                                          + (modelData.duracionMin ? " · " + Math.floor(modelData.duracionMin / 60) + " h " + (modelData.duracionMin % 60) + " min" : "")
                                          + (modelData.resumen && modelData.resumen.grabaciones ? " · ⏺ " + modelData.resumen.grabaciones : "")
                                          + (modelData.resumen && modelData.resumen.transmisiones ? " · 📡 " + modelData.resumen.transmisiones : "")
                                          + (modelData.resumen && modelData.resumen.sellado ? " · sellado ✓" : "")
                                          + (modelData.resumen && modelData.resumen.acta ? " · acta" : "")
                                }
                            }
                            MouseArea { anchors.fill: parent; onClicked: page.sel = modelData.carpeta }
                        }
                        Text {
                            anchors.centerIn: parent
                            visible: page.lista.length === 0
                            text: "No hay eventos acá"
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
                    text: "Elegí un evento a la izquierda"
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
                                      + (page.resumen.hasta ? " → " + page.resumen.hasta : (page.resumen.enCurso ? " → (en curso)" : ""))
                            }
                        }
                        GlassButton { compact: true; text: "Abrir carpeta"; onClicked: page.h.abrirCarpeta(page.sel) }
                        GlassButton {
                            compact: true; text: "Verificar sellos"
                            enabled: !!page.resumen.sellado
                            onClicked: { page.toast("Verificando… (con grabaciones grandes tarda)"); page.h.verificar(page.sel) }
                        }
                        GlassButton { compact: true; text: "Exportar…"; onClicked: exportar.open() }
                    }
                    // Pestañas
                    RowLayout {
                        spacing: 6
                        Repeater {
                            model: [{ id: "resumen", t: "Resumen" }, { id: "tiempo", t: "Línea de tiempo" },
                                    { id: "grabaciones", t: "Grabaciones y vivos" }, { id: "config", t: "Configuración" },
                                    { id: "archivos", t: "Archivos" }]
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
                            ColumnLayout {
                                width: parent.width
                                spacing: 8
                                GridLayout {
                                    columns: 2
                                    columnSpacing: 16
                                    rowSpacing: 6
                                    Repeater {
                                        model: [
                                            ["Estado", page.textoEstado(page.resumen.estado) + (page.resumen.automatico ? " (jornada automática)" : "")],
                                            ["Lugar", page.resumen.lugar || "—"],
                                            ["Etiquetas", (page.resumen.etiquetas || []).join(", ") || "—"],
                                            ["Grabaciones", page.resumen.grabaciones || 0],
                                            ["Transmisiones", page.resumen.transmisiones || 0],
                                            ["Transcripciones", page.resumen.transcripciones || 0],
                                            ["Configuración", page.resumen.config ? "foto al empezar" + (page.resumen.sellado ? " y al cerrar" : "") : "—"],
                                            ["Sellado", page.resumen.sellado ? "sí (Verificar sellos)" : (page.resumen.enCurso ? "se sella al terminar" : "no")],
                                            ["Acta", page.resumen.acta ? "sí" : "no"],
                                            ["Ocupa", page.bytes(page.resumen.bytes)]
                                        ]
                                        delegate: RowLayout {
                                            required property var modelData
                                            Layout.columnSpan: 2
                                            Text { Layout.preferredWidth: 130; text: modelData[0]; color: Theme.textDim; font.pixelSize: 12 }
                                            Text { text: String(modelData[1]); color: Theme.text; font.pixelSize: 12 }
                                        }
                                    }
                                }
                                Text { text: "En la bitácora"; color: Theme.textDim; font.pixelSize: 11; Layout.topMargin: 6 }
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
                                GlassButton { compact: true; text: "Todo"; active: tiempo.tipos.length === 0; onClicked: tiempo.tipos = [] }
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
                                    placeholderText: "Buscar…"
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
                                    text: "Nada en la bitácora con ese filtro"
                                    color: Theme.textFaint
                                }
                            }
                        }

                        // Grabaciones y vivos
                        ScrollView {
                            clip: true
                            ColumnLayout {
                                id: gv
                                readonly property var d: { page.version; return page.sel && page.tab === "grabaciones" ? page.h.grabaciones(page.sel) : ({}) }
                                width: parent.width
                                spacing: 6
                                Text { text: "Grabaciones"; color: Theme.textDim; font.pixelSize: 11 }
                                Text { visible: (gv.d.grabaciones || []).length === 0; text: "Ninguna"; color: Theme.textFaint; font.pixelSize: 12 }
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
                                                          : " · grabando") + " · " + page.bytes(modelData.bytes)
                                                    color: Theme.textDim; font.pixelSize: 11
                                                }
                                                GlassButton { compact: true; text: "Abrir"; enabled: !!modelData.existe; onClicked: page.h.abrirArchivo(modelData.archivo) }
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
                                Text { text: "Transmisiones"; color: Theme.textDim; font.pixelSize: 11; Layout.topMargin: 8 }
                                Text { visible: (gv.d.transmisiones || []).length === 0; text: "Ninguna"; color: Theme.textFaint; font.pixelSize: 12 }
                                Repeater {
                                    model: gv.d.transmisiones || []
                                    delegate: RowLayout {
                                        required property var modelData
                                        Layout.fillWidth: true
                                        Text { text: modelData.plataforma || "transmisión"; color: Theme.text; font.pixelSize: 12; Layout.preferredWidth: 90 }
                                        Text {
                                            Layout.fillWidth: true
                                            elide: Text.ElideRight
                                            text: (modelData.url || modelData.servidor || "") + "  " + modelData.desde + (modelData.hasta ? " → " + modelData.hasta : " → (al aire)")
                                                  + (modelData.caidas ? " · " + modelData.caidas + " caídas" : "")
                                            color: Theme.textDim; font.pixelSize: 11
                                        }
                                        GlassButton { compact: true; text: "Ver"; visible: !!modelData.url; onClicked: Qt.openUrlExternally(modelData.url) }
                                        GlassButton { compact: true; text: "Estudio"; visible: !!modelData.estudio; onClicked: Qt.openUrlExternally(modelData.estudio) }
                                    }
                                }
                                Text { text: "Transcripciones"; color: Theme.textDim; font.pixelSize: 11; Layout.topMargin: 8 }
                                Text { visible: (gv.d.transcripciones || []).length === 0; text: "Ninguna"; color: Theme.textFaint; font.pixelSize: 12 }
                                Repeater {
                                    model: gv.d.transcripciones || []
                                    delegate: RowLayout {
                                        required property var modelData
                                        Text { Layout.fillWidth: true; text: modelData.archivo; color: Theme.text; font.pixelSize: 12 }
                                        Text { text: page.bytes(modelData.bytes); color: Theme.textDim; font.pixelSize: 11 }
                                        GlassButton { compact: true; text: "Abrir"; onClicked: page.h.abrirArchivo(modelData.ruta) }
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
                                    model: [{ id: "inicio", t: "Al empezar" }, { id: "cierre", t: "Al cerrar" }, { id: "ultima", t: "La última" }]
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
                                    compact: true; text: "Comparar con ahora"
                                    enabled: cfg.archivos.length > 0
                                    onClicked: cfg.cambios = page.h.comparar(page.sel, cfg.foto, "", "actual")
                                }
                                GlassButton {
                                    compact: true; text: "Volver a esta configuración…"
                                    enabled: cfg.archivos.length > 0
                                    onClicked: ApplicationWindow.window.confirm({
                                        title: "Volver a la configuración de este evento",
                                        text: "Se prepara " + (cfg.archivo ? "«" + cfg.archivo + "»" : "toda la configuración (" + cfg.archivos.length + " archivos)")
                                              + " como estaba " + (cfg.foto === "inicio" ? "al empezar" : cfg.foto === "cierre" ? "al cerrar" : "la última vez")
                                              + " «" + page.sel + "», y se aplica la próxima vez que arranque la app. Lo de ahora queda respaldado. Las claves no cambian.",
                                        accept: "Preparar",
                                        reject: "Cancelar",
                                        action: () => {
                                            const e = page.h.prepararRestauracion(page.sel, cfg.foto, cfg.archivo ? [cfg.archivo] : [])
                                            page.toast(e ? e : "Listo: se aplica al reiniciar la app", e ? "error" : "ok")
                                        }
                                    })
                                }
                            }
                            Text {
                                visible: cfg.archivos.length === 0
                                text: "Este evento no tiene esa foto (las fotos empezaron a sacarse con el histórico)."
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
                                                if (cfg.cambios.length === 0) return "Igual que ahora: no cambió nada."
                                                return cfg.cambios.map(c => c.archivo + " — " + c.que + (c.total ? " (" + c.total + " cambios)" : "")
                                                    + (c.cambios ? "\n" + c.cambios.map(o => "   " + o.op + " " + o.path + (o.value !== undefined ? " = " + JSON.stringify(o.value) : "")).join("\n") : "")).join("\n\n")
                                            }
                                            return cfg.archivo ? page.h.contenidoConfig(page.sel, cfg.foto, cfg.archivo) : "Elegí un archivo"
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
                                    compact: true; text: "Abrir"
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
                    text: resultados.lista.length + " resultados para «" + resultados.texto + "»"
                    color: Theme.text; font { pixelSize: 14; weight: Font.DemiBold }
                }
                GlassButton { compact: true; text: "Cerrar"; onClicked: resultados.close() }
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
                            text: modelData.evento + " · " + modelData.archivo + (modelData.hora ? " · " + modelData.hora : " · línea " + modelData.linea)
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
            Text { text: "Exportar «" + page.sel + "»"; color: Theme.text; font { pixelSize: 14; weight: Font.DemiBold } }
            Text {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                text: "Se copia la carpeta del evento entera (bitácora, configuración, asamblea, registro, transcripciones) y lo que nombran sus fotos de configuración: se entiende sola en otra máquina."
                color: Theme.textDim; font.pixelSize: 12
            }
            TextField { id: destinoExp; Layout.fillWidth: true; color: Theme.text; font.pixelSize: 12 }
            CheckBox { id: conGrab; checked: true; text: "Con las grabaciones" }
            RowLayout {
                Item { Layout.fillWidth: true }
                GlassButton { text: "Cancelar"; onClicked: exportar.close() }
                GlassButton {
                    text: "Exportar"
                    active: true
                    onClicked: { page.h.exportar(page.sel, destinoExp.text, conGrab.checked); page.toast("Exportando…"); exportar.close() }
                }
            }
        }
    }

    // --- Qué se registra -------------------------------------------------------------
    Popup {
        id: ajustes
        anchors.centerIn: parent
        width: 520
        modal: true
        background: Rectangle { color: Theme.panel; radius: 12; border.color: Theme.glassBorder }
        readonly property var a: page.h ? page.h.ajustes : ({})
        ColumnLayout {
            anchors { fill: parent; margins: 14 }
            spacing: 8
            Text { text: "Qué se registra en cada evento"; color: Theme.text; font { pixelSize: 14; weight: Font.DemiBold } }
            Repeater {
                model: [
                    { k: "bus", t: "Lo que pasa en el estudio (escena al aire, grabación, transmisión, oradores, votaciones, alarmas)" },
                    { k: "mcp", t: "Cada herramienta del asistente o del MCP que cambia algo" },
                    { k: "botonera", t: "Cada botón de la botonera y los aparatos que se conectan" },
                    { k: "guion", t: "Cada cue del guion (no el ensayo)" },
                    { k: "aviso", t: "Avisos y errores de cualquier parte" },
                    { k: "config", t: "Cada cambio de configuración (con lo que cambió)" },
                    { k: "evento", t: "Cuándo arranca y se cierra la app, cuándo empieza y termina el evento" }
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
                    text: "Jornada automática: sin un evento elegido, todo va a «Jornada del día» (nada queda sin evento)"
                    color: Theme.text; font.pixelSize: 12
                }
            }
            Text { text: "Del bus no se anota (se repite muchas veces por segundo):"; color: Theme.textDim; font.pixelSize: 11; Layout.topMargin: 6 }
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
            RowLayout {
                Item { Layout.fillWidth: true }
                GlassButton { text: "Listo"; onClicked: ajustes.close() }
            }
        }
    }
}
