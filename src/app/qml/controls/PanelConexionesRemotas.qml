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
               text: qsTr("Conectados: %1 · presentes: %2").arg(cr.s ? cr.s.conectados : 0).arg(cr.s ? cr.s.presentes : 0) }
        GlassButton { visible: cr.esperando > 0; active: true
                      text: qsTr("%n espera que lo habiliten → Recinto", "", cr.esperando)
                      onClicked: cr.irAlRecinto() }
        Item { Layout.fillWidth: true }
        Text {
            color: cr.s && cr.s.origen.indexOf("https://") === 0 ? Theme.textFaint : Theme.amber
            font.pixelSize: 11
            text: !cr.s || cr.s.origen === "" ? qsTr("Sin dirección pública todavía (¿túnel o red local con HTTPS?)")
                  : cr.s.origen.indexOf("https://") === 0 ? qsTr("Entran por %1/sesion/").arg(cr.s.origen)
                  : qsTr("Sin HTTPS el celular no da cámara ni micrófono: %1").arg(cr.s.origen)
        }
    }
    Text {
        Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.textDim; font.pixelSize: 12
        text: qsTr("Cada uno entra con su link personal y su PIN (o con usuario y contraseña, tipeando). Sirve igual desde la sala "
                   + "(el celular de respaldo si la central no anda) que desde afuera. El link, el PIN y la contraseña se ven una sola vez.")
    }

    // --- La atención a los que esperan (la misma que en la Sala) ----------------------------
    Seccion {
        titulo: qsTr("Los que esperan que los habiliten")
        subtitulo: qsTr("Que el asistente los atienda: les mira la cara contra su ficha y habilita solo a quien tiene permiso de entrar solo en el padrón del evento; al resto te lo pregunta.")
        OpcionesAtencion { Layout.fillWidth: true; ambito: "asamblea" }
    }

    // --- Consolas de la presidencia (la página web /presidencia/) ---------------------------
    Seccion {
        titulo: qsTr("Consola de la presidencia")
        subtitulo: qsTr("Una página web para quien preside (tablet o compu): da la palabra, ve quién la pidió, maneja la votación y el debate. Cada acceso es un link propio que se puede revocar.")
        extra: GlassButton { compact: true; text: qsTr("Nuevo acceso…"); active: cr.s && cr.s.consolasUi.length === 0
                             onClicked: { nombreConsola.text = qsTr("Presidencia"); nuevaConsola.open() } }
        Flow {
            Layout.fillWidth: true
            spacing: 8
            visible: !!cr.s && cr.s.consolasUi.length > 0
            Repeater {
                model: cr.s ? cr.s.consolasUi : []
                delegate: Rectangle {
                    required property var modelData
                    implicitWidth: filaC.implicitWidth + 20; implicitHeight: 30; radius: 15
                    color: Qt.rgba(1, 1, 1, 0.05); border.color: modelData.conectada ? Theme.ok : Qt.rgba(1, 1, 1, 0.12)
                    RowLayout {
                        id: filaC
                        anchors.centerIn: parent
                        spacing: 6
                        Rectangle { implicitWidth: 8; implicitHeight: 8; radius: 4; color: modelData.conectada ? Theme.ok : Theme.textFaint }
                        Text { text: modelData.nombre; color: modelData.conectada ? Theme.text : Theme.textDim; font.pixelSize: 12 }
                        Text { visible: modelData.creado_ms > 0; color: Theme.textFaint; font.pixelSize: 11
                               text: modelData.conectada ? qsTr("· en uso") : qsTr("· desde el %1").arg(Qt.formatDate(new Date(modelData.creado_ms), "d/M")) }
                        Text { text: "✕"; color: Theme.textFaint; font.pixelSize: 12
                               ToolTip.visible: mRev.containsMouse; ToolTip.delay: 400; ToolTip.text: qsTr("Revocar este acceso")
                               MouseArea { id: mRev; anchors.fill: parent; anchors.margins: -4; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                                           onClicked: ApplicationWindow.window.confirm({ title: qsTr("¿Revocar el acceso de «%1»?").arg(modelData.nombre),
                                               text: qsTr("La consola deja de andar ya."), accept: qsTr("Revocar"), reject: qsTr("Cancelar"),
                                               action: function() { cr.s.revocarConsolaUi(modelData.id) } }) } }
                    }
                }
            }
        }
        Text {
            visible: !cr.s || cr.s.consolasUi.length === 0
            color: Theme.textFaint; font.pixelSize: 12
            text: qsTr("Todavía no hay ningún acceso. Tocá «Nuevo acceso…» y pasale el link (o el QR) a quien preside.")
        }
    }
    Dialog {
        id: nuevaConsola
        anchors.centerIn: Overlay.overlay
        modal: true
        title: qsTr("Nuevo acceso a la consola")
        standardButtons: Dialog.Ok | Dialog.Cancel
        onAccepted: {
            const r = cr.s.generarConsolaUi(nombreConsola.text.trim() || qsTr("Presidencia"))
            if (r.error) Studio.notify(r.error, "error"); else credencial.mostrar(r)
        }
        ColumnLayout {
            spacing: 8
            Text { Layout.preferredWidth: 380; wrapMode: Text.WordWrap; color: Theme.textDim; font.pixelSize: 12
                   text: qsTr("Un nombre para reconocerlo en la lista (p. ej. «Presidencia», «Vice», «Secretaría»). El link se muestra una sola vez.") }
            TextField { id: nombreConsola; Layout.fillWidth: true; placeholderText: qsTr("Presidencia"); onAccepted: nuevaConsola.accept() }
        }
    }

    // --- Lo que ven en el celular: su cámara (siempre, en un recuadro) y hasta dos canales ---
    Seccion {
        titulo: qsTr("Qué ven en el celular")
        subtitulo: qsTr("Cada uno elige en su celular qué mirar: su cámara, la salida principal u otra escena; su propia imagen queda siempre en un recuadro. Las escenas usan los canales de las pantallas de la sala (4 entre todas).")
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
        readonly property var opciones: ["—", qsTr("Salida principal")].concat(vistasFila.escenas)
        function guardar() {
            const l = []
            for (const f of [vA, vB]) {
                if (f.combo.currentIndex <= 0) continue
                l.push({ nombre: f.campo.text.trim(), fuente: f.combo.currentIndex === 1 ? "programa" : f.combo.currentText })
            }
            const e = cr.s.setVistasUi(l)
            if (e !== "") Studio.notify(e, "error")
        }
        Text { color: Theme.textDim; font.pixelSize: 12; text: qsTr("Su cámara +") }
        component CanalVista: RowLayout {
            property int indice: 0
            property alias campo: nombreV
            property alias combo: fuenteV
            readonly property var v: cr.s && cr.s.vistasUi.length > indice ? cr.s.vistasUi[indice] : null
            spacing: 4
            TextField { id: nombreV; Layout.preferredWidth: 130; color: Theme.text; placeholderText: qsTr("Nombre del canal")
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
        Text { color: Theme.textDim; font.pixelSize: 12; text: qsTr("y", "entre dos opciones") }
        CanalVista { id: vB; indice: 1 }
        Item { Layout.fillWidth: true }
    }
    }

    // --- Cómo es el acceso: PIN en el QR y acceso fijo ------------------------------------
    Seccion {
        titulo: qsTr("Cómo entran los integrantes")
        subtitulo: qsTr("Cada integrante entra desde el navegador con su link y su PIN.")
        GridLayout {
            Layout.fillWidth: true
            columns: 3
            columnSpacing: 10
            rowSpacing: 8
            Toggle { checked: !!cr.s && cr.s.pinEnQr; onToggled: (w) => cr.s.pinEnQr = w }
            Text { text: qsTr("PIN incluido en el QR"); color: Theme.text; font.pixelSize: 12 }
            Text { Layout.fillWidth: true; wrapMode: Text.WordWrap; color: cr.s && cr.s.pinEnQr ? Theme.amber : Theme.textFaint; font.pixelSize: 11
                   text: cr.s && cr.s.pinEnQr
                         ? qsTr("Escanea y entra sin tipear nada. Ojo: el QR (o el link con PIN) solo ya alcanza para entrar; tratalo como una llave.")
                         : qsTr("El QR lleva solo el link y el PIN se pasa aparte (más seguro).") }
            Toggle { checked: !!cr.s && cr.s.accesoFijo
                     onToggled: (w) => {
                         if (w) { cr.s.accesoFijo = true; return }
                         ApplicationWindow.window.confirm({ title: qsTr("¿Dejar de guardar los accesos?"),
                             text: qsTr("Los links que ya tienen siguen andando, pero no se van a poder volver a mostrar: para verlos habrá que renovarlos."),
                             accept: qsTr("Dejar de guardar"), reject: qsTr("Cancelar"), action: function() { cr.s.accesoFijo = false } })
                     } }
            Text { text: qsTr("Link y PIN fijos"); color: Theme.text; font.pixelSize: 12 }
            Text { Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.textFaint; font.pixelSize: 11
                   text: cr.s && cr.s.accesoFijo
                         ? qsTr("Cada integrante tiene siempre el mismo link y el mismo PIN (quedan guardados cifrados en este equipo). «Ver acceso…» los vuelve a mostrar; solo cambian con «Renovar…».")
                           + (cr.parts.some(p => p.credencial && !p.fijo) ? " " + qsTr("Los que ya tenían acceso de antes necesitan renovarlo una vez para que quede guardado.") : "")
                         : qsTr("Cada vez que se da acceso se genera un link y un PIN nuevos, y se muestran una sola vez.") }
        }
    }

    // --- Encabezado de la tabla ---------------------------------------------------------
    RowLayout {
        Layout.fillWidth: true
        Layout.topMargin: 4
        Text { text: qsTr("El acceso de cada integrante"); color: Theme.text; font { pixelSize: 13; weight: Font.DemiBold } }
        Text { visible: Studio.cuerpo.integrantes.length > 0; color: Theme.textFaint; font.pixelSize: 12
               text: qsTr("· %1 de %2 con acceso").arg(cr.parts.filter(p => p.credencial).length).arg(Studio.cuerpo.integrantes.length) }
        Item { Layout.fillWidth: true }
    }
    RowLayout {
        Layout.fillWidth: true
        Layout.leftMargin: 8
        visible: Studio.cuerpo.integrantes.length > 0
        spacing: 10
        Text { Layout.preferredWidth: 60; text: qsTr("Banca"); color: Theme.textFaint; font.pixelSize: 11 }
        Text { Layout.preferredWidth: 200; text: qsTr("Conferencista"); color: Theme.textFaint; font.pixelSize: 11 }
        Text { Layout.preferredWidth: 110; text: qsTr("Usuario"); color: Theme.textFaint; font.pixelSize: 11 }
        Text { Layout.preferredWidth: 130; text: qsTr("Acceso"); color: Theme.textFaint; font.pixelSize: 11 }
        Text { Layout.preferredWidth: 170; text: qsTr("Estado"); color: Theme.textFaint; font.pixelSize: 11 }
        Item { Layout.fillWidth: true }
    }
    ListView {
        Layout.fillWidth: true
        Layout.fillHeight: true
        clip: true
        spacing: 3
        model: Studio.cuerpo.integrantes
        EstadoVacio {
            anchors { top: parent.top; topMargin: 30; horizontalCenter: parent.horizontalCenter }
            width: parent.width
            visible: parent.count === 0
            icono: "▦"
            titulo: qsTr("Todavía no hay integrantes")
            texto: qsTr("Cada integrante del cuerpo puede entrar desde el navegador (en la sala con el celular, o desde afuera) con su link y su PIN. "
                        + "Primero cargá los integrantes; después, acá, «Dar acceso…» a cada uno.")
            accion: qsTr("Cargar integrantes…")
            onActivado: cuerpoCr.open()
        }
        delegate: Rectangle {
          required property var modelData
          required property int index
          readonly property var est: cr.parts.length >= 0 ? cr.estadoDe(modelData.id) : null
          width: ListView.view.width
          implicitHeight: filaI.implicitHeight + 8
          radius: 6
          color: index % 2 ? Qt.rgba(1, 1, 1, 0.025) : "transparent"
          RowLayout {
            id: filaI
            anchors { left: parent.left; right: parent.right; verticalCenter: parent.verticalCenter; leftMargin: 8 }
            spacing: 10
            Text { Layout.preferredWidth: 60; color: Theme.textDim; font.pixelSize: 12; text: modelData.banca ? modelData.banca : "" }
            Text { Layout.preferredWidth: 200; elide: Text.ElideRight; color: Theme.text; font.pixelSize: 12; text: modelData.nombre }
            Text { Layout.preferredWidth: 110; elide: Text.ElideRight; font.pixelSize: 12
                   color: est && est.usuario ? Theme.text : Theme.textFaint; text: est && est.usuario ? est.usuario : "—" }
            Text { Layout.preferredWidth: 130; font.pixelSize: 11
                   color: est && est.credencial ? Theme.textDim : Theme.textFaint
                   text: !est || !est.credencial ? qsTr("sin acceso") : est.con_clave ? qsTr("link + PIN · contraseña") : qsTr("link + PIN") }
            Text {
                Layout.preferredWidth: 170
                font.pixelSize: 12
                color: !est ? Theme.textFaint : est.bloqueado ? Theme.live : est.estado === "habilitado" ? Theme.ok
                       : est.estado === "esperando" ? Theme.amber : Theme.textDim
                text: !est || !est.credencial ? ""
                      : est.bloqueado ? qsTr("bloqueado (clave errada)")
                      : est.estado === "habilitado" ? (est.lugar === "sala" ? qsTr("presente (sala)") : qsTr("presente (afuera)"))
                      : est.estado === "esperando" ? qsTr("esperando (Recinto)")
                      : qsTr("sin conectar")
            }
            // Fijo y guardado: se ve el mismo cuando se quiera; renovar es aparte y se confirma.
            GlassButton { compact: true; visible: !!est && est.credencial && est.fijo; text: qsTr("Ver acceso…"); active: true
                          onClicked: { const r = cr.s.generarUi(modelData.id, false)
                                       if (r.error) Studio.notify(r.error, "error"); else credencial.mostrar(r) } }
            GlassButton { compact: true
                          text: !(est && est.credencial) ? qsTr("Dar acceso…") : est.fijo ? qsTr("Renovar…") : cr.s.accesoFijo ? qsTr("Renovar (queda fijo)…") : qsTr("Nuevo link…")
                          onClicked: {
                              const generar = function() {
                                  const r = cr.s.generarUi(modelData.id, true)
                                  if (r.error) { Studio.notify(r.error, "error"); return }
                                  credencial.mostrar(r)
                              }
                              if (est && est.credencial)
                                  ApplicationWindow.window.confirm({ title: qsTr("¿Generar un link y un PIN nuevos?"),
                                      text: (est.conectado ? qsTr("El link, el PIN y la contraseña anteriores de %1 dejan de andar (y si está conectado, se corta).")
                                                            : qsTr("El link, el PIN y la contraseña anteriores de %1 dejan de andar.")).arg(modelData.nombre),
                                      accept: qsTr("Renovar"), reject: qsTr("Cancelar"), action: generar })
                              else generar()
                          } }
            GlassButton { compact: true; text: qsTr("Usuario y contraseña…"); visible: !!est && est.credencial
                          onClicked: { claveDlg.id = modelData.id; claveDlg.nombre = modelData.nombre
                                       campoUsuario.text = est.usuario || ""; claveDlg.open() } }
            GlassButton { compact: true; text: qsTr("Datos…"); visible: !!est && est.credencial
                          onClicked: { datosDlg.id = modelData.id; datosDlg.nombre = modelData.nombre
                                       const d = est.datos || {}
                                       campoEmail.text = d.email || ""; campoTel.text = d.telefono || ""; campoNota.text = d.nota || ""
                                       datosDlg.open() } }
            GlassButton { compact: true; text: qsTr("Revocar"); visible: !!est && est.credencial
                          onClicked: ApplicationWindow.window.confirm({ title: qsTr("¿Revocar el acceso de %1?").arg(modelData.nombre),
                              text: qsTr("El link y la contraseña dejan de andar ya (si está conectado, se corta)."), accept: qsTr("Revocar"), reject: qsTr("Cancelar"),
                              action: function() { cr.s.revocarUi(modelData.id) } }) }
            Item { Layout.fillWidth: true }
          }
        }
    }
    PanelCuerpo { id: cuerpoCr }

    // --- El link y el PIN, una sola vez ----------------------------------------------
    Dialog {
        id: credencial
        anchors.centerIn: Overlay.overlay
        modal: true
        standardButtons: Dialog.Close
        title: qsTr("Acceso de %1").arg(datos.nombre || "")
        property var datos: ({})
        function mostrar(r) { datos = r; open(); lienzo.requestPaint() }
        ColumnLayout {
            spacing: 10
            Text { Layout.preferredWidth: 420; wrapMode: Text.WordWrap; font.pixelSize: 12
                   color: credencial.datos.fijo ? Theme.textDim : Theme.amber
                   text: (credencial.datos.fijo
                          ? (credencial.datos.repetido ? qsTr("Es el mismo de siempre (acceso fijo): no cambió nada. ") : qsTr("Queda fijo: se puede volver a ver con «Ver acceso…». "))
                          : qsTr("Se muestra UNA sola vez. "))
                         + (credencial.datos.pin_en_qr
                            ? qsTr("El QR ya trae el PIN: con escanearlo entra. Pasáselo solo a esa persona.")
                            : qsTr("Pasale el link (o el QR) y el PIN por un canal seguro, por separado si se puede.")) }
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
            Text { visible: !!credencial.datos.link_remoto; color: Theme.textDim; font.pixelSize: 11
                   text: qsTr("En la red de la sala (el QR es este):") }
            TextField { id: campoLink; Layout.preferredWidth: 420; readOnly: true; text: credencial.datos.link || ""; font.pixelSize: 11 }
            // Desde afuera (por el túnel), aparte: para quien no está en la sala.
            Text { visible: !!credencial.datos.link_remoto; color: Theme.textDim; font.pixelSize: 11
                   text: qsTr("Desde afuera (por internet):") }
            RowLayout {
                visible: !!credencial.datos.link_remoto
                TextField { Layout.preferredWidth: 330; readOnly: true; text: credencial.datos.link_remoto || ""; font.pixelSize: 11 }
                GlassButton { text: qsTr("Copiar"); compact: true
                              onClicked: { Studio.copiar(credencial.datos.link_remoto); Studio.notify(qsTr("Link de afuera copiado"), "ok") } }
            }
            RowLayout {
                Text { color: Theme.textDim; font.pixelSize: 13; text: "PIN" }
                Text { color: Theme.text; font.pixelSize: 26; font.weight: Font.Black; font.letterSpacing: 4; text: credencial.datos.pin || "" }
                Item { Layout.fillWidth: true }
                GlassButton { text: qsTr("Copiar link"); onClicked: { campoLink.selectAll(); campoLink.copy(); Studio.notify(qsTr("Link copiado"), "ok") } }
                GlassButton { visible: !!credencial.datos.link_con_pin; text: qsTr("Copiar con PIN")
                              onClicked: { Studio.copiar(credencial.datos.link_con_pin); Studio.notify(qsTr("Link con PIN copiado"), "ok") } }
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
        title: qsTr("Usuario y contraseña de %1").arg(nombre)
        property string id: ""
        property string nombre: ""
        property var r: ({})
        onOpened: r = ({})
        standardButtons: Dialog.Close
        ColumnLayout {
            spacing: 10
            Text { Layout.preferredWidth: 400; wrapMode: Text.WordWrap; color: Theme.textDim; font.pixelSize: 12
                   text: qsTr("Para entrar sin el link, tipeando en …/sesion/. La contraseña nueva reemplaza a la anterior y se ve una sola vez.") }
            RowLayout {
                Text { text: qsTr("Usuario"); color: Theme.textDim; font.pixelSize: 12 }
                TextField { id: campoUsuario; Layout.fillWidth: true; placeholderText: qsTr("p. ej. banca12 o apellido") }
            }
            GlassButton { text: qsTr("Generar contraseña"); active: true
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
                GlassButton { text: qsTr("Copiar"); onClicked: { campoClave.selectAll(); campoClave.copy(); Studio.notify(qsTr("Contraseña copiada"), "ok") } }
            }
        }
    }

    // --- Datos de contacto -----------------------------------------------------------------
    Dialog {
        id: datosDlg
        anchors.centerIn: Overlay.overlay
        modal: true
        title: qsTr("Datos de %1").arg(nombre)
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
            Text { text: qsTr("Email"); color: Theme.textDim; font.pixelSize: 12 }
            TextField { id: campoEmail; Layout.preferredWidth: 300 }
            Text { text: qsTr("Teléfono"); color: Theme.textDim; font.pixelSize: 12 }
            TextField { id: campoTel; Layout.preferredWidth: 300 }
            Text { text: qsTr("Nota"); color: Theme.textDim; font.pixelSize: 12 }
            TextField { id: campoNota; Layout.preferredWidth: 300; placeholderText: qsTr("p. ej. conecta desde la oficina del bloque") }
        }
    }
}
