// Lo que se ve donde todavía no hay nada (sin integrantes, sin puntos, sin
// conexiones): qué es esto, qué falta y el botón para hacerlo. Un espacio en
// blanco con una palabra suelta («nadie», «Sin orden del día») no le dice a
// nadie qué hacer.
import QtQuick
import QtQuick.Layouts
import BA

ColumnLayout {
    id: vacio
    property string icono: ""
    property string titulo: ""
    property string texto: ""
    /// Botón principal (vacío = sin botón).
    property string accion: ""
    /// Botón secundario.
    property string accion2: ""
    /// Chico: para una columna angosta (pedidos, hablando).
    property bool chico: false
    signal activado()
    signal activado2()

    spacing: chico ? 4 : 10
    // Una ColumnLayout toma como ancho máximo el de sus hijos (520): con
    // Layout.fillWidth quedaba corrida a la izquierda en vez de centrada.
    Layout.maximumWidth: Number.POSITIVE_INFINITY
    Text {
        Layout.alignment: Qt.AlignHCenter
        visible: vacio.icono !== ""
        text: vacio.icono
        color: Theme.textFaint
        font.pixelSize: vacio.chico ? 20 : 38
    }
    Text {
        Layout.alignment: Qt.AlignHCenter
        Layout.maximumWidth: vacio.chico ? 260 : 520
        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.WordWrap
        visible: vacio.titulo !== ""
        text: vacio.titulo
        color: vacio.chico ? Theme.textDim : Theme.text
        font { pixelSize: vacio.chico ? 12 : 16; weight: Font.DemiBold }
    }
    Text {
        Layout.alignment: Qt.AlignHCenter
        Layout.maximumWidth: vacio.chico ? 260 : 520
        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.WordWrap
        visible: vacio.texto !== ""
        text: vacio.texto
        color: Theme.textFaint
        font.pixelSize: vacio.chico ? 11 : 12
        lineHeight: 1.15
    }
    RowLayout {
        Layout.alignment: Qt.AlignHCenter
        Layout.topMargin: vacio.chico ? 2 : 6
        visible: vacio.accion !== "" || vacio.accion2 !== ""
        spacing: 8
        GlassButton { visible: vacio.accion !== ""; text: vacio.accion; active: true; compact: vacio.chico; onClicked: vacio.activado() }
        GlassButton { visible: vacio.accion2 !== ""; text: vacio.accion2; compact: vacio.chico; onClicked: vacio.activado2() }
    }
}
