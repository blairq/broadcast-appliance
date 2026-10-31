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
    readonly property var placa: Studio.placa
    // Lo que sale de la salida que maneja la interfaz (con una sola, la de siempre).
    readonly property var aire: Studio.aireActiva
    readonly property bool porOmision: !Studio.salidaActivaDatos.id || !!Studio.salidaActivaDatos.porOmision

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
            GlassButton { compact: true; visible: f.accion !== ""; text: f.accion; active: f.prendida; activeColor: f.tono; onClicked: f.tocar() }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 6

        // Con varias salidas: de cuál son el aire, la armada y el TAKE.
        SelectorSalida { Layout.fillWidth: true }

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
                    Text { text: t.placa.ftb ? qsTr("AL AIRE · EN NEGRO") : t.placa.placa ? qsTr("AL AIRE · CON PLACA") : qsTr("AL AIRE")
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
                    Text { text: qsTr("ARMADA"); color: Qt.rgba(1, 1, 1, 0.75); font { family: Theme.font; pixelSize: 10; weight: Font.DemiBold; letterSpacing: 1 } }
                    Text { Layout.fillWidth: true; text: Studio.escenaArmada || qsTr("nada"); elide: Text.ElideRight
                           color: Studio.escenaArmada !== "" ? Theme.text : Theme.textFaint
                           font { family: Theme.font; pixelSize: 14; weight: Font.DemiBold } }
                }
            }
            GlassButton { text: qsTr("CUT"); enabled: Studio.escenaArmada !== ""; onClicked: Studio.takeCon("cut", 0)
                          ToolTip.visible: hovered; ToolTip.delay: 400; ToolTip.text: qsTr("Corte seco a la armada") }
            GlassButton { text: qsTr("AUTO"); active: true; enabled: Studio.escenaArmada !== ""; onClicked: Studio.take()
                          ToolTip.visible: hovered; ToolTip.delay: 400; ToolTip.text: qsTr("TAKE con la transición") }
        }

        // --- FTB y placa ------------------------------------------------------------
        BotonesSeguridad { Layout.fillWidth: true }

        // --- Transmisión y grabación -----------------------------------------------------
        // La transmisión principal es de la por omisión; las demás salen por sus destinos
        // (se arrancan y se cortan en la página Salidas).
        Fila {
            etiqueta: t.aire.transmitiendo ? qsTr("TRANSMITIENDO") : qsTr("TRANSMISIÓN")
            reloj: t.aire.tiempoTransmision
            detalle: !t.porOmision ? qsTr("por sus destinos (página Salidas)")
                     : t.aire.transmitiendo ? Studio.streamKbps + " kbps" : (Studio.streamServer || qsTr("sin servidor configurado"))
            prendida: !!t.aire.transmitiendo
            tono: Theme.live
            accion: !t.porOmision ? "" : t.aire.transmitiendo ? qsTr("Cortar") : qsTr("Salir al aire")
            onTocar: t.aire.transmitiendo ? ApplicationWindow.window.confirmStopStream() : ApplicationWindow.window.openStreamDialog()
        }
        Fila {
            id: filaGrabacion
            etiqueta: t.aire.grabando ? qsTr("GRABANDO") : qsTr("GRABACIÓN")
            reloj: t.aire.tiempoGrabacion
            prendida: !!t.aire.grabando
            tono: Theme.live
            accion: t.aire.grabando ? qsTr("Detener") : qsTr("Grabar")
            // La de la por omisión con lo suyo de siempre (el aviso del disco); las demás, la de su salida.
            function grabar(si) {
                if (t.porOmision) { if (si !== Studio.recording) Studio.toggleRecording(); return }
                const e = Studio.grabarSalida(Studio.salidaActiva, si)
                if (e !== "") Studio.notify(e, "error")
            }
            onTocar: {
                if (!t.aire.grabando) { filaGrabacion.grabar(true); return }
                ApplicationWindow.window.confirm({
                    title: qsTr("¿Detener la grabación?"),
                    text: t.aire.transmitiendo ? qsTr("El archivo se cierra y lo que siga no queda grabado. La transmisión sigue al aire.")
                                               : qsTr("El archivo se cierra y lo que siga no queda grabado."),
                    accept: qsTr("Detener"), reject: qsTr("Seguir grabando"),
                    action: function() { filaGrabacion.grabar(false) }
                })
            }
        }
        Item { Layout.fillHeight: true }
    }
}
