// Una vista del tablero web en su propia pestaña del navegador (/control/app/?vista=…): lo que en
// la app es una tarjeta sacada a una ventana. La MISMA QML que la app (src/app/qml/controls), a
// pantalla completa de la pestaña, con el mismo aparato (su token, su salida) que el tablero.
//   ?vista=mezclador   el Mezclador entero (PanelMezclador: tiras, faders, escucha, AFV,
//                      retardos, buses, la matriz y la calibración)
import QtQuick
import QtQuick.Controls
import BA
import "qrc:/qml/controls"

ApplicationWindow {
    id: win
    visible: true
    title: vistaTitulo + " — Broadcast Appliance"
    color: Theme.bg
    font.family: Theme.font
    palette {
        window: Theme.panel
        windowText: Theme.text
        base: Theme.paletaBase
        alternateBase: Theme.paletaAlterna
        text: Theme.text
        button: Theme.paletaBoton
        buttonText: Theme.text
        highlight: Theme.accent
        highlightedText: "#ffffff"
        mid: Theme.paletaMedio
        dark: Theme.paletaOscura
        light: Theme.paletaClara
        placeholderText: Theme.textFaint
        toolTipBase: Theme.paletaBoton
        toolTipText: Theme.text
    }
    readonly property string vista: VistaWeb
    readonly property string vistaTitulo: vista === "mezclador" ? qsTr("Mezclador") : vista

    // Lo que los controles le piden a la ventana (Main.qml en la app).
    function confirm(opts) { confirmDlg.ask(opts) }
    function toast(message, level) { avisos.mostrar(message, level) }

    Connections {
        target: Replica
        function onAviso(texto, nivel) { avisos.mostrar(texto, nivel) }
        function onConfirmar(id, texto) {
            win.confirm({
                title: qsTr("Cambia el aire"),
                text: texto,
                accept: qsTr("Sí, hacelo"),
                reject: qsTr("Cancelar"),
                action: function() { Replica.confirmarLlamada(id) }
            })
        }
    }

    Loader {
        anchors { fill: parent; margins: 8 }
        sourceComponent: win.vista === "mezclador" ? cmpMezclador : cmpNada
    }
    Component { id: cmpMezclador; PanelMezclador { enVentana: true } }
    Component {
        id: cmpNada
        Text { text: qsTr("No hay una vista «%1».").arg(win.vista); color: Theme.textDim; font.pixelSize: 14 }
    }

    ConfirmDialog { id: confirmDlg }
    Column {
        id: avisos
        z: 1000
        anchors { right: parent.right; bottom: parent.bottom; margins: 18 }
        spacing: 8
        function mostrar(texto, nivel) {
            cmpAviso.createObject(avisos, { texto: texto, nivel: nivel || "info" })
        }
    }
    Component {
        id: cmpAviso
        Rectangle {
            property string texto
            property string nivel
            width: Math.min(420, win.width - 36); height: t.implicitHeight + 20
            radius: 6
            color: nivel === "error" ? "#7a2020" : nivel === "warn" ? "#6b5410" : Theme.paletaBoton
            border.color: Qt.rgba(1, 1, 1, 0.15)
            Text { id: t; anchors { fill: parent; margins: 10 } text: parent.texto; wrapMode: Text.WordWrap; color: Theme.text; font.pixelSize: 13 }
            Timer { running: true; interval: 4500; onTriggered: parent.destroy() }
        }
    }
}
