// La matriz de mezcla: fuentes (a la salida de sus filtros) × buses.
//
// Como una consola real que se maneja con el mouse: cada cruce es una tecla
// que se enciende con el color de su bus; al pasar por una fuente o por un
// bus se iluminan TODOS sus caminos —la fila, las columnas a las que llega y
// el cable de luz entre medio— y el resto de la consola se apaga un poco.
//
// Pistas de OBS: el cruce es la asignación nativa (sin retardo). Buses
// virtuales: el cruce pasa por nuestra mezcla (~25 ms) y, si se le activan
// los ajustes (clic derecho), tiene nivel, pan y mute. Ver audio/Matriz.hpp.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import BA

Item {
    id: mx
    readonly property var mz: Studio.matriz
    readonly property var filas: mz ? mz.filas : []
    // Las pistas 2-6 de OBS (las de la grabación multipista) van plegadas por
    // defecto: se ve solo la 1 (Programa). Se despliegan con el «+5» de su cabecera.
    // (Se guarda con la Matriz, en la carpeta portable: no en las preferencias del sistema.)
    readonly property bool pistasPlegadas: !mz || mz.pistasPlegadas
    readonly property var cols: mz ? mz.columnas.filter(c => !(pistasPlegadas && c.tipo === "pista" && c.indice > 0)) : []
    readonly property var cruces: mz ? mz.cruces : ({})
    readonly property var med: mz ? mz.medidores : ({})
    property string msg: ""

    // Lo que está bajo el puntero (fuente o bus) y lo que se ilumina por eso.
    property string hFila: ""
    property string hBus: ""
    readonly property bool hayHover: hFila !== "" || hBus !== ""

    // Compacta: en el desplegable del Mezclador (entran más filas a la vista).
    property bool compacta: false
    readonly property int celda: compacta ? 36 : 46
    readonly property int anchoFila: compacta ? 190 : 230
    readonly property int altoCab: compacta ? 96 : 140

    // Mide mientras esta vista se ve (por vista: la de la página Audio y la del
    // Mezclador no se apagan una a la otra).
    onVisibleChanged: if (mz) mz.usar(mx, visible)
    Component.onCompleted: if (mz) mz.usar(mx, visible)
    Component.onDestruction: if (mz) mz.usar(mx, false)

    function on(f, b) { const c = cruces[f + "|" + b]; return !!(c && c.activo) }
    function cruce(f, b) { return cruces[f + "|" + b] || ({}) }
    function colorBus(c) {
        if (!c) return Theme.accent
        if (c.tipo === "pista") return c.indice === 0 ? "#ef4444" : "#f59e0b"
        if (c.tipo === "salida") return "#a78bfa"
        if (c.tipo === "escucha") return "#22c55e"
        if (c.tipo === "fisica") return "#38bdf8"
        return "#14b8a6"
    }
    // Índices y alcances, calculados una vez por cambio de la matriz (no en cada cruce
    // y en cada movimiento del mouse): cada cruce los lee en O(1).
    readonly property var idxColDe: { const r = {}; for (let i = 0; i < cols.length; ++i) r[cols[i].id] = i; return r }
    readonly property var idxFilaDe: { const r = {}; for (let i = 0; i < filas.length; ++i) r[filas[i].nombre] = i; return r }
    readonly property var ultimaColDe: {
        const r = {}
        for (let f = 0; f < filas.length; ++f) {
            let u = -1
            for (let i = 0; i < cols.length; ++i) if (on(filas[f].nombre, cols[i].id)) u = i
            r[filas[f].nombre] = u
        }
        return r
    }
    readonly property var ultimaFilaDe: {
        const r = {}
        for (let i = 0; i < cols.length; ++i) {
            let u = -1
            for (let f = 0; f < filas.length; ++f) if (on(filas[f].nombre, cols[i].id)) u = f
            r[cols[i].id] = u
        }
        return r
    }
    function idxCol(id) { const v = idxColDe[id]; return v === undefined ? -1 : v }
    function idxFila(n) { const v = idxFilaDe[n]; return v === undefined ? -1 : v }
    function filaLit(f) { return f === hFila || (hBus !== "" && on(f, hBus)) }
    function colLit(b) { return b === hBus || (hFila !== "" && on(hFila, b)) }
    // Hasta dónde llega el cable de una fila (la última columna a la que va) y
    // el de una columna (la última fila que le manda).
    function ultimaCol(f) { const v = ultimaColDe[f]; return v === undefined ? -1 : v }
    function ultimaFila(b) { const v = ultimaFilaDe[b]; return v === undefined ? -1 : v }
    function db(k) { const v = med[k]; return v === undefined ? -100 : v }

    // --- Medidor LED (segmentos, como los de una consola) -------------------------------
    // Un solo nodo de la GPU (Vumetro por segmentos), no un Rectangle por segmento:
    // con veinte filas y columnas a 20 Hz eran cientos de bindings por cuadro.
    component Led: Vumetro {
        property real db: -100
        property bool vertical: false
        property int segs: 14
        canales: 1
        horizontal: !vertical
        segmentos: segs
        minDb: -60; maxDb: 0; amarilloDb: -18; rojoDb: -6
        picoL: db
        implicitWidth: vertical ? 6 : 90
        implicitHeight: vertical ? 70 : 6
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 10

        // --- Barra de arriba --------------------------------------------------------------
        RowLayout {
            Layout.fillWidth: true
            spacing: 10
            Text { text: "Matriz de mezcla"; color: Theme.text; font { pixelSize: 17; weight: Font.DemiBold } }
            // Lo que no quedaba claro: la matriz elige A DÓNDE VA cada entrada (las salidas son
            // las columnas). Lo que entra y cómo se procesa es de la entrada y de su rack.
            Text {
                Layout.fillWidth: true
                text: "Filas: lo que entrega cada entrada (ya con su rack) · Columnas: las salidas · Un cruce manda esa entrada a esa salida"
                color: Theme.textFaint; font.pixelSize: 11; elide: Text.ElideRight
            }
            Row {
                spacing: 14
                // En ventanas angostas la leyenda se esconde antes que pisar los botones.
                visible: mx.width > 1150
                Repeater {
                    model: [ { c: "#ef4444", t: "programa" }, { c: "#f59e0b", t: "pistas (nativo, sin retardo)" },
                             { c: "#a78bfa", t: "salida virtual" }, { c: "#14b8a6", t: "entrada virtual (~25 ms)" } ]
                    delegate: Row {
                        required property var modelData
                        spacing: 5
                        Rectangle { width: 10; height: 10; radius: 5; color: modelData.c; anchors.verticalCenter: parent.verticalCenter }
                        Text { text: modelData.t; color: Theme.textDim; font.pixelSize: 11 }
                    }
                }
            }
            GlassButton { compact: true; text: "+ Entrada virtual"; activeColor: "#14b8a6"; active: true; onClicked: entradaPop.open() }
            GlassButton { compact: true; text: "+ Salida virtual"; activeColor: "#a78bfa"; active: true; onClicked: salidaPop.open() }
            GlassButton {
                compact: true; icon: "🔌"; text: "Equipo"
                active: !!mx.mz && mx.mz.mostrarEquipo; activeColor: "#38bdf8"
                onClicked: equipoPop.open()
                ToolTip.visible: hovered; ToolTip.delay: 400
                ToolTip.text: "Las entradas y salidas de lo que está enchufado (bocinas, monitores con audio, placas USB): mostrarlas u ocultarlas, todas o una por una."
            }
        }
        Text {
            visible: mx.msg !== ""
            text: mx.msg; color: Theme.live; font.pixelSize: 12
        }

        // --- La consola ---------------------------------------------------------------------
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            radius: Theme.radius
            // Un panel oscuro con una luz de arriba, como el frente de una consola.
            gradient: Gradient {
                GradientStop { position: 0; color: "#1a1a1f" }
                GradientStop { position: 1; color: "#0e0e11" }
            }
            border.color: Theme.glassBorder
            clip: true

            Flickable {
                id: fl
                anchors { fill: parent; margins: 14 }
                contentWidth: mx.anchoFila + mx.cols.length * mx.celda + 20
                contentHeight: mx.altoCab + mx.filas.length * mx.celda + 20
                clip: true
                ScrollBar.horizontal: ScrollBar {}
                ScrollBar.vertical: ScrollBar {}

                // Encabezados de los buses
                Row {
                    x: mx.anchoFila
                    Repeater {
                        model: mx.cols
                        delegate: Item {
                            id: cab
                            required property var modelData
                            required property int index
                            readonly property color col: mx.colorBus(modelData)
                            readonly property bool lit: mx.colLit(modelData.id)
                            width: mx.celda
                            height: mx.altoCab
                            opacity: mx.hayHover && !lit ? 0.35 : 1
                            Behavior on opacity { NumberAnimation { duration: 140 } }
                            Rectangle {
                                anchors { fill: parent; margins: 3; bottomMargin: 6 }
                                radius: 10
                                color: cab.lit ? Qt.rgba(cab.col.r, cab.col.g, cab.col.b, 0.22) : Qt.rgba(1, 1, 1, 0.04)
                                border.color: cab.lit ? cab.col : Qt.rgba(1, 1, 1, 0.08)
                                Behavior on color { ColorAnimation { duration: 140 } }
                                // Franja de color arriba, como la tira de un canal.
                                Rectangle { anchors { left: parent.left; right: parent.right; top: parent.top; margins: 4 } height: 4; radius: 2; color: cab.col }
                                Text {
                                    anchors { horizontalCenter: parent.horizontalCenter; top: parent.top; topMargin: 12 }
                                    text: cab.modelData.tipo === "pista" ? String(cab.modelData.indice + 1)
                                        : cab.modelData.tipo === "escucha" ? "🎧"
                                        : cab.modelData.tipo === "fisica" ? "🔈"
                                        : cab.modelData.tipo === "salida" ? "OUT" : "IN"
                                    color: cab.col; font { pixelSize: 11; weight: Font.Bold; family: Theme.mono }
                                }
                                Led {
                                    anchors { horizontalCenter: parent.horizontalCenter; top: parent.top; topMargin: mx.compacta ? 26 : 30 }
                                    vertical: true; width: 10; height: mx.compacta ? 34 : 62; segs: mx.compacta ? 10 : 16
                                    db: mx.db("bus:" + cab.modelData.id)
                                }
                            }
                            MouseArea {
                                anchors.fill: parent
                                hoverEnabled: true
                                acceptedButtons: Qt.LeftButton | Qt.RightButton
                                onEntered: mx.hBus = cab.modelData.id
                                onExited: if (mx.hBus === cab.modelData.id) mx.hBus = ""
                                onClicked: { busPop.bus = cab.modelData; busPop.open() }
                                ToolTip.visible: containsMouse
                                ToolTip.delay: 250
                                ToolTip.text: cab.modelData.nombre +
                                    (cab.modelData.tipo === "escucha" ? "\n→ " + cab.modelData.dispositivoNombre + "\nlos auriculares del operador: nunca al aire"
                                     : cab.modelData.tipo === "fisica" ? "\nsalida del equipo\nmezcla propia (~25 ms)"
                                     : cab.modelData.tipo === "pista" ? (cab.modelData.uso ? "\n" + cab.modelData.uso : "\nlibre") + "\nnativo: sin retardo"
                                     : cab.modelData.tipo === "salida" ? "\n→ " + (cab.modelData.dispositivoNombre || "sin dispositivo")
                                     : "\nvuelve como entrada: «" + cab.modelData.nombre + "»")
                            }
                            // En Programa: desplegar o plegar las pistas 2-6.
                            Rectangle {
                                visible: cab.modelData.tipo === "pista" && cab.modelData.indice === 0
                                anchors { right: parent.right; top: parent.top; rightMargin: 1; topMargin: 1 }
                                width: 22; height: 16; radius: 8
                                color: plegar.containsMouse ? "#f59e0b" : Qt.rgba(0.96, 0.62, 0.04, 0.25)
                                border.color: "#f59e0b"
                                Text {
                                    anchors.centerIn: parent
                                    text: mx.pistasPlegadas ? "+5" : "−"
                                    color: plegar.containsMouse ? "black" : "#f59e0b"
                                    font { pixelSize: 9; weight: Font.Bold }
                                }
                                MouseArea {
                                    id: plegar
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    onClicked: mx.mz.pistasPlegadas = !mx.pistasPlegadas
                                    ToolTip.visible: containsMouse
                                    ToolTip.delay: 250
                                    ToolTip.text: mx.pistasPlegadas
                                        ? "Mostrar las pistas 2 a 6 (las de la grabación multipista)"
                                        : "Plegar las pistas 2 a 6"
                                }
                            }
                        }
                    }
                }
                // Nombres de los buses (fila aparte, legibles)
                Row {
                    x: mx.anchoFila
                    y: mx.altoCab - 28
                    Repeater {
                        model: mx.cols
                        delegate: Text {
                            required property var modelData
                            width: mx.celda
                            horizontalAlignment: Text.AlignHCenter
                            text: modelData.nombre
                            elide: Text.ElideRight
                            color: mx.colLit(modelData.id) ? mx.colorBus(modelData) : Theme.textDim
                            font.pixelSize: 10
                            opacity: mx.hayHover && !mx.colLit(modelData.id) ? 0.35 : 1
                        }
                    }
                }

                // Filas: fuente + cruces
                Column {
                    y: mx.altoCab
                    Repeater {
                        model: mx.filas
                        delegate: Row {
                            id: fila
                            required property var modelData
                            required property int index
                            readonly property bool lit: mx.filaLit(modelData.nombre)
                            height: mx.celda
                            // La fuente
                            Item {
                                width: mx.anchoFila
                                height: mx.celda
                                opacity: mx.hayHover && !fila.lit ? 0.35 : 1
                                Behavior on opacity { NumberAnimation { duration: 140 } }
                                Rectangle {
                                    anchors { fill: parent; margins: 3; rightMargin: 8 }
                                    radius: 10
                                    color: fila.lit ? Qt.rgba(1, 0.42, 0.17, 0.16) : Qt.rgba(1, 1, 1, 0.035)
                                    border.color: fila.lit ? Theme.accent : Qt.rgba(1, 1, 1, 0.07)
                                    Behavior on color { ColorAnimation { duration: 140 } }
                                    RowLayout {
                                        anchors { fill: parent; leftMargin: 10; rightMargin: 10 }
                                        spacing: 8
                                        Rectangle {
                                            visible: fila.modelData.virtual
                                            width: 8; height: 8; radius: 4; color: "#14b8a6"
                                        }
                                        ColumnLayout {
                                            Layout.fillWidth: true
                                            spacing: 3
                                            Text {
                                                Layout.fillWidth: true
                                                text: fila.modelData.nombre
                                                elide: Text.ElideRight
                                                color: Theme.text; font.pixelSize: 12
                                            }
                                            Led { Layout.fillWidth: true; Layout.preferredHeight: 8; segs: 24; db: mx.db("fila:" + fila.modelData.nombre) }
                                        }
                                    }
                                }
                                MouseArea {
                                    id: maFila
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    onEntered: mx.hFila = fila.modelData.nombre
                                    onExited: if (mx.hFila === fila.modelData.nombre) mx.hFila = ""
                                }
                                ToolTip.visible: maFila.containsMouse
                                ToolTip.delay: 600
                                ToolTip.text: "«" + fila.modelData.nombre + "»\n" +
                                              "El vúmetro mide lo que ENTREGA esta entrada: lo que sale de su rack (sus filtros, " +
                                              "voz a texto…), antes de su volumen. Se mueve aunque no vaya a ninguna salida.\n" +
                                              "La matriz no cambia lo que entra: decide a qué salidas va (las columnas: el " +
                                              "programa, las pistas, los auriculares, las salidas del equipo). Lo que entra y " +
                                              "cómo se procesa se arma en el rack de la entrada."
                            }
                            // Los cruces
                            Repeater {
                                model: mx.cols
                                delegate: Item {
                                    id: x0
                                    required property var modelData
                                    required property int index
                                    readonly property string f: fila.modelData.nombre
                                    readonly property string b: modelData.id
                                    readonly property bool activo: mx.on(f, b)
                                    readonly property var c: mx.cruce(f, b)
                                    readonly property color col: mx.colorBus(modelData)
                                    // Tramos del cable de luz que pasan por este cruce.
                                    readonly property bool hTramo:
                                        (mx.hFila === f && index <= mx.ultimaCol(f)) ||
                                        (mx.hBus !== "" && mx.on(f, mx.hBus) && index <= mx.idxCol(mx.hBus))
                                    readonly property bool vTramo:
                                        (mx.hBus === b && fila.index <= mx.ultimaFila(b)) ||
                                        (mx.hFila !== "" && mx.on(mx.hFila, b) && fila.index <= mx.idxFila(mx.hFila))
                                    width: mx.celda
                                    height: mx.celda

                                    // Cable horizontal y vertical (con un halo).
                                    Rectangle {
                                        visible: x0.hTramo
                                        anchors.verticalCenter: parent.verticalCenter
                                        width: parent.width; height: 8
                                        color: Qt.rgba(1, 0.42, 0.17, 0.18)
                                        Rectangle { anchors.centerIn: parent; width: parent.width; height: 2; color: Theme.accent }
                                    }
                                    Rectangle {
                                        visible: x0.vTramo
                                        anchors.horizontalCenter: parent.horizontalCenter
                                        width: 8; height: parent.height
                                        color: Qt.rgba(x0.col.r, x0.col.g, x0.col.b, 0.2)
                                        Rectangle { anchors.centerIn: parent; width: 2; height: parent.height; color: x0.col }
                                    }
                                    // Cuánto retardo suma este cruce (los buses virtuales pasan por la mezcla propia).
                                    Text {
                                        visible: x0.activo && (x0.c.retardo_ms || 0) > 0
                                        anchors { right: parent.right; top: parent.top; margins: 1 }
                                        text: "+" + (x0.c.retardo_ms || 0)
                                        color: Theme.amber; font.pixelSize: 8; z: 2
                                    }
                                    // La tecla
                                    Rectangle {
                                        id: tecla
                                        anchors.centerIn: parent
                                        width: x0.activo ? (mx.compacta ? 22 : 26) : (mx.compacta ? 16 : 20)
                                        height: width
                                        radius: width / 2
                                        Behavior on width { NumberAnimation { duration: 120; easing.type: Easing.OutBack } }
                                        color: x0.activo ? x0.col : "#15151a"
                                        border.width: x0.activo ? 0 : 1
                                        border.color: ma.containsMouse ? x0.col : Qt.rgba(1, 1, 1, 0.16)
                                        opacity: mx.hayHover && !(fila.lit && mx.colLit(x0.b)) && !ma.containsMouse ? 0.4 : 1
                                        Behavior on opacity { NumberAnimation { duration: 140 } }
                                        // Brillo de LED encendido.
                                        Rectangle {
                                            visible: x0.activo
                                            anchors.centerIn: parent
                                            width: parent.width + 12; height: width; radius: width / 2
                                            color: "transparent"
                                            border.width: 4
                                            border.color: Qt.rgba(x0.col.r, x0.col.g, x0.col.b, 0.25)
                                        }
                                        Rectangle {
                                            visible: x0.activo
                                            x: parent.width * 0.22; y: parent.height * 0.18
                                            width: parent.width * 0.32; height: width; radius: width / 2
                                            color: Qt.rgba(1, 1, 1, 0.35)
                                        }
                                        Text {
                                            visible: x0.activo && !!x0.c.ajustes
                                            anchors.centerIn: parent
                                            text: x0.c.mute ? "M" : (x0.c.nivelDb > 0 ? "+" : "") + Math.round(x0.c.nivelDb || 0)
                                            color: "#0b0b0d"; font { pixelSize: 9; weight: Font.Bold; family: Theme.mono }
                                        }
                                    }
                                    // PFL (escuchar un canal un rato, se suma a la escucha fija): aro
                                    // amarillo como el botón PFL; la tecla sigue siendo la escucha fija.
                                    Rectangle {
                                        visible: !!x0.c.pfl
                                        anchors.centerIn: parent
                                        width: (mx.compacta ? 22 : 26) + 6; height: width; radius: width / 2
                                        color: "transparent"
                                        border.width: 2
                                        border.color: Theme.canalPfl
                                    }
                                    MouseArea {
                                        id: ma
                                        anchors.fill: parent
                                        hoverEnabled: true
                                        acceptedButtons: Qt.LeftButton | Qt.RightButton
                                        cursorShape: Qt.PointingHandCursor
                                        onEntered: { mx.hFila = x0.f; mx.hBus = x0.b }
                                        onExited: { if (mx.hFila === x0.f) mx.hFila = ""; if (mx.hBus === x0.b) mx.hBus = "" }
                                        onClicked: (ev) => {
                                            if (ev.button === Qt.RightButton) {
                                                if (x0.modelData.tipo === "pista") { mx.msg = "Las pistas son nativas: prender o apagar, sin retardo."; return }
                                                if (x0.modelData.tipo === "escucha") { mx.msg = "La escucha es prender o apagar: el volumen es el de la fuente."; return }
                                                if (!x0.activo) mx.msg = mx.mz.setCruce(x0.f, x0.b, true)
                                                ajustePop.f = x0.f; ajustePop.b = x0.b; ajustePop.col = x0.col
                                                ajustePop.open()
                                                return
                                            }
                                            mx.msg = mx.mz.alternar(x0.f, x0.b)
                                        }
                                        ToolTip.visible: containsMouse
                                        ToolTip.delay: 450
                                        ToolTip.text: x0.f + " → " + x0.modelData.nombre +
                                            (x0.c.pfl ? "\nen PFL (se suma mientras esté prendido; el clic cambia la escucha fija)" : "") +
                                            (x0.modelData.tipo === "pista" ? "" : "\nclic derecho: nivel, pan y mute")
                                    }
                                }
                            }
                        }
                    }
                }
                Text {
                    y: mx.altoCab + 10
                    x: 12
                    visible: mx.filas.length === 0
                    text: "No hay fuentes con audio todavía (se agregan en Entradas)."
                    color: Theme.textDim; font.pixelSize: 13
                }
            }
        }
    }

    // --- Ajustes de un cruce virtual: fader, pan, mute -----------------------------------
    Popup {
        id: ajustePop
        property string f: ""
        property string b: ""
        property color col: Theme.accent
        readonly property var c: mx.cruce(f, b)
        anchors.centerIn: parent
        padding: 16
        modal: true
        background: Rectangle { color: Theme.panel; radius: Theme.radius; border.color: ajustePop.col }
        ColumnLayout {
            spacing: 10
            Text { text: ajustePop.f + "  →  " + ajustePop.b; color: Theme.text; font { pixelSize: 14; weight: Font.DemiBold } }
            RowLayout {
                spacing: 18
                // Fader
                ColumnLayout {
                    Slider {
                        id: fader
                        orientation: Qt.Vertical
                        Layout.preferredHeight: 180
                        from: -60; to: 12; stepSize: 0.5
                        value: ajustePop.c.nivelDb !== undefined ? ajustePop.c.nivelDb : 0
                        onMoved: mx.mz.ajustar(ajustePop.f, ajustePop.b, value, pan.value, ajustePop.c.mute || false)
                    }
                    Text { Layout.alignment: Qt.AlignHCenter; text: (fader.value > 0 ? "+" : "") + fader.value.toFixed(1) + " dB"; color: Theme.text; font { pixelSize: 12; family: Theme.mono } }
                }
                ColumnLayout {
                    spacing: 12
                    Text { text: "Pan"; color: Theme.textDim; font.pixelSize: 11 }
                    Slider {
                        id: pan
                        Layout.preferredWidth: 160
                        from: -1; to: 1; stepSize: 0.05
                        value: ajustePop.c.pan !== undefined ? ajustePop.c.pan : 0
                        onMoved: mx.mz.ajustar(ajustePop.f, ajustePop.b, fader.value, value, ajustePop.c.mute || false)
                    }
                    Text { text: pan.value === 0 ? "centro" : pan.value < 0 ? "izq " + Math.round(-pan.value * 100) + "%" : "der " + Math.round(pan.value * 100) + "%"; color: Theme.textDim; font.pixelSize: 11 }
                    GlassButton {
                        compact: true; text: "Mute"; active: !!ajustePop.c.mute; activeColor: Theme.live
                        onClicked: mx.mz.ajustar(ajustePop.f, ajustePop.b, fader.value, pan.value, !ajustePop.c.mute)
                    }
                    GlassButton { compact: true; text: "Sin ajustes (0 dB)"; onClicked: { mx.mz.setAjustes(ajustePop.f, ajustePop.b, false); ajustePop.close() } }
                    GlassButton { compact: true; text: "Cerrar"; onClicked: ajustePop.close() }
                }
            }
        }
    }

    // --- Un bus: nombre, dispositivo, quitar ---------------------------------------------
    Popup {
        id: busPop
        property var bus: ({})
        anchors.centerIn: parent
        padding: 16
        modal: true
        onOpened: nombreBus.text = bus.nombre || ""
        background: Rectangle { color: Theme.panel; radius: Theme.radius; border.color: mx.colorBus(busPop.bus) }
        ColumnLayout {
            spacing: 10
            width: 340
            Text {
                text: busPop.bus.tipo === "pista" ? "Pista " + (busPop.bus.indice + 1) + " de OBS"
                      : busPop.bus.tipo === "escucha" ? "Escucha: los auriculares del operador"
                      : busPop.bus.tipo === "fisica" ? "Salida del equipo"
                      : busPop.bus.tipo === "salida" ? "Salida virtual" : "Entrada virtual"
                color: Theme.text; font { pixelSize: 15; weight: Font.DemiBold }
            }
            Text {
                Layout.fillWidth: true; wrapMode: Text.WordWrap
                color: Theme.textFaint; font.pixelSize: 11
                text: busPop.bus.tipo === "fisica"
                      ? "«" + (busPop.bus.nombre || "") + "»: lo que se le manda sale por ese dispositivo (y esos canales). Pasa por la mezcla propia (~25 ms). El dispositivo se abre solo mientras recibe algo."
                      : busPop.bus.tipo === "escucha"
                      ? "Lo que se prende acá suena por «" + (busPop.bus.dispositivoNombre || "") + "» y nunca va al aire por esta columna. Las voces (texto a audio) vienen prendidas. El dispositivo es el mismo que en Configuración → Voz del sistema."
                      : busPop.bus.tipo === "pista"
                      ? (busPop.bus.indice === 0 ? "La mezcla del programa: lo que se emite y se graba." : "Libre, o la toma una grabación individual (" + (busPop.bus.uso || "ahora libre") + ").") + " Nativa: sin retardo."
                      : busPop.bus.tipo === "salida" ? "Lo que se le manda sale por un dispositivo del equipo. Pasa por la mezcla propia (~25 ms)."
                      : "Lo que se le manda vuelve como la fuente «" + busPop.bus.nombre + "»: tiene su menú de filtros en Audio y se rutea como cualquier entrada."
            }
            // La escucha: por qué dispositivo suena.
            Flow {
                visible: busPop.bus.tipo === "escucha" && !!Studio.voz
                Layout.fillWidth: true
                spacing: 6
                GlassButton {
                    compact: true; text: "La del sistema"
                    active: !!Studio.voz && Studio.voz.auriculares === ""
                    onClicked: { mx.msg = Studio.voz.setAuriculares(""); mx.mz.refrescar() }
                }
                Repeater {
                    model: Studio.voz ? Studio.voz.salidasAudio : []
                    delegate: Repeater {
                        required property var modelData
                        model: modelData.salidas
                        delegate: GlassButton {
                            required property var modelData
                            compact: true; text: modelData.nombre
                            active: Studio.voz.auriculares === modelData.id
                            onClicked: { mx.msg = Studio.voz.setAuriculares(modelData.id); mx.mz.refrescar() }
                        }
                    }
                }
            }
            RowLayout {
                visible: busPop.bus.tipo !== "escucha" && busPop.bus.tipo !== "fisica"
                TextField { id: nombreBus; Layout.fillWidth: true; placeholderText: "nombre" }
                GlassButton { compact: true; text: "Renombrar"; onClicked: mx.msg = mx.mz.renombrar(busPop.bus.id, nombreBus.text) }
            }
            ComboBox {
                id: dispBus
                visible: busPop.bus.tipo === "salida"
                Layout.fillWidth: true
                model: mx.mz ? mx.mz.dispositivos : []
                textRole: "nombre"
                currentIndex: {
                    const d = mx.mz ? mx.mz.dispositivos : []
                    for (let i = 0; i < d.length; ++i) if (d[i].id === busPop.bus.dispositivo) return i
                    return -1
                }
                onActivated: (i) => { mx.msg = mx.mz.setDispositivo(busPop.bus.id, model[i].id) }
            }
            RowLayout {
                GlassButton {
                    visible: busPop.bus.tipo === "fisica"
                    compact: true; text: "Ocultar este dispositivo"
                    onClicked: {
                        const id = String(busPop.bus.id || "")
                        mx.mz.setVisibleDispositivo(id.substring(4, id.lastIndexOf(":")), false)
                        busPop.close()
                    }
                }
                GlassButton {
                    visible: busPop.bus.tipo !== "pista" && busPop.bus.tipo !== "escucha" && busPop.bus.tipo !== "fisica"
                    compact: true; text: "Quitar"; activeColor: Theme.live
                    onClicked: { mx.msg = mx.mz.quitarBus(busPop.bus.id); busPop.close() }
                }
                Item { Layout.fillWidth: true }
                GlassButton { compact: true; text: "Cerrar"; onClicked: busPop.close() }
            }
        }
    }

    // --- Agregar buses ---------------------------------------------------------------------
    // --- El equipo: lo que está enchufado ------------------------------------------------
    Popup {
        id: equipoPop
        anchors.centerIn: parent
        padding: 16
        modal: true
        background: Rectangle { color: Theme.panel; radius: Theme.radius; border.color: "#38bdf8" }
        ColumnLayout {
            spacing: 10
            width: 460
            Text { text: "Entradas y salidas del equipo"; color: Theme.text; font { pixelSize: 15; weight: Font.DemiBold } }
            Text {
                Layout.fillWidth: true; wrapMode: Text.WordWrap
                color: Theme.textFaint; font.pixelSize: 11
                text: "Lo que se enchufa aparece solo. Las salidas, un par de canales por columna; las entradas, un " +
                      "canal por fila (de fábrica, sin ir al aire). Un dispositivo se abre solo mientras se usa; " +
                      "lo oculto no se abre nunca."
            }
            RowLayout {
                spacing: 10
                Toggle { checked: !!mx.mz && mx.mz.mostrarEquipo; onToggled: (w) => mx.mz.mostrarEquipo = w }
                Text { text: "Mostrar el equipo en la matriz"; color: Theme.text; font.pixelSize: 13 }
            }
            Repeater {
                model: mx.mz ? mx.mz.equipo : []
                delegate: RowLayout {
                    required property var modelData
                    Layout.fillWidth: true
                    spacing: 8
                    Toggle {
                        enabled: !!mx.mz && mx.mz.mostrarEquipo && !modelData.enApp
                        checked: modelData.visible
                        onToggled: (w) => mx.mz.setVisibleDispositivo(modelData.id, w)
                    }
                    Text { text: modelData.tipo === "salida" ? "🔈" : "🎙"; font.pixelSize: 13 }
                    Text {
                        Layout.fillWidth: true; elide: Text.ElideRight
                        text: modelData.nombre + "  ·  " + modelData.canales + (modelData.canales === 1 ? " canal" : " canales")
                        color: Theme.text; font.pixelSize: 12
                    }
                    Text {
                        text: modelData.enApp ? "ya está en Audio" : modelData.abierto ? "en uso" : ""
                        color: modelData.abierto ? Theme.ok : Theme.textFaint; font.pixelSize: 11
                    }
                }
            }
            GlassButton { compact: true; text: "Cerrar"; onClicked: equipoPop.close() }
        }
    }

    Popup {
        id: entradaPop
        anchors.centerIn: parent
        padding: 16
        modal: true
        background: Rectangle { color: Theme.panel; radius: Theme.radius; border.color: "#14b8a6" }
        ColumnLayout {
            width: 340
            spacing: 10
            Text { text: "Nueva entrada virtual"; color: Theme.text; font { pixelSize: 15; weight: Font.DemiBold } }
            Text {
                Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.textFaint; font.pixelSize: 11
                text: "Una submezcla: lo que le mandes en la matriz vuelve como una entrada más, con su menú de filtros en Audio."
            }
            TextField { id: nombreEntrada; Layout.fillWidth: true; placeholderText: "nombre (por ejemplo «Micrófonos»)" }
            RowLayout {
                Item { Layout.fillWidth: true }
                GlassButton { compact: true; text: "Cancelar"; onClicked: entradaPop.close() }
                GlassButton {
                    compact: true; text: "Agregar"; active: true; activeColor: "#14b8a6"
                    onClicked: { mx.msg = mx.mz.agregarEntradaVirtual(nombreEntrada.text); nombreEntrada.text = ""; entradaPop.close() }
                }
            }
        }
    }
    Popup {
        id: salidaPop
        anchors.centerIn: parent
        padding: 16
        modal: true
        background: Rectangle { color: Theme.panel; radius: Theme.radius; border.color: "#a78bfa" }
        ColumnLayout {
            width: 360
            spacing: 10
            Text { text: "Nueva salida virtual"; color: Theme.text; font { pixelSize: 15; weight: Font.DemiBold } }
            Text {
                Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.textFaint; font.pixelSize: 11
                text: "Un bus que sale por un dispositivo del equipo: otra placa, auriculares, o un cable virtual hacia otra aplicación."
            }
            TextField { id: nombreSalida; Layout.fillWidth: true; placeholderText: "nombre (por ejemplo «Retorno»)" }
            ComboBox { id: dispSalida; Layout.fillWidth: true; model: mx.mz ? mx.mz.dispositivos : []; textRole: "nombre" }
            RowLayout {
                Item { Layout.fillWidth: true }
                GlassButton { compact: true; text: "Cancelar"; onClicked: salidaPop.close() }
                GlassButton {
                    compact: true; text: "Agregar"; active: true; activeColor: "#a78bfa"
                    onClicked: {
                        const d = dispSalida.currentIndex >= 0 ? dispSalida.model[dispSalida.currentIndex].id : ""
                        mx.msg = mx.mz.agregarSalidaVirtual(nombreSalida.text, d)
                        nombreSalida.text = ""
                        salidaPop.close()
                    }
                }
            }
        }
    }
}
