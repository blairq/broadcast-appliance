// Página Pantallas: los televisores de la sala y qué mostrar en cada uno.
//
// Dos listas y nada más, porque son dos preguntas distintas: DÓNDE (los
// aparatos que hay) y QUÉ (lo que se puede proyectar). Se elige una pantalla,
// se elige un contenido, se llenan sus campos y se manda.
//
// Lo que se proyecta son páginas que ya sirve el gateway, así que agregar algo
// nuevo para mostrar no toca esta pantalla: aparece solo en la lista de
// contenidos (ver PantallasViewModel).
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import BA
import "../controls"

Rectangle {
    id: page
    color: Theme.bg
    property real leftInset: 92
    readonly property var p: Studio.pantallas
    property string elegida: ""
    property string tipo: "mensaje"
    property var datos: ({})
    property bool modoCarrusel: false
    property string carrusel: ""

    function carruselActual() {
        for (let i = 0; i < p.carruseles.length; ++i)
            if (p.carruseles[i].id === page.carrusel) return p.carruseles[i]
        return null
    }

    function tituloDe(tipo) {
        for (let i = 0; i < p.contenidos.length; ++i)
            if (p.contenidos[i].id === tipo) return p.contenidos[i].titulo
        return tipo
    }
    function contenidoActual() {
        for (let i = 0; i < p.contenidos.length; ++i)
            if (p.contenidos[i].id === page.tipo) return p.contenidos[i]
        return null
    }
    function colorDeEstado(e) {
        switch (e) {
        case "mostrando": return Theme.live
        case "libre": return Theme.ok
        case "ocupada": return Theme.warn
        case "error": case "apagada": return Theme.textFaint
        default: return Theme.textFaint
        }
    }
    function textoDeEstado(d) {
        if (d.trabajando) return "…"
        switch (d.estado) {
        case "mostrando": return "mostrando"
        case "libre": return "libre"
        case "ocupada": return "ocupada: " + d.que
        case "apagada": return "no contesta"
        case "error": return "error"
        default: return "sin consultar"
        }
    }
    function avisar(t, tipo) { Studio.notify(t, tipo || "error") }
    function chequear(err) { if (err && err !== "") page.avisar(err); return !err || err === "" }

    ColumnLayout {
        anchors { fill: parent; leftMargin: page.leftInset; margins: 20 }
        spacing: 12

        RowLayout {
            Layout.fillWidth: true
            spacing: 10
            Text { text: "Pantallas"; color: Theme.text; font { pixelSize: 20; weight: Font.DemiBold } }
            StatusPill {
                text: page.p.mostrando > 0 ? page.p.mostrando + " mostrando" : page.p.pantallas.length + " conocidas"
                dot: page.p.mostrando > 0 ? Theme.live : Theme.textFaint
                pulse: page.p.mostrando > 0
            }
            Item { Layout.fillWidth: true }
            GlassButton {
                text: page.p.buscando ? "Buscando…" : "Buscar en la red"
                icon: "⟳"
                enabled: !page.p.buscando
                onClicked: page.p.buscar()
            }
            GlassButton { text: "Agregar a mano"; icon: "+"; onClicked: aMano.open() }
        }

        Text {
            Layout.fillWidth: true
            visible: page.p.aviso !== ""
            text: "⚠ " + page.p.aviso
            color: Theme.warn; font.pixelSize: 11; wrapMode: Text.WordWrap
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 12

            // --- Dónde -------------------------------------------------------
            Panel {
                Layout.fillWidth: true
                Layout.fillHeight: true
                ColumnLayout {
                    anchors { fill: parent; margins: 12 }
                    spacing: 8
                    Text { text: "Dónde"; color: Theme.text; font { pixelSize: 14; weight: Font.DemiBold } }

                    ListView {
                        id: lista
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        spacing: 4
                        model: page.p.pantallas
                        ScrollBar.vertical: ScrollBar {}
                        delegate: Rectangle {
                            id: fila
                            required property var modelData
                            width: lista.width
                            implicitHeight: 62
                            radius: 10
                            color: page.elegida === fila.modelData.id ? Qt.rgba(1, 1, 1, 0.07)
                                 : (raton.containsMouse ? Qt.rgba(1, 1, 1, 0.035) : "transparent")
                            MouseArea {
                                id: raton
                                anchors.fill: parent
                                hoverEnabled: true
                                onClicked: page.elegida = fila.modelData.id
                            }
                            RowLayout {
                                anchors { fill: parent; leftMargin: 12; rightMargin: 10 }
                                spacing: 10
                                Rectangle {
                                    Layout.preferredWidth: 8; Layout.preferredHeight: 8; radius: 4
                                    color: page.colorDeEstado(fila.modelData.estado)
                                }
                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 1
                                    RowLayout {
                                        spacing: 6
                                        Text {
                                            text: fila.modelData.nombre
                                            color: Theme.text; font { pixelSize: 13; weight: Font.DemiBold }
                                        }
                                        Text {
                                            visible: fila.modelData.fija
                                            text: "a mano"; color: Theme.textFaint; font.pixelSize: 9
                                        }
                                    }
                                    Text {
                                        text: (fila.modelData.modelo !== "" ? fila.modelData.modelo + " · " : "") +
                                              fila.modelData.host + " · " + page.textoDeEstado(fila.modelData)
                                        color: fila.modelData.estado === "mostrando" ? Theme.live : Theme.textDim
                                        font.pixelSize: 11
                                        elide: Text.ElideRight
                                        Layout.fillWidth: true
                                    }
                                    Text {
                                        visible: fila.modelData.que !== "" && fila.modelData.estado === "mostrando"
                                        text: fila.modelData.que
                                        color: Theme.textFaint; font.pixelSize: 10
                                        elide: Text.ElideRight
                                        Layout.fillWidth: true
                                    }
                                    // Lo que tiene que mostrar siempre: se ve
                                    // aunque ahora esté apagada, que es cuando
                                    // hace falta saberlo.
                                    Text {
                                        visible: fila.modelData.fijadoTipo !== ""
                                        text: "📌 fijo: " + page.tituloDe(fila.modelData.fijadoTipo)
                                        color: Theme.accentSoft; font.pixelSize: 10
                                        elide: Text.ElideRight
                                        Layout.fillWidth: true
                                    }
                                    Text {
                                        visible: fila.modelData.puedeMostrar === 0
                                        text: "no sabe mostrar páginas (¿es un parlante?)"
                                        color: Theme.warn; font.pixelSize: 10
                                    }
                                    Text {
                                        visible: fila.modelData.error !== ""
                                        text: fila.modelData.error
                                        color: Theme.live; font.pixelSize: 10
                                        elide: Text.ElideRight
                                        Layout.fillWidth: true
                                    }
                                }
                                GlassButton {
                                    compact: true; text: "Parar"
                                    visible: fila.modelData.estado === "mostrando"
                                    onClicked: page.p.parar(fila.modelData.id)
                                }
                                GlassButton {
                                    compact: true; icon: "⋯"
                                    onClicked: menu.popup()
                                    ToolTip.visible: hovered; ToolTip.text: "Más cosas para esta pantalla"
                                }
                                Menu {
                                    id: menu
                                    MenuItem {
                                        text: "Ver cómo está"
                                        enabled: !fila.modelData.trabajando
                                        onTriggered: page.p.refrescar(fila.modelData.id)
                                    }
                                    MenuItem {
                                        text: "Renombrar…"
                                        onTriggered: { renombrar.id = fila.modelData.id
                                                       renombrar.actual = fila.modelData.nombre
                                                       renombrar.open() }
                                    }
                                    MenuSeparator {}
                                    MenuItem {
                                        text: "Que muestre siempre este carrusel"
                                        enabled: page.modoCarrusel && page.elegida === fila.modelData.id &&
                                                 page.carruselActual() && page.carruselActual().pasos.length > 0
                                        onTriggered: page.chequear(
                                            page.p.fijar(fila.modelData.id, "carrusel:" + page.carrusel, ({})))
                                    }
                                    MenuItem {
                                        text: "Que muestre siempre esto"
                                        enabled: page.elegida === fila.modelData.id &&
                                                 page.contenidoActual() && page.contenidoActual().disponible
                                        onTriggered: page.chequear(
                                            page.p.fijar(fila.modelData.id, page.tipo, page.datos))
                                    }
                                    MenuItem {
                                        text: "Dejar fijo lo que está mostrando"
                                        enabled: fila.modelData.estado === "mostrando"
                                        onTriggered: page.chequear(page.p.fijarLoQueMuestra(fila.modelData.id))
                                    }
                                    MenuItem {
                                        text: "No dejar nada fijo"
                                        enabled: fila.modelData.fijadoTipo !== ""
                                        onTriggered: page.chequear(page.p.desfijar(fila.modelData.id))
                                    }
                                    MenuSeparator {}
                                    MenuItem {
                                        text: "Olvidar esta pantalla"
                                        onTriggered: page.chequear(page.p.olvidar(fila.modelData.id))
                                    }
                                }
                            }
                        }
                        Text {
                            anchors.centerIn: parent
                            visible: lista.count === 0
                            width: parent.width - 40
                            horizontalAlignment: Text.AlignHCenter
                            wrapMode: Text.WordWrap
                            color: Theme.textFaint; font.pixelSize: 12
                            text: page.p.buscando ? "Buscando en la red…"
                                : "Ninguna todavía. «Buscar en la red» encuentra los Chromecast que estén " +
                                  "prendidos; si el televisor está en otra red, agregalo a mano con su IP."
                        }
                    }
                }
            }

            // --- Qué ---------------------------------------------------------
            Panel {
                Layout.fillWidth: false
                Layout.preferredWidth: 380
                Layout.fillHeight: true
                ColumnLayout {
                    anchors { fill: parent; margins: 12 }
                    spacing: 8
                    RowLayout {
                        Layout.fillWidth: true
                        Text { text: "Qué mostrar"; color: Theme.text; font { pixelSize: 14; weight: Font.DemiBold } }
                        Item { Layout.fillWidth: true }
                        GlassButton {
                            compact: true; text: "Una cosa"
                            active: !page.modoCarrusel
                            onClicked: page.modoCarrusel = false
                        }
                        GlassButton {
                            compact: true; text: "Carrusel"
                            active: page.modoCarrusel
                            onClicked: page.modoCarrusel = true
                            ToolTip.visible: hovered
                            ToolTip.text: "Varias cosas rotando en la misma pantalla"
                        }
                    }

                    // --- Un carrusel ------------------------------------------
                    ColumnLayout {
                        visible: page.modoCarrusel
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        spacing: 8

                        RowLayout {
                            Layout.fillWidth: true
                            ComboBox {
                                Layout.fillWidth: true
                                model: page.p.carruseles
                                textRole: "nombre"
                                enabled: page.p.carruseles.length > 0
                                displayText: page.p.carruseles.length === 0 ? "Ninguno todavía" : currentText
                                currentIndex: { for (let i = 0; i < page.p.carruseles.length; ++i) if (page.p.carruseles[i].id === page.carrusel) return i; return 0 }
                                onActivated: (i) => page.carrusel = page.p.carruseles[i].id
                            }
                            GlassButton { compact: true; icon: "+"; onClicked: nuevoCarrusel.open() }
                        }
                        Text {
                            Layout.fillWidth: true
                            visible: page.carruselActual() !== null
                            text: page.carruselActual()
                                ? page.carruselActual().pasos.length + " pasos · la vuelta entera dura " +
                                  Math.floor(page.carruselActual().vueltaSeg / 60) + ":" +
                                  String(page.carruselActual().vueltaSeg % 60).padStart(2, "0")
                                : ""
                            color: Theme.textDim; font.pixelSize: 11
                        }

                        ListView {
                            id: pasos
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            clip: true
                            spacing: 3
                            model: page.carruselActual() ? page.carruselActual().pasos : []
                            delegate: Rectangle {
                                id: paso
                                required property var modelData
                                required property int index
                                width: pasos.width
                                implicitHeight: 40
                                radius: 7
                                color: pasoRaton.containsMouse ? Qt.rgba(1, 1, 1, 0.04) : "transparent"
                                MouseArea { id: pasoRaton; anchors.fill: parent; hoverEnabled: true }
                                RowLayout {
                                    anchors { fill: parent; leftMargin: 8; rightMargin: 6 }
                                    spacing: 6
                                    Text {
                                        text: (paso.index + 1) + "."
                                        color: Theme.textFaint; font { pixelSize: 10; family: "JetBrains Mono" }
                                    }
                                    Text {
                                        Layout.fillWidth: true
                                        text: paso.modelData.que
                                        color: Theme.text; font.pixelSize: 11
                                        elide: Text.ElideRight
                                    }
                                    SpinBox {
                                        from: 3; to: 600; stepSize: 5
                                        value: paso.modelData.segundos
                                        implicitWidth: 92
                                        onValueModified: page.p.pasoSegundos(page.carrusel, paso.index, value)
                                    }
                                    GlassButton {
                                        compact: true; text: "↑"
                                        visible: pasoRaton.containsMouse && paso.index > 0
                                        onClicked: page.p.pasoMover(page.carrusel, paso.index, paso.index - 1)
                                    }
                                    GlassButton {
                                        compact: true; text: "✕"
                                        visible: pasoRaton.containsMouse
                                        onClicked: page.chequear(page.p.pasoQuitar(page.carrusel, paso.index))
                                    }
                                }
                            }
                            Text {
                                anchors.centerIn: parent
                                visible: pasos.count === 0
                                width: parent.width - 30
                                horizontalAlignment: Text.AlignHCenter
                                wrapMode: Text.WordWrap
                                color: Theme.textFaint; font.pixelSize: 11
                                text: page.carrusel === ""
                                    ? "Creá un carrusel con el +"
                                    : "Sin pasos. Elegí «Una cosa», armala, y usá «Agregar al carrusel»."
                            }
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            visible: page.carruselActual() !== null
                            Text { text: "Transición"; color: Theme.textDim; font.pixelSize: 10 }
                            Item { Layout.fillWidth: true }
                            GlassButton {
                                compact: true; text: "Fundido"
                                active: page.carruselActual() && page.carruselActual().transicion === "fundido"
                                onClicked: page.p.carruselTransicion(page.carrusel, "fundido")
                            }
                            GlassButton {
                                compact: true; text: "Corte"
                                active: page.carruselActual() && page.carruselActual().transicion === "corte"
                                onClicked: page.p.carruselTransicion(page.carrusel, "corte")
                            }
                        }
                        GlassButton {
                            Layout.fillWidth: true
                            text: "Mostrar el carrusel"; icon: "▶"; active: true
                            enabled: page.elegida !== "" && page.carruselActual() &&
                                     page.carruselActual().pasos.length > 0
                            onClicked: page.chequear(page.p.mostrarCarrusel(page.elegida, page.carrusel))
                        }
                    }

                    // --- Una cosa sola ----------------------------------------
                    ComboBox {
                        visible: !page.modoCarrusel
                        Layout.fillWidth: true
                        model: page.p.contenidos
                        textRole: "titulo"
                        currentIndex: { for (let i = 0; i < page.p.contenidos.length; ++i) if (page.p.contenidos[i].id === page.tipo) return i; return 0 }
                        onActivated: (i) => { page.tipo = page.p.contenidos[i].id; page.datos = ({}) }
                    }
                    Text {
                        visible: !page.modoCarrusel
                        Layout.fillWidth: true
                        text: page.contenidoActual() ? page.contenidoActual().ayuda : ""
                        color: Theme.textDim; font.pixelSize: 11; wrapMode: Text.WordWrap
                    }
                    Text {
                        Layout.fillWidth: true
                        visible: !page.modoCarrusel && page.contenidoActual() && !page.contenidoActual().disponible
                        text: "⚠ " + (page.contenidoActual() ? page.contenidoActual().porque : "")
                        color: Theme.warn; font.pixelSize: 11; wrapMode: Text.WordWrap
                    }

                    // Los campos salen del catálogo: un contenido nuevo trae
                    // los suyos sin tocar esta pantalla.
                    Repeater {
                        model: page.modoCarrusel || !page.contenidoActual() ? [] : page.contenidoActual().campos
                        delegate: ColumnLayout {
                            required property var modelData
                            Layout.fillWidth: true
                            spacing: 2
                            Text { text: modelData.etiqueta; color: Theme.textDim; font.pixelSize: 11 }
                            // Una lista en vez de texto libre: las escenas
                            // armadas (las mismas de la tarjeta Escenas).
                            ComboBox {
                                id: lista
                                visible: modelData.opciones === "escenas"
                                Layout.fillWidth: true
                                model: visible ? Studio.scenes : null
                                textRole: "name"
                                function anotar() {
                                    if (!visible || currentText === "") return
                                    let d = page.datos; d[modelData.clave] = currentText; page.datos = d
                                }
                                onActivated: anotar()
                                onCountChanged: anotar()
                                Component.onCompleted: anotar()
                            }
                            // Un QR: los del aparato (Público, Sala…) y los guardados; al
                            // elegir uno se llenan su texto, su estilo y el pie.
                            ComboBox {
                                id: listaQr
                                visible: modelData.opciones === "qrs"
                                Layout.fillWidth: true
                                property var opciones: visible ? [{ value: "", label: "Escribirlo a mano (abajo)" }].concat(Studio.qr.paraElegir()) : []
                                model: opciones
                                textRole: "label"
                                valueRole: "value"
                                onActivated: {
                                    let d = page.datos
                                    d.guardado = currentValue
                                    if (currentValue === "") { delete d.estilo; page.datos = d; return }
                                    const q = Studio.qr.paraProyectarElegido(currentValue)
                                    if (q.pendiente) {
                                        Studio.notify("La sala todavía no dio ese enlace: probá de nuevo en un momento (¿está andando la sala?).", "warning")
                                        return
                                    }
                                    d.texto = q.texto; d.estilo = q.estilo
                                    if (!d.pie) d.pie = q.nombre
                                    page.datos = d
                                }
                            }
                            TextArea {
                                visible: modelData.largo && modelData.opciones === ""
                                Layout.fillWidth: true
                                Layout.preferredHeight: 90
                                placeholderText: modelData.ejemplo
                                color: Theme.text; font.pixelSize: 13
                                wrapMode: TextArea.Wrap
                                onTextChanged: { let d = page.datos; d[modelData.clave] = text; page.datos = d }
                                background: Rectangle { color: Theme.field; radius: 8 }
                            }
                            TextField {
                                visible: !modelData.largo && modelData.opciones === ""
                                Layout.fillWidth: true
                                placeholderText: modelData.ejemplo
                                color: Theme.text; font.pixelSize: 13
                                onTextChanged: { let d = page.datos; d[modelData.clave] = text; page.datos = d }
                            }
                        }
                    }

                    Item { Layout.fillHeight: true; visible: !page.modoCarrusel }

                    Text {
                        Layout.fillWidth: true
                        visible: !page.modoCarrusel && page.elegida === ""
                        text: "Elegí una pantalla de la izquierda."
                        color: Theme.textFaint; font.pixelSize: 11
                    }
                    // La dirección exacta que se le va a mandar: se puede
                    // probar en un navegador sin tener el televisor a mano.
                    Text {
                        Layout.fillWidth: true
                        visible: !page.modoCarrusel && page.elegida !== ""
                        text: page.p.urlDe(page.tipo, page.datos, page.elegida)
                        color: Theme.textFaint; font { pixelSize: 9; family: "JetBrains Mono" }
                        wrapMode: Text.WrapAnywhere
                        maximumLineCount: 2
                        elide: Text.ElideRight
                    }
                    RowLayout {
                        visible: !page.modoCarrusel
                        Layout.fillWidth: true
                        spacing: 6
                        GlassButton {
                            Layout.fillWidth: true
                            text: "Mostrar"; icon: "▶"; active: true
                            enabled: page.elegida !== "" && page.contenidoActual() && page.contenidoActual().disponible
                            onClicked: page.chequear(page.p.mostrar(page.elegida, page.tipo, page.datos))
                        }
                        GlassButton {
                            text: "Al carrusel"; icon: "+"
                            enabled: page.carrusel !== "" && page.contenidoActual() && page.contenidoActual().disponible
                            onClicked: page.chequear(page.p.pasoAgregar(page.carrusel, page.tipo, page.datos, 15),
                                                     "Agregado al carrusel")
                            ToolTip.visible: hovered
                            ToolTip.text: page.carrusel === "" ? "Creá un carrusel primero (pestaña Carrusel)"
                                                               : "Suma esto como un paso más"
                        }
                    }
                }
            }
        }
    }

    Dialog {
        id: renombrar
        anchors.centerIn: parent
        modal: true
        title: "Renombrar la pantalla"
        standardButtons: Dialog.Ok | Dialog.Cancel
        property string id: ""
        property string actual: ""
        onOpened: { nuevoNombre.text = actual; nuevoNombre.forceActiveFocus(); nuevoNombre.selectAll() }
        onAccepted: page.chequear(page.p.renombrar(id, nuevoNombre.text))
        ColumnLayout {
            spacing: 8
            Text {
                Layout.preferredWidth: 360
                wrapMode: Text.WordWrap
                color: Theme.textDim; font.pixelSize: 11
                text: "El nombre que pongas gana sobre el que anuncia el aparato: «TV del hall» dice " +
                      "más que «Percee TV»."
            }
            TextField { id: nuevoNombre; Layout.preferredWidth: 360; color: Theme.text
                        onAccepted: renombrar.accept() }
        }
    }

    Dialog {
        id: nuevoCarrusel
        anchors.centerIn: parent
        modal: true
        title: "Carrusel nuevo"
        standardButtons: Dialog.Ok | Dialog.Cancel
        onOpened: { nombreCar.text = ""; nombreCar.forceActiveFocus() }
        onAccepted: page.carrusel = page.p.carruselNuevo(nombreCar.text)
        ColumnLayout {
            spacing: 8
            Text {
                Layout.preferredWidth: 340
                wrapMode: Text.WordWrap
                color: Theme.textDim; font.pixelSize: 11
                text: "Un carrusel rota varias cosas en la misma pantalla. Se le agregan pasos desde " +
                      "«Una cosa», con el botón «Al carrusel»."
            }
            TextField { id: nombreCar; Layout.preferredWidth: 340; placeholderText: "Hall"; color: Theme.text
                        onAccepted: nuevoCarrusel.accept() }
        }
    }

    Dialog {
        id: aMano
        anchors.centerIn: parent
        modal: true
        title: "Agregar una pantalla"
        standardButtons: Dialog.Ok | Dialog.Cancel
        onOpened: { nom.text = ""; ip.text = ""; ip.forceActiveFocus() }
        onAccepted: page.chequear(page.p.agregar(nom.text, ip.text, 0))
        ColumnLayout {
            spacing: 8
            Text {
                Layout.preferredWidth: 360
                wrapMode: Text.WordWrap
                color: Theme.textDim; font.pixelSize: 11
                text: "Para cuando la búsqueda no llega (otra red, otra VLAN, el firewall). Tiene que ser " +
                      "un aparato que hable Google Cast."
            }
            TextField { id: nom; Layout.preferredWidth: 360; placeholderText: "Nombre (TV del hall)"; color: Theme.text }
            TextField { id: ip; Layout.preferredWidth: 360; placeholderText: "192.168.1.50"; color: Theme.text; onAccepted: aMano.accept() }
        }
    }
}
