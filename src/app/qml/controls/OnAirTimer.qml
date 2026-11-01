// Reloj del orador al aire (modo oradores), flotante sobre cualquier pagina:
// cuanto le queda; en los ultimos 10 s crece y late; pasado cero cuenta en
// negativo ("+m:ss") en rojo. Nunca saca a nadie: solo informa.
import QtQuick
import QtQuick.Layouts
import BA

Rectangle {
    id: timer
    readonly property var conf: Studio.conference
    // Los que hablan, con su reloj (lo da el modelo: sala::OradoresSala::reloj).
    readonly property var speakers: conf.onAir
    signal clicked()

    // El mas apurado manda el color del marco.
    readonly property bool warn: conf.peorEstado === "aviso"
    readonly property bool over: conf.peorEstado === "excedido"

    visible: speakers.length > 0

    implicitWidth: row.implicitWidth + 28
    implicitHeight: row.implicitHeight + 16
    radius: height / 2
    color: over ? Qt.rgba(0.35, 0.05, 0.04, 0.92) : warn ? Qt.rgba(0.32, 0.12, 0.03, 0.92) : Qt.rgba(0.06, 0.06, 0.07, 0.88)
    border.color: over ? Theme.live : warn ? Theme.accent : Theme.ok
    border.width: 2

    RowLayout {
        id: row
        anchors.centerIn: parent
        spacing: 18
        Repeater {
            model: timer.speakers
            delegate: RowLayout {
                required property var modelData
                readonly property string estado: modelData.estadoTiempo
                spacing: 8
                Rectangle { width: 10; height: 10; radius: 5; color: Theme.colorDeEstado(estado, Theme.ok)
                    SequentialAnimation on opacity { loops: Animation.Infinite; running: timer.visible
                        NumberAnimation { to: 0.3; duration: 600 } NumberAnimation { to: 1; duration: 600 } } }
                Text { text: modelData.name; color: Theme.text; font { pixelSize: 14; weight: Font.DemiBold } }
                Text {
                    id: t
                    text: modelData.textoTiempo
                    color: Theme.colorDeEstado(estado, Theme.text)
                    font { pixelSize: estado !== "ok" ? 30 : 22; weight: Font.Black; family: Theme.font }
                    Behavior on font.pixelSize { NumberAnimation { duration: 200 } }
                    SequentialAnimation on scale {
                        loops: Animation.Infinite
                        // Los últimos 10 s y a la vista (pasado de tiempo no late para siempre).
                        running: estado === "aviso" && t.visible
                        NumberAnimation { to: 1.12; duration: 450; easing.type: Easing.InOutQuad }
                        NumberAnimation { to: 1.0; duration: 450; easing.type: Easing.InOutQuad }
                    }
                }
                Text { visible: estado === "excedido"; text: qsTr("se pasó"); color: Theme.live; font.pixelSize: 12 }
            }
        }
    }
    MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: timer.clicked() }
}
