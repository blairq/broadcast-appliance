// El recorrido de una entrada por dentro (página Latencias del pipeline): cada elemento de
// su pipeline con lo que agrega (lo que tarda Y lo que retiene), en qué memoria va el video
// (GPU o la del sistema) y dónde cruza (cada cruce copia el cuadro entero). Sale del análisis
// por elemento (TiemposModel::pipeline ← motor::Sondas): se pide con «Analizar» y queda el último.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import BA

Rectangle {
    id: r
    required property var tm
    /// El id de la entrada (canal del motor) y su título.
    property string entrada: ""
    property string titulo: entrada
    /// Los elementos a resaltar: los de la tarjeta elegida (con los conversores que pide).
    property var resaltar: []
    signal cerrar()

    readonly property bool midiendo: tm && tm.analisis.length > 0 && tm.analisis[0].estado === "midiendo"
    /// El detalle del nodo de la entrada (de TiemposModel::mapa): `pipeline` (lo medido) y
    /// `diagnostico` (resumen y hallazgos, ver tiempos::diagnosticar).
    property var detalle: ({})
    readonly property var pl: (detalle && detalle.pipeline) || ({})
    readonly property var diag: (detalle && detalle.diagnostico) || ({})
    readonly property var elementos: pl.elementos || []
    readonly property var hallazgos: diag.hallazgos || []
    readonly property real maximo: {
        let m = 1
        for (const e of elementos) m = Math.max(m, e.agrega_ms || 0)
        return m
    }
    function ms(v) { return v === null || v === undefined ? "—" : (v >= 1000 ? (v / 1000).toFixed(2) + " s" : Number(v).toFixed(v < 10 ? 1 : 0) + " ms") }
    function nombreMemoria(m) {
        return ({ gl: "GPU · GL", d3d12: "GPU · D3D12", d3d11: "GPU · D3D11", cuda: "GPU · CUDA", va: "GPU · VA",
                  dmabuf: "GPU · DMABuf", sistema: "RAM" })[m] || (m ? m : "—")
    }
    function esGpu(m) { return m !== "" && m !== undefined && m !== "sistema" }

    radius: 14
    color: Qt.rgba(0.07, 0.07, 0.09, 0.96)
    border.color: Theme.glassBorder
    implicitHeight: col.implicitHeight + 20

    ColumnLayout {
        id: col
        anchors { left: parent.left; right: parent.right; top: parent.top; margins: 10 }
        spacing: 6
        RowLayout {
            Layout.fillWidth: true
            Text {
                Layout.fillWidth: true
                text: qsTr("Por dentro de «%1»").arg(r.titulo)
                color: Theme.text; font { pixelSize: 12; weight: Font.DemiBold }
                elide: Text.ElideRight
            }
            GlassButton {
                compact: true
                text: r.midiendo ? qsTr("Midiendo…") : (r.elementos.length ? qsTr("Volver a analizar") : qsTr("Analizar 5 s"))
                icon: "▶"
                active: true
                enabled: !r.midiendo
                onClicked: { const e = r.tm.analizarEntradaUi(r.entrada, 5); if (e) Studio.notify(e, "error") }
            }
            GlassButton { compact: true; text: "✕"; onClicked: r.cerrar() }
        }
        Text {
            visible: r.elementos.length === 0
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            text: r.midiendo ? qsTr("Midiendo cada elemento…")
                : qsTr("Analizar pone una sonda en cada elemento del pipeline de esta entrada unos segundos: cuánto agrega cada uno (lo que tarda y lo que retiene) y dónde el video baja de la GPU a la memoria o vuelve a subir. Fuera del análisis no cuesta nada.")
            color: Theme.textFaint; font.pixelSize: 10
        }
        // La cuenta: la edad al salir = la captura (la fuente) + lo que agrega cada elemento.
        Text {
            visible: r.elementos.length > 0
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            readonly property real captura: { let c = 0; for (const e of r.elementos) if (e.fuente && !String(e.nombre).startsWith("salida/")) c = Math.max(c, e.agrega_ms || 0); return c }
            readonly property real resto: { let t = 0; for (const e of r.elementos) if (!e.fuente) t += (e.agrega_ms || 0); return t }
            text: qsTr("Edad al salir ≈ %1  =  captura %2  +  elementos %3").arg(r.ms(captura + resto)).arg(r.ms(captura)).arg(r.ms(resto))
            color: Theme.text; font.pixelSize: 11
        }
        // El resumen y lo que se ve (del modelo: tiempos::diagnosticar).
        Text {
            visible: r.elementos.length > 0
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            textFormat: Text.RichText
            text: {
                const s = r.diag.resumen || {}
                const partes = [qsTr("%n elemento", "", r.elementos.length), qsTr("%1 en total").arg(r.ms(s.total_ms))]
                partes.push(!s.cruces ? qsTr("sin cruces de memoria") : qsTr("%n cruce de memoria", "", s.cruces))
                if (r.pl.rearmada) partes.push("<font color='" + Theme.amber + "'>" + qsTr("la entrada se rearmó en medio del análisis") + "</font>")
                if (r.pl.t_ms) partes.push(qsTr("hace %1 s").arg(Math.max(0, Math.round((Date.now() - r.pl.t_ms) / 1000))))
                return partes.join(" · ")
            }
            color: Theme.textDim; font.pixelSize: 10
        }
        Repeater {
            model: r.hallazgos
            delegate: Text {
                required property var modelData
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                text: (modelData.gravedad === "problema" ? "● " : modelData.gravedad === "aviso" ? "▲ " : "· ") + modelData.texto
                color: modelData.gravedad === "problema" ? Theme.live : modelData.gravedad === "aviso" ? Theme.amber : Theme.textDim
                font.pixelSize: 10
            }
        }
        // Los elementos, de la fuente a la salida (sin scroll propio: se desplaza la columna entera).
        Repeater {
            model: r.elementos
            delegate: Rectangle {
                id: fila
                required property var modelData
                readonly property bool suya: r.resaltar.indexOf(modelData.nombre) >= 0
                Layout.fillWidth: true
                implicitHeight: 44
                radius: 6
                color: suya ? Qt.rgba(1, 0.42, 0.17, 0.14) : Qt.rgba(1, 1, 1, 0.03)
                border.color: suya ? Theme.accent : "transparent"
                // La barra de lo que agrega (proporcional al más lento).
                Rectangle {
                    anchors { left: parent.left; top: parent.top; bottom: parent.bottom }
                    width: parent.width * Math.min(1, Math.max(0, (fila.modelData.agrega_ms || 0) / r.maximo))
                    radius: 6
                    color: Qt.rgba(0.31, 0.85, 1, 0.12)
                }
                RowLayout {
                    anchors { fill: parent; leftMargin: 6; rightMargin: 6 }
                    spacing: 6
                    Text {
                        // El cruce: ⇣ baja a la memoria del sistema, ⇡ sube a la GPU.
                        Layout.preferredWidth: 12
                        text: fila.modelData.cruce === "baja" ? "⇣" : fila.modelData.cruce === "sube" ? "⇡" : ""
                        color: fila.modelData.cruce === "baja" ? Theme.amber : Theme.ok
                        font { pixelSize: 13; weight: Font.Bold }
                    }
                    Column {
                        Layout.fillWidth: true
                        Text {
                            width: parent.width
                            text: fila.modelData.nombre
                            color: Theme.text; font.pixelSize: 10
                            elide: Text.ElideMiddle
                        }
                        Text {
                            width: parent.width
                            text: fila.modelData.fuente
                                  ? (fila.modelData.fabrica || "") + "  ·  " + qsTr("captura (la cámara, el driver, el sistema o la etapa anterior)")
                                  : fila.modelData.traspaso
                                  ? (fila.modelData.fabrica || "") + "  ·  " + qsTr("traspaso a la cadena de la placa «sin señal» (espera si el fallbackswitch frena)")
                                  : (fila.modelData.fabrica || "") + (fila.modelData.cruce
                                  ? "  ·  " + r.nombreMemoria(fila.modelData.memoria_entrada) + " → " + r.nombreMemoria(fila.modelData.memoria_salida) : "")
                            color: Theme.textFaint; font.pixelSize: 8
                            elide: Text.ElideRight
                        }
                        // Lo que antes iba en un globo al pasar el mouse: escrito, a la vista.
                        Text {
                            width: parent.width
                            text: qsTr("p95 %1").arg(r.ms(fila.modelData.agrega_p95_ms))
                                  + (fila.modelData.declara_ms !== undefined ? " · " + qsTr("declara %1").arg(r.ms(fila.modelData.declara_ms)) : "")
                                  + (fila.modelData.nivel_cola_ms !== undefined ? " · " + qsTr("la cola guarda %1").arg(r.ms(fila.modelData.nivel_cola_ms)) : "")
                                  + ((fila.modelData.puede_entrada || []).length ? " · " + qsTr("acepta %1").arg(fila.modelData.puede_entrada.map(r.nombreMemoria).join(", ")) : "")
                            color: Theme.textFaint; font.pixelSize: 8
                            elide: Text.ElideRight
                        }
                    }
                    Rectangle {
                        readonly property string mem: fila.modelData.memoria_salida || fila.modelData.memoria_entrada || ""
                        visible: mem !== ""
                        width: chipMem.implicitWidth + 8; height: 13; radius: 6.5
                        color: r.esGpu(mem) ? Qt.rgba(0.2, 0.77, 0.42, 0.18) : Qt.rgba(1, 1, 1, 0.08)
                        Text {
                            id: chipMem
                            anchors.centerIn: parent
                            text: r.nombreMemoria(parent.mem)
                            color: r.esGpu(parent.mem) ? Theme.ok : Theme.textDim
                            font.pixelSize: 8
                        }
                    }
                    Text {
                        Layout.preferredWidth: 58
                        horizontalAlignment: Text.AlignRight
                        text: fila.modelData.agrega_ms === undefined ? "—" : r.ms(fila.modelData.agrega_ms)
                        color: (fila.modelData.agrega_ms || 0) > 40 ? Theme.amber : Theme.text
                        font { pixelSize: 11; family: "Helvetica Neue" }
                    }
                }
            }
        }
    }
}
