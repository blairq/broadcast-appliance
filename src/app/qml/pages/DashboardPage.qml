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
                required title
                required property string kind
                required property string source
                area: cardArea
                backdrop: bgLayer
                backdropOffset: Qt.point(cardArea.x, cardArea.y)
                ghost: snapGuide
                onCloseRequested: Studio.dashboard.removeCard(cardId)

                Loader {
                    id: content
                    anchors.fill: parent
                    source: "../cards/" + kind.charAt(0).toUpperCase() + kind.slice(1) + "Content.qml"
                }
                Binding { target: content.item; property: "cardId"; value: card.cardId; when: content.status === Loader.Ready && card.kind === "monitor" }
                Binding { target: content.item; property: "source"; value: card.source; when: content.status === Loader.Ready && card.kind === "monitor" }
            }
        }
    }
}
