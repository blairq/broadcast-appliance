// Tarjeta "Aire": todo lo que decide qué sale, en un solo lugar y a un toque.
//
//   - Lo que está al aire y lo armado, con CUT y AUTO (el TAKE).
//   - FTB y placa (BotonesSeguridad: al aire, prenderlos pide confirmación).
//   - Transmisión y grabación con su reloj: arrancar directo, cortar con
//     confirmación (un clic de más cortaba el aire o el respaldo).
//
// Lo mismo está repartido en «Dirección» y «Salidas»; esta es la versión
// compacta para tener siempre a mano.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import BA
import "../controls"

Item {
    id: t
    property var placa: Studio.estadoPlacaUi()
    Connections { target: Studio; function onPlacaChanged() { t.placa = Studio.estadoPlacaUi() } }

    // Un renglón de estado: etiqueta, reloj y el botón.
    component Fila: Rectangle {
        id: f
        property string etiqueta
        property string reloj
        property string detalle
        property bool prendida
        property color tono
        property string accion
        signal tocar()
        Layout.fillWidth: true
        implicitHeight: 44
        radius: 10
        color: prendida ? Qt.rgba(tono.r, tono.g, tono.b, 0.22) : Qt.rgba(1, 1, 1, 0.05)
        border.color: prendida ? Qt.rgba(tono.r, tono.g, tono.b, 0.6) : Qt.rgba(1, 1, 1, 0.08)
        RowLayout {
            anchors { fill: parent; leftMargin: 10; rightMargin: 6 }
            spacing: 8
            Rectangle { id: luz; width: 9; height: 9; radius: 5; color: f.prendida ? f.tono : Qt.rgba(1, 1, 1, 0.2)
                        SequentialAnimation on opacity {
                            running: f.prendida && luz.visible; loops: Animation.Infinite
                            NumberAnimation { to: 0.35; duration: 600 }
                            NumberAnimation { to: 1.0; duration: 600 }
                        } }
            ColumnLayout {
                spacing: 0
                Layout.fillWidth: true
                Text { text: f.etiqueta; color: f.prendida ? f.tono : Theme.textDim
                       font { family: Theme.font; pixelSize: 11; weight: Font.Bold; letterSpacing: 1 } }
                Text { text: f.detalle; visible: text !== ""; color: Theme.textFaint; font.pixelSize: 10
                       elide: Text.ElideRight; Layout.fillWidth: true }
            }
            Text { text: f.reloj; visible: f.prendida; color: Theme.text; font { family: Theme.mono; pixelSize: 16 } }
            GlassButton { compact: true; text: f.accion; active: f.prendida; activeColor: f.tono; onClicked: f.tocar() }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 6

        // --- Al aire / armada / TAKE ------------------------------------------------
        RowLayout {
            Layout.fillWidth: true
            spacing: 6
            Rectangle {
                Layout.fillWidth: true; Layout.preferredWidth: 1
                implicitHeight: 42; radius: 10
                color: t.placa.ftb ? "#000000" : Qt.rgba(1, 0.42, 0.17, 0.85)
                border.color: t.placa.ftb ? Theme.live : "transparent"
                ColumnLayout {
                    anchors { fill: parent; leftMargin: 10; rightMargin: 8; topMargin: 4; bottomMargin: 4 }
                    spacing: 0
                    Text { text: t.placa.ftb ? "AL AIRE · EN NEGRO" : t.placa.placa ? "AL AIRE · CON PLACA" : "AL AIRE"
                           color: Qt.rgba(1, 1, 1, 0.8); font { family: Theme.font; pixelSize: 10; weight: Font.DemiBold; letterSpacing: 1 } }
                    Text { Layout.fillWidth: true; text: Studio.currentScene || "—"; color: Theme.text; elide: Text.ElideRight
                           font { family: Theme.font; pixelSize: 14; weight: Font.DemiBold } }
                }
            }
            Rectangle {
                Layout.fillWidth: true; Layout.preferredWidth: 1
                implicitHeight: 42; radius: 10
                color: Qt.rgba(0.2, 0.75, 0.4, Studio.escenaArmada !== "" ? 0.35 : 0.08)
                ColumnLayout {
                    anchors { fill: parent; leftMargin: 10; rightMargin: 8; topMargin: 4; bottomMargin: 4 }
                    spacing: 0
                    Text { text: "ARMADA"; color: Qt.rgba(1, 1, 1, 0.75); font { family: Theme.font; pixelSize: 10; weight: Font.DemiBold; letterSpacing: 1 } }
                    Text { Layout.fillWidth: true; text: Studio.escenaArmada || "nada"; elide: Text.ElideRight
                           color: Studio.escenaArmada !== "" ? Theme.text : Theme.textFaint
                           font { family: Theme.font; pixelSize: 14; weight: Font.DemiBold } }
                }
            }
            GlassButton { text: "CUT"; enabled: Studio.escenaArmada !== ""; onClicked: Studio.takeCon("cut", 0)
                          ToolTip.visible: hovered; ToolTip.delay: 400; ToolTip.text: "Corte seco a la armada" }
            GlassButton { text: "AUTO"; active: true; enabled: Studio.escenaArmada !== ""; onClicked: Studio.take()
                          ToolTip.visible: hovered; ToolTip.delay: 400; ToolTip.text: "TAKE con la transición" }
        }

        // --- FTB y placa ------------------------------------------------------------
        BotonesSeguridad { Layout.fillWidth: true }

        // --- Transmisión y grabación -----------------------------------------------------
        Fila {
            etiqueta: Studio.streaming ? "TRANSMITIENDO" : "TRANSMISIÓN"
            reloj: Studio.streamTime
            detalle: Studio.streaming ? Studio.streamKbps + " kbps" : (Studio.streamServer || "sin servidor configurado")
            prendida: Studio.streaming
            tono: Theme.live
            accion: Studio.streaming ? "Cortar" : "Salir al aire"
            onTocar: Studio.streaming ? ApplicationWindow.window.confirmStopStream() : ApplicationWindow.window.openStreamDialog()
        }
        Fila {
            etiqueta: Studio.recording ? "GRABANDO" : "GRABACIÓN"
            reloj: Studio.recordTime
            prendida: Studio.recording
            tono: Theme.live
            accion: Studio.recording ? "Detener" : "Grabar"
            onTocar: {
                if (!Studio.recording) { Studio.toggleRecording(); return }
                ApplicationWindow.window.confirm({
                    title: "¿Detener la grabación?",
                    text: "El archivo se cierra y lo que siga no queda grabado." + (Studio.streaming ? " La transmisión sigue al aire." : ""),
                    accept: "Detener", reject: "Seguir grabando",
                    action: function() { if (Studio.recording) Studio.toggleRecording() }
                })
            }
        }
        Item { Layout.fillHeight: true }
    }
}
