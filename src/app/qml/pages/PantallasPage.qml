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
    function avisar(t, tipo) { ApplicationWindow.window.toast(t, tipo || "error") }
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
                                    Text {
                                        visible: fila.modelData.error !== ""
                                        text: fila.modelData.error
                                        color: Theme.live; font.pixelSize: 10
                                        elide: Text.ElideRight
                                        Layout.fillWidth: true
                                    }
                                }
                                GlassButton {
                                    compact: true; text: "Ver cómo está"; icon: "⟳"
                                    visible: raton.containsMouse && !fila.modelData.trabajando
                                    onClicked: page.p.refrescar(fila.modelData.id)
                                }
                                GlassButton {
                                    compact: true; text: "Parar"
                                    visible: fila.modelData.estado === "mostrando"
                                    onClicked: page.p.parar(fila.modelData.id)
                                }
                                GlassButton {
                                    compact: true; text: "Olvidar"
                                    visible: raton.containsMouse && fila.modelData.fija
                                    onClicked: page.chequear(page.p.olvidar(fila.modelData.id))
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
                    Text { text: "Qué mostrar"; color: Theme.text; font { pixelSize: 14; weight: Font.DemiBold } }

                    ComboBox {
                        Layout.fillWidth: true
                        model: page.p.contenidos
                        textRole: "titulo"
                        currentIndex: { for (let i = 0; i < page.p.contenidos.length; ++i) if (page.p.contenidos[i].id === page.tipo) return i; return 0 }
                        onActivated: (i) => { page.tipo = page.p.contenidos[i].id; page.datos = ({}) }
                    }
                    Text {
                        Layout.fillWidth: true
                        text: page.contenidoActual() ? page.contenidoActual().ayuda : ""
                        color: Theme.textDim; font.pixelSize: 11; wrapMode: Text.WordWrap
                    }
                    Text {
                        Layout.fillWidth: true
                        visible: page.contenidoActual() && !page.contenidoActual().disponible
                        text: "⚠ " + (page.contenidoActual() ? page.contenidoActual().porque : "")
                        color: Theme.warn; font.pixelSize: 11; wrapMode: Text.WordWrap
                    }

                    // Los campos salen del catálogo: un contenido nuevo trae
                    // los suyos sin tocar esta pantalla.
                    Repeater {
                        model: page.contenidoActual() ? page.contenidoActual().campos : []
                        delegate: ColumnLayout {
                            required property var modelData
                            Layout.fillWidth: true
                            spacing: 2
                            Text { text: modelData.etiqueta; color: Theme.textDim; font.pixelSize: 11 }
                            TextArea {
                                visible: modelData.largo
                                Layout.fillWidth: true
                                Layout.preferredHeight: 90
                                placeholderText: modelData.ejemplo
                                color: Theme.text; font.pixelSize: 13
                                wrapMode: TextArea.Wrap
                                onTextChanged: { let d = page.datos; d[modelData.clave] = text; page.datos = d }
                                background: Rectangle { color: Theme.field; radius: 8 }
                            }
                            TextField {
                                visible: !modelData.largo
                                Layout.fillWidth: true
                                placeholderText: modelData.ejemplo
                                color: Theme.text; font.pixelSize: 13
                                onTextChanged: { let d = page.datos; d[modelData.clave] = text; page.datos = d }
                            }
                        }
                    }

                    Item { Layout.fillHeight: true }

                    Text {
                        Layout.fillWidth: true
                        visible: page.elegida === ""
                        text: "Elegí una pantalla de la izquierda."
                        color: Theme.textFaint; font.pixelSize: 11
                    }
                    // La dirección exacta que se le va a mandar: se puede
                    // probar en un navegador sin tener el televisor a mano.
                    Text {
                        Layout.fillWidth: true
                        visible: page.elegida !== ""
                        text: page.p.urlDe(page.tipo, page.datos, page.elegida)
                        color: Theme.textFaint; font { pixelSize: 9; family: "JetBrains Mono" }
                        wrapMode: Text.WrapAnywhere
                        maximumLineCount: 2
                        elide: Text.ElideRight
                    }
                    GlassButton {
                        Layout.fillWidth: true
                        text: "Mostrar"
                        icon: "▶"
                        active: true
                        enabled: page.elegida !== "" && page.contenidoActual() && page.contenidoActual().disponible
                        onClicked: page.chequear(page.p.mostrar(page.elegida, page.tipo, page.datos))
                    }
                }
            }
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
