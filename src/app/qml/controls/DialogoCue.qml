// Editar un cue del guion, en un modal (lo abre el lápiz de la fila o al agregarlo).
// Cada campo con su control: lo que se elige de una lista (la escena, el archivo de
// los medios, el discurso, el gráfico y su fuente, quién habla, la acción) es un
// selector con las opciones de verdad (EventoViewModel.opcionesDeCampo); un sí/no es
// un interruptor; el resto, texto. Los cambios se aplican al toque (como antes), y
// la lista del guion los muestra enseguida (el modelo avisa).
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import BA

Dialog {
    id: dlg
    required property var ev
    property int bloque: -1
    property int cue: -1
    /// El cue tal como está en el guion (se relee cuando el guion cambia).
    readonly property var c: {
        const bs = dlg.ev ? dlg.ev.guion.bloques || [] : []
        if (dlg.bloque < 0 || dlg.bloque >= bs.length) return null
        const cs = bs[dlg.bloque].cues || []
        return dlg.cue >= 0 && dlg.cue < cs.length ? cs[dlg.cue] : null
    }
    readonly property var campos: {
        if (!dlg.c) return []
        for (const t of dlg.ev.tiposDeCue()) if (t.tipo === dlg.c.tipo) return t.campos
        return []
    }
    readonly property var kSiNo: ["repetir", "alAire", "arrancar"]
    readonly property var kNombres: ({
        escena: "Escena", archivo: "Archivo (de los medios del evento)", accion: "Qué hace",
        repetir: "En bucle", alAire: "Ponerlo al aire en su escena", discurso: "Discurso",
        arrancar: "Arrancarlo al cargarlo", participante: "Quién habla", fuente: "Fuente",
        grafico: "Gráfico", pantalla: "Pantalla", contenido: "Qué muestra", db: "Volumen (dB)"
    })

    function abrir(b, c) { dlg.bloque = b; dlg.cue = c; open() }
    function poner(campo, valor) {
        let d = {}
        d[campo] = valor
        dlg.ev.editarCue(dlg.bloque, dlg.cue, { datos: d })
    }

    anchors.centerIn: parent
    width: Math.min(parent ? parent.width - 80 : 640, 620)
    height: Math.min(parent ? parent.height - 80 : 720, 720)
    modal: true
    title: dlg.c ? "Cue " + (dlg.cue + 1) + " del bloque " + (dlg.bloque + 1) + " · " + dlg.c.tipo : "Cue"

    ScrollView {
        id: scroll
        anchors.fill: parent
        clip: true
        contentWidth: availableWidth
        ScrollBar.horizontal.policy: ScrollBar.AlwaysOff

        ColumnLayout {
            width: scroll.availableWidth
            spacing: 10

            Text {
                visible: !!dlg.c && dlg.c.falta !== ""
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                text: dlg.c ? "⚠ " + dlg.c.falta : ""
                color: Theme.live
                font.pixelSize: 12
            }

            Text { text: "Qué dice (lo que leés cuando toca)"; color: Theme.textDim; font.pixelSize: 11 }
            TextArea {
                Layout.fillWidth: true
                Layout.preferredHeight: 64
                wrapMode: TextEdit.Wrap
                text: dlg.c ? dlg.c.texto : ""
                placeholderText: "Lo que vas a leer cuando toque"
                color: Theme.text
                font.pixelSize: 12
                onEditingFinished: if (dlg.c && text !== dlg.c.texto) dlg.ev.editarCue(dlg.bloque, dlg.cue, { texto: text })
            }

            // Los campos propios del tipo.
            Repeater {
                model: dlg.campos
                delegate: ColumnLayout {
                    id: campo
                    required property string modelData
                    readonly property var valor: dlg.c ? dlg.c.datos[modelData] : undefined
                    readonly property var opciones: dlg.c ? dlg.ev.opcionesDeCampo(dlg.c.tipo, modelData) : []
                    readonly property bool siNo: dlg.kSiNo.indexOf(modelData) >= 0
                    Layout.fillWidth: true
                    spacing: 3

                    RowLayout {
                        Layout.fillWidth: true
                        Text {
                            Layout.fillWidth: true
                            text: dlg.kNombres[campo.modelData] || campo.modelData
                            color: Theme.textDim; font.pixelSize: 11
                        }
                        Toggle {
                            visible: campo.siNo
                            checked: campo.valor === true || campo.valor === "true"
                            onToggled: (w) => dlg.poner(campo.modelData, w)
                        }
                    }
                    // De una lista: la acción es cerrada; lo demás se puede escribir
                    // (un archivo que todavía no está, alguien que entra después).
                    ComboBox {
                        id: combo
                        visible: !campo.siNo && campo.opciones.length > 0
                        Layout.fillWidth: true
                        editable: campo.modelData !== "accion"
                        model: campo.opciones
                        textRole: "texto"
                        valueRole: "valor"
                        currentIndex: {
                            const v = campo.valor === undefined ? "" : String(campo.valor)
                            for (let i = 0; i < campo.opciones.length; ++i) if (campo.opciones[i].valor === v) return i
                            return -1
                        }
                        // Lo que está guardado aunque no esté en la lista (se ve igual).
                        Component.onCompleted: if (editable && currentIndex < 0 && campo.valor !== undefined) editText = String(campo.valor)
                        displayText: currentIndex >= 0 ? currentText
                                   : (campo.valor !== undefined && campo.valor !== "" ? String(campo.valor) : "Elegí…")
                        onActivated: (i) => dlg.poner(campo.modelData, campo.opciones[i].valor)
                        onAccepted: if (editText !== "" && editText !== String(campo.valor)) dlg.poner(campo.modelData, editText)
                    }
                    TextField {
                        visible: !campo.siNo && campo.opciones.length === 0
                        Layout.fillWidth: true
                        text: campo.valor === undefined ? "" : String(campo.valor)
                        color: Theme.text
                        font.pixelSize: 12
                        inputMethodHints: campo.modelData === "db" ? Qt.ImhFormattedNumbersOnly : Qt.ImhNone
                        onEditingFinished: {
                            if (text === (campo.valor === undefined ? "" : String(campo.valor))) return
                            dlg.poner(campo.modelData, campo.modelData === "db" ? (parseFloat(text) || 0) : text)
                        }
                    }
                }
            }

            // Un cue de gráfico lleva ADEMÁS los campos del gráfico elegido (de su ficha:
            // un gráfico nuevo aparece acá sin tocar esta pantalla).
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 6
                visible: !!dlg.c && dlg.c.tipo === "grafico" && (dlg.c.datos.accion || "mostrar") === "mostrar"
                Text { text: "Lo que dice el gráfico"; color: Theme.text; font { pixelSize: 12; weight: Font.DemiBold } }
                Repeater {
                    model: dlg.c && dlg.c.tipo === "grafico" ? dlg.ev.parametrosDeGrafico(dlg.c.datos.grafico || "") : []
                    delegate: ColumnLayout {
                        required property var modelData
                        Layout.fillWidth: true
                        spacing: 2
                        Text { text: modelData.titulo || modelData.clave; color: Theme.textDim; font.pixelSize: 11 }
                        TextField {
                            Layout.fillWidth: true
                            color: Theme.text
                            font.pixelSize: 12
                            // Vacío = el valor por defecto del gráfico (se ve como marca de agua).
                            placeholderText: String(modelData.porDefecto !== undefined ? modelData.porDefecto : "")
                            text: {
                                const v = dlg.c && dlg.c.datos.valores
                                return !v || v[modelData.clave] === undefined ? "" : String(v[modelData.clave])
                            }
                            onEditingFinished: dlg.ev.setValorDeCue(dlg.bloque, dlg.cue, modelData.clave, text)
                        }
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 16
                ColumnLayout {
                    spacing: 2
                    Text { text: "Dura (segundos)"; color: Theme.textDim; font.pixelSize: 11 }
                    TextField {
                        Layout.preferredWidth: 110
                        text: dlg.c ? String(dlg.c.duracionSeg) : "0"
                        color: Theme.text
                        font.pixelSize: 12
                        validator: IntValidator { bottom: 0; top: 86400 }
                        onEditingFinished: dlg.ev.editarCue(dlg.bloque, dlg.cue, { duracionSeg: parseInt(text) || 0 })
                    }
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 2
                    Text { text: "Encadena solo con el siguiente"; color: Theme.textDim; font.pixelSize: 11 }
                    Toggle {
                        checked: dlg.c ? dlg.c.automatico : false
                        onToggled: (w) => dlg.ev.editarCue(dlg.bloque, dlg.cue, { automatico: w })
                    }
                }
            }
        }
    }

    footer: RowLayout {
        spacing: 8
        Item { Layout.preferredWidth: 8 }
        GlassButton {
            icon: "✕"; text: "Quitar el cue"; compact: true
            onClicked: { const b = dlg.bloque, c = dlg.cue; dlg.close(); dlg.ev.quitarCue(b, c) }
        }
        GlassButton {
            text: "Poner al aire"; compact: true
            onClicked: dlg.ev.irA(dlg.bloque, dlg.cue)
        }
        Item { Layout.fillWidth: true }
        GlassButton { text: "Listo"; active: true; onClicked: dlg.close() }
        Item { Layout.preferredWidth: 8; Layout.preferredHeight: 52 }
    }
}
