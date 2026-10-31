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

    // ui_abrir_panel {page: "escenas", panel: "efectos", fuente}: elige la fuente y
    // muestra sus efectos (transparencia, borde difuso, sombra) en Propiedades.
    Connections {
        target: Studio
        function onAbrirPanel(pagina, panel) {
            if (pagina !== "escenas") return
            if (panel === "cerrar") { page.selected = ""; return }
            if (panel.startsWith("efectos:")) page.selected = panel.substring(8)
        }
    }

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
            Layout.preferredWidth: 310
            Layout.maximumWidth: 310
            Layout.fillHeight: true
            title: qsTr("Escenas")
            ColumnLayout {
                anchors.fill: parent
                spacing: 8
                // Con varias salidas: armar y cortar van a la elegida (cada operador la suya).
                SelectorSalida { Layout.fillWidth: true }
                // FTB y placa a un toque (encima de todo lo que sale).
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 4
                    BotonesSeguridad { Layout.fillWidth: true; angosta: true }
                    FilaDsk { Layout.fillWidth: true }
                }
                CheckBox {
                    text: qsTr("Corte directo (sin preview)")
                    checked: Studio.corteDirectoEditor
                    onToggled: Studio.corteDirectoEditor = checked
                    ToolTip.visible: hovered; ToolTip.delay: 400
                    ToolTip.text: qsTr("Apagado (recomendado): el botón de cada escena la ARMA en el preview y sale con TAKE. Prendido: corta directo (al aire pide confirmación).")
                }
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
                                    text: modelData.carpeta || qsTr("Sin carpeta")
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
                                // Arma la escena en el preview (el corte va con TAKE). Con el
                                // corte directo prendido corta, y al aire pide confirmación.
                                GlassButton {
                                    readonly property string esc: parent.parent.name
                                    visible: !parent.parent.live
                                    compact: true
                                    text: Studio.corteDirectoEditor ? qsTr("Al aire") : (Studio.escenaArmada === esc ? qsTr("Armada") : qsTr("Armar"))
                                    active: !Studio.corteDirectoEditor
                                    activeColor: "#16a34a"
                                    onClicked: {
                                        if (!Studio.corteDirectoEditor) { Studio.armarEscena(esc); return }
                                        if (!Studio.aireActiva.alAire) { Studio.takeScene(esc); return } // el aire de la salida que maneja
                                        const n = esc
                                        ApplicationWindow.window.confirm({
                                            title: qsTr("¿Cortar a «%1» al aire?").arg(n),
                                            text: qsTr("Es un corte directo, sin pasar por el preview."),
                                            accept: qsTr("Cortar"), reject: qsTr("Cancelar"),
                                            action: function() { Studio.takeScene(n) }
                                        })
                                    }
                                }
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
                    GlassButton { text: qsTr("Escena"); icon: "+"; compact: true; onClicked: newSceneDlg.open() }
                    GlassButton {
                        text: qsTr("Copiar"); icon: "⧉"; compact: true
                        // La forma barata de armar escenas parecidas: se hace
                        // UNA bien y las demás salen de copiarla. El nombre lo
                        // propone solo; renombrar es un clic más.
                        onClicked: {
                            const nueva = Studio.copyScene(page.editScene, "")
                            if (nueva !== "") page.editScene = nueva
                        }
                        ToolTip.visible: hovered; ToolTip.delay: 400
                        ToolTip.text: qsTr("Una escena nueva con todas las fuentes de «%1»").arg(page.editScene)
                    }
                    GlassButton {
                        text: qsTr("Renombrar"); compact: true
                        enabled: !Studio.sceneProtected(page.editScene)
                        onClicked: { renameDlg.from = page.editScene; renameDlg.open() }
                        ToolTip.visible: hovered && !enabled
                        ToolTip.text: qsTr("«%1» está protegida").arg(page.editScene)
                    }
                    GlassButton {
                        text: Studio.sceneProtected(page.editScene) ? qsTr("Protegida") : qsTr("Proteger")
                        icon: Studio.sceneProtected(page.editScene) ? "🔒" : "🔓"
                        compact: true
                        active: Studio.sceneProtected(page.editScene)
                        onClicked: Studio.setSceneProtected(page.editScene,
                                                            !Studio.sceneProtected(page.editScene))
                        ToolTip.visible: hovered; ToolTip.delay: 400
                        ToolTip.text: qsTr("Una escena protegida no se borra ni se renombra. Copiarla sí.\n" +
                                      "Para la escena base de la que salen las demás.")
                    }
                    GlassButton {
                        text: qsTr("Carpeta"); icon: "+"; compact: true
                        onClicked: { folderDlg.scene = page.editScene; folderDlg.open() }
                        ToolTip.visible: hovered; ToolTip.delay: 400
                        ToolTip.text: qsTr("Mover «%1» a una carpeta (o arrastrala hasta la carpeta)").arg(page.editScene)
                    }
                    GlassButton {
                        text: qsTr("Borrar"); compact: true
                        enabled: !Studio.sceneProtected(page.editScene)
                        ToolTip.visible: hovered && !enabled
                        ToolTip.text: qsTr("«%1» está protegida: sacale el candado primero").arg(page.editScene)
                        onClicked: {
                            const escena = page.editScene
                            // La unica escena no se puede borrar: el motor lo avisa, sin preguntar antes.
                            if (sceneList.count < 2) { Studio.removeScene(escena); return }
                            // El motor deja borrar la escena al aire: corta en seco a otra.
                            const alAire = escena === Studio.currentScene
                            ApplicationWindow.window.confirm({
                                title: qsTr("¿Borrar la escena «%1»?").arg(escena),
                                text: (alAire ? (Studio.alAire ? qsTr("«%1» está AL AIRE: al borrarla se corta a otra escena.")
                                                               : qsTr("«%1» es la del programa: al borrarla se corta a otra escena.")).arg(escena) + " " : "")
                                      + qsTr("Se pierden su disposición y las fuentes que solo están en ella."),
                                accept: qsTr("Borrar"),
                                reject: qsTr("Cancelar"),
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
                    text: qsTr("  (fuera del aire)")
                    color: Theme.textFaint; font.pixelSize: 11
                }
                Item { Layout.fillWidth: true }
                Text { text: qsTr("Arrastrar para mover · esquina para redimensionar"); color: Theme.textFaint; font.pixelSize: 11 }
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
            Layout.fillWidth: false  // ancho fijo aunque un texto largo empuje (ver EquiposPage)
            Layout.preferredWidth: 420
            Layout.maximumWidth: 420
            Layout.fillHeight: true
            spacing: 12
            Panel {
                Layout.fillWidth: true
                // Media columna: con 280 fijos entraban tres fuentes y
                // Propiedades, casi siempre vacía, se quedaba con el resto.
                Layout.preferredHeight: Math.max(280, (parent.height - 12) / 2)
                title: qsTr("Fuentes")
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
                                    ToolTip.visible: hovered; ToolTip.text: qsTr("Quitar de la escena"); ToolTip.delay: 400
                                    onClicked: {
                                        // Copias: este delegate puede no existir cuando el operador conteste.
                                        const fuente = name
                                        const escena = page.editScene
                                        const pagina = page   // el id no resuelve si el delegate ya murio
                                        // Sale solo de ESTA escena; el motor libera la fuente si no queda en ninguna.
                                        ApplicationWindow.window.confirm({
                                            title: qsTr("¿Quitar «%1» de la escena?").arg(fuente),
                                            text: (escena === Studio.currentScene
                                                   ? (Studio.alAire ? qsTr("«%1» está AL AIRE: la fuente sale del programa al instante.")
                                                                    : qsTr("«%1» es la del programa: la fuente sale del programa al instante.")).arg(escena) + " " : "")
                                                  + qsTr("Se quita solo de esta escena; si no está en ninguna otra, se borra con su configuración."),
                                            accept: qsTr("Quitar"),
                                            reject: qsTr("Cancelar"),
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
                        text: qsTr("Agregar fuente"); icon: "+"; compact: true
                        onClicked: addSrcMenu.popup()
                        Menu {
                            id: addSrcMenu
                            ItemsDeMenu {
                                menu: addSrcMenu
                                model: Studio.sourceTypes
                                delegate: MenuItem {
                                    required property string typeId
                                    required property string displayName
                                    text: displayName
                                    onTriggered: { const n = Studio.sources.add(typeId, displayName); if (n) page.selected = n }
                                }
                            }
                            MenuSeparator { id: sepInvitados; visible: Studio.sources.guestSlots > 0 }
                            // Invitados de la sala (WebRTC): camara y pantalla de cada lugar.
                            ItemsDeMenu {
                                menu: addSrcMenu
                                despuesDe: sepInvitados
                                model: Studio.sources.guestSlots * 2
                                delegate: MenuItem {
                                    required property int index
                                    readonly property int slot: Math.floor(index / 2) + 1
                                    readonly property bool share: index % 2 === 1
                                    readonly property string who: (Studio.conference.slotNames[slot - 1] || "")
                                    text: (share ? qsTr("Invitado %1 — pantalla") : qsTr("Invitado %1")).arg(slot) + (who ? "  (" + who + ")" : "")
                                    onTriggered: { const n = Studio.sources.addGuest(slot, share); if (n) page.selected = n }
                                }
                            }
                        }
                    }
                    // Un carrusel no es una fuente mas de la lista: es una tira
                    // de imagenes que ya esta armada en Medios y que se PONE en
                    // esta escena. De aca en mas, saltar a una lamina le cambia
                    // la imagen a esta fuente, y si la escena esta al aire, sale
                    // al aire. La tarjeta de control del dashboard pasa a
                    // manejar este carrusel cuando esta escena esta al aire.
                    // Un código QR (el del Público, el de la Sala, uno guardado…).
                    GlassButton {
                        text: qsTr("Agregar código QR"); icon: "▦"; compact: true
                        onClicked: qrMenu.popup()
                        ToolTip.visible: hovered; ToolTip.delay: 400
                        ToolTip.text: qsTr("Un QR en esta escena: el de la página del Público, el de la Conferencia, o uno de «Códigos QR». Se mantiene al día solo.")
                        Menu {
                            id: qrMenu
                            property var opciones: []
                            onAboutToShow: opciones = Studio.qr.paraElegir()
                            ItemsDeMenu {
                                menu: qrMenu
                                model: qrMenu.opciones
                                delegate: MenuItem {
                                    required property var modelData
                                    text: modelData.label
                                    onTriggered: {
                                        const n = Studio.sources.addConAjustes("ba_qr", "QR · " + modelData.label.split(":")[0].replace(" (guardado)", ""),
                                                                               { qr: modelData.value, fondo: true, tamano: 720 })
                                        if (n) page.selected = n
                                    }
                                }
                            }
                        }
                    }
                    GlassButton {
                        text: qsTr("Agregar carrusel"); icon: "▦"; compact: true
                        // Siempre a la vista: sin carruseles todavía, el menú dice dónde se arman.
                        onClicked: carruselMenu.popup()
                        ToolTip.visible: hovered
                        ToolTip.delay: 400
                        ToolTip.text: qsTr("Poner en esta escena una tira de imágenes (se arman en Medios → Carruseles: con imágenes, un PDF o un PowerPoint)")
                        Menu {
                            id: carruselMenu
                            MenuItem {
                                id: sinCarruseles
                                visible: Studio.carruseles.carruseles.length === 0
                                height: visible ? implicitHeight : 0
                                text: qsTr("No hay carruseles: armar uno en Medios → Carruseles…")
                                onTriggered: ApplicationWindow.window.page = Studio.riel.paginaDeId("medios")
                            }
                            ItemsDeMenu {
                                menu: carruselMenu
                                despuesDe: sinCarruseles
                                model: Studio.carruseles.carruseles
                                delegate: MenuItem {
                                    required property var modelData
                                    text: modelData.nombre + "  (" + qsTr("%n lámina", "", modelData.laminas) + ")" +
                                          (modelData.fuente !== "" ? "  · " + qsTr("ya puesto") : "")
                                    onTriggered: {
                                        Studio.carruseles.aplicarAEscena(modelData.nombre, page.editScene)
                                        page.selected = "Carrusel · " + modelData.nombre
                                    }
                                }
                            }
                        }
                    }
                    // Una camara o capturadora NO se puede abrir dos veces (el
                    // dispositivo es exclusivo), pero SI puede estar en varias
                    // escenas: es la MISMA fuente, con otra ubicacion. Asi
                    // ademas la transicion entre dos encuadres no tiene cortes.
                    GlassButton {
                        text: qsTr("Reusar fuente"); icon: "⧉"; compact: true
                        onClicked: { reuseMenu.items = Studio.sources.otherSources(); reuseMenu.popup() }
                        Menu {
                            id: reuseMenu
                            property var items: []
                            ItemsDeMenu {
                                menu: reuseMenu
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
                title: page.selected ? qsTr("Propiedades — %1").arg(page.selected) : qsTr("Propiedades")
                ScrollView {
                    anchors.fill: parent
                    clip: true
                    contentWidth: availableWidth
                    ColumnLayout {
                        width: parent.width
                        spacing: 10
                        Text { visible: !page.selected; text: qsTr("Elegí una fuente para ver sus propiedades."); color: Theme.textFaint; font.pixelSize: 12 }
                        // Transparencia, borde difuso y sombra del elemento EN ESTA escena.
                        PanelEfectos {
                            Layout.fillWidth: true
                            visible: page.selected !== ""
                            fuente: page.selected
                        }
                        Rectangle { visible: page.selected !== ""; Layout.fillWidth: true; height: 1; color: Qt.rgba(1, 1, 1, 0.08) }
                        // Una «Grilla de personas»: quién está en ella ahora (y fijar, sacar, mover).
                        Text {
                            visible: grillaAhora.visible
                            text: qsTr("En la grilla ahora")
                            color: Theme.textDim; font { pixelSize: 12; weight: Font.DemiBold }
                        }
                        VistaGrillas {
                            id: grillaAhora
                            Layout.fillWidth: true
                            visible: page.selected !== "" && hay
                            fuente: page.selected
                        }
                        Rectangle { visible: grillaAhora.visible; Layout.fillWidth: true; height: 1; color: Qt.rgba(1, 1, 1, 0.08) }
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
        title: qsTr("Nueva escena")
        anchors.centerIn: parent
        modal: true
        standardButtons: Dialog.Ok | Dialog.Cancel
        TextField { id: newSceneName; width: 280; placeholderText: qsTr("Nombre de la escena") }
        onAccepted: { if (newSceneName.text) Studio.addScene(newSceneName.text); newSceneName.text = "" }
    }
    // Mover una escena a una carpeta (o sacarla, dejando el nombre vacio).
    Dialog {
        id: folderDlg
        property string scene: ""
        anchors.centerIn: parent
        modal: true
        title: qsTr("Carpeta de «%1»").arg(scene)
        onOpened: folderField.forceActiveFocus()
        footer: DialogButtonBox {
            GlassButton { text: qsTr("Guardar"); active: true; onClicked: { Studio.setSceneFolder(folderDlg.scene, folderField.text); folderDlg.close() } }
            GlassButton { text: qsTr("Cancelar"); onClicked: folderDlg.close() }
        }
        ColumnLayout {
            spacing: 8
            Label { text: qsTr("Nombre de la carpeta (vacío = sin carpeta)") }
            TextField {
                id: folderField
                Layout.preferredWidth: 320
                placeholderText: qsTr("p. ej. Bloque 1, Invitados, Placas")
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
        title: qsTr("Renombrar escena")
        anchors.centerIn: parent
        modal: true
        standardButtons: Dialog.Ok | Dialog.Cancel
        onOpened: renameField.text = from
        TextField { id: renameField; width: 280 }
        onAccepted: if (renameField.text && renameField.text !== from) Studio.renameScene(from, renameField.text)
    }
}
