// Los archivos del operador, como se miran de verdad.
//
// POR QUE UN ARBOL Y NO UNA GRILLA SOLA. Buscar sirve cuando uno SABE qué
// busca; la mitad de las veces se está mirando a ver qué hay, y con doscientos
// archivos en una tira plana eso no se puede hacer aunque haya buscador. El
// árbol es para recorrer; el cuadro de texto, para ir directo.
//
// Y BUSCAR MANDA SOBRE LA CARPETA. Si alguien escribe «sponsor» y no aparece
// nada, lo primero que piensa es que el archivo no está —no que se lo tapó la
// carpeta que tenía elegida—. Por eso el cartel de la grilla vacía lo dice con
// todas las letras y ofrece mirar en todas.
//
// ARRASTRAR MUEVE, y mueve LO ELEGIDO, no lo que está debajo del cursor:
// juntar cuarenta archivos y que se mueva uno es la sorpresa más cara de este
// tipo de pantalla. Por eso al empezar a arrastrar algo que no estaba elegido,
// primero se elige.
//
// BORRAR NO BORRA: va a Papelera/. Un archivo puede estar en una escena, o ser
// el único que quedaba del evento pasado. Deshacer tiene que ser sacarlo de una
// carpeta, no volver a conseguirlo.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import BA
import "."

