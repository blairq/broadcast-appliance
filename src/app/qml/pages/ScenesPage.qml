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

    // --- Carpetas de escenas --------------------------------------------------
    // Solo ordenan la lista (el motor no las usa). El arbol se arma leyendo el
    // modelo de escenas, que informa la carpeta de cada una.
    property var closedFolders: ({})
    property int folderTick: 0
    property int scenesTick: 0
    function folderOpen(f) { folderTick; return !closedFolders[f] }
    function toggleFolder(f) { closedFolders[f] = !closedFolders[f]; folderTick++ }
    readonly property var sceneTree: {
        folderTick; scenesTick
        const m = Studio.scenes
        const sueltas = [], carpetas = [], porCarpeta = {}
        for (let i = 0; i < m.rowCount(); ++i) {
            const idx = m.index(i, 0)
            const fila = { fila: "escena",
                           nombre: m.data(idx, 257),   // NameRole
                           live: m.data(idx, 258),     // LiveRole
                           carpeta: m.data(idx, 259) } // FolderRole
            if (!fila.carpeta) { sueltas.push(fila); continue }
            if (!porCarpeta[fila.carpeta]) { porCarpeta[fila.carpeta] = []; carpetas.push(fila.carpeta) }
            porCarpeta[fila.carpeta].push(fila)
        }
        const filas = []
        if (carpetas.length > 0) {
            filas.push({ fila: "carpeta", carpeta: "", cuantas: sueltas.length })
            if (folderOpen("")) for (const f of sueltas) filas.push(f)
        } else {
            for (const f of sueltas) filas.push(f)
        }
        for (const c of carpetas) {
            filas.push({ fila: "carpeta", carpeta: c, cuantas: porCarpeta[c].length })
            if (folderOpen(c)) for (const f of porCarpeta[c]) filas.push(f)
        }
        return filas
    }
    Connections {
        target: Studio.scenes
        function onModelReset() { page.scenesTick++ }
        function onDataChanged() { page.scenesTick++ }
    }

    // El motor renderiza la escena que se edita solo mientras esta pagina esta
    // a la vista: fuera de aca no se gasta GPU en ella.
    onEditSceneChanged: if (visible) Studio.setScenePreview(editScene)
    onVisibleChanged: Studio.setScenePreview(visible ? editScene : "")
    Component.onCompleted: if (visible) Studio.setScenePreview(editScene)

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
                    id: sceneList
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    spacing: 6
                    // Arbol: un encabezado por carpeta (se pliega) y debajo sus
                    // escenas. Las que no tienen carpeta van primero, sueltas.
                    model: page.sceneTree
                    delegate: Item {
                        required property var modelData
                        width: ListView.view.width
                        height: modelData.fila === "carpeta" ? 28 : 46

                        Rectangle {
                            id: folderRow
                            anchors.fill: parent
                            visible: modelData.fila === "carpeta"
                            radius: 8
                            // Se ilumina cuando se le arrastra una escena encima.
                            color: dropHere.containsDrag ? Qt.rgba(1, 0.42, 0.17, 0.18) : "transparent"
                            border.color: dropHere.containsDrag ? Theme.accent : "transparent"
                            DropArea {
                                id: dropHere
                                // Un poco mas alta que el encabezado: es facil
                                // pasarse con el mouse.
                                anchors { fill: parent; topMargin: -6; bottomMargin: -6 }
                                property string folderName: modelData.carpeta
                                // El que se solto es el fantasma de la fila (su
                                // `escena` dice cual es).
                                onDropped: (ev) => {
                                    if (ev.source && ev.source.escena !== undefined)
                                        Studio.setSceneFolder(ev.source.escena, folderName)
                                }
                            }
                            RowLayout {
                                anchors { fill: parent; leftMargin: 2; rightMargin: 6 }
                                spacing: 6
                                Text {
                                    text: page.folderOpen(modelData.carpeta) ? "▾" : "▸"
                                    color: Theme.textFaint; font.pixelSize: 11
                                }
                                Text {
                                    Layout.fillWidth: true
                                    text: modelData.carpeta || "Sin carpeta"
                                    color: Theme.textDim
                                    font { pixelSize: 11; weight: Font.DemiBold; capitalization: Font.AllUppercase; letterSpacing: 0.4 }
                                    elide: Text.ElideRight
                                }
                                // `cuantas` puede no venir en una fila recién creada:
                                // sin el respaldo, Qt avisa «Unable to assign [undefined]
                                // to QString» en cada redibujo.
                                Text { text: modelData.cuantas !== undefined ? String(modelData.cuantas) : ""
                                       color: Theme.textFaint; font.pixelSize: 11 }
                            }
                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: page.toggleFolder(modelData.carpeta)
                            }
                        }

                        Rectangle {
                            anchors { fill: parent; leftMargin: modelData.carpeta ? 12 : 0 }
                            visible: modelData.fila === "escena"
                            readonly property string name: modelData.nombre || ""
                            readonly property bool live: !!modelData.live
                            radius: 12
                            color: name === page.editScene ? Qt.rgba(1, 1, 1, 0.12) : Qt.rgba(1, 1, 1, 0.04)
                            border.color: live ? Theme.accent : "transparent"
                            RowLayout {
                                anchors { fill: parent; leftMargin: 12; rightMargin: 8 }
                                Rectangle { width: 8; height: 8; radius: 4; color: parent.parent.live ? Theme.live : Theme.textFaint }
                                Text { text: parent.parent.name; color: Theme.text; Layout.fillWidth: true; elide: Text.ElideRight; font.pixelSize: 13 }
                                GlassButton { visible: !parent.parent.live; text: "Al aire"; compact: true; onClicked: Studio.takeScene(parent.parent.name) }
                            }
                            // Lo que se ve mientras se lleva la escena a una carpeta.
                            // `Drag.active` va ATADO al arrastre del MouseArea y el
                            // destino se resuelve en el `onDropped` de la carpeta: es
                            // el patron que funciona (leer Drag.target al soltar no,
                            // porque para entonces ya esta vacio).
                            Item {
                                id: rowGhost
                                property string escena: parent.name
                                width: 150
                                height: 28
                                visible: Drag.active
                                z: 999
                                Drag.active: rowMa.drag.active
                                Drag.hotSpot: Qt.point(20, 14)
                                Rectangle {
                                    anchors.fill: parent
                                    radius: 8
                                    color: Qt.rgba(0.1, 0.12, 0.18, 0.95)
                                    border.color: Theme.accent
                                    Text {
                                        anchors { fill: parent; leftMargin: 8; rightMargin: 8 }
                                        verticalAlignment: Text.AlignVCenter
                                        text: "📁 " + rowGhost.escena
                                        color: Theme.text; font.pixelSize: 12; elide: Text.ElideRight
                                    }
                                }
                            }
                            // Soltar una escena encima de esta la deja justo antes
                            // (y en la misma carpeta): asi se reordena a mano.
                            DropArea {
                                id: dropRow
                                anchors.fill: parent
                                onDropped: (ev) => {
                                    if (ev.source && ev.source.escena !== undefined)
                                        Studio.moveScene(ev.source.escena, parent.name)
                                }
                            }
                            // Linea que marca donde va a caer.
                            Rectangle {
                                anchors { left: parent.left; right: parent.right; top: parent.top }
                                height: 3
                                radius: 2
                                visible: dropRow.containsDrag
                                color: Theme.accent
                            }
                            MouseArea {
                                id: rowMa
                                anchors.fill: parent; z: -1
                                preventStealing: true
                                cursorShape: drag.active ? Qt.ClosedHandCursor : Qt.ArrowCursor
                                drag.target: rowGhost
                                onPressed: (m) => {
                                    rowGhost.x = m.x - 20
                                    rowGhost.y = m.y - 14
                                }
                                onReleased: if (rowGhost.Drag.active) rowGhost.Drag.drop()
                                onClicked: { Studio.sources.scene = parent.name; page.selected = "" }
                                onDoubleClicked: { renameDlg.from = parent.name; renameDlg.open() }
                            }
                        }
                    }
                }
                Flow {
                    Layout.fillWidth: true
                    spacing: 6
                    GlassButton { text: "Escena"; icon: "+"; compact: true; onClicked: newSceneDlg.open() }
                    GlassButton {
                        text: "Copiar"; icon: "⧉"; compact: true
                        // La forma barata de armar escenas parecidas: se hace
                        // UNA bien y las demás salen de copiarla. El nombre lo
                        // propone solo; renombrar es un clic más.
                        onClicked: {
                            const nueva = Studio.copyScene(page.editScene, "")
                            if (nueva !== "") page.editScene = nueva
                        }
                        ToolTip.visible: hovered; ToolTip.delay: 400
                        ToolTip.text: "Una escena nueva con todas las fuentes de «" + page.editScene + "»"
                    }
                    GlassButton {
                        text: "Renombrar"; compact: true
                        enabled: !Studio.sceneProtected(page.editScene)
                        onClicked: { renameDlg.from = page.editScene; renameDlg.open() }
                        ToolTip.visible: hovered && !enabled
                        ToolTip.text: "«" + page.editScene + "» está protegida"
                    }
                    GlassButton {
                        text: Studio.sceneProtected(page.editScene) ? "Protegida" : "Proteger"
                        icon: Studio.sceneProtected(page.editScene) ? "🔒" : "🔓"
                        compact: true
                        active: Studio.sceneProtected(page.editScene)
                        onClicked: Studio.setSceneProtected(page.editScene,
                                                            !Studio.sceneProtected(page.editScene))
                        ToolTip.visible: hovered; ToolTip.delay: 400
                        ToolTip.text: "Una escena protegida no se borra ni se renombra. Copiarla sí.\n" +
                                      "Para la escena base de la que salen las demás."
                    }
                    GlassButton {
                        text: "Carpeta"; icon: "+"; compact: true
                        onClicked: { folderDlg.scene = page.editScene; folderDlg.open() }
                        ToolTip.visible: hovered; ToolTip.delay: 400
                        ToolTip.text: "Mover «" + page.editScene + "» a una carpeta (o arrastrala hasta la carpeta)"
                    }
                    GlassButton {
                        text: "Borrar"; compact: true
                        enabled: !Studio.sceneProtected(page.editScene)
                        ToolTip.visible: hovered && !enabled
                        ToolTip.text: "«" + page.editScene + "» está protegida: sacale el candado primero"
                        onClicked: {
                            const escena = page.editScene
                            // La unica escena no se puede borrar: el motor lo avisa, sin preguntar antes.
                            if (sceneList.count < 2) { Studio.removeScene(escena); return }
                            // El motor deja borrar la escena al aire: corta en seco a otra.
                            const alAire = escena === Studio.currentScene
                            ApplicationWindow.window.confirm({
                                title: "¿Borrar la escena «" + escena + "»?",
                                text: (alAire ? "«" + escena + "» está AL AIRE: al borrarla se corta a otra escena. " : "")
                                      + "Se pierden su disposición y las fuentes que solo están en ella.",
                                accept: "Borrar",
                                reject: "Cancelar",
                                action: function() { Studio.removeScene(escena) }
                            })
                        }
                    }
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
                    text: "  (fuera del aire)"
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
                    // La escena que se esta editando, renderizada aunque no este
                    // al aire: una escena vacia se ve vacia (antes el lienzo
                    // mostraba el programa y parecia contenido de esta escena).
                    VideoItem { anchors.fill: parent; source: "scene" }
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
                                Toggle { checked: shown; onToggled: (wanted) => Studio.sources.setVisible(name, wanted) }
                                ToolButton { text: "▲"; onClicked: Studio.sources.move(name, 1) }
                                ToolButton { text: "▼"; onClicked: Studio.sources.move(name, -1) }
                                ToolButton {
                                    text: "✕"
                                    ToolTip.visible: hovered; ToolTip.text: "Quitar de la escena"; ToolTip.delay: 400
                                    onClicked: {
                                        // Copias: este delegate puede no existir cuando el operador conteste.
                                        const fuente = name
                                        const escena = page.editScene
                                        const pagina = page   // el id no resuelve si el delegate ya murio
                                        // Sale solo de ESTA escena; el motor libera la fuente si no queda en ninguna.
                                        ApplicationWindow.window.confirm({
                                            title: "¿Quitar «" + fuente + "» de la escena?",
                                            text: (escena === Studio.currentScene
                                                   ? "«" + escena + "» está AL AIRE: la fuente sale del programa al instante. " : "")
                                                  + "Se quita solo de esta escena; si no está en ninguna otra, se borra con su configuración.",
                                            accept: "Quitar",
                                            reject: "Cancelar",
                                            action: function() {
                                                // Si mientras tanto el editor paso a otra escena, no se toca nada.
                                                if (Studio.sources.scene !== escena) return
                                                if (pagina.selected === fuente) pagina.selected = ""
                                                Studio.sources.remove(fuente)
                                            }
                                        })
                                    }
                                }
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
                            MenuSeparator { visible: Studio.sources.guestSlots > 0 }
                            // Invitados de la sala (WebRTC): camara y pantalla de cada lugar.
                            Repeater {
                                model: Studio.sources.guestSlots * 2
                                delegate: MenuItem {
                                    required property int index
                                    readonly property int slot: Math.floor(index / 2) + 1
                                    readonly property bool share: index % 2 === 1
                                    readonly property string who: (Studio.conference.slotNames[slot - 1] || "")
                                    text: "Invitado " + slot + (share ? " — pantalla" : "") + (who ? "  (" + who + ")" : "")
                                    onTriggered: { const n = Studio.sources.addGuest(slot, share); if (n) page.selected = n }
                                }
                            }
                        }
                    }
                    // Una camara o capturadora NO se puede abrir dos veces (el
                    // dispositivo es exclusivo), pero SI puede estar en varias
                    // escenas: es la MISMA fuente, con otra ubicacion. Asi
                    // ademas la transicion entre dos encuadres no tiene cortes.
                    GlassButton {
                        text: "Reusar fuente"; icon: "⧉"; compact: true
                        onClicked: { reuseMenu.items = Studio.sources.otherSources(); reuseMenu.popup() }
                        Menu {
                            id: reuseMenu
                            property var items: []
                            Repeater {
                                model: reuseMenu.items
                                delegate: MenuItem {
                                    required property var modelData
                                    text: modelData.name + "   (" + modelData.typeName + ")"
                                    onTriggered: {
                                        const n = Studio.sources.addExisting(modelData.name)
                                        if (n) page.selected = n
                                    }
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
    // Mover una escena a una carpeta (o sacarla, dejando el nombre vacio).
    Dialog {
        id: folderDlg
        property string scene: ""
        anchors.centerIn: parent
        modal: true
        title: "Carpeta de «" + scene + "»"
        onOpened: folderField.forceActiveFocus()
        footer: DialogButtonBox {
            GlassButton { text: "Guardar"; active: true; onClicked: { Studio.setSceneFolder(folderDlg.scene, folderField.text); folderDlg.close() } }
            GlassButton { text: "Cancelar"; onClicked: folderDlg.close() }
        }
        ColumnLayout {
            spacing: 8
            Label { text: "Nombre de la carpeta (vacío = sin carpeta)" }
            TextField {
                id: folderField
                Layout.preferredWidth: 320
                placeholderText: "p. ej. Bloque 1, Invitados, Placas"
                onAccepted: { Studio.setSceneFolder(folderDlg.scene, text); folderDlg.close() }
            }
            Flow {
                Layout.preferredWidth: 320
                spacing: 6
                Repeater {
                    model: Studio.sceneFolders()
                    delegate: GlassButton {
                        required property string modelData
                        compact: true
                        text: modelData
                        onClicked: folderField.text = modelData
                    }
                }
            }
        }
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
