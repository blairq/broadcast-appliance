// La mesa de armado de carruseles.
//
// DOS COLUMNAS Y NO UNA. A la izquierda la biblioteca —qué carruseles hay—, a
// la derecha las láminas del que está abierto. Con una sola lista habría que
// entrar y salir para comparar dos presentaciones, que es justo lo que se hace
// mientras se prepara: «este va en la apertura, este en el cierre».
//
// LAS MINIATURAS SON GRANDES A PROPÓSITO, igual que en la grilla de medios: en
// vivo, el operador busca «la del gráfico de barras» con el ojo, no leyendo
// veinte nombres de archivo.
//
// EL CUADRO DE BÚSQUEDA filtra las láminas del carrusel abierto mientras se
// escribe, sobre el título y sobre las etiquetas que sacó el que mira
// imágenes. Es lo que convierte una presentación de cincuenta en tres.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import BA
import "."

Item {
    id: raiz

    readonly property var cc: Studio.carruseles
    /// Lo pone en true PanelWindow cuando esto vive en su propia ventana.
    property bool enVentana: false

    RowLayout {
        anchors.fill: parent
        spacing: 12

        // --- La biblioteca ---------------------------------------------------
        ColumnLayout {
            Layout.preferredWidth: 220
            Layout.fillHeight: true
            spacing: 8

            RowLayout {
                Layout.fillWidth: true
                Text {
                    Layout.fillWidth: true
                    text: "Carruseles"
                    color: Theme.textDim
                    font { family: Theme.font; pixelSize: 12; weight: Font.DemiBold }
                }
                GlassButton {
                    compact: true
                    text: "+ Nuevo"
                    onClicked: dlgNuevo.open()
                }
            }

            ListView {
                id: lista
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                spacing: 6
                model: raiz.cc.carruseles
                ScrollBar.vertical: ScrollBar {}

                delegate: Rectangle {
                    id: fila
                    required property var modelData
                    width: lista.width - 12
                    height: 56
                    radius: Theme.radiusSmall
                    readonly property bool abierto: raiz.cc.actual === modelData.nombre
                    color: abierto ? Qt.rgba(1, 0.42, 0.17, 0.22)
                         : (fma.containsMouse ? Qt.rgba(1, 1, 1, 0.09) : Qt.rgba(1, 1, 1, 0.04))
                    border.color: abierto ? Theme.accentSoft : Theme.glassBorder

                    RowLayout {
                        anchors { fill: parent; margins: 6 }
                        spacing: 8
                        Rectangle {
                            Layout.preferredWidth: 60
                            Layout.fillHeight: true
                            radius: 4
                            color: "#0b0e12"
                            clip: true
                            Image {
                                anchors.fill: parent
                                source: fila.modelData.tapa
                                fillMode: Image.PreserveAspectCrop
                                asynchronous: true
                                sourceSize.width: 120
                            }
                            Text {
                                anchors.centerIn: parent
                                visible: fila.modelData.tapa === ""
                                text: "▦"
                                color: Theme.textFaint
                                font.pixelSize: 20
                            }
                        }
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 2
                            Text {
                                Layout.fillWidth: true
                                text: fila.modelData.nombre
                                color: Theme.text
                                font { family: Theme.font; pixelSize: 12 }
                                elide: Text.ElideRight
                            }
                            Text {
                                text: fila.modelData.laminas + " láminas"
                                color: Theme.textFaint
                                font.pixelSize: 10
                            }
                        }
                    }

                    MouseArea {
                        id: fma
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: raiz.cc.actual = fila.modelData.nombre
                    }
                }

                Text {
                    anchors.centerIn: parent
                    visible: lista.count === 0
                    width: parent.width - 20
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.WordWrap
                    color: Theme.textFaint
                    font.pixelSize: 12
                    text: "Todavía no hay ninguno.\n\nArrastrá un PDF o un PowerPoint\na la pestaña Archivos, o creá\nuno vacío y agregale imágenes."
                }
            }
        }

        // --- Las láminas del abierto -----------------------------------------
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
                    enabled: raiz.cc.actual !== ""
                    color: Theme.text
                    placeholderText: raiz.cc.actual === ""
                                     ? "Elegí un carrusel de la izquierda"
                                     : "Buscar en las láminas… (título y etiquetas)"
                    onTextChanged: raiz.cc.consulta = text
                    Keys.onEscapePressed: text = ""
                }
                GlassButton {
                    compact: true
                    visible: raiz.cc.actual !== ""
                    text: "Borrar carrusel"
                    onClicked: dlgBorrar.open()
                }
            }

            GridView {
                id: grilla
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                // Grandes: es CÓMO se encuentra una lámina, no un adorno.
                cellWidth: 248
                cellHeight: 186
                model: raiz.cc.laminas
                ScrollBar.vertical: ScrollBar {}

                delegate: Item {
                    id: celda
                    required property var modelData
                    width: grilla.cellWidth - 10
                    height: grilla.cellHeight - 10

                    Rectangle {
                        anchors.fill: parent
                        radius: Theme.radiusSmall
                        color: cma.containsMouse ? Qt.rgba(1, 1, 1, 0.09) : Qt.rgba(1, 1, 1, 0.04)
                        // El borde naranja marca dónde está parado el carrusel:
                        // la misma marca que el bus de aire en las escenas.
                        border.width: celda.modelData.actual ? 2 : 1
                        border.color: celda.modelData.actual ? Theme.accent : Theme.glassBorder

                        ColumnLayout {
                            anchors { fill: parent; margins: 6 }
                            spacing: 4
                            Rectangle {
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                radius: 4
                                color: "#0b0e12"
                                clip: true
                                Image {
                                    anchors.fill: parent
                                    source: celda.modelData.url
                                    fillMode: Image.PreserveAspectFit
                                    asynchronous: true
                                    // Al tamaño de la celda: una lámina de
                                    // 1920 px por cincuenta, en memoria, es la
                                    // forma más rápida de quedarse sin RAM.
                                    sourceSize.width: 280
                                }
                                Rectangle {
                                    anchors { left: parent.left; top: parent.top; margins: 4 }
                                    width: num.implicitWidth + 10
                                    height: 18
                                    radius: 9
                                    color: Qt.rgba(0, 0, 0, 0.6)
                                    Text {
                                        id: num
                                        anchors.centerIn: parent
                                        text: (celda.modelData.indice + 1)
                                        color: Theme.text
                                        font { pixelSize: 10; weight: Font.DemiBold }
                                    }
                                }
                            }
                            Text {
                                Layout.fillWidth: true
                                text: celda.modelData.titulo
                                color: Theme.text
                                font.pixelSize: 11
                                elide: Text.ElideMiddle
                            }
                        }

                        MouseArea {
                            id: cma
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            // Un clic la pone: es la razón de que el carrusel
                            // exista, y no se le pide confirmación a nadie.
                            onClicked: raiz.cc.ir(celda.modelData.indice)
                        }

                        // Mover y quitar aparecen al pasar por encima: siempre
                        // visibles serían tres botones por lámina y cincuenta
                        // láminas, o sea ruido.
                        RowLayout {
                            anchors { right: parent.right; top: parent.top; margins: 6 }
                            visible: cma.containsMouse
                            spacing: 3
                            GlassButton {
                                compact: true; text: "◀"
                                onClicked: raiz.cc.mover(celda.modelData.indice, celda.modelData.indice - 1)
                            }
                            GlassButton {
                                compact: true; text: "▶"
                                onClicked: raiz.cc.mover(celda.modelData.indice, celda.modelData.indice + 1)
                            }
                            GlassButton {
                                compact: true; text: "✕"
                                onClicked: raiz.cc.quitar(celda.modelData.indice)
                            }
                        }
                    }
                }

                Text {
                    anchors.centerIn: parent
                    visible: grilla.count === 0
                    width: parent.width - 80
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.WordWrap
                    color: Theme.textFaint
                    font.pixelSize: 13
                    text: raiz.cc.actual === "" ? "Elegí un carrusel de la izquierda."
                        : busca.text !== "" ? "Ninguna lámina coincide con lo que buscaste."
                        : "Este carrusel está vacío.\nAgregale imágenes desde la pestaña Archivos."
                }
            }
        }
    }

    Dialog {
        id: dlgNuevo
        title: "Carrusel nuevo"
        modal: true
        anchors.centerIn: Overlay.overlay
        standardButtons: Dialog.Ok | Dialog.Cancel
        onOpened: nombre.forceActiveFocus()
        onAccepted: { if (raiz.cc.crear(nombre.text)) nombre.text = "" }
        width: 380
        ColumnLayout {
            spacing: 8
            TextField {
                id: nombre
                Layout.preferredWidth: dlgNuevo.availableWidth
                color: Theme.text
                placeholderText: "Cómo llamarlo: «Apertura», «Sponsors»…"
                onAccepted: dlgNuevo.accept()
            }
        }
    }

    // Dialogo propio y no el de la ventana principal: este panel también vive
    // en su propia ventana, donde `ApplicationWindow.window.confirm` no existe.
    Dialog {
        id: dlgBorrar
        title: "Borrar el carrusel"
        modal: true
        anchors.centerIn: Overlay.overlay
        standardButtons: Dialog.Yes | Dialog.No
        onAccepted: raiz.cc.borrar(raiz.cc.actual)
        // El ancho lo pone el dialogo y el texto se acomoda adentro. Al reves
        // —texto con ancho fijo adentro de un dialogo que se mide por su
        // contenido— es un bucle de enlace, y Qt lo avisa en cada apertura.
        width: 420
        Label {
            width: dlgBorrar.availableWidth
            wrapMode: Text.WordWrap
            // Se dice qué NO se borra: si no, nadie toca el botón por las dudas.
            text: "Se va «" + raiz.cc.actual + "». Las imágenes quedan donde están: " +
                  "pueden estar en una escena, o ser tuyas."
        }
    }
}
