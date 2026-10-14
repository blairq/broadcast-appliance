// Reloj de aire — la tarjeta de extensión de referencia.
//
// Una tarjeta de extensión es un QML suelto en el disco que el dashboard carga
// como si viniera adentro del programa. Lo que hay que saber para escribir una:
//
//   * La raíz tiene que ser UN item que se dibuje (Item, Rectangle, Column...)
//     y que se ajuste solo: el dashboard le da el tamaño, con `anchors.fill`.
//   * `Studio` está disponible, con todo lo que ven las páginas: Studio.streaming,
//     Studio.recording, Studio.currentScene, Studio.cpuPercent, Studio.rotulador,
//     Studio.evento, Studio.registro... Es de solo mirar salvo que llames a sus
//     funciones a propósito.
//   * `Theme` trae los colores y las tipografías del programa. Usalos: una
//     tarjeta con sus propios colores se ve pegada con cinta.
//   * NO importes nada que no esté en el bundle. QtQuick y QtQuick.Layouts sí;
//     QtQuick.Effects sí; Qt5Compat.GraphicalEffects NO existe.
//   * No hagas bucles largos ni esperes al disco: esto corre en el hilo de la
//     interfaz. Un `while(true)` acá congela el programa entero —y entonces el
//     diario de arranque apaga esta tarjeta la próxima vez, que es la red, no
//     el permiso para tirarse.
//
// Si algo falla, el error sale en el Registro con el archivo y la línea.
import QtQuick
import QtQuick.Layouts
import BA

Item {
    id: raiz

    // El reloj de pared. Un Timer y nada más: es lo más barato que hay y no
    // depende de que el programa le avise nada.
    property string hora: ""
    Timer {
        interval: 1000
        running: true
        repeat: true
        triggeredOnStart: true
        onTriggered: raiz.hora = Qt.formatDateTime(new Date(), "HH:mm:ss")
    }

    ColumnLayout {
        anchors.centerIn: parent
        spacing: 2

        Text {
            Layout.alignment: Qt.AlignHCenter
            text: raiz.hora
            color: Theme.text
            font { family: Theme.font; pixelSize: 38; weight: Font.Light }
        }

        Text {
            Layout.alignment: Qt.AlignHCenter
            // `streamTime` ya viene formateado por el programa: no hay que
            // volver a contar el tiempo acá.
            text: Studio.streaming ? "al aire hace " + Studio.streamTime : "fuera del aire"
            color: Studio.streaming ? Theme.live : Theme.textDim
            font.pixelSize: 12
        }

        Text {
            Layout.alignment: Qt.AlignHCenter
            Layout.topMargin: 6
            text: Studio.currentScene
            color: Theme.textFaint
            font.pixelSize: 11
            elide: Text.ElideRight
            Layout.maximumWidth: raiz.width - 20
        }
    }
}
