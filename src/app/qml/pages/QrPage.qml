// Página Códigos QR: la biblioteca y el estudio de diseño.
//
// Tres columnas: los códigos GUARDADOS (de este aparato o de este evento), lo
// que el código DICE (el tipo y sus campos), y cómo SE VE (el estilo, con la
// vista previa grande al lado).
//
// La vista previa no es una aproximación: rasteriza el mismo SVG que se
// exporta y que se le manda a un televisor. Lo que se ve acá es lo que sale.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import BA
import "../controls"

Rectangle {
    id: page
    color: Theme.bg
    property real leftInset: 92
    readonly property var q: Studio.qr
    readonly property var est: q.actual.estilo

    function tipoActual() {
        for (let i = 0; i < q.tipos.length; ++i)
            if (q.tipos[i].id === q.actual.tipo) return q.tipos[i]
        return null
    }
    function chequear(err, bien) {
        if (err && err !== "") ApplicationWindow.window.toast(err, "error")
        else if (bien) ApplicationWindow.window.toast(bien, "ok")
        return !err || err === ""
    }

    ColumnLayout {
        anchors { fill: parent; leftMargin: page.leftInset; margins: 20 }
        spacing: 12

        RowLayout {
            Layout.fillWidth: true
            spacing: 10
            Text { text: "Códigos QR"; color: Theme.text; font { pixelSize: 20; weight: Font.DemiBold } }
            StatusPill {
                text: page.q.codigos.length + " guardados"
                dot: Theme.textFaint
            }
            Item { Layout.fillWidth: true }
            Text {
                visible: page.q.modulos > 0
                text: page.q.modulos + "×" + page.q.modulos + " módulos"
                color: page.q.modulos > 80 ? Theme.warn : Theme.textFaint
                font.pixelSize: 11
                ToolTip.visible: hoverHandler.hovered
                ToolTip.text: page.q.modulos > 80
                    ? "Muy denso: de lejos va a costar leerlo. Probá con menos texto o menos corrección."
                    : "Cuántos cuadraditos de lado tiene"
                HoverHandler { id: hoverHandler }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 12

            // --- Los guardados -----------------------------------------------
            Panel {
                Layout.fillWidth: false
                Layout.preferredWidth: 270
                Layout.fillHeight: true
                ColumnLayout {
                    anchors { fill: parent; margins: 12 }
                    spacing: 8

                    RowLayout {
                        Layout.fillWidth: true
                        GlassButton {
                            compact: true; text: "Del aparato"
                            active: page.q.ambito === "global"
                            onClicked: page.q.ambito = "global"
                            ToolTip.visible: hovered
                            ToolTip.text: "Sirven para todos los eventos (el WiFi de la sala, el teleprompter)"
                        }
                        GlassButton {
                            compact: true; text: "De este evento"
                            active: page.q.ambito === "evento"
                            onClicked: page.q.ambito = "evento"
                            ToolTip.visible: hovered
                            ToolTip.text: "Viajan con el evento a otra máquina"
                        }
                    }
                    Text {
                        Layout.fillWidth: true
                        visible: page.q.ambito === "evento" && page.q.sinEvento !== ""
                        text: page.q.sinEvento
                        color: Theme.warn; font.pixelSize: 11; wrapMode: Text.WordWrap
                    }

                    ListView {
                        id: lista
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        spacing: 3
                        model: page.q.codigos
                        ScrollBar.vertical: ScrollBar {}
                        delegate: Rectangle {
                            id: fila
                            required property var modelData
                            width: lista.width
                            implicitHeight: 58
                            radius: 8
                            color: page.q.abierto === fila.modelData.id ? Qt.rgba(1, 1, 1, 0.07)
                                 : (raton.containsMouse ? Qt.rgba(1, 1, 1, 0.035) : "transparent")
                            MouseArea {
                                id: raton
                                anchors.fill: parent
                                hoverEnabled: true
                                acceptedButtons: Qt.LeftButton | Qt.RightButton
                                onClicked: (m) => m.button === Qt.RightButton ? menu.popup() : page.q.abrir(fila.modelData.id)
                            }
                            RowLayout {
                                anchors { fill: parent; leftMargin: 8; rightMargin: 8 }
                                spacing: 8
                                // La miniatura del código guardado: es la forma
                                // más rápida de encontrar el que uno busca.
                                Rectangle {
                                    Layout.preferredWidth: 42; Layout.preferredHeight: 42
                                    radius: 6; color: "white"
                                    Image {
                                        anchors { fill: parent; margins: 3 }
                                        source: "image://qr/" + fila.modelData.id + "?rev=" + page.q.revision
                                        sourceSize: Qt.size(84, 84)
                                        fillMode: Image.PreserveAspectFit
                                        asynchronous: true
                                    }
                                }
                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 0
                                    Text {
                                        Layout.fillWidth: true
                                        text: fila.modelData.nombre
                                        color: Theme.text; font { pixelSize: 12; weight: Font.DemiBold }
                                        elide: Text.ElideRight
                                    }
                                    Text {
                                        Layout.fillWidth: true
                                        text: fila.modelData.tipoTitulo
                                        color: Theme.textDim; font.pixelSize: 10
                                    }
                                    Text {
                                        Layout.fillWidth: true
                                        text: fila.modelData.resumen
                                        color: Theme.textFaint; font.pixelSize: 9
                                        elide: Text.ElideRight
                                    }
                                }
                            }
                            Menu {
                                id: menu
                                MenuItem {
                                    text: page.q.ambito === "global" ? "Copiar al evento" : "Copiar al aparato"
                                    onTriggered: page.chequear(page.q.copiarA(fila.modelData.id,
                                        page.q.ambito === "global" ? "evento" : "global"))
                                }
                                MenuItem { text: "Borrar"; onTriggered: page.chequear(page.q.borrar(fila.modelData.id)) }
                            }
                        }
                        Text {
                            anchors.centerIn: parent
                            visible: lista.count === 0
                            width: parent.width - 30
                            horizontalAlignment: Text.AlignHCenter
                            wrapMode: Text.WordWrap
                            color: Theme.textFaint; font.pixelSize: 11
                            text: "Ninguno todavía. Elegí un tipo a la derecha y guardalo."
                        }
                    }
                }
            }

            // --- Qué dice + vista previa ---------------------------------------
            Panel {
                Layout.fillWidth: true
                Layout.fillHeight: true
                ColumnLayout {
                    anchors { fill: parent; margins: 12 }
                    spacing: 10

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8
                        ComboBox {
                            Layout.preferredWidth: 220
                            model: page.q.tipos
                            textRole: "titulo"
                            currentIndex: { for (let i = 0; i < page.q.tipos.length; ++i) if (page.q.tipos[i].id === page.q.actual.tipo) return i; return 0 }
                            onActivated: (i) => page.q.setTipo(page.q.tipos[i].id)
                        }
                        TextField {
                            id: nombre
                            Layout.fillWidth: true
                            placeholderText: "Nombre para guardarlo (WiFi de la sala)"
                            text: page.q.actual.nombre
                            color: Theme.text; font.pixelSize: 12
                            onAccepted: page.chequear(page.q.guardar(text), "Guardado")
                        }
                        GlassButton {
                            compact: true; text: "Guardar"; icon: "↓"; active: page.q.sucio
                            onClicked: page.chequear(page.q.guardar(nombre.text), "Guardado")
                        }
                        GlassButton { compact: true; text: "Nuevo"; icon: "+"; onClicked: { page.q.nuevo("enlace"); nombre.text = "" } }
                    }

                    Text {
                        Layout.fillWidth: true
                        text: page.tipoActual() ? page.tipoActual().ayuda : ""
                        color: Theme.textDim; font.pixelSize: 11; wrapMode: Text.WordWrap
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        spacing: 16

                        // Los campos del tipo elegido.
                        ColumnLayout {
                            Layout.fillWidth: false
                            Layout.preferredWidth: 300
                            Layout.alignment: Qt.AlignTop
                            spacing: 6
                            Repeater {
                                model: page.tipoActual() ? page.tipoActual().campos : []
                                delegate: ColumnLayout {
                                    required property var modelData
                                    Layout.fillWidth: true
                                    spacing: 1
                                    Text { text: modelData.etiqueta; color: Theme.textDim; font.pixelSize: 10 }
                                    TextArea {
                                        visible: modelData.largo
                                        Layout.fillWidth: true
                                        Layout.preferredHeight: 80
                                        placeholderText: modelData.ejemplo
                                        text: page.q.actual.datos[modelData.clave] || ""
                                        color: Theme.text; font.pixelSize: 12
                                        wrapMode: TextArea.Wrap
                                        onTextChanged: page.q.setDato(modelData.clave, text)
                                        background: Rectangle { color: Theme.field; radius: 6 }
                                    }
                                    TextField {
                                        visible: !modelData.largo
                                        Layout.fillWidth: true
                                        placeholderText: modelData.ejemplo
                                        text: page.q.actual.datos[modelData.clave] || ""
                                        color: Theme.text; font.pixelSize: 12
                                        onTextChanged: page.q.setDato(modelData.clave, text)
                                    }
                                }
                            }
                            Text {
                                Layout.fillWidth: true
                                visible: page.tipoActual() && page.tipoActual().delAparato
                                wrapMode: Text.WordWrap
                                color: Theme.textDim; font.pixelSize: 11
                                text: "Este lo arma el sistema: lleva la clave que corresponda en cada " +
                                      "momento. Por eso se guarda QUÉ es, no el texto, y el código sale " +
                                      "siempre al día."
                            }
                            GlassButton {
                                visible: page.tipoActual() && page.tipoActual().delAparato
                                compact: true; text: "Volver a pedir el enlace"; icon: "⟳"
                                onClicked: page.q.resolver()
                            }
                        }

                        // La vista previa y lo que el código dice.
                        ColumnLayout {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            spacing: 8
                            Rectangle {
                                Layout.alignment: Qt.AlignHCenter
                                Layout.preferredWidth: Math.min(330, parent.width)
                                Layout.preferredHeight: Layout.preferredWidth
                                radius: 14
                                color: page.q.aviso === "" ? "white" : Qt.rgba(1, 1, 1, 0.05)
                                Image {
                                    anchors { fill: parent; margins: 10 }
                                    visible: page.q.aviso === ""
                                    source: "image://qr/actual?rev=" + page.q.revision
                                    sourceSize: Qt.size(660, 660)
                                    fillMode: Image.PreserveAspectFit
                                    asynchronous: true
                                    cache: false
                                }
                                Text {
                                    anchors { fill: parent; margins: 22 }
                                    visible: page.q.aviso !== ""
                                    wrapMode: Text.WordWrap
                                    horizontalAlignment: Text.AlignHCenter
                                    verticalAlignment: Text.AlignVCenter
                                    color: Theme.warn; font.pixelSize: 12
                                    text: page.q.aviso
                                }
                            }
                            // Un QR es opaco: ver qué dice evita mandar mil
                            // folletos a imprimir con la dirección vieja.
                            Text {
                                Layout.fillWidth: true
                                text: page.q.contenido
                                color: Theme.textFaint
                                font { pixelSize: 10; family: "JetBrains Mono" }
                                wrapMode: Text.WrapAnywhere
                                maximumLineCount: 4
                                elide: Text.ElideRight
                            }
                            Item { Layout.fillHeight: true }
                            RowLayout {
                                Layout.alignment: Qt.AlignHCenter
                                spacing: 8
                                GlassButton { compact: true; text: "Guardar PNG"; icon: "↓"
                                              onClicked: page.q.exportar("png", 1024) }
                                GlassButton { compact: true; text: "Guardar SVG"; icon: "↓"
                                              onClicked: page.q.exportar("svg") }
                            }
                        }
                    }
                }
            }

            // --- Cómo se ve ----------------------------------------------------
            Panel {
                Layout.fillWidth: false
                Layout.preferredWidth: 250
                Layout.fillHeight: true
                ColumnLayout {
                    anchors { fill: parent; margins: 12 }
                    spacing: 10
                    Text { text: "Cómo se ve"; color: Theme.text; font { pixelSize: 14; weight: Font.DemiBold } }

                    Text { text: "Color"; color: Theme.textDim; font.pixelSize: 10 }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 6
                        Repeater {
                            model: ["#000000", "#1d4ed8", "#ff6a2b", "#059669", "#7c3aed", "#be123c"]
                            delegate: Rectangle {
                                required property string modelData
                                Layout.preferredWidth: 26; Layout.preferredHeight: 26
                                radius: 13
                                color: modelData
                                border.color: page.est.primario === modelData ? Theme.text : "transparent"
                                border.width: 2
                                MouseArea { anchors.fill: parent; onClicked: page.q.setEstilo("primario", modelData) }
                            }
                        }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        Text { text: "Degradado"; color: Theme.textDim; font.pixelSize: 10 }
                        Item { Layout.fillWidth: true }
                        Toggle {
                            checked: page.est.secundario !== ""
                            onToggled: (w) => page.q.setEstilo("secundario", w ? "#7c3aed" : "")
                        }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        visible: page.est.secundario !== ""
                        spacing: 6
                        Repeater {
                            model: ["#7c3aed", "#ff6a2b", "#0ea5e9", "#f59e0b"]
                            delegate: Rectangle {
                                required property string modelData
                                Layout.preferredWidth: 22; Layout.preferredHeight: 22; radius: 11
                                color: modelData
                                border.color: page.est.secundario === modelData ? Theme.text : "transparent"
                                border.width: 2
                                MouseArea { anchors.fill: parent; onClicked: page.q.setEstilo("secundario", modelData) }
                            }
                        }
                    }

                    Text { text: "Cuerpo"; color: Theme.textDim; font.pixelSize: 10 }
                    ComboBox {
                        Layout.fillWidth: true
                        textRole: "t"; valueRole: "v"
                        model: [{t: "Cuadrados", v: "cuadrados"}, {t: "Redondeados", v: "redondeados"},
                                {t: "Puntos", v: "circulos"}, {t: "Separados", v: "separados"},
                                {t: "Muy redondeados", v: "muy-redondeados"}]
                        currentIndex: Math.max(0, indexOfValue(page.est.puntos))
                        onActivated: page.q.setEstilo("puntos", currentValue)
                    }
                    Text { text: "Ojos"; color: Theme.textDim; font.pixelSize: 10 }
                    ComboBox {
                        Layout.fillWidth: true
                        textRole: "t"; valueRole: "v"
                        model: [{t: "Cuadrados", v: "cuadrados"}, {t: "Redondeados", v: "redondeados"}]
                        currentIndex: Math.max(0, indexOfValue(page.est.ojos))
                        onActivated: page.q.setEstilo("ojos", currentValue)
                        ToolTip.visible: hovered
                        ToolTip.text: "Solo dos: un ojo más redondo que esto deja de leerse"
                    }
                    Text { text: "Ícono al centro"; color: Theme.textDim; font.pixelSize: 10 }
                    ComboBox {
                        Layout.fillWidth: true
                        model: page.q.iconos
                        textRole: "titulo"; valueRole: "id"
                        currentIndex: Math.max(0, indexOfValue(page.est.icono))
                        onActivated: page.q.setEstilo("icono", currentValue)
                        ToolTip.visible: hovered
                        ToolTip.text: "Tapa módulos, así que sube la corrección de errores solo"
                    }
                    Text { text: "Marco"; color: Theme.textDim; font.pixelSize: 10 }
                    ComboBox {
                        Layout.fillWidth: true
                        textRole: "t"; valueRole: "v"
                        model: [{t: "Sin marco", v: "ninguno"}, {t: "Simple", v: "simple"},
                                {t: "Redondeado", v: "redondeado"}]
                        currentIndex: Math.max(0, indexOfValue(page.est.marco))
                        onActivated: page.q.setEstilo("marco", currentValue)
                    }
                    Text { text: "Corrección de errores"; color: Theme.textDim; font.pixelSize: 10 }
                    ComboBox {
                        Layout.fillWidth: true
                        enabled: page.est.icono === ""
                        textRole: "t"; valueRole: "v"
                        model: [{t: "Baja (más chico)", v: "baja"}, {t: "Media", v: "media"},
                                {t: "Alta", v: "alta"}, {t: "Máxima (aguanta más)", v: "maxima"}]
                        currentIndex: Math.max(0, indexOfValue(page.est.correccion))
                        onActivated: page.q.setEstilo("correccion", currentValue)
                        ToolTip.visible: hovered
                        ToolTip.text: page.est.icono !== ""
                            ? "Con ícono va siempre en máxima: si no, el ícono tapa módulos y deja de leerse"
                            : "Cuánto daño aguanta antes de no leerse. Más corrección, código más grande"
                    }
                    Item { Layout.fillHeight: true }
                    GlassButton {
                        Layout.fillWidth: true
                        compact: true; text: "Volver al estilo simple"
                        onClicked: {
                            page.q.setEstilo("primario", "#000000"); page.q.setEstilo("secundario", "")
                            page.q.setEstilo("puntos", "cuadrados"); page.q.setEstilo("ojos", "cuadrados")
                            page.q.setEstilo("marco", "ninguno"); page.q.setEstilo("icono", "")
                        }
                        ToolTip.visible: hovered
                        ToolTip.text: "Negro, cuadrado y sin adornos: aburrido y siempre se lee"
                    }
                }
            }
        }
    }
}
