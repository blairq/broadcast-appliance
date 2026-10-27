// El escáner de red: qué equipos de video hay, su clave, su ficha y agregarlos.
//
// Lo mismo que red_escanear / camara_credenciales / camara_ficha /
// camara_agregar por MCP (red/Red.hpp). La clave se guarda cifrada y no se
// vuelve a mostrar.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import BA

Popup {
    id: bus
    property var red: Studio.red
    property var escenas: []
    property string sel: ""
    property int tick: 0
    modal: true
    anchors.centerIn: Overlay.overlay
    width: Math.min(1180, (Overlay.overlay ? Overlay.overlay.width : 1200) - 80)
    height: Math.min(800, (Overlay.overlay ? Overlay.overlay.height : 860) - 60)
    padding: 0
    background: Rectangle { color: Theme.panel; radius: Theme.radius; border.color: Theme.glassBorder }
    onOpened: if (red && red.equipos.length === 0 && !red.escaneando) red.escanear()
    Connections { target: bus.red; function onCambio() { bus.tick++ } }

    readonly property var equipos: { tick; return red ? red.equipos : [] }
    readonly property var actual: {
        tick
        for (const e of bus.equipos) if (e.ip === bus.sel) return e
        return null
    }
    readonly property var ficha: { tick; return red && sel !== "" ? red.fichaDe(sel) : ({}) }
    function estadoRtsp(e) {
        const rs = e.rtsp || []
        if (rs.some(r => r.codigo === 200)) return { t: qsTr("se ve"), c: Theme.ok }
        if (rs.some(r => r.codigo === 401)) return { t: e.credenciales_guardadas ? qsTr("clave incorrecta?") : qsTr("pide clave"), c: Theme.warn }
        if (e.onvif) return { t: "ONVIF", c: Theme.accent }
        return { t: rs.length ? qsTr("sin URL") : "—", c: Theme.textFaint }
    }

    RowLayout {
        anchors { fill: parent; margins: 18 }
        spacing: 16

        // --- La lista ---------------------------------------------------------------------
        // Los textos largos (nombre, IP, fabricante, URLs) se cortan con «…»: con preferredWidth 1
        // y fillWidth no imponen su ancho al layout (si no, el modal se iba por la derecha).
        ColumnLayout {
            readonly property real ancho: Math.max(260, Math.min(430, bus.width * 0.38))
            Layout.preferredWidth: ancho
            Layout.maximumWidth: ancho
            Layout.fillHeight: true
            spacing: 8
            RowLayout {
                Layout.fillWidth: true
                Text { text: qsTr("Equipos de video en la red"); color: Theme.text; Layout.fillWidth: true; font { pixelSize: 18; weight: Font.DemiBold } }
                GlassButton {
                    compact: true; text: bus.red && bus.red.escaneando ? qsTr("Buscando…") : qsTr("Buscar de nuevo")
                    enabled: bus.red && !bus.red.escaneando
                    onClicked: bus.red.escanear()
                }
            }
            Text { text: bus.red ? bus.red.progreso : ""; color: Theme.textDim; font.pixelSize: 11; Layout.fillWidth: true; Layout.preferredWidth: 1; elide: Text.ElideRight }
            ListView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                spacing: 4
                model: bus.equipos
                delegate: Rectangle {
                    required property var modelData
                    width: ListView.view.width
                    height: 54
                    radius: 10
                    readonly property bool esVideo: modelData.tipo === "cámara" || String(modelData.tipo).startsWith("grabador")
                    opacity: esVideo ? 1 : 0.55
                    color: bus.sel === modelData.ip ? Qt.rgba(1, 1, 1, 0.12) : Qt.rgba(1, 1, 1, 0.04)
                    border.color: bus.sel === modelData.ip ? Theme.accent : "transparent"
                    RowLayout {
                        anchors { fill: parent; margins: 10 }
                        spacing: 10
                        Text { text: String(modelData.tipo).startsWith("grabador") ? "🗄" : modelData.tipo === "cámara" ? "📹" : "🖧"; font.pixelSize: 20 }
                        ColumnLayout {
                            Layout.fillWidth: true
                            Layout.preferredWidth: 1
                            spacing: 1
                            Text {
                                Layout.fillWidth: true; elide: Text.ElideRight
                                text: (modelData.onvif && modelData.onvif.nombre ? modelData.onvif.nombre : (modelData.marca ? modelData.marca.nombre : "")) + "  ·  " + modelData.ip
                                color: Theme.text; font { pixelSize: 13; weight: Font.DemiBold }
                            }
                            Text {
                                Layout.fillWidth: true; elide: Text.ElideRight
                                text: modelData.tipo + (modelData.fabricante_mac ? "  ·  " + modelData.fabricante_mac : "")
                                color: Theme.textFaint; font.pixelSize: 11
                            }
                        }
                        Rectangle {
                            readonly property var st: bus.estadoRtsp(modelData)
                            radius: 6; height: 20; width: stTxt.implicitWidth + 12
                            color: Qt.rgba(0, 0, 0, 0.3); border.color: st.c
                            Text { id: stTxt; anchors.centerIn: parent; text: parent.st.t; color: parent.st.c; font.pixelSize: 10 }
                        }
                    }
                    MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: bus.sel = modelData.ip }
                }
                Text {
                    anchors.centerIn: parent
                    visible: parent.count === 0
                    text: bus.red && bus.red.escaneando ? qsTr("Buscando (unos segundos)…") : qsTr("Nada todavía: «Buscar de nuevo».")
                    color: Theme.textDim; font.pixelSize: 13
                }
            }
            GlassButton { compact: true; text: qsTr("Cerrar"); onClicked: bus.close() }
        }

        // --- El equipo elegido -------------------------------------------------------------
        ScrollView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            contentWidth: availableWidth
            clip: true
            ColumnLayout {
                width: parent.width
                spacing: 10
                Text {
                    visible: !bus.actual
                    Layout.fillWidth: true; wrapMode: Text.WordWrap
                    text: qsTr("Elegí un equipo para ver qué es, guardar su usuario y clave, y conectarlo al estudio.")
                    color: Theme.textDim; font.pixelSize: 13
                }
                ColumnLayout {
                    visible: !!bus.actual
                    Layout.fillWidth: true
                    spacing: 8
                    Text {
                        Layout.fillWidth: true; wrapMode: Text.WordWrap
                        text: bus.actual ? ((bus.actual.onvif && bus.actual.onvif.nombre) || bus.actual.marca.nombre) + " — " + bus.actual.ip : ""
                        color: Theme.text; font { pixelSize: 19; weight: Font.DemiBold }
                    }
                    Text {
                        Layout.fillWidth: true; wrapMode: Text.WordWrap
                        text: bus.actual ? qsTr("Marca: %1 (%2)").arg(bus.actual.marca.nombre).arg(bus.actual.marca.porque) +
                                           (bus.actual.mac ? "\n" + qsTr("MAC %1 — %2").arg(bus.actual.mac).arg(bus.actual.fabricante_mac || qsTr("fabricante desconocido")) : "") +
                                           "\n" + qsTr("Puertos: %1").arg((bus.actual.puertos || []).join(", ")) : ""
                        color: Theme.textDim; font.pixelSize: 12
                    }
                    Rectangle {
                        visible: !!bus.actual && !!bus.actual.notas
                        Layout.fillWidth: true
                        implicitHeight: notasTxt.implicitHeight + 16
                        radius: 8; color: Qt.rgba(1, 0.75, 0.15, 0.08); border.color: Qt.rgba(1, 0.75, 0.15, 0.4)
                        Text { id: notasTxt; anchors { fill: parent; margins: 8 } wrapMode: Text.WordWrap; text: bus.actual ? bus.actual.notas || "" : ""; color: Theme.text; font.pixelSize: 12 }
                    }
                    // Las URLs probadas.
                    Repeater {
                        model: bus.actual ? bus.actual.rtsp || [] : []
                        delegate: RowLayout {
                            required property var modelData
                            Layout.fillWidth: true
                            Text { text: modelData.codigo === 200 ? "✓" : modelData.codigo === 401 ? "🔒" : "✗"; color: modelData.codigo === 200 ? Theme.ok : Theme.textDim; font.pixelSize: 12 }
                            Text { Layout.fillWidth: true; Layout.preferredWidth: 1; elide: Text.ElideMiddle; text: modelData.url + "  (" + modelData.perfil + ")"; color: Theme.textDim; font { pixelSize: 11; family: "Menlo" } }
                            Text { text: modelData.estado + (modelData.codec ? " · " + modelData.codec : ""); color: Theme.textFaint; font.pixelSize: 11 }
                        }
                    }
                    // Usuario y clave.
                    Text { text: bus.actual && bus.actual.credenciales_guardadas ? qsTr("Usuario y clave (guardados ✓)") : qsTr("Usuario y clave"); color: Theme.textDim; font { pixelSize: 11; capitalization: Font.AllUppercase; letterSpacing: 0.5 } }
                    RowLayout {
                        Layout.fillWidth: true
                        TextField { id: usr; Layout.preferredWidth: 160; placeholderText: bus.actual ? (bus.actual.usuario_de_fabrica || "admin") : "admin" }
                        TextField { id: pwd; Layout.fillWidth: true; Layout.preferredWidth: 1; placeholderText: qsTr("Clave (o código de verificación)"); echoMode: TextInput.Password; inputMethodHints: Qt.ImhSensitiveData }
                        GlassButton {
                            compact: true; text: qsTr("Guardar y probar"); active: true
                            onClicked: {
                                const e = bus.red.guardarCredenciales(bus.sel, usr.text !== "" ? usr.text : usr.placeholderText, pwd.text)
                                guardarMsg.text = e === "" ? qsTr("Guardado (cifrado). Probando las URLs…") : e
                                if (e === "") pwd.text = ""
                            }
                        }
                    }
                    Text { id: guardarMsg; visible: text !== ""; color: Theme.textDim; font.pixelSize: 11; Layout.fillWidth: true; wrapMode: Text.WordWrap }
                    // La ficha y el plan.
                    RowLayout {
                        GlassButton { text: qsTr("Analizar (ONVIF + GStreamer)"); onClicked: { bus.red.pedirFicha(bus.sel); fichaMsg.text = qsTr("Analizando (unos segundos)…") } }
                        Text { id: fichaMsg; visible: text !== "" && !bus.ficha.plan; color: Theme.textDim; font.pixelSize: 11 }
                    }
                    Text {
                        visible: !!bus.ficha.error
                        Layout.fillWidth: true; wrapMode: Text.WordWrap
                        text: bus.ficha.error || ""; color: Theme.warn; font.pixelSize: 12
                    }
                    Text {
                        visible: !!bus.ficha.equipo
                        Layout.fillWidth: true; wrapMode: Text.WordWrap
                        text: bus.ficha.equipo ? qsTr("%1 %2 · firmware %3").arg(bus.ficha.equipo.Manufacturer || "").arg(bus.ficha.equipo.Model || "").arg(bus.ficha.equipo.FirmwareVersion || "?") : ""
                        color: Theme.textDim; font.pixelSize: 12
                    }
                    Repeater {
                        model: bus.ficha.perfiles || []
                        delegate: Text {
                            required property var modelData
                            Layout.fillWidth: true; elide: Text.ElideRight
                            text: qsTr("Perfil «%1»: %2x%3 %4 a %5 fps").arg(modelData.nombre || modelData.token).arg(modelData.ancho || "?").arg(modelData.alto || "?")
                                      .arg(modelData.codec || "").arg(modelData.fps || "?") + (modelData.ptz ? " · PTZ" : "")
                            color: Theme.textDim; font.pixelSize: 12
                        }
                    }
                    Text {
                        visible: !!bus.ficha.ptz && bus.ficha.ptz.tiene
                        text: bus.ficha.ptz ? qsTr("PTZ por ONVIF ✓") + (bus.ficha.ptz.presets ? " · " + qsTr("%n presets", "", bus.ficha.ptz.presets.length) : "") : ""
                        color: Theme.ok; font.pixelSize: 12
                    }
                    Rectangle {
                        visible: !!bus.ficha.plan && !!bus.ficha.plan.pasos
                        Layout.fillWidth: true
                        implicitHeight: planCol.implicitHeight + 16
                        radius: 8; color: Qt.rgba(0.2, 0.7, 0.4, 0.08); border.color: Qt.rgba(0.2, 0.7, 0.4, 0.4)
                        ColumnLayout {
                            id: planCol
                            anchors { left: parent.left; right: parent.right; top: parent.top; margins: 8 }
                            Text { text: qsTr("Cómo se va a conectar"); color: Theme.text; font { pixelSize: 13; weight: Font.DemiBold } }
                            Repeater {
                                model: bus.ficha.plan ? bus.ficha.plan.pasos || [] : []
                                delegate: Text { required property string modelData; Layout.fillWidth: true; wrapMode: Text.WordWrap; text: "• " + modelData; color: Theme.textDim; font.pixelSize: 12 }
                            }
                        }
                    }
                    // Si ya está conectada: su PTZ (cuando la clave llegó después) y otra escena.
                    ColumnLayout {
                        readonly property var ya: { bus.tick; return bus.red && bus.sel !== "" ? bus.red.entradasDe(bus.sel) : [] }
                        id: yaCol
                        visible: ya.length > 0
                        Layout.fillWidth: true
                        spacing: 6
                        Text { text: qsTr("Ya está en el estudio"); color: Theme.textDim; font { pixelSize: 11; capitalization: Font.AllUppercase; letterSpacing: 0.5 } }
                        Repeater {
                            model: yaCol.ya
                            delegate: ColumnLayout {
                                required property var modelData
                                Layout.fillWidth: true
                                spacing: 4
                                Text { text: qsTr("«%1»").arg(modelData.entrada); color: Theme.text; font.pixelSize: 12; Layout.fillWidth: true; Layout.preferredWidth: 1; elide: Text.ElideRight }
                                Flow {
                                Layout.fillWidth: true
                                spacing: 6
                                GlassButton {
                                    compact: true; text: qsTr("Configurar su PTZ")
                                    onClicked: { const e = bus.red.ptzDesdeUi(bus.sel, modelData.entrada); agregarMsg.text = e === "" ? qsTr("PTZ configurado: se mueve desde Cámaras PTZ.") : e }
                                    ToolTip.visible: hovered; ToolTip.delay: 400
                                    ToolTip.text: qsTr("Con la clave ONVIF ya guardada (después de cargarla y de «Analizar»)")
                                }
                                ComboBox { id: otraEscena; width: 170; model: bus.escenas }
                                GlassButton {
                                    compact: true; text: qsTr("Sumar a esa escena")
                                    onClicked: { const e = bus.red.reusarDesdeUi(bus.sel, otraEscena.currentText); agregarMsg.text = e === "" ? qsTr("Sumada a «%1» (el mismo stream, no otro).").arg(otraEscena.currentText) : e }
                                }
                                }
                            }
                        }
                    }
                    // Agregar.
                    Text { text: yaCol.visible ? qsTr("Agregar otra vez (otro stream)") : qsTr("Agregar al estudio"); color: Theme.textDim; font { pixelSize: 11; capitalization: Font.AllUppercase; letterSpacing: 0.5 } }
                    RowLayout {
                        Layout.fillWidth: true
                        TextField { id: nombreCam; Layout.fillWidth: true; Layout.preferredWidth: 1; placeholderText: bus.actual && bus.actual.onvif ? bus.actual.onvif.nombre : qsTr("Nombre de la entrada") }
                        ComboBox { id: escenaCam; Layout.preferredWidth: 170; model: bus.escenas }
                        GlassButton {
                            text: qsTr("Agregar"); active: true
                            onClicked: {
                                const e = bus.red.agregarDesdeUi(bus.sel, nombreCam.text, escenaCam.currentText)
                                agregarMsg.text = e === "" ? qsTr("Agregada: la conexión tarda unos segundos (Video → su grafo, y el PTZ si tiene).") : e
                            }
                        }
                    }
                    Text { id: agregarMsg; visible: text !== ""; color: Theme.textDim; font.pixelSize: 12; Layout.fillWidth: true; wrapMode: Text.WordWrap }
                }
            }
        }
    }
}
