// Página Entradas de audio: los micrófonos, líneas e interfaces de audio de la
// máquina que usa el evento. Se eligen del registro de la máquina («Dispositivos
// y equipos»), con el nombre que les puso el operador; cada entrada dice de qué
// dispositivo sale y si está conectado (uno ausente se espera: vuelve solo, como
// después de un corte de luz). Su procesamiento (módulos, mezcla) es la página
// Audio; acá, solo qué entra.
// Todo sale de Studio.inputs (InputsViewModel), el mismo modelo que la de video.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import BA
import "../controls"

Rectangle {
    id: page
    color: Theme.bg
    property real leftInset: 92
    readonly property var inp: Studio.inputs
    // Al entrar se busca de nuevo (lo enchufado recién aparece; igual el registro
    // se entera solo cuando el sistema avisa).
    onVisibleChanged: if (visible) inp.refresh()
    Component.onCompleted: if (visible) inp.refresh()

    RowLayout {
        anchors { fill: parent; leftMargin: page.leftInset; margins: 20 }
        spacing: 16

        // --- Agregar ------------------------------------------------------------------------------
        Panel {
            Layout.preferredWidth: 420
            Layout.alignment: Qt.AlignTop
            Layout.preferredHeight: agregarCol.implicitHeight + 64
            title: qsTr("Agregar una entrada de audio")
            // El mismo componente que el botón «+ Entrada de audio» de la página Audio.
            AgregarEntradaAudio {
                id: agregarCol
                anchors.fill: parent
            }
        }

        // --- Las que hay ---------------------------------------------------------------------------
        Panel {
            Layout.fillWidth: true
            Layout.fillHeight: true
            title: qsTr("Entradas de audio (%1)").arg(page.inp.audioInputs.length)
            ListView {
                id: lista
                anchors.fill: parent
                clip: true
                spacing: 6
                model: page.inp.audioInputs
                EstadoVacio {
                    anchors.centerIn: parent
                    width: Math.min(parent.width - 40, 520)
                    visible: lista.count === 0
                    icono: "🎙"
                    titulo: qsTr("Todavía no hay entradas de audio")
                    texto: qsTr("Elegí un micrófono o una entrada de la máquina a la izquierda y tocá «Agregar».")
                }
                delegate: Rectangle {
                    id: fila
                    required property var modelData
                    readonly property bool ausente: modelData.estado === "ausente"
                    width: lista.width
                    height: filaCol.implicitHeight + 16
                    radius: 10
                    color: Qt.rgba(1, 1, 1, 0.04)
                    border.color: ausente ? Qt.rgba(1, 0.23, 0.19, 0.5) : "transparent"
                    RowLayout {
                        id: filaCol
                        anchors { left: parent.left; right: parent.right; verticalCenter: parent.verticalCenter; margins: 10 }
                        spacing: 10
                        Text { text: "🎙"; font.pixelSize: 22; color: Theme.textFaint }
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 2
                            Text { Layout.fillWidth: true; text: fila.modelData.name; color: Theme.text; elide: Text.ElideRight
                                   font { pixelSize: 13; weight: Font.DemiBold } }
                            Text {
                                Layout.fillWidth: true
                                text: fila.ausente ? qsTr("● «%1» no está conectado: se lo espera y vuelve solo").arg(fila.modelData.where)
                                                   : qsTr("De: %1").arg(fila.modelData.where)
                                color: fila.ausente ? Theme.warn : Theme.textFaint
                                font.pixelSize: 11; elide: Text.ElideRight
                                ToolTip.visible: hDisp.hovered && !!fila.modelData.tecnico; ToolTip.text: fila.modelData.tecnico || ""
                                HoverHandler { id: hDisp }
                            }
                        }
                        StatusPill { text: fila.ausente ? qsTr("AUSENTE") : qsTr("AUDIO"); dot: fila.ausente ? Theme.live : Theme.ok }
                        GlassButton {
                            compact: true; text: qsTr("Procesamiento →")
                            onClicked: ApplicationWindow.window.showPage(Studio.riel.paginaDeId("audio"))
                            ToolTip.visible: hovered; ToolTip.delay: 400; ToolTip.text: qsTr("Sus módulos (ecualizador, compresor…) en la página Audio")
                        }
                        GlassButton {
                            compact: true; text: qsTr("Quitar")
                            onClicked: {
                                // La lista se rehace y este delegate puede morir con el diálogo abierto.
                                const inp = page.inp
                                const nombre = fila.modelData.name
                                ApplicationWindow.window.confirm({
                                    title: qsTr("¿Quitar la entrada de audio «%1»?").arg(nombre),
                                    text: qsTr("Deja de sonar en todas las escenas. El dispositivo sigue en la máquina (y con su nombre)."),
                                    accept: qsTr("Quitar"),
                                    reject: qsTr("Cancelar"),
                                    action: function() { inp.remove(nombre) }
                                })
                            }
                        }
                    }
                }
            }
        }
    }
}
