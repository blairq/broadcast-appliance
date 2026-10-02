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
    readonly property bool mueveDigital: cam !== null && cam.modo === "digital"
    // Lo que puede lo que mueve el joystick ahora.
    readonly property var caps: cam === null ? ({}) : (mueveDigital ? cam.capacidadesDigital : cam.capacidades)
    property real velocidad: 0.6
    property bool guardando: false
    property string error: ""
    property var pos: ({})

    onVisibleChanged: inp.setActive(visible)
    Component.onCompleted: {
        inp.setActive(visible)
        if (cc && cc.camaras.length > 0) sel = cc.camaras[0].entrada
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

    RowLayout {
        anchors { fill: parent; leftMargin: page.leftInset; margins: 20 }
        spacing: 16

        // --- Las entradas ---------------------------------------------------------------------
        Panel {
            Layout.preferredWidth: 280
            Layout.fillHeight: true
            title: "Cámaras (" + (page.cc ? page.cc.camaras.length : 0) + ")"
            ListView {
                id: lista
                anchors.fill: parent
                clip: true
                spacing: 8
                model: page.cc ? page.cc.camaras : []
                Text { anchors.centerIn: parent; width: parent.width - 20; visible: lista.count === 0; wrapMode: Text.WordWrap
                       horizontalAlignment: Text.AlignHCenter; color: Theme.textFaint; font.pixelSize: 13
                       text: "No hay entradas de video. Agregá una cámara en Entradas." }
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
                                Text { text: modelData.backend === "ninguno" ? (modelData.digital ? "Digital" : "Sin PTZ")
                                                                            : modelData.backend.toUpperCase() + (modelData.digital ? " + digital" : "")
                                       color: Theme.textDim; font.pixelSize: 11 }
                            }
                            Text { visible: modelData.mejoraActiva; text: "✦ Mejorar imagen"; color: Theme.accentSoft; font.pixelSize: 10 }
                        }
                    }
                    MouseArea { anchors.fill: parent; onClicked: { page.sel = modelData.entrada; page.error = "" } }
                }
            }
        }

        // --- El control ---------------------------------------------------------------------
        ScrollView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            contentWidth: availableWidth
            clip: true
            visible: page.cam !== null

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
                        Rectangle {
                            Layout.fillWidth: true
                            Layout.preferredHeight: width * 9 / 16
                            Layout.maximumHeight: 340
                            radius: 10; color: "black"; clip: true
                            Image { anchors.fill: parent; fillMode: Image.PreserveAspectFit; cache: false
                                    source: page.cam ? "image://inputs/" + encodeURIComponent(page.sel) + "?" + page.inp.stamp : "" }
                            StatusPill { anchors { left: parent.left; top: parent.top; margins: 8 }
                                         text: page.mueveDigital ? "DIGITAL" : "ÓPTICO"; dot: page.cam && page.cam.listo ? Theme.ok : Theme.warn }
                        }
                        // Mapa: el cuadro entero y la ventana del recorte. Clic = centrar ahí.
                        ColumnLayout {
                            visible: page.cam !== null && page.cam.digital
                            Layout.preferredWidth: 200
                            spacing: 6
                            Text { text: "Encuadre digital"; color: Theme.textDim; font.pixelSize: 12 }
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
                            Text { text: "Zoom " + (page.pos.zoom ? page.pos.zoom.toFixed(2) : "1.00") + "×  ·  clic: centrar, rueda: zoom"
                                   color: Theme.textFaint; font.pixelSize: 10; Layout.preferredWidth: 200; wrapMode: Text.WordWrap }
                            GlassButton { compact: true; text: "Cuadro completo"; onClicked: page.avisar(page.cc.encuadre(page.sel, 0.5, 0.5, 1, 0.8)) }
                        }
                    }
                }

                // Joystick, zoom, foco y presets.
                Panel {
                    Layout.fillWidth: true
                    Layout.preferredHeight: mandos.implicitHeight + 64
                    title: "Mover"
                    ColumnLayout {
                        id: mandos
                        anchors.fill: parent
                        spacing: 10
                        RowLayout {
                            visible: page.conHw && page.cam.digital
                            spacing: 8
                            Text { text: "El joystick mueve:"; color: Theme.textDim; font.pixelSize: 12 }
                            GlassButton { compact: true; text: "La cámara (óptico)"; active: !page.mueveDigital
                                          onClicked: page.avisar(page.cc.setModo(page.sel, "optico")) }
                            GlassButton { compact: true; text: "El recorte (digital)"; active: page.mueveDigital
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
                                    Text { text: "Zoom"; color: Theme.textDim; font.pixelSize: 12; Layout.preferredWidth: 46 }
                                    Repeater {
                                        model: [{t: "− Alejar", v: -1}, {t: "+ Acercar", v: 1}]
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
                                    Text { text: "Foco"; color: Theme.textDim; font.pixelSize: 12; Layout.preferredWidth: 46 }
                                    Repeater {
                                        model: [{t: "Cerca", v: -1}, {t: "Lejos", v: 1}]
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
                                    GlassButton { compact: true; text: "Auto"; visible: page.cam !== null && page.cam.capacidades.autoFoco === true
                                                  onClicked: page.avisar(page.cc.autoFoco(page.sel, true)) }
                                    GlassButton { compact: true; text: "Manual"; visible: page.cam !== null && page.cam.capacidades.autoFoco === true
                                                  onClicked: page.avisar(page.cc.autoFoco(page.sel, false)) }
                                }
                                RowLayout {
                                    spacing: 6
                                    Text { text: "Velocidad"; color: Theme.textDim; font.pixelSize: 12; Layout.preferredWidth: 46 }
                                    Slider { from: 0.1; to: 1; value: page.velocidad; Layout.preferredWidth: 160
                                             onMoved: page.velocidad = value }
                                    Text { text: Math.round(page.velocidad * 100) + "%"; color: Theme.textFaint; font.pixelSize: 11 }
                                }
                                GlassButton { compact: true; text: page.mueveDigital ? "⌂ Cuadro completo" : "⌂ Home"
                                              visible: page.caps.home === true; onClicked: page.avisar(page.cc.home(page.sel)) }
                            }
                        }

                        // Presets.
                        RowLayout {
                            visible: (page.caps.presets || 0) > 0
                            spacing: 8
                            Text { text: page.mueveDigital ? "Presets digitales" : "Presets de la cámara"; color: Theme.textDim; font.pixelSize: 12 }
                            Item { Layout.fillWidth: true }
                            GlassButton { compact: true; text: page.guardando ? "Tocá un número para guardar" : "Guardar…"
                                          active: page.guardando; onClicked: page.guardando = !page.guardando }
                        }
                        GridLayout {
                            visible: (page.caps.presets || 0) > 0
                            columns: 8
                            rowSpacing: 6; columnSpacing: 6
                            Repeater {
                                model: Math.min(16, page.caps.presets || 0)
                                delegate: Rectangle {
                                    required property int index
                                    readonly property int numero: index + 1
                                    readonly property bool lleno: !page.mueveDigital || (page.cam && page.cam.presetsDigitales.indexOf(numero) >= 0)
                                    width: 52; height: 40; radius: 10
                                    color: pm.pressed ? Theme.accent : page.guardando ? Qt.rgba(1, 0.42, 0.17, 0.14)
                                                                                     : lleno ? Qt.rgba(1, 1, 1, 0.1) : Qt.rgba(1, 1, 1, 0.03)
                                    border.color: page.guardando ? Theme.accent : Qt.rgba(1, 1, 1, lleno ? 0.22 : 0.08)
                                    Text { anchors.centerIn: parent; text: numero; color: lleno || page.guardando ? Theme.text : Theme.textFaint
                                           font { pixelSize: 15; weight: Font.DemiBold } }
                                    MouseArea {
                                        id: pm
                                        anchors.fill: parent
                                        onClicked: {
                                            if (page.guardando) { page.avisar(page.cc.guardarPreset(page.sel, numero)); page.guardando = false }
                                            else page.avisar(page.cc.irPreset(page.sel, numero))
                                        }
                                    }
                                }
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
                    title: "Posiciones (" + (page.cam ? page.cam.posiciones.length : 0) + ")"
                    ColumnLayout {
                        id: posCol
                        anchors.fill: parent
                        spacing: 8
                        Text { Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.textFaint; font.pixelSize: 11
                               text: "Guarda donde está ahora la cámara con un nombre: el encuadre digital y, si es motorizada, una memoria de la cámara. Se vuelve con «Ir»." }
                        RowLayout {
                            Layout.fillWidth: true
                            TextField { id: posNombre; Layout.preferredWidth: 180; placeholderText: "Nombre (p. ej. Atril)" }
                            TextField { id: posComentario; Layout.fillWidth: true; placeholderText: "Comentario (opcional)" }
                            GlassButton {
                                text: "Guardar donde está ahora"; active: true; enabled: posNombre.text.trim() !== ""
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
                                implicitHeight: filaCol.implicitHeight + 16
                                radius: 10
                                color: Qt.rgba(1, 1, 1, 0.04)
                                border.color: Qt.rgba(1, 1, 1, 0.08)
                                ColumnLayout {
                                    id: filaCol
                                    anchors { left: parent.left; right: parent.right; top: parent.top; margins: 8 }
                                    spacing: 4
                                    RowLayout {
                                        Layout.fillWidth: true
                                        Text { text: "#" + filaPos.modelData.id; color: Theme.textFaint; font.pixelSize: 11 }
                                        Text { visible: !filaPos.editando; text: filaPos.modelData.nombre; color: Theme.text
                                               font { pixelSize: 14; weight: Font.DemiBold } Layout.fillWidth: true; elide: Text.ElideRight }
                                        TextField { id: edNombre; visible: filaPos.editando; Layout.preferredWidth: 180; text: filaPos.modelData.nombre }
                                        TextField { id: edComentario; visible: filaPos.editando; Layout.fillWidth: true; text: filaPos.modelData.comentario || "" }
                                        GlassButton { compact: true; text: "Ir"; active: true; visible: !filaPos.editando
                                                      onClicked: { const r = page.cc.irAPosicion(page.sel, filaPos.modelData.id, 1.0)
                                                                   page.avisar(r.ok ? (r.aviso || "") : r.error) } }
                                        GlassButton { compact: true; text: "Regrabar"; visible: !filaPos.editando
                                                      onClicked: { const r = page.cc.editarPosicion(page.sel, filaPos.modelData.id, {regrabar: true})
                                                                   page.avisar(r.ok ? (r.aviso || "") : r.error) } }
                                        GlassButton { compact: true; text: filaPos.editando ? "Listo" : "Editar"
                                                      onClicked: {
                                                          if (filaPos.editando) {
                                                              const r = page.cc.editarPosicion(page.sel, filaPos.modelData.id,
                                                                                               {nombre: edNombre.text, comentario: edComentario.text})
                                                              page.avisar(r.ok ? "" : r.error)
                                                              if (r.ok) filaPos.editando = false
                                                          } else filaPos.editando = true
                                                      } }
                                        GlassButton { compact: true; text: "Borrar"; visible: !filaPos.editando
                                                      onClicked: {
                                                          const cc = page.cc, entrada = page.sel, id = filaPos.modelData.id, nombre = filaPos.modelData.nombre
                                                          ApplicationWindow.window.confirm({
                                                              title: "¿Borrar la posición «" + nombre + "»?",
                                                              text: "Se borra de la lista y se libera su memoria en la cámara.",
                                                              accept: "Borrar", reject: "Cancelar",
                                                              action: function() { page.avisar(cc.borrarPosicion(entrada, id)) }
                                                          })
                                                      } }
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
                                            if (m.digital) partes.push("digital " + m.digital.zoom.toFixed(2) + "× en " + m.digital.x.toFixed(2) + ", " + m.digital.y.toFixed(2))
                                            if (m.camara) partes.push(m.camara.backend.toUpperCase() + (m.camara.memoria ? " memoria " + m.camara.memoria : m.camara.token ? " token " + m.camara.token : " (grabando…)"))
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
                    title: "La cámara"
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
                            Text { text: "Control"; color: Theme.textDim; font.pixelSize: 12; Layout.preferredWidth: 110 }
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
                                    placeholderText: modelData.tipo === "password" && page.cam && page.cam.tieneClave ? "(guardada; vacío = no cambiar)" : ""
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
                                text: "Aplicar"; active: true
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
                                text: page.cc && page.cc.buscando ? "Buscando…" : "Buscar en la red"
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
                                    Text { text: (modelData.nombre || "Cámara") + (modelData.modelo ? " · " + modelData.modelo : "")
                                           color: Theme.text; font.pixelSize: 12; Layout.fillWidth: true; elide: Text.ElideRight }
                                    Text { text: modelData.host; color: Theme.textDim; font.pixelSize: 12 }
                                    GlassButton { compact: true; text: "Usar"
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
                    title: "Digital e imagen"
                    ColumnLayout {
                        id: digCol
                        anchors.fill: parent
                        spacing: 10
                        RowLayout {
                            Layout.fillWidth: true
                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 2
                                Text { text: "PTZ digital"; color: Theme.text; font.pixelSize: 13 }
                                Text { Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.textFaint; font.pixelSize: 11
                                       text: "Recorte y zoom en la GPU, en cualquier cámara. Sin zoom no cuesta nada." }
                            }
                            Toggle { checked: page.cam ? page.cam.digital : false
                                     onToggled: (w) => page.avisar(page.cc.setDigital(page.sel, w)) }
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 2
                                Text { text: "Mejorar imagen"; color: Theme.text; font.pixelSize: 13 }
                                Text { Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.textFaint; font.pixelSize: 11
                                       text: "Limpia el ruido, pega el color a los bordes y afila. Lo general (y la intensidad) está en Configuración → Video." }
                            }
                            ComboBox {
                                Layout.preferredWidth: 220
                                model: [{v: "general", t: "Como lo general (" + (page.cc && page.cc.mejoraGeneral ? "prendido" : "apagado") + ")"},
                                        {v: "si", t: "Siempre en esta cámara"},
                                        {v: "no", t: "Nunca en esta cámara"}]
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
        Item {
            visible: page.cam === null
            Layout.fillWidth: true
            Layout.fillHeight: true
            Text { anchors.centerIn: parent; color: Theme.textFaint; font.pixelSize: 14
                   text: "Elegí una cámara de la lista." }
        }
    }
}
