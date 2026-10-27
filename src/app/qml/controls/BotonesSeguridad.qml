// FTB y PLACA a un toque: lo que tapa todo lo que sale (encima del programa,
// sin tocar las escenas). Al aire, prenderlos pide confirmación; apagarlos, no.
// Titilan mientras están puestos.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import BA

GridLayout {
    id: seg
    // Angosta (la lista de escenas): en dos renglones. `angosta` lo fija desde
    // afuera: mirar el propio ancho no alcanza (con 4 columnas el ancho mínimo
    // crece y empuja al panel, que entonces nunca queda angosto).
    property bool angosta: false
    columns: angosta || width < 420 ? 2 : 4
    columnSpacing: 6
    rowSpacing: 4
    property var estado: Studio.estadoPlacaUi()
    Connections { target: Studio; function onPlacaChanged() { seg.estado = Studio.estadoPlacaUi() } }

    function poner(cual, activar) {
        const hacer = function() {
            const e = cual === "ftb" ? Studio.ftbUi(activar, 800, audioFtb.checked ? "bajar" : "mantener")
                                     : Studio.placaUi(activar, textoPlaca.editText, "", 600)
            if (e !== "") Studio.notify(e, "error")
        }
        if (activar && Studio.alAire)
            ApplicationWindow.window.confirm({
                title: cual === "ftb" ? qsTr("¿Mandar la salida a negro?") : qsTr("¿Tapar la salida con la placa «%1»?").arg(textoPlaca.editText),
                text: qsTr("Se ve en la transmisión y en la grabación."),
                accept: cual === "ftb" ? qsTr("A negro") : qsTr("Poner la placa"), reject: qsTr("Cancelar"),
                action: hacer
            })
        else
            hacer()
    }

    GlassButton {
        id: botonFtb
        text: seg.estado.ftb ? qsTr("SACAR NEGRO") : "FTB"
        active: !!seg.estado.ftb
        activeColor: "#000000"
        // Angosta, los botones no se estiran: el lugar es para el texto de la placa.
        Layout.fillWidth: seg.columns === 2 && !seg.angosta
        Layout.preferredWidth: seg.angosta ? 96 : 110
        onClicked: seg.poner("ftb", !seg.estado.ftb)
        ToolTip.visible: hovered; ToolTip.delay: 400
        ToolTip.text: qsTr("Fundido a negro de todo lo que sale")
        SequentialAnimation on opacity {
            running: !!seg.estado.ftb && seg.visible; loops: Animation.Infinite
            NumberAnimation { to: 0.5; duration: 500 }
            NumberAnimation { to: 1.0; duration: 500 }
        }
    }
    CheckBox { id: audioFtb; checked: true; text: qsTr("con audio"); font.pixelSize: 11 }
    GlassButton {
        text: seg.estado.placa ? qsTr("SACAR PLACA") : qsTr("PLACA")
        active: !!seg.estado.placa
        activeColor: Theme.amber
        Layout.fillWidth: seg.columns === 2 && !seg.angosta
        Layout.preferredWidth: seg.angosta ? 96 : 120
        onClicked: seg.poner("placa", !seg.estado.placa)
        SequentialAnimation on opacity {
            running: !!seg.estado.placa && seg.visible; loops: Animation.Infinite
            NumberAnimation { to: 0.5; duration: 500 }
            NumberAnimation { to: 1.0; duration: 500 }
        }
    }
    ComboBox {
        id: textoPlaca
        Layout.fillWidth: true
        Layout.minimumWidth: seg.angosta ? 100 : 140
        editable: true
        model: Studio.textosPlaca()
        // Editable, el campo queda con el cursor al final y en angosto se veía
        // la cola del texto («…mos enseguida»): sin foco, que se vea el principio.
        function alPrincipio() { if (!activeFocus && contentItem) contentItem.cursorPosition = 0 }
        onActiveFocusChanged: alPrincipio()
        onCurrentTextChanged: Qt.callLater(alPrincipio)
        Component.onCompleted: Qt.callLater(alPrincipio)
    }
}
