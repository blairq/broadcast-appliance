// Editor de escenas: escenas | lienzo con capas editables | fuentes y propiedades.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import BA
import "../controls"

Rectangle {
    id: page
    color: Theme.bg
    property real leftInset: 92
    property string selected: ""
    readonly property string editScene: Studio.sources.scene

    onSelectedChanged: Studio.properties.sourceName = selected

    RowLayout {
        anchors { fill: parent; leftMargin: page.leftInset; margins: 20 }
        spacing: 16

        // --- Escenas ----------------------------------------------------------
        Panel {
            Layout.preferredWidth: 240
            Layout.fillHeight: true
            title: "Escenas"
            ColumnLayout {
                anchors.fill: parent
                spacing: 8
                ListView {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    spacing: 6
                    model: Studio.scenes
                    delegate: Rectangle {
                        required property string name
                        required property bool live
                        width: ListView.view.width
                        height: 46
                        radius: 12
                        color: name === page.editScene ? Qt.rgba(1, 1, 1, 0.12) : Qt.rgba(1, 1, 1, 0.04)
                        border.color: live ? Theme.accent : "transparent"
                        RowLayout {
                            anchors { fill: parent; leftMargin: 12; rightMargin: 8 }
                            Rectangle { width: 8; height: 8; radius: 4; color: live ? Theme.live : Theme.textFaint }
                            Text { text: name; color: Theme.text; Layout.fillWidth: true; elide: Text.ElideRight; font.pixelSize: 13 }
                            GlassButton { visible: !live; text: "Al aire"; compact: true; onClicked: Studio.takeScene(name) }
                        }
                        MouseArea {
                            anchors.fill: parent; z: -1
                            onClicked: { Studio.sources.scene = name; page.selected = "" }
                            onDoubleClicked: { renameDlg.from = name; renameDlg.open() }
                        }
                    }
                }
                RowLayout {
                    GlassButton { text: "Nueva"; icon: "+"; compact: true; onClicked: newSceneDlg.open() }
                    GlassButton { text: "Renombrar"; compact: true; onClicked: { renameDlg.from = page.editScene; renameDlg.open() } }
                    GlassButton { text: "Borrar"; compact: true; onClicked: Studio.removeScene(page.editScene) }
                }
            }
        }

        // --- Lienzo ---------------------------------------------------------------
        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 10
            RowLayout {
                Text { text: page.editScene; color: Theme.text; font { pixelSize: 18; weight: Font.DemiBold } }
                Text {
                    visible: page.editScene !== Studio.currentScene
                    text: "  (fuera del aire: el lienzo muestra el programa; las capas son de esta escena)"
                    color: Theme.textFaint; font.pixelSize: 11
                }
                Item { Layout.fillWidth: true }
                Text { text: "Arrastrar para mover · esquina para redimensionar"; color: Theme.textFaint; font.pixelSize: 11 }
            }
            Item {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Rectangle {
                    id: canvas
                    readonly property real aspect: Studio.canvasAspect
                    width: Math.min(parent.width, parent.height * aspect)
                    height: width / aspect
                    anchors.centerIn: parent
                    color: "black"
                    radius: 8
                    clip: true
                    VideoItem { anchors.fill: parent; source: "program" }
                    // Capas de la escena editada (solo las de video).
                    Repeater {
                        model: Studio.sources
                        delegate: Item {
                            id: layer
                            required property string name
                            required property bool isVideo
                            required property bool shown
                            required property real rx
                            required property real ry
                            required property real rw
                            required property real rh
                            visible: isVideo && shown
                            x: rx * canvas.width; y: ry * canvas.height
                            width: rw * canvas.width; height: rh * canvas.height
                            z: name === page.selected ? 10 : 1
                            Rectangle {
                                anchors.fill: parent
                                color: name === page.selected ? Qt.rgba(1, 0.42, 0.17, 0.12) : "transparent"
                                border.color: name === page.selected ? Theme.accent : Qt.rgba(1, 1, 1, 0.35)
                                border.width: name === page.selected ? 2 : 1
                            }
                            Text { anchors { left: parent.left; top: parent.top; margins: 4 }
                                   text: name; color: Theme.text; font.pixelSize: 10
                                   style: Text.Outline; styleColor: "black" }
                            function commit() {
                                Studio.sources.setRect(name, x / canvas.width, y / canvas.height, width / canvas.width, height / canvas.height)
                                x = Qt.binding(() => rx * canvas.width); y = Qt.binding(() => ry * canvas.height)
                                width = Qt.binding(() => rw * canvas.width); height = Qt.binding(() => rh * canvas.height)
                            }
                            DragHandler {
                                target: layer
                                onActiveChanged: { if (active) page.selected = layer.name; else layer.commit() }
                            }
                            TapHandler { onTapped: page.selected = layer.name }
                            Rectangle {
                                visible: layer.name === page.selected
                                width: 14; height: 14; radius: 3
                                color: Theme.accent
                                anchors { right: parent.right; bottom: parent.bottom; margins: -7 }
                                MouseArea {
                                    anchors.fill: parent
                                    cursorShape: Qt.SizeFDiagCursor
                                    property point start; property size s0
                                    onPressed: (m) => { start = mapToItem(canvas, m.x, m.y); s0 = Qt.size(layer.width, layer.height) }
                                    onPositionChanged: (m) => { const p = mapToItem(canvas, m.x, m.y)
                                        layer.width = Math.max(20, s0.width + p.x - start.x)
                                        layer.height = Math.max(20, s0.height + p.y - start.y) }
                                    onReleased: layer.commit()
                                }
                            }
                        }
                    }
                }
            }
        }

        // --- Fuentes y propiedades -------------------------------------------------
        ColumnLayout {
            Layout.preferredWidth: 420
            Layout.fillHeight: true
            spacing: 12
            Panel {
                Layout.fillWidth: true
                Layout.preferredHeight: 280
                title: "Fuentes"
                ColumnLayout {
                    anchors.fill: parent
                    spacing: 6
                    ListView {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        spacing: 4
                        model: Studio.sources
                        delegate: Rectangle {
                            required property string name
                            required property string typeName
                            required property bool shown
                            width: ListView.view.width
                            height: 40
                            radius: 10
                            color: name === page.selected ? Qt.rgba(1, 0.42, 0.17, 0.25) : Qt.rgba(1, 1, 1, 0.05)
                            RowLayout {
                                anchors { fill: parent; leftMargin: 10; rightMargin: 4 }
                                spacing: 2
                                ColumnLayout {
                                    spacing: 0; Layout.fillWidth: true
                                    Text { text: name; color: Theme.text; elide: Text.ElideRight; Layout.fillWidth: true; font.pixelSize: 12 }
                                    Text { text: typeName; color: Theme.textFaint; elide: Text.ElideRight; Layout.fillWidth: true; font.pixelSize: 9 }
                                }
                                Toggle { checked: shown; onToggled: Studio.sources.setVisible(name, checked) }
                                ToolButton { text: "▲"; onClicked: Studio.sources.move(name, 1) }
                                ToolButton { text: "▼"; onClicked: Studio.sources.move(name, -1) }
                                ToolButton { text: "✕"; onClicked: { if (page.selected === name) page.selected = ""; Studio.sources.remove(name) } }
                            }
                            MouseArea { anchors.fill: parent; z: -1; onClicked: page.selected = name }
                        }
                    }
                    GlassButton {
                        text: "Agregar fuente"; icon: "+"; compact: true
                        onClicked: addSrcMenu.popup()
                        Menu {
                            id: addSrcMenu
                            Repeater {
                                model: Studio.sourceTypes
                                delegate: MenuItem {
                                    required property string typeId
                                    required property string displayName
                                    text: displayName
                                    onTriggered: { const n = Studio.sources.add(typeId, displayName); if (n) page.selected = n }
                                }
                            }
                        }
                    }
                }
            }
            Panel {
                Layout.fillWidth: true
                Layout.fillHeight: true
                title: page.selected ? "Propiedades — " + page.selected : "Propiedades"
                ScrollView {
                    anchors.fill: parent
                    clip: true
                    contentWidth: availableWidth
                    ColumnLayout {
                        width: parent.width
                        spacing: 10
                        Text { visible: !page.selected; text: "Elegi una fuente para ver sus propiedades."; color: Theme.textFaint; font.pixelSize: 12 }
                        Repeater {
                            model: Studio.properties
                            delegate: FormField {
                                required property string name
                                required label
                                required kind
                                required value
                                required options
                                required minimum
                                required maximum
                                required step
                                required suffix
                                required help
                                required enabled
                                Layout.fillWidth: true
                                onEdited: (v) => Studio.properties.setValue(name, v)
                                onPressed: Studio.properties.press(name)
                            }
                        }
                    }
                }
            }
        }
    }

    // --- Dialogos ---------------------------------------------------------------
    Dialog {
        id: newSceneDlg
        title: "Nueva escena"
        anchors.centerIn: parent
        modal: true
        standardButtons: Dialog.Ok | Dialog.Cancel
        TextField { id: newSceneName; width: 280; placeholderText: "Nombre de la escena" }
        onAccepted: { if (newSceneName.text) Studio.addScene(newSceneName.text); newSceneName.text = "" }
    }
    Dialog {
        id: renameDlg
        property string from: ""
        title: "Renombrar escena"
        anchors.centerIn: parent
        modal: true
        standardButtons: Dialog.Ok | Dialog.Cancel
        onOpened: renameField.text = from
        TextField { id: renameField; width: 280 }
        onAccepted: if (renameField.text && renameField.text !== from) Studio.renameScene(from, renameField.text)
    }
}
