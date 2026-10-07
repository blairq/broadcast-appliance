// Asamblea: todo lo de una sesión de un cuerpo deliberativo en un lugar.
//
//   Conferencia   la central del recinto (Bosch DCN): micrófonos de banca,
//                 pedidos de palabra, oradores, unidades y conexión
//   Recinto       la grilla de bancas (cámara, al aire, fila de oradores)
//   Orden del día los puntos de la sesión y cuál se trata
//   Votación      quórum y resultados oficiales
//   Conexiones remotas  cómo entra cada conferencista desde el navegador (en la
//                 sala o afuera): usuario, link y PIN, contraseña, datos. A los que
//                 entran se los habilita en el Recinto (su banca muestra la cara).
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
            Text { text: "Asamblea" + (Studio.cuerpo.nombre ? " · " + Studio.cuerpo.nombre : ""); color: Theme.text
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
            TabButton { text: "Conexiones remotas" + (Studio.sesionRemota && Studio.sesionRemota.conectados > 0 ? " (" + Studio.sesionRemota.conectados + ")" : "") }
        }
        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: tabs.currentIndex
            Panel { PanelConferencia { id: conferencia; anchors.fill: parent } }
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
            Panel { PanelConexionesRemotas { anchors.fill: parent; anchors.margins: 12; onIrAlRecinto: tabs.currentIndex = 1 } }
        }
    }

    // ui_show_page {page: "asamblea", seccion: "conferencia[/sala|unidades|votacion|conexion|avanzado]" | "recinto" | "orden" | "votacion" | "conexiones"}
    Connections {
        target: Studio
        function onMostrarSeccion(pagina, seccion) {
            if (pagina !== "asamblea") return
            const partes = seccion.toLowerCase().split("/")
            const i = ["conferencia", "recinto", "orden", "votacion", "conexiones"].indexOf(partes[0])
            if (i >= 0) tabs.currentIndex = i
            if (i === 0 && partes.length > 1) {
                const j = ["sala", "unidades", "votacion", "conexion", "avanzado"].indexOf(partes[1])
                if (j >= 0) conferencia.pestana = j
            }
        }
    }

    PanelCuerpo { id: cuerpo }
}
