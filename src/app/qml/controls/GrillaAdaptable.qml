// Grilla de videos que se adapta a la cantidad (solo presentación).
//
// Pocos: celdas grandes que llenan el lugar (las columnas que dan la celda más
// grande). Muchos: si ni así entran con un tamaño en que se distinga una cara y
// se lea el nombre (celdaMinima), quedan a ese tamaño, tantas columnas como
// entren, y la grilla se desliza. Las celdas son de proporción 16:9.
//
// El delegado ocupa su celda (GridView.view.cellWidth/cellHeight, con `margen`
// alrededor) y se entera de si está dentro de la parte que se ve con
// `grilla.enVista(y, height)`: fuera de la vista sus VideoItem no piden cuadros
// (VideoItem.enVista).
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

    // La disposición: cuántas columnas y de qué tamaño.
    readonly property var disp: {
        const W = Math.max(1, width), H = Math.max(1, height), n = Math.max(1, vista.count)
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
        return { cols: cols, desliza: desliza, celdaAncho: ancho + m2, celdaAlto: ancho * proporcion + extraAlto + m2 }
    }

    /// Una celda (y y alto en coordenadas del contenido) está dentro de lo que se ve.
    function enVista(y, alto) {
        return y + alto > vista.contentY && y < vista.contentY + vista.height
    }

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
        topMargin: grilla.disp.desliza ? 0 : Math.max(0, (grilla.height - Math.ceil(count / grilla.disp.cols) * cellHeight) / 2)
        clip: true
        interactive: grilla.disp.desliza
        boundsBehavior: Flickable.StopAtBounds
        // Las filas de arriba y de abajo que no se ven no se crean de más.
        cacheBuffer: 0
        ScrollBar.vertical: barra
    }
    ScrollBar {
        id: barra
        anchors { right: parent.right; top: parent.top; bottom: parent.bottom }
        policy: grilla.disp.desliza ? ScrollBar.AlwaysOn : ScrollBar.AlwaysOff
    }
}
