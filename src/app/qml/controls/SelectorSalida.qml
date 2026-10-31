// Selector de salida (docs/planes/PLAN-SALIDAS.md, etapa 5): cuál de las salidas
// maneja esta interfaz. Cada salida es un operador completo, con su preview, su
// TAKE y su mezcla; lo que muestran y hacen Dirección, Escenas y el Mezclador es
// de la elegida (Studio.salidaActiva: el modelo, no esta vista).
//
// Con una sola salida no aparece: la interfaz se ve como siempre.
//
// Cada pastilla lleva el color de su salida (el mismo del tally y del multiview) y
// dice si esa salida está al aire o grabando, para no cortar a ciegas en otra.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import BA

Flow {
    id: sel
    visible: Studio.variasSalidas
    spacing: 6

    Repeater {
        model: Studio.salidas
        delegate: Rectangle {
            id: pastilla
            // Por `model`: los roles «id» y «color» chocan con los del Rectangle.
            required property var model
            readonly property string salida: model.id
            readonly property string nombre: model.nombre
            readonly property color tono: model.color
            readonly property bool transmitiendo: !!model.transmitiendo
            readonly property bool grabando: !!model.grabando
            readonly property bool elegida: salida === Studio.salidaActiva
            height: 30
            width: fila.implicitWidth + 20
            radius: 15
            color: elegida ? Qt.rgba(1, 1, 1, 0.12) : (ma.containsMouse ? Qt.rgba(1, 1, 1, 0.07) : Qt.rgba(1, 1, 1, 0.03))
            border.color: elegida ? pastilla.tono : Theme.glassBorder
            border.width: elegida ? 2 : 1
            RowLayout {
                id: fila
                anchors.centerIn: parent
                spacing: 6
                Rectangle { width: 10; height: 10; radius: 5; color: pastilla.tono }
                Text {
                    text: pastilla.nombre
                    color: pastilla.elegida ? Theme.text : Theme.textDim
                    font { family: Theme.font; pixelSize: 12; weight: pastilla.elegida ? Font.DemiBold : Font.Normal }
                }
                Text {
                    visible: pastilla.transmitiendo || pastilla.grabando
                    text: pastilla.transmitiendo ? qsTr("AIRE") : qsTr("REC")
                    color: Theme.live
                    font { family: Theme.font; pixelSize: 9; weight: Font.Bold; letterSpacing: 1 }
                }
            }
            MouseArea {
                id: ma
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: Studio.salidaActiva = pastilla.salida
            }
            ToolTip.visible: ma.containsMouse
            ToolTip.delay: 400
            ToolTip.text: pastilla.elegida ? qsTr("Estás manejando «%1»").arg(pastilla.nombre)
                                           : qsTr("Manejar «%1»: su preview, su TAKE y su mezcla").arg(pastilla.nombre)
        }
    }
}
