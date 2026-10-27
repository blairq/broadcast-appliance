// Los controles de la Vista 360 y de la Panorámica (página Video): qué entra,
// qué sale, la vista (pan, tilt y zoom), la tira, cada cámara (su deformación y
// cómo encaja) y la guía para calibrar a mano. Lo mismo por MCP:
// video_tarjeta_ajustar (y video_panoramica para armarla).
//
// Calibrar: 1) «Entrada»: los círculos y las franjas sobre lo crudo; mover centro
// y radio de cada ojo de pez hasta que encajen. 2) «Tira/Panorama + esfera»: cómo
// encaja cada cámara (yaw, pitch, roll) y su campo, hasta que el horizonte quede
// derecho y las costuras no se noten. 3) Un rectángulo o un círculo encima de algo
// que lo sea (una puerta, un reloj): la deformación (k1, k2) hasta que coincida.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import BA

ColumnLayout {
    id: p360
    /// Los parámetros de la tarjeta (efectivos).
    property var p: ({})
    /// true: la Panorámica (un stream por cámara, lado a lado).
    property bool camara: false
    /// En la Panorámica: cuántas cámaras tiene conectadas.
    property int lentes: 0
    /// En la Panorámica: aplicar un atajo ("360" | "fila", con el campo de cada lente).
    signal atajo(string preset, real campo)
    /// Pide un cambio: ajustar(clave, valor) (la página lo junta y lo manda).
    signal ajustar(string clave, var valor)
    spacing: 6

    function num(k, d) { return typeof p[k] === "number" ? p[k] : d }
    readonly property bool conLentes: camara || p.modo === "ojo" || p.modo === "dos-ojos" || p.modo === "lentes"
    readonly property int cuantas: camara ? Math.max(1, lentes) : (p.modo === "dos-ojos" ? 2 : p.modo === "lentes" ? 2 : 1)
    readonly property bool variasLentes: cuantas > 1

    Text {
        Layout.fillWidth: true; wrapMode: Text.WordWrap
        color: Theme.textDim; font.pixelSize: 12
        text: p360.camara
              ? qsTr("Une %n cámara (un cable de cada una; la de arriba es la lente 1) en una imagen, todo en la placa. La tira mide, de fábrica, la suma de las resoluciones; la vista es un PTZ: yaw, pitch y fov son el pan, el tilt y el zoom.", "", p360.cuantas)
              : qsTr("Una cámara 360 en un stream vista como una común, en la placa. Yaw, pitch y fov son el pan, el tilt y el zoom.")
    }

    // --- Qué entra, qué sale --------------------------------------------------------------
    GridLayout {
        Layout.fillWidth: true
        columns: 2; columnSpacing: 8; rowSpacing: 4
        Text { visible: !p360.camara; text: qsTr("Entra"); color: Theme.textDim; font.pixelSize: 12 }
        ComboBox {
            visible: !p360.camara
            Layout.fillWidth: true
            readonly property var vals: ["equirect", "ojo", "dos-ojos"]
            model: [qsTr("Equirectangular"), qsTr("Un ojo de pez"), qsTr("Dos ojos de pez lado a lado")]
            currentIndex: Math.max(0, vals.indexOf(p360.p.modo || "equirect"))
            onActivated: (i) => p360.ajustar("modo", vals[i])
        }
        Text { text: qsTr("Sale"); color: Theme.textDim; font.pixelSize: 12 }
        ComboBox {
            Layout.fillWidth: true
            readonly property var vals: ["vista", "tira", "panorama", "entrada"]
            model: [qsTr("La vista (un PTZ)"), qsTr("La tira (panorámica)"), qsTr("La esfera entera (360)"), qsTr("La entrada (calibrar)")]
            currentIndex: Math.max(0, vals.indexOf(p360.p.ver || "vista"))
            onActivated: (i) => p360.ajustar("ver", vals[i])
        }
        Text { text: qsTr("Guía"); color: Theme.textDim; font.pixelSize: 12 }
        ComboBox {
            Layout.fillWidth: true
            readonly property var vals: ["ninguna", "rectangulo", "circulo", "grilla", "cruz", "esfera"]
            model: [qsTr("Ninguna"), qsTr("Rectángulo"), qsTr("Círculo"), qsTr("Grilla"), qsTr("Cruz"), qsTr("Esfera (paralelos y meridianos)")]
            currentIndex: Math.max(0, vals.indexOf(p360.p.guia || "ninguna"))
            onActivated: (i) => p360.ajustar("guia", vals[i])
        }
    }

    // --- Atajos de la Panorámica ------------------------------------------------------------
    RowLayout {
        visible: p360.camara
        Layout.fillWidth: true
        spacing: 6
        Text { text: qsTr("Armar como"); color: Theme.textDim; font.pixelSize: 12 }
        GlassButton {
            compact: true; text: qsTr("Fila de cámaras")
            onClicked: p360.atajo("fila", parseFloat(campoAtajo.text.replace(",", ".")) || 60)
            ToolTip.visible: hovered; ToolTip.delay: 400
            ToolTip.text: qsTr("Comunes de ese campo, repartidas con un 10% que se pisa; sale la tira (la suma de las resoluciones)")
        }
        TextField { id: campoAtajo; Layout.preferredWidth: 46; text: "60"; font.pixelSize: 11; horizontalAlignment: Text.AlignRight }
        Text { text: "°"; color: Theme.textDim; font.pixelSize: 12 }
        GlassButton {
            compact: true; text: qsTr("360 de dos lentes"); enabled: p360.cuantas === 2
            onClicked: p360.atajo("360", 190)
            ToolTip.visible: hovered; ToolTip.delay: 400
            ToolTip.text: qsTr("Dos ojos de pez de 190° espalda con espalda; sale la vista (el PTZ)")
        }
    }

    // --- Los pasos de la calibración ------------------------------------------------------
    Flow {
        Layout.fillWidth: true
        spacing: 6
        GlassButton {
            visible: p360.conLentes; compact: true; text: qsTr("1 · Encajar círculos")
            onClicked: { p360.ajustar("ver", "entrada"); p360.ajustar("guia", "ninguna") }
        }
        GlassButton {
            compact: true; text: (p360.conLentes ? "2 · " : "") + (p360.camara ? qsTr("Tira + esfera") : qsTr("Panorama + esfera"))
            onClicked: { p360.ajustar("ver", p360.camara ? "tira" : "panorama"); p360.ajustar("guia", "esfera") }
        }
        GlassButton {
            compact: true; text: (p360.conLentes ? "3 · " : "") + qsTr("Vista + rectángulo")
            onClicked: { p360.ajustar("ver", "vista"); p360.ajustar("guia", "rectangulo") }
        }
        GlassButton {
            compact: true; text: qsTr("Sin guía")
            onClicked: { p360.ajustar("ver", "vista"); p360.ajustar("guia", "ninguna") }
        }
    }

    // --- Las secciones de deslizadores ----------------------------------------------------
    component Fila: ColumnLayout {
        id: fila
        required property var modelData
        Layout.fillWidth: true
        spacing: 0
        readonly property real valor: p360.num(modelData.k, modelData.def !== undefined ? modelData.def : 0)
        RowLayout {
            Layout.fillWidth: true
            Text { text: fila.modelData.t; color: Theme.textDim; font.pixelSize: 12; Layout.fillWidth: true; elide: Text.ElideRight }
            // El número exacto (a mano): Enter lo aplica.
            TextField {
                Layout.preferredWidth: 70
                horizontalAlignment: Text.AlignRight
                font.pixelSize: 11
                text: fila.modelData.entero ? Math.round(fila.valor) : Number(fila.valor).toFixed(fila.modelData.dec || 3)
                onAccepted: {
                    const v = parseFloat(text.replace(",", "."))
                    if (!isNaN(v)) p360.ajustar(fila.modelData.k, fila.modelData.entero ? Math.round(v) : v)
                }
            }
        }
        Slider {
            Layout.fillWidth: true
            from: fila.modelData.min; to: fila.modelData.max
            stepSize: fila.modelData.entero ? 1 : 0
            value: fila.valor
            onMoved: p360.ajustar(fila.modelData.k, fila.modelData.entero ? Math.round(value) : value)
        }
    }
    component Seccion: ColumnLayout {
        id: sec
        property string titulo: ""
        property bool abierta: false
        property var filas: []
        Layout.fillWidth: true
        spacing: 2
        GlassButton {
            Layout.fillWidth: true; compact: true
            text: (sec.abierta ? "▾ " : "▸ ") + sec.titulo
            onClicked: sec.abierta = !sec.abierta
        }
        Repeater {
            model: sec.abierta ? sec.filas : []
            delegate: Fila {}
        }
    }

    Seccion {
        titulo: qsTr("Vista: pan, tilt y zoom")
        abierta: true
        filas: [
            { k: "yaw", t: qsTr("Pan: hacia dónde mira (°)"), min: -180, max: 180, dec: 1 },
            { k: "pitch", t: qsTr("Tilt: arriba o abajo (°)"), min: -90, max: 90, dec: 1 },
            { k: "fov", t: qsTr("Zoom: cuánto abre (°)"), min: 10, max: 170, dec: 1, def: 90 },
            { k: "tira_campo", t: qsTr("Tira: grados de costado"), min: 10, max: 360, dec: 1, def: 180 },
            { k: "ancho", t: qsTr("Ancho que sale (0 = como entra)"), min: 0, max: 16384, entero: true },
            { k: "alto", t: qsTr("Alto que sale (0 = como entra)"), min: 0, max: 8192, entero: true }]
    }
    Seccion {
        visible: p360.camara
        titulo: qsTr("Streams de las cámaras")
        filas: [
            { k: "lente_ancho", t: qsTr("Ancho de cada stream"), min: 320, max: 3840, entero: true, def: 1920 },
            { k: "lente_alto", t: qsTr("Alto de cada stream"), min: 240, max: 2160, entero: true, def: 1080 }]
    }
    Repeater {
        model: p360.conLentes ? p360.cuantas : 0
        delegate: ColumnLayout {
            id: lenteCol
            required property int index
            readonly property string b: "lente" + (index + 1)
            readonly property bool ojo: p360.camara || p360.p.modo === "lentes" ? (p360.p[b + "_tipo"] || "ojo") === "ojo" : true
            Layout.fillWidth: true
            spacing: 2
            RowLayout {
                visible: p360.camara || p360.p.modo === "lentes"
                Layout.fillWidth: true
                Text { text: qsTr("Lente %1").arg(lenteCol.index + 1); color: Theme.text; font.pixelSize: 12; Layout.fillWidth: true }
                ComboBox {
                    Layout.preferredWidth: 150
                    model: [qsTr("Ojo de pez"), qsTr("Común")]
                    currentIndex: lenteCol.ojo ? 0 : 1
                    onActivated: (i) => p360.ajustar(lenteCol.b + "_tipo", i === 0 ? "ojo" : "normal")
                }
            }
            Seccion {
                titulo: (p360.p.modo === "dos-ojos" && lenteCol.index === 1 ? qsTr("Lente %1 (atrás): deformación y cómo encaja")
                                                                       : qsTr("Lente %1: deformación y cómo encaja")).arg(lenteCol.index + 1)
                filas: [
                    { k: lenteCol.b + "_x", t: qsTr("Centro óptico, de costado"), min: -0.5, max: 1.5, def: 0.5, dec: 4 },
                    { k: lenteCol.b + "_y", t: qsTr("Centro óptico, de arriba abajo"), min: -0.5, max: 1.5, def: 0.5, dec: 4 }]
                    .concat(lenteCol.ojo ? [{ k: lenteCol.b + "_radio", t: qsTr("Radio del círculo"), min: 0.05, max: 1.5, def: 0.5, dec: 4 }] : [])
                    .concat([
                    { k: lenteCol.b + "_campo", t: lenteCol.ojo ? qsTr("Campo del círculo (°)") : qsTr("Campo de ancho (°)"), min: 10, max: 270, def: lenteCol.ojo ? 190 : 60, dec: 2 },
                    { k: lenteCol.b + "_yaw", t: qsTr("Encaje: de costado (°)"), min: -180, max: 180, dec: 2 },
                    { k: lenteCol.b + "_pitch", t: qsTr("Encaje: de arriba abajo (°)"), min: -90, max: 90, dec: 2 },
                    { k: lenteCol.b + "_roll", t: qsTr("Encaje: rotación (°)"), min: -180, max: 180, dec: 2 },
                    { k: lenteCol.b + "_k1", t: qsTr("Deformación k1 (barril < 0 < cojín)"), min: -1, max: 1, dec: 4 },
                    { k: lenteCol.b + "_k2", t: qsTr("Deformación k2"), min: -1, max: 1, dec: 4 }])
            }
        }
    }
    Seccion {
        visible: p360.variasLentes
        titulo: qsTr("Costuras")
        filas: [{ k: "mezcla", t: qsTr("Franja de mezcla entre lentes (°)"), min: 0, max: 40, def: 5, dec: 1 }]
    }
    Seccion {
        titulo: qsTr("Guía: dónde y cómo")
        filas: [
            { k: "guia_x", t: qsTr("Centro, de costado"), min: -0.5, max: 1.5, def: 0.5, dec: 4 },
            { k: "guia_y", t: qsTr("Centro, de arriba abajo"), min: -0.5, max: 1.5, def: 0.5, dec: 4 },
            { k: "guia_ancho", t: qsTr("Ancho (en la grilla, el paso)"), min: 0, max: 2, def: 0.5, dec: 4 },
            { k: "guia_alto", t: qsTr("Alto (en la grilla, el paso)"), min: 0, max: 2, def: 0.5, dec: 4 },
            { k: "guia_grosor", t: qsTr("Grosor (px)"), min: 0.5, max: 20, def: 3, dec: 1 }]
    }
    RowLayout {
        spacing: 6
        Text { text: qsTr("Color de la guía"); color: Theme.textDim; font.pixelSize: 12 }
        Repeater {
            model: [{ c: 0xFF00FF00, t: "#00FF00" }, { c: 0xFFFF00FF, t: "#FF00FF" }, { c: 0xFFFFFF00, t: "#FFFF00" },
                    { c: 0xFF00FFFF, t: "#00FFFF" }, { c: 0xFFFF3030, t: "#FF3030" }, { c: 0xFFFFFFFF, t: "#FFFFFF" }]
            delegate: Rectangle {
                required property var modelData
                width: 20; height: 20; radius: 4
                color: modelData.t
                border.color: Number(p360.p.guia_color) === modelData.c ? Theme.text : Qt.rgba(1, 1, 1, 0.25)
                border.width: Number(p360.p.guia_color) === modelData.c ? 2 : 1
                MouseArea { anchors.fill: parent; onClicked: p360.ajustar("guia_color", modelData.c) }
            }
        }
    }
}
