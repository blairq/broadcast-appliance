// El control PTZ en su ventana (PtzContent con enVentana): las imágenes arriba,
// los puntos prefijados siempre a la vista y los controles al costado.
//
//   DIGITAL   la TOMA ENTERA (antes del recorte) grande, con el recuadro de lo que
//             sale: clic centra, arrastrar encuadra, agarrar el recuadro lo mueve,
//             rueda acerca. Al lado, chica, lo que sale (con el recorte).
//   ÓPTICO    la imagen en vivo grande (clic apunta, arrastrar encuadra) y una
//             FOTO: tocarla lleva la cámara a ese punto, aunque se haya movido
//             (vuelve a donde se sacó y apunta; ControlCamaras::apuntarEnFoto).
//
// ⚙ Configuración despliega grabar, regrabar, sacar foto y borrar puntos, y el
// seguimiento de caras; cerrada, la ventana queda más chica.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import BA

ColumnLayout {
    id: v
    /// La tarjeta (PtzContent): cámara, posiciones, caras, avisar(). (Quien la pasa no puede
    /// escribir `tarjeta: t`: `t` es también de acá; PtzContent da `estaTarjeta`.)
    required property var tarjeta
    readonly property var t: tarjeta
    property bool configurar: false
    property var fotoInfo: ({ hay: false })
    spacing: 8

    function leerFoto() { v.fotoInfo = v.t.cc && v.t.entrada !== "" ? v.t.cc.foto(v.t.entrada) : ({ hay: false }) }
    Component.onCompleted: leerFoto()
    Connections { target: v.t.cc; function onCambio() { v.leerFoto() } }
    // «movida» cambia sin aviso (la mueve el joystick): se relee seguido mientras se mira.
    Timer { interval: 700; repeat: true; running: v.visible && !v.t.mueveDigital; onTriggered: v.leerFoto() }

    /// Mueve si se puede; si la cámara está al aire, pregunta primero (la regla es del modelo).
    function conPermiso(fn) {
        const motivo = v.t.cc.chequearMovimiento(v.t.entrada, false, "")
        if (motivo === "") { fn(); return }
        ApplicationWindow.window.confirm({ title: qsTr("La cámara está al aire"), text: qsTr("%1 ¿Mover igual?").arg(motivo),
                                           accept: qsTr("Mover igual"), reject: qsTr("Cancelar"), action: fn })
    }
    function irA(p) {
        v.conPermiso(function() { const r = v.t.cc.irAPosicion(v.t.entrada, p.id, -1); v.t.avisar(r.ok ? (r.aviso || "") : r.error) })
    }
    // Digital: el encuadre se da en coordenadas de la toma entera.
    readonly property real zoom: v.t.pos.zoom || 1
    function encuadrar(x, y, z, seg) { v.t.avisar(v.t.cc.encuadre(v.t.entrada, x, y, Math.max(1, Math.min(8, z)), seg)) }

    // --- Arriba: cámara, qué se mueve, foto, configuración --------------------------------
    RowLayout {
        Layout.fillWidth: true
        spacing: 6
        Text {
            Layout.fillWidth: true
            text: v.t.entrada
            elide: Text.ElideRight
            color: Theme.text; font { pixelSize: 15; weight: Font.DemiBold }
        }
        StatusPill { text: v.t.mueveDigital ? qsTr("DIGITAL") : qsTr("ÓPTICO"); dot: v.t.cam && v.t.cam.listo ? Theme.ok : Theme.warn }
        GlassButton {
            visible: !v.t.mueveDigital
            compact: true; text: qsTr("Sacar foto"); icon: "📷"
            onClicked: { const r = v.t.cc.sacarFoto(v.t.entrada); v.t.avisar(r.ok ? (r.aviso || "") : r.error) }
            ToolTip.visible: hovered; ToolTip.delay: 400
            ToolTip.text: qsTr("Una foto de lo que ve ahora (mejor bien abierta): después, tocar un punto de la foto lleva la cámara ahí")
        }
        GlassButton {
            compact: true; text: qsTr("Configuración"); icon: "⚙"
            active: v.configurar
            onClicked: v.configurar = !v.configurar
            ToolTip.visible: hovered; ToolTip.delay: 400
            ToolTip.text: v.configurar ? qsTr("Achicar: solo las imágenes, los puntos y el joystick") : qsTr("Grabar, regrabar y borrar puntos prefijados; seguimiento de caras")
        }
    }

    // --- Las imágenes y los controles ---------------------------------------------------
    RowLayout {
        Layout.fillWidth: true
        Layout.fillHeight: true
        Layout.minimumHeight: 220
        spacing: 8

        // La grande: la toma entera (digital) o en vivo (óptico).
        ImagenApuntable {
            id: grande
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.preferredWidth: 3
            fuente: v.t.mueveDigital ? "entrada-entera:" + v.t.entrada : "entrada:" + v.t.entrada
            titulo: v.t.mueveDigital ? qsTr("TOMA ENTERA") : qsTr("EN VIVO")
            ayuda: v.t.mueveDigital ? qsTr("clic: centrar · arrastrar: encuadrar · agarrar el recuadro: moverlo · rueda: zoom · doble clic: todo")
                                    : qsTr("clic: apuntar · arrastrar: encuadrar · rueda: zoom (aproximado en cámaras motorizadas)")
            // Digital: el recuadro de lo que sale, para agarrarlo.
            arrastrable: v.t.mueveDigital ? { x: v.t.pos.x || 0.5, y: v.t.pos.y || 0.5, w: 1 / v.zoom, h: 1 / v.zoom } : null
            Rectangle {
                visible: v.t.mueveDigital && grande.hayImagen
                x: grande.ix + ((v.t.pos.x || 0.5) - 0.5 / v.zoom) * grande.iw
                y: grande.iy + ((v.t.pos.y || 0.5) - 0.5 / v.zoom) * grande.ih
                width: grande.iw / v.zoom
                height: grande.ih / v.zoom
                color: "transparent"
                border { color: Theme.live; width: 2 }
                Text {
                    anchors { left: parent.left; bottom: parent.top; bottomMargin: 2 }
                    text: qsTr("SALE · %1×").arg(v.zoom.toFixed(2))
                    color: Theme.live; font { pixelSize: 10; weight: Font.DemiBold }
                    style: Text.Outline; styleColor: Qt.rgba(0, 0, 0, 0.8)
                }
            }
            // Las caras analizadas, sobre la imagen en vivo (óptico: es lo que ve la cámara).
            Repeater {
                model: v.t.mueveDigital ? [] : v.t.caras
                delegate: Rectangle {
                    required property var modelData
                    required property int index
                    x: grande.ix + modelData.x * grande.iw
                    y: grande.iy + modelData.y * grande.ih
                    width: modelData.ancho * grande.iw
                    height: modelData.alto * grande.ih
                    color: "transparent"
                    border.width: index === v.t.elegida ? 3 : 2
                    border.color: index === v.t.elegida ? Theme.accent : (modelData.nombre ? "#4fc3f7" : Qt.rgba(1, 1, 1, 0.7))
                    Text {
                        anchors { left: parent.left; bottom: parent.top; bottomMargin: 2 }
                        text: v.t.nombreCara(modelData, index)
                        color: parent.border.color; font.pixelSize: 11
                        style: Text.Outline; styleColor: Qt.rgba(0, 0, 0, 0.8)
                    }
                }
            }
            onClic: (x, y) => v.conPermiso(function() {
                if (v.t.mueveDigital) v.encuadrar(x, y, v.zoom, 0.3)
                else v.t.avisar(v.t.cc.apuntar(v.t.entrada, x, y, 0, 0))
            })
            onCuadro: (x, y, w, h) => v.conPermiso(function() {
                if (v.t.mueveDigital) v.encuadrar(x, y, 1 / Math.max(w, h), 0.4)
                else v.t.avisar(v.t.cc.apuntar(v.t.entrada, x, y, w, h))
            })
            onMovido: (x, y) => { if (v.t.mueveDigital) { arrastre.x = x; arrastre.y = y; if (!arrastre.running) arrastre.start() } }
            onSoltado: { arrastre.stop(); if (v.t.mueveDigital) v.encuadrar(arrastre.x, arrastre.y, v.zoom, 0) }
            onRueda: (s) => {
                if (v.t.mueveDigital) v.encuadrar(v.t.pos.x || 0.5, v.t.pos.y || 0.5, v.zoom * (s > 0 ? 1.15 : 1 / 1.15), 0.15)
                else { v.t.avisar(v.t.cc.mover(v.t.entrada, 0, 0, s * v.t.velocidad)); pararZoom.restart() }
            }
            onDobleClic: if (v.t.mueveDigital) v.conPermiso(function() { v.encuadrar(0.5, 0.5, 1, 0.8) })
            // Mientras se arrastra el recuadro, el encuadre lo sigue (como mucho 30 veces por segundo).
            Timer {
                id: arrastre
                property real x: 0.5
                property real y: 0.5
                interval: 33; repeat: true
                onTriggered: v.encuadrar(x, y, v.zoom, 0)
            }
            Timer { id: pararZoom; interval: 180; onTriggered: v.t.cc.parar(v.t.entrada) }
        }

        // Al costado: lo que sale (digital) o la foto (óptico), y el joystick.
        ColumnLayout {
            Layout.fillHeight: true
            Layout.preferredWidth: 2
            Layout.fillWidth: true
            Layout.maximumWidth: 420
            spacing: 8

            ImagenApuntable {
                id: chica
                Layout.fillWidth: true
                Layout.preferredHeight: width * 9 / 16
                fuente: v.t.mueveDigital ? "entrada:" + v.t.entrada : (v.fotoInfo.hay ? v.fotoInfo.url : "")
                titulo: v.t.mueveDigital ? qsTr("LO QUE SALE") : qsTr("FOTO")
                ayuda: v.t.mueveDigital ? qsTr("clic: centrar en ese punto · arrastrar: encuadrar")
                       : !v.fotoInfo.hay ? qsTr("«Sacar foto» (arriba) con la cámara bien abierta")
                       : !v.fotoInfo.movida ? qsTr("clic: apuntar · arrastrar: encuadrar")
                       : v.fotoInfo.conMemoria ? qsTr("se movió: al tocar, vuelve a donde se sacó y apunta")
                                               : qsTr("se movió y no tiene memoria para volver: sacá otra")
                onClic: (x, y) => v.conPermiso(function() {
                    v.t.avisar(v.t.mueveDigital ? v.t.cc.apuntar(v.t.entrada, x, y, 0, 0)
                                                : v.t.cc.apuntarEnFoto(v.t.entrada, x, y, 0, 0))
                })
                onCuadro: (x, y, w, h) => v.conPermiso(function() {
                    v.t.avisar(v.t.mueveDigital ? v.t.cc.apuntar(v.t.entrada, x, y, w, h)
                                                : v.t.cc.apuntarEnFoto(v.t.entrada, x, y, w, h))
                })
            }

            // Joystick, zoom, velocidad, inicio.
            RowLayout {
                Layout.fillWidth: true
                spacing: 10
                Joystick { tarjeta: v.t; Layout.preferredWidth: 104; Layout.preferredHeight: 104 }
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 6
                    Repeater {
                        model: v.t.caps.zoom === true ? [{t: qsTr("+ Acercar"), v: 1}, {t: qsTr("− Alejar"), v: -1}] : []
                        delegate: Rectangle {
                            required property var modelData
                            Layout.fillWidth: true
                            height: 28; radius: 10
                            color: zm.pressed ? Theme.accent : Qt.rgba(1, 1, 1, 0.08)
                            border.color: Qt.rgba(1, 1, 1, 0.16)
                            Text { anchors.centerIn: parent; text: modelData.t; color: Theme.text; font.pixelSize: 12 }
                            MouseArea {
                                id: zm
                                anchors.fill: parent
                                onPressed: v.t.avisar(v.t.cc.mover(v.t.entrada, 0, 0, modelData.v * v.t.velocidad))
                                onReleased: v.t.cc.parar(v.t.entrada)
                                onCanceled: v.t.cc.parar(v.t.entrada)
                            }
                        }
                    }
                    RowLayout {
                        spacing: 4
                        Text { text: qsTr("Vel."); color: Theme.textDim; font.pixelSize: 11 }
                        Slider { Layout.fillWidth: true; from: 0.1; to: 1; value: v.t.velocidad; onMoved: v.t.velocidad = value }
                    }
                    GlassButton {
                        compact: true
                        visible: v.t.caps.home === true
                        text: qsTr("Inicio")
                        onClicked: v.conPermiso(function() { v.t.avisar(v.t.cc.home(v.t.entrada)) })
                    }
                }
            }
            Item { Layout.fillHeight: true }
        }
    }

    // --- Los puntos prefijados: siempre a la vista ----------------------------------------
    RowLayout {
        Layout.fillWidth: true
        spacing: 8
        Text { text: qsTr("PUNTOS PREFIJADOS"); color: Theme.textDim; font { pixelSize: 11; weight: Font.DemiBold; letterSpacing: 1 } }
        Text {
            visible: v.t.posiciones.length === 0
            text: qsTr("Todavía no hay: «Configuración» → «Guardar donde está».")
            color: Theme.textFaint; font.pixelSize: 11
        }
        Item { Layout.fillWidth: true }
    }
    ListView {
        id: puntos
        visible: v.t.posiciones.length > 0
        Layout.fillWidth: true
        Layout.preferredHeight: 112
        orientation: ListView.Horizontal
        spacing: 8
        clip: true
        model: v.t.posiciones
        ScrollBar.horizontal: ScrollBar { policy: puntos.contentWidth > puntos.width ? ScrollBar.AlwaysOn : ScrollBar.AsNeeded }
        delegate: Rectangle {
            id: punto
            required property var modelData
            width: 150; height: 104
            radius: 10
            color: Qt.rgba(1, 1, 1, 0.06)
            border.color: ma.containsMouse ? Theme.accent : Qt.rgba(1, 1, 1, 0.14)
            border.width: ma.containsMouse ? 2 : 1
            // Al aire, apagada: tocarla pregunta (se vería el movimiento).
            opacity: v.t.cc && v.t.cc.alAire(v.t.entrada) ? 0.7 : 1
            Rectangle {
                id: miniatura
                anchors { left: parent.left; right: parent.right; top: parent.top; margins: 4 }
                height: 72; radius: 7
                color: "black"; clip: true
                Image {
                    anchors.fill: parent
                    source: punto.modelData.miniatura_url || ""
                    fillMode: Image.PreserveAspectCrop
                    asynchronous: true
                    cache: false
                }
                Text {
                    visible: !punto.modelData.miniatura_url
                    anchors.centerIn: parent
                    text: qsTr("sin foto"); color: Theme.textFaint; font.pixelSize: 10
                }
                // Su número de preset (los controles que llaman por número van a esta).
                Rectangle {
                    anchors { left: parent.left; top: parent.top; margins: 4 }
                    width: num.implicitWidth + 10; height: 18; radius: 5
                    color: Qt.rgba(0, 0, 0, 0.7)
                    Text { id: num; anchors.centerIn: parent; text: punto.modelData.numero || ""; color: "white"
                           font { pixelSize: 11; weight: Font.DemiBold } }
                }
            }
            Text {
                anchors { left: parent.left; right: parent.right; bottom: parent.bottom; margins: 6 }
                text: punto.modelData.nombre
                elide: Text.ElideRight
                horizontalAlignment: Text.AlignHCenter
                color: Theme.text; font.pixelSize: 12
            }
            MouseArea {
                id: ma
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: v.irA(punto.modelData)
            }
            ToolTip.visible: ma.containsMouse && (punto.modelData.comentario || "") !== ""
            ToolTip.delay: 400
            ToolTip.text: punto.modelData.comentario || ""
            // Configurando: regrabar acá, foto nueva, borrar.
            Row {
                visible: v.configurar
                anchors { right: parent.right; top: parent.top; margins: 6 }
                spacing: 3
                z: 2
                Repeater {
                    model: [{ i: "⟳", q: "regrabar", ayuda: qsTr("Regrabar: donde está ahora la cámara") },
                            { i: "📷", q: "miniatura", ayuda: qsTr("Sacarle una foto nueva") },
                            { i: "✕", q: "borrar", ayuda: qsTr("Borrar este punto") }]
                    delegate: Rectangle {
                        required property var modelData
                        width: 24; height: 24; radius: 6
                        color: bm.containsMouse ? Theme.accent : Qt.rgba(0, 0, 0, 0.65)
                        Text { anchors.centerIn: parent; text: modelData.i; color: "white"; font.pixelSize: 11 }
                        MouseArea {
                            id: bm
                            anchors.fill: parent
                            hoverEnabled: true
                            onClicked: {
                                const p = punto.modelData
                                if (modelData.q === "borrar") {
                                    ApplicationWindow.window.confirm({ title: qsTr("¿Borrar «%1»?").arg(p.nombre),
                                        text: qsTr("Se borra el punto prefijado (y su memoria en la cámara)."),
                                        accept: qsTr("Borrar"), reject: qsTr("Cancelar"),
                                        action: function() { v.t.avisar(v.t.cc.borrarPosicion(v.t.entrada, p.id)) } })
                                    return
                                }
                                const c = {}
                                c[modelData.q] = true
                                const r = v.t.cc.editarPosicion(v.t.entrada, p.id, c)
                                v.t.avisar(r.ok ? (r.aviso || (modelData.q === "regrabar" ? qsTr("«%1» regrabado").arg(p.nombre) : "")) : r.error)
                            }
                        }
                        ToolTip.visible: bm.containsMouse
                        ToolTip.text: modelData.ayuda
                    }
                }
            }
        }
    }

    // --- Configuración (se despliega) -----------------------------------------------------
    Rectangle {
        visible: v.configurar
        Layout.fillWidth: true
        implicitHeight: conf.implicitHeight + 20
        radius: 10
        color: Qt.rgba(1, 1, 1, 0.04)
        border.color: Qt.rgba(1, 1, 1, 0.1)
        ColumnLayout {
            id: conf
            anchors { left: parent.left; right: parent.right; top: parent.top; margins: 10 }
            spacing: 8
            RowLayout {
                Layout.fillWidth: true
                spacing: 6
                Text { text: qsTr("Nuevo punto:"); color: Theme.textDim; font.pixelSize: 12 }
                TextField {
                    id: nombreNuevo
                    Layout.fillWidth: true
                    placeholderText: qsTr("Nombre (p. ej. «Atril», «Plano general»)")
                    onAccepted: guardar.clicked()
                }
                GlassButton {
                    id: guardar
                    compact: true; text: qsTr("Guardar donde está"); icon: "＋"
                    enabled: nombreNuevo.text.trim() !== ""
                    onClicked: {
                        const r = v.t.cc.crearPosicion(v.t.entrada, nombreNuevo.text.trim(), "", 0, -1)
                        v.t.avisar(r.ok ? (r.aviso || "") : r.error)
                        if (r.ok) nombreNuevo.text = ""
                    }
                }
            }
            Text {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                text: qsTr("En cada punto: ⟳ lo regraba donde está ahora la cámara, 📷 le saca una foto nueva, ✕ lo borra.")
                color: Theme.textFaint; font.pixelSize: 11
            }
            // Rostros y seguimiento.
            RowLayout {
                Layout.fillWidth: true
                spacing: 6
                GlassButton {
                    compact: true
                    text: v.t.analizando ? qsTr("Analizando…") : qsTr("Analizar rostros")
                    icon: "☻"
                    enabled: !v.t.analizando && v.t.sg && v.t.sg.disponible
                    onClicked: { v.t.analizando = true; v.t.sg.analizar(v.t.entrada) }
                }
                ComboBox {
                    Layout.fillWidth: true
                    Layout.minimumWidth: 140
                    model: v.t.caras.map((c, i) => v.t.nombreCara(c, i))
                    currentIndex: v.t.elegida
                    displayText: v.t.elegida >= 0 && v.t.elegida < v.t.caras.length ? v.t.nombreCara(v.t.caras[v.t.elegida], v.t.elegida)
                                                                                     : (v.t.caras.length ? qsTr("¿A quién seguir?") : qsTr("Sin caras analizadas"))
                    onActivated: (i) => v.t.elegida = i
                    enabled: v.t.caras.length > 0
                }
                GlassButton {
                    compact: true
                    text: v.t.siguiendo ? qsTr("Dejar de seguir") : qsTr("Seguir")
                    icon: v.t.siguiendo ? "■" : "◎"
                    active: v.t.siguiendo !== null
                    enabled: v.t.sg && v.t.sg.disponible
                    onClicked: v.t.siguiendo ? v.t.sg.dejar(v.t.entrada) : v.t.seguirElegida()
                }
            }
        }
    }
    Text {
        visible: v.t.siguiendo !== null
        Layout.fillWidth: true
        text: v.t.siguiendo ? qsTr("Siguiendo a %1 — %2").arg(v.t.siguiendo.nombre || qsTr("la cara elegida")).arg(v.t.siguiendo.estado) : ""
        color: v.t.siguiendo && v.t.siguiendo.estado.startsWith("perdida") ? Theme.warn : Theme.ok
        font.pixelSize: 11; elide: Text.ElideRight
    }
    Text {
        visible: v.t.error !== ""
        Layout.fillWidth: true
        wrapMode: Text.WordWrap
        text: v.t.error; color: Theme.warn; font.pixelSize: 11
    }
}
