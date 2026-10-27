// Los eventos (arranque/Configuraciones: cada carpeta es un evento con todo lo suyo)
// (escenas, medios, plugins, Cuerpo, botonera, eventos), sin compartir nada. Cambiar de
// una a otra no copia nada: la app se reinicia con la otra carpeta. Crear sí escribe
// sus archivos una vez (vacía, copia de la actual, una demo o la de un evento).
// Lo mismo por MCP: eventos_listar, evento_nuevo, evento_cambiar, evento_borrar, evento_recuperar.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import BA

ColumnLayout {
    id: pc
    spacing: 10
    readonly property var cf: Studio.configuraciones
    function avisar(e) { if (e) Studio.notify(e, "error") }
    function tamano(b) { return b > 1e9 ? (b / 1e9).toFixed(1) + " GB" : b > 1e6 ? Math.round(b / 1e6) + " MB" : Math.round(b / 1e3) + " KB" }

    Text { text: "Eventos"; color: Theme.text; font { pixelSize: 15; weight: Font.DemiBold } }
    Text {
        Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.textDim; font.pixelSize: 12
        text: "Cada evento tiene todo lo suyo: escenas, mezclador, medios, gráficos, plugins, asamblea, botonera, orden del día, " +
              "página web, guion y grabaciones. Nada se comparte, así un cambio en uno no rompe otro. Abrir otro evento no copia nada: " +
              "la app se reinicia con él. (Lo mismo que «Evento nuevo…» y «Abrir otro…» en la página del evento.)"
    }
    Repeater {
        model: pc.cf ? pc.cf.lista : []
        delegate: Rectangle {
            required property var modelData
            Layout.fillWidth: true
            implicitHeight: fila.implicitHeight + 16
            radius: 10
            color: modelData.activa ? Qt.rgba(1, 0.42, 0.17, 0.12) : Qt.rgba(1, 1, 1, 0.04)
            border.color: modelData.activa ? Theme.accent : Theme.glassBorder
            RowLayout {
                id: fila
                anchors { fill: parent; margins: 8 }
                spacing: 10
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 2
                    Text { text: modelData.nombre + (modelData.activa ? "  · abierto" : "") + (modelData.plantilla ? "  · plantilla" : ""); color: Theme.text
                           font { pixelSize: 14; weight: Font.DemiBold } }
                    Text { Layout.fillWidth: true; elide: Text.ElideRight; color: Theme.textFaint; font.pixelSize: 11
                           text: (modelData.descripcion ? modelData.descripcion + " · " : "") +
                                 (modelData.midiendo ? "midiendo…" : "ocupa " + pc.tamano(modelData.bytes) +
                                  (modelData.bytes_grabaciones > 0 ? " (" + pc.tamano(modelData.bytes_grabaciones) + " de grabaciones)" : "")) +
                                 (modelData.desde ? " · desde " + modelData.desde : "") }
                }
                GlassButton { compact: true; text: "Abrir"; visible: !modelData.activa; active: true
                              onClicked: ApplicationWindow.window.confirm({
                                  title: "¿Abrir «" + modelData.nombre + "»?",
                                  text: "La app se reinicia con ese evento: se corta lo que esté andando (transmisión, grabación, conferencia).",
                                  accept: "Abrir", reject: "Cancelar",
                                  action: function() { pc.avisar(pc.cf.cambiarUi(modelData.nombre)) } }) }
                GlassButton { compact: true; text: "Duplicar"
                              onClicked: { nuevo.desde = modelData.activa ? "actual" : modelData.nombre; nombreNuevo.text = modelData.nombre + " (copia)" } }
                GlassButton { compact: true; text: modelData.plantilla ? "Quitar de plantillas" : "Plantilla"
                              onClicked: pc.avisar(pc.cf.marcarPlantillaUi(modelData.nombre, !modelData.plantilla))
                              ToolTip.visible: hovered; ToolTip.delay: 400
                              ToolTip.text: "Que aparezca en «Evento nuevo › desde una plantilla»" }
                GlassButton { compact: true; text: "Renombrar…"; visible: !modelData.activa
                              onClicked: { renombrar.de = modelData.nombre; renombrarCampo.text = modelData.nombre; renombrar.open() } }
                GlassButton { compact: true; text: "✕"; visible: !modelData.activa
                              onClicked: ApplicationWindow.window.confirm({
                                  title: "¿Borrar «" + modelData.nombre + "»?",
                                  text: "Va a la papelera con todo lo suyo: se puede recuperar desde «Papelera», abajo, durante un mes." +
                                        (modelData.plantilla ? "\n\nEs una plantilla: deja de aparecer en «Evento nuevo › desde una plantilla»." : ""),
                                  accept: "Borrar", reject: "Cancelar",
                                  action: function() { pc.avisar(pc.cf.borrarUi(modelData.nombre)) } }) }
            }
        }
    }

    // --- Una nueva -----------------------------------------------------------------------
    RowLayout {
        id: nuevo
        Layout.fillWidth: true
        spacing: 8
        property string desde: "cero"
        readonly property var opciones: {
            const l = [{ t: "Desde cero (de fábrica)", v: "cero" }, { t: "Copia total del abierto", v: "actual" }]
            for (const d of (pc.cf ? pc.cf.plantillasUi() : [])) l.push({ t: "Plantilla: " + d.nombre, v: "plantilla:" + (d.carpeta || d.nombre) })
            for (const c of (pc.cf ? pc.cf.lista : [])) if (!c.activa) l.push({ t: "Copia total de " + c.nombre, v: c.nombre })
            return l
        }
        TextField { id: nombreNuevo; Layout.preferredWidth: 220; color: Theme.text; placeholderText: "Nombre del evento nuevo" }
        ComboBox {
            Layout.preferredWidth: 260
            model: nuevo.opciones
            textRole: "t"
            currentIndex: Math.max(0, nuevo.opciones.findIndex(o => o.v === nuevo.desde))
            onActivated: (i) => nuevo.desde = nuevo.opciones[i].v
        }
        GlassButton {
            text: "Crear"; active: true; enabled: nombreNuevo.text.trim() !== ""
            onClicked: {
                const e = pc.cf.nuevoEventoUi(nombreNuevo.text.trim(), nuevo.desde, false)
                if (e !== "") { pc.avisar(e); return }
                nombreNuevo.text = ""
            }
        }
        Item { Layout.fillWidth: true }
    }

    // La copia en curso (en otro hilo) y su aviso al terminar.
    Text {
        readonly property var c: pc.cf ? pc.cf.copia : ({})
        visible: !!c.nombre
        Layout.fillWidth: true
        color: Theme.accent; font.pixelSize: 12
        text: "Copiando «" + (c.nombre || "") + "»… " + (c.total > 0 ? Math.min(100, Math.round(100 * c.hechos / c.total)) + " %" : "")
    }

    // La papelera: lo borrado vuelve con su nombre (si ya está usado, avisa).
    ColumnLayout {
        id: papelera
        Layout.fillWidth: true
        spacing: 4
        visible: pc.cf && pc.cf.papelera.length > 0
        property bool abierta: false
        readonly property real ocupa: pc.cf ? pc.cf.papelera.reduce((t, p) => t + (p.bytes || 0), 0) : 0
        RowLayout {
            spacing: 8
            GlassButton {
                compact: true
                text: (papelera.abierta ? "▾ " : "▸ ") + "Papelera (" + (pc.cf ? pc.cf.papelera.length : 0) + ")" +
                      (papelera.ocupa > 0 ? " · " + pc.tamano(papelera.ocupa) : "")
                onClicked: papelera.abierta = !papelera.abierta
            }
            GlassButton {
                compact: true; text: pc.cf && pc.cf.vaciando ? "Vaciando…" : "Vaciar"
                visible: papelera.abierta; enabled: pc.cf && !pc.cf.vaciando
                onClicked: ApplicationWindow.window.confirm({
                    title: "¿Vaciar la papelera?",
                    text: "Se borra todo de verdad" + (papelera.ocupa > 0 ? " (" + pc.tamano(papelera.ocupa) + ")" : "") +
                          ": ya no se puede recuperar.",
                    accept: "Vaciar", reject: "Cancelar",
                    action: function() { pc.avisar(pc.cf.vaciarPapeleraUi("")) } })
            }
            Text { color: Theme.textFaint; font.pixelSize: 11; text: "lo que lleva más de un mes se borra solo" }
        }
        Repeater {
            model: papelera.abierta && pc.cf ? pc.cf.papelera : []
            delegate: RowLayout {
                required property var modelData
                Layout.fillWidth: true
                spacing: 10
                Text { Layout.fillWidth: true; elide: Text.ElideRight; color: Theme.textDim; font.pixelSize: 12
                       text: modelData.nombre + (modelData.borrado_ms > 1 ? "  · borrado el " +
                             new Date(modelData.borrado_ms).toLocaleString(Qt.locale(), "d/M/yyyy HH:mm") : "  · a medio borrar") +
                             (modelData.bytes ? " · " + pc.tamano(modelData.bytes) : "") }
                GlassButton { compact: true; text: "Recuperar"; visible: modelData.borrado_ms !== 1; onClicked: pc.avisar(pc.cf.recuperarUi(modelData.id)) }
            }
        }
    }

    Dialog {
        id: renombrar
        property string de: ""
        title: "Renombrar «" + de + "»"
        modal: true
        anchors.centerIn: Overlay.overlay
        standardButtons: Dialog.Ok | Dialog.Cancel
        TextField { id: renombrarCampo; width: 280; color: Theme.text }
        onAccepted: pc.avisar(pc.cf.renombrarUi(de, renombrarCampo.text.trim()))
    }
}
