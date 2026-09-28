// Medios: los archivos del operador — música, videos, imágenes, presentaciones.
//
// POR QUE EXISTE ESTA PESTAÑA. `medios` era una de las dos únicas categorías
// del MCP sin una sola línea de interfaz: el operador no tenía dónde ver sus
// propios archivos. Para poner un video de fondo había que pedírselo al
// asistente o saberse la ruta de memoria, en un aparato donde todo lo demás se
// hace con las manos.
//
// LAS VISTAS PREVIAS SON GRANDES A PROPÓSITO. No son un adorno: son CÓMO se
// encuentra un archivo. Una grilla de iconos con el nombre abajo obliga a leer
// doscientos nombres; una de miniaturas se recorre con el ojo. Es la misma
// razón por la que el carrusel las muestra grandes.
//
// Y EL FILTRO FILTRA MIENTRAS SE ESCRIBE, sin volver al disco: la lista ya está
// en memoria. Con doscientos archivos, una lista sin filtro es tan inservible
// como no tenerla.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import BA
import "../controls"

Rectangle {
    id: page
    color: Theme.bg
    property real leftInset: 92

    readonly property var m: Studio.medios

    function tamano(bytes) {
        if (bytes < 1024) return bytes + " B"
        if (bytes < 1024 * 1024) return Math.round(bytes / 1024) + " KB"
        if (bytes < 1024 * 1024 * 1024) return (bytes / 1024 / 1024).toFixed(1) + " MB"
        return (bytes / 1024 / 1024 / 1024).toFixed(1) + " GB"
    }

    ColumnLayout {
        anchors { fill: parent; topMargin: 18; bottomMargin: 14 }
        anchors.leftMargin: page.leftInset
        anchors.rightMargin: 20
        spacing: 12

        // --- Buscar y filtrar ------------------------------------------------
        Panel {
            Layout.fillWidth: true
            ColumnLayout {
                anchors.fill: parent
                spacing: 8

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 10
                    TextField {
                        id: busca
                        Layout.fillWidth: true
                        color: Theme.text
                        placeholderText: "Buscar… (sin tildes: «logo camion» encuentra «Logo_Camión.png»)"
                        // En cada tecla: la lista está en memoria y filtrar es
                        // comparar texto.
                        onTextChanged: page.m.consulta = text
                        Keys.onEscapePressed: text = ""
                    }
                    GlassButton {
                        compact: true
                        visible: busca.text !== ""
                        text: "✕"
                        onClicked: busca.text = ""
                    }
                    GlassButton {
                        compact: true
                        text: "Refrescar"
                        onClicked: page.m.refrescar()
                        ToolTip.visible: hovered
                        ToolTip.delay: 400
                        ToolTip.text: "Volver a mirar el disco: después de copiar archivos a Medios/"
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
                            active: page.m.tipo === modelData.id
                            // El número al lado: es lo que deja decir «no hay
                            // ningún video» en vez de mostrar una grilla vacía
                            // sin explicación.
                            text: modelData.rotulo +
                                  (modelData.id === "" ? ""
                                   : " (" + (page.m.cuenta[modelData.id] || 0) + ")")
                            onClicked: page.m.tipo = modelData.id
                        }
                    }
                    Item { Layout.fillWidth: true }
                    Text {
                        text: page.m.mostrados === page.m.total
                              ? page.m.total + " archivos"
                              : page.m.mostrados + " de " + page.m.total
                        color: Theme.textDim
                        font.pixelSize: 12
                    }
                }
            }
        }

        // --- La grilla --------------------------------------------------------
        Panel {
            Layout.fillWidth: true
            Layout.fillHeight: true

            Item {
                anchors.fill: parent

                GridView {
                    id: grilla
                    anchors.fill: parent
                    clip: true
                    // Miniaturas grandes: se recorren con el ojo, no leyendo
                    // doscientos nombres.
                    cellWidth: 232
                    cellHeight: 176
                    model: page.m.archivos
                    ScrollBar.vertical: ScrollBar {}

                    delegate: Item {
                        id: celda
                        required property var modelData
                        width: grilla.cellWidth - 10
                        height: grilla.cellHeight - 10

                        Rectangle {
                            anchors.fill: parent
                            radius: Theme.radiusSmall
                            color: ma.containsMouse ? Qt.rgba(1, 1, 1, 0.09) : Qt.rgba(1, 1, 1, 0.04)
                            border.color: Theme.glassBorder
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
                                    // eso congela la grilla, así que van con su
                                    // símbolo y su extensión, que alcanza para
                                    // reconocerlos.
                                    Image {
                                        anchors.fill: parent
                                        visible: celda.modelData.tipo === "imagen"
                                        source: celda.modelData.tipo === "imagen" ? celda.modelData.url : ""
                                        fillMode: Image.PreserveAspectFit
                                        asynchronous: true
                                        cache: true
                                        // Se decodifica al tamaño de la celda y
                                        // no al del archivo: una foto de 24 MP
                                        // en memoria, doscientas veces, es la
                                        // forma más rápida de quedarse sin RAM.
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
                                    text: celda.modelData.carpeta + " · " + page.tamano(celda.modelData.bytes)
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
                                onClicked: page.seleccionado = celda.modelData
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
                    text: page.m.total === 0
                          ? "No hay archivos de medios todavía.\n\nCopiá tus imágenes, videos y música a:\n" +
                            page.m.carpetaDeMedios()
                          : "Nada coincide con lo que buscaste."
                }
            }
        }
    }

    /// Lo último que se tocó, para las acciones de abajo.
    property var seleccionado: null

    // --- Qué hacer con el archivo elegido ------------------------------------
    Rectangle {
        visible: page.seleccionado !== null
        anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
        anchors.leftMargin: page.leftInset
        anchors.rightMargin: 20
        anchors.bottomMargin: 14
        height: 56
        radius: Theme.radiusSmall
        color: Qt.rgba(0.06, 0.06, 0.07, 0.96)
        border.color: Theme.glassBorder

        RowLayout {
            anchors { fill: parent; leftMargin: 14; rightMargin: 14 }
            spacing: 10
            Text {
                Layout.fillWidth: true
                text: page.seleccionado ? page.seleccionado.relativa : ""
                color: Theme.text
                font.pixelSize: 13
                elide: Text.ElideMiddle
            }
            GlassButton {
                text: "Usar en una escena"
                onClicked: ApplicationWindow.window.pedirAlAsistente(
                    "Poné este archivo en la escena que corresponda: " + page.seleccionado.relativa)
            }
            GlassButton {
                visible: page.seleccionado && page.seleccionado.desarmable
                text: "Armar un carrusel"
                icon: "▦"
                onClicked: ApplicationWindow.window.pedirAlAsistente(
                    "Desarmá esta presentación en imágenes y armá un carrusel: " +
                    page.seleccionado.relativa)
                ToolTip.visible: hovered
                ToolTip.delay: 400
                ToolTip.text: "Convertir cada página en una lámina de un carrusel"
            }
            GlassButton {
                text: "✕"
                compact: true
                onClicked: page.seleccionado = null
            }
        }
    }
}
