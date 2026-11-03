// Las cuentas de una plataforma (YouTube, Twitch o Facebook): puede haber varias (otro
// canal, otra Página). Cada una con su estado (conectada, vencida, sin permiso…), su nombre
// (se edita en el lugar), conectar o reautorizar, desconectar y quitar. La «principal» es la
// que usa el resto de la página (videos, chat, subidas); las demás se usan como destinos de
// las salidas (página Salidas). Todo sale de Studio.cuentas.vivos (el modelo avisa por fila);
// esta vista solo muestra y pide.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import BA

Rectangle {
    id: raiz
    /// "youtube" | "twitch" | "facebook"
    property string plataforma: "youtube"
    /// El color de la plataforma (el botón de conectar).
    property color colorMarca: Theme.accent
    readonly property var lista: Studio.cuentas.vivos.cuentasPor[raiz.plataforma]

    implicitHeight: cuerpo.implicitHeight + 24
    radius: Theme.radiusSmall
    color: Qt.rgba(1, 1, 1, 0.03)
    border.color: Theme.glassBorder

    ColumnLayout {
        id: cuerpo
        anchors { left: parent.left; right: parent.right; top: parent.top; margins: 12 }
        spacing: 6

        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            Text { text: qsTr("Cuentas"); color: Theme.text; font { pixelSize: 13; weight: Font.DemiBold } }
            Text {
                Layout.fillWidth: true; elide: Text.ElideRight
                text: qsTr("La principal es la de esta página; todas se pueden elegir como destino de cualquier salida.")
                color: Theme.textFaint; font.pixelSize: 11
            }
            TextField {
                id: nombreNueva
                Layout.preferredWidth: 200
                placeholderText: qsTr("Nombre de otra cuenta")
                onAccepted: agregarBtn.agregar()
            }
            GlassButton {
                id: agregarBtn
                compact: true
                text: qsTr("+ Cuenta")
                function agregar() {
                    if (Studio.cuentas.vivos.agregarCuenta(raiz.plataforma, nombreNueva.text)) nombreNueva.text = ""
                }
                onClicked: agregar()
                ToolTip.visible: hovered; ToolTip.delay: 400
                ToolTip.text: qsTr("Suma otra cuenta de esta plataforma (usa la misma app); después conectala")
            }
        }

        Repeater {
            model: raiz.lista
            delegate: RowLayout {
                id: fila
                required property string cuenta
                required property string nombre
                required property string canal
                required property string estado
                required property string estadoTexto
                required property bool principal
                required property bool conectada
                required property bool conectando
                Layout.fillWidth: true
                spacing: 8

                Rectangle {
                    width: 9; height: 9; radius: 5
                    color: fila.estado === "conectada" ? Theme.ok
                         : fila.estado === "vencida" || fila.estado === "sin_permiso" ? Theme.amber
                         : fila.estado === "conectando" ? Theme.accent : Theme.textFaint
                }
                TextField {
                    Layout.preferredWidth: 180
                    text: fila.nombre
                    font.pixelSize: 12
                    onEditingFinished: if (text.trim() !== "" && text !== fila.nombre) Studio.cuentas.vivos.renombrarCuenta(raiz.plataforma, fila.cuenta, text)
                }
                Text { visible: fila.principal; text: qsTr("principal"); color: Theme.textFaint; font.pixelSize: 11 }
                Text {
                    Layout.fillWidth: true; elide: Text.ElideRight
                    text: fila.canal !== "" ? qsTr("%1 · %2").arg(fila.canal).arg(fila.estadoTexto) : fila.estadoTexto
                    color: fila.estado === "vencida" || fila.estado === "sin_permiso" ? Theme.amber : Theme.textDim
                    font.pixelSize: 12
                }
                GlassButton {
                    visible: !fila.conectada && !fila.conectando && fila.estado !== "sin_app"
                    compact: true; active: true; activeColor: raiz.colorMarca
                    text: fila.estado === "vencida" ? qsTr("Reautorizar") : qsTr("Conectar")
                    onClicked: Studio.cuentas.vivos.conectarCuenta(raiz.plataforma, fila.cuenta)
                }
                GlassButton {
                    visible: fila.estado === "sin_permiso"
                    compact: true; active: true; activeColor: Theme.amber
                    text: qsTr("Reautorizar")
                    onClicked: Studio.cuentas.vivos.conectarCuenta(raiz.plataforma, fila.cuenta)
                }
                GlassButton {
                    visible: fila.conectando
                    compact: true; text: qsTr("Cancelar")
                    onClicked: Studio.cuentas.vivos.cancelarConexion(raiz.plataforma, fila.cuenta)
                }
                GlassButton {
                    visible: fila.conectada
                    compact: true; text: qsTr("Desconectar")
                    onClicked: {
                        const p = raiz.plataforma, c = fila.cuenta
                        ApplicationWindow.window.confirm({ title: qsTr("¿Desconectar «%1»?").arg(fila.nombre),
                                                           text: qsTr("Se revoca el acceso en la plataforma y se borra el token guardado. Los destinos que la usan siguen con su clave."),
                                                           accept: qsTr("Desconectar"), reject: qsTr("Cancelar"),
                                                           action: function() { Studio.cuentas.vivos.desconectarCuenta(p, c) } })
                    }
                }
                GlassButton {
                    visible: !fila.principal
                    compact: true; text: "✕"
                    onClicked: {
                        const p = raiz.plataforma, c = fila.cuenta
                        ApplicationWindow.window.confirm({ title: qsTr("¿Quitar «%1»?").arg(fila.nombre),
                                                           text: qsTr("Se revoca su acceso, se borra su token y sale de la lista."),
                                                           accept: qsTr("Quitar"), reject: qsTr("Cancelar"),
                                                           action: function() { Studio.cuentas.vivos.quitarCuenta(p, c) } })
                    }
                    ToolTip.visible: hovered; ToolTip.delay: 400
                    ToolTip.text: qsTr("Quitar esta cuenta")
                }
            }
        }
    }
}
