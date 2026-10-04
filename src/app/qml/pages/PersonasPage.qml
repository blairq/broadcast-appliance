// Página Personas: quién es quién, para que el sistema lo reconozca.
//
// POR QUE EXISTE. El reconocimiento no adivina nombres: aprende cuando alguien
// DICE quién es. Acá se dice, se corrige lo que quedó mal, se le suman caras a
// quien cuesta reconocer y se borra a quien no tiene que estar. Sin esta
// página el sistema junta desconocidos y no sirve para nada.
//
// A LA IZQUIERDA LA CAMARA, A LA DERECHA LA LIBRETA, y esa disposición es el
// punto: dar de alta a alguien es mirar una cara y escribir un nombre. Con la
// lista de un lado y la cámara del otro, las dos cosas están a la vista.
//
// LA FOTO ES PARA MIRAR. El reconocimiento usa los vectores; la foto está para
// que el operador confirme de un vistazo que la ficha es de quien cree. Un
// nombre sin cara no se verifica.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import BA
import "../controls"

Rectangle {
    id: page
    color: Theme.bg
    property real leftInset: 92
    readonly property var pv: Studio.personas

    property string editando: ""     // id de la ficha abierta ("" = una nueva)
    property string fuente: "program"
    // Lo último que se vio. NO se pide y se usa en la misma línea: el
    // reconocimiento corre en otro hilo y contesta cuando termina.
    readonly property var vistos: page.pv.vistos

    function abrir(p) {
        page.editando = p ? p.id : ""
        campoNombre.text = p ? p.nombre : ""
        campoCargo.text = p ? p.cargo : ""
        campoNota.text = p ? p.nota : ""
    }
    function mirar() {
        page.pv.mirar(page.fuente)
    }
    Timer {
        // Cada segundo alcanza: esto es para confirmar a quién se está viendo,
        // no para seguirle la cara a nadie. Mirar más seguido gasta CPU que le
        // hace falta al video.
        interval: 1000; running: page.visible && page.pv.disponible; repeat: true
        onTriggered: page.mirar()
    }
    Component.onCompleted: page.pv.refrescar()
    Connections { target: page.pv; function onNotify(t, k) { Studio.notify(t, k) } }

    ColumnLayout {
        anchors { fill: parent; leftMargin: page.leftInset; margins: 18 }
        spacing: 12

        RowLayout {
            Layout.fillWidth: true
            spacing: 10
            Text { text: "Personas"; color: Theme.text; font { pixelSize: 20; weight: Font.DemiBold } }
            StatusPill {
                text: page.pv.personas.length + " en la libreta"
                dot: page.pv.disponible ? Theme.ok : Theme.textFaint
            }
            StatusPill {
                // Las fichas que anotó solo el sistema y esperan que alguien
                // diga quién es. Si no se ven, nadie las completa nunca.
                visible: page.pv.sinNombre > 0
                text: page.pv.sinNombre + " sin nombre"
                dot: Theme.warn
            }
            Item { Layout.fillWidth: true }
            Text {
                text: "Se reconoce a quien ya se dio de alta. La foto es para que vos confirmes; " +
                      "el sistema compara con los vectores."
                color: Theme.textFaint; font.pixelSize: 11
                elide: Text.ElideRight; Layout.maximumWidth: 560
            }
        }

        // Sin los modelos no se puede hacer nada acá, y hay que decirlo: una
        // lista vacía se lee como «no hay nadie guardado».
        Panel {
            Layout.fillWidth: true
            visible: !page.pv.disponible
            padding: 14
            implicitHeight: sinModelos.implicitHeight + 2 * padding
            ColumnLayout {
                id: sinModelos
                anchors.fill: parent
                spacing: 6
                Text { text: "El reconocimiento de rostros no está disponible"
                       color: Theme.warn; font { pixelSize: 14; weight: Font.DemiBold } }
                Text { Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.textDim
                       font.pixelSize: 12; text: page.pv.aviso }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: page.pv.disponible
            spacing: 14

            // --- La cámara: a quién estoy viendo ---------------------------
            Panel {
                Layout.preferredWidth: 420
                Layout.fillHeight: true
                padding: 12
                ColumnLayout {
                    anchors.fill: parent
                    spacing: 8
                    RowLayout {
                        Layout.fillWidth: true
                        Text { text: "Mirando"; color: Theme.textDim; font.pixelSize: 12 }
                        ComboBox {
                            Layout.fillWidth: true
                            model: page.pv.fuentes()
                            textRole: "etiqueta"
                            valueRole: "clave"
                            currentIndex: { const f = page.pv.fuentes()
                                            for (let i = 0; i < f.length; ++i)
                                                if (f[i].clave === page.fuente) return i
                                            return 0 }
                            onActivated: (i) => { page.fuente = page.pv.fuentes()[i].clave
                                                  page.pv.feedAuto = page.fuente
                                                  page.mirar() }
                        }
                    }
                    // --- El anotado automático ------------------------------
                    // Lo que hace y lo que NO hace, dicho acá y no en un manual
                    // que nadie abre: anota caras, no nombres.
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8
                        Toggle {
                            // Toggle y no Switch: el Switch del estilo Basic se
                            // ve apagado en tema oscuro (ver Toggle.qml).
                            id: swAuto
                            checked: page.pv.autoalta
                            onToggled: (wanted) => { page.pv.feedAuto = page.fuente
                                                     page.pv.autoalta = wanted }
                        }
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 0
                            Text { text: "Anotar sola a cada persona nueva"
                                   color: Theme.text; font.pixelSize: 12 }
                            Text {
                                Layout.fillWidth: true
                                wrapMode: Text.WordWrap
                                color: Theme.textFaint; font.pixelSize: 10
                                text: swAuto.checked
                                      ? "Mirando «" + page.pv.etiquetaDe(page.pv.feedAuto) +
                                        "». Cada cara que no conozca queda anotada sin nombre, con su " +
                                        "foto, para que la completes. No rotula a nadie."
                                      : "Abre una ficha sin nombre por cada cara desconocida que pase " +
                                        "por esta cámara, y le suma poses a las que ya conoce."
                            }
                        }
                    }
                    // --- La acreditación de la puerta -----------------------
                    // La otra mitad de la gente. El lobby ya recibe al que
                    // entra por la sala; esto recibe al que entra caminando, y
                    // los dos caen en el mismo padrón.
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8
                        Toggle {
                            id: swPuerta
                            checked: page.pv.puertaAuto
                            onToggled: (wanted) => { page.pv.feedPuerta = page.fuente
                                                     page.pv.puertaAuto = wanted }
                        }
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 0
                            Text { text: "Acreditar a quien llega por esta cámara"
                                   color: Theme.text; font.pixelSize: 12 }
                            Text {
                                Layout.fillWidth: true
                                wrapMode: Text.WordWrap
                                color: Theme.textFaint; font.pixelSize: 10
                                text: swPuerta.checked
                                      ? "Mirando «" + page.pv.etiquetaDe(page.pv.feedPuerta) +
                                        "». Al que reconoce lo marca como llegado en el padrón del " +
                                        "evento. Al que no está en la lista lo avisa y no lo anota."
                                      : "Para la entrada del lugar: reconoce al que llega y deja " +
                                        "constancia en el padrón, igual que el lobby con los de la sala."
                            }
                        }
                    }
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: width * 9 / 16
                        color: "black"; radius: 10; clip: true
                        VideoItem { anchors.fill: parent; source: page.fuente; fillMode: VideoItem.Fit }
                        // Los recuadros de las caras, con su nombre o el aviso
                        // de que no se sabe quién es.
                        Repeater {
                            model: page.vistos
                            delegate: Item {
                                required property var modelData
                                Rectangle {
                                    color: "transparent"
                                    border.width: 2
                                    // Tres estados, no dos: «sé quién es»,
                                    // «la tengo anotada pero nadie le puso
                                    // nombre» y «no la vi nunca». El del medio
                                    // es el que pide trabajo del operador.
                                    border.color: modelData.nombre !== "" ? Theme.ok
                                                : modelData.id !== "" ? Theme.accent : Theme.warn
                                    radius: 4
                                    // Las cajas vienen en píxeles del cuadro
                                    // que miró el detector. El video entra
                                    // AJUSTADO (Fit): se escala por el lado que
                                    // primero toca el borde y queda centrado,
                                    // con bandas negras en el otro. Hay que
                                    // aplicar esa misma escala Y ese mismo
                                    // corrimiento, o el recuadro cae al lado de
                                    // la cara. El 1920 de antes daba por hecho
                                    // un ancho que casi ninguna fuente tiene.
                                    readonly property Item marco: parent.parent
                                    readonly property real vw: modelData.cw > 0 ? modelData.cw : 1920
                                    readonly property real vh: modelData.ch > 0 ? modelData.ch : 1080
                                    readonly property real k: Math.min(marco.width / vw, marco.height / vh)
                                    readonly property real ox: (marco.width - vw * k) / 2
                                    readonly property real oy: (marco.height - vh * k) / 2
                                    x: ox + modelData.x * k; y: oy + modelData.y * k
                                    width: modelData.ancho * k; height: modelData.alto * k
                                    Rectangle {
                                        anchors { bottom: parent.top; left: parent.left; bottomMargin: 2 }
                                        width: etq.implicitWidth + 10; height: 18; radius: 5
                                        color: modelData.nombre !== "" ? Theme.ok
                                             : modelData.id !== "" ? Theme.accent : Theme.warn
                                        Text {
                                            id: etq; anchors.centerIn: parent
                                            text: modelData.nombre !== ""
                                                  ? modelData.nombre + "  " + Math.round(modelData.parecido * 100) + "%"
                                                  : modelData.id !== "" ? "anotada, sin nombre"
                                                  : "no sé quién es"
                                            color: "white"; font.pixelSize: 10
                                        }
                                    }
                                }
                            }
                        }
                        Text {
                            anchors.centerIn: parent
                            visible: page.vistos.length === 0
                            text: "No se ve ninguna cara"
                            color: Theme.textFaint; font.pixelSize: 13
                        }
                    }

                    // --- La ficha que se está editando -------------------------
                    Text { text: page.editando === "" ? "Persona nueva" : "Editando"
                           color: Theme.textDim; font { pixelSize: 12; weight: Font.DemiBold } }
                    TextField { id: campoNombre; Layout.fillWidth: true; placeholderText: "Nombre y apellido" }
                    TextField { id: campoCargo; Layout.fillWidth: true
                                placeholderText: "Cargo (lo que va en el rótulo)" }
                    TextField { id: campoNota; Layout.fillWidth: true
                                placeholderText: "Nota para el operador (no sale al aire)" }
                    RowLayout {
                        Layout.fillWidth: true
                        GlassButton {
                            text: page.editando === "" ? "Crear y tomarle la cara" : "Guardar"
                            active: true
                            enabled: campoNombre.text.trim() !== ""
                            onClicked: {
                                const id = page.pv.guardar(page.editando, campoNombre.text,
                                                           campoCargo.text, campoNota.text)
                                if (id === "") return
                                // Al crear se le toma la cara de una: si no,
                                // queda una ficha que no reconoce a nadie y
                                // nadie se entera hasta que falla.
                                // Lo que salga lo avisa `notify` cuando el
                                // otro hilo conteste, no acá.
                                if (page.editando === "") page.pv.sumarCara(id, page.fuente, false)
                                page.abrir(null)
                            }
                        }
                        GlassButton { text: "Limpiar"; visible: page.editando !== ""
                                      onClicked: page.abrir(null) }
                        Item { Layout.fillWidth: true }
                    }
                }
            }

            // --- La libreta ------------------------------------------------
            Panel {
                Layout.fillWidth: true
                Layout.fillHeight: true
                padding: 12
                ColumnLayout {
                    anchors.fill: parent
                    spacing: 8
                    Text {
                        Layout.fillWidth: true
                        visible: page.pv.personas.length === 0
                        wrapMode: Text.WordWrap; color: Theme.textFaint; font.pixelSize: 12
                        text: "Todavía no hay nadie. Poné a alguien frente a la cámara, escribí su " +
                              "nombre a la izquierda y dale «Crear y tomarle la cara»."
                    }
                    ScrollView {
                        id: sc
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        contentWidth: availableWidth
                        clip: true
                        ColumnLayout {
                            width: sc.availableWidth
                            spacing: 6
                            Repeater {
                                model: page.pv.personas
                                delegate: Rectangle {
                                    id: fila
                                    required property var modelData
                                    Layout.fillWidth: true
                                    implicitHeight: 72
                                    radius: 12
                                    color: page.editando === modelData.id ? Qt.rgba(1, 1, 1, 0.09)
                                                                          : Qt.rgba(1, 1, 1, 0.04)
                                    border.color: page.editando === modelData.id ? Theme.accentSoft
                                                                                 : Qt.rgba(1, 1, 1, 0.08)
                                    RowLayout {
                                        anchors { fill: parent; margins: 10 }
                                        spacing: 12
                                        Rectangle {
                                            Layout.preferredWidth: 52; Layout.preferredHeight: 52
                                            radius: 26; color: Qt.rgba(1, 1, 1, 0.06); clip: true
                                            Image {
                                                anchors.fill: parent
                                                source: fila.modelData.foto
                                                fillMode: Image.PreserveAspectCrop
                                                visible: fila.modelData.foto !== ""
                                            }
                                            Text {
                                                anchors.centerIn: parent
                                                visible: fila.modelData.foto === ""
                                                text: "?"; color: Theme.textFaint; font.pixelSize: 20
                                            }
                                        }
                                        ColumnLayout {
                                            Layout.fillWidth: true
                                            spacing: 1
                                            // Una ficha sin nombre es la que
                                            // anotó solo el sistema. Se dice
                                            // así, y no con el nombre vacío:
                                            // una fila en blanco parece un
                                            // error, no una tarea.
                                            Text { text: fila.modelData.nombre !== ""
                                                         ? fila.modelData.nombre : "Sin nombre"
                                                   color: fila.modelData.nombre !== "" ? Theme.text
                                                                                       : Theme.accent
                                                   font { pixelSize: 14; weight: Font.DemiBold
                                                          italic: fila.modelData.nombre === "" }
                                                   elide: Text.ElideRight; Layout.fillWidth: true }
                                            Text { text: fila.modelData.cargo; color: Theme.textDim
                                                   font.pixelSize: 12; elide: Text.ElideRight
                                                   Layout.fillWidth: true; visible: text !== "" }
                                            Text {
                                                color: fila.modelData.caras === 0 ? Theme.warn : Theme.textFaint
                                                font.pixelSize: 10
                                                // Cero caras es el caso que hay
                                                // que ver: la ficha existe pero
                                                // no reconoce a nadie.
                                                text: fila.modelData.caras === 0
                                                      ? "sin ninguna cara: no se la va a reconocer"
                                                      : fila.modelData.caras + " cara(s)" +
                                                        (fila.modelData.vistaSeg >= 0
                                                         ? " · vista hace " + (fila.modelData.vistaSeg < 90
                                                            ? fila.modelData.vistaSeg + " s"
                                                            : Math.round(fila.modelData.vistaSeg / 60) + " min")
                                                         : " · nunca vista")
                                            }
                                        }
                                        GlassButton {
                                            compact: true; text: "Sumar cara"; icon: "＋"
                                            onClicked: page.pv.sumarCara(fila.modelData.id, page.fuente, false)
                                            ToolTip.visible: hovered
                                            ToolTip.text: "Le suma la cara que se ve ahora.\nDe frente, de costado, " +
                                                          "con anteojos y sin: así se la reconoce en el día real."
                                        }
                                        GlassButton {
                                            compact: true; text: "Editar"
                                            onClicked: page.abrir(fila.modelData)
                                        }
                                        GlassButton {
                                            compact: true; text: "✕"
                                            onClicked: {
                                                // Se copia TODO a variables locales antes de armar
                                                // el diálogo. La acción se ejecuta después, cuando
                                                // el operador confirma, y para entonces esta fila
                                                // puede no existir más: la lista se rearma cada vez
                                                // que la libreta cambia. Mirando `page` o
                                                // `fila.modelData` desde adentro daba
                                                // «page is not defined» y no borraba nada.
                                                const hoja = page
                                                const id = fila.modelData.id
                                                const nombre = fila.modelData.nombre
                                                const caras = fila.modelData.caras
                                                ApplicationWindow.window.confirm({
                                                    title: "¿Borrar a «" + (nombre !== "" ? nombre : "esta persona") + "»?",
                                                    text: "Se va la ficha, su foto y sus " + caras +
                                                          " cara(s). Para volver a reconocerla hay que darla de " +
                                                          "alta otra vez y mostrarle la cara de nuevo.",
                                                    accept: "Borrar",
                                                    action: () => {
                                                        if (hoja.editando === id) hoja.abrir(null)
                                                        hoja.pv.borrar(id)
                                                    }
                                                })
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }

}
