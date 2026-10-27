// La urna, del lado del que conduce.
//
// SON DOS COSAS Y NO SE MEZCLAN, y por eso el modo se elige arriba y se lee en
// el título del resultado:
//   · Encuesta  — vota cualquiera con el enlace, una vez por teléfono. Los
//     números son INDICATIVOS y no sirven para decidir nada.
//   · Votación  — vota quien está en el padrón con derecho y ya llegó, con su
//     código. Es lo que hace que una asamblea valga.
//
// Abrir y cerrar se hace EN VIVO, con la sala mirando: por eso existe este
// panel y no alcanza con pedírselo al asistente.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import BA
import "."

Item {
    id: raiz

    readonly property var pub: Studio.publico
    /// Lo pone en true VentanaSuelta (ver PanelPreguntas).
    property bool enVentana: false
    readonly property var ev: Studio.evento
    readonly property var v: pub.votacion
    readonly property bool hay: v && v.hubo === true
    readonly property bool abierta: hay && v.abierta === true

    Component.onCompleted: pub.activo = true
    Component.onDestruction: pub.activo = false

    // Quién vota en una formal lo decide el modelo (los del padrón con voto que
    // llegaron): acá solo se mira cuántos hay para habilitar el botón.

    ColumnLayout {
        anchors.fill: parent
        spacing: 10

        RowLayout {
            Layout.fillWidth: true
            visible: !raiz.enVentana
            // El resultado OFICIAL (tablero del recinto o Secretaría): el que va al acta.
            GlassButton {
                compact: true
                text: qsTr("Cargar resultado oficial")
                onClicked: oficial.open()
            }
            Text {
                Layout.fillWidth: true
                elide: Text.ElideRight
                readonly property var ult: Studio.quorum.oficiales.length > 0 ? Studio.quorum.oficiales[Studio.quorum.oficiales.length - 1] : null
                text: !ult ? qsTr("Todavía no hay resultados oficiales (los del tablero o Secretaría, para el acta).")
                      : (ult.aprobado ? qsTr("Último oficial: %1 — APROBADO %2-%3-%4") : qsTr("Último oficial: %1 — RECHAZADO %2-%3-%4"))
                        .arg(ult.asunto).arg(ult.afirmativos).arg(ult.negativos).arg(ult.abstenciones)
                color: ult && ult.aprobado ? Theme.ok : Theme.textDim
                font.pixelSize: 11
            }
            GlassButton {
                compact: true
                text: "⧉"
                onClicked: ApplicationWindow.window.sacarPanel("votacion", qsTr("Votación"))
                ToolTip.visible: hovered
                ToolTip.delay: 400
                ToolTip.text: qsTr("Sacar a su propia ventana, para mandarla a otro monitor")
            }
        }

        // --- Lo que está pasando ahora ---------------------------------------
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: enCurso.implicitHeight + 24
            visible: raiz.hay
            radius: Theme.radiusSmall
            color: Qt.rgba(1, 1, 1, 0.04)
            border.color: raiz.abierta ? Theme.live : Theme.glassBorder

            ColumnLayout {
                id: enCurso
                anchors { left: parent.left; right: parent.right; verticalCenter: parent.verticalCenter
                          leftMargin: 14; rightMargin: 14 }
                spacing: 6

                RowLayout {
                    Layout.fillWidth: true
                    Text {
                        // Cuál fue, dicho siempre: una encuesta leída como
                        // decisión es el error que toda esta separación existe
                        // para evitar.
                        text: (raiz.v.modo === "formal" ? qsTr("VOTACIÓN") : qsTr("ENCUESTA")) +
                              (raiz.abierta ? " · " + qsTr("abierta") : " · " + qsTr("cerrada")) +
                              (raiz.v.secreta ? " · " + qsTr("secreta") : "")
                        color: raiz.abierta ? Theme.live : Theme.textDim
                        font { pixelSize: 11; weight: Font.DemiBold; letterSpacing: 0.6 }
                    }
                    Item { Layout.fillWidth: true }
                    Text {
                        // El QUÓRUM, con sus dos números. Sin el denominador,
                        // dos a favor y cero en contra parece unánime cuando en
                        // realidad no vino nadie.
                        text: raiz.v.podian > 0
                              ? qsTr("votaron %1 de %2").arg(raiz.v.votaron).arg(raiz.v.podian)
                              : qsTr("votaron %1").arg(raiz.v.votaron)
                        color: Theme.text
                        font { pixelSize: 13; weight: Font.DemiBold }
                    }
                }

                Text {
                    Layout.fillWidth: true
                    textFormat: Text.PlainText
                    wrapMode: Text.WordWrap
                    text: raiz.v.pregunta || ""
                    color: Theme.text
                    font.pixelSize: 15
                }

                Repeater {
                    model: raiz.v.resultado || []
                    delegate: ColumnLayout {
                        id: barra
                        required property var modelData
                        Layout.fillWidth: true
                        spacing: 2
                        readonly property int total: {
                            let t = 0
                            const rs = raiz.v.resultado || []
                            for (let i = 0; i < rs.length; ++i) t += rs[i].votos
                            return Math.max(1, t)
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            Text { text: barra.modelData.opcion; color: Theme.text; font.pixelSize: 13 }
                            Item { Layout.fillWidth: true }
                            Text { text: barra.modelData.votos; color: Theme.textDim; font.pixelSize: 13 }
                        }
                        Rectangle {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 8
                            radius: 4
                            color: Qt.rgba(0, 0, 0, 0.35)
                            Rectangle {
                                width: parent.width * (barra.modelData.votos / barra.total)
                                height: parent.height
                                radius: parent.radius
                                color: Theme.accent
                                Behavior on width { NumberAnimation { duration: 200 } }
                            }
                        }
                    }
                }

                Text {
                    Layout.fillWidth: true
                    visible: (raiz.v.faltanVotar || []).length > 0
                    wrapMode: Text.WordWrap
                    text: qsTr("Faltan votar: %1").arg((raiz.v.faltanVotar || []).join(", "))
                    color: Theme.amber
                    font.pixelSize: 12
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8
                    GlassButton {
                        visible: raiz.hay
                        text: qsTr("Proyectar")
                        icon: "⧉"
                        onClicked: proyectarDlg.open()
                        ToolTip.visible: hovered
                        ToolTip.delay: 400
                        ToolTip.text: qsTr("Mandarla a un televisor de la sala, con el QR para votar")
                    }
                    GlassButton {
                        visible: raiz.abierta
                        text: qsTr("Cerrar la votación")
                        onClicked: raiz.pub.cerrarVotacion()
                        ToolTip.visible: hovered
                        ToolTip.delay: 400
                        ToolTip.text: qsTr("Deja el resultado firme y se lo muestra a todos los teléfonos")
                    }
                    Toggle {
                        visible: raiz.abierta
                        checked: raiz.v.enVivo === true
                        onToggled: (w) => raiz.pub.mostrarEnVivo(w)
                    }
                    Text {
                        visible: raiz.abierta
                        text: qsTr("Mostrar el resultado mientras está abierta")
                        color: Theme.textDim
                        font.pixelSize: 12
                    }
                    Item { Layout.fillWidth: true }
                }
            }
        }

        Text {
            Layout.fillWidth: true
            visible: raiz.abierta && raiz.v.modo === "formal" && raiz.v.enVivo === true
            wrapMode: Text.WordWrap
            text: qsTr("Con el resultado a la vista, el que todavía no votó ve que va perdiendo y eso " +
                  "cambia lo que vota. En una votación formal conviene tenerlo apagado.")
            color: Theme.amber
            font.pixelSize: 11
        }

        // --- Abrir una nueva --------------------------------------------------
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: nueva.implicitHeight + 28
            radius: Theme.radiusSmall
            color: Qt.rgba(1, 1, 1, 0.03)
            border.color: Theme.glassBorder

            ColumnLayout {
                id: nueva
                anchors { left: parent.left; right: parent.right; top: parent.top; margins: 14 }
                spacing: 8

                Text {
                    text: raiz.abierta ? qsTr("Abrir otra (reemplaza a la que está abierta)") : qsTr("Abrir una votación o una encuesta")
                    color: Theme.text
                    font { pixelSize: 14; weight: Font.DemiBold }
                }
                Text {
                    Layout.fillWidth: true
                    visible: !raiz.abierta
                    wrapMode: Text.WordWrap
                    text: qsTr("Se vota desde el teléfono con el QR. La votación formal es la del padrón (vale para decidir); "
                          + "la encuesta es abierta a cualquiera y solo indica. Para votar con las bancas de la central, está Conferencia → Votación y asistencia.")
                    color: Theme.textFaint
                    font.pixelSize: 11
                }
                Text {
                    Layout.fillWidth: true
                    visible: raiz.abierta
                    wrapMode: Text.WordWrap
                    text: qsTr("La que está abierta queda registrada igual: para el acta importa que se " +
                          "votó, no que alguien se haya acordado de cerrarla.")
                    color: Theme.textFaint
                    font.pixelSize: 11
                }

                TextField {
                    id: campoPregunta
                    Layout.fillWidth: true
                    color: Theme.text
                    placeholderText: qsTr("¿Qué se vota? Ej.: ¿Aprobamos el balance?")
                }
                TextField {
                    id: campoOpciones
                    Layout.fillWidth: true
                    color: Theme.text
                    text: qsTr("A favor, En contra, Abstención")
                    placeholderText: qsTr("Opciones separadas por coma")
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 10
                    ComboBox {
                        id: modo
                        model: [
                            { texto: qsTr("Votación formal (del padrón)"), valor: "formal" },
                            { texto: qsTr("Encuesta abierta (indicativa)"), valor: "encuesta" }
                        ]
                        textRole: "texto"
                        valueRole: "valor"
                        Layout.preferredWidth: 260
                    }
                    Toggle {
                        id: secreta
                        enabled: modo.currentValue === "formal"
                    }
                    Text {
                        text: qsTr("Secreta")
                        color: modo.currentValue === "formal" ? Theme.text : Theme.textFaint
                        font.pixelSize: 12
                    }
                    Item { Layout.fillWidth: true }
                }

                Text {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    // Los números los calcula el padrón (modelo): con voto y presentes, con voto en total.
                    readonly property var resumen: raiz.ev ? raiz.ev.resumenPadron : ({})
                    color: modo.currentValue === "formal" && !(resumen.votoPresente > 0)
                           ? Theme.live : Theme.textDim
                    font.pixelSize: 12
                    text: modo.currentValue === "formal"
                          ? (!(resumen.votoPresente > 0)
                             ? qsTr("Nadie con derecho a voto llegó todavía. Una votación formal sin " +
                               "votantes da un resultado que parece válido.")
                             : qsTr("Pueden votar %1 presentes, de %2 con derecho en el padrón.")
                               .arg(resumen.votoPresente).arg(resumen.conVoto))
                          : qsTr("Vota cualquiera que tenga el enlace, una vez por teléfono. El resultado " +
                            "es indicativo: no sirve para decidir.")
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8
                    GlassButton {
                        text: modo.currentValue === "formal" ? qsTr("Abrir la votación") : qsTr("Abrir la encuesta")
                        active: true
                        enabled: campoPregunta.text.trim() !== "" &&
                                 (modo.currentValue === "encuesta" || (raiz.ev && raiz.ev.resumenPadron.votoPresente > 0))
                        onClicked: {
                            const ops = campoOpciones.text.split(",").map((s) => s.trim())
                                                                    .filter((s) => s !== "")
                            raiz.pub.abrirVotacion(campoPregunta.text, ops, modo.currentValue, secreta.checked, false)
                            campoPregunta.text = ""
                        }
                    }
                    Item { Layout.fillWidth: true }
                }
            }
        }
        // Lo que sobra va abajo: sin esto el layout repartía el alto
        // entre los campos y quedaban huecos enormes.
        Item { Layout.fillHeight: true }
    }

    // A que televisor. La lista es la misma de la pagina de Pantallas: el
    // modelo es uno solo (`Studio.pantallas`) y esto solo elige.
    Dialog {
        id: proyectarDlg
        anchors.centerIn: Overlay.overlay
        width: 420
        modal: true
        title: qsTr("Proyectar la votación")
        standardButtons: Dialog.Close
        ColumnLayout {
            spacing: 8
            Text {
                Layout.preferredWidth: 380
                wrapMode: Text.WordWrap
                text: qsTr("Se manda la que esté abierta, con el QR para votar desde el teléfono. El " +
                      "resultado se ve solo si decidiste mostrarlo.")
                color: Theme.textDim
                font.pixelSize: 12
            }
            Repeater {
                model: Studio.pantallas.pantallas
                delegate: GlassButton {
                    required property var modelData
                    Layout.preferredWidth: 380
                    text: modelData.nombre
                    onClicked: {
                        const err = Studio.pantallas.mostrar(modelData.id, "votacion", {})
                        if (err) Studio.notify(err, "error")
                        proyectarDlg.close()
                    }
                }
            }
            Text {
                Layout.preferredWidth: 380
                visible: Studio.pantallas.pantallas.length === 0
                wrapMode: Text.WordWrap
                text: qsTr("No hay ninguna pantalla. Buscalas en la página de Pantallas.")
                color: Theme.textFaint
                font.pixelSize: 12
            }
        }
    }

    Text {
        anchors.centerIn: parent
        visible: !raiz.hay
        width: parent.width - 80
        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.WordWrap
        color: Theme.textFaint
        font.pixelSize: 12
        text: raiz.pub.enLinea ? "" : qsTr("No puedo ver la urna: el servidor no responde.")
    }

    Popup {
        id: oficial
        modal: true
        width: 460
        padding: 14
        anchors.centerIn: Overlay.overlay
        background: Rectangle { color: Theme.bg; border.color: Theme.glassBorder; radius: 12 }
        contentItem: ColumnLayout {
            spacing: 8
            Text {
                text: qsTr("Resultado oficial")
                color: Theme.text
                font.pixelSize: 15
                font.weight: Font.DemiBold
            }
            TextField { id: ofAsunto; Layout.fillWidth: true; placeholderText: qsTr("Asunto (Expte. 123/24 – en general)"); color: Theme.text }
            GridLayout {
                columns: 3
                Text { text: qsTr("Afirmativos"); color: Theme.textDim; font.pixelSize: 11 }
                Text { text: qsTr("Negativos"); color: Theme.textDim; font.pixelSize: 11 }
                Text { text: qsTr("Abstenciones"); color: Theme.textDim; font.pixelSize: 11 }
                SpinBox { id: ofAf; from: 0; to: 1000; editable: true }
                SpinBox { id: ofNe; from: 0; to: 1000; editable: true }
                SpinBox { id: ofAb; from: 0; to: 1000; editable: true }
            }
            ComboBox {
                id: ofMayoria
                Layout.fillWidth: true
                model: [ { t: qsTr("Mayoría de las reglas del cuerpo"), v: "" }, { t: qsTr("Mayoría simple"), v: "simple" },
                         { t: qsTr("Mayoría absoluta"), v: "absoluta" }, { t: qsTr("2/3 de presentes"), v: "dos_tercios_presentes" },
                         { t: qsTr("2/3 de miembros"), v: "dos_tercios_miembros" } ]
                textRole: "t"
            }
            Text { text: qsTr("O la votación nominal (CSV «nombre;voto», un integrante por línea):"); color: Theme.textDim; font.pixelSize: 11 }
            ScrollView {
                Layout.fillWidth: true
                Layout.preferredHeight: 90
                TextArea { id: ofCsv; color: Theme.text; placeholderText: "Ana Pérez;afirmativo\nJuan Gómez;negativo" }
            }
            RowLayout {
                GlassButton {
                    compact: true; text: qsTr("Cargar"); active: true
                    onClicked: {
                        const may = ofMayoria.model[ofMayoria.currentIndex].v
                        const e = ofCsv.text.trim() !== "" ? Studio.quorum.cargarCsvUi(ofAsunto.text, ofCsv.text, may)
                                                           : Studio.quorum.cargarOficialUi(ofAsunto.text, ofAf.value, ofNe.value, ofAb.value, may)
                        if (e !== "") Studio.notify(e, "error"); else { oficial.close(); ofCsv.text = ""; ofAsunto.text = "" }
                    }
                }
                Item { Layout.fillWidth: true }
                GlassButton { compact: true; text: qsTr("Cerrar"); onClicked: oficial.close() }
            }
        }
    }
}
