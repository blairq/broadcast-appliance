// El QR para manejar las escenas desde una tablet o un celular: la Botonera con la
// plantilla «Director» (las escenas con su tally, TAKE, FTB, grabación y aire).
// Al abrirse busca el perfil «Control de escenas» (si no está, lo arma con esa
// plantilla) y pide un QR de un solo uso. Cuando el aparato lo escanea, el pedido
// aparece acá mismo para aprobarlo (también en la página Botonera).
// Lo abre el botón «QR de escenas» de la tarjeta Dirección.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import BA

Dialog {
    id: dlg
    readonly property var bot: Studio.botonera
    readonly property string kPerfil: "Control de escenas"
    property var r: ({})
    property real ahora: Date.now()
    property string error: ""

    function generar() {
        dlg.error = ""
        dlg.r = ({})
        let id = ""
        for (const p of (dlg.bot ? dlg.bot.perfiles : [])) if (p.nombre === dlg.kPerfil) id = p.id
        if (id === "") {
            // Con las escenas de ahora: una escena nueva se suma después en la página Botonera.
            const res = dlg.bot.crearPerfilUi(dlg.kPerfil, "director")
            if (res.startsWith("error:")) { dlg.error = res.slice(6).trim(); return }
            id = res
        } else {
            // Uno hecho antes de que la plantilla tuviera los botones del guion: se le suman.
            dlg.bot.completarGuionUi(id)
        }
        Studio.emparejarTabletUi(id, qsTr("Tablet de escenas"), 30)
    }

    anchors.centerIn: parent
    // Sigue al ancho de la ventana solo abierto (cerrado, su padre arranca en 0: binding loop).
    width: visible && parent ? Math.min(parent.width - 60, 560) : 560
    modal: true
    title: qsTr("QR para manejar las escenas")
    onOpened: generar()

    Connections {
        target: Studio
        function onEmparejamientoListo(res) {
            if (!dlg.visible) return
            dlg.r = res
            if (res.error && !res.url) dlg.error = res.error
        }
    }
    Timer { interval: 1000; repeat: true; running: dlg.visible; onTriggered: dlg.ahora = Date.now() }

    ColumnLayout {
        anchors.fill: parent
        spacing: 12

        Text {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            text: qsTr("Escanealo con la tablet o el celular: abre la Botonera con las escenas (un toque arma, TAKE corta), " +
                  "FTB, grabación y transmisión. El código sirve una sola vez.")
            color: Theme.textDim; font.pixelSize: 12
        }
        Text {
            visible: dlg.error !== ""
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            text: "⚠ " + dlg.error
            color: Theme.live; font.pixelSize: 12
        }
        RowLayout {
            Layout.fillWidth: true
            spacing: 16
            Rectangle {
                width: 220; height: 220; radius: 10; color: "white"
                Image { anchors.fill: parent; anchors.margins: 8; source: dlg.r.qr || ""; fillMode: Image.PreserveAspectFit; smooth: false }
                Text {
                    anchors.centerIn: parent
                    visible: !dlg.r.qr && dlg.error === ""
                    text: qsTr("Generando…"); color: "#666"; font.pixelSize: 13
                }
            }
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 6
                Text {
                    readonly property int resta: Math.max(0, Math.round(((dlg.r.vence_ms || 0) - dlg.ahora) / 1000))
                    visible: !!dlg.r.url
                    text: resta > 0 ? qsTr("Vence en %1 s").arg(resta) : qsTr("El código venció: generá otro")
                    color: resta > 0 ? Theme.text : Theme.live; font.pixelSize: 14
                }
                TextField { visible: !!dlg.r.url; Layout.fillWidth: true; readOnly: true; text: dlg.r.url || ""; color: Theme.textDim; font.pixelSize: 11 }
                Text {
                    visible: !!dlg.r.huella
                    Layout.fillWidth: true; wrapMode: Text.WrapAnywhere
                    text: qsTr("El certificado es propio: la tablet avisa la primera vez. Huella: %1").arg(dlg.r.huella || "")
                    color: Theme.textFaint; font.pixelSize: 10
                }
                Text {
                    visible: !!dlg.r.aviso
                    Layout.fillWidth: true; wrapMode: Text.WordWrap
                    text: dlg.r.aviso || ""
                    color: Theme.amber; font.pixelSize: 11
                }
                GlassButton { compact: true; text: qsTr("Otro QR"); onClicked: dlg.generar() }
            }
        }

        // Los aparatos que lo escanearon y esperan: aprobar acá mismo.
        Text {
            visible: dlg.bot && dlg.bot.solicitudes.length > 0
            text: qsTr("Piden entrar")
            color: Theme.text; font { pixelSize: 13; weight: Font.DemiBold }
        }
        Repeater {
            model: dlg.bot ? dlg.bot.solicitudes : []
            delegate: RowLayout {
                required property var modelData
                Layout.fillWidth: true
                spacing: 8
                Text {
                    Layout.fillWidth: true
                    elide: Text.ElideRight
                    text: qsTr("%1 · %2 · perfil «%3»").arg(modelData.nombre).arg(modelData.ip).arg(modelData.perfil)
                    color: Theme.text; font.pixelSize: 12
                }
                GlassButton { compact: true; text: qsTr("Aprobar"); active: true; onClicked: Studio.aprobarTabletUi(modelData.id, true) }
                GlassButton { compact: true; text: qsTr("Rechazar"); onClicked: Studio.aprobarTabletUi(modelData.id, false) }
            }
        }
    }

    footer: RowLayout {
        Item { Layout.fillWidth: true }
        GlassButton { text: qsTr("Cerrar"); onClicked: dlg.close() }
        Item { Layout.preferredWidth: 12; Layout.preferredHeight: 50 }
    }
}
