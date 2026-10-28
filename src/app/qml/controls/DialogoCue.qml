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
        escena: qsTr("Escena"), archivo: qsTr("Archivo (de los medios del evento)"), accion: qsTr("Qué hace"),
        repetir: qsTr("En bucle"), alAire: qsTr("Ponerlo al aire en su escena"), discurso: qsTr("Discurso"),
        arrancar: qsTr("Arrancarlo al cargarlo"), participante: qsTr("Quién habla"), fuente: qsTr("Fuente"),
        grafico: qsTr("Gráfico"), pantalla: qsTr("Pantalla"), contenido: qsTr("Qué muestra"), db: qsTr("Volumen (dB)"),
        seVa: qsTr("Cuándo se va de la pantalla"), carrusel: qsTr("Carrusel"), lamina: qsTr("Lámina"),
        prompter: qsTr("En qué teleprompter")
    })
    // Listas cerradas (no se escribe otra cosa).
    readonly property var kCerrados: ["accion", "seVa", "carrusel", "lamina", "prompter"]

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
    title: dlg.c ? qsTr("Cue %1 del bloque %2 · %3").arg(dlg.cue + 1).arg(dlg.bloque + 1).arg(dlg.c.tipo) : qsTr("Cue")

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

            Text { text: qsTr("Qué dice (lo que leés cuando toca)"); color: Theme.textDim; font.pixelSize: 11 }
            TextArea {
                Layout.fillWidth: true
                Layout.preferredHeight: 64
                wrapMode: TextEdit.Wrap
                text: dlg.c ? dlg.c.texto : ""
                placeholderText: qsTr("Lo que vas a leer cuando toque")
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
                    readonly property var opciones: dlg.c ? dlg.ev.opcionesDeCampo(dlg.c.tipo, modelData, dlg.c.datos) : []
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
                        editable: dlg.kCerrados.indexOf(campo.modelData) < 0
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
                                   : (campo.valor !== undefined && campo.valor !== "" ? String(campo.valor) : qsTr("Elegí…"))
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
                Text { text: qsTr("Lo que dice el gráfico"); color: Theme.text; font { pixelSize: 12; weight: Font.DemiBold } }
                Repeater {
                    model: dlg.c && dlg.c.tipo === "grafico" ? dlg.ev.parametrosDeGrafico(dlg.c.datos.grafico || "") : []
                    delegate: ColumnLayout {
                        required property var modelData
                        Layout.fillWidth: true
                        spacing: 2
                        // Los milisegundos de «cuándo se va» valen solo si se va sola (o si la cue
                        // deja decidir al gráfico): con «al avanzar» o «queda» no se usan.
                        visible: modelData.clave !== "salidaEn" || ["", "sola"].indexOf(dlg.c.datos.seVa || "") >= 0
                        Text {
                            text: modelData.tipo === "ms" ? qsTr("%1 (milisegundos)").arg(modelData.titulo || modelData.clave) : (modelData.titulo || modelData.clave)
                            color: Theme.textDim; font.pixelSize: 11
                        }
                        // Para qué sirve (de la ficha del gráfico), y lo que vale el tiempo.
                        Text {
                            Layout.fillWidth: true
                            visible: text !== ""
                            wrapMode: Text.WordWrap
                            text: (modelData.ayuda || "") +
                                  (modelData.clave === "salidaEn" ? (modelData.ayuda ? " " : "") +
                                   qsTr("A ese momento arranca la salida y el gráfico se va solo; 0 = queda puesto hasta sacarlo.") : "")
                            color: Theme.textFaint; font.pixelSize: 10
                        }
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
                    Text { text: qsTr("Cuánto dura en el guion (segundos)"); color: Theme.textDim; font.pixelSize: 11 }
                    Text {
                        text: qsTr("Para el reloj y la hora de fin; no cambia el gráfico ni el medio.")
                        color: Theme.textFaint; font.pixelSize: 10
                    }
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
                    Text { text: qsTr("Encadena solo con el siguiente"); color: Theme.textDim; font.pixelSize: 11 }
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
            icon: "✕"; text: qsTr("Quitar el cue"); compact: true
            onClicked: { const b = dlg.bloque, c = dlg.cue; dlg.close(); dlg.ev.quitarCue(b, c) }
        }
        GlassButton {
            text: qsTr("Poner al aire"); compact: true
            onClicked: dlg.ev.irA(dlg.bloque, dlg.cue)
        }
        GlassButton {
            readonly property string url: dlg.c ? dlg.ev.enlaceDeCue(dlg.c.tipo, dlg.c.datos || ({})) : ""
            visible: url !== ""
            text: qsTr("Ver el teleprompter ↗"); compact: true
            onClicked: Qt.openUrlExternally(url)
            ToolTip.visible: hovered; ToolTip.delay: 400; ToolTip.text: qsTr("La pantalla del orador en el navegador (%1)").arg(url)
        }
        Item { Layout.fillWidth: true }
        GlassButton { text: qsTr("Listo"); active: true; onClicked: dlg.close() }
        Item { Layout.preferredWidth: 8; Layout.preferredHeight: 52 }
    }
}
