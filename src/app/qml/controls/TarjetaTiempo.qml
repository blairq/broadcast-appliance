// Una tarjeta de la página Latencias del pipeline: un nodo del pipeline (entrada, tarjeta, módulo,
// escena, mezcla, salida) con lo que agrega, lo acumulado desde la captura y su chispa
// de los últimos 60 s. Se inclina siguiendo al puntero (perspectiva); un click la elige (la
// página muestra arriba qué se puede hacer con ella: analizarla por dentro, la gráfica, el
// detalle del dorso). Todo sale del nodo (TiemposModel::mapa).
import QtQuick
import QtQuick.Controls
import QtQuick.Effects
import BA

Item {
    id: t
    required property var nodo
    /// La historia del nodo ([{t, ms, acumulado}]), la pide la página.
    property var historia: []
    property bool resaltada: false
    /// Está en la línea de tiempo (con este color).
    property bool enLinea: false
    property color colorLinea: "#4fd8ff"
    /// La alarma del plan de resolución: {nivel, motivos} (o nada).
    property var alarma: undefined
    /// La elegida (la marca la página: borde de acento y una ✓).
    property bool marcada: false
    /// Dada vuelta: el dorso con el detalle (lo decide la página, «Detalle»).
    property bool volteada: false
    signal elegida(string id)

    width: 196
    height: 118

    readonly property color colorEstado: nodo.estado === "tarde" ? Theme.live
                                       : nodo.estado === "justo" ? Theme.amber
                                       : nodo.estado === "bien" ? Theme.ok : Theme.textFaint
    readonly property color colorMedio: nodo.medio === "audio" ? "#ffb347" : nodo.medio === "video" ? "#4fd8ff" : "#c38bff"
    readonly property string icono: ({ entrada: "◉", tarjeta: "▣", modulo: "🎚", escena: "▦", mezcla: "◎",
                                       transmision: "📡", grabacion: "⏺", pantalla: "🖵", derivacion: "✎" })[nodo.tipo] || "•"
    function ms(v) { return v === null || v === undefined ? "—" : (v >= 1000 ? (v / 1000).toFixed(2) + " s" : Number(v).toFixed(v < 10 ? 1 : 0) + " ms") }

    // --- La inclinación que sigue al puntero (y se endereza sola) -----------------------
    property real inclX: 0
    property real inclY: 0
    Behavior on inclX { NumberAnimation { duration: 180; easing.type: Easing.OutCubic } }
    Behavior on inclY { NumberAnimation { duration: 180; easing.type: Easing.OutCubic } }
    HoverHandler {
        id: hover
        cursorShape: Qt.PointingHandCursor // se puede tocar
        onPointChanged: {
            if (!hovered) return
            t.inclY = (point.position.x / t.width - 0.5) * 16
            t.inclX = -(point.position.y / t.height - 0.5) * 16
        }
        onHoveredChanged: if (!hovered) { t.inclX = 0; t.inclY = 0 }
    }
    scale: hover.hovered || t.resaltada ? 1.05 : 1.0
    Behavior on scale { NumberAnimation { duration: 160; easing.type: Easing.OutBack } }
    z: hover.hovered ? 10 : 0

    // La inclinación es de todo (el halo y la tarjeta); el giro, solo de la tarjeta.
    Item {
        id: inclinado
        anchors.fill: parent
        transform: [
            Rotation { origin.x: inclinado.width / 2; origin.y: inclinado.height / 2; axis { x: 1; y: 0; z: 0 } angle: t.inclX },
            Rotation { origin.x: inclinado.width / 2; origin.y: inclinado.height / 2; axis { x: 0; y: 1; z: 0 } angle: t.inclY }
        ]

    // --- La sombra de color (el estado, como un halo) -------------------------------------
    Rectangle { id: halo; anchors.fill: parent; radius: 14; color: t.colorEstado; visible: false }
    MultiEffect {
        anchors.fill: halo
        source: halo
        autoPaddingEnabled: true
        shadowEnabled: true
        shadowColor: t.colorEstado
        shadowBlur: 1.0
        shadowOpacity: t.nodo.estado === "sin_datos" ? 0.0 : (hover.hovered || t.resaltada ? 0.85 : 0.45)
        shadowScale: 1.02
        opacity: 0.9
    }

    Flipable {
        id: caja
        anchors.fill: parent
        property bool vuelta: t.volteada
        transform: Rotation {
            origin.x: caja.width / 2; origin.y: caja.height / 2; axis { x: 0; y: 1; z: 0 }
            angle: caja.vuelta ? 180 : 0
            Behavior on angle { NumberAnimation { duration: 420; easing.type: Easing.OutCubic } }
        }
        // Un click la elige, y nada más (con el doble click que la daba vuelta, un doble click
        // la elegía y la soltaba: parecía que no respondía). Lo demás, en la barra de la página.
        TapHandler { onTapped: t.elegida(t.nodo.id) }

        front: Rectangle {
            anchors.fill: parent
            radius: 14
            // Base oscura (el halo de color queda solo afuera, como luz) y el vidrio encima.
            gradient: Gradient {
                GradientStop { position: 0.0; color: "#1c1e26" }
                GradientStop { position: 1.0; color: "#121318" }
            }
            border.color: t.marcada ? Theme.accent
                        : t.alarma ? Theme.colorAlarma(t.alarma.nivel)
                        : Qt.rgba(t.colorEstado.r, t.colorEstado.g, t.colorEstado.b, t.nodo.estado === "sin_datos" ? 0.25 : 0.85)
            border.width: t.marcada ? 3 : t.resaltada || t.alarma ? 2 : 1
            opacity: t.nodo.activo === false ? 0.5 : 1.0
            // El brillo de arriba (vidrio).
            Rectangle {
                anchors { left: parent.left; right: parent.right; top: parent.top; margins: 1 }
                height: parent.height * 0.42
                radius: 13
                gradient: Gradient {
                    GradientStop { position: 0.0; color: Qt.rgba(1, 1, 1, 0.10) }
                    GradientStop { position: 1.0; color: "transparent" }
                }
            }
            // La elegida: una ✓ de acento arriba a la derecha.
            Rectangle {
                visible: t.marcada
                anchors { right: parent.right; top: parent.top; margins: 6 }
                width: 18; height: 18; radius: 9
                color: Theme.accent
                Text { anchors.centerIn: parent; text: "✓"; color: "white"; font { pixelSize: 11; weight: Font.Bold } }
            }
            // En la línea de tiempo: un punto con su color arriba a la derecha (debajo de la ✓).
            Rectangle {
                visible: t.enLinea
                anchors.topMargin: t.marcada ? 28 : 9
                anchors { right: parent.right; top: parent.top; margins: 9 }
                width: 10; height: 10; radius: 5
                color: t.colorLinea
                border.color: "white"; border.width: 1
            }
            // La franja del medio: cian video, ámbar audio.
            Rectangle { x: 0; y: 14; width: 3; height: parent.height - 28; radius: 1.5; color: t.colorMedio }

            Column {
                anchors { left: parent.left; right: parent.right; top: parent.top; leftMargin: 14; rightMargin: 10; topMargin: 9 }
                spacing: 1
                Row {
                    spacing: 6
                    width: parent.width
                    Text { text: t.icono; color: t.colorMedio; font.pixelSize: 13 }
                    Text {
                        width: parent.width - 60
                        text: t.nodo.titulo
                        color: Theme.text
                        font { pixelSize: 12; weight: Font.DemiBold }
                        elide: Text.ElideRight
                    }
                }
                Text {
                    text: t.ms(t.nodo.ms)
                    color: t.nodo.ms === null ? Theme.textFaint : Theme.text
                    font { pixelSize: 25; weight: Font.Light; family: "Helvetica Neue" }
                }
                Text {
                    text: (t.nodo.p95_ms !== null && t.nodo.p95_ms !== undefined ? qsTr("p95 %1").arg(t.ms(t.nodo.p95_ms)) + "   " : "") +
                          (t.nodo.acumulado_ms !== null && t.nodo.acumulado_ms !== undefined ? qsTr("desde la captura %1").arg(t.ms(t.nodo.acumulado_ms)) : "")
                    color: Theme.textDim
                    font.pixelSize: 9
                }
            }
            // La chispa de los últimos 60 s.
            Canvas {
                id: chispa
                anchors { left: parent.left; right: parent.right; bottom: parent.bottom; leftMargin: 14; rightMargin: 10; bottomMargin: 20 }
                height: 20
                property var datos: t.historia
                onDatosChanged: requestPaint()
                onPaint: {
                    const c = getContext("2d")
                    c.reset()
                    const d = datos || []
                    if (d.length < 2) return
                    let max = 1
                    for (const p of d) max = Math.max(max, p.ms)
                    const x0 = d[0].t, x1 = d[d.length - 1].t, ancho = Math.max(1, x1 - x0)
                    c.beginPath()
                    for (let i = 0; i < d.length; ++i) {
                        const x = (d[i].t - x0) / ancho * width, y = height - d[i].ms / max * (height - 2) - 1
                        if (i === 0) c.moveTo(x, y); else c.lineTo(x, y)
                    }
                    c.strokeStyle = t.colorMedio
                    c.lineWidth = 1.4
                    c.stroke()
                    c.lineTo(width, height); c.lineTo(0, height); c.closePath()
                    const g = c.createLinearGradient(0, 0, 0, height)
                    g.addColorStop(0, Qt.rgba(t.colorMedio.r, t.colorMedio.g, t.colorMedio.b, 0.28))
                    g.addColorStop(1, "transparent")
                    c.fillStyle = g
                    c.fill()
                }
            }
            // Abajo: medido / declarado y el estado.
            Row {
                anchors { left: parent.left; bottom: parent.bottom; leftMargin: 14; bottomMargin: 5 }
                spacing: 6
                Rectangle {
                    width: etiqueta.implicitWidth + 10; height: 13; radius: 6.5
                    color: t.nodo.fuente === "medido" ? Qt.rgba(0.2, 0.77, 0.42, 0.18) : Qt.rgba(1, 1, 1, 0.08)
                    Text {
                        id: etiqueta
                        anchors.centerIn: parent
                        text: t.nodo.fuente === "medido" ? qsTr("medido") : t.nodo.fuente === "declarado" ? qsTr("declarado")
                            : t.nodo.fuente === "derivacion" ? qsTr("no atrasa") : t.nodo.fuente === "hora_corrida" ? qsTr("hora corrida") : qsTr("sin medir")
                        color: t.nodo.fuente === "medido" ? Theme.ok : t.nodo.fuente === "hora_corrida" ? Theme.live : Theme.textDim
                        font.pixelSize: 8
                    }
                }
                // La alarma del plan de resolución (los motivos, atrás).
                Rectangle {
                    visible: !!t.alarma
                    width: chipAlarma.implicitWidth + 10; height: 13; radius: 6.5
                    readonly property color c: t.alarma ? Theme.colorAlarma(t.alarma.nivel) : "transparent"
                    color: Qt.rgba(c.r, c.g, c.b, 0.22)
                    Text {
                        id: chipAlarma
                        anchors.centerIn: parent
                        text: "! " + (t.alarma ? Theme.textoAlarma(t.alarma.nivel) : "")
                        color: parent.c
                        font.pixelSize: 8
                    }
                }
                // Lo medido por elemento: en qué memoria sale el video y si la tarjeta cruza.
                Rectangle {
                    readonly property var d: t.nodo.detalle || {}
                    readonly property string mem: d.memoria_salida || ""
                    visible: !!d.medido_sondas && mem !== ""
                    width: chipMem.implicitWidth + 10; height: 13; radius: 6.5
                    color: d.cruce === "baja" ? Qt.rgba(1, 0.7, 0.28, 0.2) : mem !== "sistema" ? Qt.rgba(0.31, 0.85, 1, 0.16) : Qt.rgba(1, 1, 1, 0.08)
                    Text {
                        id: chipMem
                        anchors.centerIn: parent
                        text: parent.d.cruce === "baja" ? qsTr("⇣ baja a RAM") : parent.d.cruce === "sube" ? qsTr("⇡ sube a GPU")
                            : parent.mem === "sistema" ? "RAM" : "GPU"
                        color: parent.d.cruce === "baja" ? Theme.amber : parent.mem !== "sistema" ? "#4fd8ff" : Theme.textDim
                        font.pixelSize: 8
                    }
                }
                Rectangle { width: 7; height: 7; radius: 3.5; anchors.verticalCenter: parent.verticalCenter; color: t.colorEstado }
            }
        }

        back: Rectangle {
            anchors.fill: parent
            radius: 14
            color: Qt.rgba(0.07, 0.07, 0.09, 0.96)
            border.color: t.colorMedio
            border.width: 1
            Flickable {
                anchors { fill: parent; margins: 9 }
                contentHeight: lista.implicitHeight
                clip: true
                Column {
                    id: lista
                    width: parent.width
                    spacing: 1
                    Text { text: t.nodo.titulo; color: Theme.text; font.pixelSize: 11; font.weight: Font.DemiBold; width: parent.width; elide: Text.ElideRight }
                    Repeater {
                        model: t.alarma ? t.alarma.motivos : []
                        delegate: Text {
                            required property var modelData
                            width: lista.width
                            wrapMode: Text.WordWrap
                            text: "! " + modelData
                            color: Theme.colorAlarma(t.alarma.nivel)
                            font.pixelSize: 9
                        }
                    }
                    Repeater {
                        model: {
                            const d = t.nodo.detalle || {}
                            const filas = []
                            for (const k in d) {
                                const v = d[k]
                                if (v === null || v === undefined || v === "") continue
                                if (k === "elementos" || k === "pipeline") continue // el recorrido: abajo, en su panel
                                const txt = typeof v === "object" ? (v.ancho ? v.ancho + "x" + v.alto + " " + (v.formato || "") + " " + (v.fps || "") + " fps" : JSON.stringify(v))
                                                                 : typeof v === "number" ? (Math.round(v * 10) / 10).toString() : String(v)
                                filas.push({ k: k.replace(/_/g, " "), v: txt })
                            }
                            return filas
                        }
                        delegate: Text {
                            required property var modelData
                            width: lista.width
                            text: "<font color='#8a8a92'>" + modelData.k + "</font>  " + modelData.v
                            textFormat: Text.RichText
                            color: Theme.textDim
                            font.pixelSize: 9
                            elide: Text.ElideRight
                        }
                    }
                }
            }
        }
    }
    }
}
