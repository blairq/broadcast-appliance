// Pagina Cámaras PTZ: un solo control para todas las cámaras (Studio.camaras,
// ptz/ControlCamaras). Cada entrada de video tiene PTZ digital (recorte y
// zoom en la GPU); si la cámara se mueve de verdad se le elige su backend
// (VISCA, ONVIF, Panasonic) y aparecen los controles que ese backend puede
// (foco, autofoco, presets de la cámara…). Con los dos, se elige qué mueve el
// joystick. Acá también va «Mejorar imagen» por entrada.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import BA
import "../controls"

Rectangle {
    id: page
    // Interlock: la regla es del modelo (chequearMovimiento); si pide confirmar, se pregunta.
    function siNoAlAire(accion) {
        const motivo = page.cc ? page.cc.chequearMovimiento(page.sel, false, "") : ""
        if (motivo === "") { accion(); return }
        ApplicationWindow.window.confirm({
            title: qsTr("La cámara «%1» está al aire").arg(page.sel),
            text: qsTr("%1 ¿Mover igual?").arg(motivo),
            accept: qsTr("Mover igual"), reject: qsTr("Cancelar"),
            action: accion
        })
    }
    color: Theme.bg
    property real leftInset: 92
    readonly property var cc: Studio.camaras
    readonly property var inp: Studio.inputs
    property string sel: ""
    readonly property var cam: {
        const l = cc ? cc.camaras : []
        for (let i = 0; i < l.length; ++i) if (l[i].entrada === sel) return l[i]
        return null
    }
    readonly property bool conHw: cam !== null && cam.backend !== "ninguno"
    // Si la entrada es una cámara de red: ip, marca, modelo, onvif, ptz (sí/no/no se sabe), usuario…
    property int redTick: 0
    readonly property var red: { redTick; return Studio.red && sel !== "" ? Studio.red.camaraDeEntrada(sel) : ({}) }
    readonly property bool esDeRed: page.red.ip !== undefined
    property string ptzMsg: ""
    property bool ptzOk: false
    property bool activandoPtz: false
    readonly property bool mueveDigital: cam !== null && cam.modo === "digital"
    // Lo que puede lo que mueve el joystick ahora.
    readonly property var caps: cam === null ? ({}) : (mueveDigital ? cam.capacidadesDigital : cam.capacidades)
    property real velocidad: 0.6
    property string error: ""
    property var pos: ({})
    // La grilla del recinto en lugar del control de una cámara.
    property bool verRecinto: false

    onVisibleChanged: { inp.setActive(visible); mirar() }
    Component.onCompleted: {
        if (visible) inp.setActive(true) // creada oculta no apaga a Entradas, que comparte `inp`
        if (cc && cc.camaras.length > 0) sel = cc.camaras[0].entrada
        mirar()
    }
    Connections {
        target: page.cc
        function onCambio() {
            if (page.sel === "" && page.cc.camaras.length > 0) page.sel = page.cc.camaras[0].entrada
        }
    }
    // El encuadre digital se lee seguido mientras se ve la página (es barato).
    Timer {
        interval: 150; repeat: true; running: page.visible && page.cam !== null && page.cam.digital
        onTriggered: page.pos = page.cc.posicion(page.sel)
    }

    function avisar(e) { page.error = e }
    // Lo que termina después (la foto de una posición) avisa por acá.
    Connections { target: page.cc; function onAviso(texto) { page.avisar(texto) } }
    // ui_show_page {page: "camaras", seccion: "recinto" | <entrada>}
    Connections {
        target: Studio
        function onMostrarSeccion(pagina, seccion) {
            if (pagina !== "camaras") return
            if (seccion.toLowerCase() === "recinto") { page.verRecinto = true; return }
            page.sel = seccion
            page.verRecinto = false
        }
    }
    onSelChanged: { page.ptzMsg = ""; page.redTick++; mirar() }
    // La imagen en vivo de la elegida, solo mientras se ve la página (renderizarla cuesta).
    property string mirando: ""
    function mirar() {
        const quiero = page.visible && !page.verRecinto ? page.sel : ""
        if (quiero === page.mirando) return
        if (page.mirando !== "") Studio.frames.mirarEntrada(page.mirando, false)
        if (quiero !== "") Studio.frames.mirarEntrada(quiero, true)
        page.mirando = quiero
    }
    onVerRecintoChanged: mirar()
    Component.onDestruction: if (page.mirando !== "") Studio.frames.mirarEntrada(page.mirando, false)
    Connections {
        target: Studio.red
        function onPtzListo(entrada, ok, texto) {
            if (entrada !== page.sel) return
            page.activandoPtz = false
            page.ptzOk = ok
            page.ptzMsg = texto
            page.redTick++
        }
        function onCambio() { page.redTick++ }
    }

    RowLayout {
        anchors { fill: parent; leftMargin: page.leftInset; margins: 20 }
        spacing: 16

        // --- Las entradas ---------------------------------------------------------------------
        Panel {
            Layout.preferredWidth: 280
            Layout.fillHeight: true
            title: qsTr("Cámaras (%1)").arg(page.cc ? page.cc.camaras.length : 0)
            GlassButton {
                id: botonRecinto
                anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
                text: qsTr("▦ Recinto (bancas)"); active: page.verRecinto
                onClicked: page.verRecinto = true
            }
            ListView {
                id: lista
                anchors { left: parent.left; right: parent.right; top: parent.top; bottom: botonRecinto.top; bottomMargin: 8 }
                clip: true
                spacing: 8
                model: page.cc ? page.cc.camaras : []
                Text { anchors.centerIn: parent; width: parent.width - 20; visible: lista.count === 0; wrapMode: Text.WordWrap
                       horizontalAlignment: Text.AlignHCenter; color: Theme.textFaint; font.pixelSize: 13
                       text: qsTr("No hay entradas de video. Agregá una cámara en Entradas.") }
                delegate: Rectangle {
                    required property var modelData
                    width: lista.width
                    height: 76
                    radius: 12
                    color: modelData.entrada === page.sel ? Qt.rgba(1, 0.42, 0.17, 0.16) : Qt.rgba(1, 1, 1, 0.04)
                    border.color: modelData.entrada === page.sel ? Theme.accent : Qt.rgba(1, 1, 1, 0.08)
                    RowLayout {
                        anchors { fill: parent; margins: 8 }
                        spacing: 8
                        Rectangle {
                            Layout.preferredWidth: 96; Layout.preferredHeight: 54; radius: 6; color: "black"; clip: true
                            Image { anchors.fill: parent; fillMode: Image.PreserveAspectFit; cache: false
                                    source: "image://inputs/" + encodeURIComponent(modelData.entrada) + "?" + page.inp.stamp }
                        }
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 2
                            Text { text: modelData.entrada; color: Theme.text; font { pixelSize: 13; weight: Font.DemiBold }
                                   elide: Text.ElideRight; Layout.fillWidth: true }
                            RowLayout {
                                spacing: 6
                                Rectangle { implicitWidth: 8; implicitHeight: 8; radius: 4; color: modelData.listo ? Theme.ok : Theme.warn }
                                Text { text: modelData.backend === "ninguno" ? (modelData.digital ? qsTr("Digital") : qsTr("Sin PTZ"))
                                                                            : modelData.backend.toUpperCase() + (modelData.digital ? " + " + qsTr("digital") : "")
                                       color: Theme.textDim; font.pixelSize: 11 }
                            }
                            Text { visible: modelData.mejoraActiva; text: qsTr("✦ Mejorar imagen"); color: Theme.accentSoft; font.pixelSize: 10 }
                            Text {
                                readonly property var r: { page.redTick; return Studio.red ? Studio.red.camaraDeEntrada(modelData.entrada) : ({}) }
                                visible: modelData.backend === "ninguno" && r.ip !== undefined && r.ptz !== false
                                text: r.ptz === true ? qsTr("⚙ PTZ real disponible: configuralo") : qsTr("⚙ Cámara de red: ¿tiene PTZ real?")
                                color: Theme.amber; font.pixelSize: 10
                            }
                        }
                    }
                    MouseArea { anchors.fill: parent; onClicked: { page.sel = modelData.entrada; page.error = ""; page.verRecinto = false } }
                }
            }
        }

        // --- El control ---------------------------------------------------------------------
        ScrollView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            contentWidth: availableWidth
            clip: true
            visible: page.cam !== null && !page.verRecinto

            ColumnLayout {
                width: parent.width
                spacing: 14

                // Vista y mapa del encuadre digital.
                Panel {
                    Layout.fillWidth: true
                    Layout.preferredHeight: vistaFila.implicitHeight + 64
                    title: page.sel
                    RowLayout {
                        id: vistaFila
                        anchors.fill: parent
                        spacing: 14
                        // La imagen en vivo: clic apunta ahí, arrastrar encuadra ese cuadro
                        // (lo mismo que la ventana de la tarjeta PTZ: ControlCamaras::apuntar).
                        Rectangle {
                            id: vista
                            Layout.fillWidth: true
                            Layout.preferredHeight: width * 9 / 16
                            Layout.maximumHeight: 340
                            radius: 10; color: "black"; clip: true
                            VideoItem { id: video; anchors.fill: parent; source: page.cam ? "entrada:" + page.sel : ""; fillMode: VideoItem.Fit }
                            Text { visible: !video.hasFrame; anchors.centerIn: parent
                                   text: qsTr("Sin señal"); color: Theme.textFaint; font.pixelSize: 13 }
                            // Donde cae la imagen dentro del rectángulo (Fit deja bandas).
                            readonly property real aspecto: video.frameSize.width > 0 ? video.frameSize.width / video.frameSize.height : 16 / 9
                            readonly property real iw: Math.min(width, height * aspecto)
                            readonly property real ih: iw / aspecto
                            readonly property real ix: (width - iw) / 2
                            readonly property real iy: (height - ih) / 2
                            function u(x) { return Math.max(0, Math.min(1, (x - ix) / iw)) }
                            function v(y) { return Math.max(0, Math.min(1, (y - iy) / ih)) }
                            Rectangle {
                                id: cuadro
                                visible: false
                                color: Qt.rgba(1, 0.42, 0.17, 0.15)
                                border { color: Theme.accent; width: 2 }
                            }
                            // La mira en el centro: ahí va lo que se toque.
                            Rectangle { anchors.centerIn: parent; width: 18; height: 1; color: Qt.rgba(1, 1, 1, 0.35) }
                            Rectangle { anchors.centerIn: parent; width: 1; height: 18; color: Qt.rgba(1, 1, 1, 0.35) }
                            StatusPill { anchors { left: parent.left; top: parent.top; margins: 8 }
                                         text: page.mueveDigital ? qsTr("DIGITAL") : qsTr("ÓPTICO"); dot: page.cam && page.cam.listo ? Theme.ok : Theme.warn }
                            Text {
                                anchors { left: parent.left; right: parent.right; bottom: parent.bottom; margins: 6 }
                                horizontalAlignment: Text.AlignHCenter; elide: Text.ElideRight
                                text: page.mueveDigital ? qsTr("Clic: apuntar · arrastrar: encuadrar ese cuadro · rueda: zoom · doble clic: cuadro completo")
                                                       : qsTr("Clic: apuntar · arrastrar: encuadrar ese cuadro")
                                color: Qt.rgba(1, 1, 1, 0.55); font.pixelSize: 10
                                style: Text.Outline; styleColor: Qt.rgba(0, 0, 0, 0.7)
                            }
                            MouseArea {
                                anchors.fill: parent
                                enabled: page.cam !== null
                                preventStealing: true // que el ScrollView no se lleve el arrastre
                                cursorShape: Qt.CrossCursor
                                property point desde
                                onPressed: (m) => { desde = Qt.point(m.x, m.y); cuadro.visible = false }
                                onPositionChanged: (m) => {
                                    if (!pressed) return
                                    if (Math.abs(m.x - desde.x) < 6 && Math.abs(m.y - desde.y) < 6) return
                                    cuadro.x = Math.min(m.x, desde.x); cuadro.y = Math.min(m.y, desde.y)
                                    cuadro.width = Math.abs(m.x - desde.x); cuadro.height = Math.abs(m.y - desde.y)
                                    cuadro.visible = true
                                }
                                onReleased: (m) => {
                                    let u = vista.u(m.x), v = vista.v(m.y), an = 0, al = 0
                                    if (cuadro.visible) {
                                        cuadro.visible = false
                                        u = vista.u(cuadro.x + cuadro.width / 2); v = vista.v(cuadro.y + cuadro.height / 2)
                                        an = cuadro.width / vista.iw; al = cuadro.height / vista.ih
                                    }
                                    page.siNoAlAire(function() { page.avisar(page.cc.apuntar(page.sel, u, v, an, al)) })
                                }
                                onDoubleClicked: if (page.mueveDigital) page.siNoAlAire(function() { page.avisar(page.cc.encuadre(page.sel, 0.5, 0.5, 1, 0.8)) })
                                onWheel: (w) => {
                                    if (!page.mueveDigital) return
                                    const z = page.pos.zoom || 1
                                    page.avisar(page.cc.encuadre(page.sel, page.pos.x || 0.5, page.pos.y || 0.5,
                                                                 Math.max(1, Math.min(8, z * (w.angleDelta.y > 0 ? 1.15 : 1 / 1.15))), 0.15))
                                }
                            }
                        }
                        // Mapa: el cuadro entero y la ventana del recorte. Clic = centrar ahí.
                        ColumnLayout {
                            visible: page.cam !== null && page.cam.digital
                            Layout.preferredWidth: 200
                            spacing: 6
                            Text { text: qsTr("Encuadre digital"); color: Theme.textDim; font.pixelSize: 12 }
                            Rectangle {
                                id: mapa
                                Layout.preferredWidth: 200; Layout.preferredHeight: 112
                                color: Qt.rgba(1, 1, 1, 0.05); border.color: Qt.rgba(1, 1, 1, 0.2); radius: 4
                                readonly property real zoomActual: page.pos.zoom || 1
                                Rectangle {
                                    width: mapa.width / mapa.zoomActual; height: mapa.height / mapa.zoomActual
                                    x: ((page.pos.x || 0.5) - 0.5 / mapa.zoomActual) * mapa.width
                                    y: ((page.pos.y || 0.5) - 0.5 / mapa.zoomActual) * mapa.height
                                    color: Qt.rgba(1, 0.42, 0.17, 0.18); border.color: Theme.accent; border.width: 2
                                }
                                MouseArea {
                                    anchors.fill: parent
                                    onClicked: (m) => page.avisar(page.cc.encuadre(page.sel, m.x / width, m.y / height, mapa.zoomActual, 0.6))
                                    onWheel: (w) => page.avisar(page.cc.encuadre(page.sel, page.pos.x || 0.5, page.pos.y || 0.5,
                                                                                 Math.max(1, Math.min(8, mapa.zoomActual * (w.angleDelta.y > 0 ? 1.15 : 1 / 1.15))), 0.15))
                                }
                            }
                            Text { text: qsTr("Zoom %1×  ·  clic: centrar, rueda: zoom").arg(page.pos.zoom ? page.pos.zoom.toFixed(2) : "1.00")
                                   color: Theme.textFaint; font.pixelSize: 10; Layout.preferredWidth: 200; wrapMode: Text.WordWrap }
                            GlassButton { compact: true; text: qsTr("Cuadro completo"); onClicked: page.avisar(page.cc.encuadre(page.sel, 0.5, 0.5, 1, 0.8)) }
                        }
                    }
                }

                // Una cámara de red que todavía se mueve solo en digital: cómo
                // activar su PTZ de verdad (ONVIF), sin buscar el backend a mano.
                Panel {
                    Layout.fillWidth: true
                    visible: page.esDeRed && !page.conHw
                    Layout.preferredHeight: visible ? ptzRealCol.implicitHeight + 64 : 0
                    title: qsTr("Mover la cámara de verdad (PTZ real)")
                    ColumnLayout {
                        id: ptzRealCol
                        anchors.fill: parent
                        spacing: 8
                        Text {
                            Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.text; font.pixelSize: 12
                            text: qsTr("«%1» es una cámara de red en %2%3. Ahora el joystick mueve solo el RECORTE digital. %4")
                                  .arg(page.sel).arg(page.red.ip || "")
                                  .arg(page.red.marca ? " (" + page.red.marca + (page.red.modelo ? " " + page.red.modelo : "") + ")" : "")
                                  .arg(page.red.ptz === true ? qsTr("La cámara tiene PTZ por ONVIF: con su usuario y clave se mueve el motor, con zoom óptico y sus presets.")
                                                             : qsTr("Si es una cámara PTZ (domo, motorizada), con su usuario y clave ONVIF se mueve el motor, con zoom óptico y sus presets."))
                        }
                        Repeater {
                            model: page.red.notas || []
                            delegate: Text { required property var modelData; Layout.fillWidth: true; wrapMode: Text.WordWrap
                                             text: "• " + modelData; color: Theme.textDim; font.pixelSize: 11 }
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 8
                            TextField {
                                id: ptzUsuario
                                Layout.preferredWidth: 180
                                placeholderText: qsTr("Usuario ONVIF")
                                text: page.red.usuario || ""
                            }
                            TextField {
                                id: ptzClave
                                Layout.fillWidth: true
                                echoMode: TextInput.Password
                                placeholderText: page.red.tieneClave ? qsTr("Clave (ya guardada: vacío = usar esa)") : qsTr("Clave")
                            }
                            GlassButton {
                                text: page.activandoPtz ? qsTr("Consultando la cámara…") : qsTr("Activar PTZ real")
                                active: true
                                enabled: !page.activandoPtz && ptzUsuario.text.trim() !== "" && (ptzClave.text !== "" || page.red.tieneClave)
                                onClicked: {
                                    page.activandoPtz = true
                                    page.ptzMsg = ""
                                    Studio.red.activarPtzReal(page.sel, ptzUsuario.text, ptzClave.text)
                                    ptzClave.text = ""
                                }
                            }
                        }
                        Text {
                            visible: page.ptzMsg !== ""
                            Layout.fillWidth: true; wrapMode: Text.WordWrap; font.pixelSize: 12
                            text: page.ptzMsg; color: page.ptzOk ? Theme.ok : Theme.warn
                        }
                        Text {
                            Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.textFaint; font.pixelSize: 11
                            text: qsTr("La clave se guarda cifrada en este equipo. Si la cámara no es ONVIF (VISCA, Panasonic…), elegí su control abajo, en «La cámara».")
                        }
                    }
                }

                // Joystick, zoom, foco y presets.
                Panel {
                    Layout.fillWidth: true
                    Layout.preferredHeight: mandos.implicitHeight + 64
                    title: qsTr("Mover")
                    ColumnLayout {
                        id: mandos
                        anchors.fill: parent
                        spacing: 10
                        Text {
                            visible: !page.conHw
                            Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.textDim; font.pixelSize: 12
                            text: page.cam && page.cam.digital ? qsTr("Mueve el recorte digital (zoom y encuadre en la GPU): esta cámara no tiene el motor configurado.") + " " +
                                                                 (page.esDeRed ? qsTr("Para moverla de verdad: «Activar PTZ real», arriba.") : qsTr("Si es motorizada, elegí su control en «La cámara», abajo."))
                                                               : qsTr("Esta cámara no tiene PTZ: prendé el digital (abajo) o configurá su control.")
                        }
                        Text {
                            visible: page.conHw
                            Layout.fillWidth: true; wrapMode: Text.WordWrap; color: page.cam && page.cam.listo ? Theme.ok : Theme.warn; font.pixelSize: 12
                            text: page.cam ? (page.cam.conf && page.cam.conf.host ? qsTr("Motor: %1 en %2").arg(page.cam.backend.toUpperCase()).arg(page.cam.conf.host)
                                                                                  : qsTr("Motor: %1").arg(page.cam.backend.toUpperCase())) +
                                             " — " + (page.cam.estado || (page.cam.listo ? qsTr("contesta") : qsTr("no contesta"))) : ""
                        }
                        RowLayout {
                            visible: page.conHw && page.cam.digital
                            spacing: 8
                            Text { text: qsTr("El joystick mueve:"); color: Theme.textDim; font.pixelSize: 12 }
                            GlassButton { compact: true; text: qsTr("La cámara (óptico)"); active: !page.mueveDigital
                                          onClicked: page.avisar(page.cc.setModo(page.sel, "optico")) }
                            GlassButton { compact: true; text: qsTr("El recorte (digital)"); active: page.mueveDigital
                                          onClicked: page.avisar(page.cc.setModo(page.sel, "digital")) }
                        }
                        RowLayout {
                            spacing: 24
                            // Joystick: el centro es quieto; cuanto más lejos, más rápido.
                            Rectangle {
                                id: pad
                                visible: page.caps.panTilt === true
                                implicitWidth: 170; implicitHeight: 170; radius: 85
                                color: Qt.rgba(1, 1, 1, 0.05); border.color: Qt.rgba(1, 1, 1, 0.18)
                                property real px: 0
                                property real py: 0
                                Text { anchors { horizontalCenter: parent.horizontalCenter; top: parent.top; topMargin: 6 }
                                       text: "▲"; color: Theme.textFaint }
                                Text { anchors { horizontalCenter: parent.horizontalCenter; bottom: parent.bottom; bottomMargin: 6 }
                                       text: "▼"; color: Theme.textFaint }
                                Text { anchors { verticalCenter: parent.verticalCenter; left: parent.left; leftMargin: 8 }
                                       text: "◀"; color: Theme.textFaint }
                                Text { anchors { verticalCenter: parent.verticalCenter; right: parent.right; rightMargin: 8 }
                                       text: "▶"; color: Theme.textFaint }
                                Rectangle {
                                    width: 46; height: 46; radius: 23
                                    color: area.pressed ? Theme.accent : Qt.rgba(1, 1, 1, 0.22)
                                    x: pad.width / 2 - width / 2 + pad.px * (pad.width / 2 - 23)
                                    y: pad.height / 2 - height / 2 - pad.py * (pad.height / 2 - 23)
                                }
                                Timer {
                                    id: envio
                                    interval: 60; repeat: true; running: area.pressed
                                    onTriggered: page.cc.mover(page.sel, pad.px * page.velocidad, pad.py * page.velocidad, 0)
                                }
                                MouseArea {
                                    id: area
                                    anchors.fill: parent
                                    function leer(m) {
                                        let dx = (m.x - pad.width / 2) / (pad.width / 2 - 23)
                                        let dy = -(m.y - pad.height / 2) / (pad.height / 2 - 23)
                                        const r = Math.sqrt(dx * dx + dy * dy)
                                        if (r > 1) { dx /= r; dy /= r }
                                        // Zona muerta chica en el centro.
                                        pad.px = Math.abs(dx) < 0.08 ? 0 : dx
                                        pad.py = Math.abs(dy) < 0.08 ? 0 : dy
                                    }
                                    onPressed: (m) => { leer(m); page.avisar(page.cc.mover(page.sel, pad.px * page.velocidad, pad.py * page.velocidad, 0)) }
                                    onPositionChanged: (m) => leer(m)
                                    onReleased: { pad.px = 0; pad.py = 0; page.cc.parar(page.sel) }
                                    onCanceled: { pad.px = 0; pad.py = 0; page.cc.parar(page.sel) }
                                }
                            }
                            ColumnLayout {
                                spacing: 8
                                // Zoom y foco: mantener apretado.
                                RowLayout {
                                    visible: page.caps.zoom === true
                                    spacing: 6
                                    Text { text: qsTr("Zoom"); color: Theme.textDim; font.pixelSize: 12; Layout.preferredWidth: 46 }
                                    Repeater {
                                        model: [{t: qsTr("− Alejar"), v: -1}, {t: qsTr("+ Acercar"), v: 1}]
                                        delegate: Rectangle {
                                            required property var modelData
                                            width: 96; height: 34; radius: 10
                                            color: zm.pressed ? Theme.accent : Qt.rgba(1, 1, 1, 0.08)
                                            border.color: Qt.rgba(1, 1, 1, 0.16)
                                            Text { anchors.centerIn: parent; text: modelData.t; color: Theme.text; font.pixelSize: 12 }
                                            MouseArea {
                                                id: zm
                                                anchors.fill: parent
                                                onPressed: page.avisar(page.cc.mover(page.sel, 0, 0, modelData.v * page.velocidad))
                                                onReleased: page.cc.parar(page.sel)
                                                onCanceled: page.cc.parar(page.sel)
                                            }
                                        }
                                    }
                                }
                                RowLayout {
                                    visible: !page.mueveDigital && page.conHw && page.cam.capacidades.foco === true
                                    spacing: 6
                                    Text { text: qsTr("Foco"); color: Theme.textDim; font.pixelSize: 12; Layout.preferredWidth: 46 }
                                    Repeater {
                                        model: [{t: qsTr("Cerca"), v: -1}, {t: qsTr("Lejos"), v: 1}]
                                        delegate: Rectangle {
                                            required property var modelData
                                            width: 96; height: 34; radius: 10
                                            color: fm.pressed ? Theme.accent : Qt.rgba(1, 1, 1, 0.08)
                                            border.color: Qt.rgba(1, 1, 1, 0.16)
                                            Text { anchors.centerIn: parent; text: modelData.t; color: Theme.text; font.pixelSize: 12 }
                                            MouseArea {
                                                id: fm
                                                anchors.fill: parent
                                                onPressed: page.avisar(page.cc.enfocar(page.sel, modelData.v * 0.5))
                                                onReleased: page.cc.enfocar(page.sel, 0)
                                                onCanceled: page.cc.enfocar(page.sel, 0)
                                            }
                                        }
                                    }
                                    GlassButton { compact: true; text: qsTr("Auto"); visible: page.cam !== null && page.cam.capacidades.autoFoco === true
                                                  onClicked: page.avisar(page.cc.autoFoco(page.sel, true)) }
                                    GlassButton { compact: true; text: qsTr("Manual"); visible: page.cam !== null && page.cam.capacidades.autoFoco === true
                                                  onClicked: page.avisar(page.cc.autoFoco(page.sel, false)) }
                                }
                                RowLayout {
                                    spacing: 6
                                    Text { text: qsTr("Velocidad"); color: Theme.textDim; font.pixelSize: 12; Layout.preferredWidth: 46 }
                                    Slider { from: 0.1; to: 1; value: page.velocidad; Layout.preferredWidth: 160
                                             onMoved: page.velocidad = value }
                                    Text { text: Math.round(page.velocidad * 100) + "%"; color: Theme.textFaint; font.pixelSize: 11 }
                                }
                                GlassButton { compact: true; text: page.mueveDigital ? qsTr("⌂ Cuadro completo") : qsTr("⌂ Home")
                                              visible: page.caps.home === true; onClicked: page.avisar(page.cc.home(page.sel)) }
                            }
                        }

                        Text { Layout.fillWidth: true; wrapMode: Text.WordWrap; font.pixelSize: 12
                               color: page.error !== "" ? Theme.live : Theme.textFaint
                               text: page.error !== "" ? page.error
                                     : (page.cam ? (page.mueveDigital ? page.cam.estadoDigital : page.cam.estado) : "") }
                    }
                }

                // Posiciones con nombre: id, nombre, comentario, definición y posición.
                Panel {
                    Layout.fillWidth: true
                    Layout.preferredHeight: posCol.implicitHeight + 64
                    title: qsTr("Posiciones y presets (%1)").arg(page.cam ? page.cam.posiciones.length : 0)
                    ColumnLayout {
                        id: posCol
                        anchors.fill: parent
                        spacing: 8
                        Text { Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.textFaint; font.pixelSize: 11
                               text: qsTr("Guarda donde está ahora la cámara con un nombre: el encuadre digital y, si es motorizada (ONVIF, VISCA, Panasonic), una memoria de la cámara; igual en todas. Cada una tiene su número de preset (los controles y botones que llaman por número van a esa). El nombre y el comentario sirven para pedirla de palabra.") }
                        RowLayout {
                            Layout.fillWidth: true
                            TextField { id: posNombre; Layout.preferredWidth: 180; placeholderText: qsTr("Nombre (p. ej. Atril)") }
                            TextField { id: posComentario; Layout.fillWidth: true; placeholderText: qsTr("Comentario (opcional)") }
                            GlassButton {
                                text: qsTr("Guardar donde está ahora"); active: true; enabled: posNombre.text.trim() !== ""
                                onClicked: {
                                    const r = page.cc.crearPosicion(page.sel, posNombre.text, posComentario.text)
                                    page.avisar(r.ok ? (r.aviso || "") : r.error)
                                    if (r.ok) { posNombre.text = ""; posComentario.text = "" }
                                }
                            }
                        }
                        Repeater {
                            model: page.cam ? page.cam.posiciones : []
                            delegate: Rectangle {
                                id: filaPos
                                required property var modelData
                                property bool editando: false
                                Layout.fillWidth: true
                                implicitHeight: Math.max(filaCol.implicitHeight, 72) + 16
                                radius: 10
                                color: Qt.rgba(1, 1, 1, 0.04)
                                border.color: Qt.rgba(1, 1, 1, 0.08)
                                // La foto de cómo se veía: tocarla = ir.
                                Rectangle {
                                    id: miniPos
                                    anchors { left: parent.left; top: parent.top; margins: 8 }
                                    width: 128; height: 72; radius: 6; color: "black"; clip: true
                                    Image { anchors.fill: parent; fillMode: Image.PreserveAspectCrop; asynchronous: true
                                            source: filaPos.modelData.miniatura_url || "" }
                                    Text { anchors.centerIn: parent; visible: !filaPos.modelData.miniatura_url
                                           text: qsTr("sin foto"); color: Theme.textFaint; font.pixelSize: 11 }
                                    MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor
                                                onClicked: page.siNoAlAire(function() { const r = page.cc.irAPosicion(page.sel, filaPos.modelData.id, -1)
                                                             page.avisar(r.ok ? (r.aviso || "") : r.error) }) }
                                }
                                ColumnLayout {
                                    id: filaCol
                                    anchors { left: miniPos.right; right: parent.right; top: parent.top; margins: 8 }
                                    spacing: 4
                                    RowLayout {
                                        Layout.fillWidth: true
                                        // Su número de preset (editable).
                                        Text { visible: !filaPos.editando; text: qsTr("N.º %1").arg(filaPos.modelData.numero || "—"); color: Theme.accentSoft
                                               font { pixelSize: 12; weight: Font.DemiBold } }
                                        SpinBox { id: edNumero; visible: filaPos.editando; from: 1; to: 999; editable: true
                                                  value: filaPos.modelData.numero || 1; Layout.preferredWidth: 96
                                                  ToolTip.visible: hovered; ToolTip.text: qsTr("Número de preset (único en la cámara)") }
                                        Text { visible: !filaPos.editando; text: filaPos.modelData.nombre; color: Theme.text
                                               font { pixelSize: 14; weight: Font.DemiBold } Layout.fillWidth: true; elide: Text.ElideRight }
                                        TextField { id: edNombre; visible: filaPos.editando; Layout.preferredWidth: 180; text: filaPos.modelData.nombre }
                                        TextField { id: edComentario; visible: filaPos.editando; Layout.fillWidth: true; text: filaPos.modelData.comentario || "" }
                                        GlassButton { compact: true; text: qsTr("Ir"); active: true; visible: !filaPos.editando
                                                      onClicked: page.siNoAlAire(function() { const r = page.cc.irAPosicion(page.sel, filaPos.modelData.id, -1)
                                                                   page.avisar(r.ok ? (r.aviso || "") : r.error) }) }
                                        GlassButton { compact: true; text: qsTr("Foto"); visible: !filaPos.editando
                                                      ToolTip.visible: hovered; ToolTip.delay: 400; ToolTip.text: qsTr("Vuelve a sacar la foto con lo que se ve ahora")
                                                      onClicked: page.avisar(page.cc.capturarMiniatura(page.sel, filaPos.modelData.id)) }
                                        GlassButton { compact: true; text: qsTr("Regrabar"); visible: !filaPos.editando
                                                      onClicked: { const r = page.cc.editarPosicion(page.sel, filaPos.modelData.id, {regrabar: true})
                                                                   page.avisar(r.ok ? (r.aviso || "") : r.error) } }
                                        GlassButton { compact: true; text: filaPos.editando ? qsTr("Listo") : qsTr("Editar")
                                                      onClicked: {
                                                          if (filaPos.editando) {
                                                              const r = page.cc.editarPosicion(page.sel, filaPos.modelData.id,
                                                                                               {nombre: edNombre.text, comentario: edComentario.text, numero: edNumero.value,
                                                                                                velocidad: edVelocidad.value, segundos: edSegundos.value / 10})
                                                              page.avisar(r.ok ? "" : r.error)
                                                              if (r.ok) filaPos.editando = false
                                                          } else filaPos.editando = true
                                                      } }
                                        GlassButton { compact: true; text: qsTr("Borrar"); visible: !filaPos.editando
                                                      onClicked: {
                                                          const cc = page.cc, entrada = page.sel, id = filaPos.modelData.id, nombre = filaPos.modelData.nombre
                                                          ApplicationWindow.window.confirm({
                                                              title: qsTr("¿Borrar la posición «%1»?").arg(nombre),
                                                              text: qsTr("Se borra de la lista y se libera su memoria en la cámara."),
                                                              accept: qsTr("Borrar"), reject: qsTr("Cancelar"),
                                                              action: function() { page.avisar(cc.borrarPosicion(entrada, id)) }
                                                          })
                                                      } }
                                    }
                                    RowLayout {
                                        visible: filaPos.editando
                                        Layout.fillWidth: true
                                        Text { text: qsTr("Velocidad de la cabeza"); color: Theme.textDim; font.pixelSize: 11 }
                                        Slider { id: edVelocidad; from: 0; to: 1; Layout.preferredWidth: 140
                                                 value: filaPos.modelData.velocidad || 0 }
                                        Text { text: edVelocidad.value < 0.01 ? qsTr("la de la cámara") : Math.round(edVelocidad.value * 100) + " %"
                                               color: Theme.textDim; font.pixelSize: 11; Layout.preferredWidth: 90 }
                                        Text { text: qsTr("Recorte en"); color: Theme.textDim; font.pixelSize: 11 }
                                        // En décimas de segundo; -1 décima = sin fijar (usa lo que diga quien la llama).
                                        SpinBox { id: edSegundos; from: -1; to: 100; editable: true
                                                  value: filaPos.modelData.segundos !== undefined ? Math.round(filaPos.modelData.segundos * 10) : -1
                                                  textFromValue: function(v) { return v < 0 ? "—" : (v / 10).toFixed(1) + " s" }
                                                  valueFromText: function(t) { const n = parseFloat(t.replace(",", ".")); return isNaN(n) ? -1 : Math.round(n * 10) } }
                                    }
                                    Text { visible: !filaPos.editando && (filaPos.modelData.comentario || "") !== ""
                                           text: filaPos.modelData.comentario || ""; color: Theme.textDim; font.pixelSize: 12
                                           Layout.fillWidth: true; wrapMode: Text.WordWrap }
                                    Text {
                                        Layout.fillWidth: true; color: Theme.textFaint; font.pixelSize: 11; wrapMode: Text.WordWrap
                                        text: {
                                            const m = filaPos.modelData
                                            const partes = []
                                            if (m.definicion) partes.push(m.definicion.ancho + "×" + m.definicion.alto)
                                            if (m.velocidad) partes.push(qsTr("cabeza al %1 %").arg(Math.round(m.velocidad * 100)))
                                            if (m.segundos !== undefined) partes.push(qsTr("recorte en %1 s").arg(m.segundos.toFixed(1)))
                                            if (m.digital) partes.push(qsTr("digital %1× en %2, %3").arg(m.digital.zoom.toFixed(2)).arg(m.digital.x.toFixed(2)).arg(m.digital.y.toFixed(2)))
                                            if (m.camara) partes.push(m.camara.backend.toUpperCase() + (m.camara.memoria ? " " + qsTr("memoria %1").arg(m.camara.memoria) : m.camara.token ? " " + qsTr("token %1").arg(m.camara.token) : " " + qsTr("(grabando…)")))
                                            return partes.join("  ·  ")
                                        }
                                    }
                                }
                            }
                        }
                    }
                }

                // La cámara: backend y su configuración.
                Panel {
                    id: panelHw
                    Layout.fillWidth: true
                    Layout.preferredHeight: hwCol.implicitHeight + 64
                    title: qsTr("La cámara: su control (motor)")
                    property string backend: page.cam ? page.cam.backend : "ninguno"
                    property var valores: ({})
                    function definicion() {
                        const l = page.cc.backends
                        for (let i = 0; i < l.length; ++i) if (l[i].id === backend) return l[i]
                        return l[0]
                    }
                    Connections {
                        target: page
                        function onSelChanged() { panelHw.cargar() }
                    }
                    function cargar() {
                        backend = page.cam ? page.cam.backend : "ninguno"
                        valores = page.cam ? Object.assign({}, page.cam.conf) : ({})
                    }
                    Component.onCompleted: cargar()
                    ColumnLayout {
                        id: hwCol
                        anchors.fill: parent
                        spacing: 8
                        RowLayout {
                            Layout.fillWidth: true
                            Text { text: qsTr("Control"); color: Theme.textDim; font.pixelSize: 12; Layout.preferredWidth: 110 }
                            ComboBox {
                                id: backendBox
                                Layout.fillWidth: true
                                model: page.cc ? page.cc.backends : []
                                textRole: "nombre"
                                valueRole: "id"
                                currentIndex: { const l = page.cc ? page.cc.backends : []
                                                for (let i = 0; i < l.length; ++i) if (l[i].id === panelHw.backend) return i
                                                return 0 }
                                onActivated: panelHw.backend = currentValue
                            }
                        }
                        Text { Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.textFaint; font.pixelSize: 11
                               text: panelHw.definicion().ayuda }
                        Repeater {
                            model: panelHw.definicion().campos
                            delegate: RowLayout {
                                required property var modelData
                                Layout.fillWidth: true
                                Text { text: modelData.etiqueta; color: Theme.textDim; font.pixelSize: 12; Layout.preferredWidth: 110
                                       wrapMode: Text.WordWrap }
                                TextField {
                                    visible: modelData.tipo !== "choice"
                                    Layout.fillWidth: true
                                    echoMode: modelData.tipo === "password" ? TextInput.Password : TextInput.Normal
                                    placeholderText: modelData.tipo === "password" && page.cam && page.cam.tieneClave ? qsTr("(guardada; vacío = no cambiar)") : ""
                                    text: panelHw.valores[modelData.clave] !== undefined ? String(panelHw.valores[modelData.clave]) : ""
                                    onTextEdited: { const v = panelHw.valores; v[modelData.clave] = text; panelHw.valores = v }
                                }
                                ComboBox {
                                    visible: modelData.tipo === "choice"
                                    Layout.fillWidth: true
                                    model: modelData.opciones || []
                                    textRole: "texto"
                                    valueRole: "valor"
                                    currentIndex: { const o = modelData.opciones || []
                                                    for (let i = 0; i < o.length; ++i) if (o[i].valor === panelHw.valores[modelData.clave]) return i
                                                    return 0 }
                                    onActivated: { const v = panelHw.valores; v[modelData.clave] = currentValue; panelHw.valores = v }
                                }
                            }
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            GlassButton {
                                text: qsTr("Aplicar"); active: true
                                onClicked: {
                                    const v = Object.assign({}, panelHw.valores)
                                    // Lo que no se tocó del combo vale su primera opción.
                                    const campos = panelHw.definicion().campos
                                    for (let i = 0; i < campos.length; ++i)
                                        if (campos[i].tipo === "choice" && v[campos[i].clave] === undefined) v[campos[i].clave] = campos[i].opciones[0].valor
                                    page.avisar(page.cc.configurar(page.sel, panelHw.backend, v))
                                }
                            }
                            GlassButton {
                                visible: panelHw.backend === "onvif"
                                text: page.cc && page.cc.buscando ? qsTr("Buscando…") : qsTr("Buscar en la red")
                                enabled: page.cc && !page.cc.buscando
                                onClicked: page.cc.buscarOnvif()
                            }
                            Item { Layout.fillWidth: true }
                        }
                        Repeater {
                            model: panelHw.backend === "onvif" && page.cc ? page.cc.encontradas : []
                            delegate: Rectangle {
                                required property var modelData
                                Layout.fillWidth: true
                                height: 40; radius: 8; color: Qt.rgba(1, 1, 1, 0.05)
                                RowLayout {
                                    anchors { fill: parent; margins: 8 }
                                    Text { text: (modelData.nombre || qsTr("Cámara")) + (modelData.modelo ? " · " + modelData.modelo : "")
                                           color: Theme.text; font.pixelSize: 12; Layout.fillWidth: true; elide: Text.ElideRight }
                                    Text { text: modelData.host; color: Theme.textDim; font.pixelSize: 12 }
                                    GlassButton { compact: true; text: qsTr("Usar")
                                                  onClicked: { const v = panelHw.valores; v.host = modelData.direccion; panelHw.valores = v } }
                                }
                            }
                        }
                    }
                }

                // Digital e imagen.
                Panel {
                    Layout.fillWidth: true
                    Layout.preferredHeight: digCol.implicitHeight + 64
                    title: qsTr("Digital e imagen")
                    ColumnLayout {
                        id: digCol
                        anchors.fill: parent
                        spacing: 10
                        RowLayout {
                            Layout.fillWidth: true
                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 2
                                Text { text: qsTr("PTZ digital"); color: Theme.text; font.pixelSize: 13 }
                                Text { Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.textFaint; font.pixelSize: 11
                                       text: qsTr("Recorte y zoom en la GPU, en cualquier cámara. Sin zoom no cuesta nada.") }
                            }
                            Toggle { checked: page.cam ? page.cam.digital : false
                                     onToggled: (w) => page.avisar(page.cc.setDigital(page.sel, w)) }
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 2
                                Text { text: qsTr("Mejorar imagen"); color: Theme.text; font.pixelSize: 13 }
                                Text { Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.textFaint; font.pixelSize: 11
                                       text: qsTr("Limpia el ruido, pega el color a los bordes y afila. Lo general (y la intensidad) está en Configuración → Cámaras y entradas.") }
                            }
                            ComboBox {
                                Layout.preferredWidth: 220
                                model: [{v: "general", t: page.cc && page.cc.mejoraGeneral ? qsTr("Como lo general (prendido)") : qsTr("Como lo general (apagado)")},
                                        {v: "si", t: qsTr("Siempre en esta cámara")},
                                        {v: "no", t: qsTr("Nunca en esta cámara")}]
                                textRole: "t"
                                valueRole: "v"
                                currentIndex: page.cam ? ["general", "si", "no"].indexOf(page.cam.mejora) : 0
                                onActivated: page.avisar(page.cc.setMejora(page.sel, currentValue))
                            }
                        }
                    }
                }
            }
        }
        Panel {
            visible: page.verRecinto
            Layout.fillWidth: true
            Layout.fillHeight: true
            title: qsTr("Recinto") + (Studio.cuerpo.nombre ? " · " + Studio.cuerpo.nombre : "")
            ScrollView {
                anchors.fill: parent
                contentWidth: availableWidth
                clip: true
                RecintoGrilla { width: parent.width }
            }
        }
        Item {
            visible: page.cam === null && !page.verRecinto
            Layout.fillWidth: true
            Layout.fillHeight: true
            Text { anchors.centerIn: parent; color: Theme.textFaint; font.pixelSize: 14
                   text: qsTr("Elegí una cámara de la lista.") }
        }
    }
}
