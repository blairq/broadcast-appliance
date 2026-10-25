// Una o varias cosas del catálogo, dibujadas juntas: lo de adentro de una
// ventana suelta (VentanaSuelta) y de un monitor de Salidas → Pantallas
// (PantallaWindow). La misma vista para los dos: una ventana y un monitor solo
// se diferencian en dónde están, no en cómo se dibuja lo que muestran.
//
// Varias cosas que son todas VIDEO (el programa, escenas, invitados) van en UN
// muro de video con su distribución (cuadrícula, foco, PiP, fila) y las marcas
// de operador, como un multiview de control. Si hay otra cosa (un panel, una
// página, una multivista), cada una va en su celda de una grilla, con su título.
import QtQuick
import BA

Item {
    id: vc
    /// Ids del catálogo.
    property var cosas: []
    /// Con varias cosas de video: grid | focus | pip | row.
    property string distribucion: "grid"
    /// La ventana principal (le da los componentes a VistaCatalogo).
    property var principal: null
    /// Con una sola cosa: si un panel lleva su marco y su título.
    property bool marcoSolo: true

    // Enlazado al catálogo: si una escena se borra, deja de ser video y se dibuja su hueco.
    readonly property var porId: Studio.catalogo.porId
    readonly property bool soloVideo: {
        if (!cosas || cosas.length === 0) return false
        for (let i = 0; i < cosas.length; ++i) {
            const c = porId[cosas[i]]
            if (!c || !c.dibujo || c.dibujo.modo !== "video") return false
        }
        return true
    }
    readonly property bool muro: soloVideo && cosas.length > 1
    readonly property var clavesDelMuro: {
        if (!muro) return []
        let out = []
        for (let i = 0; i < cosas.length; ++i) out = out.concat(porId[cosas[i]].dibujo.claves || [])
        return out
    }
    /// Lo que pide medir la única cosa (un panel grande, como el Mezclador).
    readonly property int anchoPedido: celdas.count === 1 && celdas.itemAt(0) ? celdas.itemAt(0).anchoPedido : 0
    readonly property int altoPedido: celdas.count === 1 && celdas.itemAt(0) ? celdas.itemAt(0).altoPedido : 0

    VistaVideo {
        anchors.fill: parent
        visible: vc.muro
        claves: vc.muro ? vc.clavesDelMuro : []
        distribucion: vc.distribucion
    }

    // Sin layouts (ni un monitor ni una ventana se arman con height-for-width): cuentas directas.
    readonly property int columnas: Math.max(1, Math.ceil(Math.sqrt(cosas.length)))
    readonly property int filas: Math.max(1, Math.ceil(cosas.length / columnas))
    Repeater {
        id: celdas
        model: vc.muro ? [] : vc.cosas
        delegate: VistaCatalogo {
            required property int index
            required property var modelData
            readonly property real celdaW: vc.width / vc.columnas
            readonly property real celdaH: vc.height / vc.filas
            x: (index % vc.columnas) * celdaW
            y: Math.floor(index / vc.columnas) * celdaH
            width: celdaW
            height: celdaH
            itemId: modelData
            principal: vc.principal
            focus: index === 0
            // Con varias, cada panel lleva su título: hay que saber qué es cada cosa.
            conMarco: vc.cosas.length > 1 || vc.marcoSolo
        }
    }
}
