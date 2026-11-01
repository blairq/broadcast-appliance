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
    // El encuadre digital (lo avisa el modelo mientras se mueve, a quien lo mira).
    readonly property var pos: cc && cc.encuadres[entrada] ? cc.encuadres[entrada] : ({})
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
    Suscripcion { objetivo: Studio.frames; que: "entrada:" + dlg.entrada; activa: dlg.opened && dlg.modo === "camara" && dlg.entrada !== "" }

    Suscripcion { objetivo: dlg.cc; que: dlg.entrada; activa: dlg.opened && dlg.digital && dlg.entrada !== "" }
    // Interlock: la regla es del modelo (chequearMovimiento); si pide confirmar, se pregunta.
    function siNoAlAire(accion) {
        const motivo = dlg.cc ? dlg.cc.chequearMovimiento(dlg.entrada, false, "") : ""
        if (motivo === "") { accion(); return }
        ApplicationWindow.window.confirm({
            title: qsTr("La cámara «%1» está al aire").arg(dlg.entrada),
            text: qsTr("%1 ¿Mover igual?").arg(motivo),
            accept: qsTr("Mover igual"), reject: qsTr("Cancelar"), action: accion
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
        title: qsTr("Foto o video para el botón")
        nameFilters: [qsTr("Imágenes y videos (*.jpg *.jpeg *.png *.gif *.webp *.bmp *.heic *.mp4 *.mov *.m4v *.mkv *.webm *.avi)"),
                      qsTr("Todos (*)")]
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
            Text { text: qsTr("Cara del botón"); color: Theme.text; font { pixelSize: 16; weight: Font.DemiBold } }
            Item { Layout.fillWidth: true }
            GlassButton { compact: true; text: qsTr("Archivo…"); active: dlg.modo === "archivo"
                          onClicked: { dlg.modo = "archivo"; archivo.open() } }
            GlassButton { compact: true; text: qsTr("Cámara"); active: dlg.modo === "camara"; onClicked: dlg.modo = "camara" }
        }
        Text {
            Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.textDim; font.pixelSize: 12
            text: dlg.modo === "archivo"
                  ? qsTr("Una imagen, un GIF o un video: de un video se toman los primeros 5 segundos, que se repiten en el botón.")
                  : qsTr("Apuntá la cámara con el mouse y sacá una foto o un video corto (hasta 5 s, se repite en el botón).")
        }

        // --- Cámara -------------------------------------------------------------------
        ColumnLayout {
            visible: dlg.modo === "camara"
            Layout.fillWidth: true
            spacing: 8
            RowLayout {
                spacing: 8
                Text { text: qsTr("Cámara"); color: Theme.textDim; font.pixelSize: 12 }
                ComboBox {
                    Layout.fillWidth: true
                    model: dlg.cc ? dlg.cc.camaras.map(c => c.entrada) : []
                    currentIndex: model.indexOf(dlg.entrada)
                    onActivated: dlg.entrada = currentText
                }
            }
            ImagenApuntable {
                id: vista
                Layout.fillWidth: true
                Layout.preferredHeight: width * 9 / 16
                enabled: dlg.cam !== null
                fuente: dlg.entrada !== "" ? "entrada:" + dlg.entrada : ""
                ayuda: dlg.digital ? qsTr("Clic: apuntar · arrastrar: encuadrar · rueda: zoom · doble clic: cuadro completo · el cuadrado es lo que sale en el botón")
                                   : qsTr("Clic: apuntar · arrastrar: encuadrar · rueda: zoom · el cuadrado es lo que sale en el botón")
                // Lo que va a salir en el botón: el cuadrado del centro.
                Rectangle {
                    anchors.centerIn: parent
                    width: Math.min(vista.iw, vista.ih); height: width
                    color: "transparent"; border { color: Qt.rgba(1, 1, 1, 0.5); width: 1 }
                }
                onClic: (u, v) => dlg.siNoAlAire(function() { dlg.avisar(dlg.cc.apuntar(dlg.entrada, u, v, 0, 0)) })
                onCuadro: (u, v, an, al) => dlg.siNoAlAire(function() { dlg.avisar(dlg.cc.apuntar(dlg.entrada, u, v, an, al)) })
                onDobleClic: if (dlg.digital) dlg.siNoAlAire(function() { dlg.avisar(dlg.cc.encuadre(dlg.entrada, 0.5, 0.5, 1, 0.8)) })
                onRueda: (sentido) => {
                    if (dlg.digital) {
                        const z = dlg.pos.zoom || 1
                        dlg.avisar(dlg.cc.encuadre(dlg.entrada, dlg.pos.x || 0.5, dlg.pos.y || 0.5,
                                                   Math.max(1, Math.min(8, z * (sentido > 0 ? 1.15 : 1 / 1.15))), 0.15))
                    } else {
                        // Óptico: un empujón de zoom que se frena solo (como la ventana del PTZ).
                        dlg.avisar(dlg.cc.mover(dlg.entrada, 0, 0, sentido * 0.5))
                        pararZoom.restart()
                    }
                }
                Timer { id: pararZoom; interval: 250; onTriggered: dlg.cc.parar(dlg.entrada) }
            }
            RowLayout {
                spacing: 8
                GlassButton { text: qsTr("📷  Foto"); active: true; enabled: dlg.esperando === "" && dlg.entrada !== ""
                              onClicked: dlg.empezar(dlg.caras.capturarFoto(dlg.entrada)) }
                GlassButton { text: qsTr("⏺  Video de %1 s").arg(dlg.segundos); enabled: dlg.esperando === "" && dlg.entrada !== ""
                              onClicked: dlg.empezar(dlg.caras.grabar(dlg.entrada, dlg.segundos)) }
                Slider { from: 1; to: 5; stepSize: 1; value: dlg.segundos; Layout.preferredWidth: 120
                         onMoved: dlg.segundos = value }
                Item { Layout.fillWidth: true }
            }
        }

        RowLayout {
            Text { Layout.fillWidth: true; text: dlg.caras.estado; color: dlg.caras.ocupada ? Theme.accent : Theme.textFaint
                   font.pixelSize: 12; elide: Text.ElideRight }
            GlassButton { compact: true; text: qsTr("Cerrar"); onClicked: dlg.close() }
        }
    }
}
