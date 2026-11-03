// Elegir el vivo de una cuenta como destino de una salida (página Salidas): la
// plataforma, la cuenta (las de Studio.cuentas.vivos.cuentasPor) y un vivo de esa cuenta
// (los que trae cargarVivos: programados o activos) o uno nuevo (título, privacidad,
// inicio). En Twitch el vivo es el canal. Si otra salida ya manda a ese vivo, el modelo no
// lo asigna y lo dice (`asignado` con el error); si sale bien, se cierra.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import BA

Popup {
    id: dlg
    /// La salida a la que se asigna (id).
    property string salida: ""
    property string plataforma: "youtube"
    property string cuenta: "principal"
    /// El vivo elegido de la lista (id y título).
    property string vivo: ""
    property string vivoTitulo: ""
    property bool nuevo: false
    readonly property var vivos: Studio.cuentas.vivos
    readonly property var de: Studio.cuentas.vivos.vivosDe
    readonly property bool aparte: dlg.de.aparte !== false

    function cargar() {
        dlg.vivo = ""; dlg.vivoTitulo = ""
        dlg.vivos.cargarVivos(dlg.plataforma, dlg.cuenta)
    }
    onOpened: { dlg.nuevo = false; error.text = ""; cargar() }

    parent: Overlay.overlay
    anchors.centerIn: parent
    modal: true
    padding: 16
    width: 560
    background: Rectangle { color: Theme.panel; radius: Theme.radiusSmall; border.color: Theme.glassBorder }

    Connections {
        target: dlg.vivos
        function onAsignado(e) {
            if (!dlg.opened) return
            error.text = e
            if (e === "") dlg.close()
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 10
        Text { text: qsTr("Vivo de una cuenta"); color: Theme.text; font { pixelSize: 15; weight: Font.DemiBold } }
        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            ComboBox {
                id: platCombo
                Layout.preferredWidth: 150
                model: [{ value: "youtube", label: "YouTube" }, { value: "twitch", label: "Twitch" }, { value: "facebook", label: "Facebook" }]
                textRole: "label"
                valueRole: "value"
                onActivated: { dlg.plataforma = currentValue; dlg.cuenta = "principal"; cuentaCombo.currentIndex = 0; dlg.cargar() }
            }
            ComboBox {
                id: cuentaCombo
                Layout.fillWidth: true
                model: Studio.cuentas.vivos.cuentasPor[dlg.plataforma]
                textRole: "nombre"
                valueRole: "cuenta"
                onActivated: { dlg.cuenta = currentValue; dlg.cargar() }
            }
        }
        Text {
            visible: dlg.de.error !== undefined && dlg.de.error !== ""
            Layout.fillWidth: true; wrapMode: Text.WordWrap
            text: dlg.de.error || ""
            color: Theme.amber; font.pixelSize: 12
        }
        BusyIndicator { visible: dlg.de.cargando === true; running: visible; Layout.alignment: Qt.AlignHCenter }
        // Los vivos de la cuenta (o el canal, en Twitch).
        ListView {
            id: lista
            visible: !dlg.nuevo
            Layout.fillWidth: true
            Layout.preferredHeight: 200
            clip: true
            spacing: 4
            model: dlg.vivos.vivos
            Text {
                anchors.centerIn: parent
                visible: lista.count === 0 && dlg.de.cargando !== true && (dlg.de.error || "") === ""
                text: qsTr("Esta cuenta no tiene vivos programados: creá uno nuevo.")
                color: Theme.textFaint; font.pixelSize: 12
            }
            delegate: Rectangle {
                id: v
                required property string uid
                required property string titulo
                required property string estado
                required property string privacidad
                required property string inicio
                width: ListView.view.width
                height: 40
                radius: 8
                color: dlg.vivo === v.uid && dlg.vivoTitulo === v.titulo ? Qt.rgba(1, 1, 1, 0.12) : Qt.rgba(1, 1, 1, 0.04)
                RowLayout {
                    anchors { fill: parent; margins: 8 }
                    spacing: 8
                    Text { Layout.fillWidth: true; elide: Text.ElideRight; text: v.titulo || qsTr("(sin título)"); color: Theme.text; font.pixelSize: 13 }
                    Text { text: v.estado; color: Theme.textDim; font.pixelSize: 11 }
                    Text { text: v.privacidad; color: Theme.textFaint; font.pixelSize: 11 }
                    Text { text: v.inicio; color: Theme.textFaint; font.pixelSize: 11 }
                }
                MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: { dlg.vivo = v.uid; dlg.vivoTitulo = v.titulo } }
            }
        }
        // Uno nuevo.
        GridLayout {
            visible: dlg.nuevo
            Layout.fillWidth: true
            columns: 2; columnSpacing: 8; rowSpacing: 6
            Text { text: qsTr("Título"); color: Theme.textDim; font.pixelSize: 12 }
            TextField { id: titulo; Layout.fillWidth: true; placeholderText: qsTr("p. ej. «Sesión ordinaria — audio original»") }
            Text { visible: dlg.aparte; text: qsTr("Privacidad"); color: Theme.textDim; font.pixelSize: 12 }
            ComboBox {
                id: privacidad
                visible: dlg.aparte
                Layout.fillWidth: true
                model: [{ value: "private", label: qsTr("Privado") }, { value: "unlisted", label: qsTr("No listado") }, { value: "public", label: qsTr("Público") }]
                textRole: "label"
                valueRole: "value"
            }
            Text { visible: dlg.aparte; text: qsTr("Inicio"); color: Theme.textDim; font.pixelSize: 12 }
            TextField { id: inicio; visible: dlg.aparte; Layout.fillWidth: true; placeholderText: qsTr("ISO 8601 (vacío = ahora), p. ej. 2026-11-04T18:00:00-03:00") }
        }
        Text { id: error; visible: text !== ""; Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.live; font.pixelSize: 12 }
        RowLayout {
            Layout.fillWidth: true
            GlassButton {
                compact: true
                text: dlg.nuevo ? qsTr("Elegir uno existente") : (dlg.aparte ? qsTr("Crear uno nuevo…") : qsTr("Cambiar el título…"))
                onClicked: dlg.nuevo = !dlg.nuevo
            }
            Item { Layout.fillWidth: true }
            GlassButton { compact: true; text: qsTr("Cancelar"); onClicked: dlg.close() }
            GlassButton {
                compact: true; active: true
                text: dlg.nuevo ? qsTr("Crear y asignar") : qsTr("Asignar")
                enabled: dlg.nuevo ? titulo.text.trim() !== "" : (lista.count > 0 && (dlg.vivo !== "" || !dlg.aparte))
                onClicked: {
                    error.text = ""
                    if (dlg.nuevo)
                        dlg.vivos.crearVivoUi(dlg.salida, dlg.plataforma, dlg.cuenta, titulo.text, privacidad.currentValue, inicio.text)
                    else
                        dlg.vivos.asignarVivoUi(dlg.salida, dlg.plataforma, dlg.cuenta, dlg.vivo, dlg.vivoTitulo)
                }
            }
        }
    }
}
