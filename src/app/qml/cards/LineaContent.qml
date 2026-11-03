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
    readonly property var guionVm: Studio.evento.guionVm
    readonly property bool hayEvento: guionVm.abierto !== ""

    // La línea recalcula cada segundo y pide miniaturas solo si alguien la mira: esta
    // tarjeta es una de las que miran (por vista: puede haber varias).
    readonly property var mirada: [tarjeta.visible && tarjeta.hayEvento, tarjeta.visible && tarjeta.hayEvento && tarjeta.vista === 2]
    Suscripcion { objetivo: Studio.linea; activa: tarjeta.mirada[0] }
    Suscripcion { objetivo: Studio.linea; que: "multipista"; activa: tarjeta.mirada[1] }

    AtajosLinea { activos: tarjeta.enVentana && tarjeta.mirada[0] }

    /// Las vistas (conducción, escaleta, multipista) no gastan una fila de botones: van en el
    /// «⠿» pegado al título de la tarjeta (GlassCard.accionesTitulo). En su ventana propia no
    /// hay título de tarjeta: el mismo botón va arriba a la izquierda del contenido.
    readonly property var nombresVistas: [qsTr("Conducción"), qsTr("Escaleta"), qsTr("Línea")]
    property Component accionesTitulo: Component {
      Row {
        spacing: 14
        GlassButton {
            id: puntos
            compact: true
            anchors.verticalCenter: parent.verticalCenter
            implicitWidth: 30
            implicitHeight: 26
            active: opciones.opened
            // Seis puntos dibujados (2 × 3) y centrados: el «⠿» de la fuente queda corrido.
            Grid {
                anchors.centerIn: parent
                columns: 2
                rowSpacing: 2
                columnSpacing: 3
                Repeater {
                    model: 6
                    Rectangle { width: 3; height: 3; radius: 1.5; color: Theme.text }
                }
            }
            onClicked: opciones.opened ? opciones.close() : opciones.open()
            ToolTip.visible: hovered && !opciones.opened; ToolTip.delay: 500
            ToolTip.text: qsTr("Vista: %1").arg(tarjeta.nombresVistas[tarjeta.vista])
            Popup {
                id: opciones
                y: parent.height + 6
                padding: 8
                background: Rectangle { color: Theme.panel; radius: Theme.radiusSmall; border.color: Theme.glassBorder }
                ColumnLayout {
                    spacing: 4
                    Repeater {
                        model: tarjeta.nombresVistas
                        delegate: GlassButton {
                            required property string modelData
                            required property int index
                            Layout.fillWidth: true
                            compact: true
                            text: modelData
                            active: tarjeta.vista === index
                            onClicked: { tarjeta.vista = index; opciones.close() }
                        }
                    }
                }
            }
        }
        // Lo de la vista que se usa siempre, en la misma línea del título: el zoom de la multipista.
        Loader {
            anchors.verticalCenter: parent.verticalCenter
            active: tarjeta.vista === 2 && multipista.barraZoom !== null
            sourceComponent: multipista.barraZoom
        }
      }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 6
        visible: tarjeta.hayEvento

        // Solo en su ventana propia (en el tablero va pegado al título de la tarjeta).
        Loader {
            active: tarjeta.enVentana
            visible: active
            sourceComponent: tarjeta.accionesTitulo
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
                                ? ApplicationWindow.window.showPage((Studio.riel.paginaPorId["guion"] ?? -1)) : undefined
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
            id: multipista
            visible: tarjeta.vista === 2
            // En el tablero el zoom va en la línea del título (accionesTitulo); en la ventana, acá.
            zoomAfuera: !tarjeta.enVentana
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
