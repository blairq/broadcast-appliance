// Dashboard: fondo en vivo + tarjetas de vidrio movibles con encaje magnetico.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import BA
import "../controls"

Item {
    id: page
    property real leftInset: 92 // espacio del riel de navegacion

    // --- Fondo: una o varias fuentes en vivo -------------------------------
    Item {
        id: bgLayer
        anchors.fill: parent
        Rectangle { anchors.fill: parent; color: Theme.bg }
        // «multiview:<id>»: el fondo es esa multivista de la página Multiview (sus
        // fuentes, distribución y estilo, en vivo), en 16:9 centrado como en sus ventanas.
        // («multiview» solo, el de antes por MCP: la elegida en la página.)
        readonly property bool esMultiview: Studio.dashboard.backgroundLayout.startsWith("multiview")
        readonly property var mv: (Studio.multiviews, !esMultiview ? ({})
                                   : Studio.dashboard.backgroundLayout.startsWith("multiview:") ? Studio.multiviewDe(Studio.dashboard.backgroundLayout.slice(10))
                                   : Studio.multiview)
        readonly property bool mvValida: esMultiview && mv.id !== undefined
        Rectangle { anchors.fill: parent; color: "black"; visible: bgLayer.mvValida }
        VideoWall {
            readonly property real ancho16: Math.min(parent.width, parent.height * 16 / 9)
            anchors.centerIn: parent
            width: bgLayer.mvValida ? ancho16 : parent.width
            height: bgLayer.mvValida ? ancho16 * 9 / 16 : parent.height
            keys: bgLayer.mvValida ? (bgLayer.mv.fuentes || []) : Studio.dashboard.background
            layout: bgLayer.mvValida ? (bgLayer.mv.distribucion || "grid") : (bgLayer.esMultiview ? "grid" : Studio.dashboard.backgroundLayout)
            operador: true
            fondo: bgLayer.mvValida ? (bgLayer.mv.fondo || "transparent") : "transparent"
            borde: bgLayer.mvValida ? (bgLayer.mv.borde || 0) : 0
            colorBorde: bgLayer.mvValida ? (bgLayer.mv.colorBorde || "white") : "white"
            labels: bgLayer.mvValida && bgLayer.mv.nombres === true
            tamEtiqueta: bgLayer.mvValida ? (bgLayer.mv.tamNombres || 18) : 11
            tally: !bgLayer.mvValida || bgLayer.mv.tally !== false
            vumetros: !bgLayer.mvValida || bgLayer.mv.vumetros !== false
            escala: bgLayer.mvValida ? width / 1920 : 1
            grosorTally: bgLayer.mvValida ? (bgLayer.mv.grosorTally !== undefined ? bgLayer.mv.grosorTally : 2) : -1
            gap: bgLayer.mvValida ? (bgLayer.mv.separacion || 0) : 2
            // ENTERO, sin recortar. La celda de la grilla casi nunca tiene la
            // proporcion del video, y `Cover` llenaba recortando: al programa
            // le comia una franja arriba y otra abajo —justo donde suelen ir
            // el titulo y el zocalo— sin que nada lo dijera. Acá el fondo es
            // para MIRAR lo que esta pasando, asi que se ve completo aunque
            // queden bandas; para llenar la pantalla estan las ventanas de
            // salida, que ahi si conviene `Cover`.
            fillMode: VideoItem.Fit
        }
        // Oscurecido + viñeta para que las tarjetas se lean sobre cualquier video.
        Rectangle { anchors.fill: parent; color: "black"; opacity: Studio.dashboard.backgroundDim }
        Rectangle {
            anchors.fill: parent
            gradient: Gradient {
                GradientStop { position: 0.0; color: Qt.rgba(0, 0, 0, 0.55) }
                GradientStop { position: 0.3; color: Qt.rgba(0, 0, 0, 0.0) }
                GradientStop { position: 0.75; color: Qt.rgba(0, 0, 0, 0.1) }
                GradientStop { position: 1.0; color: Qt.rgba(0, 0, 0, 0.6) }
            }
        }
    }

    // --- Encabezado flotante --------------------------------------------------
    RowLayout {
        id: top
        anchors { left: parent.left; right: parent.right; top: parent.top; leftMargin: page.leftInset + 8; rightMargin: 24; topMargin: 20 }
        spacing: 10
        ColumnLayout {
            spacing: 0
            Text { text: "Broadcast Appliance"; color: Theme.text; font { family: Theme.font; pixelSize: 20; weight: Font.DemiBold } }
            Text { text: qsTr("Escena: %1   ·   %2").arg(Studio.currentScene).arg(Studio.canvasLabel); color: Theme.textDim; font.pixelSize: 12 }
        }
        Item { Layout.fillWidth: true }
        // Al aire = sale algo (Studio.alAire), igual que el cartel de arriba de las otras páginas.
        StatusPill { text: Studio.reconnecting ? qsTr("RECONECTANDO")
                           : Studio.transmitiendo ? (Studio.streaming ? qsTr("AL AIRE  %1").arg(Studio.streamTime) : qsTr("AL AIRE"))
                           : Studio.alAire ? qsTr("AL AIRE · GRABANDO") : qsTr("FUERA DEL AIRE")
                     dot: Studio.reconnecting ? Theme.amber : Theme.live; on: Studio.alAire || Studio.reconnecting; pulse: true }
        StatusPill { text: Studio.recording ? "REC  " + Studio.recordTime : "REC"; dot: Theme.accent; on: Studio.recording; pulse: true }
        // Se mide por «CPU 100%» y no por su propio texto: si no, al pasar de
        // una cifra a dos se ensancha y encoge varias veces por segundo,
        // moviendo todas las pildoras de al lado.
        StatusPill {
            text: "CPU " + Math.round(Studio.cpuPercent) + "%"
            medidaDe: "CPU 100%"
            pixelSize: 15
            dot: Studio.cpuPercent > 80 ? Theme.live : Theme.ok
        }
        // Lo que no anda de la asistencia con IA (clic: qué y dónde se arregla).
        PastillaIA {}
        Item { width: 8 }
        GlassButton {
            text: qsTr("Tarjeta"); icon: "+"
            onClicked: addMenu.popup()
            Menu {
                id: addMenu
                ItemsDeMenu {
                    menu: addMenu
                    model: Studio.dashboard.cardKinds
                    delegate: MenuItem {
                        required property string modelData
                        text: Studio.dashboard.kindTitle(modelData)
                        onTriggered: Studio.dashboard.addCard(modelData, modelData === "monitor" ? "program" : "")
                    }
                }
                // Las que vienen de Extensiones/. Van abajo y separadas: son
                // de otra procedencia y conviene que se note.
                MenuSeparator {
                    id: sepExtensiones
                    visible: Studio.extensiones && Studio.extensiones.cuantas > 0
                    height: visible ? implicitHeight : 0
                }
                ItemsDeMenu {
                    menu: addMenu
                    despuesDe: sepExtensiones
                    model: Studio.extensiones ? Studio.extensiones.kinds() : []
                    delegate: MenuItem {
                        required property string modelData
                        text: Studio.extensiones.tituloDeKind(modelData)
                        onTriggered: Studio.dashboard.addCard(modelData, "")
                    }
                }
            }
        }
        GlassButton {
            text: qsTr("Sacar a ventana"); icon: "⧉"
            onClicked: Studio.ventanas.abrir(["fondo"], "")
            ToolTip.visible: hovered_; ToolTip.text: qsTr("Abre el video de fondo en una ventana aparte (sin cortes), p. ej. para otro monitor")
            property bool hovered_: false
            HoverHandler { onHoveredChanged: parent.hovered_ = hovered }
        }
        GlassButton {
            text: qsTr("Fondo"); icon: "◐"
            onClicked: bgMenu.popup()
            Menu {
                id: bgMenu
                // Las multivistas armadas en la página Multiview: una de ellas como fondo.
                MenuItem { id: tituloMultiview; text: "Multiview"; enabled: false; visible: Studio.multiviews.length > 0; height: visible ? implicitHeight : 0 }
                ItemsDeMenu {
                    menu: bgMenu
                    despuesDe: tituloMultiview
                    model: Studio.multiviews
                    delegate: MenuItem {
                        required property var modelData
                        text: "▥  " + modelData.nombre + "  (" + (modelData.fuentes || []).length + ")"
                        checkable: true
                        checked: Studio.dashboard.backgroundLayout === "multiview:" + modelData.id
                        onTriggered: Studio.dashboard.backgroundLayout = "multiview:" + modelData.id
                    }
                }
                MenuSeparator { visible: Studio.multiviews.length > 0 }
                MenuItem { id: tituloFuentes; text: qsTr("Fuentes"); enabled: false; height: implicitHeight }
                ItemsDeMenu {
                    menu: bgMenu
                    despuesDe: tituloFuentes
                    model: Studio.frames.keys
                    delegate: MenuItem {
                        required property string modelData
                        text: Studio.frames.label(modelData)
                        checkable: true
                        checked: !bgLayer.esMultiview && Studio.dashboard.background.indexOf(modelData) >= 0
                        onTriggered: {
                            // Elegir una fuente deja la multivista y vuelve a las fuentes sueltas.
                            if (bgLayer.esMultiview) {
                                Studio.dashboard.backgroundLayout = "grid"
                                if (Studio.dashboard.background.indexOf(modelData) >= 0) return
                            }
                            const bg = Studio.dashboard.background.slice()
                            const i = bg.indexOf(modelData)
                            if (i >= 0) bg.splice(i, 1); else bg.push(modelData)
                            Studio.dashboard.background = bg
                        }
                    }
                }
                MenuSeparator { id: sepDistribucion }
                ItemsDeMenu {
                    menu: bgMenu
                    despuesDe: sepDistribucion
                    model: [ { id: "grid", t: qsTr("Distribucion: cuadricula") }, { id: "focus", t: qsTr("Distribucion: foco") },
                             { id: "pip", t: qsTr("Distribucion: imagen en imagen") }, { id: "row", t: qsTr("Distribucion: fila") } ]
                    delegate: MenuItem {
                        required property var modelData
                        text: modelData.t; checkable: true
                        enabled: !bgLayer.esMultiview
                        checked: Studio.dashboard.backgroundLayout === modelData.id
                        onTriggered: Studio.dashboard.backgroundLayout = modelData.id
                    }
                }
                MenuSeparator {}
                MenuItem { text: qsTr("Sacar a una ventana"); onTriggered: Studio.ventanas.abrir(["fondo"], "") }
                MenuSeparator {}
                MenuItem { text: qsTr("Mas oscuro"); onTriggered: Studio.dashboard.backgroundDim += 0.1 }
                MenuItem { text: qsTr("Mas claro"); onTriggered: Studio.dashboard.backgroundDim -= 0.1 }
                MenuSeparator {}
                MenuItem {
                    text: qsTr("Restablecer disposicion")
                    onTriggered: ApplicationWindow.window.confirm({
                        title: qsTr("¿Restablecer la disposición del tablero?"),
                        text: qsTr("Se pierden las tarjetas que agregaste, cómo las acomodaste y el fondo que elegiste: el tablero vuelve a como vino de fábrica."),
                        accept: qsTr("Restablecer"),
                        reject: qsTr("Cancelar"),
                        action: function() { Studio.dashboard.resetLayout() }
                    })
                }
            }
        }
    }

    // --- Area de tarjetas -------------------------------------------------------
    Item {
        id: cardArea
        anchors { left: parent.left; right: parent.right; top: top.bottom; bottom: parent.bottom
                  leftMargin: page.leftInset; rightMargin: barraTarjetas.anchoCerrada + 20; topMargin: 8; bottomMargin: 12 }

        // Grilla de encaje: solo mientras se mueve o redimensiona una tarjeta.
        Canvas {
            id: gridOverlay
            anchors.fill: parent
            visible: opacity > 0
            opacity: snapGuide.visible ? 1 : 0
            Behavior on opacity { NumberAnimation { duration: 140 } }
            onWidthChanged: requestPaint()
            onHeightChanged: requestPaint()
            onPaint: {
                const c = getContext("2d")
                c.reset()
                // fillRect y no stroke(): las líneas trazadas en Retina son caras (ver AudioPage).
                c.fillStyle = Qt.rgba(1, 1, 1, 0.10)
                const cols = Studio.dashboard.gridCols, rows = Studio.dashboard.gridRows
                for (let i = 1; i < cols; ++i) c.fillRect(Math.round(width * i / cols), 0, 1, height)
                for (let j = 1; j < rows; ++j) c.fillRect(0, Math.round(height * j / rows), width, 1)
            }
        }

        // Guia de encaje (se ve mientras se arrastra).
        Rectangle {
            id: snapGuide
            visible: false
            radius: Theme.radius
            color: Qt.rgba(1, 0.42, 0.17, 0.10)
            border { color: Qt.rgba(1, 0.6, 0.35, 0.8); width: 2 }
            Behavior on x { NumberAnimation { duration: 90; easing.type: Easing.OutCubic } }
            Behavior on y { NumberAnimation { duration: 90; easing.type: Easing.OutCubic } }
            Behavior on width { NumberAnimation { duration: 90 } }
            Behavior on height { NumberAnimation { duration: 90 } }
        }

        Repeater {
            model: Studio.dashboard
            delegate: GlassCard {
                id: card
                required cardId
                required fx
                required fy
                required fw
                required fh
                required property string kind
                required property string source
                // El titulo de una tarjeta de extension lo sabe su ficha, no
                // el modelo del dashboard: el modelo solo conoce las de
                // fabrica y devolveria «ext:reloj» pelado.
                required title
                onKindChanged: if (kind.startsWith("ext:") && Studio.extensiones)
                                   title = Studio.extensiones.tituloDeKind(kind)
                Component.onCompleted: if (kind.startsWith("ext:") && Studio.extensiones)
                                           title = Studio.extensiones.tituloDeKind(kind)
                area: cardArea
                backdrop: bgLayer
                backdropOffset: Qt.point(cardArea.x, cardArea.y)
                ghost: snapGuide
                onCloseRequested: Studio.dashboard.removeCard(cardId)

                // El punto de inserción: una tarjeta cuyo `kind` empieza con
                // «ext:» se resuelve contra Extensiones/<id>/ en el disco. El
                // resto sale de qrc, como siempre. Ver ExtensionesModel.hpp.
                Loader {
                    id: content
                    anchors.fill: parent
                    // T48: las extensiones se arman de a poco (no traban la interfaz) y con tiempo
                    // máximo: si en 5 s no terminaron, se cortan y quedan apagadas.
                    asynchronous: esExt
                    Timer {
                        interval: 5000
                        running: content.esExt && content.status === Loader.Loading
                        onTriggered: {
                            if (content.status !== Loader.Loading) return
                            content.source = ""
                            Studio.extensiones.tardo(content.extId, interval)
                        }
                    }
                    // UN LOADER ES UN AMBITO DE FOCO: sin esto, el foco se
                    // queda en el marco y las teclas de la tarjeta no llegan
                    // NUNCA a su contenido. No falla ni avisa: simplemente no
                    // pasa nada al apretar una flecha.
                    focus: true
                    readonly property bool esExt: card.kind.startsWith("ext:")
                    readonly property string extId: esExt ? card.kind.slice(4) : ""
                    source: {
                        if (!esExt)
                            return "../cards/" + card.kind.charAt(0).toUpperCase() +
                                   card.kind.slice(1) + "Content.qml"
                        if (!Studio.extensiones) return ""
                        // Se anota en el diario ANTES de que el QML se evalúe:
                        // si cuelga la interfaz, el arranque no termina y la
                        // próxima vez esta tarjeta queda apagada sola.
                        Studio.extensiones.cargando(extId)
                        return Studio.extensiones.vista(extId)
                    }
                    onStatusChanged: {
                        if (!esExt) return
                        if (status === Loader.Ready) Studio.extensiones.cargada(extId, true)
                        else if (status === Loader.Error)
                            Studio.extensiones.cargada(extId, false, "no se pudo cargar el QML")
                    }
                }

                // Una tarjeta rota lo dice en su lugar, en vez de quedar en
                // blanco: en blanco parece que no hace nada y se busca el
                // problema en otro lado.
                Text {
                    anchors { fill: parent; margins: 14 }
                    // `source` es una URL, no un texto: compararla contra ""
                    // da false aunque este vacia. Lo que dice que no hay nada
                    // que cargar es el estado Null.
                    visible: content.status === Loader.Error ||
                             (content.esExt && content.status === Loader.Null)
                    wrapMode: Text.WordWrap
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                    color: Theme.textDim
                    font.pixelSize: 11
                    text: {
                        if (!content.esExt) return qsTr("Esta tarjeta no se pudo cargar.")
                        const f = Studio.extensiones ? Studio.extensiones.ficha(content.extId) : null
                        if (!f || !f.id) return qsTr("La extensión «%1» ya no está.").arg(content.extId)
                        const vivo = Studio.extensiones.motivoNoCarga(content.extId)
                        if (vivo !== "") return qsTr("«%1»: %2").arg(f.nombre).arg(vivo)
                        if (f.apagada)
                            return qsTr("«%1» está apagada: se llevó puesto el arranque.\nSe prende desde el Registro.").arg(f.nombre)
                        if (f.problema) return qsTr("«%1»: %2").arg(f.nombre).arg(f.problema)
                        return qsTr("«%1» tiene un error de QML. Mirá el Registro, que trae el archivo y la línea.").arg(f.nombre)
                    }
                }
                Binding { target: content.item; property: "cardId"; value: card.cardId; when: content.status === Loader.Ready && (card.kind === "monitor" || card.kind === "ptz") }
                Binding { target: content.item; property: "source"; value: card.source; when: content.status === Loader.Ready && (card.kind === "monitor" || card.kind === "ptz") }
            }
        }
    }

    // --- Barra de tarjetas: abrir cada una como tarjeta o como ventana -----------------
    // Encima de las tarjetas (desplegada las tapa un rato; cerrada, tiene su lugar).
    BarraTarjetas {
        id: barraTarjetas
        z: 20
        anchors { right: parent.right; top: top.bottom; rightMargin: 12; topMargin: 8 }
    }
}
