// Tarjeta "Fuentes": capas de la escena al aire con su visibilidad.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import BA
import "../controls"

// CON EL TECLADO (un clic en la tarjeta y ya):
//   ↑ ↓          elegir capa
//   Espacio      mostrarla u ocultarla
//
// Ocultar una capa es de las cosas que se piden en voz alta y con apuro —«saca
// el rotulo»— y hacerlo con el mouse es apuntarle a un interruptor de 30 px.
Item {
    id: tarjeta
    focus: true
    property int elegido: -1

    function mover(paso) {
        const n = Studio.sources.rowCount()
        if (n === 0) return
        tarjeta.elegido = tarjeta.elegido < 0 ? 0 : Math.max(0, Math.min(n - 1, tarjeta.elegido + paso))
    }
    function alternar() {
        const c = Studio.sources.capa(tarjeta.elegido)
        if (!c || c.nombre === undefined) return
        Studio.sources.setVisible(c.nombre, !c.visible)
    }

    Keys.onPressed: (e) => {
        switch (e.key) {
            case Qt.Key_Up:    tarjeta.mover(-1); e.accepted = true; break
            case Qt.Key_Down:  tarjeta.mover(1);  e.accepted = true; break
            case Qt.Key_Home:  tarjeta.elegido = 0; e.accepted = true; break
            case Qt.Key_End:   tarjeta.elegido = Studio.sources.rowCount() - 1; e.accepted = true; break
            case Qt.Key_Space: tarjeta.alternar(); e.accepted = true; break
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 6
        Text { text: Studio.currentScene; color: Theme.textFaint; font.pixelSize: 11 }
        ListView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            spacing: 4
            model: Studio.sources
            delegate: Rectangle {
                required property int index
                required property string name
                required property string typeName
                required property bool shown
                width: ListView.view.width
                height: 36
                radius: 10
                color: Qt.rgba(1, 1, 1, shown ? 0.07 : 0.03)
                // La elegida por el teclado se marca: mover un interruptor a
                // ciegas en una escena al aire no es una opcion.
                border.width: tarjeta.activeFocus && index === tarjeta.elegido ? 2 : 0
                border.color: Theme.accent
                RowLayout {
                    anchors { fill: parent; leftMargin: 10; rightMargin: 6 }
                    ColumnLayout {
                        spacing: 0
                        Layout.fillWidth: true
                        Text { text: name; color: shown ? Theme.text : Theme.textFaint; elide: Text.ElideRight; Layout.fillWidth: true; font.pixelSize: 12 }
                        Text { text: typeName; color: Theme.textFaint; elide: Text.ElideRight; Layout.fillWidth: true; font.pixelSize: 9 }
                    }
                    Toggle { checked: shown; onToggled: (wanted) => Studio.sources.setVisible(name, wanted) }
                }
            }
        }
    }
}
