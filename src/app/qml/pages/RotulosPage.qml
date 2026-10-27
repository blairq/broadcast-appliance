// Rótulos en vivo: los textos de los gráficos de la escena que está al aire.
//
// No es el editor (eso es Gráficos): acá no se diseña nada, se escribe el
// nombre del que está hablando y se lo tira al aire. Por eso está ordenado por
// FUENTE de la escena y no por gráfico, y por eso cada tarjeta tiene el botón
// grande al lado del texto.
//
// Clic derecho en el riel la saca a su propia ventana: en una cabina, esto va
// en la pantalla chica al lado del teclado.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import BA
import "../controls"

Rectangle {
    id: page
    color: Theme.bg
    property real leftInset: 92
    readonly property var r: Studio.rotulador

    ColumnLayout {
        anchors { fill: parent; leftMargin: page.leftInset; margins: 20 }
        spacing: 14

        RowLayout {
            Layout.fillWidth: true
            spacing: 10
            Text {
                text: qsTr("Rótulos")
                color: Theme.text
                font { pixelSize: 20; weight: Font.DemiBold }
            }
            Rectangle {
                radius: 4
                color: Qt.rgba(1, 1, 1, 0.07)
                implicitWidth: esc.implicitWidth + 16
                implicitHeight: 22
                Text {
                    id: esc
                    anchors.centerIn: parent
                    text: page.r.escena === "" ? qsTr("sin escena al aire") : qsTr("al aire: %1").arg(page.r.escena)
                    color: Theme.textDim
                    font.pixelSize: 11
                }
            }
            Item { Layout.fillWidth: true }
            GlassButton {
                icon: "▣"; text: qsTr("Desde el celular"); compact: true
                onClicked: qrDlg.mostrar("local", true)
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Un QR para rotular desde un teléfono, con su clave")
            }
            GlassButton {
                icon: "↻"; compact: true
                onClicked: page.r.refrescar()
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Volver a mirar qué hay en la escena")
            }
        }

        // --- Sin fuentes de gráfico en esta escena ---------------------------
        Panel {
            visible: page.r.fuentes.length === 0 && page.r.vinculos.length === 0
            Layout.fillWidth: true
            Layout.fillHeight: true
            Text {
                anchors.centerIn: parent
                width: parent.width - 80
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
                color: Theme.textDim
                font.pixelSize: 14
                text: qsTr("Ninguna escena tiene una fuente de «Gráfico animado».\n\n" +
                      "Agregale una en Escenas y fuentes: ubicala donde querés que se vea el zócalo, " +
                      "y después volvé acá a escribir los textos.")
            }
        }

        // --- Una tarjeta por fuente -----------------------------------------
        ScrollView {
            visible: page.r.fuentes.length > 0 || page.r.vinculos.length > 0
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            contentWidth: availableWidth

            ColumnLayout {
                width: parent.width
                spacing: 12

                Repeater {
                    model: page.r.fuentes
                    delegate: Panel {
                        required property var modelData
                        id: tarjeta
                        Layout.fillWidth: true
                        padding: 14
                        implicitHeight: cuerpo.implicitHeight + 2 * padding

                        ColumnLayout {
                            id: cuerpo
                            anchors.fill: parent // el margen lo pone Panel (`padding`)
                            spacing: 10

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 10
                                Rectangle {
                                    Layout.preferredWidth: 8
                                    Layout.preferredHeight: 8
                                    radius: 4
                                    color: tarjeta.modelData.enAire ? Theme.live : Qt.rgba(1, 1, 1, 0.2)
                                }
                                Text {
                                    text: tarjeta.modelData.fuente
                                    color: Theme.text
                                    font { pixelSize: 14; weight: Font.DemiBold }
                                }
                                Text {
                                    visible: !tarjeta.modelData.escenaAlAire
                                    text: qsTr("en «%1» (no está al aire)").arg(tarjeta.modelData.escena)
                                    color: Theme.textFaint
                                    font.pixelSize: 11
                                }
                                Item { Layout.fillWidth: true }
                                ComboBox {
                                    Layout.preferredWidth: 200
                                    model: page.r.graficos
                                    currentIndex: page.r.graficos.indexOf(tarjeta.modelData.grafico)
                                    font.pixelSize: 12
                                    onActivated: page.r.setGrafico(tarjeta.modelData.id,
                                                                   page.r.graficos[currentIndex])
                                }
                            }

                            // Los campos del gráfico. Se escriben acá y NO salen
                            // al aire hasta el botón: en vivo, que un texto
                            // cambie letra por letra sería un desastre.
                            Repeater {
                                model: tarjeta.modelData.campos
                                delegate: ColumnLayout {
                                    required property var modelData
                                    Layout.fillWidth: true
                                    spacing: 2
                                    Text {
                                        text: modelData.titulo || modelData.clave
                                        color: Theme.textDim
                                        font.pixelSize: 11
                                    }
                                    // La fuente de video que va adentro del gráfico (la cámara del
                                    // zócalo): las entradas de video, o el nombre de cualquier fuente.
                                    ComboBox {
                                        visible: modelData.tipo === "fuente"
                                        Layout.fillWidth: true
                                        editable: true
                                        font.pixelSize: 14
                                        model: Studio.inputs.videoInputs.map(f => f.name)
                                        editText: {
                                            const v = tarjeta.modelData.valores[modelData.clave]
                                            return v === undefined ? "" : String(v)
                                        }
                                        onActivated: page.r.setValor(tarjeta.modelData.id, modelData.clave, currentText)
                                        onAccepted: page.r.setValor(tarjeta.modelData.id, modelData.clave, editText)
                                    }
                                    TextField {
                                        visible: modelData.tipo !== "fuente"
                                        Layout.fillWidth: true
                                        color: Theme.text
                                        font.pixelSize: 14
                                        text: {
                                            const v = tarjeta.modelData.valores[modelData.clave]
                                            return v === undefined ? "" : String(v)
                                        }
                                        onTextEdited: page.r.setValor(tarjeta.modelData.id,
                                                                      modelData.clave, text)
                                        onAccepted: page.r.disparar(tarjeta.modelData.id, false)
                                        ToolTip.visible: hovered && modelData.ayuda !== ""
                                        ToolTip.text: modelData.ayuda
                                        ToolTip.delay: 600
                                    }
                                }
                            }

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 8
                                GlassButton {
                                    visible: tarjeta.modelData.escenaAlAire
                                    text: qsTr("Al aire"); icon: "▶"
                                    onClicked: page.r.disparar(tarjeta.modelData.id, false)
                                }
                                // De una escena que no está al aire: se dibuja ahí y la
                                // escena queda armada en el preview; sale con TAKE.
                                GlassButton {
                                    visible: !tarjeta.modelData.escenaAlAire
                                    text: qsTr("Armar en PVW"); icon: "▷"; active: true
                                    onClicked: {
                                        if (page.r.disparar(tarjeta.modelData.id, false) === "")
                                            Studio.armarEscena(tarjeta.modelData.escena)
                                    }
                                    ToolTip.visible: hovered
                                    ToolTip.text: qsTr("Lo dibuja en «%1» y la deja armada: sale con TAKE").arg(tarjeta.modelData.escena)
                                }
                                GlassButton {
                                    visible: tarjeta.modelData.escenaAlAire
                                    text: qsTr("En bucle"); compact: true
                                    onClicked: page.r.disparar(tarjeta.modelData.id, true)
                                    ToolTip.visible: hovered
                                    ToolTip.text: qsTr("Para un ticker: se repite hasta que lo saques")
                                }
                                GlassButton {
                                    text: qsTr("Sacar"); icon: "✕"; compact: true
                                    enabled: tarjeta.modelData.enAire
                                    onClicked: page.r.ocultar(tarjeta.modelData.id)
                                }
                                Item { Layout.fillWidth: true }
                            }

                            // Los botones de prompt prearmado que trae el
                            // gráfico en su ficha. Son lo que el que diseñó el
                            // gráfico ya sabe que le van a querer pedir.
                            Flow {
                                Layout.fillWidth: true
                                visible: tarjeta.modelData.acciones.length > 0 && Studio.agent.running
                                spacing: 6
                                Repeater {
                                    model: tarjeta.modelData.acciones
                                    delegate: GlassButton {
                                        required property var modelData
                                        compact: true
                                        icon: modelData.icono
                                        text: modelData.titulo
                                        onClicked: {
                                            // El contexto de la barra ya dice la escena; acá se
                                            // agrega de qué fuente se habla, que es lo que el
                                            // asistente necesita para no adivinar.
                                            ApplicationWindow.window.pedirAlAsistente(
                                                "Hablo de la fuente de gráfico «" + tarjeta.modelData.fuente +
                                                "», que muestra «" + tarjeta.modelData.grafico + "» (id " +
                                                tarjeta.modelData.id + "). " + modelData.prompt)
                                        }
                                        ToolTip.visible: hovered
                                        ToolTip.text: modelData.prompt
                                        ToolTip.delay: 500
                                    }
                                }
                            }
                        }
                    }
                }
                // --- Los que se escriben solos ---------------------------
                // Se muestran aunque su fuente no esté en esta escena: un
                // rótulo que cambia solo y no se sabe por qué es peor que uno
                // que no cambia.
                Panel {
                    visible: page.r.vinculos.length > 0
                    Layout.fillWidth: true
                    padding: 14
                    implicitHeight: autos.implicitHeight + 2 * padding
                    ColumnLayout {
                        id: autos
                        anchors.fill: parent // el margen lo pone Panel (`padding`)
                        spacing: 8
                        Text {
                            text: qsTr("Se escriben solos")
                            color: Theme.text
                            font { pixelSize: 13; weight: Font.DemiBold }
                        }
                        Repeater {
                            model: page.r.vinculos
                            delegate: ColumnLayout {
                                required property var modelData
                                id: vin
                                Layout.fillWidth: true
                                spacing: 1
                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: 8
                                    Rectangle {
                                        Layout.preferredWidth: 8
                                        Layout.preferredHeight: 8
                                        radius: 4
                                        color: !vin.modelData.activo ? Qt.rgba(1, 1, 1, 0.2)
                                             : vin.modelData.problema !== "" ? Theme.live
                                             : vin.modelData.fuenteExiste ? Theme.ok : Theme.textDim
                                    }
                                    Text {
                                        Layout.fillWidth: true
                                        elide: Text.ElideRight
                                        color: Theme.text
                                        font.pixelSize: 12
                                        text: vin.modelData.fuente + " · " + vin.modelData.grafico
                                    }
                                    GlassButton {
                                        text: qsTr("Ahora"); compact: true
                                        onClicked: page.r.dispararVinculo(vin.modelData.id)
                                    }
                                    GlassButton {
                                        text: vin.modelData.activo ? qsTr("Apagar") : qsTr("Prender"); compact: true
                                        onClicked: page.r.activarVinculo(vin.modelData.id,
                                                                         !vin.modelData.activo)
                                    }
                                    GlassButton {
                                        icon: "✕"; compact: true
                                        onClicked: page.r.quitarVinculo(vin.modelData.id)
                                    }
                                }
                                Text {
                                    Layout.fillWidth: true
                                    wrapMode: Text.WordWrap
                                    font.pixelSize: 10
                                    color: vin.modelData.problema !== "" ? Theme.live : Theme.textFaint
                                    text: {
                                        if (vin.modelData.problema !== "") return vin.modelData.problema
                                        if (!vin.modelData.fuenteExiste)
                                            return qsTr("esperando a que «%1» aparezca en alguna escena").arg(vin.modelData.fuente)
                                        const r = vin.modelData.resuelve
                                        let ps = []
                                        for (const k in r) ps.push(k + ": " + r[k])
                                        return ps.join("   ")
                                    }
                                }
                            }
                        }
                        Text {
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                            color: Theme.textFaint
                            font.pixelSize: 10
                            text: qsTr("Se revisan una vez por segundo y salen al aire solos cuando lo que " +
                                  "dicen cambia. Para armar uno, pedíselo al asistente acá abajo.")
                        }
                    }
                }

                Item { Layout.fillWidth: true; implicitHeight: 4 }
            }
        }
    }

    // --- El QR para el celular -----------------------------------------------
    // La clave puede ir DENTRO del QR (se entra sin tipear nada) o no ir: si el
    // QR va a quedar pegado en una pared, la clave se dice aparte.
    Dialog {
        id: qrDlg
        anchors.centerIn: parent
        modal: true
        standardButtons: Dialog.Close
        title: qsTr("Rotular desde el celular")
        property string target: "local"
        property bool conClave: true
        property string url: ""
        property string clave: ""
        property var modulos: []
        property string mensaje: ""
        property bool cargando: false

        function mostrar(t, k) {
            target = t; conClave = k
            url = ""; modulos = []; mensaje = ""; cargando = true
            open()
            page.r.pedirQr(t, k)
        }

        Connections {
            target: page.r
            function onQrListo(url, modulos, mensaje, clave) {
                qrDlg.cargando = false
                qrDlg.url = url
                qrDlg.modulos = modulos
                qrDlg.mensaje = mensaje
                qrDlg.clave = clave
                qrCanvas.requestPaint()
            }
        }

        ColumnLayout {
            spacing: 12
            Rectangle {
                Layout.alignment: Qt.AlignHCenter
                Layout.preferredWidth: 300
                Layout.preferredHeight: 300
                radius: 12
                color: qrDlg.url !== "" ? "white" : Qt.rgba(1, 1, 1, 0.05)
                Canvas {
                    id: qrCanvas
                    anchors.fill: parent
                    anchors.margins: 12
                    visible: qrDlg.url !== ""
                    onPaint: {
                        const ctx = getContext("2d")
                        ctx.reset()
                        const filas = qrDlg.modulos
                        if (!filas || filas.length === 0) return
                        const n = filas.length + 8 // zona silenciosa de 4 módulos
                        const cell = Math.floor(Math.min(width, height) / n)
                        const off = Math.floor((Math.min(width, height) - cell * n) / 2) + 4 * cell
                        ctx.fillStyle = "#000000"
                        for (let y = 0; y < filas.length; ++y)
                            for (let x = 0; x < filas[y].length; ++x)
                                if (filas[y][x] === "1") ctx.fillRect(off + x * cell, off + y * cell, cell, cell)
                    }
                }
                Text {
                    anchors.fill: parent
                    anchors.margins: 20
                    visible: qrDlg.url === ""
                    wrapMode: Text.Wrap
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                    color: Theme.textDim; font.pixelSize: 13
                    text: qrDlg.cargando ? qsTr("Generando…") : qrDlg.mensaje
                }
            }

            RowLayout {
                Layout.alignment: Qt.AlignHCenter
                spacing: 6
                GlassButton {
                    compact: true; text: qsTr("Red local")
                    active: qrDlg.target === "local"
                    onClicked: qrDlg.mostrar("local", qrDlg.conClave)
                }
                GlassButton {
                    compact: true; text: qsTr("Por internet")
                    active: qrDlg.target === "remote"
                    onClicked: qrDlg.mostrar("remote", qrDlg.conClave)
                }
            }

            CheckBox {
                checked: qrDlg.conClave
                text: qsTr("Incluir la clave en el QR (entra sin tipearla)")
                onToggled: qrDlg.mostrar(qrDlg.target, checked)
            }

            Text {
                Layout.preferredWidth: 300
                wrapMode: Text.WordWrap
                color: Theme.textDim
                font.pixelSize: 11
                text: qrDlg.conClave
                    ? qsTr("La clave viaja en el fragmento del enlace: no llega al servidor ni queda en " +
                      "ningún registro. Aun así, el que tenga este QR puede rotular: no lo dejes pegado " +
                      "en una pared.")
                    : qsTr("Sin la clave adentro, el QR abre la página y pide la clave. Decísela vos: %1").arg(qrDlg.clave)
            }

            Text {
                Layout.preferredWidth: 300
                wrapMode: Text.WrapAnywhere
                visible: qrDlg.url !== ""
                color: Theme.textFaint
                font.pixelSize: 10
                text: qrDlg.url
            }
        }
    }
}
