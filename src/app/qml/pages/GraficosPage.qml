// Editor de gráficos animados: la lista, el código, la vista previa y la
// barra de tiempo. Solo presentación: todo sale de Studio.grafico, que es el
// MISMO motor que usa el asistente por MCP.
//
// La vista previa se dibuja con lo que hay en el editor, no con el archivo
// guardado: se ve lo que se está escribiendo.
import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import BA
import "../controls"

Rectangle {
    id: page
    color: Theme.bg
    property real leftInset: 92
    readonly property var g: Studio.grafico

    function mmss(ms) {
        const s = Math.round(ms / 100) / 10
        return s.toFixed(1) + " s"
    }

    // El video en vivo que lleva el gráfico: dónde va en el momento de la barra. Cambia
    // en cada cuadro al reproducir: se actualiza en su lugar (no se reemplaza el modelo).
    ListModel { id: recuadrosModelo }
    function sincronizarRecuadros() {
        const r = page.g.recuadros
        while (recuadrosModelo.count > r.length)
            recuadrosModelo.remove(recuadrosModelo.count - 1)
        for (let i = 0; i < r.length; ++i) {
            const o = { rx: r[i].x, ry: r[i].y, rw: r[i].ancho, rh: r[i].alto, alfa: r[i].alfa,
                        nombre: r[i].fuente, arriba: r[i].arriba }
            if (i < recuadrosModelo.count)
                recuadrosModelo.set(i, o)
            else
                recuadrosModelo.append(o)
        }
    }
    Connections {
        target: page.g
        function onRecuadrosChanged() { page.sincronizarRecuadros() }
    }
    Component.onCompleted: sincronizarRecuadros()

    // Un GIF, una imagen, una carpeta de PNG o un video con alfa para el gráfico abierto.
    FileDialog {
        id: medioDlg
        title: qsTr("Medio para el gráfico (GIF, imagen o video con alfa)")
        nameFilters: [qsTr("Imágenes y videos (*.gif *.png *.jpg *.jpeg *.webp *.webm *.mov *.mp4 *.mkv)"), qsTr("Todos (*)")]
        onAccepted: page.g.agregarMedio(selectedFile)
    }

    ColumnLayout {
        anchors { fill: parent; leftMargin: page.leftInset; margins: 20 }
        spacing: 14

        // --- Sin motor -------------------------------------------------------
        Panel {
            visible: !page.g.disponible
            Layout.fillWidth: true
            Layout.fillHeight: true
            Text {
                anchors.centerIn: parent
                width: parent.width - 80
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
                color: Theme.textDim
                font.pixelSize: 14
                text: qsTr("Este bundle no trae el renderizador de gráficos (ba-grafico).\nSin él no se pueden ver ni usar gráficos animados.")
            }
        }

        RowLayout {
            visible: page.g.disponible
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 14

            // ---- Los gráficos que hay ----------------------------------------
            Panel {
                Layout.preferredWidth: 230
                Layout.fillHeight: true
                title: qsTr("Gráficos")
                ColumnLayout {
                    anchors.fill: parent
                    spacing: 8
                    ListView {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        spacing: 3
                        model: page.g.graficos
                        delegate: Item {
                          id: filaG
                          required property var modelData
                          required property int index
                          // El título de la categoría arriba del primero de cada una.
                          readonly property bool primeroDeSuCategoria: index === 0 ||
                              page.g.graficos[index - 1].categoria !== modelData.categoria
                          width: ListView.view.width
                          height: fichaG.height + (primeroDeSuCategoria ? 22 : 0)
                          Text {
                            visible: filaG.primeroDeSuCategoria
                            y: 6; x: 4
                            text: (filaG.modelData.categoria || qsTr("General")).toUpperCase()
                            color: Theme.textFaint
                            font { pixelSize: 10; weight: Font.DemiBold; letterSpacing: 1.5 }
                          }
                          Rectangle {
                            id: fichaG
                            readonly property var modelData: filaG.modelData
                            y: filaG.primeroDeSuCategoria ? 22 : 0
                            width: parent.width
                            height: 44
                            radius: 5
                            color: modelData.nombre === page.g.abierto ? Qt.rgba(1, 1, 1, 0.10)
                                 : (ma.containsMouse ? Qt.rgba(1, 1, 1, 0.05) : "transparent")
                            ColumnLayout {
                                anchors { fill: parent; leftMargin: 10; rightMargin: 8 }
                                spacing: 0
                                Text {
                                    Layout.fillWidth: true
                                    elide: Text.ElideRight
                                    text: modelData.titulo
                                    color: Theme.text
                                    font { pixelSize: 13; weight: Font.Medium }
                                }
                                // Qué es (antes decía «descripción» a secas: el tipo, que parecía un texto que faltaba).
                                Text {
                                    Layout.fillWidth: true
                                    elide: Text.ElideRight
                                    text: modelData.descripcion
                                          ? modelData.descripcion
                                          : (modelData.plugin ? qsTr("lo dibuja el plugin «%1»").arg(modelData.plugin)
                                             : (modelData.declarativo ? qsTr("armado con capas, sin código") : qsTr("escrito en QML")))
                                    color: Theme.textFaint
                                    font.pixelSize: 10
                                    ToolTip.visible: maDesc.containsMouse && truncated
                                    ToolTip.text: text
                                    MouseArea { id: maDesc; anchors.fill: parent; hoverEnabled: true; acceptedButtons: Qt.NoButton }
                                }
                            }
                            MouseArea {
                                id: ma
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: page.g.abrir(modelData.nombre)
                            }
                          }
                        }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 6
                        GlassButton {
                            text: qsTr("+ Descripción"); compact: true
                            Layout.fillWidth: true
                            onClicked: nuevoDlg.abrir(true)
                            ToolTip.visible: hovered
                            ToolTip.text: qsTr("Un gráfico sin código: capas y animaciones de un repertorio")
                        }
                        GlassButton {
                            text: "+ QML"; compact: true
                            onClicked: nuevoDlg.abrir(false)
                            ToolTip.visible: hovered
                            ToolTip.text: qsTr("Para lo que el repertorio no alcance")
                        }
                    }
                }
            }

            // ---- El editor ----------------------------------------------------
            Panel {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.minimumWidth: 320
                title: page.g.abierto === "" ? qsTr("Editor")
                     : (page.g.sucio ? qsTr("%1 — sin guardar").arg(page.g.abierto) : page.g.abierto)

                ColumnLayout {
                    anchors.fill: parent
                    spacing: 8

                    Text {
                        visible: page.g.abierto === ""
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                        wrapMode: Text.WordWrap
                        color: Theme.textFaint
                        font.pixelSize: 13
                        text: qsTr("Elegí un gráfico de la izquierda, o creá uno.\n\nLos de «descripción» se arman con capas y animaciones de un repertorio, sin escribir código.")
                    }

                    // Uno de un plugin no se edita acá: su código es el del plugin.
                    Text {
                        visible: page.g.abierto !== "" && page.g.plugin !== ""
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                        wrapMode: Text.WordWrap
                        color: Theme.textFaint
                        font.pixelSize: 13
                        text: qsTr("Este gráfico lo dibuja el plugin «%1».\n\nSe cambia en su código (sección Plugins, o pidiéndoselo al asistente). Acá se prueba con sus parámetros y se saca al aire.").arg(page.g.plugin)
                    }

                    ScrollView {
                        visible: page.g.abierto !== "" && page.g.plugin === ""
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        TextArea {
                            id: editor
                            text: page.g.fuente
                            color: Theme.text
                            font { family: Theme.mono; pixelSize: 12 }
                            selectByMouse: true
                            wrapMode: TextArea.NoWrap
                            background: Rectangle { color: Qt.rgba(0, 0, 0, 0.30); radius: 4 }
                            // Se manda al motor al soltar la tecla, no en cada
                            // pulsación: redibujar por letra no lo aguanta nadie.
                            onTextChanged: reboteEdicion.restart()
                            Timer {
                                id: reboteEdicion
                                interval: 350
                                onTriggered: page.g.fuente = editor.text
                            }
                            Connections {
                                target: page.g
                                function onAbiertoChanged() { editor.text = page.g.fuente }
                            }
                        }
                    }

                    RowLayout {
                        visible: page.g.abierto !== ""
                        Layout.fillWidth: true
                        spacing: 6
                        GlassButton {
                            visible: page.g.plugin === ""
                            text: qsTr("Guardar"); compact: true
                            active: page.g.sucio
                            onClicked: page.g.guardar()
                        }
                        Item { Layout.fillWidth: true }
                        Text {
                            visible: page.g.error !== ""
                            Layout.fillWidth: true
                            Layout.maximumWidth: 420
                            wrapMode: Text.WordWrap
                            text: page.g.error
                            color: Theme.live
                            font.pixelSize: 11
                        }
                    }
                }
            }

            // ---- La vista previa y los parámetros ------------------------------
            ColumnLayout {
                Layout.preferredWidth: 430
                Layout.maximumWidth: 430
                Layout.fillHeight: true
                spacing: 14

                Panel {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 320
                    title: qsTr("Cómo se ve")
                    ColumnLayout {
                        anchors.fill: parent
                        spacing: 8

                        // Damero: sin esto no se ve qué parte es transparente,
                        // que es justo lo que hay que mirar en un gráfico.
                        Rectangle {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            radius: 4
                            color: "#20242a"
                            Canvas {
                                anchors.fill: parent
                                onPaint: {
                                    const c = getContext("2d")
                                    const p = 12
                                    for (let y = 0; y < height; y += p)
                                        for (let x = 0; x < width; x += p) {
                                            c.fillStyle = ((x / p + y / p) % 2 === 0) ? "#20242a" : "#272c33"
                                            c.fillRect(x, y, p, p)
                                        }
                                }
                            }
                            // El cuadro tal cual lo tiene el modelo, en el mismo
                            // nodo: sin cargas ni cuadros en blanco entre dibujos
                            // (con una Image y un data: URI parpadeaba al dar play).
                            ImagenViva {
                                anchors.fill: parent
                                anchors.margins: 6
                                imagen: page.g.imagen
                            }
                            // Donde va a ir el video en vivo (una cámara, un invitado): un
                            // recuadro sobre el cuadro, que entra y se va con la barra de tiempo.
                            Item {
                                id: lienzoVista
                                anchors.fill: parent
                                anchors.margins: 6
                                readonly property real asp: page.g.aspecto > 0 ? page.g.aspecto : 1
                                readonly property real pw: Math.min(width, height * asp)
                                readonly property real ph: pw / asp
                                readonly property real px0: (width - pw) / 2
                                readonly property real py0: (height - ph) / 2
                                Repeater {
                                    model: recuadrosModelo
                                    delegate: Rectangle {
                                        required property real rx
                                        required property real ry
                                        required property real rw
                                        required property real rh
                                        required property real alfa
                                        required property string nombre
                                        x: lienzoVista.px0 + rx * lienzoVista.pw
                                        y: lienzoVista.py0 + ry * lienzoVista.ph
                                        width: rw * lienzoVista.pw
                                        height: rh * lienzoVista.ph
                                        color: Qt.rgba(0.16, 0.45, 0.85, 0.30 * alfa)
                                        border.color: Qt.rgba(0.45, 0.70, 1.0, 0.25 + 0.75 * alfa)
                                        border.width: 2
                                        Text {
                                            anchors.centerIn: parent
                                            width: parent.width - 8
                                            horizontalAlignment: Text.AlignHCenter
                                            elide: Text.ElideRight
                                            text: "● " + (nombre !== "" ? nombre : qsTr("video en vivo"))
                                            color: "white"
                                            opacity: 0.4 + 0.6 * alfa
                                            font.pixelSize: 11
                                        }
                                    }
                                }
                            }
                        }

                        // La barra de tiempo. Arrastrar hacia atrás funciona
                        // porque el motor dibuja cada instante desde cero.
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 8
                            Text {
                                text: page.mmss(page.g.momentoMs)
                                color: Theme.text
                                font { family: Theme.mono; pixelSize: 12 }
                                Layout.preferredWidth: 46
                            }
                            Slider {
                                id: barra
                                Layout.fillWidth: true
                                from: 0
                                to: page.g.duracionMs
                                value: page.g.momentoMs
                                onMoved: page.g.momentoMs = value
                            }
                            Text {
                                text: qsTr("de %1").arg(page.mmss(page.g.duracionMs))
                                color: Theme.textFaint
                                font.pixelSize: 11
                            }
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 6
                            GlassButton {
                                icon: "⏮"; compact: true
                                onClicked: page.g.momentoMs = 0
                            }
                            GlassButton {
                                text: page.g.reproduciendo ? qsTr("❚❚ Pausa") : qsTr("▶ Reproducir"); compact: true
                                Layout.fillWidth: true
                                onClicked: page.g.reproduciendo = !page.g.reproduciendo
                                active: page.g.reproduciendo
                            }
                            Text { text: qsTr("Dura"); color: Theme.textDim; font.pixelSize: 11 }
                            TextField {
                                Layout.preferredWidth: 70
                                text: String(page.g.duracionMs)
                                color: Theme.text
                                font.pixelSize: 12
                                validator: IntValidator { bottom: 100; top: 15000 }
                                onEditingFinished: page.g.duracionMs = parseInt(text) || 1500
                            }
                        }
                    }
                }

                Panel {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    title: qsTr("Parámetros")
                    ScrollView {
                        anchors.fill: parent
                        clip: true
                        ColumnLayout {
                            width: parent.parent.width
                            spacing: 8
                            Repeater {
                                model: page.g.parametros
                                delegate: ColumnLayout {
                                    required property var modelData
                                    Layout.fillWidth: true
                                    spacing: 1
                                    Text {
                                        text: modelData.titulo
                                        color: Theme.textDim
                                        font.pixelSize: 11
                                    }
                                    // Una fuente de video (la cámara que va adentro del gráfico):
                                    // las entradas de video de la app, o el nombre de cualquier fuente.
                                    ComboBox {
                                        visible: modelData.tipo === "fuente"
                                        Layout.fillWidth: true
                                        editable: true
                                        model: Studio.inputs.videoInputs.map(f => f.name)
                                        readonly property var valor: page.g.valores[modelData.clave]
                                        editText: valor === undefined ? "" : String(valor)
                                        onActivated: page.g.setValor(modelData.clave, currentText)
                                        onAccepted: page.g.setValor(modelData.clave, editText)
                                    }
                                    TextField {
                                        visible: modelData.tipo !== "fuente"
                                        Layout.fillWidth: true
                                        // Los datos de una torta o unas barras se escriben como texto:
                                        // «Sí: 34; No: 12 #c62828» (o una lista JSON, que se muestra igual).
                                        readonly property var valor: page.g.valores[modelData.clave]
                                        text: valor === undefined ? ""
                                              : (typeof valor === "object" ? JSON.stringify(valor) : String(valor))
                                        placeholderText: modelData.tipo === "datos" ? qsTr("Sí: 34; No: 12; Abstenciones: 3") : ""
                                        color: Theme.text
                                        font.pixelSize: 12
                                        onEditingFinished: page.g.setValor(modelData.clave, text)
                                    }
                                    Text {
                                        visible: modelData.ayuda !== ""
                                        Layout.fillWidth: true
                                        wrapMode: Text.WordWrap
                                        text: modelData.ayuda
                                        color: Theme.textFaint
                                        font.pixelSize: 10
                                    }
                                }
                            }

                            Rectangle {
                                Layout.fillWidth: true
                                Layout.topMargin: 6
                                height: 1
                                color: Theme.glassBorder
                            }
                            // Los medios del gráfico: GIF, imágenes y videos con alfa,
                            // con su tiempo atado al del gráfico (capa «medio»).
                            Text {
                                visible: page.g.plugin === ""
                                text: qsTr("Medios (GIF, imágenes, videos con alfa)")
                                color: Theme.textDim
                                font { pixelSize: 11; weight: Font.DemiBold }
                            }
                            Text {
                                visible: page.g.plugin === "" && page.g.medios.length === 0
                                Layout.fillWidth: true
                                wrapMode: Text.WordWrap
                                text: qsTr("Ninguno. Un video (WebM con alfa, ProRes 4444) se pasa a cuadros con su transparencia; en el gráfico va como capa «medio» con «archivo»: «medios/…».")
                                color: Theme.textFaint
                                font.pixelSize: 11
                            }
                            Repeater {
                                model: page.g.plugin === "" ? page.g.medios : []
                                delegate: RowLayout {
                                    required property var modelData
                                    Layout.fillWidth: true
                                    spacing: 6
                                    Text {
                                        Layout.fillWidth: true
                                        elide: Text.ElideMiddle
                                        text: modelData.archivo + "  ·  " + modelData.tipo +
                                              (modelData.cuadros ? " · " + qsTr("%n cuadro", "", modelData.cuadros) : "") +
                                              (modelData.alfa === false ? " · " + qsTr("sin alfa") : "")
                                        color: Theme.text
                                        font.pixelSize: 12
                                        ToolTip.visible: maMedio.containsMouse
                                        ToolTip.text: qsTr("En el gráfico: \"archivo\": \"%1\"").arg(modelData.archivo)
                                        MouseArea { id: maMedio; anchors.fill: parent; hoverEnabled: true; acceptedButtons: Qt.NoButton }
                                    }
                                    GlassButton {
                                        icon: "✕"; compact: true
                                        onClicked: page.g.quitarMedio(modelData.archivo)
                                    }
                                }
                            }
                            GlassButton {
                                visible: page.g.plugin === ""
                                text: page.g.importando ? qsTr("Convirtiendo el video…") : qsTr("Agregar medio…")
                                compact: true
                                enabled: !page.g.importando && page.g.abierto !== ""
                                onClicked: medioDlg.open()
                            }

                            Rectangle {
                                visible: page.g.plugin === ""
                                Layout.fillWidth: true
                                Layout.topMargin: 6
                                height: 1
                                color: Theme.glassBorder
                            }
                            // Los botones que este gráfico le va a ofrecer al
                            // operador en la pantalla de Rótulos. Se diseñan
                            // ACÁ, mientras se diseña la animación, que es
                            // cuando se sabe para qué sirve cada parámetro.
                            Text {
                                text: qsTr("Botones que ofrece")
                                color: Theme.textDim
                                font { pixelSize: 11; weight: Font.DemiBold }
                            }
                            Text {
                                visible: page.g.acciones.length === 0
                                Layout.fillWidth: true
                                wrapMode: Text.WordWrap
                                text: qsTr("Todavía no ofrece ninguno. Un botón es un pedido ya escrito («poné al orador que sigue») que el operador aprieta sin tener que explicarle nada al asistente.")
                                color: Theme.textFaint
                                font.pixelSize: 11
                            }
                            Repeater {
                                model: page.g.acciones
                                delegate: ColumnLayout {
                                    required property var modelData
                                    Layout.fillWidth: true
                                    spacing: 0
                                    Text {
                                        text: modelData.icono + "  " + modelData.titulo
                                        color: Theme.text
                                        font.pixelSize: 12
                                    }
                                    Text {
                                        Layout.fillWidth: true
                                        wrapMode: Text.WordWrap
                                        text: modelData.prompt
                                        color: Theme.textFaint
                                        font.pixelSize: 10
                                    }
                                }
                            }
                            GlassButton {
                                text: page.g.acciones.length === 0 ? qsTr("Proponer botones") : qsTr("Revisar los botones")
                                compact: true
                                enabled: Studio.agent.running
                                onClicked: ApplicationWindow.window.pedirAlAsistente(
                                    qsTr("Proponé los botones («acciones») de este gráfico y escribilos en su " +
                                    "grafico.json: cada uno con «titulo» corto, «icono» de un carácter y " +
                                    "«prompt» redactado como se lo pediría el operador en vivo. Mirá qué " +
                                    "parámetros tiene para saber qué le van a querer cambiar, y no pongas " +
                                    "más de cuatro."))
                            }

                            Rectangle {
                                Layout.fillWidth: true
                                Layout.topMargin: 6
                                height: 1
                                color: Theme.glassBorder
                            }
                            Text {
                                text: qsTr("Ponerlo en una escena")
                                color: Theme.textDim
                                font { pixelSize: 11; weight: Font.DemiBold }
                            }
                            Text {
                                visible: page.g.enEscena.length === 0
                                Layout.fillWidth: true
                                wrapMode: Text.WordWrap
                                text: qsTr("No hay ninguna fuente «Gráfico animado» en las escenas. Agregá una desde Escenas y volvé.")
                                color: Theme.textFaint
                                font.pixelSize: 11
                            }
                            Repeater {
                                model: page.g.enEscena
                                delegate: RowLayout {
                                    required property var modelData
                                    Layout.fillWidth: true
                                    spacing: 6
                                    Text {
                                        Layout.fillWidth: true
                                        elide: Text.ElideRight
                                        text: modelData.fuente + "  " + modelData.ancho + "×" + modelData.alto
                                        color: modelData.reproduciendo ? Theme.ok : Theme.text
                                        font.pixelSize: 12
                                    }
                                    GlassButton {
                                        text: qsTr("Al aire"); compact: true
                                        onClicked: {
                                            const e = page.g.mostrarEnEscena(modelData.id, false)
                                            if (e !== "") page.g.notify(e, "error")
                                        }
                                    }
                                    GlassButton {
                                        icon: "✕"; compact: true
                                        onClicked: page.g.ocultarDeEscena(modelData.id)
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
