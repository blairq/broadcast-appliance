// Cualquier tarjeta del tablero en su propia ventana (VentanaSuelta con id
// «tarjeta:<kind>»): la misma tarjeta, otra vista de lo mismo. La abre la barra
// de tarjetas del Dashboard (doble clic, o «Ventana» desplegada).
import QtQuick
import BA

Item {
    id: tv
    /// Lo pone VentanaSuelta: «tarjeta:<kind>».
    property string panelId: ""
    property bool enVentana: false
    readonly property string kind: panelId.startsWith("tarjeta:") ? panelId.slice(8) : ""
    readonly property int anchoVentana: 560
    readonly property int altoVentana: 520

    Rectangle { anchors.fill: parent; color: Theme.bg }
    Loader {
        id: contenido
        anchors { fill: parent; margins: 12 }
        focus: true
        source: tv.kind === "" ? "" : "../cards/" + tv.kind.charAt(0).toUpperCase() + tv.kind.slice(1) + "Content.qml"
        onLoaded: {
            if (item.enVentana !== undefined) item.enVentana = true
            // El monitor, con el programa (en el tablero lo elige cada tarjeta).
            if (tv.kind === "monitor" && item.source !== undefined) item.source = "program"
        }
    }
    Text {
        visible: contenido.status === Loader.Error
        anchors.centerIn: parent
        text: qsTr("Esta tarjeta no se pudo abrir en una ventana.")
        color: Theme.textDim; font.pixelSize: 12
    }
}
