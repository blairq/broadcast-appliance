// Página Teleprompter: los discursos, el control y quién está conectado.
//
// El teleprompter existía completo —el estado en el gateway, la tablet y el
// control en la web— pero no había dónde escribir los textos ni forma de
// saber si la tablet del atril estaba prendida. Esto es eso.
//
// Tres columnas, en el orden en que se usan: los DOCUMENTOS (carpetas y
// archivos, que viven en la carpeta del evento), el EDITOR (escribir y
// mandar), y a la derecha el CONTROL con los aparatos conectados y los QR.
//
// Todo el estado es del gateway: acá no se guarda nada. Mientras la página
// está a la vista se le pregunta dos veces por segundo, y al salir se deja de
// preguntar (`mirar`), porque preguntar cuando nadie mira es gastar.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import BA
import "../controls"

Rectangle {
    id: page
    color: Theme.bg
    property real leftInset: 92
    readonly property var p: Studio.prompter
    readonly property var est: p.estado

    // Solo se consulta mientras esta página se ve.
    onVisibleChanged: p.mirar(visible)
    Component.onCompleted: { if (visible) p.mirar(true); p.refrescar() }
    Component.onDestruction: p.mirar(false)

    function mmss(s) {
        const t = Math.max(0, Math.round(s))
        return Math.floor(t / 60) + ":" + String(t % 60).padStart(2, "0")
    }
    function avisar(txt, tipo) { Studio.notify(txt, tipo || "error") }
    /// Los errores de los ViewModel vuelven como string: vacío es que salió bien.
    function chequear(err, bien) {
        if (err && err !== "") avisar(err)
        else if (bien) avisar(bien, "ok")
        return !err || err === ""
    }

    ColumnLayout {
        anchors { fill: parent; leftMargin: page.leftInset; margins: 20 }
        spacing: 12

        // --- Encabezado ------------------------------------------------------
        RowLayout {
            Layout.fillWidth: true
            spacing: 10
            Text { text: "Teleprompter"; color: Theme.text; font { pixelSize: 20; weight: Font.DemiBold } }
            StatusPill {
                text: page.p.enLinea ? (page.est.corriendo ? "corriendo" : "listo") : "sin sala"
                dot: page.p.enLinea ? (page.est.corriendo ? Theme.live : Theme.ok) : Theme.textFaint
                pulse: page.est.corriendo === true
                medidaDe: "corriendo"
            }
            StatusPill {
                text: page.p.oradores + " en el atril"
                dot: page.p.oradores > 0 ? Theme.ok : Theme.textFaint
                medidaDe: "0 en el atril"
            }
            StatusPill {
                text: page.p.controles + " con el control"
                dot: page.p.controles > 0 ? Theme.ok : Theme.textFaint
                medidaDe: "0 con el control"
            }
            // «Voz del teleprompter»: el guion en voz alta, al ritmo del teleprompter.
            // Es una fuente de audio (Audio, Matriz): de fábrica, solo en los auriculares.
            GlassButton {
                visible: !!Studio.vozPrompter
                compact: true
                icon: "🗣"
                text: Studio.vozPrompter && Studio.vozPrompter.activa ? "Leyendo en voz alta" : "Leer en voz alta"
                active: !!Studio.vozPrompter && Studio.vozPrompter.activa
                onClicked: Studio.vozPrompter.activa = !Studio.vozPrompter.activa
                ToolTip.visible: hovered; ToolTip.delay: 400
                ToolTip.text: (Studio.vozPrompter ? Studio.vozPrompter.estado + ". " : "") +
                              "Dice el texto siguiendo los controles del teleprompter (velocidad, pausa, saltos). " +
                              "Suena por la fuente «Voz del teleprompter»: en Audio se le ponen filtros y en la " +
                              "Matriz se rutea; de fábrica, solo en tus auriculares."
            }
            Item { Layout.fillWidth: true }
            Text {
                visible: page.p.sinEvento !== ""
                text: "⚠ " + page.p.sinEvento
                color: Theme.warn; font.pixelSize: 11
                wrapMode: Text.WordWrap
                Layout.maximumWidth: 520
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 12

            // --- Documentos --------------------------------------------------
            Panel {
                Layout.fillWidth: false
                Layout.preferredWidth: 300
                Layout.fillHeight: true
                ColumnLayout {
                    anchors { fill: parent; margins: 12 }
                    spacing: 8

                    RowLayout {
                        Layout.fillWidth: true
                        Text { text: "Discursos"; color: Theme.text; font { pixelSize: 14; weight: Font.DemiBold } }
                        Item { Layout.fillWidth: true }
                        GlassButton {
                            compact: true; icon: "🗀"; text: "Carpeta"
                            enabled: page.p.sinEvento === ""
                            onClicked: nuevaCarpeta.open()
                            ToolTip.visible: hovered; ToolTip.text: "Crear una carpeta acá"
                        }
                        GlassButton {
                            compact: true; icon: "+"; text: "Nuevo"
                            enabled: page.p.sinEvento === ""
                            active: true
                            onClicked: nuevoDoc.open()
                        }
                    }

                    // La carpeta que se mira. Un desplegable y no un árbol: se
                    // navega con un clic y no hay que ir plegando ramas.
                    ComboBox {
                        Layout.fillWidth: true
                        model: page.p.carpetas
                        visible: page.p.carpetas.length > 1
                        displayText: currentIndex <= 0 ? "Discursos/ (todo lo de la raíz)" : currentText
                        currentIndex: page.p.carpetas.indexOf(page.p.carpeta)
                        onActivated: (i) => page.p.carpeta = page.p.carpetas[i]
                    }

                    ListView {
                        id: docs
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        spacing: 2
                        model: page.p.documentos
                        ScrollBar.vertical: ScrollBar {}
                        delegate: Rectangle {
                            id: doc
                            required property var modelData
                            width: docs.width
                            implicitHeight: 46
                            radius: 8
                            color: page.p.abierto === doc.modelData.rel ? Qt.rgba(1, 1, 1, 0.07)
                                 : (raton.containsMouse ? Qt.rgba(1, 1, 1, 0.035) : "transparent")
                            MouseArea {
                                id: raton
                                anchors.fill: parent
                                hoverEnabled: true
                                acceptedButtons: Qt.LeftButton | Qt.RightButton
                                onClicked: (m) => {
                                    if (m.button === Qt.RightButton) menu.popup()
                                    else if (page.p.sucio) confirmarCambio.abrirOtro(doc.modelData.rel)
                                    else page.p.abrir(doc.modelData.rel)
                                }
                            }
                            RowLayout {
                                anchors { fill: parent; leftMargin: 10; rightMargin: 8 }
                                spacing: 8
                                // Un punto naranja: este es el que está en el aire.
                                Rectangle {
                                    Layout.preferredWidth: 7; Layout.preferredHeight: 7; radius: 4
                                    color: Theme.accent
                                    visible: doc.modelData.cargado
                                }
                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 0
                                    Text {
                                        Layout.fillWidth: true
                                        text: doc.modelData.nombre
                                        color: Theme.text; font.pixelSize: 12
                                        elide: Text.ElideRight
                                    }
                                    Text {
                                        text: doc.modelData.palabras + " palabras · " + page.mmss(doc.modelData.duracionSeg) +
                                              " · " + doc.modelData.modificado
                                        color: Theme.textFaint; font.pixelSize: 10
                                    }
                                }
                                GlassButton {
                                    compact: true; text: "Cargar"
                                    visible: raton.containsMouse || doc.modelData.cargado
                                    onClicked: page.chequear(page.p.cargar(doc.modelData.rel),
                                                             "«" + doc.modelData.nombre + "» está en el teleprompter")
                                    ToolTip.visible: hovered
                                    ToolTip.text: "Lo manda al teleprompter. Queda listo, pero parado."
                                }
                            }
                            Menu {
                                id: menu
                                MenuItem {
                                    text: "Renombrar…"
                                    onTriggered: { renombrar.rel = doc.modelData.rel; renombrar.open() }
                                }
                                Menu {
                                    id: menuMover
                                    title: "Mover a…"
                                    enabled: page.p.carpetas.length > 1
                                    ItemsDeMenu {
                                        menu: menuMover
                                        model: page.p.carpetas
                                        delegate: MenuItem {
                                            required property string modelData
                                            text: modelData === "" ? "Discursos/ (raíz)" : modelData
                                            onTriggered: page.chequear(page.p.mover(doc.modelData.rel, modelData))
                                        }
                                    }
                                }
                                MenuItem {
                                    text: "Borrar"
                                    onTriggered: { borrar.rel = doc.modelData.rel; borrar.open() }
                                }
                            }
                        }
                        Text {
                            anchors.centerIn: parent
                            visible: docs.count === 0
                            width: parent.width - 30
                            horizontalAlignment: Text.AlignHCenter
                            wrapMode: Text.WordWrap
                            color: Theme.textFaint; font.pixelSize: 11
                            text: page.p.sinEvento !== "" ? "Sin evento abierto."
                                : "No hay discursos en esta carpeta. «Nuevo» crea uno."
                        }
                    }
                }
            }

            // --- Editor ------------------------------------------------------
            Panel {
                Layout.fillWidth: true
                Layout.fillHeight: true
                ColumnLayout {
                    anchors { fill: parent; margins: 12 }
                    spacing: 8

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8
                        Text {
                            Layout.fillWidth: true
                            text: page.p.abierto === "" ? "Ningún documento abierto"
                                : page.p.abierto + (page.p.sucio ? " •" : "")
                            color: page.p.sucio ? Theme.warn : Theme.text
                            font { pixelSize: 13; weight: Font.DemiBold }
                            elide: Text.ElideMiddle
                        }
                        Text {
                            visible: page.p.abierto !== ""
                            text: page.p.palabras + " palabras · " + page.mmss(page.p.duracionSeg) + " leyendo"
                            color: Theme.textDim; font.pixelSize: 11
                        }
                        GlassButton {
                            compact: true; text: "Guardar"; icon: "↓"
                            enabled: page.p.sucio
                            onClicked: page.chequear(page.p.guardar(), "Guardado")
                        }
                        GlassButton {
                            compact: true; text: "Mandar al prompter"; icon: "▤"
                            active: true
                            enabled: page.p.abierto !== ""
                            onClicked: page.chequear(page.p.cargarDelEditor(), "Mandado, y parado")
                            ToolTip.visible: hovered
                            ToolTip.text: "Manda lo que ves acá, sin guardarlo primero. Queda listo, pero parado."
                        }
                    }

                    ScrollView {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        TextArea {
                            id: editor
                            // Se escribe desde el ViewModel solo cuando cambia
                            // de documento: si no, cada tecla volvería a
                            // llegar y el cursor saltaría al principio.
                            text: page.p.texto
                            enabled: page.p.abierto !== ""
                            wrapMode: TextArea.Wrap
                            selectByMouse: true
                            color: Theme.text
                            font { family: Theme.font; pixelSize: 15 }
                            placeholderText: page.p.abierto === ""
                                ? "Abrí un discurso de la izquierda, o creá uno nuevo."
                                : "Escribí el discurso. Un renglón vacío separa párrafos, y el control " +
                                  "puede saltar de uno a otro."
                            onTextChanged: if (text !== page.p.texto) page.p.editar(text)
                            background: Rectangle { color: "transparent" }
                        }
                    }
                }
            }

            // --- Control + conectados ----------------------------------------
            // `fillWidth: false` NO es de adorno: un layout anidado adentro de
            // otro tiene fillWidth en TRUE por defecto (al revés que un Item
            // común), así que sin esto esta columna se come todo el ancho y al
            // editor no le queda nada.
            ColumnLayout {
                Layout.fillWidth: false
                Layout.preferredWidth: 300
                Layout.fillHeight: true
                spacing: 12

                Panel {
                    Layout.fillWidth: true
                    padding: 12
                    implicitHeight: control.implicitHeight + 2 * padding
                    ColumnLayout {
                        id: control
                        anchors.fill: parent // el margen lo pone Panel (`padding`)
                        spacing: 10

                        Text {
                            Layout.fillWidth: true
                            text: page.est.titulo ? page.est.titulo : "Sin discurso cargado"
                            color: page.est.titulo ? Theme.text : Theme.textFaint
                            font { pixelSize: 13; weight: Font.DemiBold }
                            elide: Text.ElideMiddle
                        }

                        // Por dónde va. Arrastrable: es como se retoma cuando
                        // el orador se salteó un pedazo.
                        Slider {
                            Layout.fillWidth: true
                            from: 0; to: 1
                            value: page.p.avance
                            enabled: page.p.enLinea && page.est.palabras > 0
                            onMoved: page.p.irA(value)
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            Text {
                                text: Math.round(page.p.avance * 100) + "%"
                                color: Theme.textDim; font.pixelSize: 11
                            }
                            Item { Layout.fillWidth: true }
                            Text {
                                text: page.est.palabras > 0 ? "faltan " + page.mmss(page.p.faltaSeg) : ""
                                color: Theme.textDim; font.pixelSize: 11
                            }
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 8
                            GlassButton {
                                Layout.fillWidth: true
                                text: page.est.corriendo ? "Parar" : "Arrancar"
                                icon: page.est.corriendo ? "⏸" : "▶"
                                active: !page.est.corriendo
                                enabled: page.p.enLinea && page.est.palabras > 0
                                onClicked: page.p.arrancar(!page.est.corriendo)
                            }
                            GlassButton {
                                text: "PARÁ AHÍ"
                                enabled: page.p.enLinea
                                onClicked: page.p.panico()
                                ToolTip.visible: hovered
                                ToolTip.text: "Para en seco sin mover la posición: se retoma justo ahí"
                            }
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 6
                            Text { text: "Velocidad"; color: Theme.textDim; font.pixelSize: 11 }
                            Item { Layout.fillWidth: true }
                            GlassButton {
                                compact: true; text: "−"
                                enabled: page.p.enLinea
                                onClicked: page.p.setPpm((page.est.ppm || 140) - 10)
                            }
                            Text {
                                text: (page.est.ppm || 140) + " ppm"
                                color: Theme.text; font { pixelSize: 12; family: "JetBrains Mono" }
                            }
                            GlassButton {
                                compact: true; text: "+"
                                enabled: page.p.enLinea
                                onClicked: page.p.setPpm((page.est.ppm || 140) + 10)
                            }
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 6
                            GlassButton {
                                compact: true; text: "⏮ Principio"
                                enabled: page.p.enLinea
                                onClicked: page.p.irA(0)
                            }
                            GlassButton {
                                compact: true; text: "⇄ Espejo"
                                active: page.est.espejo === true
                                enabled: page.p.enLinea
                                onClicked: page.p.setEspejo(!page.est.espejo)
                                ToolTip.visible: hovered
                                ToolTip.text: "Para el vidrio del teleprompter"
                            }
                        }

                        Text { text: "Avisarle al orador"; color: Theme.textDim; font.pixelSize: 11 }
                        Flow {
                            Layout.fillWidth: true
                            spacing: 5
                            Repeater {
                                model: ["Redondeá", "Más despacio", "Se escucha bajo", "Mirá a cámara"]
                                delegate: GlassButton {
                                    required property string modelData
                                    compact: true
                                    text: modelData
                                    active: page.est.aviso === modelData
                                    enabled: page.p.enLinea
                                    onClicked: page.p.avisar(page.est.aviso === modelData ? "" : modelData)
                                }
                            }
                        }
                    }
                }

                // --- Conectados ----------------------------------------------
                Panel {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    ColumnLayout {
                        anchors { fill: parent; margins: 12 }
                        spacing: 8
                        RowLayout {
                            Layout.fillWidth: true
                            Text { text: "Conectados"; color: Theme.text; font { pixelSize: 13; weight: Font.DemiBold } }
                            Item { Layout.fillWidth: true }
                            GlassButton {
                                compact: true; icon: "▦"; text: "Atril"
                                onClicked: { qr.vista = "orador"; qr.ofreceClave = false; qr.mostrar("local", false) }
                                ToolTip.visible: hovered
                                ToolTip.text: "QR de la pantalla del orador (solo lee: no lleva clave)"
                            }
                            GlassButton {
                                compact: true; icon: "▦"; text: "Control"
                                active: true
                                onClicked: { qr.vista = "control"; qr.ofreceClave = true; qr.mostrar("local", true) }
                                ToolTip.visible: hovered
                                ToolTip.text: "QR del control, con su clave"
                            }
                        }

                        ListView {
                            id: conec
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            clip: true
                            spacing: 3
                            model: page.p.conectados
                            delegate: RowLayout {
                                required property var modelData
                                width: conec.width
                                spacing: 8
                                Rectangle {
                                    Layout.preferredWidth: 7; Layout.preferredHeight: 7; radius: 4
                                    color: modelData.vista === "orador" ? Theme.ok : Theme.accent
                                }
                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 0
                                    Text {
                                        text: modelData.nombre !== "" ? modelData.nombre
                                            : (modelData.vista === "orador" ? "Pantalla del orador" : "Control")
                                        color: Theme.text; font.pixelSize: 12
                                    }
                                    Text {
                                        text: modelData.ip + " · " + (modelData.vista === "orador" ? "atril" : "control")
                                        color: Theme.textFaint; font.pixelSize: 10
                                    }
                                }
                            }
                            Text {
                                anchors.centerIn: parent
                                visible: conec.count === 0
                                width: parent.width - 20
                                horizontalAlignment: Text.AlignHCenter
                                wrapMode: Text.WordWrap
                                color: Theme.textFaint; font.pixelSize: 11
                                text: page.p.enLinea
                                    ? "Nadie conectado. Los QR de arriba abren la pantalla del atril y el control."
                                    : "La sala no está corriendo: sin ella no hay teleprompter."
                            }
                        }

                        Text {
                            Layout.fillWidth: true
                            visible: page.p.clave !== ""
                            text: "Clave del control: " + page.p.clave
                            color: Theme.textFaint
                            font { pixelSize: 10; family: "JetBrains Mono" }
                            wrapMode: Text.WrapAnywhere
                        }
                    }
                }
            }
        }
    }

    // --- Diálogos -------------------------------------------------------------
    QrDialog {
        id: qr
        fuente: page.p
        title: vista === "orador" ? "Pantalla del orador" : "Control del teleprompter"
        avisoSinClave: "Sin la clave adentro, el QR abre el control y la pide. Decísela vos:"
    }

    Dialog {
        id: nuevoDoc
        anchors.centerIn: parent
        modal: true
        title: "Discurso nuevo"
        standardButtons: Dialog.Ok | Dialog.Cancel
        onOpened: { nombreDoc.text = ""; nombreDoc.forceActiveFocus() }
        onAccepted: page.chequear(page.p.crear(nombreDoc.text))
        ColumnLayout {
            TextField {
                id: nombreDoc
                Layout.preferredWidth: 320
                placeholderText: "Palabras de apertura"
                color: Theme.text
                onAccepted: nuevoDoc.accept()
            }
            Text {
                text: "Se crea en " + (page.p.carpeta === "" ? "Discursos/" : "Discursos/" + page.p.carpeta)
                color: Theme.textFaint; font.pixelSize: 11
            }
        }
    }

    Dialog {
        id: nuevaCarpeta
        anchors.centerIn: parent
        modal: true
        title: "Carpeta nueva"
        standardButtons: Dialog.Ok | Dialog.Cancel
        onOpened: { nombreCarpeta.text = ""; nombreCarpeta.forceActiveFocus() }
        onAccepted: page.chequear(page.p.crearCarpeta(nombreCarpeta.text))
        TextField {
            id: nombreCarpeta
            width: 320
            placeholderText: "Apertura"
            color: Theme.text
            onAccepted: nuevaCarpeta.accept()
        }
    }

    Dialog {
        id: renombrar
        anchors.centerIn: parent
        modal: true
        title: "Renombrar"
        standardButtons: Dialog.Ok | Dialog.Cancel
        property string rel: ""
        onOpened: { nombreNuevo.text = rel.split("/").pop(); nombreNuevo.forceActiveFocus() }
        onAccepted: page.chequear(page.p.renombrar(rel, nombreNuevo.text))
        TextField {
            id: nombreNuevo
            width: 320
            color: Theme.text
            onAccepted: renombrar.accept()
        }
    }

    Dialog {
        id: borrar
        anchors.centerIn: parent
        modal: true
        title: "¿Borrar el discurso?"
        standardButtons: Dialog.Yes | Dialog.No
        property string rel: ""
        onAccepted: page.chequear(page.p.borrar(rel))
        Text {
            width: 320
            wrapMode: Text.WordWrap
            color: Theme.text
            text: "Se borra «" + borrar.rel + "» de la carpeta del evento. Si algún cue del guion lo " +
                  "usaba, va a quedar señalado en la revisión previa."
        }
    }

    // Cambiar de documento con cambios sin guardar: se pregunta, no se pierde
    // en silencio.
    Dialog {
        id: confirmarCambio
        anchors.centerIn: parent
        modal: true
        title: "Hay cambios sin guardar"
        standardButtons: Dialog.Save | Dialog.Discard | Dialog.Cancel
        property string proximo: ""
        function abrirOtro(rel) { proximo = rel; open() }
        onAccepted: { if (page.chequear(page.p.guardar())) page.p.abrir(proximo) }
        onDiscarded: { page.p.abrir(proximo); close() }
        Text {
            width: 340
            wrapMode: Text.WordWrap
            color: Theme.text
            text: "«" + page.p.abierto + "» tiene cambios que no están en el disco."
        }
    }
}
