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
    /// Lo pone en true PanelWindow (ver PanelPreguntas).
    property bool enVentana: false
    readonly property var ev: Studio.evento
    readonly property var v: pub.votacion
    readonly property bool hay: v && v.hubo === true
    readonly property bool abierta: hay && v.abierta === true

    Component.onCompleted: pub.activo = true
    Component.onDestruction: pub.activo = false

    /// Los del padrón que pueden votar Y ya llegaron. El que tiene derecho y no
    /// vino cuenta para el denominador pero no vota desde su casa.
    function habilitados() {
        const out = []
        const p = ev.padron || []
        for (let i = 0; i < p.length; ++i)
            if (p[i].vota && p[i].estado === "llego")
                out.push({ codigo: p[i].codigo, nombre: p[i].nombre })
        return out
    }
    function conDerecho() {
        const p = ev.padron || []
        let n = 0
        for (let i = 0; i < p.length; ++i) if (p[i].vota) ++n
        return n
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 10

        RowLayout {
            Layout.fillWidth: true
            visible: !raiz.enVentana
            Item { Layout.fillWidth: true }
            GlassButton {
                compact: true
                text: "⧉"
                onClicked: ApplicationWindow.window.sacarPanel("votacion", "Votación")
                ToolTip.visible: hovered
                ToolTip.delay: 400
                ToolTip.text: "Sacar a su propia ventana, para mandarla a otro monitor"
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
                        text: (raiz.v.modo === "formal" ? "VOTACIÓN" : "ENCUESTA") +
                              (raiz.abierta ? " · abierta" : " · cerrada") +
                              (raiz.v.secreta ? " · secreta" : "")
                        color: raiz.abierta ? Theme.live : Theme.textDim
                        font { pixelSize: 11; weight: Font.DemiBold; letterSpacing: 0.6 }
                    }
                    Item { Layout.fillWidth: true }
                    Text {
                        // El QUÓRUM, con sus dos números. Sin el denominador,
                        // dos a favor y cero en contra parece unánime cuando en
                        // realidad no vino nadie.
                        text: raiz.v.podian > 0
                              ? "votaron " + raiz.v.votaron + " de " + raiz.v.podian
                              : "votaron " + raiz.v.votaron
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
                    text: "Faltan votar: " + (raiz.v.faltanVotar || []).join(", ")
                    color: Theme.amber
                    font.pixelSize: 12
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8
                    GlassButton {
                        visible: raiz.hay
                        text: "Proyectar"
                        icon: "⧉"
                        onClicked: proyectarDlg.open()
                        ToolTip.visible: hovered
                        ToolTip.delay: 400
                        ToolTip.text: "Mandarla a un televisor de la sala, con el QR para votar"
                    }
                    GlassButton {
                        visible: raiz.abierta
                        text: "Cerrar la votación"
                        onClicked: raiz.pub.cerrarVotacion()
                        ToolTip.visible: hovered
                        ToolTip.delay: 400
                        ToolTip.text: "Deja el resultado firme y se lo muestra a todos los teléfonos"
                    }
                    Toggle {
                        visible: raiz.abierta
                        checked: raiz.v.enVivo === true
                        onToggled: (w) => raiz.pub.mostrarEnVivo(w)
                    }
                    Text {
                        visible: raiz.abierta
                        text: "Mostrar el resultado mientras está abierta"
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
            text: "Con el resultado a la vista, el que todavía no votó ve que va perdiendo y eso " +
                  "cambia lo que vota. En una votación formal conviene tenerlo apagado."
            color: Theme.amber
            font.pixelSize: 11
        }

        // --- Abrir una nueva --------------------------------------------------
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            radius: Theme.radiusSmall
            color: Qt.rgba(1, 1, 1, 0.03)
            border.color: Theme.glassBorder

            ColumnLayout {
                anchors { fill: parent; margins: 14 }
                spacing: 8

                Text {
                    text: raiz.abierta ? "Abrir otra (reemplaza a la que está abierta)" : "Abrir una"
                    color: Theme.text
                    font { pixelSize: 14; weight: Font.DemiBold }
                }
                Text {
                    Layout.fillWidth: true
                    visible: raiz.abierta
                    wrapMode: Text.WordWrap
                    text: "La que está abierta queda registrada igual: para el acta importa que se " +
                          "votó, no que alguien se haya acordado de cerrarla."
                    color: Theme.textFaint
                    font.pixelSize: 11
                }

                TextField {
                    id: campoPregunta
                    Layout.fillWidth: true
                    color: Theme.text
                    placeholderText: "¿Qué se vota? Ej.: ¿Aprobamos el balance?"
                }
                TextField {
                    id: campoOpciones
                    Layout.fillWidth: true
                    color: Theme.text
                    text: "A favor, En contra, Abstención"
                    placeholderText: "Opciones separadas por coma"
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 10
                    ComboBox {
                        id: modo
                        model: [
                            { texto: "Votación formal (del padrón)", valor: "formal" },
                            { texto: "Encuesta abierta (indicativa)", valor: "encuesta" }
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
                        text: "Secreta"
                        color: modo.currentValue === "formal" ? Theme.text : Theme.textFaint
                        font.pixelSize: 12
                    }
                    Item { Layout.fillWidth: true }
                }

                Text {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    color: modo.currentValue === "formal" && raiz.habilitados().length === 0
                           ? Theme.live : Theme.textDim
                    font.pixelSize: 12
                    text: modo.currentValue === "formal"
                          ? (raiz.habilitados().length === 0
                             ? "Nadie con derecho a voto llegó todavía. Una votación formal sin " +
                               "votantes da un resultado que parece válido."
                             : "Pueden votar " + raiz.habilitados().length + " presentes, de " +
                               raiz.conDerecho() + " con derecho en el padrón.")
                          : "Vota cualquiera que tenga el enlace, una vez por teléfono. El resultado " +
                            "es indicativo: no sirve para decidir."
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8
                    GlassButton {
                        text: "Abrir"
                        enabled: campoPregunta.text.trim() !== "" &&
                                 (modo.currentValue === "encuesta" || raiz.habilitados().length > 0)
                        onClicked: {
                            const ops = campoOpciones.text.split(",").map((s) => s.trim())
                                                                    .filter((s) => s !== "")
                            raiz.pub.abrirVotacion(campoPregunta.text, ops, modo.currentValue,
                                                   secreta.checked, false,
                                                   modo.currentValue === "formal" ? raiz.habilitados() : [],
                                                   modo.currentValue === "formal" ? raiz.conDerecho() : 0)
                            campoPregunta.text = ""
                        }
                    }
                    Item { Layout.fillWidth: true }
                }
            }
        }
    }

    // A que televisor. La lista es la misma de la pagina de Pantallas: el
    // modelo es uno solo (`Studio.pantallas`) y esto solo elige.
    Dialog {
        id: proyectarDlg
        anchors.centerIn: Overlay.overlay
        width: 420
        modal: true
        title: "Proyectar la votación"
        standardButtons: Dialog.Close
        ColumnLayout {
            spacing: 8
            Text {
                Layout.preferredWidth: 380
                wrapMode: Text.WordWrap
                text: "Se manda la que esté abierta, con el QR para votar desde el teléfono. El " +
                      "resultado se ve solo si decidiste mostrarlo."
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
                        if (err) ApplicationWindow.window.toast(err, "error")
                        proyectarDlg.close()
                    }
                }
            }
            Text {
                Layout.preferredWidth: 380
                visible: Studio.pantallas.pantallas.length === 0
                wrapMode: Text.WordWrap
                text: "No hay ninguna pantalla. Buscalas en la página de Pantallas."
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
        text: raiz.pub.enLinea ? "" : "No puedo ver la urna: la sala no responde."
    }
}
