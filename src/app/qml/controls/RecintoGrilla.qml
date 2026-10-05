// El recinto: las bancas como se ven desde la cabina, para tocar. Fuente:
// Studio.cuerpo.recinto (legislatura/Cuerpo) y Studio.bancas (la fila de
// oradores presenciales).
//
// TOCAR UNA BANCA abre lo que se puede hacer con ella: apuntar la cámara (la
// deja lista, sin sacar nada al aire), ponerla al aire (arma la escena de su
// cámara, abre su micrófono, arranca su reloj) o sumarla a la fila. La que
// habla va en rojo; las que esperan en la fila, con su número de turno.
//
// EDITAR: se toca una banca (o una de las que todavía no tienen lugar) y
// después el lugar vacío donde va. «Acomodar solas» las pone en orden.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import BA

ColumnLayout {
    id: rec
    spacing: 10
    readonly property var r: Studio.cuerpo.recinto
    readonly property var b: Studio.bancas.estado
    readonly property int filas: r.filas || 0
    readonly property int columnas: r.columnas || 0
    property bool editando: false
    property string elegida: ""
    property string aviso: ""

    function lugar(f, c) {
        const l = r.bancas || []
        for (let i = 0; i < l.length; ++i) if (l[i].fila === f && l[i].columna === c) return l[i]
        return null
    }
    function turno(id) {
        const c = b.cola || []
        for (let i = 0; i < c.length; ++i) if (c[i].id === id) return i + 1
        return 0
    }
    // Un color estable por bloque (sin tabla que mantener).
    function colorBloque(bloque) {
        if (!bloque) return Qt.rgba(1, 1, 1, 0.25)
        let h = 0
        for (let i = 0; i < bloque.length; ++i) h = (h * 31 + bloque.charCodeAt(i)) % 360
        return Qt.hsla(h / 360, 0.55, 0.55, 1)
    }
    function miniatura(ptz) {
        if (!ptz || !ptz.camara || !Studio.camaras) return ""
        Studio.camaras.camaras // para que se actualice cuando cambian las posiciones
        const l = Studio.camaras.posiciones(ptz.camara)
        for (let i = 0; i < l.length; ++i)
            if (l[i].id === ptz.posicion || l[i].nombre.toLowerCase() === String(ptz.posicion).toLowerCase())
                return l[i].miniatura_url || ""
        return ""
    }
    function editar(cambios) {
        const e = Studio.cuerpo.editarRecinto(cambios)
        rec.aviso = e
        return e === ""
    }

    // --- Cabecera --------------------------------------------------------------------
    RowLayout {
        Layout.fillWidth: true
        spacing: 8
        Text { text: "Filas"; color: Theme.textDim; font.pixelSize: 12 }
        SpinBox { from: 1; to: 40; value: Math.max(1, rec.filas); editable: true
                  onValueModified: rec.editar({filas: value, columnas: Math.max(1, rec.columnas)}) }
        Text { text: "Columnas"; color: Theme.textDim; font.pixelSize: 12 }
        SpinBox { from: 1; to: 60; value: Math.max(1, rec.columnas); editable: true
                  onValueModified: rec.editar({filas: Math.max(1, rec.filas), columnas: value}) }
        GlassButton {
            compact: true; text: "Acomodar solas"
            onClicked: {
                const n = (Studio.cuerpo.integrantes || []).filter(i => (i.banca || "") !== "").length
                const col = Math.max(rec.columnas, 1)
                rec.editar({filas: Math.max(rec.filas, Math.ceil(n / col), 1), columnas: col, auto: true})
            }
        }
        Item { Layout.fillWidth: true }
        GlassButton { compact: true; text: rec.editando ? "Listo" : "Editar lugares"; active: rec.editando
                      onClicked: { rec.editando = !rec.editando; rec.elegida = "" } }
    }
    Text {
        Layout.fillWidth: true; wrapMode: Text.WordWrap; font.pixelSize: 12
        visible: rec.aviso !== "" || rec.filas === 0 || rec.editando
        color: rec.aviso !== "" ? Theme.live : Theme.textFaint
        text: rec.aviso !== "" ? rec.aviso
              : rec.filas === 0 ? "Todavía no hay grilla. Cargá el cuerpo (Personas → Cuerpo) con su banca y su preset, y tocá «Acomodar solas»."
              : rec.elegida !== "" ? "Banca " + rec.elegida + ": tocá el lugar vacío donde va (o «Sacar»)."
              : "Tocá una banca y después el lugar donde va."
    }
    // Las que todavía no tienen lugar.
    Flow {
        Layout.fillWidth: true
        spacing: 6
        visible: rec.editando && (rec.r.sin_lugar || []).length > 0
        Text { text: "Sin lugar:"; color: Theme.textDim; font.pixelSize: 12 }
        Repeater {
            model: rec.r.sin_lugar || []
            delegate: GlassButton {
                required property var modelData
                compact: true; text: modelData; active: rec.elegida === modelData
                onClicked: rec.elegida = modelData
            }
        }
        GlassButton { compact: true; text: "Sacar"; visible: rec.elegida !== ""
                      onClicked: { rec.editar({mover: {banca: rec.elegida, fila: -1}}); rec.elegida = "" } }
    }

    // --- La grilla ---------------------------------------------------------------------
    Rectangle {
        Layout.fillWidth: true
        implicitHeight: 26
        visible: rec.filas > 0
        radius: 6
        color: Qt.rgba(1, 1, 1, 0.05)
        Text { anchors.centerIn: parent; text: "PRESIDENCIA"; color: Theme.textFaint
               font { pixelSize: 11; letterSpacing: 2; weight: Font.DemiBold } }
    }
    GridLayout {
        id: grilla
        Layout.fillWidth: true
        visible: rec.filas > 0
        columns: Math.max(1, rec.columnas)
        rowSpacing: 6; columnSpacing: 6
        Repeater {
            model: rec.filas * rec.columnas
            delegate: Rectangle {
                id: celda
                required property int index
                readonly property int fila: Math.floor(index / rec.columnas)
                readonly property int columna: index % rec.columnas
                readonly property var l: rec.lugar(fila, columna)
                readonly property var i: l && l.integrante ? l.integrante : null
                readonly property bool alAire: i !== null && rec.b.al_aire && rec.b.al_aire.id === i.id
                readonly property int turno: i !== null ? rec.turno(i.id) : 0
                readonly property string mini: i !== null ? rec.miniatura(i.preset_ptz) : ""
                Layout.fillWidth: true
                Layout.preferredHeight: 74
                radius: 8
                color: l === null ? "transparent" : (alAire ? Qt.rgba(0.9, 0.15, 0.15, 0.35) : Qt.rgba(1, 1, 1, 0.06))
                border.width: alAire || (l && rec.elegida === l.banca) ? 2 : 1
                border.color: alAire ? Theme.live : (l && rec.elegida === l.banca) ? Theme.accent
                              : l === null ? (rec.editando ? Qt.rgba(1, 1, 1, 0.15) : "transparent") : Qt.rgba(1, 1, 1, 0.1)
                clip: true
                // La foto del preset de fondo, apagada: se reconoce el plano de un vistazo.
                Image { anchors.fill: parent; source: celda.mini; fillMode: Image.PreserveAspectCrop; opacity: 0.35
                        visible: celda.mini !== ""; asynchronous: true }
                Rectangle { anchors { left: parent.left; top: parent.top; bottom: parent.bottom } width: 4
                            visible: celda.i !== null; color: rec.colorBloque(celda.i ? celda.i.bloque : "") }
                ColumnLayout {
                    anchors { fill: parent; leftMargin: 9; rightMargin: 5; topMargin: 4; bottomMargin: 4 }
                    visible: celda.l !== null
                    spacing: 0
                    RowLayout {
                        Layout.fillWidth: true
                        Text { text: celda.l ? celda.l.banca : ""; color: Theme.text; font { pixelSize: 15; weight: Font.Black } }
                        Item { Layout.fillWidth: true }
                        Text { visible: celda.turno > 0; text: "#" + celda.turno; color: Theme.amber; font { pixelSize: 11; weight: Font.DemiBold } }
                        Text { visible: celda.i !== null && !celda.i.validado; text: "⚠"; color: Theme.amber; font.pixelSize: 11
                               ToolTip.visible: hv.hovered; ToolTip.text: "Nombre sin validar (Personas → Cuerpo)"
                               HoverHandler { id: hv } }
                    }
                    Text { Layout.fillWidth: true; elide: Text.ElideRight; maximumLineCount: 2; wrapMode: Text.WordWrap
                           text: celda.i ? celda.i.nombre : "(nadie con esta banca)"
                           color: celda.i ? Theme.text : Theme.textFaint; font.pixelSize: 11 }
                    Item { Layout.fillHeight: true }
                    Text { Layout.fillWidth: true; elide: Text.ElideRight; font.pixelSize: 10
                           text: celda.i && celda.i.preset_ptz && celda.i.preset_ptz.camara ? "🎥 " + celda.i.preset_ptz.posicion : "sin preset"
                           color: celda.i && celda.i.preset_ptz && celda.i.preset_ptz.camara ? Theme.textDim : Theme.amber }
                }
                MouseArea {
                    anchors.fill: parent
                    onClicked: {
                        rec.aviso = ""
                        if (rec.editando) {
                            if (celda.l !== null) rec.elegida = rec.elegida === celda.l.banca ? "" : celda.l.banca
                            else if (rec.elegida !== "") {
                                rec.editar({mover: {banca: rec.elegida, fila: celda.fila, columna: celda.columna}})
                                rec.elegida = ""
                            }
                            return
                        }
                        if (celda.l !== null) menu.abrir(celda.l, celda)
                    }
                }
            }
        }
    }
    Item { Layout.fillHeight: true }

    Menu {
        id: menu
        property var l: null
        function abrir(l, item) { menu.l = l; popup(item, 0, item.height) }
        MenuItem { text: "Apuntar la cámara"; enabled: menu.l && menu.l.integrante
                   onTriggered: rec.aviso = Studio.bancas.apuntar(menu.l.banca) }
        MenuItem { text: "Al aire (arma su cámara)"; enabled: menu.l && menu.l.integrante
                   onTriggered: { const e = Studio.bancas.alAire(menu.l.banca, "armar"); rec.aviso = e !== "" ? e : Studio.bancas.ultimoAviso() } }
        MenuItem { text: "Sumar a la fila"; enabled: menu.l && menu.l.integrante
                   onTriggered: rec.aviso = Studio.bancas.agregar(menu.l.banca) }
        MenuItem { text: "Sacar de la fila"; enabled: menu.l && menu.l.integrante && rec.turno(menu.l.integrante.id) > 0
                   onTriggered: Studio.bancas.quitar(menu.l.banca) }
        MenuItem { text: "Terminar (cierra su micrófono)"
                   enabled: menu.l && menu.l.integrante && rec.b.al_aire && rec.b.al_aire.id === menu.l.integrante.id
                   onTriggered: Studio.bancas.terminar() }
    }
}
