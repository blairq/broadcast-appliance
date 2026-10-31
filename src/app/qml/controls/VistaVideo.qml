// Video de una cosa del catálogo: unas claves con su distribución (el
// programa, una escena, lo de fondo del tablero) o una multivista VIVA (sigue a
// la página Multiview: fuentes, distribución y estilo).
//
// La usan las ventanas sueltas y los monitores (VistaCatalogo, VistaCosas):
// antes cada una armaba su VideoWall con sus reglas.
//
// Las escenas que no están al aire se piden al motor mientras se miran
// (Studio.frames.mirarEscena) y se sueltan al dejar de mostrarlas.
import QtQuick
import BA

Item {
    id: vv
    property var claves: []
    property string distribucion: "grid"
    /// "" = no es multivista; "*" = la elegida en la página Multiview; si no, su id
    /// (si se borró, la elegida).
    property string multiview: ""
    readonly property bool esMultiview: multiview !== ""
    readonly property var mv: !esMultiview ? ({})
                              : (Studio.multivistas.lista, multiview !== "*" && Studio.multivistas.de(multiview).id !== undefined
                                 ? Studio.multivistas.de(multiview) : Studio.multivistas.elegida)
    readonly property var clavesVivas: esMultiview ? (mv.fuentes || []) : (claves || [])
    /// Una sola fuente suele ir a un proyector o un televisor de la sala: sin
    /// marcas de operador. Con varias es un multiview de control.
    property bool operador: clavesVivas.length > 1

    // --- Escenas pedidas al motor --------------------------------------------
    property var miradas: []
    function mirar() {
        const nuevas = vv.clavesVivas.filter(k => k.startsWith("escena:")).map(k => k.slice(7))
        for (const e of nuevas) if (vv.miradas.indexOf(e) < 0) Studio.frames.mirarEscena(e, true)
        for (const e of vv.miradas) if (nuevas.indexOf(e) < 0) Studio.frames.mirarEscena(e, false)
        vv.miradas = nuevas
    }
    onClavesVivasChanged: mirar()
    Component.onCompleted: mirar()
    Component.onDestruction: { for (const e of vv.miradas) Studio.frames.mirarEscena(e, false) }

    Rectangle { anchors.fill: parent; color: "black"; visible: vv.esMultiview }
    VideoWall {
        // 16:9 centrado en la multivista (como en la página); lo demás, entero.
        readonly property real ancho16: Math.min(parent.width, parent.height * 16 / 9)
        anchors.centerIn: parent
        width: vv.esMultiview ? ancho16 : parent.width
        height: vv.esMultiview ? ancho16 * 9 / 16 : parent.height
        keys: vv.clavesVivas
        layout: vv.esMultiview ? (vv.mv.distribucion || "grid") : vv.distribucion
        fillMode: VideoItem.Fit
        operador: vv.operador
        gap: vv.esMultiview ? (vv.mv.separacion || 0) : 0
        fondo: vv.esMultiview ? (vv.mv.fondo || "transparent") : "transparent"
        borde: vv.esMultiview ? (vv.mv.borde || 0) : 0
        colorBorde: vv.esMultiview ? (vv.mv.colorBorde || "white") : "white"
        labels: vv.esMultiview && vv.mv.nombres === true
        tamEtiqueta: vv.esMultiview ? (vv.mv.tamNombres || 18) : 11
        tally: vv.esMultiview ? vv.mv.tally !== false : vv.operador
        vumetros: vv.esMultiview ? vv.mv.vumetros !== false : vv.operador
        escala: vv.esMultiview ? width / 1920 : 1
        grosorTally: vv.esMultiview ? (vv.mv.grosorTally !== undefined ? vv.mv.grosorTally : 2) : -1
    }
}
