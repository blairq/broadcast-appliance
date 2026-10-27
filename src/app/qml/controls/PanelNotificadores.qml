// Los avisos fuera del programa: webhook, Telegram y mail. El secreto (URL,
// token o contraseña) se carga acá y se guarda cifrado; nunca por el chat.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import BA

ColumnLayout {
    id: pn
    spacing: 6
    readonly property var n: Studio.notificador
    Connections {
        target: Studio.notificador
        function onProbado(id, ok, detalle) { Studio.notify(ok ? qsTr("El aviso de prueba llegó") : qsTr("No llegó: %1").arg(detalle), ok ? "ok" : "error") }
    }
    RowLayout {
        Layout.fillWidth: true
        Text { text: qsTr("Notificaciones"); color: Theme.text; font.pixelSize: 13; font.weight: Font.DemiBold; Layout.fillWidth: true }
        GlassButton { compact: true; text: qsTr("＋ Notificador"); onClicked: alta.open() }
    }
    Text {
        visible: pn.n.lista.length === 0
        Layout.fillWidth: true; wrapMode: Text.WordWrap
        text: qsTr("Para que un disparador avise por Telegram, mail o a un sistema propio (webhook) aunque nadie mire la pantalla.")
        color: Theme.textFaint; font.pixelSize: 11
    }
    Repeater {
        model: pn.n.lista
        delegate: RowLayout {
            required property var modelData
            Layout.fillWidth: true
            Text {
                Layout.fillWidth: true; elide: Text.ElideRight
                text: modelData.tiene_secreto ? modelData.nombre + " · " + modelData.tipo
                                              : qsTr("%1 · %2 · falta el secreto").arg(modelData.nombre).arg(modelData.tipo)
                color: modelData.tiene_secreto ? Theme.text : Theme.amber; font.pixelSize: 12
            }
            GlassButton { compact: true; text: qsTr("Probar"); enabled: modelData.tiene_secreto; onClicked: pn.n.probarUi(modelData.id) }
            GlassButton { compact: true; text: qsTr("Secreto"); onClicked: { alta.editando = modelData; alta.open() } }
            GlassButton { compact: true; text: "✕"; onClicked: pn.n.borrarUi(modelData.id) }
        }
    }
    Popup {
        id: alta
        property var editando: null
        modal: true
        width: 440
        padding: 14
        anchors.centerIn: Overlay.overlay
        background: Rectangle { color: Theme.bg; border.color: Theme.glassBorder; radius: 12 }
        onClosed: editando = null
        onOpened: {
            const e = editando || {}
            tipo.currentIndex = Math.max(0, tipo.model.indexOf(e.tipo || "telegram"))
            nombreN.text = e.nombre || ""
            chat.text = e.chat_id || ""
            servidor.text = e.servidor || ""
            puerto.value = e.puerto || 465
            usuario.text = e.usuario || ""
            de.text = e.de || ""
            para.text = e.para || ""
            secreto.text = ""
        }
        contentItem: ColumnLayout {
            spacing: 6
            Text {
                text: alta.editando ? qsTr("Notificador «%1»").arg(alta.editando.nombre) : qsTr("Nuevo notificador")
                color: Theme.text
                font.pixelSize: 15
                font.weight: Font.DemiBold
            }
            ComboBox { id: tipo; Layout.fillWidth: true; model: ["telegram", "smtp", "webhook"]; enabled: !alta.editando }
            TextField { id: nombreN; Layout.fillWidth: true; placeholderText: qsTr("Nombre (Guardia técnica)"); color: Theme.text }
            TextField { id: chat; visible: tipo.currentText === "telegram"; Layout.fillWidth: true; placeholderText: qsTr("chat_id del grupo o persona"); color: Theme.text }
            RowLayout {
                visible: tipo.currentText === "smtp"
                TextField { id: servidor; Layout.fillWidth: true; placeholderText: "smtp.servidor.gob.ar"; color: Theme.text }
                SpinBox { id: puerto; from: 1; to: 65535; value: 465; editable: true; Layout.preferredWidth: 110 }
            }
            TextField { id: usuario; visible: tipo.currentText === "smtp"; Layout.fillWidth: true; placeholderText: qsTr("Usuario"); color: Theme.text }
            TextField { id: de; visible: tipo.currentText === "smtp"; Layout.fillWidth: true; placeholderText: qsTr("De (avisos@…)"); color: Theme.text }
            TextField { id: para; visible: tipo.currentText === "smtp"; Layout.fillWidth: true; placeholderText: qsTr("Para (guardia@…)"); color: Theme.text }
            TextField {
                id: secreto
                Layout.fillWidth: true
                echoMode: TextInput.Password
                color: Theme.text
                placeholderText: tipo.currentText === "telegram" ? qsTr("Token del bot") : tipo.currentText === "smtp" ? qsTr("Contraseña") : qsTr("URL del webhook")
            }
            Text {
                Layout.fillWidth: true; wrapMode: Text.WordWrap
                text: qsTr("Se guarda cifrado en Credenciales y no se muestra de nuevo. Dejalo vacío para no cambiarlo.")
                color: Theme.textFaint; font.pixelSize: 10
            }
            RowLayout {
                GlassButton {
                    compact: true; text: qsTr("Guardar"); active: true
                    onClicked: {
                        const d = { tipo: tipo.currentText, nombre: nombreN.text, chat_id: chat.text, servidor: servidor.text,
                                    puerto: puerto.value, usuario: usuario.text, de: de.text, para: para.text }
                        if (alta.editando) d.id = alta.editando.id
                        const e = pn.n.agregarUi(d, secreto.text)
                        if (e !== "") Studio.notify(e, "error"); else alta.close()
                    }
                }
                Item { Layout.fillWidth: true }
                GlassButton { compact: true; text: qsTr("Cancelar"); onClicked: alta.close() }
            }
        }
    }
}
