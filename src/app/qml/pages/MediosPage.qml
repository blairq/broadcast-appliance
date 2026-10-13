// Medios: los archivos del operador — música, videos, imágenes, presentaciones
// — y los carruseles que se arman con ellos.
//
// POR QUE EXISTE ESTA PESTAÑA. `medios` era una de las dos únicas categorías
// del MCP sin una sola línea de interfaz: el operador no tenía dónde ver sus
// propios archivos. Para poner un video de fondo había que pedírselo al
// asistente o saberse la ruta de memoria, en un aparato donde todo lo demás se
// hace con las manos.
//
// DOS SOLAPAS Y NO DOS PESTAÑAS. Los carruseles se arman ACÁ y no adentro de
// una escena: uno se arma una vez y se aplica en la apertura y en el cierre. Y
// se arman donde están los PDF y los PowerPoint de los que salen, que es esta
// misma pantalla.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import BA
import "../controls"

Rectangle {
    id: page
    color: Theme.bg
    property real leftInset: 92

    readonly property var m: Studio.medios
    readonly property var cc: Studio.carruseles
    /// 0 = los archivos, 1 = armar carruseles.
    property int solapa: 0

    function tamano(bytes) {
        if (bytes < 1024) return bytes + " B"
        if (bytes < 1024 * 1024) return Math.round(bytes / 1024) + " KB"
        if (bytes < 1024 * 1024 * 1024) return (bytes / 1024 / 1024).toFixed(1) + " MB"
        return (bytes / 1024 / 1024 / 1024).toFixed(1) + " GB"
    }

    /// De lo elegido, el primero que se puede desarmar en láminas.
    function primerDocumento() {
        const s = page.m.seleccion
        for (let i = 0; i < s.length; i++) {
            const e = s[i].split(".").pop().toLowerCase()
            if (e === "pdf" || e === "pptx" || e === "ppt" || e === "odp") return s[i]
        }
        return ""
    }

    ColumnLayout {
        anchors { fill: parent; topMargin: 18; bottomMargin: 14 }
        anchors.leftMargin: page.leftInset
        anchors.rightMargin: 20
        spacing: 12

        // --- Archivos / Carruseles -------------------------------------------
        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            GlassButton {
                text: "Archivos"
                active: page.solapa === 0
                onClicked: page.solapa = 0
            }
            GlassButton {
                text: "Carruseles"
                active: page.solapa === 1
                onClicked: page.solapa = 1
            }
            GlassButton {
                text: "Playlist"
                active: page.solapa === 2
                onClicked: page.solapa = 2
            }
            Item { Layout.fillWidth: true }

            // La barra de trabajo. Desarmar cincuenta láminas tarda; sin esto
            // la ventana se queda muda y el operador no sabe si está
            // trabajando o se colgó, diez minutos antes de empezar.
            RowLayout {
                visible: page.cc.convirtiendo
                spacing: 8
                Text {
                    text: page.cc.etapa + (page.cc.total > 0
                          ? " " + page.cc.hecho + "/" + page.cc.total : "…")
                    color: Theme.textDim
                    font.pixelSize: 12
                }
                ProgressBar {
                    Layout.preferredWidth: 180
                    // LibreOffice no informa progreso: mientras convierte, la
                    // barra va indeterminada en vez de fingir un número.
                    indeterminate: page.cc.total <= 0
                    from: 0; to: Math.max(1, page.cc.total)
                    value: page.cc.hecho
                }
                GlassButton {
                    compact: true
                    text: "Cancelar"
                    onClicked: page.cc.cancelar()
                }
            }
        }

        Panel {
            Layout.fillWidth: true
            Layout.fillHeight: true
            PanelArchivos {
                anchors.fill: parent
                visible: page.solapa === 0
            }
            PanelCarruseles {
                anchors.fill: parent
                visible: page.solapa === 1
            }
            PanelPlaylist {
                anchors.fill: parent
                visible: page.solapa === 2
            }
        }
    }

    // --- Qué hacer con lo elegido --------------------------------------------
    // Aparece sólo cuando hay algo elegido: una barra de acciones permanente
    // con todo apagado ocupa lugar y no dice nada.
    Rectangle {
        visible: page.solapa === 0 && page.m.cuantosElegidos > 0
        anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
        anchors.leftMargin: page.leftInset
        anchors.rightMargin: 20
        anchors.bottomMargin: 14
        height: 56
        radius: Theme.radiusSmall
        color: Qt.rgba(0.06, 0.06, 0.07, 0.96)
        border.color: Theme.glassBorder

        RowLayout {
            anchors { fill: parent; leftMargin: 14; rightMargin: 14 }
            spacing: 10
            Text {
                Layout.fillWidth: true
                text: page.m.cuantosElegidos === 1
                      ? page.m.seleccion[0]
                      : page.m.cuantosElegidos + " archivos · " + page.tamano(page.m.bytesElegidos)
                color: Theme.text
                font.pixelSize: 13
                elide: Text.ElideMiddle
            }
            Text {
                visible: page.m.cuantosElegidos > 1
                text: "Arrastralos a una carpeta para moverlos"
                color: Theme.textFaint
                font.pixelSize: 11
            }
            GlassButton {
                visible: page.m.cuantosElegidos === 1
                text: "Usar en una escena"
                onClicked: ApplicationWindow.window.pedirAlAsistente(
                    "Poné este archivo en la escena que corresponda: " + page.m.seleccion[0])
            }
            GlassButton {
                visible: page.primerDocumento() !== ""
                // Lo hace el aparato, no el asistente: es una conversión con
                // pasos fijos, no algo que haya que pensar.
                enabled: !page.cc.convirtiendo
                text: "Armar un carrusel"
                icon: "▦"
                onClicked: {
                    page.solapa = 1
                    page.cc.desarmar(page.m.rutaAbsoluta(page.primerDocumento()), "")
                    page.m.noElegirNada()
                }
                ToolTip.visible: hovered
                ToolTip.delay: 400
                ToolTip.text: "Convertir cada página en una lámina de un carrusel"
            }
            GlassButton {
                text: "Sumar a la playlist"
                onClicked: {
                    const e = Studio.playlist.agregarVariosUi(page.m.seleccion.map((r) => page.m.rutaAbsoluta(r)))
                    if (e !== "") { Studio.notify(e, "error"); return }
                    page.m.noElegirNada()
                    page.solapa = 2
                }
            }
            GlassButton {
                visible: page.cc.actual !== ""
                text: "Sumar a «" + page.cc.actual + "»"
                onClicked: {
                    const s = page.m.seleccion
                    for (let i = 0; i < s.length; i++) page.cc.agregar(page.m.rutaAbsoluta(s[i]))
                    page.m.noElegirNada()
                }
            }
            GlassButton {
                text: "✕"
                compact: true
                onClicked: page.m.noElegirNada()
            }
        }
    }
}
