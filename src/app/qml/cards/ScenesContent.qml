// Tarjeta «Escenas»: los DOS BUSES de una mesa de switcher.
//
// Arriba lo que esta AL AIRE. Abajo lo ARMADO, que es lo que va a salir con el
// proximo TAKE. Tocar una escena la ARMA; no la saca al aire.
//
// POR QUE DOS PASOS. Antes, tocar una escena la ponia al aire en el acto. En
// vivo eso es operar a ciegas: si la camara esta tapada, si el invitado se fue,
// si la placa quedo con el nombre anterior, se descubre cuando ya salio. Con el
// bus de preview se arma, se MIRA, y recien ahi se corta.
//
// EL CORTE DIRECTO SIGUE ESTANDO, con doble clic: hay momentos en los que hay
// que salir YA y pedir dos pasos seria peor que el problema que resuelve.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import BA
import "../controls"

Item {
    id: tarjeta

    ColumnLayout {
        anchors.fill: parent
        spacing: 8

        // --- Al aire / armada, de un vistazo ---------------------------------
        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            Rectangle {
                Layout.fillWidth: true
                implicitHeight: 34
                radius: 10
                color: Qt.rgba(1, 0.42, 0.17, 0.85)
                Text {
                    anchors { fill: parent; leftMargin: 10; rightMargin: 10 }
                    verticalAlignment: Text.AlignVCenter
                    text: "AL AIRE · " + (Studio.currentScene || "—")
                    color: Theme.text; elide: Text.ElideRight
                    font { family: Theme.font; pixelSize: 12; weight: Font.DemiBold }
                }
            }
            Rectangle {
                Layout.fillWidth: true
                implicitHeight: 34
                radius: 10
                color: Qt.rgba(1, 1, 1, 0.07)
                border.color: Studio.escenaArmada !== "" ? Theme.ok : Qt.rgba(1, 1, 1, 0.1)
                Text {
                    anchors { fill: parent; leftMargin: 10; rightMargin: 10 }
                    verticalAlignment: Text.AlignVCenter
                    text: "ARMADA · " + (Studio.escenaArmada || "nada")
                    color: Studio.escenaArmada !== "" ? Theme.text : Theme.textFaint
                    elide: Text.ElideRight
                    font { family: Theme.font; pixelSize: 12; weight: Font.DemiBold }
                }
            }
            GlassButton {
                text: "TAKE"; active: true
                enabled: Studio.escenaArmada !== ""
                onClicked: Studio.take()
                ToolTip.visible: hovered
                ToolTip.text: "Saca al aire la escena armada.\nLa que sale queda armada, para volver."
            }
        }

        // --- Las escenas: tocar ARMA, doble clic corta ------------------------
        Flow {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 8
            Repeater {
                model: Studio.scenes
                delegate: Rectangle {
                    required property string name
                    required property bool live
                    required property bool armada
                    height: 44
                    width: Math.max(110, label.implicitWidth + 32)
                    radius: 14
                    color: live ? Qt.rgba(1, 0.42, 0.17, 0.85)
                         : (sma.containsMouse ? Qt.rgba(1, 1, 1, 0.14) : Qt.rgba(1, 1, 1, 0.07))
                    // El borde verde es la marca del bus de preview: se ve sin
                    // competir con el naranja del aire, que manda.
                    border.width: armada ? 2 : 1
                    border.color: armada ? Theme.ok
                                : (live ? Theme.accentSoft : Qt.rgba(1, 1, 1, 0.1))
                    Behavior on color { ColorAnimation { duration: 180 } }
                    Text {
                        id: label
                        anchors.centerIn: parent
                        text: name
                        color: Theme.text
                        font { family: Theme.font; pixelSize: 13
                               weight: (live || armada) ? Font.DemiBold : Font.Normal }
                    }
                    MouseArea {
                        id: sma
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: Studio.armarEscena(name)
                        onDoubleClicked: Studio.takeScene(name) // corte directo
                        ToolTip.visible: containsMouse
                        ToolTip.text: live ? "Está al aire"
                                           : "Clic: armar para el TAKE\nDoble clic: cortar al aire ya"
                        ToolTip.delay: 600
                    }
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Text { text: "Transicion: " + Studio.transitionType; color: Theme.textFaint; font.pixelSize: 11 }
            Item { Layout.fillWidth: true }
            GlassButton { text: "Editar"; compact: true; onClicked: ApplicationWindow.window.showPage(1) }
        }
    }
}
