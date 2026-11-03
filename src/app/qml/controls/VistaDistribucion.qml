// Una distribución de pantallas guardada, dibujada ENTERA en un solo lugar: lo de
// adentro de un monitor o de una ventana suelta que muestra «distribucion:<id>»
// (pantallas/Catalogo.hpp). Cada ventana de la distribución es un rectángulo con su
// vista en vivo (VistaCosas, la misma de los lienzos de VentanaLienzo.qml), en la
// proporción del rectángulo de los monitores que usa (lo que sobra, negro).
//
// La carga VistaCatalogo por URL (no como tipo): VistaCosas ya usa VistaCatalogo y un
// tipo que se contiene a sí mismo no compila. Una distribución nunca muestra otra
// (el modelo no lo acepta y el catálogo saca esas piezas), así que no hay vueltas.
import QtQuick
import BA

Rectangle {
    id: vd
    /// El `dibujo` de la cosa del catálogo: {proporcion, piezas:[{cosas, disposicion, x, y, ancho, alto}]}.
    property var dibujo: null
    property var principal: null
    color: "black"

    readonly property real prop: Math.max(0.2, vd.dibujo && vd.dibujo.proporcion ? vd.dibujo.proporcion : 16 / 9)
    readonly property real aw: Math.min(vd.width, vd.height * vd.prop)
    readonly property real ah: vd.aw / vd.prop
    readonly property real ax: (vd.width - vd.aw) / 2
    readonly property real ay: (vd.height - vd.ah) / 2

    Repeater {
        model: vd.dibujo && vd.dibujo.piezas ? vd.dibujo.piezas : []
        delegate: Item {
            required property var modelData
            x: Math.round(vd.ax + modelData.x * vd.aw)
            y: Math.round(vd.ay + modelData.y * vd.ah)
            width: Math.round(modelData.ancho * vd.aw)
            height: Math.round(modelData.alto * vd.ah)
            clip: true
            VistaCosas {
                anchors.fill: parent
                cosas: modelData.cosas
                distribucion: modelData.disposicion || "grid"
                principal: vd.principal
                marcoSolo: false
            }
        }
    }
}
