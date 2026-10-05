// La central de conferencia del recinto (Bosch DCN): micrófonos de banca,
// pedidos de palabra y oradores. Fuente: Studio.dcn (legislatura/ConferenciaDcn),
// que le pide todo al plugin bosch-dcn; las respuestas llegan por `hecho` y
// nada espera en la interfaz. Guía: docs/bosch-dcn-ng-open-interface.md §10.
//
// Arriba el enlace, el modo y los botones de todos los días (Siguiente, Cerrar
// todos). En el medio la grilla del Recinto: un toque abre o cierra el
// micrófono de esa banca. A la derecha quién pidió la palabra y quién habla.
// Las pestañas Unidades y Conexión son para armar la sala una vez.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import BA

ColumnLayout {
    id: pc
    spacing: 10
    readonly property var dcn: Studio.dcn
    readonly property var e: dcn ? dcn.estado : ({})
    readonly property bool ok: !!(dcn && dcn.conectada)
    readonly property string plugin: dcn ? dcn.plugin : "no_instalado"
    property var puertos: []
    property var tramas: []
    property int pestana: 0

    function pedir(que, control, args) { pc.dcn.pedirUi(que, control, args || {}) }
    Connections {
        target: pc.dcn
        function onHecho(que, ok, r, error) {
            if (!ok) { Studio.notify(error, "error"); return }
            if (que === "puertos") pc.puertos = r.puertos || []
            else if (que === "traza") pc.tramas = r.tramas || []
            else if (que === "conectar") Studio.notify("Central: " + (r.enlace === "ok" ? "conectada" : (r.motivo || r.enlace)), "info")
        }
    }

    // --- Barra: enlace, modo y lo de todos los días ------------------------------------
    RowLayout {
        Layout.fillWidth: true
        spacing: 10
        Rectangle { implicitWidth: 12; implicitHeight: 12; radius: 6
                    color: pc.ok ? Theme.ok : (pc.e.enlace === "sincronizando" ? Theme.amber : Theme.live) }
        Text {
            text: pc.ok ? "Central conectada" + (pc.e.ccu && pc.e.ccu.version ? " · " + pc.e.ccu.version : "")
                  : pc.plugin === "no_instalado" ? "Sin configurar: elegí el puerto en Conexión"
                  : pc.plugin !== "corriendo" ? "El control de la central está apagado"
                  : "Sin control de la central: " + (pc.e.motivo || "…")
            color: pc.ok ? Theme.text : Theme.live; font { pixelSize: 13; weight: Font.DemiBold }
        }
        Text { visible: pc.ok && pc.e.solo_escuchar; text: "· solo escuchar"; color: Theme.amber; font.pixelSize: 12 }
        Item { Layout.fillWidth: true }
        Text { visible: pc.ok; color: Theme.textDim; font.pixelSize: 12
               text: "Abiertos " + (pc.e.abiertos || []).length + (pc.e.max_abiertos ? " de " + pc.e.max_abiertos : "") }
        ComboBox {
            visible: pc.ok
            implicitWidth: 190
            model: [ { t: "Operador (la app da la palabra)", v: "operador" }, { t: "Operador con réplicas", v: "operador_replicas" },
                     { t: "Delegado con lista", v: "delegado_lista" }, { t: "Delegado con reemplazo", v: "reemplazo" },
                     { t: "Por voz", v: "voz" }, { t: "Pulsar para hablar", v: "pulsar" } ]
            textRole: "t"
            currentIndex: Math.max(0, model.findIndex(o => o.v === pc.e.modo))
            onActivated: (i) => {
                const v = model[i].v
                ApplicationWindow.window.confirm({ title: "¿Cambiar el modo de la central?",
                    text: "Cambia cómo se abren los micrófonos de toda la sala.", accept: "Cambiar", reject: "Cancelar",
                    action: function() { pc.pedir("modo", "modo", {modo: v}) } })
            }
            ToolTip.visible: hovered; ToolTip.delay: 400; ToolTip.text: "Modo de operación de la central"
        }
        GlassButton { text: "Siguiente ▶"; active: true; enabled: pc.ok && (pc.e.pedidos || []).length > 0
                      onClicked: pc.pedir("siguiente", "siguiente")
                      ToolTip.visible: hovered; ToolTip.delay: 400; ToolTip.text: "Le da la palabra al primero de la lista" }
        GlassButton { text: "Cerrar todos"; enabled: pc.ok && (pc.e.abiertos || []).length > 0
                      onClicked: ApplicationWindow.window.confirm({ title: "¿Cerrar todos los micrófonos?",
                          text: "Menos el del presidente.", accept: "Cerrar", reject: "Cancelar",
                          action: function() { pc.pedir("cerrar", "cerrar_todos") } }) }
    }
    Text {
        Layout.fillWidth: true; wrapMode: Text.WordWrap; font.pixelSize: 12
        visible: pc.ok && pc.e.modo_operador === false
        color: Theme.amber
        text: "La central no está en modo operador: los micrófonos los decide ella (o los delegados). Desde acá se abre y cierra solo lo que la central acepte."
    }

    TabBar {
        id: tabs
        Layout.fillWidth: true
        currentIndex: pc.pestana
        onCurrentIndexChanged: pc.pestana = currentIndex
        TabButton { text: "Sala" }
        TabButton { text: "Unidades" + ((pc.e.bancas_sin_unidad || []).length ? " (" + pc.e.bancas_sin_unidad.length + " sin unidad)" : "") }
        TabButton { text: "Conexión" }
    }

    StackLayout {
        Layout.fillWidth: true
        Layout.fillHeight: true
        currentIndex: tabs.currentIndex

        // --- Sala: la grilla y las dos listas ------------------------------------------
        RowLayout {
            spacing: 12
            ScrollView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                contentWidth: availableWidth
                clip: true
                RecintoGrilla { width: parent.width; tocarAbreMicrofono: true }
            }
            ColumnLayout {
                Layout.preferredWidth: 300
                Layout.fillHeight: true
                spacing: 8
                Text { text: "Pidieron la palabra"; color: Theme.text; font { pixelSize: 13; weight: Font.DemiBold } }
                ListView {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.minimumHeight: 120
                    clip: true
                    spacing: 4
                    model: pc.e.pedidos || []
                    Text { anchors.centerIn: parent; visible: parent.count === 0; text: "nadie"; color: Theme.textFaint; font.pixelSize: 12 }
                    delegate: Rectangle {
                        required property var modelData
                        required property int index
                        width: ListView.view.width; height: 40; radius: 8
                        color: index === 0 ? Qt.rgba(0.9, 0.2, 0.2, 0.18) : Qt.rgba(1, 1, 1, 0.05)
                        RowLayout {
                            anchors { fill: parent; leftMargin: 8; rightMargin: 4 }
                            Text { text: modelData.turno; color: Theme.amber; font { pixelSize: 14; weight: Font.Black } }
                            Text { Layout.fillWidth: true; elide: Text.ElideRight; color: Theme.text; font.pixelSize: 12
                                   text: (modelData.banca ? "Banca " + modelData.banca : "unidad " + modelData.unidad) + (modelData.nombre ? " · " + modelData.nombre : "") }
                            GlassButton { compact: true; text: "▲"; visible: index > 0
                                          onClicked: pc.pedir("pedido", "pedido", {banca: modelData.banca, unidad: modelData.unidad, accion: "primero"}) }
                            GlassButton { compact: true; text: "Dar"; active: true
                                          onClicked: pc.pedir("mic", "microfono", {banca: modelData.banca, unidad: modelData.unidad, abierto: true}) }
                            GlassButton { compact: true; text: "✕"
                                          onClicked: pc.pedir("pedido", "pedido", {banca: modelData.banca, unidad: modelData.unidad, accion: "quitar"}) }
                        }
                    }
                }
                Text { text: "Hablando"; color: Theme.text; font { pixelSize: 13; weight: Font.DemiBold } }
                ListView {
                    Layout.fillWidth: true
                    Layout.preferredHeight: Math.max(60, Math.min(220, count * 44))
                    clip: true
                    spacing: 4
                    model: pc.e.hablando || []
                    Text { anchors.centerIn: parent; visible: parent.count === 0; text: "nadie"; color: Theme.textFaint; font.pixelSize: 12 }
                    delegate: Rectangle {
                        required property var modelData
                        width: ListView.view.width; height: 40; radius: 8
                        color: modelData.microfono ? Qt.rgba(0.78, 0.16, 0.16, 0.6) : Qt.rgba(1, 1, 1, 0.05)
                        RowLayout {
                            anchors { fill: parent; leftMargin: 8; rightMargin: 4 }
                            Text { text: modelData.microfono ? "●" : "○"; color: "white"; font.pixelSize: 13 }
                            Text { Layout.fillWidth: true; elide: Text.ElideRight; color: Theme.text; font.pixelSize: 12
                                   text: (modelData.banca ? "Banca " + modelData.banca : "unidad " + modelData.unidad) + (modelData.nombre ? " · " + modelData.nombre : "") }
                            GlassButton { compact: true; text: modelData.microfono ? "Cerrar" : "Abrir"
                                          onClicked: pc.pedir("mic", "microfono", {banca: modelData.banca, unidad: modelData.unidad, abierto: !modelData.microfono, confirmar: true}) }
                            GlassButton { compact: true; text: "Terminar"
                                          onClicked: pc.pedir("mic", "microfono", {banca: modelData.banca, unidad: modelData.unidad, abierto: false, quitar: true, confirmar: true}) }
                        }
                    }
                }
            }
        }

        // --- Unidades: qué unidad de la central es cada banca --------------------------------
        ColumnLayout {
            spacing: 8
            Text {
                Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.textDim; font.pixelSize: 12
                text: "La central habla de unidades; la app, de bancas. «Aprender todas» va banca por banca: alguien aprieta el botón de micrófono de la banca que se muestra y queda asignada (se guarda en el Cuerpo)."
            }
            RowLayout {
                spacing: 8
                GlassButton { text: pc.e.aprendiendo ? "Dejar de aprender" : "Aprender todas"; active: !!pc.e.aprendiendo; enabled: pc.ok
                              onClicked: pc.pedir("aprender", "unidad_asignar", pc.e.aprendiendo ? {cancelar: true} : {aprender_todas: true}) }
                Text { visible: !!pc.e.aprendiendo; color: Theme.amber; font { pixelSize: 15; weight: Font.Black }
                       text: "Apretá el botón de la banca " + (pc.e.aprendiendo || "") }
                Item { Layout.fillWidth: true }
                GlassButton { text: "Cuerpo…"; onClicked: panelCuerpo.open() }
            }
            Text { visible: (pc.e.unidades_sin_banca || []).length > 0; color: Theme.textFaint; font.pixelSize: 12
                   text: "Unidades conectadas sin banca: " + (pc.e.unidades_sin_banca || []).join(", ") }
            ListView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                spacing: 3
                model: pc.e.unidades || []
                delegate: RowLayout {
                    required property var modelData
                    width: ListView.view.width
                    spacing: 10
                    Text { text: "Unidad " + modelData.unidad; color: Theme.text; font.pixelSize: 12; Layout.preferredWidth: 90 }
                    Text { text: modelData.tipo; color: modelData.tipo === "presidente" ? "#d4af37" : Theme.textDim; font.pixelSize: 12; Layout.preferredWidth: 80 }
                    Text { text: modelData.conectada ? "conectada" : "desconectada"; color: modelData.conectada ? Theme.ok : Theme.live; font.pixelSize: 12; Layout.preferredWidth: 90 }
                    TextField { id: campoBanca; Layout.preferredWidth: 90; placeholderText: "banca"; text: modelData.banca || "" }
                    GlassButton { compact: true; text: "Asignar"; enabled: campoBanca.text.trim() !== "" && campoBanca.text !== (modelData.banca || "")
                                  onClicked: pc.pedir("asignar", "unidad_asignar", {banca: campoBanca.text.trim(), unidad: modelData.unidad}) }
                    Item { Layout.fillWidth: true }
                }
            }
        }

        // --- Conexión ----------------------------------------------------------------
        ColumnLayout {
            id: conexion
            spacing: 8
            readonly property var cfg: pc.dcn ? pc.dcn.config : ({})
            GridLayout {
                columns: 2
                columnSpacing: 10; rowSpacing: 6
                Text { text: "Cómo"; color: Theme.textDim; font.pixelSize: 12 }
                ComboBox {
                    id: transp
                    implicitWidth: 260
                    model: [ { t: "Puerto serie (RS-232, adaptador USB)", v: "serie" }, { t: "Red (CCU2, TCP 9451)", v: "tcp" },
                             { t: "Simulador (sin el equipo)", v: "simulador" } ]
                    textRole: "t"
                    currentIndex: Math.max(0, model.findIndex(o => o.v === (conexion.cfg.transporte || "serie")))
                }
                Text { visible: transp.currentIndex === 0; text: "Puerto"; color: Theme.textDim; font.pixelSize: 12 }
                RowLayout {
                    visible: transp.currentIndex === 0
                    ComboBox {
                        id: puerto
                        implicitWidth: 260
                        editable: true
                        model: pc.puertos.map(p => p.puerto)
                        editText: conexion.cfg.puerto || ""
                    }
                    GlassButton { compact: true; text: "Buscar"; onClicked: pc.pedir("puertos", "puertos") }
                }
                Text { visible: transp.currentIndex === 0; text: "Velocidad"; color: Theme.textDim; font.pixelSize: 12 }
                ComboBox {
                    id: velocidad
                    visible: transp.currentIndex === 0
                    model: [9600, 19200, 57600, 115200]
                    currentIndex: Math.max(0, model.indexOf(conexion.cfg.velocidad || 19200))
                }
                Text { visible: transp.currentIndex === 1; text: "Dirección"; color: Theme.textDim; font.pixelSize: 12 }
                TextField { id: host; visible: transp.currentIndex === 1; implicitWidth: 260; placeholderText: "192.168.0.10"
                            text: conexion.cfg.host || "" }
                Text { text: "Solo escuchar"; color: Theme.textDim; font.pixelSize: 12 }
                RowLayout {
                    Switch { id: soloEsc; checked: conexion.cfg.solo_escuchar !== false }
                    Text { color: Theme.textFaint; font.pixelSize: 11; Layout.preferredWidth: 360; wrapMode: Text.WordWrap
                           text: "Recibe todo pero no le manda órdenes a la central. Para la primera prueba en la sala; apagalo cuando responda bien." }
                }
                Text { text: "Escape (avanzado)"; color: Theme.textDim; font.pixelSize: 12 }
                TextField { id: campoEscape; implicitWidth: 90; text: conexion.cfg.escape || "0xC8" }
            }
            RowLayout {
                spacing: 8
                GlassButton {
                    text: pc.ok ? "Reconectar" : "Conectar"; active: true
                    onClicked: {
                        const v = transp.model[transp.currentIndex].v
                        const c = {transporte: v, solo_escuchar: soloEsc.checked, escape: campoEscape.text.trim() || "0xC8"}
                        if (v === "serie") { c.puerto = puerto.editText.trim(); c.velocidad = velocidad.model[velocidad.currentIndex] }
                        if (v === "tcp") c.host = host.text.trim()
                        pc.dcn.conectarUi(c)
                    }
                }
                GlassButton { text: "Desconectar"; enabled: pc.plugin === "corriendo"
                              onClicked: pc.pedir("desconectar", "desconectar") }
                Item { Layout.fillWidth: true }
                Text { visible: pc.e.contadores !== undefined; color: Theme.textFaint; font.pixelSize: 11
                       text: pc.e.contadores ? "enviados " + pc.e.contadores.enviados + " · recibidos " + pc.e.contadores.recibidos
                                               + " · reenvíos " + pc.e.contadores.reenvios + " · checksum mal " + pc.e.contadores.checksum_mal : "" }
            }
            Text {
                Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.textFaint; font.pixelSize: 11
                text: "La central tiene que tener habilitada la licencia del Open Interface (LBB4187/00) y su puerto RS-232 en modo Open Interface, a la misma velocidad que acá. El cable es null-modem."
            }
            RowLayout {
                spacing: 8
                Text { text: "Tramas"; color: Theme.text; font { pixelSize: 13; weight: Font.DemiBold } }
                Switch { id: trazaSw; enabled: pc.plugin === "corriendo"; onToggled: pc.pedir("traza", "traza", {activa: checked, ultimas: 200}) }
                GlassButton { compact: true; text: "Actualizar"; enabled: pc.plugin === "corriendo"; onClicked: pc.pedir("traza", "traza", {ultimas: 200}) }
            }
            ScrollView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                TextArea {
                    readOnly: true
                    font { family: Theme.mono; pixelSize: 11 }
                    color: Theme.textDim
                    text: pc.tramas.join("\n") || "(prendé «Tramas» para verlas)"
                }
            }
        }
    }

    PanelCuerpo { id: panelCuerpo }
}
