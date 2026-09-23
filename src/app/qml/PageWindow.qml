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
import QtQuick.Window
import BA
import "controls"

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

    // AL ABRIRSE, A UN MONITOR LIBRE. Sin esto cada pestaña que se saca nace
    // encima de la anterior, en el mismo monitor, y hay que acomodarlas una
    // por una. Se busca uno donde no haya ya una ventana nuestra; si están
    // todos ocupados, se queda donde el escritorio la ponga —amontonar en un
    // orden inventado no es mejor que amontonar—.
    Component.onCompleted: {
        const pantallas = Qt.application.screens
        if (pantallas.length < 2) return
        const ocupados = {}
        const vs = Studio.ventanasAbiertas()
        for (let i = 0; i < vs.length; ++i) ocupados[vs[i]] = true
        for (let k = 0; k < pantallas.length; ++k) {
            if (ocupados[pantallas[k].name]) continue
            w.screen = pantallas[k]
            if (!Studio.puedeUbicarVentanas) w.visibility = Window.FullScreen
            return
        }
    }

    // La misma barra que las ventanas de video: monitor, pantalla completa,
    // cerrar. Antes no tenían ninguna y había que acomodarlas desde el
    // escritorio, que en Wayland encima no se puede.
    BarraVentana {
        ventana: w
        z: 100
        alCerrar: () => w.close()
    }

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
