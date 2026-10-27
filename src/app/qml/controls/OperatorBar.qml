// Barra de mensajes del asistente al operador (abajo de todo). Color segun la
// prioridad; si es una pregunta, botones de opciones y/o texto libre; si es el
// mensaje de un invitado, respuestas sugeridas que se mandan con un click (o se
// editan antes). Formulario de credencial: los datos van directo de la app al
// almacen cifrado (el asistente no los ve). Varios mensajes: se recorren con
// las flechas.
import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import BA

Item {
    id: bar
    readonly property var msgs: Studio.messages
    property int index: 0
    readonly property int n: msgs.count
    readonly property var cur: n > 0 ? msgs.items[Math.min(index, n - 1)] : null
    readonly property color accent: cur ? colorFor(cur.priority) : Theme.accent
    visible: n > 0
    implicitHeight: visible ? card.implicitHeight : 0
    // Llega algo nuevo: se muestra lo mas importante (va primero).
    onNChanged: index = 0
    onCurChanged: if (cur && cur.aire) card.forceActiveFocus()

    function colorFor(p) {
        if (p === "critical") return "#ef4444"
        if (p === "warning") return "#f59e0b"
        if (p === "success") return "#22c55e"
        return "#3b82f6"
    }
    function label(p) {
        return p === "critical" ? qsTr("Urgente") : p === "warning" ? qsTr("Atención") : p === "success" ? qsTr("Listo") : qsTr("Asistente")
    }

    Rectangle {
        id: card
        width: parent.width
        implicitHeight: col.implicitHeight + 26 + (bar.cur && bar.cur.aire ? 16 : 0)
        radius: 18
        color: Qt.rgba(bar.accent.r * 0.16 + 0.05, bar.accent.g * 0.16 + 0.05, bar.accent.b * 0.16 + 0.06, 1)
        border.width: 2
        border.color: bar.accent
        SequentialAnimation on border.color {
            running: bar.cur !== null && bar.cur.priority === "critical" && bar.visible
            loops: Animation.Infinite
            ColorAnimation { to: "#ffffff"; duration: 500 }
            ColorAnimation { to: "#ef4444"; duration: 500 }
        }

        // Una acción de AIRE: banda roja arriba, Enter aprueba y Esc rechaza.
        focus: bar.cur !== null && bar.cur.aire === true
        Keys.onReturnPressed: if (bar.cur && bar.cur.aire) bar.msgs.answer(bar.cur.id, "Aprobar")
        Keys.onEnterPressed: if (bar.cur && bar.cur.aire) bar.msgs.answer(bar.cur.id, "Aprobar")
        Keys.onEscapePressed: if (bar.cur && bar.cur.aire) bar.msgs.answer(bar.cur.id, "Rechazar")
        Rectangle {
            visible: bar.cur !== null && bar.cur.aire === true
            anchors { left: parent.left; right: parent.right; top: parent.top }
            height: 22; radius: 18
            color: "#dc2626"
            Text {
                anchors.centerIn: parent
                text: qsTr("ACCIÓN DE AIRE · Enter confirma · Esc rechaza")
                color: "white"; font { pixelSize: 11; weight: Font.Bold; letterSpacing: 1 }
            }
        }

        ColumnLayout {
            id: col
            anchors { left: parent.left; right: parent.right; top: parent.top; margins: 14; topMargin: bar.cur && bar.cur.aire ? 30 : 14 }
            spacing: 8

            RowLayout {
                Layout.fillWidth: true
                spacing: 10
                Rectangle {
                    width: 28; height: 28; radius: 14; color: bar.accent
                    Text { anchors.centerIn: parent; text: "✦"; color: "white"; font.pixelSize: 14 }
                }
                Text {
                    text: bar.cur ? (bar.cur.title !== "" ? bar.cur.title : bar.label(bar.cur.priority)) : ""
                    color: bar.accent; font { pixelSize: 13; weight: Font.Bold; capitalization: Font.AllUppercase; letterSpacing: 0.8 }
                }
                Text {
                    visible: bar.cur !== null && (bar.cur.kind === "question" || bar.cur.kind === "credential")
                    text: bar.cur && bar.cur.kind === "credential" ? qsTr("· datos para conectar") : qsTr("· te pregunta")
                    color: Theme.textDim; font.pixelSize: 12
                }
                Item { Layout.fillWidth: true }
                RowLayout {
                    visible: bar.n > 1
                    spacing: 4
                    GlassButton { compact: true; text: "‹"; enabled: bar.index > 0; onClicked: bar.index-- }
                    Text { text: (bar.index + 1) + " / " + bar.n; color: Theme.textDim; font.pixelSize: 12 }
                    GlassButton { compact: true; text: "›"; enabled: bar.index < bar.n - 1; onClicked: bar.index++ }
                }
                GlassButton { compact: true; text: "✕"; onClicked: bar.msgs.dismiss(bar.cur.id)
                              ToolTip.visible: hovered; ToolTip.text: bar.cur && bar.cur.kind === "reply" ? qsTr("Ignorar") : qsTr("Cerrar") }
            }

            // Texto (o el mensaje del invitado)
            Text {
                Layout.fillWidth: true
                visible: bar.cur !== null && bar.cur.kind !== "reply"
                text: bar.cur ? bar.cur.text : ""
                textFormat: Text.PlainText
                color: Theme.text; font { pixelSize: 15; weight: bar.cur && bar.cur.approval ? Font.DemiBold : Font.Normal }
                wrapMode: Text.WordWrap
            }
            // Aprobacion (salvaguarda): la accion EXACTA que pidio el asistente y por que se pregunta.
            Text {
                Layout.fillWidth: true
                visible: bar.cur !== null && bar.cur.approval === true && bar.cur.reason !== ""
                text: bar.cur ? "🛡 " + bar.cur.reason : ""
                textFormat: Text.PlainText
                color: Theme.textDim; font.pixelSize: 12; wrapMode: Text.WordWrap
            }
            Rectangle {
                Layout.fillWidth: true
                visible: bar.cur !== null && bar.cur.approval === true && bar.cur.details !== ""
                implicitHeight: Math.min(detailsText.implicitHeight + 14, 180)
                radius: 10
                color: Qt.rgba(0, 0, 0, 0.35)
                clip: true
                Text {
                    id: detailsText
                    anchors { left: parent.left; right: parent.right; top: parent.top; margins: 7 }
                    text: bar.cur ? bar.cur.details : ""
                    textFormat: Text.PlainText
                    color: "#e5e7eb"; font { family: "JetBrains Mono"; pixelSize: 12 } wrapMode: Text.WrapAnywhere
                }
            }
            Rectangle {
                Layout.fillWidth: true
                visible: bar.cur !== null && bar.cur.kind === "reply"
                implicitHeight: guestCol.implicitHeight + 16
                radius: 12
                color: Qt.rgba(1, 1, 1, 0.06)
                ColumnLayout {
                    id: guestCol
                    anchors { left: parent.left; right: parent.right; top: parent.top; margins: 8 }
                    RowLayout {
                        spacing: 4
                        // Texto plano: el nombre lo eligio el invitado.
                        Text { text: bar.cur ? bar.cur.participantName : ""; textFormat: Text.PlainText
                               color: Theme.text; font { pixelSize: 12; bold: true } }
                        Text { text: bar.cur && bar.cur.to === "all" ? qsTr("escribió en el chat general:") : qsTr("te escribió en privado:")
                               color: Theme.textDim; font.pixelSize: 12 }
                    }
                    Text {
                        Layout.fillWidth: true
                        text: bar.cur ? qsTr("«%1»").arg(bar.cur.guestText) : ""
                        textFormat: Text.PlainText
                        color: Theme.text; font { pixelSize: 15; italic: true } wrapMode: Text.WordWrap
                    }
                }
            }

            // Opciones de una pregunta
            Flow {
                Layout.fillWidth: true
                spacing: 8
                visible: bar.cur !== null && bar.cur.kind === "question" && bar.cur.options.length > 0
                Repeater {
                    model: bar.cur && bar.cur.kind === "question" ? bar.cur.options : []
                    delegate: GlassButton {
                        required property string modelData
                        required property int index
                        text: modelData
                        // En una aprobacion, solo "Aprobar" resalta; "Rechazar" queda neutro.
                        // `cur` puede quedar en null justo cuando el mensaje se
                        // cierra y el delegado todavia existe.
                        active: !bar.cur || !bar.cur.approval || index === 0
                        activeColor: bar.cur && bar.cur.approval ? "#16a34a" : bar.accent
                        onClicked: if (bar.cur) bar.msgs.answer(bar.cur.id, modelData)
                    }
                }
            }

            // Imagen adjunta (p. ej. la foto de quien espera en el lobby).
            Image {
                Layout.alignment: Qt.AlignHCenter
                Layout.preferredWidth: Math.min(260, implicitWidth)
                Layout.preferredHeight: Layout.preferredWidth * (implicitHeight > 0 ? implicitHeight / implicitWidth : 0.5625)
                visible: !!bar.cur && bar.cur.image !== ""
                source: bar.cur && bar.cur.image !== "" ? bar.cur.image : ""
                fillMode: Image.PreserveAspectFit
                asynchronous: true
            }

            // Respuestas sugeridas para el invitado
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 6
                visible: bar.cur !== null && bar.cur.kind === "reply"
                Text { text: qsTr("Respuestas sugeridas (un click y se manda):"); color: Theme.textDim; font.pixelSize: 12 }
                Repeater {
                    model: bar.cur && bar.cur.kind === "reply" ? bar.cur.suggestions : []
                    delegate: RowLayout {
                        required property string modelData
                        required property int index
                        Layout.fillWidth: true
                        Rectangle {
                            Layout.fillWidth: true
                            implicitHeight: sugText.implicitHeight + 14
                            radius: 10
                            color: sugMa.containsMouse ? Qt.rgba(bar.accent.r, bar.accent.g, bar.accent.b, 0.35) : Qt.rgba(1, 1, 1, 0.07)
                            border.color: Qt.rgba(bar.accent.r, bar.accent.g, bar.accent.b, 0.7)
                            Text {
                                id: sugText
                                anchors { left: parent.left; right: parent.right; verticalCenter: parent.verticalCenter; margins: 10 }
                                text: (index + 1) + ".  " + modelData
                                textFormat: Text.PlainText
                                color: Theme.text; font.pixelSize: 14; wrapMode: Text.WordWrap
                            }
                            MouseArea { id: sugMa; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                                        onClicked: replyErr.text = bar.msgs.sendReply(bar.cur.id, modelData) }
                        }
                        GlassButton { compact: true; text: qsTr("Editar"); onClicked: { field.text = modelData; field.forceActiveFocus() } }
                    }
                }
            }

            // Texto libre (pregunta abierta o respuesta editada)
            RowLayout {
                Layout.fillWidth: true
                visible: bar.cur !== null && ((bar.cur.kind === "question" && bar.cur.allowText) || bar.cur.kind === "reply")
                TextField {
                    id: field
                    Layout.fillWidth: true
                    placeholderText: bar.cur && bar.cur.kind === "reply" ? qsTr("o escribí tu respuesta…") : qsTr("Tu respuesta…")
                    onAccepted: sendBtn.clicked()
                }
                GlassButton {
                    id: sendBtn
                    text: bar.cur && bar.cur.kind === "reply" ? qsTr("Enviar al invitado") : qsTr("Responder")
                    active: true
                    activeColor: bar.accent
                    enabled: field.text.trim() !== ""
                    onClicked: {
                        if (bar.cur.kind === "reply") replyErr.text = bar.msgs.sendReply(bar.cur.id, field.text)
                        else bar.msgs.answer(bar.cur.id, field.text)
                        if (replyErr.text === "") field.text = ""
                    }
                }
            }
            // Formulario de credencial (YouTube / Twitch / Facebook)
            ColumnLayout {
                id: credForm
                Layout.fillWidth: true
                spacing: 6
                visible: bar.cur !== null && bar.cur.kind === "credential"
                property string error: ""
                function submit(values) {
                    credForm.error = bar.msgs.submitCredential(bar.cur.id, values)
                    if (credForm.error === "")
                        for (let i = 0; i < credFields.count; ++i) credFields.itemAt(i).clear()
                }
                Repeater {
                    id: credFields
                    model: bar.cur && bar.cur.kind === "credential" ? bar.cur.fields : []
                    delegate: RowLayout {
                        required property var modelData
                        id: credRow
                        property bool shown: false
                        function clear() { credInput.text = ""; shown = false }
                        readonly property string key: modelData.key
                        readonly property string value: credInput.text.trim()
                        Layout.fillWidth: true
                        Text { text: modelData.label; color: Theme.textDim; font.pixelSize: 12; Layout.preferredWidth: 190; wrapMode: Text.WordWrap }
                        TextField {
                            id: credInput
                            Layout.fillWidth: true
                            placeholderText: modelData.placeholder || ""
                            echoMode: modelData.secret && !credRow.shown ? TextInput.Password : TextInput.Normal
                            inputMethodHints: Qt.ImhSensitiveData | Qt.ImhNoPredictiveText
                        }
                        GlassButton { visible: modelData.secret; compact: true; text: credRow.shown ? "🙈" : "👁"
                                      onClicked: credRow.shown = !credRow.shown
                                      ToolTip.visible: hovered; ToolTip.text: qsTr("Mostrar / ocultar") }
                    }
                }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8
                    GlassButton {
                        text: qsTr("Guardar")
                        active: true
                        activeColor: bar.accent
                        onClicked: {
                            const v = {}
                            for (let i = 0; i < credFields.count; ++i) v[credFields.itemAt(i).key] = credFields.itemAt(i).value
                            credForm.submit(v)
                        }
                    }
                    GlassButton {
                        visible: bar.cur !== null && bar.cur.importFile === true
                        text: qsTr("Importar el JSON de Descargas")
                        onClicked: credForm.submit({ "fromDownloads": true })
                        ToolTip.visible: hovered; ToolTip.text: qsTr("Busca el client_secret_….json más nuevo que bajaste de Google")
                    }
                    GlassButton {
                        visible: bar.cur !== null && bar.cur.importFile === true
                        text: qsTr("Elegir archivo…")
                        onClicked: credFile.open()
                    }
                    Item { Layout.fillWidth: true }
                    Text { text: qsTr("🔒 Se guarda cifrado en este equipo; el asistente no lo ve."); color: Theme.textFaint; font.pixelSize: 11 }
                }
                Text { visible: credForm.error !== ""; text: credForm.error; color: Theme.live; font.pixelSize: 12
                       Layout.fillWidth: true; wrapMode: Text.WordWrap }
                FileDialog {
                    id: credFile
                    title: qsTr("JSON del cliente OAuth de Google")
                    nameFilters: [qsTr("Cliente OAuth (client_secret*.json *.json)")]
                    onAccepted: credForm.submit({ "importFile": selectedFile.toString() })
                }
            }

            Text { id: replyErr; visible: text !== ""; color: Theme.live; font.pixelSize: 12 }
        }
    }
}
