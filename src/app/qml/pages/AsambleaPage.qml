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
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2
                Text { Layout.fillWidth: true; elide: Text.ElideRight; color: Theme.text
                       text: "Asamblea" + (Studio.cuerpo.nombre ? " · " + Studio.cuerpo.nombre : "")
                       font { pixelSize: 20; weight: Font.DemiBold } }
                Text { Layout.fillWidth: true; elide: Text.ElideRight; color: Theme.textFaint; font.pixelSize: 12
                       text: "La sesión del cuerpo: micrófonos de la sala, bancas, orden del día, votaciones, los que entran desde afuera y los debates." }
            }
            GlassButton { text: "Integrantes (" + Studio.cuerpo.integrantes.length + ")…"
                          active: Studio.cuerpo.integrantes.length === 0
                          onClicked: cuerpo.open()
                          ToolTip.visible: hovered; ToolTip.delay: 400
                          ToolTip.text: "Quién se sienta en cada banca: nombre, bloque, foto, cámara y unidad de la central" }
        }
        // De un vistazo cómo está la sesión; cada pastilla lleva a su pestaña.
        Flow {
            Layout.fillWidth: true
            spacing: 8
            component Pastilla: Rectangle {
                id: pas
                property string texto: ""
                property color punto: Theme.textFaint
                property int pestana: 0
                implicitWidth: filaP.implicitWidth + 20
                implicitHeight: 26
                radius: 13
                color: mP.containsMouse ? Qt.rgba(1, 1, 1, 0.09) : Qt.rgba(1, 1, 1, 0.04)
                border.color: Qt.rgba(1, 1, 1, 0.1)
                RowLayout {
                    id: filaP
                    anchors.centerIn: parent
                    spacing: 6
                    Rectangle { implicitWidth: 8; implicitHeight: 8; radius: 4; color: pas.punto }
                    Text { text: pas.texto; color: Theme.textDim; font.pixelSize: 12 }
                }
                MouseArea { id: mP; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                            onClicked: tabs.currentIndex = pas.pestana }
            }
            readonly property var dcn: Studio.dcn
            readonly property var de: dcn ? dcn.estado : ({})
            readonly property var sr: Studio.sesionRemota
            readonly property var od: Studio.ordenDelDia
            Pastilla {
                pestana: 0
                punto: parent.dcn && parent.dcn.conectada ? Theme.ok : Theme.live
                texto: parent.dcn && parent.dcn.conectada
                       ? "Central: " + (parent.de.abiertos || []).length + " micrófono" + ((parent.de.abiertos || []).length === 1 ? "" : "s") + " abierto"
                         + ((parent.de.abiertos || []).length === 1 ? "" : "s") + ((parent.de.pedidos || []).length ? " · " + parent.de.pedidos.length + " pidieron la palabra" : "")
                       : "Central sin conectar"
            }
            Pastilla {
                pestana: 1
                punto: Studio.cuerpo.integrantes.length > 0 ? Theme.ok : Theme.amber
                texto: Studio.cuerpo.integrantes.length > 0
                       ? Studio.cuerpo.integrantes.length + " integrante" + (Studio.cuerpo.integrantes.length === 1 ? "" : "s")
                         + " · " + ((Studio.cuerpo.recinto.bancas || []).length) + " bancas"
                       : "Sin integrantes cargados"
            }
            Pastilla {
                pestana: 2
                punto: parent.od.actual && parent.od.actual.n ? Theme.live : parent.od.puntos.length > 0 ? Theme.ok : Theme.textFaint
                texto: parent.od.actual && parent.od.actual.n ? "En tratamiento: punto " + parent.od.actual.n
                       : parent.od.puntos.length > 0 ? parent.od.puntos.length + " puntos en el orden del día" : "Sin orden del día"
            }
            Pastilla {
                pestana: 4
                punto: parent.sr && parent.sr.conectados > 0 ? Theme.ok : Theme.textFaint
                texto: parent.sr && parent.sr.conectados > 0
                       ? parent.sr.presentes + " remoto" + (parent.sr.presentes === 1 ? "" : "s") + " presente" + (parent.sr.presentes === 1 ? "" : "s")
                         + " de " + parent.sr.conectados + " conectado" + (parent.sr.conectados === 1 ? "" : "s")
                       : "Nadie conectado desde afuera"
            }
            Pastilla {
                pestana: 5
                visible: !!(Studio.debate && Studio.debate.estado.activo)
                punto: Theme.live
                texto: "Debate en curso"
            }
        }
        TabBar {
            id: tabs
            Layout.fillWidth: true
            TabButton { text: "Conferencia" }
            TabButton { text: "Recinto" }
            TabButton { text: "Orden del día" }
            TabButton { text: "Votación" }
            TabButton { text: "Conexiones remotas" + (Studio.sesionRemota && Studio.sesionRemota.conectados > 0 ? " (" + Studio.sesionRemota.conectados + ")" : "") }
            TabButton { text: "Debate" + (Studio.debate && Studio.debate.estado.activo ? " ●" : "") }
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
                    RecintoGrilla { width: parent.width; onPedirCuerpo: cuerpo.open() }
                }
            }
            Panel { PanelOrdenDelDia { anchors.fill: parent } }
            Panel { PanelVotacion { anchors.fill: parent } }
            Panel { PanelConexionesRemotas { anchors.fill: parent; anchors.margins: 12; onIrAlRecinto: tabs.currentIndex = 1 } }
            Panel { ScrollView { anchors.fill: parent; contentWidth: availableWidth; clip: true
                                 PanelDebate { width: parent.width - 24; x: 12; y: 12 } } }
        }
    }

    // ui_show_page {page: "asamblea", seccion: "conferencia[/sala|unidades|votacion|conexion|avanzado]" | "recinto" | "orden" | "votacion" | "conexiones" | "debate"}
    Connections {
        target: Studio
        function onMostrarSeccion(pagina, seccion) {
            if (pagina !== "asamblea") return
            const partes = seccion.toLowerCase().split("/")
            const i = ["conferencia", "recinto", "orden", "votacion", "conexiones", "debate"].indexOf(partes[0])
            if (i >= 0) tabs.currentIndex = i
            if (i === 0 && partes.length > 1) {
                const j = ["sala", "unidades", "votacion", "conexion", "avanzado"].indexOf(partes[1])
                if (j >= 0) conferencia.pestana = j
            }
        }
    }

    PanelCuerpo { id: cuerpo }
}
