// La pastilla «IA» del encabezado: si alguna parte de la asistencia con IA no anda o
// falta configurarla (el asistente, el modelo local, el dictado, la voz del operador,
// los auriculares…). Un clic abre la lista; cada opción lleva a donde se arregla.
// El modelo es Studio.estadoIA (agent/EstadoIA.hpp), el mismo que ia_estado.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import BA

StatusPill {
    id: p
    readonly property var ia: Studio.estadoIA
    readonly property int errores: ia ? ia.errores : 0
    readonly property int avisos: ia ? ia.avisos : 0
    readonly property int total: errores + avisos
    text: total > 0 ? "IA · " + total : "IA"
    dot: errores > 0 ? Theme.live : avisos > 0 ? Theme.amber : Theme.ok
    on: true
    pulse: errores > 0

    function ir(prob) {
        if (prob.seccion) Studio.seccionConfig = prob.seccion
        ApplicationWindow.window.showPage(Studio.riel.paginaDeId(prob.pagina))
    }

    MouseArea {
        id: ma
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: menu.popup(p, 0, p.height + 4)
    }
    ToolTip.visible: ma.containsMouse && !menu.visible
    ToolTip.delay: 400
    ToolTip.text: p.total === 0 ? "La asistencia con IA está andando entera"
                                : "Inteligencia artificial: " + (p.errores > 0 ? p.errores + " sin andar" : "")
                                  + (p.errores > 0 && p.avisos > 0 ? ", " : "") + (p.avisos > 0 ? p.avisos + " para revisar" : "")
                                  + " (clic: qué y dónde se arregla)"

    Menu {
        id: menu
        width: 420
        MenuItem {
            enabled: false
            contentItem: Text {
                text: p.total === 0 ? "Inteligencia artificial: todo andando" : "Inteligencia artificial: qué falta"
                color: Theme.text; font { pixelSize: 13; weight: Font.DemiBold }
            }
        }
        Repeater {
            model: p.ia ? p.ia.problemas : []
            delegate: MenuItem {
                id: item
                required property var modelData
                width: menu.width
                onTriggered: p.ir(modelData)
                contentItem: RowLayout {
                    spacing: 10
                    Rectangle {
                        Layout.alignment: Qt.AlignTop
                        Layout.topMargin: 5
                        width: 8; height: 8; radius: 4
                        color: item.modelData.gravedad === "error" ? Theme.live : Theme.amber
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 2
                        Text { Layout.fillWidth: true; text: item.modelData.titulo; color: Theme.text
                               font.pixelSize: 13; font.weight: Font.DemiBold; wrapMode: Text.WordWrap }
                        Text { Layout.fillWidth: true; visible: text !== ""; text: item.modelData.detalle
                               color: Theme.textDim; font.pixelSize: 11; wrapMode: Text.WordWrap }
                    }
                    Text { Layout.alignment: Qt.AlignVCenter; text: "›"; color: Theme.textFaint; font.pixelSize: 16 }
                }
            }
        }
        MenuSeparator { visible: p.total > 0 }
        MenuItem {
            visible: p.total > 0
            height: visible ? implicitHeight : 0
            text: "Pedirle al asistente que me guíe"
            enabled: !!Studio.agent && Studio.agent.running
            onTriggered: ApplicationWindow.window.pedirAlAsistente(
                "Ayudame a poner en marcha lo que falta de la inteligencia artificial: mirá ia_estado y seguí la guía puesta-en-marcha, de a un paso.")
        }
    }
}
