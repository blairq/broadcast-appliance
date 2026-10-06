// La vista general del audio (Audio → Estudio), como la de Video: todas las
// fuentes, cada una en su franja con su cadena entera de izquierda a derecha
// —origen (y dónde corre: GStreamer o libobs) → los módulos de su rack → los
// filtros de su cadena de GStreamer (automixer…) → la mezcla—, los cables
// entre franjas (tarjetas «Entrada») y las mesas del automixer. Se mira; para
// cambiar algo, un clic en la tarjeta abre el rack de esa fuente.
// Los datos: Studio.audio.estudio() (audio/RackManager::estudio).
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import BA

Item {
    id: est
    property var au: null
    property var datos: ({ nodos: [], cables: [], franjas: [] })
    signal abrirRack(string fuente, string nodo)

    function refrescar() { if (est.au) est.datos = est.au.estudio() }
    Timer { interval: 1500; running: est.visible; repeat: true; triggeredOnStart: true; onTriggered: est.refrescar() }

    function colorDe(tipo) {
        if (tipo === "origen-gst") return "#10b981"
        if (tipo === "origen-obs") return "#0ea5e9"
        if (tipo === "plugin") return Theme.accent
        if (tipo === "automixer" || tipo === "mesa") return "#fbbf24"
        if (tipo === "gst") return "#34d399"
        if (tipo === "mezcla") return "#64748b"
        if (tipo === "entrada") return "#38bdf8"
        if (tipo === "monitor") return "#a78bfa"
        if (tipo === "texto") return "#22c55e"
        if (tipo === "habla") return "#f472b6"
        if (tipo === "traducir") return "#eab308"
        if (tipo === "analyzer" || tipo === "generator") return "#94a3b8"
        return Theme.accent
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 8
        RowLayout {
            Layout.fillWidth: true
            spacing: 14
            Text {
                Layout.fillWidth: true
                text: "Todo el audio: cada fuente con su cadena (origen → rack → filtros → mezcla). Verde: corre en GStreamer; celeste: en libobs. Un clic abre su rack."
                color: Theme.textDim; font.pixelSize: 12; wrapMode: Text.WordWrap
            }
            Text {
                readonly property int gst: (est.datos.franjas || []).filter(f => f.motor === "gstreamer" && f.titulo !== "Automixer").length
                readonly property int obs: (est.datos.franjas || []).filter(f => f.motor === "libobs").length
                text: gst + " en GStreamer · " + obs + " en libobs"
                color: Theme.textFaint; font.pixelSize: 11
            }
        }
        Patchbay {
            id: grafo
            Layout.fillWidth: true
            Layout.fillHeight: true
            nodos: est.datos.nodos || []
            cables: est.datos.cables || []
            franjas: (est.datos.franjas || []).map(f => ({ titulo: f.titulo, y: f.y, alto: f.alto,
                                                          color: f.motor === "gstreamer" ? Qt.rgba(0.06, 0.73, 0.5, 0.06) : Qt.rgba(0.05, 0.65, 0.91, 0.06) }))
            colorDe: est.colorDe
            onElegir: (id) => {
                for (const n of (est.datos.nodos || []))
                    if (n.id === id && n.fuente) { est.abrirRack(n.fuente, n.rack || ""); return }
            }
            // Las posiciones salen de los racks (se mueven en cada rack): acá no se guardan.
            onMover: (id, x, y) => {}
            onConectar: (desde, salida, hacia, entrada) => {}
            onDesconectar: (desde, salida, hacia, entrada) => {}
        }
    }
}
