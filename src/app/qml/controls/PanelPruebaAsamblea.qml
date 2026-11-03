// La prueba de asamblea: integrantes simulados que entran por la sesión remota (link + PIN,
// la espera, la palabra, el voto) para probar la asamblea antes de la sesión real.
// Solo presentación: todo sale de Studio.pruebaAsamblea (ui/PruebaAsambleaViewModel), el
// mismo modelo que usan las herramientas prueba_asamblea_* del MCP. Ver
// docs/funciones/PRUEBA-ASAMBLEA.md.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import BA

Seccion {
    id: pa
    readonly property var m: Studio.pruebaAsamblea
    readonly property var v: m ? m.vivo : ({})
    readonly property var ult: m ? m.ultimoInforme : ({})
    readonly property var porOmision: m ? m.comportamientoPorOmision : ({})
    /// Para ir a habilitarlos (la página cambia de pestaña).
    signal irAlRecinto()

    titulo: qsTr("Prueba de asamblea")
    subtitulo: qsTr("Integrantes de prueba entran como desde el celular: con su link y PIN (o usuario y contraseña), esperan que los habilites en el Recinto, piden la palabra, hablan con cámara y micrófono y votan. Corre en un evento de prueba: nunca toca el padrón real, y al terminar se borran ellos y su acceso.")
    resaltado: pa.m && pa.m.corriendo ? Theme.amber : "transparent"
    extra: StatusPill {
        visible: !!pa.m && pa.m.activa
        text: pa.m && pa.m.estado === "preparada" ? qsTr("preparada") : qsTr("%1/%2 conectados").arg(pa.v.conectados || 0).arg(pa.v.pedidos || 0)
        dot: Theme.ok
    }

    // --- Dónde corre: solo en un evento sin integrantes reales ------------------------------
    Text {
        Layout.fillWidth: true
        visible: !!pa.m && !pa.m.activa && pa.m.impedimento !== ""
        wrapMode: Text.WordWrap; color: Theme.amber; font.pixelSize: 12
        text: pa.m ? pa.m.impedimento : ""
    }
    RowLayout {
        visible: !!pa.m && !pa.m.activa && pa.m.integrantesReales > 0
        spacing: 8
        GlassButton {
            text: qsTr("Crear un evento de prueba y abrirlo…")
            compact: true
            onClicked: ApplicationWindow.window.confirm({
                title: qsTr("Evento de prueba"),
                text: qsTr("Se crea el evento «Prueba de asamblea» vacío (desde cero) y se abre: el programa se reinicia y se corta lo que sale. Para volver a este evento, abrilo desde la pestaña Eventos. ¿Lo creo?"),
                accept: qsTr("Crear y abrir"), reject: qsTr("Cancelar"),
                action: function() { Studio.configuraciones.nuevoEventoUi(qsTr("Prueba de asamblea"), "cero", true) }
            })
        }
    }
    GlassButton {
        visible: !!pa.m && !pa.m.activa && pa.m.integrantesPrueba > 0
        compact: true
        text: qsTr("Borrar los %n integrantes de prueba que quedaron", "", pa.m ? pa.m.integrantesPrueba : 0)
        onClicked: { const e = pa.m.limpiar(); if (e) Studio.notify(e, "error") }
    }

    // --- Para arrancar ------------------------------------------------------------------------
    GridLayout {
        Layout.fillWidth: true
        visible: !!pa.m && !pa.m.corriendo && pa.m.estado !== "terminando" && pa.m.integrantesReales === 0
        columns: 4
        columnSpacing: 8
        rowSpacing: 6
        Text { text: qsTr("Integrantes"); color: Theme.textDim; font.pixelSize: 12 }
        SpinBox { id: cuantos; from: 1; to: pa.m ? pa.m.maxIntegrantes : 500; value: 10; editable: true
                  enabled: !!pa.m && pa.m.estado === "apagada" }
        Text { text: qsTr("Minutos"); color: Theme.textDim; font.pixelSize: 12 }
        SpinBox { id: minutos; from: 1; to: 120; value: 5; editable: true }
        Text { text: qsTr("% afuera"); color: Theme.textDim; font.pixelSize: 12 }
        SpinBox { id: afuera; from: 0; to: 100; value: pa.porOmision.afuera_pct || 0; editable: true }
        Text { text: qsTr("% piden la palabra"); color: Theme.textDim; font.pixelSize: 12 }
        SpinBox { id: piden; from: 0; to: 100; value: pa.porOmision.piden_palabra_pct || 0; editable: true }
        Text { text: qsTr("% votan"); color: Theme.textDim; font.pixelSize: 12 }
        SpinBox { id: votan; from: 0; to: 100; value: pa.porOmision.votan_pct || 0; editable: true }
        Text { text: qsTr("Cómo votan"); color: Theme.textDim; font.pixelSize: 12 }
        ComboBox {
            id: voto
            Layout.fillWidth: true
            textRole: "texto"
            valueRole: "valor"
            model: [
                { texto: qsTr("Al azar"), valor: "azar" },
                { texto: qsTr("Mayoría afirmativa (60/30/10)"), valor: "afirmativo:60,negativo:30,abstencion:10" },
                { texto: qsTr("Pareja (45/45/10)"), valor: "afirmativo:45,negativo:45,abstencion:10" },
                { texto: qsTr("Mayoría negativa (30/60/10)"), valor: "afirmativo:30,negativo:60,abstencion:10" },
                { texto: qsTr("Todos afirmativo"), valor: "afirmativo:1" }
            ]
        }
        Text { text: qsTr("% se reconectan"); color: Theme.textDim; font.pixelSize: 12 }
        SpinBox { id: reconectan; from: 0; to: 100; value: pa.porOmision.reconectan_pct || 0; editable: true }
        Text { text: qsTr("% PIN errado"); color: Theme.textDim; font.pixelSize: 12 }
        SpinBox { id: pinErrado; from: 0; to: 100; value: pa.porOmision.pin_errado_pct || 0; editable: true }
        Text { text: qsTr("% con usuario"); color: Theme.textDim; font.pixelSize: 12 }
        SpinBox { id: conUsuario; from: 0; to: 100; value: pa.porOmision.con_usuario_pct || 0; editable: true }
        Text { text: qsTr("Habilitarlos solos a los (s)"); color: Theme.textDim; font.pixelSize: 12 }
        SpinBox { id: habilitarSolos; from: 0; to: 600; value: 0; editable: true
                  ToolTip.visible: hovered; ToolTip.delay: 500
                  ToolTip.text: qsTr("0: los habilitás vos en el Recinto, viéndoles la cara (el número)") }
    }
    CheckBox {
        id: conHinchada
        visible: !!pa.m && !pa.m.corriendo && pa.m.integrantesReales === 0
        checked: !!pa.m && pa.m.hinchadaDisponible
        enabled: !!pa.m && pa.m.hinchadaDisponible
        text: pa.m && pa.m.hinchadaDisponible ? qsTr("Los genera esta máquina (hinchada)")
                                              : qsTr("Sin probador en el programa: generador externo")
    }
    Text {
        Layout.fillWidth: true
        visible: !!pa.m && !pa.m.corriendo && pa.m.integrantesReales === 0
        wrapMode: Text.WordWrap; color: Theme.textFaint; font.pixelSize: 11
        text: qsTr("Los que erran el PIN lo prueban 5 veces: el acceso queda bloqueado 15 minutos, como pasaría de verdad. Los que se reconectan pierden la red una vez y vuelven solos: si fue en menos de 2 minutos, siguen habilitados.")
    }
    RowLayout {
        visible: !!pa.m && !pa.m.corriendo && pa.m.estado !== "terminando" && pa.m.integrantesReales === 0
        spacing: 8
        GlassButton {
            visible: !!pa.m && pa.m.estado === "apagada"
            enabled: !!pa.m && pa.m.impedimento === ""
            compact: true
            text: qsTr("Generar integrantes de prueba")
            onClicked: { const e = pa.m.preparar(cuantos.value); if (e) Studio.notify(qsTr("Prueba de asamblea: %1").arg(e), "error") }
        }
        GlassButton {
            active: true
            enabled: !!pa.m && (pa.m.estado === "preparada" || pa.m.impedimento === "")
            text: qsTr("Arrancar la prueba")
            onClicked: {
                const e = pa.m.arrancar(cuantos.value, minutos.value, {
                    afuera_pct: afuera.value, piden_palabra_pct: piden.value, votan_pct: votan.value,
                    voto: voto.currentValue, reconectan_pct: reconectan.value, pin_errado_pct: pinErrado.value,
                    con_usuario_pct: conUsuario.value, habilitar_solos_s: habilitarSolos.value
                }, conHinchada.checked)
                if (e) Studio.notify(qsTr("Prueba de asamblea: %1").arg(e), "error")
            }
        }
        GlassButton {
            visible: !!pa.m && pa.m.estado === "preparada"
            compact: true
            text: qsTr("Descartar")
            onClicked: pa.m.parar()
        }
    }

    // --- En curso ----------------------------------------------------------------------------
    Flow {
        Layout.fillWidth: true
        visible: !!pa.m && pa.m.activa
        spacing: 6
        StatusPill { text: qsTr("%1 esperan que los habiliten").arg(pa.v.esperando || 0); dot: (pa.v.esperando || 0) > 0 ? Theme.amber : Theme.textFaint }
        StatusPill { text: qsTr("%1 habilitados").arg(pa.v.habilitados || 0); dot: Theme.ok }
        StatusPill { text: qsTr("%1 afuera").arg(pa.v.afuera || 0); dot: Theme.textFaint }
        StatusPill { text: qsTr("%1 en la fila").arg(pa.v.enLaFila || 0); dot: Theme.textFaint }
        StatusPill { text: qsTr("%1 con la palabra").arg(pa.v.conPalabra || 0); dot: (pa.v.conPalabra || 0) > 0 ? Theme.live : Theme.textFaint }
        StatusPill { text: qsTr("%1 votaron").arg(pa.v.votaron || 0); dot: Theme.ok }
        StatusPill { visible: (pa.v.bloqueados || 0) > 0; text: qsTr("%1 bloqueados").arg(pa.v.bloqueados || 0); dot: Theme.live }
    }
    RowLayout {
        visible: !!pa.m && pa.m.activa
        spacing: 8
        GlassButton { compact: true; text: qsTr("Ir al Recinto (a habilitarlos)"); visible: (pa.v.esperando || 0) > 0; onClicked: pa.irAlRecinto() }
        GlassButton {
            visible: !!pa.m && pa.m.corriendo
            text: qsTr("Terminar la prueba")
            onClicked: pa.m.parar()
        }
        Text { visible: !!pa.m && pa.m.estado === "terminando"; text: qsTr("Terminando: el probador se va y se limpia…"); color: Theme.textDim; font.pixelSize: 12 }
    }
    // Para un generador externo: el archivo de credenciales y la línea de hinchada.
    Text {
        Layout.fillWidth: true
        visible: !!pa.m && pa.m.corriendo && pa.m.generador === "externo" && pa.m.archivoCredenciales !== ""
        wrapMode: Text.WordWrap; color: Theme.textDim; font.pixelSize: 11
        text: qsTr("Generador externo: copiá este archivo a la otra máquina (son credenciales de prueba: se revocan al terminar) y corré ahí la línea de abajo:")
    }
    TextField {
        Layout.fillWidth: true
        visible: !!pa.m && pa.m.corriendo && pa.m.generador === "externo" && pa.m.archivoCredenciales !== ""
        readOnly: true; selectByMouse: true; font.family: Theme.mono; font.pixelSize: 11
        text: pa.m ? pa.m.archivoCredenciales : ""
    }
    TextField {
        Layout.fillWidth: true
        visible: !!pa.m && pa.m.corriendo && pa.m.generador === "externo" && pa.m.comandoExterno !== ""
        readOnly: true; selectByMouse: true; font.family: Theme.mono; font.pixelSize: 11
        text: pa.m ? pa.m.comandoExterno : ""
    }

    // --- La última ----------------------------------------------------------------------------
    Text {
        Layout.fillWidth: true
        visible: !!pa.m && !pa.m.activa && pa.ult.nota !== undefined
        wrapMode: Text.WordWrap; font.pixelSize: 11
        color: pa.ult.bien ? Theme.ok : Theme.amber
        text: pa.ult.nota !== undefined ? qsTr("Última: %1 (en el evento: %2)").arg(pa.ult.nota).arg(pa.ult.archivo || "–") : ""
    }
}
