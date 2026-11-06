// Página Pruebas (id «pruebas»): todo lo de probar el sistema antes del evento, en un lugar.
//
//   - Prueba de sala: el simulacro con N participantes de prueba (PanelPruebaSala) y, para
//     probar entre dos o más PC, «Generar para otro equipo» (PanelGenerarParaOtro).
//   - Capacidad: ¿cuántos invitados aguanta esta máquina, otra o la conexión? (PanelCapacidad).
//   - Prueba de asamblea: integrantes simulados por la sesión remota (PanelPruebaAsamblea).
//   - Enlace: el túnel, cuánto anda y cuánto necesita el evento (PanelEnlace).
//   - Historial: todas las pruebas de esta máquina, abrir, comparar y exportar (PanelHistorialPruebas).
//
// Solo presentación: los mismos modelos de siempre (Studio.pruebaSala, Studio.capacidad,
// Studio.pruebaAsamblea, Studio.enlace), los que usan las herramientas MCP prueba_sala_*,
// capacidad_* y prueba_asamblea_*. Ver docs/funciones/PRUEBA-SALA.md y CAPACIDAD.md.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import BA
import "../controls"

Rectangle {
    id: page
    color: Theme.bg
    property real leftInset: 92
    /// sala | capacidad | asamblea | enlace | historial
    property string pestana: "sala"

    function irA(id) { ApplicationWindow.window.showPage((Studio.riel.paginaPorId[id] ?? -1)) }
    // ui_show_page {page: "pruebas", seccion: sala|capacidad|asamblea|enlace|historial}.
    Connections {
        target: Studio
        function onMostrarSeccion(pagina, seccion) {
            if (pagina === "pruebas" && ["sala", "capacidad", "asamblea", "enlace", "historial"].indexOf(seccion) >= 0)
                page.pestana = seccion
        }
    }

    ColumnLayout {
        anchors { fill: parent; leftMargin: page.leftInset; margins: 20 }
        spacing: 12

        RowLayout {
            Layout.fillWidth: true
            spacing: 10
            Text { text: qsTr("Pruebas"); color: Theme.text; font { pixelSize: 20; weight: Font.DemiBold } }
            GlassButton { compact: true; text: qsTr("Prueba de sala"); active: page.pestana === "sala"; onClicked: page.pestana = "sala" }
            GlassButton { compact: true; text: qsTr("Capacidad"); active: page.pestana === "capacidad"; onClicked: page.pestana = "capacidad" }
            GlassButton { compact: true; text: qsTr("Prueba de asamblea"); active: page.pestana === "asamblea"; onClicked: page.pestana = "asamblea" }
            GlassButton { compact: true; text: qsTr("Enlace"); active: page.pestana === "enlace"; onClicked: page.pestana = "enlace" }
            GlassButton { compact: true; text: qsTr("Historial"); active: page.pestana === "historial"; onClicked: { page.pestana = "historial"; Studio.historialPruebas.recargar() } }
            StatusPill {
                visible: Studio.pruebaSala.corriendo
                text: qsTr("prueba de sala corriendo"); dot: Theme.amber
            }
            StatusPill {
                visible: Studio.pruebaSala.generando.activo === true
                text: qsTr("generando para otro equipo"); dot: Theme.amber
            }
            StatusPill {
                visible: Studio.capacidad.ocupado
                text: qsTr("capacidad: %1").arg(Studio.capacidad.estado); dot: Theme.amber
            }
            Item { Layout.fillWidth: true }
        }

        // --- Prueba de sala --------------------------------------------------------------
        ScrollView {
            id: salaScroll
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: page.pestana === "sala"
            contentWidth: availableWidth
            clip: true
            ColumnLayout {
                width: salaScroll.availableWidth
                spacing: 12
                PanelPruebaSala { Layout.fillWidth: true; Layout.maximumWidth: 900 }
                PanelGenerarParaOtro { Layout.fillWidth: true; Layout.maximumWidth: 900 }
            }
        }

        // --- Capacidad -------------------------------------------------------------------
        Loader {
            Layout.fillWidth: true
            Layout.fillHeight: true
            active: page.pestana === "capacidad"
            visible: active
            sourceComponent: Component { PanelCapacidad {} }
        }

        // --- Prueba de asamblea ----------------------------------------------------------
        ScrollView {
            id: asambleaScroll
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: page.pestana === "asamblea"
            contentWidth: availableWidth
            clip: true
            ColumnLayout {
                width: Math.min(900, asambleaScroll.availableWidth)
                spacing: 12
                PanelPruebaAsamblea { Layout.fillWidth: true; onIrAlRecinto: page.irA("asamblea") }
            }
        }

        // --- Historial -------------------------------------------------------------------
        Loader {
            Layout.fillWidth: true
            Layout.fillHeight: true
            active: page.pestana === "historial"
            visible: active
            sourceComponent: Component { PanelHistorialPruebas {} }
        }

        // --- Enlace ----------------------------------------------------------------------
        ScrollView {
            id: enlaceScroll
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: page.pestana === "enlace"
            contentWidth: availableWidth
            clip: true
            ColumnLayout {
                width: Math.min(900, enlaceScroll.availableWidth)
                PanelEnlace { Layout.fillWidth: true }
            }
        }
    }
}
