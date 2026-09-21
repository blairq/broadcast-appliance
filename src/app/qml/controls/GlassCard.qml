// Tarjeta de vidrio del dashboard (solo presentacion).
//
// - Vidrio: desenfoca lo que hay DETRAS (el `backdrop`) con MultiEffect.
// - Se mueve arrastrando el encabezado y cambia de tamaño desde la esquina.
// - Al soltar, pide al modelo el lugar ajustado (grilla + imanes) y anima con
//   un resorte hacia ahi: el efecto "stick".
// - La posicion vive en el modelo (fracciones del area); aca solo se dibuja.
import QtQuick
import QtQuick.Controls
import QtQuick.Effects
import BA

Item {
    id: card

    property Item area          // contenedor del dashboard (coordenadas)
    property Item backdrop      // lo que se ve detras (para el vidrio)
    property Item ghost         // guia de encaje compartida
    property point backdropOffset: Qt.point(0, 0) // posicion del area dentro del backdrop
    property int cardId: 0
    property real fx: 0
    property real fy: 0
    property real fw: 0.25
    property real fh: 0.25
    property string title: ""
    property string subtitle: ""
    property bool closable: true
    property alias headerExtra: extraSlot.data
    default property alias content: body.data

    property bool moving: dragHandler.active || resizeArea.pressed

    x: area ? fx * area.width : 0
    y: area ? fy * area.height : 0
    width: area ? fw * area.width : 0
    height: area ? fh * area.height : 0

    // "Stick": al soltar, resorte hacia el lugar ajustado.
    Behavior on x { enabled: !card.moving; SpringAnimation { spring: 4.5; damping: 0.32; epsilon: 0.3 } }
    Behavior on y { enabled: !card.moving; SpringAnimation { spring: 4.5; damping: 0.32; epsilon: 0.3 } }
    Behavior on width { enabled: !card.moving; SpringAnimation { spring: 4.5; damping: 0.35; epsilon: 0.3 } }
    Behavior on height { enabled: !card.moving; SpringAnimation { spring: 4.5; damping: 0.35; epsilon: 0.3 } }

    signal closeRequested()

    function proposal() {
        return Studio.dashboard.snap(cardId, x / area.width, y / area.height,
                                     width / area.width, height / area.height,
                                     area.width / Math.max(1, area.height))
    }
    function showGhost() {
        const r = proposal()
        ghost.x = r.x * area.width; ghost.y = r.y * area.height
        ghost.width = r.width * area.width; ghost.height = r.height * area.height
        ghost.visible = true
    }
    function commit() {
        ghost.visible = false
        const r = proposal()
        Studio.dashboard.setCardRect(cardId, r.x, r.y, r.width, r.height)
        // Vuelve a atar la geometria al modelo (el arrastre rompio el binding).
        x = Qt.binding(() => fx * area.width)
        y = Qt.binding(() => fy * area.height)
        width = Qt.binding(() => fw * area.width)
        height = Qt.binding(() => fh * area.height)
    }

    // --- Sombra suave (MultiEffect: Qt 6.6 no tiene RectangularShadow) ------
    Rectangle { id: shadowSrc; anchors.fill: parent; radius: Theme.radius; color: "black"; visible: false }
    MultiEffect {
        anchors.fill: shadowSrc
        source: shadowSrc
        autoPaddingEnabled: true
        shadowEnabled: true
        shadowBlur: card.moving ? 1.0 : 0.7
        shadowVerticalOffset: card.moving ? 18 : 8
        shadowColor: Qt.rgba(0, 0, 0, card.moving ? 0.6 : 0.45)
        opacity: 0.9
    }

    // --- Vidrio: desenfoque de lo que hay detras ------------------------------
    Item {
        id: glass
        anchors.fill: parent
        scale: card.moving ? 1.015 : 1.0
        Behavior on scale { NumberAnimation { duration: 160; easing.type: Easing.OutCubic } }

        ShaderEffectSource {
            id: behind
            anchors.fill: parent
            sourceItem: card.backdrop
            sourceRect: Qt.rect(card.x + card.backdropOffset.x, card.y + card.backdropOffset.y, card.width, card.height)
            visible: false
            live: true
        }
        Rectangle { id: mask; anchors.fill: parent; radius: Theme.radius; visible: false; layer.enabled: true }
        MultiEffect {
            anchors.fill: parent
            source: behind
            blurEnabled: true
            blur: 1.0
            blurMax: 56
            saturation: 0.25
            brightness: -0.08
            maskEnabled: true
            maskSource: mask
        }
        Rectangle {
            anchors.fill: parent
            radius: Theme.radius
            color: Theme.glassTint
            border.color: card.moving ? Qt.rgba(1, 0.55, 0.3, 0.55) : Theme.glassBorder
            border.width: 1
            gradient: Gradient {
                GradientStop { position: 0.0; color: Theme.glassHighlight }
                GradientStop { position: 0.35; color: Theme.glassTint }
                GradientStop { position: 1.0; color: Qt.rgba(0.02, 0.02, 0.03, 0.55) }
            }
        }
    }

    // --- Encabezado (arrastre) -------------------------------------------------
    Item {
        id: header
        anchors { left: parent.left; right: parent.right; top: parent.top }
        height: 46

        Text {
            id: titleText
            anchors { left: parent.left; leftMargin: 18; verticalCenter: parent.verticalCenter }
            text: card.title
            color: Theme.text
            font { family: Theme.font; pixelSize: 16; weight: Font.DemiBold }
        }
        Text {
            anchors { left: titleText.right; leftMargin: 8; baseline: titleText.baseline }
            text: card.subtitle
            color: Theme.textFaint
            font { family: Theme.font; pixelSize: 11 }
        }
        Row {
            id: extraSlot
            anchors { right: closeBtn.left; rightMargin: 6; verticalCenter: parent.verticalCenter }
            spacing: 6
        }
        Text {
            id: closeBtn
            visible: card.closable
            anchors { right: parent.right; rightMargin: 14; verticalCenter: parent.verticalCenter }
            text: "✕"
            color: closeMa.containsMouse ? Theme.text : Theme.textFaint
            font.pixelSize: 13
            MouseArea { id: closeMa; anchors.fill: parent; anchors.margins: -8; hoverEnabled: true; onClicked: card.closeRequested() }
        }
        DragHandler {
            id: dragHandler
            target: card
            cursorShape: Qt.ClosedHandCursor
            onActiveChanged: {
                if (active) { Studio.dashboard.raise(card.cardId) } else { card.commit() }
            }
            onTranslationChanged: if (active) card.showGhost()
        }
        HoverHandler { cursorShape: Qt.OpenHandCursor }
    }

    // --- Contenido --------------------------------------------------------------
    Item {
        id: body
        anchors { left: parent.left; right: parent.right; top: header.bottom; bottom: parent.bottom
                  leftMargin: 16; rightMargin: 16; bottomMargin: 14 }
        clip: true
    }

    // --- Esquina de redimensionado ---------------------------------------------
    MouseArea {
        id: resizeArea
        width: 22; height: 22
        anchors { right: parent.right; bottom: parent.bottom }
        cursorShape: Qt.SizeFDiagCursor
        property point start
        property size startSize
        onPressed: (m) => { start = mapToItem(card.area, m.x, m.y); startSize = Qt.size(card.width, card.height) }
        onPositionChanged: (m) => {
            // Con hoverEnabled esto tambien llega SIN el boton apretado: sin este
            // corte la tarjeta se redimensionaba con solo pasar el mouse (con un
            // `start` de un arrastre anterior), y como la esquina va pegada al
            // borde, el cursor quedaba encima otra vez y crecia sola.
            if (!pressed) return
            const p = mapToItem(card.area, m.x, m.y)
            card.width = Math.max(140, startSize.width + p.x - start.x)
            card.height = Math.max(110, startSize.height + p.y - start.y)
            card.showGhost()
        }
        onReleased: card.commit()
        Canvas {
            anchors.fill: parent
            opacity: resizeArea.containsMouse || resizeArea.pressed ? 0.9 : 0.35
            onPaint: {
                const c = getContext("2d"); c.strokeStyle = "#ffffff"; c.lineWidth = 1.5
                c.beginPath(); c.moveTo(width - 6, 8); c.lineTo(8, height - 6); c.moveTo(width - 6, 14); c.lineTo(14, height - 6); c.stroke()
            }
        }
        hoverEnabled: true
    }
}
