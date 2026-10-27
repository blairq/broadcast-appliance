// El visor: doble clic en un archivo de Medios y se ve, acá mismo.
//
// PARA QUE. Antes de poner algo al aire hay que saber QUÉ es. La miniatura de
// la grilla alcanza para reconocer una imagen, pero no para saber si el video
// es el corte bueno o el de la prueba, ni si el audio es la cortina o la voz
// del orador. Y abrirlo en otro programa saca al operador del aparato, que es
// lo que este aparato existe para evitar.
//
// LO REPRODUCE FFMPEG, sin proceso aparte ni VLC: el motor de Qt Multimedia ES
// ffmpeg y Qt lo trae enlazado adentro de su plugin (se verificó con ldd: no
// pide ningún libav del sistema). Es el mismo Qt vendoreado que ya viaja, así
// que anda igual en Linux y en Windows.
//
// SE PARA AL CERRAR, SIEMPRE. Un video que sigue sonando atrás de la ventana
// cerrada es la clase de cosa que se descubre al aire.
//
// LAS FLECHAS PASAN AL SIGUIENTE, sobre lo que se está viendo y en el orden en
// que se ve: mirar diez tomas es mirarlas de corrido, no abrir y cerrar diez
// veces.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtMultimedia
import BA
import "."

Popup {
    id: visor

    readonly property var m: Studio.medios
    /// El índice dentro de lo que la grilla está mostrando.
    property int indice: -1
    readonly property var actual: (indice >= 0 && indice < m.archivos.length)
                                  ? m.archivos[indice] : null
    readonly property string tipo: actual ? actual.tipo : ""

    function abrir(i) {
        visor.indice = i
        visor.open()
    }
    function pasar(cuanto) {
        const n = visor.m.archivos.length
        if (n === 0) return
        // Da la vuelta: al llegar al final sigue por el principio, como el
        // carrusel. Frenar seco obliga a pensar dónde está uno.
        visor.indice = (visor.indice + cuanto + n) % n
    }

    modal: true
    focus: true
    // El padre es el OVERLAY de la ventana, no el panel que lo declara. Con el
    // panel de padre, un modal del tamaño de la pantalla queda atado a un
    // recuadro chico y no se ve: esto ya pasó y la captura salió sin modal.
    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(parent ? parent.width - 80 : 1200, 1280)
    height: Math.min(parent ? parent.height - 80 : 760, 800)
    padding: 0
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

    background: Rectangle {
        color: Qt.rgba(0.05, 0.05, 0.06, 0.98)
        radius: Theme.radius
        border.color: Theme.glassBorder
    }

    // Se pone a andar sola. Si alguien hizo doble clic en un video es porque
    // quiere VERLO; dejarlo en negro esperando que encuentre el botón de play
    // es un paso de más en el único momento en que no sobra tiempo.
    onOpened: if (visor.tipo === "video" || visor.tipo === "audio") reproductor.play()
    // Parar al cerrar no es prolijidad: un video que sigue sonando atrás de una
    // ventana cerrada se descubre al aire.
    onClosed: reproductor.stop()
    // Y al cambiar de archivo, también: si no, el anterior sigue sonando
    // debajo del que se está mirando.
    onIndiceChanged: {
        reproductor.stop()
        if (visor.tipo === "video" || visor.tipo === "audio") reproductor.play()
    }

    // Los atajos NO van en el Popup: `Keys` sólo se engancha a un Item, y un
    // Popup no lo es —Qt lo avisa en el registro y los atajos no andan—. Van
    // en un Shortcut, que además sólo escucha mientras el visor está abierto.
    Shortcut { sequence: "Left";  enabled: visor.visible; onActivated: visor.pasar(-1) }
    Shortcut { sequence: "Right"; enabled: visor.visible; onActivated: visor.pasar(1) }
    Shortcut {
        sequence: "Space"
        enabled: visor.visible
        onActivated: {
            if (reproductor.playbackState === MediaPlayer.PlayingState) reproductor.pause()
            else if (visor.tipo === "video" || visor.tipo === "audio") reproductor.play()
        }
    }

    MediaPlayer {
        id: reproductor
        source: (visor.visible && (visor.tipo === "video" || visor.tipo === "audio") && visor.actual)
                ? visor.actual.url : ""
        videoOutput: salida
        audioOutput: AudioOutput { id: audio; volume: 0.8 }
        onErrorOccurred: (err, txt) => mensajeError.text = txt
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 12
        spacing: 8

        // --- Qué se está viendo ----------------------------------------------
        RowLayout {
            Layout.fillWidth: true
            spacing: 10
            Text {
                Layout.fillWidth: true
                text: visor.actual ? visor.actual.nombre : ""
                color: Theme.text
                font { family: Theme.font; pixelSize: 15; weight: Font.DemiBold }
                elide: Text.ElideMiddle
            }
            Text {
                text: visor.actual
                      ? qsTr("%1 de %2 · %3").arg(visor.indice + 1).arg(visor.m.archivos.length).arg(visor.actual.carpeta)
                      : ""
                color: Theme.textFaint
                font.pixelSize: 11
            }
            GlassButton { compact: true; text: "✕"; onClicked: visor.close() }
        }

        // --- El archivo -------------------------------------------------------
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            radius: Theme.radiusSmall
            color: "#07090c"
            clip: true

            // Imagen: entera, sin recortar y sin agrandarla más allá de su
            // tamaño real (una miniatura estirada a pantalla completa se ve
            // peor que chica y centrada).
            Image {
                anchors.fill: parent
                anchors.margins: 6
                visible: visor.tipo === "imagen"
                source: (visor.visible && visor.tipo === "imagen" && visor.actual) ? visor.actual.url : ""
                fillMode: Image.PreserveAspectFit
                asynchronous: true
                mipmap: true
            }

            VideoOutput {
                id: salida
                anchors.fill: parent
                anchors.margins: 6
                visible: visor.tipo === "video"
                fillMode: VideoOutput.PreserveAspectFit
            }

            // Audio: no hay nada que mirar, así que se muestra lo que importa
            // —qué es y cuánto dura— en grande.
            ColumnLayout {
                anchors.centerIn: parent
                visible: visor.tipo === "audio"
                spacing: 10
                Text {
                    Layout.alignment: Qt.AlignHCenter
                    text: "♪"
                    color: Theme.textFaint
                    font.pixelSize: 72
                }
                Text {
                    Layout.alignment: Qt.AlignHCenter
                    text: visor.actual ? visor.actual.nombre : ""
                    color: Theme.textDim
                    font.pixelSize: 14
                }
            }

            // UN DOCUMENTO NO SE MIRA ACA, y es a proposito. Mostrar un PDF
            // pediria desplegar otro modulo QML entero para una vista de
            // lectura que no sirve en vivo: lo que el operador necesita de una
            // presentacion es poder SALTAR a una lamina, y para eso ya esta el
            // carrusel. El visor manda para alla en vez de fingir que sabe.
            Text {
                anchors.centerIn: parent
                visible: visor.tipo === "documento"
                width: parent.width - 80
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
                color: Theme.textFaint
                font.pixelSize: 13
                text: qsTr("Una presentación se mira desarmándola en láminas:\n" +
                           "así se puede saltar a cualquiera en vivo, que es para lo que sirve.\n\n" +
                           "Cerrá esto y usá «Armar un carrusel».")
            }

            Text {
                id: mensajeError
                anchors { left: parent.left; right: parent.right; bottom: parent.bottom; margins: 10 }
                color: Theme.live
                font.pixelSize: 12
                wrapMode: Text.WordWrap
                visible: text !== "" && (visor.tipo === "video" || visor.tipo === "audio")
            }
        }

        // --- Los controles ----------------------------------------------------
        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            visible: visor.tipo === "video" || visor.tipo === "audio"

            GlassButton {
                compact: true
                text: reproductor.playbackState === MediaPlayer.PlayingState ? "❚❚" : "▶"
                onClicked: reproductor.playbackState === MediaPlayer.PlayingState
                           ? reproductor.pause() : reproductor.play()
            }
            Text {
                text: visor.reloj(reproductor.position)
                color: Theme.textDim
                font.pixelSize: 11
            }
            Slider {
                id: barra
                Layout.fillWidth: true
                from: 0
                to: Math.max(1, reproductor.duration)
                // Mientras se arrastra NO se sigue la posición del
                // reproductor: el pulgar se escaparía de abajo del dedo.
                value: pressed ? value : reproductor.position
                onMoved: reproductor.position = value
            }
            Text {
                text: visor.reloj(reproductor.duration)
                color: Theme.textDim
                font.pixelSize: 11
            }
            // Sin emoji: el bundle lleva sus propias tipografías y no hay
            // ninguna garantía de que tengan el altavoz. Una palabra se ve
            // igual en las dos plataformas.
            Text { text: qsTr("Vol"); color: Theme.textFaint; font.pixelSize: 11 }
            Slider {
                Layout.preferredWidth: 90
                from: 0; to: 1
                value: audio.volume
                onMoved: audio.volume = value
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            GlassButton { compact: true; text: qsTr("◀ Anterior"); onClicked: visor.pasar(-1) }
            GlassButton { compact: true; text: qsTr("Siguiente ▶"); onClicked: visor.pasar(1) }
            Item { Layout.fillWidth: true }
            Text {
                text: visor.actual ? visor.actual.relativa : ""
                color: Theme.textFaint
                font.pixelSize: 10
                elide: Text.ElideMiddle
                Layout.maximumWidth: 380
            }
            GlassButton {
                text: qsTr("Usar en una escena")
                onClicked: {
                    ApplicationWindow.window.pedirAlAsistente(
                        qsTr("Poné este archivo en la escena que corresponda: %1").arg(visor.actual.relativa))
                    visor.close()
                }
            }
        }
    }

    /// m:ss a partir de milisegundos. Las horas sólo si las hay: «0:03:12»
    /// para un corte de tres minutos es ruido.
    function reloj(ms) {
        if (!ms || ms < 0) ms = 0
        const s = Math.floor(ms / 1000)
        const h = Math.floor(s / 3600)
        const m2 = Math.floor((s % 3600) / 60)
        const s2 = s % 60
        const dos = (n) => (n < 10 ? "0" : "") + n
        return h > 0 ? h + ":" + dos(m2) + ":" + dos(s2) : m2 + ":" + dos(s2)
    }
}
