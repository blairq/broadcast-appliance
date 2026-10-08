// Conexiones remotas (docs/funciones/SESION-REMOTA.md): la administración de cómo entra
// cada conferencista desde el navegador (en la sala o afuera): su usuario, su
// link personal con PIN, su contraseña (para entrar tipeando), sus datos de
// contacto, si está conectado o bloqueado, y el registro. Habilitar a los que
// entran se hace en el Recinto (su banca muestra la cara). Fuente:
// Studio.sesionRemota (asamblea/SesionRemota); lo mismo por MCP con sesion_*.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import BA

ColumnLayout {
    id: cr
    spacing: 10
    readonly property var s: Studio.sesionRemota
    readonly property var parts: s ? s.participantes : []
    readonly property int esperando: parts.filter(p => p.estado === "esperando").length
    /// Lo pide la página para ir al Recinto (a habilitar).
    signal irAlRecinto()

    function estadoDe(id) {
        for (const p of cr.parts) if (p.id === id) return p
        return null
    }

    // --- Arriba ---------------------------------------------------------------------
    RowLayout {
        Layout.fillWidth: true
        spacing: 12
        Text { color: Theme.text; font.pixelSize: 14; font.weight: Font.DemiBold
               text: "Conectados: " + (cr.s ? cr.s.conectados : 0) + " · presentes: " + (cr.s ? cr.s.presentes : 0) }
        GlassButton { visible: cr.esperando > 0; active: true
                      text: cr.esperando + (cr.esperando === 1 ? " espera" : " esperan") + " que lo habiliten → Recinto"
                      onClicked: cr.irAlRecinto() }
        Item { Layout.fillWidth: true }
        Text {
            color: cr.s && cr.s.origen.indexOf("https://") === 0 ? Theme.textFaint : Theme.amber
            font.pixelSize: 11
            text: !cr.s || cr.s.origen === "" ? "Sin dirección pública todavía (¿túnel o red local con HTTPS?)"
                  : cr.s.origen.indexOf("https://") === 0 ? "Entran por " + cr.s.origen + "/sesion/"
                  : "Sin HTTPS el celular no da cámara ni micrófono: " + cr.s.origen
        }
    }
    Text {
        Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.textDim; font.pixelSize: 12
        text: "Cada uno entra con su link personal y su PIN (o con usuario y contraseña, tipeando). Sirve igual desde la sala " +
              "(el celular de respaldo si la central no anda) que desde afuera. El link, el PIN y la contraseña se ven una sola vez."
    }

    // --- Consolas de la presidencia (la página web /presidencia/) ---------------------------
    RowLayout {
        Layout.fillWidth: true
        spacing: 8
        Text { color: Theme.text; font.pixelSize: 13; font.weight: Font.DemiBold; text: "Consola de la presidencia" }
        Repeater {
            model: cr.s ? cr.s.consolasUi : []
            delegate: Rectangle {
                required property var modelData
                implicitWidth: filaC.implicitWidth + 16; implicitHeight: 30; radius: 15
                color: Qt.rgba(1, 1, 1, 0.05); border.color: modelData.conectada ? Theme.ok : Qt.rgba(1, 1, 1, 0.12)
                RowLayout {
                    id: filaC
                    anchors.centerIn: parent
                    spacing: 6
                    Text { text: (modelData.conectada ? "● " : "○ ") + modelData.nombre; color: modelData.conectada ? Theme.ok : Theme.textDim; font.pixelSize: 12 }
                    Text { text: "✕"; color: Theme.textFaint; font.pixelSize: 12
                           MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor
                                       onClicked: ApplicationWindow.window.confirm({ title: "¿Revocar el acceso de «" + modelData.nombre + "»?",
                                           text: "La consola deja de andar ya.", accept: "Revocar", reject: "Cancelar",
                                           action: function() { cr.s.revocarConsolaUi(modelData.id) } }) } }
                }
            }
        }
        GlassButton { compact: true; text: "Nuevo acceso a la consola…"
                      onClicked: { const r = cr.s.generarConsolaUi("Presidencia"); if (r.error) Studio.notify(r.error, "error"); else credencial.mostrar(r) } }
        Item { Layout.fillWidth: true }
    }

    // --- Lo que ven en el celular: su cámara (siempre, en un recuadro) y hasta dos canales ---
    RowLayout {
        id: vistasFila
        Layout.fillWidth: true
        spacing: 8
        property int tick: 0
        Connections {
            target: Studio.scenes
            function onRowsInserted() { vistasFila.tick++ }
            function onRowsRemoved() { vistasFila.tick++ }
            function onModelReset() { vistasFila.tick++ }
            function onDataChanged() { vistasFila.tick++ }
        }
        readonly property var escenas: {
            vistasFila.tick
            const m = Studio.scenes, l = []
            if (!m) return l
            for (let i = 0; i < m.rowCount(); ++i) l.push(m.data(m.index(i, 0), 257)) // NameRole
            return l
        }
        readonly property var opciones: ["—", "Salida principal"].concat(vistasFila.escenas)
        function guardar() {
            const l = []
            for (const f of [vA, vB]) {
                if (f.combo.currentIndex <= 0) continue
                l.push({ nombre: f.campo.text.trim(), fuente: f.combo.currentIndex === 1 ? "programa" : f.combo.currentText })
            }
            const e = cr.s.setVistasUi(l)
            if (e !== "") Studio.notify(e, "error")
        }
        Text { color: Theme.text; font.pixelSize: 13; font.weight: Font.DemiBold; text: "En el celular ven" }
        Text { color: Theme.textDim; font.pixelSize: 12; text: "su cámara +" }
        component CanalVista: RowLayout {
            property int indice: 0
            property alias campo: nombreV
            property alias combo: fuenteV
            readonly property var v: cr.s && cr.s.vistasUi.length > indice ? cr.s.vistasUi[indice] : null
            spacing: 4
            TextField { id: nombreV; Layout.preferredWidth: 130; color: Theme.text; placeholderText: "Nombre del canal"
                        text: parent.v ? parent.v.nombre : ""; onEditingFinished: vistasFila.guardar() }
            ComboBox {
                id: fuenteV
                Layout.preferredWidth: 170
                model: vistasFila.opciones
                currentIndex: !parent.v ? 0 : parent.v.fuente === "programa" ? 1 : Math.max(0, vistasFila.opciones.indexOf(parent.v.fuente))
                onActivated: vistasFila.guardar()
            }
        }
        CanalVista { id: vA; indice: 0 }
        CanalVista { id: vB; indice: 1 }
        Item { Layout.fillWidth: true }
    }
    Text {
        Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.textFaint; font.pixelSize: 11
        text: "Cada uno elige en su celular qué mirar: su cámara, la salida principal u otra escena; su propia imagen queda siempre en un recuadro. " +
              "Las escenas usan los canales de las pantallas de la sala (4 entre todas)."
    }

    // --- Encabezado de la tabla ---------------------------------------------------------
    RowLayout {
        Layout.fillWidth: true
        spacing: 10
        Text { Layout.preferredWidth: 60; text: "Banca"; color: Theme.textFaint; font.pixelSize: 11 }
        Text { Layout.preferredWidth: 200; text: "Conferencista"; color: Theme.textFaint; font.pixelSize: 11 }
        Text { Layout.preferredWidth: 110; text: "Usuario"; color: Theme.textFaint; font.pixelSize: 11 }
        Text { Layout.preferredWidth: 130; text: "Acceso"; color: Theme.textFaint; font.pixelSize: 11 }
        Text { Layout.preferredWidth: 170; text: "Estado"; color: Theme.textFaint; font.pixelSize: 11 }
        Item { Layout.fillWidth: true }
    }
    ListView {
        Layout.fillWidth: true
        Layout.fillHeight: true
        clip: true
        spacing: 3
        model: Studio.cuerpo.integrantes
        delegate: RowLayout {
            required property var modelData
            readonly property var est: cr.parts.length >= 0 ? cr.estadoDe(modelData.id) : null
            width: ListView.view.width
            spacing: 10
            Text { Layout.preferredWidth: 60; color: Theme.textDim; font.pixelSize: 12; text: modelData.banca ? modelData.banca : "" }
            Text { Layout.preferredWidth: 200; elide: Text.ElideRight; color: Theme.text; font.pixelSize: 12; text: modelData.nombre }
            Text { Layout.preferredWidth: 110; elide: Text.ElideRight; font.pixelSize: 12
                   color: est && est.usuario ? Theme.text : Theme.textFaint; text: est && est.usuario ? est.usuario : "—" }
            Text { Layout.preferredWidth: 130; font.pixelSize: 11
                   color: est && est.credencial ? Theme.textDim : Theme.textFaint
                   text: !est || !est.credencial ? "sin acceso" : "link + PIN" + (est.con_clave ? " · contraseña" : "") }
            Text {
                Layout.preferredWidth: 170
                font.pixelSize: 12
                color: !est ? Theme.textFaint : est.bloqueado ? Theme.live : est.estado === "habilitado" ? Theme.ok
                       : est.estado === "esperando" ? Theme.amber : Theme.textDim
                text: !est || !est.credencial ? ""
                      : est.bloqueado ? "bloqueado (clave errada)"
                      : est.estado === "habilitado" ? "presente (" + (est.lugar === "sala" ? "sala" : "afuera") + ")"
                      : est.estado === "esperando" ? "esperando (Recinto)"
                      : "sin conectar"
            }
            GlassButton { compact: true; text: est && est.credencial ? "Nuevo link…" : "Dar acceso…"
                          onClicked: {
                              const generar = function() {
                                  const r = cr.s.generarUi(modelData.id)
                                  if (r.error) { Studio.notify(r.error, "error"); return }
                                  credencial.mostrar(r)
                              }
                              if (est && est.credencial)
                                  ApplicationWindow.window.confirm({ title: "¿Generar un link nuevo?",
                                      text: "El link, el PIN y la contraseña anteriores de " + modelData.nombre + " dejan de andar.",
                                      accept: "Generar", reject: "Cancelar", action: generar })
                              else generar()
                          } }
            GlassButton { compact: true; text: "Usuario y contraseña…"; visible: !!est && est.credencial
                          onClicked: { claveDlg.id = modelData.id; claveDlg.nombre = modelData.nombre
                                       campoUsuario.text = est.usuario || ""; claveDlg.open() } }
            GlassButton { compact: true; text: "Datos…"; visible: !!est && est.credencial
                          onClicked: { datosDlg.id = modelData.id; datosDlg.nombre = modelData.nombre
                                       const d = est.datos || {}
                                       campoEmail.text = d.email || ""; campoTel.text = d.telefono || ""; campoNota.text = d.nota || ""
                                       datosDlg.open() } }
            GlassButton { compact: true; text: "Revocar"; visible: !!est && est.credencial
                          onClicked: ApplicationWindow.window.confirm({ title: "¿Revocar el acceso de " + modelData.nombre + "?",
                              text: "El link y la contraseña dejan de andar ya (si está conectado, se corta).", accept: "Revocar", reject: "Cancelar",
                              action: function() { cr.s.revocarUi(modelData.id) } }) }
            Item { Layout.fillWidth: true }
        }
    }

    // --- El link y el PIN, una sola vez ----------------------------------------------
    Dialog {
        id: credencial
        anchors.centerIn: Overlay.overlay
        modal: true
        standardButtons: Dialog.Close
        title: "Acceso de " + (datos.nombre || "")
        property var datos: ({})
        function mostrar(r) { datos = r; open(); lienzo.requestPaint() }
        ColumnLayout {
            spacing: 10
            Text { Layout.preferredWidth: 420; wrapMode: Text.WordWrap; color: Theme.amber; font.pixelSize: 12
                   text: "Se muestra UNA sola vez. Pasale el link (o el QR) y el PIN por un canal seguro, por separado si se puede." }
            Rectangle {
                Layout.alignment: Qt.AlignHCenter
                Layout.preferredWidth: 260; Layout.preferredHeight: 260; radius: 10; color: "white"
                Canvas {
                    id: lienzo
                    anchors { fill: parent; margins: 10 }
                    onPaint: {
                        const ctx = getContext("2d")
                        ctx.reset()
                        const filas = credencial.datos.qr
                        if (!filas || filas.length === 0) return
                        const n = filas.length + 8
                        const cell = Math.floor(Math.min(width, height) / n)
                        const off = Math.floor((Math.min(width, height) - cell * n) / 2) + 4 * cell
                        ctx.fillStyle = "#000000"
                        for (let y = 0; y < filas.length; ++y)
                            for (let x = 0; x < filas[y].length; ++x)
                                if (filas[y][x] === "1") ctx.fillRect(off + x * cell, off + y * cell, cell, cell)
                    }
                }
            }
            TextField { id: campoLink; Layout.preferredWidth: 420; readOnly: true; text: credencial.datos.link || ""; font.pixelSize: 11 }
            RowLayout {
                Text { color: Theme.textDim; font.pixelSize: 13; text: "PIN" }
                Text { color: Theme.text; font.pixelSize: 26; font.weight: Font.Black; font.letterSpacing: 4; text: credencial.datos.pin || "" }
                Item { Layout.fillWidth: true }
                GlassButton { text: "Copiar link"; onClicked: { campoLink.selectAll(); campoLink.copy(); Studio.notify("Link copiado", "ok") } }
            }
            Text { visible: !!credencial.datos.aviso; Layout.preferredWidth: 420; wrapMode: Text.WordWrap
                   color: Theme.amber; font.pixelSize: 11; text: credencial.datos.aviso || "" }
        }
    }

    // --- Usuario y contraseña -----------------------------------------------------------
    Dialog {
        id: claveDlg
        anchors.centerIn: Overlay.overlay
        modal: true
        title: "Usuario y contraseña de " + nombre
        property string id: ""
        property string nombre: ""
        property var r: ({})
        onOpened: r = ({})
        standardButtons: Dialog.Close
        ColumnLayout {
            spacing: 10
            Text { Layout.preferredWidth: 400; wrapMode: Text.WordWrap; color: Theme.textDim; font.pixelSize: 12
                   text: "Para entrar sin el link, tipeando en …/sesion/. La contraseña nueva reemplaza a la anterior y se ve una sola vez." }
            RowLayout {
                Text { text: "Usuario"; color: Theme.textDim; font.pixelSize: 12 }
                TextField { id: campoUsuario; Layout.fillWidth: true; placeholderText: "p. ej. banca12 o apellido" }
            }
            GlassButton { text: "Generar contraseña"; active: true
                          onClicked: {
                              const x = cr.s.generarClaveUi(claveDlg.id, campoUsuario.text.trim())
                              if (x.error) { Studio.notify(x.error, "error"); return }
                              claveDlg.r = x
                          } }
            RowLayout {
                visible: !!claveDlg.r.clave
                Text { color: Theme.textDim; font.pixelSize: 13; text: (claveDlg.r.usuario || "") + "  /" }
                TextField { id: campoClave; readOnly: true; text: claveDlg.r.clave || ""; font.pixelSize: 20; font.letterSpacing: 2
                            Layout.preferredWidth: 220 }
                GlassButton { text: "Copiar"; onClicked: { campoClave.selectAll(); campoClave.copy(); Studio.notify("Contraseña copiada", "ok") } }
            }
        }
    }

    // --- Datos de contacto -----------------------------------------------------------------
    Dialog {
        id: datosDlg
        anchors.centerIn: Overlay.overlay
        modal: true
        title: "Datos de " + nombre
        property string id: ""
        property string nombre: ""
        standardButtons: Dialog.Save | Dialog.Cancel
        onAccepted: {
            const e = cr.s.setDatosUi(datosDlg.id, {email: campoEmail.text.trim(), telefono: campoTel.text.trim(), nota: campoNota.text.trim()})
            if (e) Studio.notify(e, "error")
        }
        GridLayout {
            columns: 2
            columnSpacing: 10; rowSpacing: 6
            Text { text: "Email"; color: Theme.textDim; font.pixelSize: 12 }
            TextField { id: campoEmail; Layout.preferredWidth: 300 }
            Text { text: "Teléfono"; color: Theme.textDim; font.pixelSize: 12 }
            TextField { id: campoTel; Layout.preferredWidth: 300 }
            Text { text: "Nota"; color: Theme.textDim; font.pixelSize: 12 }
            TextField { id: campoNota; Layout.preferredWidth: 300; placeholderText: "p. ej. conecta desde la oficina del bloque" }
        }
    }
}
