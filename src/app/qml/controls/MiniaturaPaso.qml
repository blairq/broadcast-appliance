// La miniatura de un paso de la línea de tiempo (escaleta, Storyboard, conducción y
// las cajas de la Línea). De dónde sale lo decide el presupuesto de LineaViewModel
// (`mini`: {fuente, clave, escena}): el programa o el PVW de la salida, una vista viva
// chica («mini:<escena>»), o una foto que se saca cada tanto SOLO si la caja se ve —
// por eso avisa con mirarFoto al aparecer y al irse—.
import QtQuick
import BA

Rectangle {
    id: m
    /// {fuente: programa|pvw|viva|foto|"", clave, escena}
    property var mini: ({})
    readonly property string fuente: mini && mini.fuente ? mini.fuente : ""
    readonly property string escena: mini && mini.escena ? mini.escena : ""
    readonly property bool esFoto: fuente === "foto" && escena !== ""
    readonly property var foto: esFoto && Studio.linea ? Studio.linea.fotos[escena] : undefined
    radius: 4
    color: "#000000"
    clip: true

    VideoItem {
        id: video
        anchors.fill: parent
        visible: m.mini && !!m.mini.clave
        source: m.mini && m.mini.clave ? m.mini.clave : ""
        fillMode: VideoItem.Cover
    }
    Loader {
        anchors.fill: parent
        active: m.esFoto && m.foto !== undefined
        sourceComponent: ImagenViva { imagen: m.foto; ajustar: false }
    }
    // Sin escena (una pausa, una espera) o la foto que todavía no llegó: el nombre.
    Text {
        anchors { fill: parent; margins: 3 }
        visible: !video.hasFrame && m.foto === undefined
        text: m.escena !== "" ? m.escena : "—"
        color: Theme.textFaint
        font.pixelSize: Math.max(8, Math.min(11, m.height / 4))
        wrapMode: Text.WrapAnywhere
        elide: Text.ElideRight
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
    }
    // La edad de la foto: «hace 20 s» (no es en vivo).
    Text {
        anchors { right: parent.right; bottom: parent.bottom; margins: 2 }
        visible: m.esFoto && m.foto !== undefined && m.height > 40
        text: Studio.linea ? qsTr("hace %1").arg(Theme.hace(Studio.linea.edadFotos[m.escena] || 0)) : ""
        color: Theme.text
        font.pixelSize: 9
        style: Text.Outline
        styleColor: "#000000"
    }

    // Solo se fotografía lo que se ve: el ListView crea y destruye las cajas al desplazar.
    property string avisada: ""
    function avisar() {
        const quiere = esFoto && visible ? escena : ""
        if (quiere === avisada || !Studio.linea) return
        if (avisada !== "") Studio.linea.mirarFoto(avisada, false)
        if (quiere !== "") Studio.linea.mirarFoto(quiere, true)
        avisada = quiere
    }
    onEsFotoChanged: avisar()
    onEscenaChanged: avisar()
    onVisibleChanged: avisar()
    Component.onCompleted: avisar()
    Component.onDestruction: if (avisada !== "" && Studio.linea) Studio.linea.mirarFoto(avisada, false)
}
