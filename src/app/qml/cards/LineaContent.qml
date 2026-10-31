// Tarjeta «Línea»: la línea de tiempo del evento en el tablero (y en su ventana, con
// doble clic en la barra de tarjetas). Las mismas vistas de la página Evento → Guion:
// la conducción (al aire, el próximo, GO/HOLD), la escaleta y la multipista. Todas leen
// Studio.linea; esta tarjeta solo elige cuál se ve.
//
// Los atajos de la línea (Espacio/F12, H, F9/F10…) valen solo en su ventana: en el
// tablero las teclas son de las otras tarjetas.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import BA
import "../controls"

Item {
    id: tarjeta
    /// Lo pone TarjetaEnVentana cuando vive en su propia ventana.
    property bool enVentana: false
    /// 0 = conducción, 1 = escaleta, 2 = multipista.
    property int vista: 0
    readonly property var ev: Studio.evento
    readonly property bool hayEvento: !!ev && ev.abierto !== ""

    // La línea recalcula cada segundo y pide miniaturas solo si alguien la mira: esta
    // tarjeta es una de las que miran (con su propio nombre, puede haber varias).
    readonly property string quien: "tarjeta:" + Math.random().toString(36).slice(2)
    readonly property var mirada: [tarjeta.visible && tarjeta.hayEvento, tarjeta.visible && tarjeta.hayEvento && tarjeta.vista === 2]
    onMiradaChanged: if (Studio.linea) Studio.linea.mirar(quien, mirada[0], mirada[1])
    Component.onCompleted: miradaChanged()
    Component.onDestruction: if (Studio.linea) Studio.linea.mirar(quien, false, false)

    AtajosLinea { activos: tarjeta.enVentana && tarjeta.mirada[0] }

    ColumnLayout {
        anchors.fill: parent
        spacing: 6
        visible: tarjeta.hayEvento

        RowLayout {
            Layout.fillWidth: true
            spacing: 6
            Repeater {
                model: [qsTr("Conducción"), qsTr("Escaleta"), qsTr("Línea")]
                delegate: GlassButton {
                    required property string modelData
                    required property int index
                    compact: true
                    text: modelData
                    active: tarjeta.vista === index
                    onClicked: tarjeta.vista = index
                }
            }
            Item { Layout.fillWidth: true }
        }

        ScrollView {
            id: conduccionScroll
            visible: tarjeta.vista === 0
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            contentWidth: availableWidth
            ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
            LineaConduccion {
                width: conduccionScroll.availableWidth
                // El informe de tiempos es de la página del evento.
                onPedirInforme: ApplicationWindow.window && ApplicationWindow.window.showPage
                                ? ApplicationWindow.window.showPage(Studio.riel.paginaDeId("guion")) : undefined
            }
        }
        LineaEscaleta {
            visible: tarjeta.vista === 1
            Layout.fillWidth: true
            Layout.fillHeight: true
        }
        LineaBarra {
            visible: tarjeta.vista === 1
            Layout.fillWidth: true
        }
        LineaMultipista {
            visible: tarjeta.vista === 2
            activa: tarjeta.mirada[1]
            Layout.fillWidth: true
            Layout.fillHeight: true
        }
    }

    Text {
        visible: !tarjeta.hayEvento
        anchors.centerIn: parent
        width: parent.width - 24
        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.WordWrap
        text: qsTr("No hay un evento abierto: la línea es la del guion del evento.")
        color: Theme.textDim
        font.pixelSize: 12
    }
}
