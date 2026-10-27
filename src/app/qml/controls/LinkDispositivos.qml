// «Ponerles nombre…»: lleva a «Dispositivos y equipos», donde se nombran y se
// configuran los dispositivos de la máquina. Lo usan las páginas de entradas de
// video y de audio, que eligen de esa lista: el que no encuentra su cámara o no
// la reconoce por el nombre técnico tiene que saber dónde se arregla.
import QtQuick
import QtQuick.Controls
import BA

Text {
    id: link
    property string texto: qsTr("Ponerles nombre a los dispositivos o ver cuáles faltan → Dispositivos y equipos")
    text: texto
    color: hover.hovered ? Theme.accent : Theme.textFaint
    font { pixelSize: 11; underline: hover.hovered }
    wrapMode: Text.WordWrap
    HoverHandler { id: hover; cursorShape: Qt.PointingHandCursor }
    TapHandler { onTapped: ApplicationWindow.window.showPage(Studio.riel.paginaDeId("equipos")) }
}
