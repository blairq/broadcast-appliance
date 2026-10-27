// El QR de un enlace o texto que el asistente le quiere pasar al operador
// (herramienta MCP qr_mostrar): se escanea con el celular o se copia con un
// botón. Con una descripción grande y un COLOR (marco y banda): con varios QR a
// la vez se le puede decir «escaneá el QR verde». El código en sí queda negro
// sobre blanco, que es lo que siempre se lee.
//
// Una ventana por QR (Main.qml crea una por pedido); se puede mover y cerrar.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import BA

Dialog {
    id: dlg
    modal: false
    closePolicy: Popup.CloseOnEscape
    width: Math.min(parent ? parent.width - 32 : 440, 440)
    padding: 0
    topPadding: 0
    header: null

    property string texto: ""
    property string descripcion: ""
    property string mensaje: ""
    property string colorNombre: "verde"
    property bool copiado: false
    signal cerrada()

    /// Los colores que se pueden nombrar (para decir «el QR verde»).
    readonly property var colores: ({
        "verde": "#16a34a", "azul": "#2563eb", "rojo": "#dc2626", "amarillo": "#eab308",
        "naranja": "#ea580c", "violeta": "#9333ea", "rosa": "#db2777", "celeste": "#0891b2",
        "gris": "#4b5563"
    })
    /// Cómo se llama cada color en la pantalla (la clave de arriba es la del MCP y no se traduce).
    readonly property var etiquetasColor: ({
        "verde": qsTr("verde"), "azul": qsTr("azul"), "rojo": qsTr("rojo"), "amarillo": qsTr("amarillo"),
        "naranja": qsTr("naranja"), "violeta": qsTr("violeta"), "rosa": qsTr("rosa"), "celeste": qsTr("celeste"),
        "gris": qsTr("gris")
    })
    readonly property color colorQr: dlg.colores[dlg.colorNombre] !== undefined ? dlg.colores[dlg.colorNombre]
                                     : (/^#[0-9a-fA-F]{6}$/.test(dlg.colorNombre) ? dlg.colorNombre : "#16a34a")
    readonly property string nombreColor: dlg.colores[dlg.colorNombre] !== undefined ? dlg.colorNombre : ""

    onClosed: dlg.cerrada()

    background: Rectangle {
        radius: 16
        color: Theme.panel
        border.color: dlg.colorQr
        border.width: 6
    }

    contentItem: ColumnLayout {
        spacing: 10
        // La banda de color: la descripción, grande (se puede corregir).
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: banda.implicitHeight + 20
            radius: 12
            color: dlg.colorQr
            ColumnLayout {
                id: banda
                anchors { left: parent.left; right: parent.right; verticalCenter: parent.verticalCenter; margins: 12 }
                spacing: 2
                Text {
                    visible: dlg.nombreColor !== ""
                    text: qsTr("QR %1").arg((dlg.etiquetasColor[dlg.nombreColor] || dlg.nombreColor).toUpperCase())
                    color: "white"
                    font { pixelSize: 12; weight: Font.DemiBold; letterSpacing: 1.2 }
                }
                TextField {
                    id: descCampo
                    Layout.fillWidth: true
                    text: dlg.descripcion
                    placeholderText: qsTr("Descripción: qué es este QR (p. ej. «Enlace para los invitados»)")
                    placeholderTextColor: Qt.rgba(1, 1, 1, 0.7)
                    color: "white"
                    font { pixelSize: 20; weight: Font.DemiBold }
                    background: Rectangle { color: "transparent"; border.color: descCampo.activeFocus ? "white" : "transparent"; radius: 6 }
                    onTextEdited: dlg.descripcion = text
                }
            }
            MouseArea { // arrastrar la ventana desde la banda
                anchors.fill: parent
                z: -1
                property point desde
                onPressed: (m) => desde = Qt.point(m.x, m.y)
                onPositionChanged: (m) => { dlg.x += m.x - desde.x; dlg.y += m.y - desde.y }
            }
        }
        Text {
            visible: dlg.mensaje !== ""
            Layout.fillWidth: true
            Layout.leftMargin: 14; Layout.rightMargin: 14
            wrapMode: Text.WordWrap
            text: dlg.mensaje
            color: Theme.textDim
            font.pixelSize: 13
        }
        Rectangle {
            Layout.alignment: Qt.AlignHCenter
            Layout.preferredWidth: Math.min(dlg.width - 60, 320)
            Layout.preferredHeight: Layout.preferredWidth
            radius: 12
            color: "white"
            border.color: dlg.colorQr
            border.width: 4
            Image {
                anchors { fill: parent; margins: 14 }
                source: dlg.texto !== "" ? "image://qr/texto/" + encodeURIComponent(dlg.texto) : ""
                sourceSize: Qt.size(640, 640)
                fillMode: Image.PreserveAspectFit
                smooth: false
                cache: false
            }
        }
        Text {
            Layout.alignment: Qt.AlignHCenter
            text: qsTr("Escanealo con el celular, o copialo.")
            color: Theme.textFaint
            font.pixelSize: 11
        }
        // El texto, seleccionable (por si se quiere copiar a mano una parte).
        Rectangle {
            Layout.fillWidth: true
            Layout.leftMargin: 14; Layout.rightMargin: 14
            Layout.preferredHeight: textoEdit.implicitHeight + 16
            radius: 8
            color: Theme.field
            TextEdit {
                id: textoEdit
                anchors { fill: parent; margins: 8 }
                readOnly: true
                selectByMouse: true
                wrapMode: TextEdit.WrapAnywhere
                text: dlg.texto
                color: Theme.text
                font { pixelSize: 13; family: Theme.mono }
            }
        }
        // El color: lo elige el asistente; el operador lo puede cambiar.
        RowLayout {
            Layout.leftMargin: 14; Layout.rightMargin: 14
            spacing: 6
            Text { text: qsTr("Color:"); color: Theme.textDim; font.pixelSize: 12 }
            Repeater {
                model: Object.keys(dlg.colores)
                delegate: Rectangle {
                    required property string modelData
                    width: 22; height: 22; radius: 11
                    color: dlg.colores[modelData]
                    border.color: dlg.colorNombre === modelData ? "white" : "transparent"
                    border.width: 2
                    ToolTip.visible: chipMa.containsMouse; ToolTip.text: dlg.etiquetasColor[modelData] || modelData; ToolTip.delay: 300
                    MouseArea { id: chipMa; anchors.fill: parent; hoverEnabled: true; onClicked: dlg.colorNombre = modelData }
                }
            }
        }
        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 14; Layout.rightMargin: 14; Layout.bottomMargin: 14
            spacing: 10
            Item { Layout.fillWidth: true }
            GlassButton {
                text: dlg.copiado ? qsTr("Copiado ✓") : qsTr("Copiar al portapapeles")
                icon: "⧉"
                active: true
                onClicked: { Studio.copiar(dlg.texto); dlg.copiado = true }
            }
            GlassButton {
                visible: /^https?:\/\//.test(dlg.texto)
                text: qsTr("Abrir")
                onClicked: Qt.openUrlExternally(dlg.texto)
            }
            GlassButton { text: qsTr("Cerrar"); onClicked: dlg.close() }
        }
    }
}
