// Una capa SOBRE la terminal: tapa lo que haya debajo y explica por qué no se
// puede usar todavía (falta instalar, está detenido, falta la clave).
//
// POR QUE EXISTE. Estos carteles eran columnas sueltas centradas sobre la
// terminal, sin fondo ni borde. Lo que había abajo se leía a través del texto
// del cartel, y como al cambiar de agente la pantalla del anterior seguía ahí,
// parecía que el cambio no había pasado: se veía «Iniciar el asistente» encima
// de una sesión de Claude Code viva.
//
// Tapa de verdad, y además SE COME LOS CLICS: debajo hay una terminal que toma
// el foco del teclado. Sin eso, un clic al lado del botón devolvía el foco a
// una terminal que el cartel dice que no está andando.
import QtQuick
import QtQuick.Layouts
import BA

// NO se ancla sola: quien la usa decide hasta dónde llega. Tapando el panel
// entero se comía la barra de herramientas —y ahí está el botón de arrancar,
// justo lo que el cartel te pide que hagas—.
Item {
    id: capa
    default property alias contenido: caja.data
    property int anchoMax: 560

    // Atenúa lo de atrás en vez de ocultarlo: que se note que hay una sesión
    // debajo, sin que compita con lo que hay que leer.
    Rectangle { anchors.fill: parent; color: Qt.rgba(0.02, 0.03, 0.05, 0.86) }

    // Los eventos mueren acá.
    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.AllButtons
        onWheel: (w) => { w.accepted = true }
    }

    Rectangle {
        anchors.centerIn: parent
        width: Math.min(capa.width - 48, capa.anchoMax)
        height: caja.implicitHeight + 48
        radius: Theme.radius
        color: Qt.rgba(0.09, 0.10, 0.13, 1.0) // OPACO a propósito
        border.color: Theme.glassBorder
        ColumnLayout {
            id: caja
            anchors { fill: parent; margins: 24 }
            spacing: 12
        }
    }
}
