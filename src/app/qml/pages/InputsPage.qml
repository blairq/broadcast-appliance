// Pagina Entradas: video de HARDWARE (camaras USB, capturadoras) y de RED
// (camaras IP por RTSP, SRT, RTMP, HLS, UDP), con miniatura viva de cada
// entrada, en que escenas esta y si llega imagen. Todo sale de Studio.inputs.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import BA
import "../controls"

Rectangle {
    id: page
    color: Theme.bg
    property real leftInset: 92
    // Las entradas que se graban aparte (ISO); se refresca cada segundo mientras se mira.
    property var isos: Studio.isosUi()
    Timer { interval: 1000; repeat: true; running: page.visible; onTriggered: page.isos = Studio.isosUi() }
    readonly property var inp: Studio.inputs
    property string hwError: ""
    property string micError: ""
    property string netError: ""
    // Miniaturas vivas solo mientras la pagina esta a la vista.
    onVisibleChanged: inp.setActive(visible)
    Component.onCompleted: inp.setActive(visible)

    RowLayout {
        anchors { fill: parent; leftMargin: page.leftInset; margins: 20 }
        spacing: 16

        // --- Agregar ------------------------------------------------------------------------------
        ScrollView {
            Layout.preferredWidth: 420
            Layout.fillHeight: true
            contentWidth: availableWidth
            clip: true
            ColumnLayout {
                width: parent.width
                spacing: 14

                // Microfonos y lineas: van a un canal global (suenan en todas
                // las escenas), por eso no piden escena.
                Panel {
                    Layout.fillWidth: true
                    Layout.preferredHeight: micCol.implicitHeight + 64
                    title: "Micrófonos y entradas de audio"
                    ColumnLayout {
                        id: micCol
                        anchors.fill: parent
                        spacing: 8
                        Text { Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.textDim; font.pixelSize: 12
                               text: "Micrófonos, líneas e interfaces de audio de esta máquina. Se escuchan en todas las escenas; su procesamiento se arma en la página Audio." }
                        RowLayout {
                            Layout.fillWidth: true
                            ComboBox {
                                id: micBox
                                Layout.fillWidth: true
                                model: page.inp.audioDevices
                                textRole: "name"
                                valueRole: "value"
                            }
                            GlassButton { compact: true; text: "↻"; onClicked: page.inp.refresh()
                                          ToolTip.visible: hovered; ToolTip.text: "Buscar dispositivos de nuevo" }
                        }
                        Text { visible: page.inp.audioDevices.length === 0; text: "No se encontraron entradas de audio."; color: Theme.textFaint; font.pixelSize: 12 }
                        RowLayout {
                            Layout.fillWidth: true
                            TextField { id: micName; Layout.fillWidth: true; placeholderText: "Nombre (opcional)" }
                            GlassButton {
                                text: "Agregar"; active: true; enabled: micBox.currentIndex >= 0
                                onClicked: {
                                    page.micError = page.inp.addAudioInput(micBox.currentValue, micName.text)
                                    if (page.micError === "") micName.text = ""
                                }
                            }
                        }
                        Text { visible: page.micError !== ""; text: page.micError; color: Theme.live; font.pixelSize: 12; Layout.fillWidth: true; wrapMode: Text.WordWrap }
                    }
                }

                Panel {
                    Layout.fillWidth: true
                    Layout.preferredHeight: hwCol.implicitHeight + 64
                    title: "Hardware: cámaras y capturadoras"
                    ColumnLayout {
                        id: hwCol
                        anchors.fill: parent
                        spacing: 8
                        Text { Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.textDim; font.pixelSize: 12
                               text: page.inp.hardwareAvailable ? "Cámaras USB, capturadoras HDMI/SDI y placas de captura conectadas a esta máquina."
                                                                : "El plugin de cámaras no está cargado en esta plataforma." }
                        RowLayout {
                            Layout.fillWidth: true
                            ComboBox {
                                id: devBox
                                Layout.fillWidth: true
                                model: page.inp.devices
                                textRole: "name"
                                valueRole: "value"
                                onActivated: modeBox.model = page.inp.modes(currentValue)
                            }
                            GlassButton { compact: true; text: "↻"; onClicked: page.inp.refresh()
                                          ToolTip.visible: hovered; ToolTip.text: "Buscar dispositivos de nuevo" }
                        }
                        Text { visible: page.inp.devices.length === 0; text: "No se encontraron cámaras ni capturadoras."; color: Theme.textFaint; font.pixelSize: 12 }
                        ComboBox {
                            id: modeBox
                            Layout.fillWidth: true
                            visible: count > 0
                            textRole: "label"
                            displayText: currentIndex < 0 ? "Formato automático" : currentText
                            currentIndex: -1
                        }
                        TextField { id: hwName; Layout.fillWidth: true; placeholderText: "Nombre (opcional)" }
                        RowLayout {
                            Layout.fillWidth: true
                            ComboBox { id: hwScene; Layout.fillWidth: true; model: page.inp.scenes
                                       Component.onCompleted: currentIndex = Math.max(0, page.inp.scenes.indexOf(Studio.currentScene)) }
                            GlassButton {
                                text: "Agregar"; active: true; enabled: devBox.currentIndex >= 0
                                onClicked: {
                                    page.hwError = page.inp.addHardware(devBox.currentValue, hwName.text, hwScene.currentText,
                                                                        modeBox.currentIndex >= 0 ? modeBox.model[modeBox.currentIndex] : ({}))
                                    if (page.hwError === "") hwName.text = ""
                                }
                            }
                        }
                        Text { visible: page.hwError !== ""; text: page.hwError; color: Theme.live; font.pixelSize: 12; Layout.fillWidth: true; wrapMode: Text.WordWrap }
                    }
                }

                Panel {
                    Layout.fillWidth: true
                    visible: page.inp.ndiAvailable
                    Layout.preferredHeight: visible ? ndiCol.implicitHeight + 64 : 0
                    title: "Red: NDI®"
                    ColumnLayout {
                        id: ndiCol
                        anchors.fill: parent
                        spacing: 8
                        RowLayout {
                            Layout.fillWidth: true
                            ComboBox { id: ndiBox; Layout.fillWidth: true; model: page.inp.ndiFound; textRole: "name"; valueRole: "name"
                                       displayText: count === 0 ? (page.inp.ndiSearching ? "Buscando en la red…" : "Tocá Buscar") : currentText }
                            GlassButton { compact: true; text: page.inp.ndiSearching ? "…" : "Buscar"; enabled: !page.inp.ndiSearching; onClicked: page.inp.searchNdi() }
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            ComboBox { id: ndiScene; Layout.fillWidth: true; model: page.inp.scenes
                                       Component.onCompleted: currentIndex = Math.max(0, page.inp.scenes.indexOf(Studio.currentScene)) }
                            GlassButton { text: "Agregar"; active: true; enabled: ndiBox.count > 0
                                          onClicked: page.netError = page.inp.addNdi(ndiBox.currentValue, "", ndiScene.currentText) }
                        }
                        Text { Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.textFaint; font.pixelSize: 11
                               text: "Recepción abierta con libNoIDea (VideoLAN, experimental): NDI completo (SpeedHQ); NDI|HX no. NDI® es una marca de Vizrt NDI AB; este programa no está afiliado a Vizrt." }
                    }
                }

                Panel {
                    Layout.fillWidth: true
                    Layout.preferredHeight: netCol.implicitHeight + 64
                    title: "Red: cámaras IP y señales"
                    ColumnLayout {
                        id: netCol
                        anchors.fill: parent
                        spacing: 8
                        GlassButton {
                            Layout.fillWidth: true
                            text: "Buscar cámaras en la red…"
                            icon: "⌕"
                            active: true
                            onClicked: { buscador.escenas = page.inp.scenes; buscador.open() }
                            ToolTip.visible: hovered; ToolTip.delay: 400
                            ToolTip.text: "Encuentra cámaras IP y grabadores (ONVIF, RTSP), reconoce la marca, prueba sus URLs típicas, guarda la clave cifrada y las conecta con su PTZ"
                        }
                        Text { text: "o a mano:"; color: Theme.textFaint; font.pixelSize: 11 }
                        TextField { id: netUrl; Layout.fillWidth: true; placeholderText: "rtsp://usuario:clave@192.168.1.50:554/stream1" }
                        Flow {
                            Layout.fillWidth: true
                            spacing: 6
                            Repeater {
                                model: [
                                    { t: "Cámara IP (RTSP)", u: "rtsp://usuario:clave@192.168.1.50:554/stream1" },
                                    { t: "SRT", u: "srt://192.168.1.20:9000?mode=caller&latency=120000" },
                                    { t: "RTMP", u: "rtmp://servidor/app/clave" },
                                    { t: "HLS", u: "https://servidor/live/playlist.m3u8" },
                                    { t: "UDP", u: "udp://@:5000" }
                                ]
                                delegate: GlassButton { required property var modelData; compact: true; text: modelData.t
                                                        onClicked: { netUrl.text = modelData.u; netUrl.forceActiveFocus(); netUrl.selectAll() } }
                            }
                        }
                        TextField { id: netName; Layout.fillWidth: true; placeholderText: "Nombre (opcional)" }
                        RowLayout {
                            // Estado local (no hay modelo detras): lo mueve el propio handler.
                            Toggle { id: lowLat; checked: true; onToggled: (wanted) => lowLat.checked = wanted }
                            Text { text: "Latencia baja (desactivar si la señal es inestable)"; color: Theme.textDim; font.pixelSize: 12; Layout.fillWidth: true; wrapMode: Text.WordWrap }
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            ComboBox { id: netScene; Layout.fillWidth: true; model: page.inp.scenes
                                       Component.onCompleted: currentIndex = Math.max(0, page.inp.scenes.indexOf(Studio.currentScene)) }
                            GlassButton {
                                text: "Agregar"; active: true; enabled: netUrl.text.trim() !== ""
                                onClicked: {
                                    page.netError = page.inp.addNetwork(netUrl.text, netName.text, netScene.currentText, lowLat.checked)
                                    if (page.netError === "") { netUrl.text = ""; netName.text = "" }
                                }
                            }
                        }
                        Text { visible: page.netError !== ""; text: page.netError; color: Theme.live; font.pixelSize: 12; Layout.fillWidth: true; wrapMode: Text.WordWrap }
                        Text { Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.textFaint; font.pixelSize: 11
                               text: "Reconecta sola si la señal se corta. HTTPS/RTMPS se verifican con las raíces de certificados del sistema portable. Por MCP: network_input_add." }
                    }
                }
            }
        }

        // --- Entradas existentes -----------------------------------------------------------------
        Panel {
            Layout.fillWidth: true
            Layout.fillHeight: true
            title: "Entradas (" + page.inp.inputs.length + ")"
            GridView {
                id: grid
                anchors.fill: parent
                clip: true
                cellWidth: Math.max(260, width / Math.max(1, Math.floor(width / 300)))
                cellHeight: cellWidth * 9 / 16 + 76
                model: page.inp.inputs
                Text { anchors.centerIn: parent; visible: grid.count === 0; color: Theme.textFaint; font.pixelSize: 13
                       text: "Todavía no hay entradas: agregá una cámara o una señal de red." }
                delegate: Item {
                    required property var modelData
                    width: grid.cellWidth
                    height: grid.cellHeight
                    Rectangle {
                        anchors { fill: parent; margins: 6 }
                        radius: 14
                        color: Qt.rgba(1, 1, 1, 0.04)
                        border.color: modelData.receiving ? Qt.rgba(0.2, 0.77, 0.42, 0.5) : Qt.rgba(1, 0.23, 0.19, 0.5)
                        ColumnLayout {
                            anchors { fill: parent; margins: 8 }
                            spacing: 4
                            Rectangle {
                                Layout.fillWidth: true
                                Layout.preferredHeight: width * 9 / 16
                                radius: 10
                                color: "black"
                                clip: true
                                Image {
                                    anchors.fill: parent
                                    fillMode: Image.PreserveAspectFit
                                    cache: false
                                    visible: modelData.kind !== "audio"
                                    source: modelData.receiving && modelData.kind !== "audio"
                                            ? "image://inputs/" + encodeURIComponent(modelData.name) + "?" + page.inp.stamp : ""
                                }
                                // Una entrada de audio no tiene imagen: se muestra su nivel.
                                Text { anchors.centerIn: parent; visible: modelData.kind === "audio"
                                       text: "🎙"; font.pixelSize: 34; color: Theme.textFaint }
                                Text { anchors.centerIn: parent; visible: !modelData.receiving && modelData.kind !== "audio"; color: Theme.textFaint; font.pixelSize: 12
                                       text: modelData.kind === "red" ? "sin señal (reintentando…)" : "sin imagen" }
                                StatusPill { anchors { left: parent.left; top: parent.top; margins: 6 }
                                             text: modelData.kind === "red" ? "RED" : modelData.kind === "ndi" ? "NDI" : modelData.kind === "audio" ? "AUDIO" : "HARDWARE"
                                             dot: modelData.receiving ? Theme.ok : Theme.live }
                            }
                            RowLayout {
                                Layout.fillWidth: true
                                Text { text: modelData.name; color: Theme.text; font { pixelSize: 13; weight: Font.DemiBold } elide: Text.ElideRight; Layout.fillWidth: true }
                                Text { visible: modelData.receiving; text: modelData.width + "×" + modelData.height; color: Theme.textDim; font.pixelSize: 11 }
                                // ISO: grabar esta entrada aparte (para editar después).
                                GlassButton {
                                    readonly property bool grabando: page.isos.some(g => g.fuente === modelData.name)
                                    visible: modelData.kind !== "audio"
                                    compact: true
                                    text: grabando ? "● ISO" : "○ ISO"
                                    active: grabando
                                    activeColor: Theme.live
                                    onClicked: { const e = Studio.isoUi(modelData.name, !grabando); if (e !== "") Studio.notify(e, "error"); page.isos = Studio.isosUi() }
                                    ToolTip.visible: hovered; ToolTip.delay: 400
                                    ToolTip.text: grabando ? "Se graba aparte en Recordings/entradas/: tocá para parar" : "Grabar esta entrada aparte (es un encode más)"
                                }
                                GlassButton {
                                    compact: true; text: "Quitar"
                                    onClicked: {
                                        // La grilla se rearma cuando cambia el estado de una entrada y este delegate
                                        // puede morir con el dialogo abierto: la accion usa solo estas copias.
                                        const inp = page.inp
                                        const nombre = modelData.name
                                        const escenas = modelData.scenes.join(", ")
                                        const alAire = modelData.scenes.indexOf(Studio.currentScene) >= 0
                                        ApplicationWindow.window.confirm({
                                            title: "¿Quitar la entrada «" + nombre + "»?",
                                            text: (alAire ? "Está en la escena al aire: desaparece del aire al instante. " : "")
                                                  + (escenas !== "" ? "Se saca de todas las escenas donde está (" + escenas + ") y el cambio se guarda."
                                                                    : "No está en ninguna escena."),
                                            accept: "Quitar",
                                            reject: "Cancelar",
                                            action: function() { inp.remove(nombre) }
                                        })
                                    }
                                }
                            }
                            Text { Layout.fillWidth: true; text: modelData.where; color: Theme.textFaint; font.pixelSize: 11; elide: Text.ElideMiddle }
                            Text { Layout.fillWidth: true; text: modelData.scenes.length > 0 ? "En: " + modelData.scenes.join(", ") : "No está en ninguna escena"
                                   color: Theme.textFaint; font.pixelSize: 11; elide: Text.ElideRight }
                        }
                    }
                }
            }
        }
    }

    BuscadorRed { id: buscador }
}
