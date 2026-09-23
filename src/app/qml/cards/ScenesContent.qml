// Tarjeta «Escenas»: los DOS BUSES de una mesa de switcher, y las escenas
// agrupadas POR CARPETA.
//
// Arriba lo que está AL AIRE. Al lado lo ARMADO, que es lo que va a salir con
// el próximo TAKE. Tocar una escena la ARMA; no la saca al aire.
//
// POR QUE DOS PASOS. Antes, tocar una escena la ponía al aire en el acto. En
// vivo eso es operar a ciegas: si la cámara está tapada, si el invitado se
// fue, si la placa quedó con el nombre anterior, se descubre cuando ya salió.
// Con el bus de preview se arma, se MIRA, y recién ahí se corta.
//
// EL CORTE DIRECTO SIGUE ESTANDO, con doble clic: hay momentos en los que hay
// que salir YA y pedir dos pasos sería peor que el problema que resuelve.
//
// POR CARPETAS. Estaban todas en una sola tira, en el orden del modelo: con
// veinte escenas eso es una pared de botones donde hay que leer uno por uno.
// Agrupadas se buscan igual que en la pestaña Escenas, que es donde el
// operador las acomodó.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import BA
import "../controls"

Item {
    id: tarjeta

    // Se rearma solo cuando cambia la LISTA (se agregó, borró, renombró o
    // cambió de carpeta una escena), no en cada corte: al aire y armada salen
    // de dos propiedades que avisan por su cuenta.
    property var grupos: Studio.escenasPorCarpeta()
    Connections {
        target: Studio
        function onEscenasCambiaron() { tarjeta.grupos = Studio.escenasPorCarpeta() }
    }

    // El botón de una escena, uno solo para todo el árbol.
    Component {
        id: chip
        Rectangle {
            required property var modelData
            readonly property string nombre: modelData
            readonly property bool live: Studio.currentScene === nombre
            readonly property bool armada: Studio.escenaArmada === nombre
            height: 40
            width: Math.max(104, label.implicitWidth + 28)
            radius: 13
            color: live ? Qt.rgba(1, 0.42, 0.17, 0.85)
                 : (sma.containsMouse ? Qt.rgba(1, 1, 1, 0.14) : Qt.rgba(1, 1, 1, 0.07))
            // El borde verde es la marca del bus de preview: se ve sin
            // competir con el naranja del aire, que manda.
            border.width: armada ? 2 : 1
            border.color: armada ? Theme.ok : (live ? Theme.accentSoft : Qt.rgba(1, 1, 1, 0.1))
            Behavior on color { ColorAnimation { duration: 180 } }
            Text {
                id: label
                anchors.centerIn: parent
                text: parent.nombre
                color: Theme.text
                font { family: Theme.font; pixelSize: 13
                       weight: (parent.live || parent.armada) ? Font.DemiBold : Font.Normal }
            }
            MouseArea {
                id: sma
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: Studio.armarEscena(parent.nombre)
                onDoubleClicked: Studio.takeScene(parent.nombre) // corte directo
                ToolTip.visible: containsMouse
                ToolTip.text: parent.live ? "Está al aire"
                                          : "Clic: armar para el TAKE\nDoble clic: cortar al aire ya"
                ToolTip.delay: 600
            }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 8

        // --- Al aire / armada, de un vistazo ---------------------------------
        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            Rectangle {
                Layout.fillWidth: true; Layout.preferredWidth: 1
                implicitHeight: 34
                radius: 10
                color: Qt.rgba(1, 0.42, 0.17, 0.85)
                Text {
                    anchors { fill: parent; leftMargin: 10; rightMargin: 10 }
                    verticalAlignment: Text.AlignVCenter
                    text: "AL AIRE · " + (Studio.currentScene || "—")
                    color: Theme.text; elide: Text.ElideRight
                    font { family: Theme.font; pixelSize: 12; weight: Font.DemiBold }
                }
            }
            Rectangle {
                Layout.fillWidth: true; Layout.preferredWidth: 1
                implicitHeight: 34
                radius: 10
                color: Qt.rgba(1, 1, 1, 0.07)
                border.color: Studio.escenaArmada !== "" ? Theme.ok : Qt.rgba(1, 1, 1, 0.1)
                Text {
                    anchors { fill: parent; leftMargin: 10; rightMargin: 10 }
                    verticalAlignment: Text.AlignVCenter
                    text: "ARMADA · " + (Studio.escenaArmada || "nada")
                    color: Studio.escenaArmada !== "" ? Theme.text : Theme.textFaint
                    elide: Text.ElideRight
                    font { family: Theme.font; pixelSize: 12; weight: Font.DemiBold }
                }
            }
            GlassButton {
                text: "TAKE"; active: true
                enabled: Studio.escenaArmada !== ""
                onClicked: Studio.take()
                ToolTip.visible: hovered
                ToolTip.text: "Saca al aire la escena armada.\nLa que sale queda armada, para volver."
            }
        }

        // --- El árbol de escenas ---------------------------------------------
        ScrollView {
            id: arbol
            Layout.fillWidth: true
            Layout.fillHeight: true
            contentWidth: availableWidth
            clip: true

            ColumnLayout {
                width: arbol.availableWidth
                spacing: 6
                Repeater {
                    model: tarjeta.grupos
                    delegate: ColumnLayout {
                        id: grupo
                        required property var modelData
                        Layout.fillWidth: true
                        spacing: 4
                        // La carpeta vacía son las escenas sueltas: van sin
                        // título, porque un renglón que dice «sin carpeta» no
                        // dice nada y ocupa lugar.
                        Text {
                            visible: grupo.modelData.carpeta !== ""
                            Layout.fillWidth: true
                            text: "▾  " + grupo.modelData.carpeta
                            color: Theme.textDim
                            font { family: Theme.font; pixelSize: 11; weight: Font.DemiBold }
                        }
                        Flow {
                            Layout.fillWidth: true
                            Layout.leftMargin: grupo.modelData.carpeta !== "" ? 12 : 0
                            spacing: 8
                            Repeater { model: grupo.modelData.escenas; delegate: chip }
                        }
                    }
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Text { text: "Transición: " + Studio.transitionType; color: Theme.textFaint; font.pixelSize: 11 }
            Item { Layout.fillWidth: true }
            GlassButton { text: "Editar"; compact: true; onClicked: ApplicationWindow.window.showPage(1) }
        }
    }
}
