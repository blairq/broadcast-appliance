// Pildora de estado con punto (AL AIRE, REC, gateway...).
import QtQuick
import BA

Rectangle {
    id: pill
    property string text: ""
    property color dot: Theme.ok
    property bool on: true
    property bool pulse: false
    /// Tamaño de la letra. El que trae alcanza para un rotulo; un numero que
    /// hay que leer de un vistazo desde lejos quiere mas.
    property int pixelSize: 12
    /// Texto con el que se MIDE el ancho: el mas largo que la pildora va a
    /// llegar a mostrar. Vacio = se mide con el propio texto, que es lo normal.
    ///
    /// Se usa cuando el texto cambia de largo solo. El caso que lo pedia: el
    /// porcentaje de CPU pasa de una cifra a dos varias veces por segundo, y la
    /// pildora se agrandaba y achicaba sin parar arrastrando a todo lo que
    /// tenia al lado. Midiendo por «CPU 100%» el ancho ya no depende del valor.
    property string medidaDe: ""

    implicitHeight: 30
    implicitWidth: Math.max(label.implicitWidth, medida.implicitWidth) + 36
    radius: height / 2
    color: on ? Qt.rgba(dot.r, dot.g, dot.b, 0.18) : Qt.rgba(1, 1, 1, 0.06)
    border.color: on ? Qt.rgba(dot.r, dot.g, dot.b, 0.55) : Qt.rgba(1, 1, 1, 0.12)

    Rectangle {
        id: d
        width: 8; height: 8; radius: 4
        anchors { left: parent.left; leftMargin: 12; verticalCenter: parent.verticalCenter }
        color: pill.on ? pill.dot : Theme.textFaint
        SequentialAnimation on opacity {
            // Solo a la vista: una animación infinita oculta mantiene el render a 60 Hz.
            running: pill.on && pill.pulse && pill.visible
            loops: Animation.Infinite
            NumberAnimation { to: 0.25; duration: 650; easing.type: Easing.InOutSine }
            NumberAnimation { to: 1.0; duration: 650; easing.type: Easing.InOutSine }
        }
    }
    Text {
        id: label
        anchors { left: d.right; leftMargin: 8; verticalCenter: parent.verticalCenter }
        text: pill.text
        color: pill.on ? Theme.text : Theme.textDim
        font { family: Theme.font; pixelSize: pill.pixelSize; weight: Font.DemiBold; letterSpacing: 0.6 }
    }
    // Solo para medir: nunca se dibuja.
    Text {
        id: medida
        visible: false
        text: pill.medidaDe
        font: label.font
    }
}
