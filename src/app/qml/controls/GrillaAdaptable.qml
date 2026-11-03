// Grilla de videos que se adapta a la cantidad (solo presentación).
//
// Pocos: celdas grandes que llenan el lugar (las columnas que dan la celda más
// grande). Muchos: si ni así entran con un tamaño en que se distinga una cara y
// se lea el nombre (celdaMinima), quedan a ese tamaño, tantas columnas como
// entren, y la grilla se desliza. Las celdas son de proporción 16:9.
//
// El delegado ocupa su celda (GridView.view.cellWidth/cellHeight, con `margen`
// alrededor) y se entera de si está dentro de la parte que se ve con
// `grilla.enVista(index)`: fuera de la vista sus VideoItem no piden cuadros
// (VideoItem.enVista). La parte que se ve es un rango de índices (desde..hasta) que
// se calcula UNA vez por desplazamiento, acá: las celdas se reevalúan solo cuando
// una fila entra o sale, no con cada píxel.
//
// Con 120 en la sala:
// - La disposición (`disp`) se recalcula una vez por vuelta del bucle (Qt.callLater)
//   y solo se reemplaza si cambió: entran 30 de golpe y la grilla se reacomoda una
//   vez, no 30. Tampoco lee `vista.count` en un binding: la GridView aplica los
//   cambios del modelo al reacomodarse, y se reacomodaba justamente porque cambiaba
//   `disp` (el binding loop de `disp`).
// - Las celdas se reusan al desplazar (reuseItems: el delegado avisa con
//   GridView.onPooled/onReused) y hay una fila preparada arriba y abajo
//   (cacheBuffer), sin video hasta que entra.
import QtQuick
import QtQuick.Controls

Item {
    id: grilla
    property alias model: vista.model
    property alias delegate: vista.delegate
    readonly property alias count: vista.count
    /// El ancho mínimo de una celda (con su margen): por debajo, se desliza.
    property real celdaMinima: 200
    /// Alto / ancho del video de la celda.
    property real proporcion: 9 / 16
    /// Lo que se agrega al alto de cada celda debajo del video (0: el nombre va encima).
    property real extraAlto: 0
    property real margen: 4
    /// Lo que se deja a la derecha para la barra de desplazamiento.
    readonly property real anchoBarra: 12
    readonly property bool desliza: disp.desliza

    // La disposición: cuántas columnas, de qué tamaño y cuánto aire arriba (centrada en el
    // alto cuando no se desliza). La pone `recalcular`; arranca con la celda mínima (así,
    // antes del primer cálculo, la GridView no crea cientos de celdas de 1 px).
    property var disp: ({ cols: 1, desliza: false, celdaAncho: celdaMinima, celdaAlto: celdaMinima * proporcion + extraAlto, arriba: 0 })

    /// El rango de índices que se ve (con las filas a medias). Fuera, `enVista` es falso.
    readonly property int desde: Math.max(0, Math.floor((vista.contentY - vista.originY) / Math.max(1, vista.cellHeight))) * disp.cols
    readonly property int hasta: (Math.floor((vista.contentY - vista.originY + vista.height - 0.5) / Math.max(1, vista.cellHeight)) + 1) * disp.cols - 1
    /// La celda `indice` está dentro de lo que se ve (depende de desde/hasta, que cambian
    /// cuando una fila entra o sale, no con cada píxel).
    function enVista(indice) {
        return indice >= desde && indice <= hasta
    }

    function calcular() {
        const W = Math.max(1, width), H = Math.max(1, height), cuantos = vista.count, n = Math.max(1, cuantos)
        const m2 = 2 * margen
        const maxCols = Math.max(1, Math.floor(W / celdaMinima))
        let cols = 1, ancho = 0
        for (let c = 1; c <= Math.min(n, maxCols); ++c) {
            const filas = Math.ceil(n / c)
            const a = Math.min(W / c - m2, (H / filas - m2 - extraAlto) / proporcion)
            if (a > ancho + 0.5) { ancho = a; cols = c }
        }
        const desliza = ancho < celdaMinima - m2
        // Deslizándose, la barra va al costado (no tapa la última columna).
        if (desliza) { cols = Math.max(1, Math.floor((W - anchoBarra) / celdaMinima)); ancho = (W - anchoBarra) / cols - m2 }
        ancho = Math.max(1, ancho)
        const celdaAlto = ancho * proporcion + extraAlto + m2
        const arriba = desliza ? 0 : Math.max(0, (H - Math.ceil(cuantos / cols) * celdaAlto) / 2)
        return { cols: cols, desliza: desliza, celdaAncho: ancho + m2, celdaAlto: celdaAlto, arriba: arriba }
    }
    function recalcular() {
        const d = calcular(), a = disp
        if (d.cols !== a.cols || d.desliza !== a.desliza || Math.abs(d.celdaAncho - a.celdaAncho) > 0.01
                || Math.abs(d.celdaAlto - a.celdaAlto) > 0.01 || Math.abs(d.arriba - a.arriba) > 0.5)
            disp = d
    }
    // Una vez por vuelta del bucle, por más cambios que lleguen juntos.
    function pedirRecalculo() { Qt.callLater(grilla.recalcular) }
    onWidthChanged: pedirRecalculo()
    onHeightChanged: pedirRecalculo()
    onCeldaMinimaChanged: pedirRecalculo()
    onProporcionChanged: pedirRecalculo()
    onExtraAltoChanged: pedirRecalculo()
    onMargenChanged: pedirRecalculo()
    Connections {
        target: vista
        function onCountChanged() { grilla.pedirRecalculo() }
    }
    Component.onCompleted: recalcular()

    GridView {
        id: vista
        // El ancho justo de las columnas, centrado: así las celdas quedan juntas aunque
        // el alto mande (pocos en una tarjeta ancha y baja).
        width: Math.min(grilla.width, grilla.disp.cols * grilla.disp.celdaAncho + 0.5)
        height: grilla.height
        x: grilla.disp.desliza ? 0 : (grilla.width - width) / 2
        cellWidth: grilla.disp.celdaAncho
        cellHeight: grilla.disp.celdaAlto
        // Las que no se deslizan, centradas en el alto.
        topMargin: grilla.disp.arriba
        clip: true
        interactive: grilla.disp.desliza
        boundsBehavior: Flickable.StopAtBounds
        // Una fila preparada arriba y otra abajo (sin video hasta que entra: enVista), y las
        // celdas que salen se reusan para las que entran en vez de destruirlas y crearlas.
        cacheBuffer: grilla.disp.desliza ? Math.ceil(grilla.disp.celdaAlto) : 0
        reuseItems: true
        ScrollBar.vertical: barra
    }
    ScrollBar {
        id: barra
        anchors { right: parent.right; top: parent.top; bottom: parent.bottom }
        policy: grilla.disp.desliza ? ScrollBar.AlwaysOn : ScrollBar.AlwaysOff
    }
}
