// Generar para otro equipo: esta máquina se suma como GENERADORA a la prueba de sala de otro
// BroadcastAppliance (el otro la arrancó con «Repartir entre varias máquinas» y muestra un
// código BAGEN1.). Solo presentación: todo sale de Studio.pruebaSala (generarPara,
// dejarDeGenerar, `generando`), lo mismo que la herramienta MCP prueba_sala_generar.
// Ver docs/funciones/PRUEBA-SALA.md («Con dos o más PC»).
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import BA

Seccion {
    id: gp
    readonly property var ps: Studio.pruebaSala
    readonly property var g: ps.generando
    readonly property bool activo: g.activo === true

    titulo: qsTr("Generar para otro equipo")
    subtitulo: qsTr("Esta PC manda participantes de prueba a la prueba de sala de OTRA: en la otra, arrancá la prueba con «Repartir entre varias máquinas» y copiá su código. La otra mide; acá solo se genera.")
    resaltado: gp.activo ? Theme.amber : "transparent"

    RowLayout {
        Layout.fillWidth: true
        visible: !gp.activo
        spacing: 8
        TextField {
            id: codigo
            Layout.fillWidth: true
            placeholderText: qsTr("Código del otro equipo (BAGEN1.…)")
            echoMode: TextInput.PasswordEchoOnEdit
        }
        GlassButton {
            text: qsTr("Empezar a generar…")
            active: true
            enabled: codigo.text.trim() !== "" && gp.ps.hinchadaDisponible && !gp.ps.corriendo
            onClicked: ApplicationWindow.window.confirm({
                title: qsTr("¿Generar para el otro equipo?"),
                text: qsTr("Esta máquina va a mandar participantes de prueba a la prueba del otro hasta que termine: se va a cargar unos minutos."),
                accept: qsTr("Generar"), reject: qsTr("Cancelar"),
                action: function() {
                    const e = gp.ps.generarPara(codigo.text)
                    if (e) Studio.notify(e, "error"); else codigo.text = ""
                }
            })
        }
    }
    RowLayout {
        Layout.fillWidth: true
        visible: gp.activo
        spacing: 8
        StatusPill {
            text: gp.g.estado === "arrancando" ? qsTr("arrancando")
                  : gp.g.estado === "parando" ? qsTr("parando")
                  : qsTr("generando para %1").arg(gp.g.maquina || "")
            dot: Theme.amber
        }
        StatusPill { text: qsTr("hinchada %1 %").arg(gp.g.cpu >= 0 ? Math.round(gp.g.cpu) : "–"); dot: gp.g.cpu > 90 ? Theme.live : Theme.ok }
        StatusPill { text: qsTr("%1 de %2 procesos").arg(gp.g.andando || 0).arg(gp.g.procesos || 0); dot: Theme.textDim }
        Item { Layout.fillWidth: true }
        GlassButton { text: qsTr("Dejar de generar"); onClicked: gp.ps.dejarDeGenerar() }
    }
    Text {
        Layout.fillWidth: true
        visible: gp.activo
        wrapMode: Text.WordWrap; color: Theme.textFaint; font.pixelSize: 11
        text: qsTr("La primera vez, hinchada graba su video de prueba (unos minutos) antes de mandar. Termina sola cuando termina la prueba del otro.")
    }
    Text {
        Layout.fillWidth: true
        visible: !gp.activo && (gp.g.error || "") !== ""
        wrapMode: Text.WordWrap; color: Theme.amber; font.pixelSize: 11
        text: gp.g.error || ""
    }
    Text {
        Layout.fillWidth: true
        visible: !gp.ps.hinchadaDisponible
        wrapMode: Text.WordWrap; color: Theme.amber; font.pixelSize: 11
        text: qsTr("El generador de prueba (hinchada) no viene en este programa.")
    }
}
