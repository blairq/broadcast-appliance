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
        VideoWall {
            anchors.fill: parent
            keys: Studio.dashboard.background
            layout: Studio.dashboard.backgroundLayout
            gap: 2
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
            Text { text: "Escena: " + Studio.currentScene + "   ·   " + Studio.canvasLabel; color: Theme.textDim; font.pixelSize: 12 }
        }
        Item { Layout.fillWidth: true }
        StatusPill { text: Studio.streaming ? "AL AIRE  " + Studio.streamTime : "FUERA DEL AIRE"; dot: Theme.live; on: Studio.streaming; pulse: true }
        StatusPill { text: Studio.recording ? "REC  " + Studio.recordTime : "REC"; dot: Theme.accent; on: Studio.recording; pulse: true }
        StatusPill { text: "CPU " + Math.round(Studio.cpuPercent) + "%"; dot: Studio.cpuPercent > 80 ? Theme.live : Theme.ok }
        Item { width: 8 }
        GlassButton {
            text: "Tarjeta"; icon: "+"
            onClicked: addMenu.popup()
            Menu {
                id: addMenu
                Repeater {
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
                    visible: Studio.extensiones && Studio.extensiones.cuantas > 0
                    height: visible ? implicitHeight : 0
                }
                Repeater {
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
            text: "Sacar a ventana"; icon: "⧉"
            onClicked: Studio.dashboard.windows.open(Studio.dashboard.background, Studio.dashboard.backgroundLayout)
            ToolTip.visible: hovered_; ToolTip.text: "Abre el video de fondo en una ventana aparte (sin cortes), p. ej. para otro monitor"
            property bool hovered_: false
            HoverHandler { onHoveredChanged: parent.hovered_ = hovered }
        }
        GlassButton {
            text: "Fondo"; icon: "◐"
            onClicked: bgMenu.popup()
            Menu {
                id: bgMenu
                Repeater {
                    model: Studio.frames.keys
                    delegate: MenuItem {
                        required property string modelData
                        text: Studio.frames.label(modelData)
                        checkable: true
                        checked: Studio.dashboard.background.indexOf(modelData) >= 0
                        onTriggered: {
                            const bg = Studio.dashboard.background.slice()
                            const i = bg.indexOf(modelData)
                            if (i >= 0) bg.splice(i, 1); else bg.push(modelData)
                            Studio.dashboard.background = bg
                        }
                    }
                }
                MenuSeparator {}
                Repeater {
                    model: [ { id: "grid", t: "Distribucion: cuadricula" }, { id: "focus", t: "Distribucion: foco" },
                             { id: "pip", t: "Distribucion: imagen en imagen" }, { id: "row", t: "Distribucion: fila" } ]
                    delegate: MenuItem {
                        required property var modelData
                        text: modelData.t; checkable: true
                        checked: Studio.dashboard.backgroundLayout === modelData.id
                        onTriggered: Studio.dashboard.backgroundLayout = modelData.id
                    }
                }
                MenuSeparator {}
                MenuItem { text: "Sacar a una ventana"; onTriggered: Studio.dashboard.windows.open(Studio.dashboard.background, Studio.dashboard.backgroundLayout) }
                MenuSeparator {}
                MenuItem { text: "Mas oscuro"; onTriggered: Studio.dashboard.backgroundDim += 0.1 }
                MenuItem { text: "Mas claro"; onTriggered: Studio.dashboard.backgroundDim -= 0.1 }
                MenuSeparator {}
                MenuItem {
                    text: "Restablecer disposicion"
                    onTriggered: ApplicationWindow.window.confirm({
                        title: "¿Restablecer la disposición del tablero?",
                        text: "Se pierden las tarjetas que agregaste, cómo las acomodaste y el fondo que elegiste: el tablero vuelve a como vino de fábrica.",
                        accept: "Restablecer",
                        reject: "Cancelar",
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
                  leftMargin: page.leftInset; rightMargin: 12; topMargin: 8; bottomMargin: 12 }

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
                c.strokeStyle = Qt.rgba(1, 1, 1, 0.10)
                c.lineWidth = 1
                const cols = Studio.dashboard.gridCols, rows = Studio.dashboard.gridRows
                c.beginPath()
                for (let i = 1; i < cols; ++i) {
                    const x = Math.round(width * i / cols) + 0.5
                    c.moveTo(x, 0); c.lineTo(x, height)
                }
                for (let j = 1; j < rows; ++j) {
                    const y = Math.round(height * j / rows) + 0.5
                    c.moveTo(0, y); c.lineTo(width, y)
                }
                c.stroke()
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
                        if (!content.esExt) return "Esta tarjeta no se pudo cargar."
                        const f = Studio.extensiones ? Studio.extensiones.ficha(content.extId) : null
                        if (!f || !f.id) return "La extensión «" + content.extId + "» ya no está."
                        if (f.apagada)
                            return "«" + f.nombre + "» está apagada: se llevó puesto el arranque.\n" +
                                   "Se prende desde el Registro."
                        if (f.problema) return "«" + f.nombre + "»: " + f.problema
                        return "«" + f.nombre + "» tiene un error de QML. Mirá el Registro, que trae " +
                               "el archivo y la línea."
                    }
                }
                Binding { target: content.item; property: "cardId"; value: card.cardId; when: content.status === Loader.Ready && card.kind === "monitor" }
                Binding { target: content.item; property: "source"; value: card.source; when: content.status === Loader.Ready && card.kind === "monitor" }
            }
        }
    }
}
