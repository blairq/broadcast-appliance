// El enlace: la velocidad por el túnel (hasta el servidor) y cuánto necesita el evento.
// Solo presentación: todo sale de Studio.enlace (ui/EnlaceViewModel), el mismo modelo que
// usan la prueba de sala y las herramientas enlace_* del MCP. Ver docs/funciones/ENLACE.md.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import BA

Panel {
    id: panel
    readonly property var en: Studio.enlace
    readonly property var med: en.medicion
    readonly property var nec: en.necesidad
    readonly property var falta: nec.necesita || ({})
    readonly property var rep: med.reposo || ({})
    readonly property var sub: med.subida || ({})
    readonly property var baj: med.bajada || ({})
    title: qsTr("Enlace (por el túnel)")
    Layout.preferredHeight: col.implicitHeight + 62

    function mbps(kbps) { return kbps === undefined || kbps < 0 ? "–" : (kbps / 1000).toFixed(1) + " Mbps" }
    function ms(x) { return x === undefined || x < 0 ? "–" : Math.round(x) + " ms" }
    function pct(x) { return x === undefined ? "–" : (x * 100).toFixed(1) + " %" }
    function colorVeredicto(v) { return v === "entra" ? Theme.ok : v === "justo" ? Theme.amber : v === "no_entra" ? Theme.live : Theme.textFaint }
    function textoVeredicto(v) {
        return v === "entra" ? qsTr("Entra") : v === "justo" ? qsTr("Justo") : v === "no_entra" ? qsTr("No entra") : qsTr("Sin medir")
    }
    function medir() {
        const r = panel.en.riesgo
        if (!r) { const e = panel.en.medir(false); if (e) Studio.notify(e, "error"); return }
        ApplicationWindow.window.confirm({
            title: qsTr("Medir el enlace ahora"),
            text: qsTr("%1 Se mide suave (hasta 5 Mbps): alcanza para la demora y la pérdida, no para la velocidad máxima. ¿Mido igual?").arg(r),
            accept: qsTr("Medir"), reject: qsTr("Cancelar"),
            action: () => { const e = panel.en.medir(true); if (e) Studio.notify(e, "error") }
        })
    }

    ColumnLayout {
        id: col
        anchors.fill: parent
        spacing: 8

        Text {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            color: Theme.textFaint
            font.pixelSize: 11
            text: panel.en.estado === "sin_sala" ? qsTr("La conferencia no está andando.")
                  : !panel.en.hayTunel ? qsTr("Sin túnel WireGuard: la medición va por el túnel hasta el servidor propio (Acceso directo por un servidor propio…).")
                  : qsTr("Los invitados entran por el túnel y el retorno sale una vez por cada uno: lo que manda es la subida de este lugar. Se mide hasta el eco del servidor (%1).").arg(panel.en.eco)
        }

        // --- Cuánto hace falta ---------------------------------------------------------
        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            Text { text: qsTr("Invitados"); color: Theme.textDim; font.pixelSize: 12 }
            SpinBox {
                from: 1; to: 64; editable: true
                value: panel.en.invitados
                onValueModified: panel.en.invitados = value
            }
            StatusPill { text: panel.textoVeredicto(panel.nec.veredicto); dot: panel.colorVeredicto(panel.nec.veredicto) }
        }
        Text {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            color: Theme.text
            font.pixelSize: 11
            text: qsTr("Hace falta: subida %1 · bajada %2 (%3 en alta; con WireGuard %4 / %5)")
                  .arg(panel.mbps(panel.falta.subidaKbps)).arg(panel.mbps(panel.falta.bajadaKbps))
                  .arg(panel.falta.enAlta !== undefined ? panel.falta.enAlta : "–")
                  .arg(panel.mbps(panel.falta.subidaEnlaceKbps)).arg(panel.mbps(panel.falta.bajadaEnlaceKbps))
        }
        Text {
            Layout.fillWidth: true
            visible: (panel.nec.entranConMargen || -1) >= 0
            wrapMode: Text.WordWrap
            color: Theme.textDim
            font.pixelSize: 11
            text: qsTr("Con lo medido entran hasta %1 invitados con margen (%2 justo).").arg(panel.nec.entranConMargen).arg(panel.nec.entranJusto)
        }
        Repeater {
            model: panel.nec.motivos || []
            delegate: Text {
                required property var modelData
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                color: Theme.amber
                font.pixelSize: 11
                text: "· " + modelData
            }
        }

        // --- Lo medido -----------------------------------------------------------------
        GridLayout {
            Layout.fillWidth: true
            visible: panel.sub.capacidadKbps !== undefined || panel.rep.rttMs !== undefined
            columns: 2
            columnSpacing: 10
            rowSpacing: 2
            Text { text: qsTr("Subida (sin pérdida / máx.)"); color: Theme.textDim; font.pixelSize: 11 }
            Text { text: panel.mbps(panel.sub.kbpsSinPerdida) + " / " + panel.mbps(panel.sub.capacidadKbps); color: Theme.text; font { family: Theme.mono; pixelSize: 11 } }
            Text { text: qsTr("Bajada (sin pérdida / máx.)"); color: Theme.textDim; font.pixelSize: 11 }
            Text { text: panel.mbps(panel.baj.kbpsSinPerdida) + " / " + panel.mbps(panel.baj.capacidadKbps); color: Theme.text; font { family: Theme.mono; pixelSize: 11 } }
            Text { text: qsTr("Demora en reposo / con carga"); color: Theme.textDim; font.pixelSize: 11 }
            Text {
                text: panel.ms(panel.rep.rttMs) + " / " + panel.ms(Math.max(panel.sub.rttCargaMs || -1, panel.baj.rttCargaMs || -1))
                color: Theme.text; font { family: Theme.mono; pixelSize: 11 }
            }
            Text { text: qsTr("Jitter / pérdida"); color: Theme.textDim; font.pixelSize: 11 }
            Text {
                text: panel.ms(Math.max(panel.sub.jitterMs || 0, panel.baj.jitterMs || 0)) + " / " + panel.pct(Math.max(panel.sub.perdida || 0, panel.baj.perdida || 0))
                color: Theme.text; font { family: Theme.mono; pixelSize: 11 }
            }
        }
        Text {
            Layout.fillWidth: true
            visible: !panel.en.midiendo && panel.en.ultima.ok === false && panel.en.ultima.cancelado !== true
            wrapMode: Text.WordWrap
            color: Theme.live
            font.pixelSize: 11
            text: panel.en.ultima.error || ""
        }

        // --- Midiendo ------------------------------------------------------------------
        ColumnLayout {
            Layout.fillWidth: true
            visible: panel.en.midiendo
            spacing: 4
            Text {
                color: Theme.textDim; font.pixelSize: 11
                text: panel.en.fase === "reposo" ? qsTr("Demora en reposo…")
                      : panel.en.fase === "subida" ? qsTr("Subida: %1…").arg(panel.mbps(panel.med.kbpsEscalon))
                      : panel.en.fase === "bajada" ? qsTr("Bajada: %1…").arg(panel.mbps(panel.med.kbpsEscalon))
                      : qsTr("Buscando el eco del servidor…")
            }
            ProgressBar { Layout.fillWidth: true; from: 0; to: 1; value: panel.en.avance }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            GlassButton {
                visible: !panel.en.midiendo
                enabled: panel.en.hayTunel
                text: qsTr("Medir el enlace")
                active: true
                compact: true
                onClicked: panel.medir()
            }
            GlassButton {
                visible: panel.en.midiendo
                text: qsTr("Cancelar")
                compact: true
                onClicked: panel.en.cancelar()
            }
            GlassButton {
                visible: panel.en.hayTunel && !panel.en.midiendo
                text: qsTr("Instalar el eco en el servidor…")
                compact: true
                onClicked: { ecoDlg.script = ""; ecoDlg.error = ""; ecoDlg.open(); panel.en.pedirInstalador() }
            }
        }
    }

    // El eco del servidor: lo que se pega como root en el VPS (instala o actualiza).
    Dialog {
        id: ecoDlg
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: visible ? Math.min(parent.width - 80, 760) : 760
        modal: true
        title: qsTr("Eco del medidor en el servidor")
        standardButtons: Dialog.Close
        property string script: ""
        property string error: ""
        Connections {
            target: panel.en
            function onInstaladorListo(script, error) { ecoDlg.script = script; ecoDlg.error = error }
        }
        ColumnLayout {
            anchors.fill: parent
            spacing: 10
            Text {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                color: Theme.textDim; font.pixelSize: 12
                text: qsTr("En el servidor del túnel, como root, pegá esto en una terminal: instala (o actualiza) el eco del medidor como servicio (ba-enlace-eco, UDP 51821, solo por el túnel). Se puede repetir sin problema.")
            }
            Text { visible: ecoDlg.error !== ""; text: ecoDlg.error; color: Theme.live; font.pixelSize: 12 }
            ScrollView {
                visible: ecoDlg.script !== ""
                Layout.fillWidth: true
                Layout.preferredHeight: 260
                TextArea { readOnly: true; text: ecoDlg.script; font.family: Theme.mono; font.pixelSize: 11; wrapMode: TextEdit.NoWrap; selectByMouse: true }
            }
            GlassButton { visible: ecoDlg.script !== ""; text: qsTr("Copiar"); compact: true; onClicked: Studio.copiar(ecoDlg.script) }
        }
    }
}
