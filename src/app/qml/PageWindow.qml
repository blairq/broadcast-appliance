// Una pestaña sacada a su propia ventana, para mandarla a otro monitor.
//
// No se saca de la ventana principal: se abre OTRA VISTA de lo mismo. Todas
// las páginas leen su estado de los `Studio.*`, así que las dos se mantienen
// sincronizadas solas y el operador puede tener el guion en un monitor y las
// escenas en el otro sin perder ninguno de los dos.
//
//   Esc   cierra la ventana
import QtQuick
import QtQuick.Controls
import BA

Window {
    id: w
    required property int page
    required property string titulo
    /// El componente de la pagina (lo arma Main.qml, que es quien los conoce).
    required property Component contenido

    // El nombre con el que la encuentran las herramientas de ventanas
    // (ventana_listar / ventana_mover). Va por `objectName` y no por el
    // titulo: el titulo es para leer y puede cambiar o repetirse.
    objectName: "ba:pagina:" + idPagina
    /// El id corto de la pagina (el mismo de ui_show_page).
    property string idPagina: ""

    title: "Broadcast Appliance — " + titulo
    color: Theme.bg
    width: 1180
    height: 760
    minimumWidth: 720
    minimumHeight: 460
    visible: true

    signal cerrada(int page)
    onClosing: w.cerrada(w.page)

    Loader {
        anchors.fill: parent
        sourceComponent: w.contenido
        // Sin el riel de navegacion al costado, la pagina no necesita dejarle
        // lugar: gana los 92 px que en la ventana principal ocupa el riel.
        onLoaded: if (item && item.leftInset !== undefined) item.leftInset = 14
    }

    Shortcut {
        sequence: StandardKey.Cancel
        onActivated: w.close()
    }
}
