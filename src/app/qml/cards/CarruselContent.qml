// Tarjeta «Carrusel»: el control, en vivo.
//
// EL CASO, otra vez: el orador dice «volvé a la del gráfico de barras». Con un
// botón de siguiente eso son veinte clics. Acá se ve la tira entera y se toca
// la que hay que poner.
//
// POR QUE EN PERSPECTIVA Y NO UNA GRILLA. No es decoración: en una grilla
// todas las tarjetas pesan lo mismo y hay que LEERLAS. Acá la del centro es la
// que está puesta —grande, derecha, al frente— y las de al lado se van
// acostando hacia el fondo. El ojo encuentra el centro sin buscarlo, y el
// movimiento al girar dice para qué lado se va sin ningún cartel.
//
// EL CUADRO DE BUSQUEDA filtra sobre el título y sobre lo que el que mira
// imágenes dijo de cada lámina. Con cincuenta láminas es la diferencia entre
// encontrarla y pasarlas de a una.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import BA
import "../controls"

Item {
    id: tarjeta

    readonly property var cc: Studio.carruseles
    readonly property var laminas: cc.laminas

    ColumnLayout {
        anchors.fill: parent
        spacing: 6

        // --- Qué carrusel, y buscar adentro ----------------------------------
        RowLayout {
            Layout.fillWidth: true
            spacing: 6

            ComboBox {
                id: cual
                Layout.preferredWidth: 150
                model: tarjeta.cc.carruseles
                textRole: "nombre"
                // Sigue al que este abierto, venga de donde venga: de la
                // pestaña Medios, del asistente o de esta misma tarjeta.
                currentIndex: {
                    const l = tarjeta.cc.carruseles
                    for (let i = 0; i < l.length; i++)
                        if (l[i].nombre === tarjeta.cc.actual) return i
                    return -1
                }
                onActivated: tarjeta.cc.actual = tarjeta.cc.carruseles[currentIndex].nombre
            }
            TextField {
                id: busca
                Layout.fillWidth: true
                color: Theme.text
                placeholderText: "Buscar lámina…"
                onTextChanged: tarjeta.cc.consulta = text
                Keys.onEscapePressed: text = ""
            }
        }

        // --- La tira ----------------------------------------------------------
        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            PathView {
                id: tira
                anchors.fill: parent
                model: tarjeta.laminas
                // Tres a la vista y las demás plegadas: más que eso en una
                // tarjeta de dashboard son estampillas ilegibles.
                pathItemCount: 5
                preferredHighlightBegin: 0.5
                preferredHighlightEnd: 0.5
                highlightRangeMode: PathView.StrictlyEnforceRange
                clip: true
                // Sin rebote en los extremos: la tira da la vuelta, igual que
                // `siguiente` en el modelo.
                snapMode: PathView.SnapToItem

                path: Path {
                    startX: 0
                    startY: tira.height / 2
                    PathAttribute { name: "z"; value: 0 }
                    PathAttribute { name: "giro"; value: 62 }
                    PathAttribute { name: "escala"; value: 0.72 }
                    PathLine { x: tira.width * 0.38; y: tira.height / 2 }
                    PathAttribute { name: "z"; value: 1 }
                    PathAttribute { name: "giro"; value: 32 }
                    PathAttribute { name: "escala"; value: 0.84 }
                    PathLine { x: tira.width / 2; y: tira.height / 2 }
                    PathAttribute { name: "z"; value: 2 }
                    PathAttribute { name: "giro"; value: 0 }
                    PathAttribute { name: "escala"; value: 1.0 }
                    PathLine { x: tira.width * 0.62; y: tira.height / 2 }
                    PathAttribute { name: "z"; value: 1 }
                    PathAttribute { name: "giro"; value: -32 }
                    PathAttribute { name: "escala"; value: 0.84 }
                    PathLine { x: tira.width; y: tira.height / 2 }
                    PathAttribute { name: "z"; value: 0 }
                    PathAttribute { name: "giro"; value: -62 }
                    PathAttribute { name: "escala"; value: 0.72 }
                }

                delegate: Item {
                    id: hoja
                    required property var modelData
                    // PathView entrega los atributos del camino como
                    // propiedades adjuntas; sólo existen mientras el ítem vive
                    // dentro de la vista, de ahí el `?? `.
                    readonly property real giro: PathView.giro ?? 0
                    readonly property real escala: PathView.escala ?? 1
                    z: PathView.z ?? 0
                    width: tira.width * 0.36
                    height: tira.height * 0.9
                    scale: escala

                    transform: Rotation {
                        origin { x: hoja.width / 2; y: hoja.height / 2 }
                        axis { x: 0; y: 1; z: 0 }
                        angle: hoja.giro
                        // La perspectiva es lo que hace que el giro se lea como
                        // profundidad y no como un achatamiento raro.
                        Behavior on angle { NumberAnimation { duration: 180 } }
                    }

                    Rectangle {
                        anchors.fill: parent
                        anchors.margins: 4
                        radius: 8
                        color: "#0b0e12"
                        border.width: hoja.modelData.actual ? 2 : 1
                        border.color: hoja.modelData.actual ? Theme.accent : Theme.glassBorder
                        clip: true

                        Image {
                            anchors { fill: parent; margins: 3 }
                            source: hoja.modelData.url
                            fillMode: Image.PreserveAspectFit
                            asynchronous: true
                            sourceSize.width: 420
                        }

                        Rectangle {
                            anchors { left: parent.left; bottom: parent.bottom; margins: 5 }
                            width: rot.implicitWidth + 12
                            height: 20
                            radius: 10
                            color: Qt.rgba(0, 0, 0, 0.66)
                            Text {
                                id: rot
                                anchors.centerIn: parent
                                text: (hoja.modelData.indice + 1) + " · " + hoja.modelData.titulo
                                color: Theme.text
                                font.pixelSize: 10
                                elide: Text.ElideRight
                            }
                        }

                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            // Un toque la pone. Si no es la del centro, además
                            // la trae al centro: tocar «la de al lado» y que
                            // salga al aire sin moverse desorienta.
                            onClicked: {
                                tarjeta.cc.ir(hoja.modelData.indice)
                                tira.currentIndex = index
                            }
                        }
                    }
                }
            }

            Text {
                anchors.centerIn: parent
                visible: tira.count === 0
                width: parent.width - 40
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
                color: Theme.textFaint
                font.pixelSize: 12
                text: tarjeta.cc.actual === ""
                      ? "Elegí un carrusel arriba.\nSe arman en Medios."
                      : busca.text !== "" ? "Ninguna lámina coincide."
                                          : "Este carrusel está vacío."
            }
        }

        // --- Los botones de siempre -------------------------------------------
        RowLayout {
            Layout.fillWidth: true
            spacing: 6
            GlassButton {
                compact: true; text: "◀"
                enabled: tira.count > 0
                onClicked: tira.decrementCurrentIndex()
            }
            GlassButton {
                compact: true; text: "▶"
                enabled: tira.count > 0
                onClicked: tira.incrementCurrentIndex()
            }
            Item { Layout.fillWidth: true }
            Text {
                visible: tira.count > 0
                text: (tira.currentIndex + 1) + " de " + tira.count
                color: Theme.textFaint
                font.pixelSize: 11
            }
            GlassButton {
                compact: true
                text: "Poner"
                active: true
                enabled: tira.count > 0
                // Girar la tira NO saca nada al aire: mirar para decidir es
                // media operación, y que mirar cambiara lo que se ve sería la
                // peor sorpresa posible con la sala mirando.
                onClicked: tarjeta.cc.ir(tarjeta.laminas[tira.currentIndex].indice)
                ToolTip.visible: hovered
                ToolTip.text: "Pone la lámina del centro. Girar la tira no cambia nada."
            }
        }
    }
}
