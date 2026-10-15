// Servicios → Templates: el tema de TODAS las interfaces web de control (dashboard,
// botonera, presidencia, rótulos, corrección). Uno para todas: se elige acá (o en
// Configuración → Apariencia, o con web_tema del MCP) y cambia en cada aparato sin
// recargar. El catálogo y el elegido salen de Studio (control/TemasWeb.hpp).
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import BA

ColumnLayout {
    id: raiz
    spacing: 12

    Text {
        Layout.fillWidth: true
        text: "El estilo de las páginas de control en los aparatos (tablets, celulares, notebooks). Cambia en todos al instante, sin recargar."
        color: Theme.textDim
        font.pixelSize: 13
        wrapMode: Text.WordWrap
    }

    Flow {
        Layout.fillWidth: true
        spacing: 16
        Repeater {
            model: Studio.temasWeb
            delegate: Panel {
                id: tarjeta
                required property var modelData
                readonly property bool elegido: Studio.temaWeb === modelData.id
                width: 300
                padding: 14
                implicitHeight: col.implicitHeight + 2 * padding
                border.color: elegido ? Theme.accent : Theme.glassBorder
                border.width: elegido ? 2 : 1

                ColumnLayout {
                    id: col
                    anchors.fill: parent
                    spacing: 10

                    // La muestra: el fondo, un botón en relieve, uno con el acento y un texto.
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 120
                        radius: 12
                        color: tarjeta.modelData.fondo
                        Row {
                            anchors.centerIn: parent
                            spacing: 14
                            Rectangle {
                                width: 74; height: 44; radius: 12
                                color: tarjeta.modelData.superficie
                                border.color: Qt.rgba(1, 1, 1, 0.12)
                                Text {
                                    anchors.centerIn: parent
                                    text: "Escena"
                                    color: tarjeta.modelData.texto
                                    font { pixelSize: 13; weight: Font.DemiBold }
                                }
                            }
                            Rectangle {
                                width: 74; height: 44; radius: 12
                                color: tarjeta.modelData.acento
                                Text {
                                    anchors.centerIn: parent
                                    text: "Auto"
                                    color: "#ffffff"
                                    font { pixelSize: 13; weight: Font.DemiBold }
                                }
                            }
                            Rectangle {
                                width: 44; height: 44; radius: 22
                                color: tarjeta.modelData.superficie
                                border.color: tarjeta.modelData.acento
                                border.width: 3
                            }
                        }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        Text {
                            Layout.fillWidth: true
                            text: tarjeta.modelData.nombre
                            color: Theme.text
                            font { pixelSize: 15; weight: Font.DemiBold }
                        }
                        StatusPill { visible: tarjeta.elegido; text: "En uso"; dot: Theme.ok; on: true }
                    }
                    Text {
                        Layout.fillWidth: true
                        text: tarjeta.modelData.descripcion
                        color: Theme.textDim
                        font.pixelSize: 12
                        wrapMode: Text.WordWrap
                    }
                    GlassButton {
                        Layout.alignment: Qt.AlignRight
                        compact: true
                        text: tarjeta.elegido ? "Elegido" : "Usar este"
                        enabled: !tarjeta.elegido
                        active: tarjeta.elegido
                        onClicked: Studio.temaWeb = tarjeta.modelData.id
                    }
                }
            }
        }
    }
}
