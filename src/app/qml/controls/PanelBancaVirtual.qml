// La banca virtual (docs/SESION-REMOTA.md): cada integrante entra desde el
// navegador con su link personal y su PIN, en la sala o afuera. Acá se generan
// los links, se ve a los que entraron (su foto de la cámara) y se los habilita:
// habilitados cuentan como presentes. Fuente: Studio.sesionRemota
// (legislatura/SesionRemota); lo mismo por MCP con sesion_*.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import BA

ColumnLayout {
    id: bv
    spacing: 10
    readonly property var s: Studio.sesionRemota
    readonly property var parts: s ? s.participantes : []
    readonly property var esperando: parts.filter(p => p.estado === "esperando")
    property int fotosVersion: 0

    function estadoDe(id) {
        for (const p of bv.parts) if (p.id === id) return p
        return null
    }

    // Las fotos de los que esperan, cada 1,5 s mientras el panel se ve.
    Timer {
        interval: 1500; repeat: true; running: bv.visible && bv.esperando.length > 0
        triggeredOnStart: true
        onTriggered: { for (const p of bv.esperando) bv.s.pedirFoto(p.id) }
    }
    Connections {
        target: bv.s
        function onFotoLista(id) { bv.fotosVersion++ }
    }

    // --- Arriba: cuántos ----------------------------------------------------------
    RowLayout {
        Layout.fillWidth: true
        spacing: 12
        Text { color: Theme.text; font { pixelSize: 14; weight: Font.DemiBold }
               text: "Presentes por la banca virtual: " + (bv.s ? bv.s.presentes : 0) }
        Text { color: Theme.textDim; font.pixelSize: 12; text: "Conectados: " + (bv.s ? bv.s.conectados : 0) }
        Item { Layout.fillWidth: true }
        Text {
            color: bv.s && bv.s.origen.indexOf("https://") === 0 ? Theme.textFaint : Theme.amber
            font.pixelSize: 11
            text: !bv.s || bv.s.origen === "" ? "Sin dirección pública todavía (¿está andando el gateway?)"
                  : bv.s.origen.indexOf("https://") === 0 ? "Links con " + bv.s.origen
                  : "Sin HTTPS el celular no da cámara ni micrófono: " + bv.s.origen
        }
    }

    // --- Los que esperan que los habiliten ----------------------------------------
    Text { visible: bv.esperando.length > 0; color: Theme.amber; font { pixelSize: 13; weight: Font.DemiBold }
           text: "Esperan que los habiliten (miralos por la cámara antes)" }
    Flow {
        Layout.fillWidth: true
        visible: bv.esperando.length > 0
        spacing: 10
        Repeater {
            model: bv.esperando
            delegate: Rectangle {
                required property var modelData
                width: 220; height: 230; radius: 10
                color: Qt.rgba(1, 1, 1, 0.05); border.color: Theme.amber
                ColumnLayout {
                    anchors { fill: parent; margins: 8 }
                    spacing: 4
                    Rectangle {
                        Layout.fillWidth: true; Layout.preferredHeight: 130; radius: 6; color: "black"; clip: true
                        Image {
                            anchors.fill: parent
                            fillMode: Image.PreserveAspectFit
                            cache: false
                            // fotosVersion fuerza a releer la foto nueva.
                            source: bv.fotosVersion >= 0 ? bv.s.fotoUrl(modelData.id) : ""
                        }
                        Text { anchors.centerIn: parent; visible: bv.fotosVersion >= 0 && bv.s.fotoUrl(modelData.id) === ""
                               text: "sin imagen todavía"; color: Theme.textFaint; font.pixelSize: 11 }
                    }
                    Text { Layout.fillWidth: true; elide: Text.ElideRight; color: Theme.text; font { pixelSize: 13; weight: Font.DemiBold }
                           text: modelData.nombre + (modelData.banca ? " · Banca " + modelData.banca : "") }
                    Text { color: Theme.textDim; font.pixelSize: 11
                           text: (modelData.lugar === "sala" ? "desde la sala" : "desde afuera") + " · " + (modelData.ip || "") }
                    RowLayout {
                        GlassButton { text: "Habilitar"; active: true
                                      onClicked: { const e = bv.s.habilitarUi(modelData.id, true); if (e) Studio.notify(e, "error") } }
                        GlassButton { text: "Sacar"
                                      onClicked: ApplicationWindow.window.confirm({ title: "¿Sacar a " + modelData.nombre + " de la sesión?",
                                          text: "Se corta su conexión. Si vuelve a entrar, hay que habilitarlo de nuevo.",
                                          accept: "Sacar", reject: "Cancelar",
                                          action: function() { bv.s.expulsarUi(modelData.id, "") } }) }
                    }
                }
            }
        }
    }

    // --- Todos los integrantes: link, estado ----------------------------------------
    Text { color: Theme.text; font.pixelSize: 13; font.weight: Font.DemiBold; text: "Integrantes" }
    ListView {
        Layout.fillWidth: true
        Layout.fillHeight: true
        clip: true
        spacing: 3
        model: Studio.cuerpo.integrantes
        delegate: RowLayout {
            required property var modelData
            readonly property var est: bv.parts.length >= 0 ? bv.estadoDe(modelData.id) : null
            width: ListView.view.width
            spacing: 10
            Text { Layout.preferredWidth: 60; color: Theme.textDim; font.pixelSize: 12; text: modelData.banca ? "Banca " + modelData.banca : "" }
            Text { Layout.preferredWidth: 220; elide: Text.ElideRight; color: Theme.text; font.pixelSize: 12; text: modelData.nombre }
            Text {
                Layout.preferredWidth: 170
                font.pixelSize: 12
                color: !est ? Theme.textFaint : est.estado === "habilitado" ? Theme.ok : est.estado === "esperando" ? Theme.amber : Theme.textDim
                text: !est ? "sin link"
                      : est.bloqueado ? "bloqueado (PIN errado)"
                      : est.estado === "habilitado" ? "presente (" + (est.lugar === "sala" ? "sala" : "afuera") + ")"
                      : est.estado === "esperando" ? "esperando"
                      : est.credencial ? "con link, sin conectar" : "sin link"
            }
            GlassButton { compact: true; text: est && est.credencial ? "Nuevo link…" : "Link y PIN…"
                          onClicked: {
                              const generar = function() {
                                  const r = bv.s.generarUi(modelData.id)
                                  if (r.error) { Studio.notify(r.error, "error"); return }
                                  credencial.mostrar(r)
                              }
                              if (est && est.credencial)
                                  ApplicationWindow.window.confirm({ title: "¿Generar un link nuevo?",
                                      text: "El link y el PIN anteriores de " + modelData.nombre + " dejan de andar.",
                                      accept: "Generar", reject: "Cancelar", action: generar })
                              else generar()
                          } }
            GlassButton { compact: true; text: "Deshabilitar"; visible: !!est && est.estado === "habilitado"
                          onClicked: bv.s.habilitarUi(modelData.id, false) }
            GlassButton { compact: true; text: "Revocar"; visible: !!est && est.credencial
                          onClicked: ApplicationWindow.window.confirm({ title: "¿Revocar el link de " + modelData.nombre + "?",
                              text: "Deja de andar ya (si está conectado, se corta).", accept: "Revocar", reject: "Cancelar",
                              action: function() { bv.s.revocarUi(modelData.id) } }) }
            Item { Layout.fillWidth: true }
        }
    }

    // --- El link y el PIN, una sola vez ----------------------------------------------
    Dialog {
        id: credencial
        anchors.centerIn: Overlay.overlay
        modal: true
        standardButtons: Dialog.Close
        title: "Banca virtual de " + (datos.nombre || "")
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
            TextField { id: campoLink; Layout.preferredWidth: 420; readOnly: true; text: credencial.datos.link || ""
                        font.pixelSize: 11 }
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
}
