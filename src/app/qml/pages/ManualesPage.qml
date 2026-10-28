// Página Manuales: la biblioteca de manuales de usuario que arma el asistente de evento
// (ui/ManualesViewModel): capturas de cada pantalla en un evento vacío, aparte, y los
// documentos en Word y PDF, en el idioma de la interfaz. Acá se eligen las opciones, se
// manda a generar y se abren los que ya están (la lista se llena sola mientras escribe).
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import BA
import "../controls"

Rectangle {
    id: page
    color: Theme.bg
    property real leftInset: 92
    onVisibleChanged: if (visible && Studio.manuales) Studio.manuales.refrescar()

    ColumnLayout {
        anchors { fill: parent; leftMargin: page.leftInset; margins: 20 }
        spacing: 12
        Text { text: qsTr("Manuales"); color: Theme.text; font { pixelSize: 20; weight: Font.DemiBold } }
        ScrollView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            contentWidth: availableWidth
            ColumnLayout {
                id: man
                width: parent.width
                spacing: 12
                readonly property var m: Studio.manuales
                readonly property var ag: Studio.agent
                readonly property string modeloActual: man.ag ? ((man.ag.current.name || man.ag.provider) + (man.ag.model ? " · " + man.ag.model : "")) : ""

                Text {
                    Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.textDim; font.pixelSize: 12
                    text: qsTr("El asistente de evento arma la biblioteca de manuales de usuario: saca una captura de cada pantalla "
                          + "(en un evento vacío, aparte, sin tocar el tuyo) y escribe los documentos en Word y PDF: el de los "
                          + "asistentes y la voz, el general, los flujos de trabajo y uno por tipo de evento. Tarda bastante y usa "
                          + "mucho del modelo; el programa se reinicia dos veces (al ir al evento de los manuales y al volver).")
                }
                // El modelo: con uno chico los manuales salen incompletos.
                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: recomendacion.implicitHeight + 20
                    radius: 8
                    color: Qt.rgba(1, 0.72, 0.24, 0.10)
                    border.color: Theme.amber
                    Text {
                        id: recomendacion
                        anchors { fill: parent; margins: 10 }
                        wrapMode: Text.WordWrap; color: Theme.text; font.pixelSize: 12
                        text: qsTr("Activá el asistente con un modelo igual o mejor que %1 (para que quede de producción, %2): es una "
                              + "tarea larga y con modelos más chicos los manuales salen incompletos.").arg(man.m.modeloMinimo).arg(man.m.modeloRecomendado)
                              + (man.modeloActual !== "" ? "\n" + qsTr("Ahora el asistente de evento usa: %1.").arg(man.modeloActual) : "")
                    }
                }
                GridLayout {
                    columns: 2
                    columnSpacing: 12; rowSpacing: 8
                    Text { text: qsTr("Idioma"); color: Theme.textDim; font.pixelSize: 12 }
                    Text { text: qsTr("%1 (el de la interfaz)").arg(man.m.nombreIdioma); color: Theme.text; font.pixelSize: 13 }
                    Text { text: qsTr("Alcance"); color: Theme.textDim; font.pixelSize: 12 }
                    ComboBox {
                        implicitWidth: 360
                        model: [ { t: qsTr("Siete flujos de trabajo agrupados"), v: 7 }, { t: qsTr("Un manual por cada tipo de evento (19)"), v: 19 } ]
                        textRole: "t"
                        currentIndex: man.m.alcance === 19 ? 1 : 0
                        onActivated: (i) => man.m.alcance = model[i].v
                    }
                    Text { text: qsTr("PDF"); color: Theme.textDim; font.pixelSize: 12 }
                    CheckBox { text: qsTr("Además del Word, en PDF"); checked: man.m.pdf; onToggled: man.m.pdf = checked }
                    Text { text: qsTr("Capturas"); color: Theme.textDim; font.pixelSize: 12 }
                    CheckBox { text: qsTr("Marcar lo importante en algunas (recuadros)"); checked: man.m.anotar; onToggled: man.m.anotar = checked }
                }
                RowLayout {
                    spacing: 8
                    GlassButton {
                        active: true
                        text: qsTr("Generar los manuales con el asistente")
                        onClicked: {
                            // Primero, que haya un asistente que pueda tomarlo (elegido, instalado,
                            // con su clave y el MCP prendido); si no, a la pestaña Asistente.
                            const falta = man.ag ? man.ag.faltaParaEncargo() : qsTr("El asistente no está disponible.")
                            if (falta !== "") {
                                ApplicationWindow.window.confirm({
                                    title: qsTr("Falta el asistente"),
                                    text: falta + "\n\n" + qsTr("Los manuales los arma el asistente de evento, con un modelo igual o mejor que %1.").arg(man.m.modeloMinimo),
                                    accept: qsTr("Ir al asistente"), reject: qsTr("Cancelar"),
                                    action: () => { ApplicationWindow.window.page = 7 }
                                })
                                return
                            }
                            ApplicationWindow.window.confirm({
                                title: qsTr("¿Generar los manuales?"),
                                text: qsTr("Se lo encargo al asistente de evento (%1). Abre un evento aparte para las capturas, así "
                                      + "que el programa se reinicia; al terminar vuelve al tuyo. No lo hagas en vivo.")
                                      .arg(man.modeloActual !== "" ? man.modeloActual : qsTr("sin modelo elegido"))
                                      + (man.ag.running ? "" : "\n\n" + qsTr("El asistente no está corriendo: lo arranco y se lo paso cuando esté listo.")),
                                accept: qsTr("Generar"), reject: qsTr("Cancelar"),
                                action: () => { const e = man.m.generar(); if (e !== "") Studio.notify(e, "error") }
                            })
                        }
                    }
                    GlassButton { text: qsTr("Abrir la carpeta"); onClicked: man.m.abrirCarpeta() }
                }
                Text {
                    Layout.fillWidth: true; wrapMode: Text.WrapAnywhere; color: Theme.textFaint; font.pixelSize: 11
                    text: qsTr("Se guardan en %1").arg(man.m.carpeta)
                }

                // --- Los que ya están ---------------------------------------------------
                Text {
                    text: man.m.manuales.length > 0 ? qsTr("Manuales generados (%1 capturas)").arg(man.m.capturas) : qsTr("Todavía no hay manuales en este idioma.")
                    color: Theme.text; font { pixelSize: 14; weight: Font.DemiBold }
                }
                Repeater {
                    model: man.m.manuales
                    delegate: RowLayout {
                        required property var modelData
                        Layout.fillWidth: true
                        spacing: 8
                        Text { Layout.fillWidth: true; elide: Text.ElideRight; text: modelData.nombre; color: Theme.text; font.pixelSize: 13 }
                        Text { text: modelData.modificado || ""; color: Theme.textFaint; font.pixelSize: 11 }
                        GlassButton { compact: true; text: "PDF"; visible: modelData.pdf !== ""; onClicked: man.m.abrir(modelData.pdf) }
                        GlassButton { compact: true; text: "Word"; visible: modelData.docx !== ""; onClicked: man.m.abrir(modelData.docx) }
                    }
                }
            }
        }
    }
}
