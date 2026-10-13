// Un panel suelto en su propia ventana, para mandarlo a otro monitor.
//
// POR QUE ADEMAS DE PageWindow. Esa saca una PÁGINA entera; esto saca UNA COSA.
// En un evento eso es la diferencia entre tener «Público» en el segundo monitor
// y tener la cola de preguntas en uno y la urna en otro, que es como se trabaja
// cuando hay alguien moderando y alguien conduciendo.
//
// Como con las páginas, no se saca nada de la ventana principal: se abre OTRA
// VISTA de lo mismo. Los paneles leen su estado de los `Studio.*`, así que las
// dos copias muestran lo mismo y se actualizan juntas.
//
//   Esc   cierra la ventana
import QtQuick
import QtQuick.Controls
import QtQuick.Window
import BA
import "controls"

Window {
    id: w
    /// El id del panel ("padron", "preguntas", "votacion").
    required property string panelId
    required property string titulo
    /// El componente del panel (lo arma Main.qml, que es quien los conoce).
    required property Component contenido

    // Como lo encuentran `ventana_listar` y `ventana_mover`: sin esto no se lo
    // puede mandar a un monitor desde el asistente, que es medio sentido de
    // tener ventanas sueltas.
    objectName: "ba:panel:" + panelId

    title: "Broadcast Appliance — " + titulo
    color: Theme.bg

    // Los controles de Qt, en oscuro. Los colores salen de Theme porque esta
    // misma paleta la necesitan las tres ventanas (ver Theme.qml).
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
    // Más chicas que las páginas: un panel no necesita el ancho de una pantalla
    // y así entran dos cómodas en un monitor.
    width: 620
    height: 720
    minimumWidth: 380
    minimumHeight: 320
    visible: true

    signal cerrada(string panelId)
    onClosing: w.cerrada(w.panelId)

    // A un monitor libre, igual que las páginas sueltas: si no, cada panel que
    // se saca nace encima del anterior y hay que acomodarlos de a uno.
    Component.onCompleted: {
        const pantallas = Qt.application.screens
        if (pantallas.length < 2) return
        const ocupados = {}
        const vs = Studio.ventanasAbiertas()
        for (let i = 0; i < vs.length; ++i) ocupados[vs[i]] = true
        for (let k = 0; k < pantallas.length; ++k) {
            if (ocupados[pantallas[k].name]) continue
            w.screen = pantallas[k]
            return
        }
    }

    BarraVentana {
        ventana: w
        z: 100
        alCerrar: () => w.close()
    }

    Panel {
        anchors { fill: parent; margins: 12; topMargin: 44 }
        title: w.titulo
        Loader {
            anchors.fill: parent
            // Ver DashboardPage: un Loader es un ambito de foco y sin esto el
            // panel suelto no responderia al teclado.
            focus: true
            sourceComponent: w.contenido
            // El panel ya está en su ventana: que no ofrezca sacarlo otra vez.
            onLoaded: {
                if (!item) return
                // Los paneles con algo propio (la tarjeta PTZ: «ptz:<entrada>»)
                // lo leen de su id.
                if (item.panelId !== undefined) item.panelId = w.panelId
                if (item.enVentana !== undefined) item.enVentana = true
                // Un panel grande (el Mezclador) pide su tamaño.
                if (item.anchoVentana !== undefined) w.width = item.anchoVentana
                if (item.altoVentana !== undefined) w.height = item.altoVentana
            }
        }
    }

    Shortcut {
        // `sequences`: StandardKey.Cancel tiene varias teclas (Esc y Cmd+.) y con
        // `sequence` Qt avisaba que solo tomaba una.
        sequences: [StandardKey.Cancel]
        onActivated: w.close()
    }
}
