// Salidas → Pantallas → «Pantallas conectadas»: los monitores de esta máquina,
// dibujados como están puestos, y qué va en cada uno.
//
// A la izquierda el dibujo de la disposición (sale de la geometría que da el
// sistema: si el de la derecha está más arriba, se lo ve más arriba); a la
// derecha la lista, en el mismo orden físico. Un clic en el dibujo elige el
// monitor en la lista.
//
// Por monitor se elige UNA cosa del catálogo, o varias que van en grilla, y si
// va a pantalla completa. Se recuerda por monitor (Studio.pantallasConectadas):
// uno que no está queda «esperando» y su contenido no se manda a otro.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import BA

Panel {
    id: sec
    title: qsTr("Pantallas conectadas")
    readonly property var m: Studio.pantallasConectadas
    property string elegido: ""

    // Lo que dice cada monitor, arriba a la derecha del título.
    Text {
        parent: sec
        anchors { right: parent.right; top: parent.top; rightMargin: 18; topMargin: 16 }
        text: qsTr("%n monitor", "", sec.m.conectados) +
              (sec.m.esperando > 0 ? " · " + qsTr("%n esperando que aparezca", "", sec.m.esperando) : "")
        color: sec.m.esperando > 0 ? Theme.warn : Theme.textDim
        font.pixelSize: 11
    }

    RowLayout {
        anchors.fill: parent
        spacing: 14

        // --- El dibujo de la disposición -----------------------------------
        Item {
            id: lienzo
            Layout.preferredWidth: 300
            Layout.fillHeight: true
            // El rectángulo de todos, con la proporción real, centrado.
            readonly property real prop: Math.max(0.2, sec.m.proporcion)
            readonly property real aw: Math.min(width, height * prop)
            readonly property real ah: aw / prop
            readonly property real ax: (width - aw) / 2
            readonly property real ay: (height - ah) / 2
            Repeater {
                model: sec.m
                delegate: Rectangle {
                    id: caja
                    required property string monitorId
                    required property int orden
                    required property string nombre
                    required property bool conectado
                    required property rect dibujo
                    required property string titulo
                    required property bool principal
                    visible: conectado
                    x: lienzo.ax + dibujo.x * lienzo.aw + 2
                    y: lienzo.ay + dibujo.y * lienzo.ah + 2
                    width: Math.max(8, dibujo.width * lienzo.aw - 4)
                    height: Math.max(8, dibujo.height * lienzo.ah - 4)
                    radius: 6
                    color: titulo !== "" ? Qt.rgba(1, 0.42, 0.17, 0.18) : Qt.rgba(1, 1, 1, 0.06)
                    border.color: sec.elegido === monitorId ? Theme.accent : Qt.rgba(1, 1, 1, 0.22)
                    border.width: sec.elegido === monitorId ? 2 : 1
                    Column {
                        anchors.centerIn: parent
                        width: parent.width - 10
                        spacing: 2
                        Text {
                            width: parent.width; horizontalAlignment: Text.AlignHCenter
                            text: caja.orden + (caja.principal ? " ★" : "")
                            color: Theme.text; font { pixelSize: 18; weight: Font.DemiBold }
                        }
                        Text {
                            width: parent.width; horizontalAlignment: Text.AlignHCenter
                            text: caja.titulo !== "" ? caja.titulo : caja.nombre
                            color: caja.titulo !== "" ? Theme.accentSoft : Theme.textFaint
                            font.pixelSize: 10; elide: Text.ElideRight
                        }
                    }
                    MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: sec.elegido = caja.monitorId }
                }
            }
            GlassButton {
                anchors { left: parent.left; bottom: parent.bottom }
                compact: true; text: qsTr("Identificar"); icon: "⧉"
                onClicked: Studio.escritorio.identificarPantallas()
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Muestra en cada monitor su número y su nombre, unos segundos")
            }
        }

        // --- La lista: qué va en cada uno -----------------------------------
        ListView {
            id: lista
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            spacing: 6
            model: sec.m
            ScrollBar.vertical: ScrollBar {}
            delegate: Rectangle {
                id: fila
                required property string monitorId
                required property int orden
                required property string nombre
                required property bool conectado
                required property bool principal
                required property rect geometria
                required property var cosas
                required property string titulo
                required property bool pantallaCompleta
                required property bool esperando
                width: lista.width
                height: 104
                radius: 10
                color: sec.elegido === monitorId ? Qt.rgba(1, 1, 1, 0.07) : Qt.rgba(1, 1, 1, 0.025)
                border.color: sec.elegido === monitorId ? Theme.accent : "transparent"
                MouseArea { anchors.fill: parent; onClicked: sec.elegido = fila.monitorId }

                Column {
                    anchors { fill: parent; margins: 10 }
                    spacing: 6
                    Row {
                        spacing: 8
                        Text {
                            text: fila.conectado ? String(fila.orden) : "–"
                            color: Theme.text; font { pixelSize: 15; weight: Font.DemiBold }
                        }
                        Text {
                            text: fila.nombre
                            color: Theme.text; font { pixelSize: 13; weight: Font.DemiBold }
                        }
                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            text: fila.conectado
                                  ? fila.geometria.width + "×" + fila.geometria.height + (fila.principal ? " · " + qsTr("principal") : "")
                                  : qsTr("no está: se lo espera (su contenido no va a otro monitor)")
                            color: fila.conectado ? Theme.textFaint : Theme.warn
                            font.pixelSize: 11
                        }
                    }
                    Text {
                        width: parent.width
                        text: fila.titulo !== "" ? qsTr("Muestra: %1").arg(fila.titulo) : qsTr("No muestra nada")
                        color: fila.titulo !== "" ? Theme.accentSoft : Theme.textFaint
                        font.pixelSize: 11; elide: Text.ElideRight
                    }
                    Row {
                        spacing: 6
                        ComboBox {
                            id: que
                            width: Math.min(320, fila.width - 400)
                            model: Studio.catalogo.locales
                            textRole: "etiqueta"
                            valueRole: "itemId"
                            // Arranca en lo que muestra (la primera cosa), si hay.
                            Component.onCompleted: if (fila.cosas.length > 0) currentIndex = Math.max(0, indexOfValue(fila.cosas[0]))
                        }
                        GlassButton {
                            compact: true; text: qsTr("Poner"); active: true
                            enabled: que.currentValue !== undefined && que.currentValue !== ""
                            onClicked: sec.chequear(sec.m.asignar(fila.monitorId, [que.currentValue], fila.pantallaCompleta))
                            ToolTip.visible: hovered; ToolTip.text: qsTr("Que este monitor muestre solo esto")
                        }
                        GlassButton {
                            compact: true; text: qsTr("Sumar"); icon: "+"
                            enabled: fila.cosas.length > 0 && que.currentValue !== undefined && fila.cosas.indexOf(que.currentValue) < 0
                            onClicked: sec.chequear(sec.m.asignar(fila.monitorId, Array.from(fila.cosas).concat([que.currentValue]), fila.pantallaCompleta))
                            ToolTip.visible: hovered; ToolTip.text: qsTr("Agregarlo a lo que ya muestra: van en grilla")
                        }
                        Text { anchors.verticalCenter: parent.verticalCenter; text: qsTr("Pantalla completa"); color: Theme.textDim; font.pixelSize: 11 }
                        Toggle {
                            anchors.verticalCenter: parent.verticalCenter
                            checked: fila.pantallaCompleta
                            onToggled: (quiere) => sec.chequear(sec.m.setPantallaCompleta(fila.monitorId, quiere))
                        }
                        GlassButton {
                            compact: true
                            visible: fila.cosas.length > 0
                            text: fila.conectado ? qsTr("Quitar") : qsTr("Olvidar")
                            onClicked: sec.chequear(fila.conectado ? sec.m.quitar(fila.monitorId) : sec.m.olvidar(fila.monitorId))
                        }
                    }
                }
            }
            Text {
                anchors.centerIn: parent
                visible: lista.count === 0
                text: qsTr("El sistema no informó ningún monitor.")
                color: Theme.textFaint; font.pixelSize: 12
            }
        }
    }

    function chequear(err) { if (err && err !== "") Studio.notify(err, "error") }
}
