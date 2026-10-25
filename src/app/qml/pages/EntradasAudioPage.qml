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
    property string error: ""
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
            title: "Agregar una entrada de audio"
            ColumnLayout {
                id: agregarCol
                anchors.fill: parent
                spacing: 8
                Text { Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.textDim; font.pixelSize: 12
                       text: "Micrófonos, líneas e interfaces de audio de esta máquina. Se escuchan en todas las escenas; su procesamiento se arma en la página Audio." }
                RowLayout {
                    Layout.fillWidth: true
                    ComboBox {
                        id: micBox
                        Layout.fillWidth: true
                        model: page.inp.audioDevices
                        textRole: "label"
                        valueRole: "value"
                    }
                    GlassButton { compact: true; text: "↻"; onClicked: page.inp.refresh()
                                  ToolTip.visible: hovered; ToolTip.text: "Buscar dispositivos de nuevo" }
                }
                // El nombre del sistema, si el operador le puso otro: para reconocerlo.
                Text {
                    readonly property var elegido: micBox.currentIndex >= 0 ? page.inp.audioDevices[micBox.currentIndex] : null
                    visible: !!elegido && elegido.propio
                    text: elegido ? "Del sistema: " + elegido.tecnico : ""
                    Layout.fillWidth: true; elide: Text.ElideRight
                    color: Theme.textFaint; font.pixelSize: 11
                }
                Text { visible: page.inp.audioDevices.length === 0; text: "No se encontraron entradas de audio."; color: Theme.textFaint; font.pixelSize: 12 }
                RowLayout {
                    Layout.fillWidth: true
                    TextField { id: micName; Layout.fillWidth: true; placeholderText: "Nombre (opcional: si no, el del dispositivo)" }
                    GlassButton {
                        text: "Agregar"; active: true; enabled: micBox.currentIndex >= 0
                        onClicked: {
                            page.error = page.inp.addAudioInput(micBox.currentValue, micName.text)
                            if (page.error === "") micName.text = ""
                        }
                    }
                }
                Text { visible: page.error !== ""; text: page.error; color: Theme.live; font.pixelSize: 12; Layout.fillWidth: true; wrapMode: Text.WordWrap }
                LinkDispositivos { Layout.fillWidth: true }
            }
        }

        // --- Las que hay ---------------------------------------------------------------------------
        Panel {
            Layout.fillWidth: true
            Layout.fillHeight: true
            title: "Entradas de audio (" + page.inp.audioInputs.length + ")"
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
                    titulo: "Todavía no hay entradas de audio"
                    texto: "Elegí un micrófono o una entrada de la máquina a la izquierda y tocá «Agregar»."
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
                                text: fila.ausente ? "● «" + fila.modelData.where + "» no está conectado: se lo espera y vuelve solo"
                                                   : "De: " + fila.modelData.where
                                color: fila.ausente ? Theme.warn : Theme.textFaint
                                font.pixelSize: 11; elide: Text.ElideRight
                                ToolTip.visible: hDisp.hovered && !!fila.modelData.tecnico; ToolTip.text: fila.modelData.tecnico || ""
                                HoverHandler { id: hDisp }
                            }
                        }
                        StatusPill { text: fila.ausente ? "AUSENTE" : "AUDIO"; dot: fila.ausente ? Theme.live : Theme.ok }
                        GlassButton {
                            compact: true; text: "Procesamiento →"
                            onClicked: ApplicationWindow.window.showPage(Studio.riel.paginaDeId("audio"))
                            ToolTip.visible: hovered; ToolTip.delay: 400; ToolTip.text: "Sus módulos (ecualizador, compresor…) en la página Audio"
                        }
                        GlassButton {
                            compact: true; text: "Quitar"
                            onClicked: {
                                // La lista se rehace y este delegate puede morir con el diálogo abierto.
                                const inp = page.inp
                                const nombre = fila.modelData.name
                                ApplicationWindow.window.confirm({
                                    title: "¿Quitar la entrada de audio «" + nombre + "»?",
                                    text: "Deja de sonar en todas las escenas. El dispositivo sigue en la máquina (y con su nombre).",
                                    accept: "Quitar",
                                    reject: "Cancelar",
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
