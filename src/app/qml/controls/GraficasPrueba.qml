// Los gráficos en vivo de una prueba, de los dos lados: la máquina MEDIDA (CPU de la
// máquina, de la app y del gateway; la red que entra y sale) y la que GENERA (CPU de la
// máquina y de hinchada; su red), para ver si el generador da abasto. `serie` es la del modelo
// (PruebaSalaViewModel.serie o CapacidadViewModel.serie: un punto por segundo).
import QtQuick
import QtQuick.Layouts
import BA

GridLayout {
    id: gs
    property var serie: []
    /// La prueba de sala local: la generadora es esta (cpuGenerador) y las otras (cpuGeneradoras).
    property bool deSala: false
    columns: width > 640 ? 2 : 1
    columnSpacing: 12
    rowSpacing: 8

    GraficaPrueba {
        Layout.fillWidth: true
        titulo: qsTr("Máquina medida: CPU")
        unidad: "%"; tope: 100
        serie: gs.serie
        lineas: [{ clave: "cpuMaquina", titulo: qsTr("máquina"), color: Theme.accent },
                 { clave: "cpuApp", titulo: qsTr("app"), color: Theme.ok },
                 { clave: "cpuGateway", titulo: qsTr("gateway"), color: Theme.canalAfv }]
    }
    GraficaPrueba {
        Layout.fillWidth: true
        titulo: qsTr("Máquina medida: red")
        unidad: "Mbps"
        serie: gs.serie
        lineas: [{ clave: "redEntraMbps", titulo: qsTr("entra"), color: Theme.canalSinc },
                 { clave: "redSaleMbps", titulo: qsTr("sale"), color: Theme.amber }]
    }
    GraficaPrueba {
        Layout.fillWidth: true
        titulo: qsTr("La que genera: CPU")
        unidad: "%"; tope: 100
        serie: gs.serie
        lineas: gs.deSala
                ? [{ clave: "cpuGenerador", titulo: qsTr("hinchada (acá)"), color: Theme.canalPfl },
                   { clave: "cpuGeneradoras", titulo: qsTr("otras máquinas"), color: Theme.live }]
                : [{ clave: "cpuEquipo", titulo: qsTr("máquina"), color: Theme.accentSoft },
                   { clave: "cpuProbador", titulo: qsTr("hinchada"), color: Theme.canalPfl }]
    }
    // Lugar previsto: la fluidez del video dibujado (cuadros perdidos / irregulares por video:
    // celdas de la grilla, invitados, programa). La medida la agrega el estado de la app
    // (trabajo de fluidez); cuando la serie la traiga como `cuadrosPerdidosPct`, se ve sola.
    GraficaPrueba {
        Layout.fillWidth: true
        visible: gs.serie.some(p => p.cuadrosPerdidosPct !== undefined)
        titulo: qsTr("Máquina medida: cuadros perdidos / regularidad")
        unidad: "%"
        serie: gs.serie
        lineas: [{ clave: "cuadrosPerdidosPct", titulo: qsTr("perdidos"), color: Theme.live }]
    }
    GraficaPrueba {
        Layout.fillWidth: true
        visible: !gs.deSala
        titulo: qsTr("La que genera: red")
        unidad: "Mbps"
        serie: gs.serie
        lineas: [{ clave: "redEntraEquipoMbps", titulo: qsTr("entra"), color: Theme.canalSinc },
                 { clave: "redSaleEquipoMbps", titulo: qsTr("sale"), color: Theme.amber }]
    }
}
