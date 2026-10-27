// Pagina Entradas de video: video de HARDWARE (camaras USB, capturadoras) y de
// RED (camaras IP por RTSP, SRT, RTMP, HLS, UDP, NDI), con miniatura viva de cada
// entrada, en que escenas esta y si llega imagen. Todo sale de Studio.inputs.
// Las camaras para elegir son las del registro de la maquina (pagina
// «Dispositivos y equipos»), con el nombre que les puso el operador; una entrada
// cuyo dispositivo no esta se muestra como desconectada y se la espera.
// Los microfonos tienen su pagina: «Entradas de audio».
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
    property string netError: ""
    // Miniaturas vivas solo mientras la pagina esta a la vista.
    onVisibleChanged: inp.setActive(visible)
    // Solo prende: si se crea oculta (en segundo plano) no apaga a la otra que comparte `inp` (Cámaras PTZ).
    Component.onCompleted: if (visible) inp.setActive(true)

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

                Panel {
                    Layout.fillWidth: true
                    Layout.preferredHeight: hwCol.implicitHeight + 64
                    title: qsTr("Hardware: cámaras y capturadoras")
                    ColumnLayout {
                        id: hwCol
                        anchors.fill: parent
                        spacing: 8
                        Text { Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.textDim; font.pixelSize: 12
                               text: page.inp.hardwareAvailable ? qsTr("Cámaras USB, capturadoras HDMI/SDI y placas de captura de esta máquina, con el nombre que les pusiste en Dispositivos y equipos.")
                                                                : qsTr("El plugin de cámaras no está cargado en esta plataforma.") }
                        LinkDispositivos { Layout.fillWidth: true }
                        RowLayout {
                            Layout.fillWidth: true
                            ComboBox {
                                id: devBox
                                Layout.fillWidth: true
                                model: page.inp.devices
                                textRole: "label"
                                valueRole: "value"
                                onActivated: modeBox.model = page.inp.modes(currentValue)
                            }
                            GlassButton { compact: true; text: "↻"; onClicked: page.inp.refresh()
                                          ToolTip.visible: hovered; ToolTip.text: qsTr("Buscar dispositivos de nuevo") }
                        }
                        Text { visible: page.inp.devices.length === 0; text: qsTr("No se encontraron cámaras ni capturadoras."); color: Theme.textFaint; font.pixelSize: 12 }
                        ComboBox {
                            id: modeBox
                            Layout.fillWidth: true
                            visible: count > 0
                            textRole: "label"
                            displayText: currentIndex < 0 ? qsTr("Formato automático") : currentText
                            onModelChanged: currentIndex = count > 0 ? 0 : -1 // el primero es «Automático»
                        }
                        // Por qué el automático elige ese (tamaño, cps, formato y quién lo decodifica).
                        Text {
                            readonly property var elegido: modeBox.currentIndex >= 0 && modeBox.model ? modeBox.model[modeBox.currentIndex] : null
                            visible: modeBox.visible && !!elegido && !!elegido.detalle
                            text: elegido && elegido.detalle ? elegido.detalle : ""
                            Layout.fillWidth: true; wrapMode: Text.WordWrap
                            color: Theme.textFaint; font.pixelSize: 11
                        }
                        TextField { id: hwName; Layout.fillWidth: true; placeholderText: qsTr("Nombre (opcional: si no, el del dispositivo)") }
                        RowLayout {
                            Layout.fillWidth: true
                            ComboBox { id: hwScene; Layout.fillWidth: true; model: page.inp.scenes
                                       Component.onCompleted: currentIndex = Math.max(0, page.inp.scenes.indexOf(Studio.currentScene)) }
                            GlassButton {
                                text: qsTr("Agregar"); active: true; enabled: devBox.currentIndex >= 0
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
                    title: qsTr("Red: NDI®")
                    ColumnLayout {
                        id: ndiCol
                        anchors.fill: parent
                        spacing: 8
                        RowLayout {
                            Layout.fillWidth: true
                            ComboBox { id: ndiBox; Layout.fillWidth: true; model: page.inp.ndiFound; textRole: "label"; valueRole: "name"
                                       displayText: count === 0 ? (page.inp.ndiSearching ? qsTr("Buscando en la red…") : qsTr("Tocá Buscar")) : currentText }
                            GlassButton { compact: true; text: page.inp.ndiSearching ? "…" : qsTr("Buscar"); enabled: !page.inp.ndiSearching; onClicked: page.inp.searchNdi() }
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            ComboBox { id: ndiScene; Layout.fillWidth: true; model: page.inp.scenes
                                       Component.onCompleted: currentIndex = Math.max(0, page.inp.scenes.indexOf(Studio.currentScene)) }
                            GlassButton { text: qsTr("Agregar"); active: true; enabled: ndiBox.count > 0
                                          onClicked: page.netError = page.inp.addNdi(ndiBox.currentValue, "", ndiScene.currentText) }
                        }
                        Text { Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.textFaint; font.pixelSize: 11
                               text: qsTr("Recepción abierta con libNoIDea (VideoLAN, experimental): NDI completo (SpeedHQ); NDI|HX no. NDI® es una marca de Vizrt NDI AB; este programa no está afiliado a Vizrt.") }
                    }
                }

                Panel {
                    Layout.fillWidth: true
                    Layout.preferredHeight: netCol.implicitHeight + 64
                    title: qsTr("Red: cámaras IP y señales")
                    ColumnLayout {
                        id: netCol
                        anchors.fill: parent
                        spacing: 8
                        GlassButton {
                            Layout.fillWidth: true
                            text: qsTr("Buscar cámaras en la red…")
                            icon: "⌕"
                            active: true
                            onClicked: { buscador.escenas = page.inp.scenes; buscador.open() }
                            ToolTip.visible: hovered; ToolTip.delay: 400
                            ToolTip.text: qsTr("Encuentra cámaras IP y grabadores (ONVIF, RTSP), reconoce la marca, prueba sus URLs típicas, guarda la clave cifrada y las conecta con su PTZ")
                        }
                        Text { text: qsTr("o a mano:"); color: Theme.textFaint; font.pixelSize: 11 }
                        TextField { id: netUrl; Layout.fillWidth: true; placeholderText: "rtsp://usuario:clave@192.168.1.50:554/stream1" }
                        Flow {
                            Layout.fillWidth: true
                            spacing: 6
                            Repeater {
                                model: [
                                    { t: qsTr("Cámara IP (RTSP)"), u: "rtsp://usuario:clave@192.168.1.50:554/stream1" },
                                    { t: "SRT", u: "srt://192.168.1.20:9000?mode=caller&latency=120000" },
                                    { t: "RTMP", u: "rtmp://servidor/app/clave" },
                                    { t: "HLS", u: "https://servidor/live/playlist.m3u8" },
                                    { t: "UDP", u: "udp://@:5000" }
                                ]
                                delegate: GlassButton { required property var modelData; compact: true; text: modelData.t
                                                        onClicked: { netUrl.text = modelData.u; netUrl.forceActiveFocus(); netUrl.selectAll() } }
                            }
                        }
                        TextField { id: netName; Layout.fillWidth: true; placeholderText: qsTr("Nombre (opcional)") }
                        RowLayout {
                            // Estado local (no hay modelo detras): lo mueve el propio handler.
                            Toggle { id: lowLat; checked: true; onToggled: (wanted) => lowLat.checked = wanted }
                            Text { text: qsTr("Latencia baja (desactivar si la señal es inestable)"); color: Theme.textDim; font.pixelSize: 12; Layout.fillWidth: true; wrapMode: Text.WordWrap }
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            ComboBox { id: netScene; Layout.fillWidth: true; model: page.inp.scenes
                                       Component.onCompleted: currentIndex = Math.max(0, page.inp.scenes.indexOf(Studio.currentScene)) }
                            GlassButton {
                                text: qsTr("Agregar"); active: true; enabled: netUrl.text.trim() !== ""
                                onClicked: {
                                    page.netError = page.inp.addNetwork(netUrl.text, netName.text, netScene.currentText, lowLat.checked)
                                    if (page.netError === "") { netUrl.text = ""; netName.text = "" }
                                }
                            }
                        }
                        Text { visible: page.netError !== ""; text: page.netError; color: Theme.live; font.pixelSize: 12; Layout.fillWidth: true; wrapMode: Text.WordWrap }
                        Text { Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.textFaint; font.pixelSize: 11
                               text: qsTr("Reconecta sola si la señal se corta. HTTPS/RTMPS se verifican con las raíces de certificados del sistema portable. Por MCP: network_input_add.") }
                    }
                }
            }
        }

        // --- Entradas existentes -----------------------------------------------------------------
        Panel {
            Layout.fillWidth: true
            Layout.fillHeight: true
            title: qsTr("Entradas de video (%1)").arg(page.inp.videoInputs.length)
            GridView {
                id: grid
                anchors.fill: parent
                clip: true
                cellWidth: Math.max(260, width / Math.max(1, Math.floor(width / 300)))
                cellHeight: cellWidth * 9 / 16 + 76
                model: page.inp.videoInputs
                Text { anchors.centerIn: parent; visible: grid.count === 0; color: Theme.textFaint; font.pixelSize: 13
                       text: qsTr("Todavía no hay entradas de video: agregá una cámara o una señal de red.") }
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
                                       width: parent.width - 16; horizontalAlignment: Text.AlignHCenter; wrapMode: Text.WordWrap
                                       text: modelData.estado === "ausente" ? qsTr("«%1» no está conectado: se lo espera").arg(modelData.dispositivo)
                                             : modelData.kind === "red" ? qsTr("sin señal (reintentando…)") : qsTr("sin imagen") }
                                StatusPill { anchors { left: parent.left; top: parent.top; margins: 6 }
                                             text: modelData.kind === "red" ? qsTr("RED") : modelData.kind === "ndi" ? "NDI" : modelData.kind === "audio" ? qsTr("AUDIO") : qsTr("HARDWARE")
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
                                    ToolTip.text: grabando ? qsTr("Se graba aparte en Recordings/entradas/: tocá para parar") : qsTr("Grabar esta entrada aparte (es un encode más)")
                                }
                                GlassButton {
                                    compact: true; text: qsTr("Quitar")
                                    onClicked: {
                                        // La grilla se rearma cuando cambia el estado de una entrada y este delegate
                                        // puede morir con el dialogo abierto: la accion usa solo estas copias.
                                        const inp = page.inp
                                        const nombre = modelData.name
                                        const escenas = modelData.scenes.join(", ")
                                        const alAire = modelData.scenes.indexOf(Studio.currentScene) >= 0
                                        ApplicationWindow.window.confirm({
                                            title: qsTr("¿Quitar la entrada «%1»?").arg(nombre),
                                            text: (alAire ? qsTr("Está en la escena al aire: desaparece del aire al instante.") + " " : "")
                                                  + (escenas !== "" ? qsTr("Se saca de todas las escenas donde está (%1) y el cambio se guarda.").arg(escenas)
                                                                    : qsTr("No está en ninguna escena.")),
                                            accept: qsTr("Quitar"),
                                            reject: qsTr("Cancelar"),
                                            action: function() { inp.remove(nombre) }
                                        })
                                    }
                                }
                            }
                            // El dispositivo, con el nombre del operador (el id técnico, al pasar el mouse).
                            Text {
                                Layout.fillWidth: true
                                text: modelData.estado === "ausente" ? qsTr("● desconectado · %1").arg(modelData.where) : modelData.where
                                color: modelData.estado === "ausente" ? Theme.warn : Theme.textFaint
                                font.pixelSize: 11; elide: Text.ElideMiddle
                                ToolTip.visible: whereHover.hovered && modelData.tecnico !== modelData.where
                                ToolTip.text: modelData.tecnico || ""
                                HoverHandler { id: whereHover }
                            }
                            Text { Layout.fillWidth: true; text: modelData.scenes.length > 0 ? qsTr("En: %1").arg(modelData.scenes.join(", ")) : qsTr("No está en ninguna escena")
                                   color: Theme.textFaint; font.pixelSize: 11; elide: Text.ElideRight }
                        }
                    }
                }
            }
        }
    }

    BuscadorRed { id: buscador }
}
