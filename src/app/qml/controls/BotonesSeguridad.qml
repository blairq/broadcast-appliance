// FTB y PLACA a un toque: lo que tapa todo lo que sale (encima del programa,
// sin tocar las escenas). Al aire, prenderlos pide confirmación; apagarlos, no.
// Titilan mientras están puestos.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import BA

GridLayout {
    id: seg
    // Angosta (la lista de escenas): en dos renglones.
    columns: width < 420 ? 2 : 4
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
        if (activar && Studio.alAire())
            ApplicationWindow.window.confirm({
                title: cual === "ftb" ? "¿Mandar la salida a negro?" : "¿Tapar la salida con la placa «" + textoPlaca.editText + "»?",
                text: "Se ve en la transmisión y en la grabación.",
                accept: cual === "ftb" ? "A negro" : "Poner la placa", reject: "Cancelar",
                action: hacer
            })
        else
            hacer()
    }

    GlassButton {
        id: botonFtb
        text: seg.estado.ftb ? "SACAR NEGRO" : "FTB"
        active: !!seg.estado.ftb
        activeColor: "#000000"
        Layout.fillWidth: seg.columns === 2
        Layout.preferredWidth: 110
        onClicked: seg.poner("ftb", !seg.estado.ftb)
        ToolTip.visible: hovered; ToolTip.delay: 400
        ToolTip.text: "Fundido a negro de todo lo que sale"
        SequentialAnimation on opacity {
            running: !!seg.estado.ftb; loops: Animation.Infinite
            NumberAnimation { to: 0.5; duration: 500 }
            NumberAnimation { to: 1.0; duration: 500 }
        }
    }
    CheckBox { id: audioFtb; checked: true; text: "con audio"; font.pixelSize: 11 }
    GlassButton {
        text: seg.estado.placa ? "SACAR PLACA" : "PLACA"
        active: !!seg.estado.placa
        activeColor: Theme.amber
        Layout.fillWidth: seg.columns === 2
        Layout.preferredWidth: 120
        onClicked: seg.poner("placa", !seg.estado.placa)
        SequentialAnimation on opacity {
            running: !!seg.estado.placa; loops: Animation.Infinite
            NumberAnimation { to: 0.5; duration: 500 }
            NumberAnimation { to: 1.0; duration: 500 }
        }
    }
    ComboBox {
        id: textoPlaca
        Layout.fillWidth: true
        Layout.minimumWidth: 140
        editable: true
        model: Studio.textosPlaca()
    }
}
