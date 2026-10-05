// Legislatura: todo lo de una sesión de un cuerpo deliberativo en un lugar.
//
//   Conferencia   la central del recinto (Bosch DCN): micrófonos de banca,
//                 pedidos de palabra, oradores, unidades y conexión
//   Recinto       la grilla de bancas (cámara, al aire, fila de oradores)
//   Orden del día los puntos de la sesión y cuál se trata
//   Votación      quórum y resultados oficiales
//
// El Cuerpo (quién se sienta en cada banca) se edita con el botón de arriba.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import BA
import "../controls"

Rectangle {
    id: page
    color: Theme.bg
    property real leftInset: 92

    ColumnLayout {
        anchors { fill: parent; leftMargin: page.leftInset; margins: 20 }
        spacing: 12
        RowLayout {
            Layout.fillWidth: true
            spacing: 10
            Text { text: "Legislatura" + (Studio.cuerpo.nombre ? " · " + Studio.cuerpo.nombre : ""); color: Theme.text
                   font { pixelSize: 20; weight: Font.DemiBold } }
            Item { Layout.fillWidth: true }
            GlassButton { text: "Cuerpo (" + Studio.cuerpo.integrantes.length + ")…"; onClicked: cuerpo.open() }
        }
        TabBar {
            id: tabs
            Layout.fillWidth: true
            TabButton { text: "Conferencia" }
            TabButton { text: "Recinto" }
            TabButton { text: "Orden del día" }
            TabButton { text: "Votación" }
        }
        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: tabs.currentIndex
            Panel { PanelConferencia { anchors.fill: parent } }
            Panel {
                ScrollView {
                    anchors.fill: parent
                    contentWidth: availableWidth
                    clip: true
                    RecintoGrilla { width: parent.width }
                }
            }
            Panel { PanelOrdenDelDia { anchors.fill: parent } }
            Panel { PanelVotacion { anchors.fill: parent } }
        }
    }

    // ui_show_page {page: "legislatura", seccion: "conferencia" | "recinto" | "orden" | "votacion"}
    Connections {
        target: Studio
        function onMostrarSeccion(pagina, seccion) {
            if (pagina !== "legislatura") return
            const i = ["conferencia", "recinto", "orden", "votacion"].indexOf(seccion.toLowerCase())
            if (i >= 0) tabs.currentIndex = i
        }
    }

    PanelCuerpo { id: cuerpo }
}
