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
    readonly property var cc: Studio.carruseles
    /// 0 = los archivos, 1 = armar carruseles. Los carruseles se arman ACA y no
    /// adentro de una escena: uno se arma una vez y se aplica en la apertura y
    /// en el cierre. Y se arman donde están los PDF y los PowerPoint que se
    /// desarman, que es de donde salen casi siempre.
    property int solapa: 0

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

        // --- Archivos / Carruseles -------------------------------------------
        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            GlassButton {
                text: "Archivos"
                active: page.solapa === 0
                onClicked: page.solapa = 0
            }
            GlassButton {
                text: "Carruseles"
                active: page.solapa === 1
                onClicked: page.solapa = 1
            }
            Item { Layout.fillWidth: true }

            // La barra de trabajo. Desarmar cincuenta láminas tarda; sin esto
            // la ventana se queda muda y el operador no sabe si está
            // trabajando o se colgó, diez minutos antes de empezar.
            RowLayout {
                visible: page.cc.convirtiendo
                spacing: 8
                Text {
                    text: page.cc.etapa + (page.cc.total > 0
                          ? " " + page.cc.hecho + "/" + page.cc.total : "…")
                    color: Theme.textDim
                    font.pixelSize: 12
                }
                ProgressBar {
                    Layout.preferredWidth: 180
                    // LibreOffice no informa progreso: mientras convierte, la
                    // barra va indeterminada en vez de fingir un número.
                    indeterminate: page.cc.total <= 0
                    from: 0; to: Math.max(1, page.cc.total)
                    value: page.cc.hecho
                }
                GlassButton {
                    compact: true
                    text: "Cancelar"
                    onClicked: page.cc.cancelar()
                }
            }
        }

        // --- Buscar y filtrar ------------------------------------------------
        Panel {
            id: panelBuscar
            visible: page.solapa === 0
            Layout.fillWidth: true
            // EL ALTO SE DECLARA. `Panel` es un Rectangle y no tiene
            // implicitHeight: adentro de un ColumnLayout eso son CERO pixeles.
            // El buscador se seguia viendo —los hijos desbordan un padre de
            // alto cero— pero ocupaba un lugar que el layout creia vacio, asi
            // que la grilla empezaba arriba de todo y al desplazarla las
            // miniaturas pasaban por encima del buscador.
            Layout.preferredHeight: filas.implicitHeight + 2 * panelBuscar.padding
            ColumnLayout {
                id: filas
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
            visible: page.solapa === 0
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

        // --- Armar carruseles -------------------------------------------------
        Panel {
            visible: page.solapa === 1
            Layout.fillWidth: true
            Layout.fillHeight: true
            PanelCarruseles { anchors { fill: parent; margins: 10 } }
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
                // Lo hace el aparato, no el asistente: es una conversión con
                // pasos fijos, no algo que haya que pensar. Pedírsela a una
                // LLM sería esperar unos segundos y gastar tokens para que
                // termine llamando a esto mismo.
                enabled: !page.cc.convirtiendo
                text: "Armar un carrusel"
                icon: "▦"
                onClicked: {
                    // Se salta a la solapa de carruseles: ahí está la barra de
                    // trabajo y ahí va a aparecer el resultado.
                    page.solapa = 1
                    page.cc.desarmar(page.seleccionado.ruta, "")
                    page.seleccionado = null
                }
                ToolTip.visible: hovered
                ToolTip.delay: 400
                ToolTip.text: "Convertir cada página en una lámina de un carrusel"
            }
            GlassButton {
                // Una imagen suelta va al carrusel que esté abierto: es como se
                // arma uno a mano, sin pasar por ninguna presentación.
                visible: page.seleccionado && page.seleccionado.tipo === "imagen" &&
                         page.cc.actual !== ""
                text: "Sumar a «" + page.cc.actual + "»"
                onClicked: { page.cc.agregar(page.seleccionado.ruta); page.seleccionado = null }
            }
            GlassButton {
                text: "✕"
                compact: true
                onClicked: page.seleccionado = null
            }
        }
    }
}
