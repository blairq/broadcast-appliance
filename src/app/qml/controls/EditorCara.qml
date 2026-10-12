// La cara de un botón de la botonera: una foto o un video de hasta 5 s.
//
//   - «Archivo»: una imagen, un GIF o un video (se toman los primeros 5 s).
//   - «Cámara»: la imagen en vivo de una cámara, que se apunta con el mouse
//     como en la página Cámaras (clic: apuntar, arrastrar: encuadrar ese
//     cuadro, rueda: zoom), y «Foto» o «Video» de 1 a 5 s.
//
// Termina con `elegida(id)`: la celda la nombra «medio:<id>». Lo hace
// Studio.carasBotonera (control/CarasBotonera), que la deja lista para la
// tablet y el Stream Deck.
import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import BA

Popup {
    id: dlg
    signal elegida(string id)

    readonly property var caras: Studio.carasBotonera
    readonly property var cc: Studio.camaras
    property string modo: "archivo"          // archivo | camara
    property string entrada: ""
    property real segundos: 5
    property var pos: ({})
    readonly property var cam: {
        const l = cc ? cc.camaras : []
        for (let i = 0; i < l.length; ++i) if (l[i].entrada === entrada) return l[i]
        return null
    }
    readonly property bool digital: cam !== null && cam.modo === "digital"

    function abrir(comoCamara) {
        modo = comoCamara ? "camara" : "archivo"
        if (entrada === "" && cc && cc.camaras.length > 0) entrada = cc.camaras[0].entrada
        open()
        if (!comoCamara) archivo.open()
    }

    // La imagen en vivo se pide solo mientras se ve (renderizarla cuesta).
    property string mirando: ""
    function mirar() {
        const quiero = dlg.opened && dlg.modo === "camara" ? dlg.entrada : ""
        if (quiero === dlg.mirando) return
        if (dlg.mirando !== "") Studio.frames.mirarEntrada(dlg.mirando, false)
        if (quiero !== "") Studio.frames.mirarEntrada(quiero, true)
        dlg.mirando = quiero
    }
    onOpenedChanged: mirar()
    onModoChanged: mirar()
    onEntradaChanged: mirar()
    Component.onDestruction: if (mirando !== "") Studio.frames.mirarEntrada(mirando, false)

    Timer {
        interval: 150; repeat: true; running: dlg.opened && dlg.digital
        onTriggered: dlg.pos = dlg.cc.posicion(dlg.entrada)
    }
    // Interlock: la cámara AL AIRE no se mueve sin preguntar.
    function siNoAlAire(accion) {
        if (!dlg.cc || !dlg.cc.alAire(dlg.entrada) || !dlg.cc.interlock().activo) { accion(); return }
        ApplicationWindow.window.confirm({
            title: "La cámara «" + dlg.entrada + "» está al aire",
            text: "Moverla se ve en la transmisión. ¿Mover igual?",
            accept: "Mover igual", reject: "Cancelar", action: accion
        })
    }
    function avisar(e) { if (e) Studio.notify(e, "error") }
    // La toma propia en curso (las de MCP corren aparte).
    property string esperando: ""
    function empezar(r) {
        if (r.error) { Studio.notify(r.error, "error"); return }
        dlg.esperando = r.id
    }

    Connections {
        target: dlg.caras
        function onLista(id, error) {
            if (id !== dlg.esperando) return
            dlg.esperando = ""
            if (error !== "") { Studio.notify(error, "error"); return }
            if (!dlg.opened) return
            dlg.elegida(id)
            dlg.close()
        }
    }

    FileDialog {
        id: archivo
        title: "Foto o video para el botón"
        nameFilters: ["Imágenes y videos (*.jpg *.jpeg *.png *.gif *.webp *.bmp *.heic *.mp4 *.mov *.m4v *.mkv *.webm *.avi)",
                      "Todos (*)"]
        onAccepted: dlg.empezar(dlg.caras.importar(selectedFile.toString()))
        onRejected: if (dlg.modo === "archivo") dlg.close()
    }

    modal: true
    anchors.centerIn: Overlay.overlay
    width: Math.min(760, (parent ? parent.width : 800) - 40)
    padding: 18
    background: Rectangle { color: Theme.panel; radius: 14; border.color: Theme.glassBorder }

    contentItem: ColumnLayout {
        spacing: 10
        RowLayout {
            spacing: 8
            Text { text: "Cara del botón"; color: Theme.text; font { pixelSize: 16; weight: Font.DemiBold } }
            Item { Layout.fillWidth: true }
            GlassButton { compact: true; text: "Archivo…"; active: dlg.modo === "archivo"
                          onClicked: { dlg.modo = "archivo"; archivo.open() } }
            GlassButton { compact: true; text: "Cámara"; active: dlg.modo === "camara"; onClicked: dlg.modo = "camara" }
        }
        Text {
            Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.textDim; font.pixelSize: 12
            text: dlg.modo === "archivo"
                  ? "Una imagen, un GIF o un video: de un video se toman los primeros 5 segundos, que se repiten en el botón."
                  : "Apuntá la cámara con el mouse y sacá una foto o un video corto (hasta 5 s, se repite en el botón)."
        }

        // --- Cámara -------------------------------------------------------------------
        ColumnLayout {
            visible: dlg.modo === "camara"
            Layout.fillWidth: true
            spacing: 8
            RowLayout {
                spacing: 8
                Text { text: "Cámara"; color: Theme.textDim; font.pixelSize: 12 }
                ComboBox {
                    Layout.fillWidth: true
                    model: dlg.cc ? dlg.cc.camaras.map(c => c.entrada) : []
                    currentIndex: model.indexOf(dlg.entrada)
                    onActivated: dlg.entrada = currentText
                }
            }
            Rectangle {
                id: vista
                Layout.fillWidth: true
                Layout.preferredHeight: width * 9 / 16
                radius: 10; color: "black"; clip: true
                VideoItem { id: video; anchors.fill: parent; source: dlg.entrada !== "" ? "entrada:" + dlg.entrada : ""; fillMode: VideoItem.Fit }
                Text { visible: !video.hasFrame; anchors.centerIn: parent; text: "Sin señal"; color: Theme.textFaint; font.pixelSize: 13 }
                readonly property real aspecto: video.frameSize.width > 0 ? video.frameSize.width / video.frameSize.height : 16 / 9
                readonly property real iw: Math.min(width, height * aspecto)
                readonly property real ih: iw / aspecto
                readonly property real ix: (width - iw) / 2
                readonly property real iy: (height - ih) / 2
                function u(x) { return Math.max(0, Math.min(1, (x - ix) / iw)) }
                function v(y) { return Math.max(0, Math.min(1, (y - iy) / ih)) }
                // Lo que va a salir en el botón: el cuadrado del centro.
                Rectangle {
                    anchors.centerIn: parent
                    width: Math.min(vista.iw, vista.ih); height: width
                    color: "transparent"; border { color: Qt.rgba(1, 1, 1, 0.5); width: 1 }
                }
                Rectangle { id: cuadro; visible: false; color: Qt.rgba(1, 0.42, 0.17, 0.15); border { color: Theme.accent; width: 2 } }
                Text {
                    anchors { left: parent.left; right: parent.right; bottom: parent.bottom; margins: 6 }
                    horizontalAlignment: Text.AlignHCenter; elide: Text.ElideRight
                    text: "Clic: apuntar · arrastrar: encuadrar · " + (dlg.digital ? "rueda: zoom · doble clic: cuadro completo" : "rueda: zoom") +
                          " · el cuadrado es lo que sale en el botón"
                    color: Qt.rgba(1, 1, 1, 0.6); font.pixelSize: 10
                    style: Text.Outline; styleColor: Qt.rgba(0, 0, 0, 0.7)
                }
                MouseArea {
                    anchors.fill: parent
                    enabled: dlg.cam !== null
                    preventStealing: true
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
                        dlg.siNoAlAire(function() { dlg.avisar(dlg.cc.apuntar(dlg.entrada, u, v, an, al)) })
                    }
                    onDoubleClicked: if (dlg.digital) dlg.siNoAlAire(function() { dlg.avisar(dlg.cc.encuadre(dlg.entrada, 0.5, 0.5, 1, 0.8)) })
                    onWheel: (w) => {
                        if (dlg.digital) {
                            const z = dlg.pos.zoom || 1
                            dlg.avisar(dlg.cc.encuadre(dlg.entrada, dlg.pos.x || 0.5, dlg.pos.y || 0.5,
                                                       Math.max(1, Math.min(8, z * (w.angleDelta.y > 0 ? 1.15 : 1 / 1.15))), 0.15))
                        } else {
                            dlg.cc.mover(dlg.entrada, 0, 0, w.angleDelta.y > 0 ? 0.5 : -0.5)
                            pararZoom.restart()
                        }
                    }
                }
                Timer { id: pararZoom; interval: 250; onTriggered: dlg.cc.mover(dlg.entrada, 0, 0, 0) }
            }
            RowLayout {
                spacing: 8
                GlassButton { text: "📷  Foto"; active: true; enabled: dlg.esperando === "" && dlg.entrada !== ""
                              onClicked: dlg.empezar(dlg.caras.capturarFoto(dlg.entrada)) }
                GlassButton { text: "⏺  Video de " + dlg.segundos + " s"; enabled: dlg.esperando === "" && dlg.entrada !== ""
                              onClicked: dlg.empezar(dlg.caras.grabar(dlg.entrada, dlg.segundos)) }
                Slider { from: 1; to: 5; stepSize: 1; value: dlg.segundos; Layout.preferredWidth: 120
                         onMoved: dlg.segundos = value }
                Item { Layout.fillWidth: true }
            }
        }

        RowLayout {
            Text { Layout.fillWidth: true; text: dlg.caras.estado; color: dlg.caras.ocupada ? Theme.accent : Theme.textFaint
                   font.pixelSize: 12; elide: Text.ElideRight }
            GlassButton { compact: true; text: "Cerrar"; onClicked: dlg.close() }
        }
    }
}