Item {
    id: raiz

    readonly property var m: Studio.medios
    /// Lo que se está arrastrando ahora (lo pone la grilla).
    property bool arrastrando: false
    /// El último tocado, para las acciones de una sola cosa (renombrar).
    property var ultimo: null

    function tamano(bytes) {
        if (bytes < 1024) return bytes + " B"
        if (bytes < 1024 * 1024) return Math.round(bytes / 1024) + " KB"
        if (bytes < 1024 * 1024 * 1024) return (bytes / 1024 / 1024).toFixed(1) + " MB"
        return (bytes / 1024 / 1024 / 1024).toFixed(1) + " GB"
    }

    // El bulto que sigue al mouse mientras se arrastra. Uno solo para toda la
    // grilla: son cuarenta archivos moviéndose juntos, no cuarenta bultos.
    Item {
        id: fantasma
        width: 150
        height: 40
        z: 1000
        visible: raiz.arrastrando
        Drag.active: raiz.arrastrando
        Drag.keys: ["ba:medios"]
        Drag.hotSpot: Qt.point(75, 20)
        Rectangle {
            anchors.fill: parent
            radius: Theme.radiusSmall
            color: Qt.rgba(1, 0.42, 0.17, 0.92)
            Text {
                anchors.centerIn: parent
                text: raiz.m.cuantosElegidos + (raiz.m.cuantosElegidos === 1 ? " archivo" : " archivos")
                color: "white"
                font { family: Theme.font; pixelSize: 12; weight: Font.DemiBold }
            }
        }
    }

    RowLayout {
        anchors.fill: parent
        spacing: 10

        // --- El árbol de carpetas --------------------------------------------
        ColumnLayout {
            Layout.preferredWidth: 220
            Layout.fillHeight: true
            spacing: 6

            RowLayout {
                Layout.fillWidth: true
                Text {
                    Layout.fillWidth: true
                    text: "Carpetas"
                    color: Theme.textDim
                    font { family: Theme.font; pixelSize: 12; weight: Font.DemiBold }
                }
                GlassButton {
                    compact: true
                    text: "+"
                    onClicked: dlgCarpeta.open()
                    ToolTip.visible: hovered
                    ToolTip.text: "Carpeta nueva adentro de la elegida"
                }
            }

            ListView {
                id: arbol
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                model: raiz.m.arbol
                ScrollBar.vertical: ScrollBar {}

                delegate: Item {
                    id: fila
                    required property var modelData
                    width: arbol.width - 10
                    // Una carpeta cuya madre está cerrada no ocupa lugar: el
                    // modelo ya resolvió `visible`, acá sólo se obedece.
                    height: modelData.visible ? 30 : 0
                    visible: modelData.visible

                    Rectangle {
                        anchors.fill: parent
                        anchors.leftMargin: fila.modelData.nivel * 14
                        radius: 6
                        readonly property bool encima: zona.containsDrag
                        color: fila.modelData.elegida ? Qt.rgba(1, 0.42, 0.17, 0.22)
                             : encima ? Qt.rgba(0.2, 0.8, 0.4, 0.28)
                             : (fma.containsMouse ? Qt.rgba(1, 1, 1, 0.08) : "transparent")
                        border.color: encima ? Theme.ok
                                    : (fila.modelData.elegida ? Theme.accentSoft : "transparent")

                        RowLayout {
                            anchors { fill: parent; leftMargin: 6; rightMargin: 8 }
                            spacing: 4
                            // El triángulo sólo donde hay algo que plegar: uno
                            // que no hace nada invita a tocarlo dos veces.
                            Text {
                                Layout.preferredWidth: 12
                                text: fila.modelData.total > fila.modelData.propios ||
                                      fila.modelData.nivel === 0 ? (fila.modelData.abierta ? "▾" : "▸") : ""
                                color: Theme.textFaint
                                font.pixelSize: 10
                                MouseArea {
                                    anchors.fill: parent
                                    anchors.margins: -4
                                    onClicked: raiz.m.plegar(fila.modelData.relativa)
                                }
                            }
                            Text {
                                Layout.fillWidth: true
                                text: fila.modelData.nombre
                                color: fila.modelData.elegida ? Theme.text : Theme.textDim
                                font { family: Theme.font; pixelSize: 12
                                       weight: fila.modelData.nivel === 0 ? Font.DemiBold : Font.Normal }
                                elide: Text.ElideMiddle
                            }
                            Text {
                                text: fila.modelData.total > 0 ? fila.modelData.total : ""
                                color: Theme.textFaint
                                font.pixelSize: 10
                            }
                        }

                        MouseArea {
                            id: fma
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            acceptedButtons: Qt.LeftButton | Qt.RightButton
                            onClicked: (mouse) => {
                                if (mouse.button === Qt.RightButton) {
                                    if (!fila.modelData.raiz) {
                                        menuCarpeta.rel = fila.modelData.relativa
                                        menuCarpeta.nombre = fila.modelData.nombre
                                        menuCarpeta.popup()
                                    }
                                    return
                                }
                                raiz.m.carpeta = fila.modelData.relativa
                            }
                        }

                        // Soltar acá mueve lo elegido. Es el gesto central de
                        // esta pantalla: ordenar es arrastrar.
                        DropArea {
                            id: zona
                            anchors.fill: parent
                            keys: ["ba:medios"]
                            onDropped: (drop) => {
                                raiz.m.moverElegidos(fila.modelData.relativa)
                                drop.accept()
                            }
                        }
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                GlassButton {
                    compact: true
                    text: "Ver todo"
                    active: raiz.m.carpeta === ""
                    onClicked: raiz.m.carpeta = ""
                }
                Toggle {
                    checked: raiz.m.conSubcarpetas
                    onToggled: raiz.m.conSubcarpetas = checked
                }
                Text {
                    Layout.fillWidth: true
                    text: "con subcarpetas"
                    color: Theme.textFaint
                    font.pixelSize: 10
                    elide: Text.ElideRight
                }
            }
        }

        // --- Buscar, filtrar, ordenar y la grilla -----------------------------
        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 8

            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                TextField {
                    id: busca
                    Layout.fillWidth: true
                    color: Theme.text
                    placeholderText: "Buscar… (sin tildes: «logo camion» encuentra «Logo_Camión.png»)"
                    onTextChanged: raiz.m.consulta = text
                    Keys.onEscapePressed: text = ""
                }
                GlassButton {
                    compact: true
                    visible: busca.text !== ""
                    text: "✕"
                    onClicked: busca.text = ""
                }
                ComboBox {
                    id: orden
                    Layout.preferredWidth: 130
                    model: [
                        { id: "nombre", rotulo: "Nombre" },
                        { id: "fecha", rotulo: "Fecha" },
                        { id: "tamano", rotulo: "Tamaño" },
                        { id: "tipo", rotulo: "Tipo" }
                    ]
                    textRole: "rotulo"
                    onActivated: raiz.m.orden = model[currentIndex].id
                }
                GlassButton {
                    compact: true
                    text: raiz.m.ordenDescendente ? "↓" : "↑"
                    onClicked: raiz.m.ordenDescendente = !raiz.m.ordenDescendente
                    ToolTip.visible: hovered
                    ToolTip.text: raiz.m.ordenDescendente ? "De mayor a menor" : "De menor a mayor"
                }
                GlassButton {
                    compact: true
                    text: "Refrescar"
                    onClicked: raiz.m.refrescar()
                    ToolTip.visible: hovered
                    ToolTip.delay: 400
                    ToolTip.text: "Volver a mirar el disco: después de copiar archivos a mano"
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 6
                Repeater {
                    model: [
                        { id: "", rotulo: "Todo" },
                        { id: "imagen", rotulo: "Imágenes" },
                        { id: "video", rotulo: "Videos" },
                        { id: "audio", rotulo: "Audio" },
                        { id: "documento", rotulo: "Presentaciones" }
                    ]
                    delegate: GlassButton {
                        required property var modelData
                        compact: true
                        active: raiz.m.tipo === modelData.id
                        // El número al lado: es lo que deja decir «no hay
                        // ningún video» en vez de mostrar una grilla vacía sin
                        // explicación.
                        text: modelData.rotulo +
                              (modelData.id === "" ? ""
                               : " (" + (raiz.m.cuenta[modelData.id] || 0) + ")")
                        onClicked: raiz.m.tipo = modelData.id
                    }
                }
                Item { Layout.fillWidth: true }
                GlassButton {
                    compact: true
                    text: "Elegir todo"
                    onClicked: raiz.m.elegirTodo()
                }
                Text {
                    text: raiz.m.mostrados === raiz.m.total
                          ? raiz.m.total + " archivos"
                          : raiz.m.mostrados + " de " + raiz.m.total
                    color: Theme.textDim
                    font.pixelSize: 12
                }
            }

            Item {
                Layout.fillWidth: true
                Layout.fillHeight: true

                GridView {
                    id: grilla
                    anchors.fill: parent
                    clip: true
                    cellWidth: 232
                    cellHeight: 176
                    model: raiz.m.archivos
                    ScrollBar.vertical: ScrollBar {}
                    focus: true

                    // Los atajos de cualquier gestor de archivos. Sin esto,
                    // elegir cuarenta y borrarlos es cuarenta clics y un menú.
                    Keys.onPressed: (e) => {
                        if (e.key === Qt.Key_A && (e.modifiers & Qt.ControlModifier)) {
                            raiz.m.elegirTodo(); e.accepted = true
                        } else if (e.key === Qt.Key_Escape) {
                            raiz.m.noElegirNada(); e.accepted = true
                        } else if (e.key === Qt.Key_Delete && raiz.m.cuantosElegidos > 0) {
                            dlgBorrar.open(); e.accepted = true
                        }
                    }

                    delegate: Item {
                        id: celda
                        required property var modelData
                        required property int index
                        readonly property int indiceEnGrilla: index
                        readonly property bool elegido: raiz.m.seleccion.indexOf(modelData.relativa) >= 0
                        width: grilla.cellWidth - 10
                        height: grilla.cellHeight - 10

                        Rectangle {
                            anchors.fill: parent
                            radius: Theme.radiusSmall
                            color: celda.elegido ? Qt.rgba(1, 0.42, 0.17, 0.20)
                                 : (ma.containsMouse ? Qt.rgba(1, 1, 1, 0.09) : Qt.rgba(1, 1, 1, 0.04))
                            border.width: celda.elegido ? 2 : 1
                            border.color: celda.elegido ? Theme.accent : Theme.glassBorder
                            Behavior on color { ColorAnimation { duration: 120 } }

                            ColumnLayout {
                                anchors { fill: parent; margins: 8 }
                                spacing: 6

                                Rectangle {
                                    Layout.fillWidth: true
                                    Layout.fillHeight: true
                                    radius: 6
                                    color: "#0b0e12"
                                    clip: true

                                    // Sólo las imágenes tienen vista previa de
                                    // verdad. Un video pide decodificar y un
                                    // PDF, renderizar: con doscientos archivos
                                    // eso congela la grilla.
                                    Image {
                                        anchors.fill: parent
                                        visible: celda.modelData.tipo === "imagen"
                                        source: celda.modelData.tipo === "imagen" ? celda.modelData.url : ""
                                        fillMode: Image.PreserveAspectFit
                                        asynchronous: true
                                        cache: true
                                        sourceSize.width: 260
                                        sourceSize.height: 180
                                    }
                                    Text {
                                        anchors.centerIn: parent
                                        visible: celda.modelData.tipo !== "imagen"
                                        text: celda.modelData.tipo === "video" ? "▶"
                                            : celda.modelData.tipo === "audio" ? "♪" : "▤"
                                        color: Theme.textFaint
                                        font.pixelSize: 40
                                    }
                                    Text {
                                        anchors { bottom: parent.bottom; right: parent.right; margins: 6 }
                                        visible: celda.modelData.tipo !== "imagen"
                                        text: celda.modelData.nombre.split(".").pop().toUpperCase()
                                        color: Theme.textDim
                                        font { pixelSize: 11; weight: Font.DemiBold }
                                    }
                                }

                                Text {
                                    Layout.fillWidth: true
                                    text: celda.modelData.nombre
                                    color: Theme.text
                                    font.pixelSize: 12
                                    elide: Text.ElideMiddle
                                }
                                Text {
                                    Layout.fillWidth: true
                                    text: celda.modelData.carpeta + " · " + raiz.tamano(celda.modelData.bytes)
                                    color: Theme.textFaint
                                    font.pixelSize: 10
                                    elide: Text.ElideRight
                                }
                            }

                            MouseArea {
                                id: ma
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                acceptedButtons: Qt.LeftButton | Qt.RightButton
                                drag.target: fantasma

                                onPressed: (mouse) => {
                                    grilla.forceActiveFocus()
                                    raiz.ultimo = celda.modelData
                                    if (mouse.button === Qt.RightButton) return
                                    const ctrl = (mouse.modifiers & Qt.ControlModifier) !== 0
                                    const shift = (mouse.modifiers & Qt.ShiftModifier) !== 0
                                    // Si ya estaba elegido y no hay
                                    // modificador, NO se reemplaza la
                                    // selección: es lo que permite agarrar
                                    // cuarenta y arrastrarlos juntos.
                                    if (!ctrl && !shift && celda.elegido) return
                                    raiz.m.elegir(celda.modelData.relativa,
                                                  ctrl ? "sumar" : shift ? "hasta" : "solo")
                                }
                                onClicked: (mouse) => {
                                    if (mouse.button === Qt.RightButton) menuArchivo.popup()
                                }
                                // Doble clic lo ABRE. Es lo que uno hace sin
                                // pensar en cualquier carpeta del mundo, y
                                // antes de poner algo al aire hay que saber
                                // que es: la miniatura no dice si el video es
                                // el corte bueno o el de la prueba.
                                onDoubleClicked: (mouse) => {
                                    if (mouse.button !== Qt.LeftButton) return
                                    visor.abrir(celda.indiceEnGrilla)
                                }

                                drag.onActiveChanged: {
                                    if (drag.active) {
                                        const p = mapToItem(raiz, ma.mouseX, ma.mouseY)
                                        fantasma.x = p.x - 75
                                        fantasma.y = p.y - 20
                                        raiz.arrastrando = true
                                    } else {
                                        fantasma.Drag.drop()
                                        raiz.arrastrando = false
                                    }
                                }
                            }
                        }
                    }
                }

                Text {
                    anchors.centerIn: parent
                    visible: grilla.count === 0
                    width: parent.width - 120
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.WordWrap
                    color: Theme.textFaint
                    font.pixelSize: 13
                    text: raiz.m.total === 0
                          ? "No hay archivos de medios todavía.\n\nCopiá tus imágenes, videos y música a:\n" +
                            raiz.m.carpetaDeMedios()
                          : (busca.text !== "" && raiz.m.carpeta !== "")
                            ? "Nada coincide en «" + raiz.m.carpeta + "».\nProbá mirando en todas las carpetas."
                            : "Nada coincide con lo que buscaste."
                }

                // Volver a «todas» sin tener que encontrar el botón: el cartel
                // de arriba lo sugiere y esto lo hace.
                GlassButton {
                    anchors { horizontalCenter: parent.horizontalCenter; top: parent.verticalCenter; topMargin: 40 }
                    visible: grilla.count === 0 && busca.text !== "" && raiz.m.carpeta !== ""
                    text: "Buscar en todas las carpetas"
                    onClicked: raiz.m.carpeta = ""
                }
            }
        }
    }

    // El visor: uno solo para toda la grilla, y se le dice por que indice
    // empezar. Ver VisorMedio.qml.
    VisorMedio { id: visor }

    // --- Qué hacer con lo elegido --------------------------------------------
    Menu {
        id: menuArchivo
        MenuItem {
            text: "Abrir"
            enabled: raiz.m.cuantosElegidos === 1
            onTriggered: visor.abrir(raiz.m.archivos.findIndex(
                (a) => a.relativa === raiz.m.seleccion[0]))
        }
        MenuItem {
            text: "Renombrar…"
            enabled: raiz.m.cuantosElegidos === 1
            onTriggered: { dlgRenombrar.rel = raiz.m.seleccion[0]; dlgRenombrar.open() }
        }
        MenuItem { text: "Elegir todo"; onTriggered: raiz.m.elegirTodo() }
        MenuItem { text: "No elegir nada"; onTriggered: raiz.m.noElegirNada() }
        MenuSeparator {}
        MenuItem { text: "Borrar (a la papelera)"; onTriggered: dlgBorrar.open() }
    }

    Menu {
        id: menuCarpeta
        property string rel: ""
        property string nombre: ""
        MenuItem {
            text: "Renombrar carpeta…"
            onTriggered: { dlgRenombrar.rel = menuCarpeta.rel; dlgRenombrar.open() }
        }
        MenuItem {
            text: "Borrar carpeta (a la papelera)"
            onTriggered: { raiz.m.elegir(menuCarpeta.rel, "solo"); dlgBorrar.open() }
        }
    }

    Dialog {
        id: dlgCarpeta
        title: "Carpeta nueva"
        modal: true
        width: 380
        anchors.centerIn: Overlay.overlay
        standardButtons: Dialog.Ok | Dialog.Cancel
        onOpened: nombreCarpeta.forceActiveFocus()
        onAccepted: {
            raiz.m.nuevaCarpeta(raiz.m.carpeta === "" ? "Media" : raiz.m.carpeta, nombreCarpeta.text)
            nombreCarpeta.text = ""
        }
        ColumnLayout {
            spacing: 6
            Label {
                width: dlgCarpeta.availableWidth
                text: "Adentro de «" + (raiz.m.carpeta === "" ? "Media" : raiz.m.carpeta) + "»"
                color: Theme.textFaint
            }
            TextField {
                id: nombreCarpeta
                Layout.preferredWidth: dlgCarpeta.availableWidth
                color: Theme.text
                placeholderText: "Sponsors, Camara 2, Apertura…"
                onAccepted: dlgCarpeta.accept()
            }
        }
    }

    Dialog {
        id: dlgRenombrar
        property string rel: ""
        title: "Renombrar"
        modal: true
        width: 420
        anchors.centerIn: Overlay.overlay
        standardButtons: Dialog.Ok | Dialog.Cancel
        onOpened: { nombreNuevo.text = dlgRenombrar.rel.split("/").pop(); nombreNuevo.forceActiveFocus() }
        onAccepted: raiz.m.renombrar(dlgRenombrar.rel, nombreNuevo.text)
        TextField {
            id: nombreNuevo
            width: dlgRenombrar.availableWidth
            color: Theme.text
            onAccepted: dlgRenombrar.accept()
        }
    }

    Dialog {
        id: dlgBorrar
        title: "Borrar"
        modal: true
        width: 440
        anchors.centerIn: Overlay.overlay
        standardButtons: Dialog.Yes | Dialog.No
        onAccepted: raiz.m.borrarElegidos()
        Label {
            width: dlgBorrar.availableWidth
            wrapMode: Text.WordWrap
            // Se dice a dónde van: si no, nadie toca el botón por las dudas.
            text: raiz.m.cuantosElegidos + (raiz.m.cuantosElegidos === 1 ? " archivo" : " archivos") +
                  " (" + raiz.tamano(raiz.m.bytesElegidos) + ") van a Papelera/.\n\n" +
                  "No se borran del disco: si alguno estaba en una escena, se lo saca de ahí y listo."
        }
    }
}
