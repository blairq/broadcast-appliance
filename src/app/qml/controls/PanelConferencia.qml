// La central de conferencia del recinto (Bosch DCN): micrófonos de banca,
// pedidos de palabra y oradores. Fuente: Studio.dcn (legislatura/ConferenciaDcn),
// que le pide todo al plugin bosch-dcn; las respuestas llegan por `hecho` y
// nada espera en la interfaz. Guía: docs/bosch/open-interface.md §10.
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
    property var funciones: []
    property string resultadoFuncion: ""
    property int pestana: 0

    function pedir(que, control, args) { pc.dcn.pedirUi(que, control, args || {}) }
    Connections {
        target: pc.dcn
        function onHecho(que, ok, r, error) {
            if (!ok) { Studio.notify(error, "error"); return }
            if (que === "puertos") pc.puertos = r.puertos || []
            else if (que === "traza") pc.tramas = r.tramas || []
            else if (que === "funciones") pc.funciones = r.funciones || []
            else if (que === "funcion") pc.resultadoFuncion = JSON.stringify(r.resultado, null, 1)
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
            implicitWidth: 270  // que entre «Operador (la app da la palabra)» entero
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
        TabButton { text: "Votación y asistencia" }
        TabButton { text: "Conexión" }
        TabButton { text: "Avanzado" }
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
                Text {
                    Layout.fillWidth: true; elide: Text.ElideRight; color: Theme.amber; font.pixelSize: 12
                    visible: !!(pc.e.proximo && pc.e.proximo.unidad)
                    text: pc.e.proximo && pc.e.proximo.unidad
                          ? "Próximo según la central: " + (pc.e.proximo.banca ? "banca " + pc.e.proximo.banca : "unidad " + pc.e.proximo.unidad)
                            + (pc.e.proximo.nombre ? " · " + pc.e.proximo.nombre : "") : ""
                }
                CheckBox {
                    text: "Cerrar micrófonos al pasar al siguiente"
                    enabled: pc.ok && !pc.e.solo_escuchar
                    checked: pc.e.cerrar_tras_siguiente === true
                    onToggled: pc.pedir("cts", "cerrar_tras_siguiente", {activo: checked})
                    ToolTip.visible: hovered; ToolTip.delay: 400
                    ToolTip.text: "Con «Siguiente», la central cierra los micrófonos de los que estaban hablando (función sin documentar: probala en la sala)"
                }
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
                    Text { Layout.preferredWidth: 230; elide: Text.ElideRight; color: Theme.textFaint; font.pixelSize: 11
                           text: modelData.mitad ? "DDI " + modelData.ddi + " · micrófono " + modelData.mitad
                                 : (modelData.tipo_nombre || "")
                           HoverHandler { id: sobreDdi }
                           ToolTip.visible: sobreDdi.hovered && !!modelData.mitad; ToolTip.delay: 400
                           ToolTip.text: "Una DCN-DDI en modo doble son dos micrófonos: la izquierda es la unidad y la derecha la unidad + 256. Cada uno va en su banca." }
                    Text { text: modelData.conectada ? "conectada" : "desconectada"; color: modelData.conectada ? Theme.ok : Theme.live; font.pixelSize: 12; Layout.preferredWidth: 90 }
                    TextField { id: campoBanca; Layout.preferredWidth: 90; placeholderText: "banca"; text: modelData.banca || "" }
                    GlassButton { compact: true; text: "Asignar"; enabled: campoBanca.text.trim() !== "" && campoBanca.text !== (modelData.banca || "")
                                  onClicked: pc.pedir("asignar", "unidad_asignar", {banca: campoBanca.text.trim(), unidad: modelData.unidad}) }
                    Item { Layout.fillWidth: true }
                }
            }
        }

        // --- Votación y asistencia (fase 2) ------------------------------------------------
        ColumnLayout {
            id: vot
            spacing: 8
            readonly property var v: pc.e.votacion || ({})
            readonly property var asis: pc.e.asistencia || ({})
            readonly property var err: pc.e.fase2_errores || ({})
            readonly property var vr: Studio.sesionRemota ? Studio.sesionRemota.votacionActual : ({})
            readonly property string est: v.estado || "sin_preparar"
            Text { visible: !!vot.err.votacion; Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.amber; font.pixelSize: 12
                   text: "La votación de la central no arrancó: " + (vot.err.votacion || "") }
            RowLayout {
                spacing: 8
                Layout.fillWidth: true
                TextField {
                    id: temaVot
                    Layout.fillWidth: true
                    placeholderText: "Tema de la votación"
                    text: Studio.ordenDelDia.actual && Studio.ordenDelDia.actual.titulo
                          ? "Punto " + Studio.ordenDelDia.actual.n + ": " + Studio.ordenDelDia.actual.titulo : ""
                }
                ComboBox {
                    id: menuVot
                    implicitWidth: 200
                    model: [ { t: "Sí / No / Abstención", v: "si_no_abstencion" }, { t: "Sí / No", v: "si_no" },
                             { t: "A favor / En contra", v: "a_favor_en_contra" } ]
                    textRole: "t"
                }
                CheckBox { id: nominalVot; text: "Nominal"; checked: true
                           ToolTip.visible: hovered; ToolTip.text: "Voto abierto: se sabe qué votó cada banca" }
            }
            RowLayout {
                spacing: 8
                GlassButton { text: "Preparar"; enabled: pc.ok && vot.est !== "abierta" && vot.est !== "pausada" && temaVot.text.trim() !== ""
                              onClicked: pc.pedir("votacion", "votacion", {accion: "preparar", tema: temaVot.text.trim(),
                                                  numero: (vot.v.numero || 0) + 1, menu: menuVot.model[menuVot.currentIndex].v, abierta: nominalVot.checked}) }
                GlassButton { text: "Abrir la votación"; active: true; enabled: pc.ok && vot.est === "preparada"
                              onClicked: pc.pedir("votacion", "votacion", {accion: "abrir"}) }
                GlassButton { text: vot.est === "pausada" ? "Reanudar" : "Pausar"; enabled: pc.ok && (vot.est === "abierta" || vot.est === "pausada")
                              onClicked: pc.pedir("votacion", "votacion", {accion: vot.est === "pausada" ? "reanudar" : "pausar"}) }
                GlassButton { text: "Cerrar"; enabled: pc.ok && (vot.est === "abierta" || vot.est === "pausada")
                              onClicked: ApplicationWindow.window.confirm({ title: "¿Cerrar la votación?", text: vot.v.tema || "",
                                  accept: "Cerrar", reject: "Seguir", action: function() { pc.pedir("votacion", "votacion", {accion: "cerrar"}) } }) }
                Item { Layout.fillWidth: true }
                GlassButton {
                    text: "Pasar a Quórum (oficial)"; enabled: vot.est === "cerrada" && !!vot.v.totales
                    onClicked: { const e = pc.dcn.pasarAQuorumUi(); Studio.notify(e !== "" ? e : "Resultado cargado en Quórum", e !== "" ? "error" : "info") }
                    ToolTip.visible: hovered; ToolTip.text: "Carga el resultado como oficial en Quórum: acta, CSV nominal y gráfico de votación"
                }
            }
            Text {
                Layout.fillWidth: true; color: Theme.text; font { pixelSize: 15; weight: Font.DemiBold }
                text: vot.est === "sin_preparar" ? "Sin votación" :
                      (vot.v.tema || "") + " — " + ({preparada: "preparada", abierta: "ABIERTA", pausada: "en pausa", cerrada: "cerrada"}[vot.est] || vot.est)
            }
            RowLayout {
                visible: !!vot.v.totales
                spacing: 18
                Repeater {
                    model: vot.v.totales ? ["afirmativo", "negativo", "abstencion", "no_participa"].filter(k => vot.v.totales[k] !== undefined) : []
                    delegate: ColumnLayout {
                        required property string modelData
                        spacing: 0
                        Text { text: vot.v.totales[modelData]; color: Theme.text; font { pixelSize: 30; weight: Font.Black } }
                        Text { text: modelData.replace("_", " "); color: Theme.textDim; font.pixelSize: 11 }
                    }
                }
                ColumnLayout {
                    spacing: 0
                    Text { text: (vot.v.presentes || 0) + " presentes"; color: Theme.textDim; font.pixelSize: 12 }
                    Text { text: (vot.v.no_votaron || 0) + " sin votar"; color: Theme.textDim; font.pixelSize: 12 }
                }
            }
            ListView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                spacing: 2
                model: vot.v.nominal || []
                delegate: RowLayout {
                    required property var modelData
                    width: ListView.view.width
                    Text { Layout.preferredWidth: 80; text: modelData.banca ? "Banca " + modelData.banca : "unidad " + modelData.unidad
                           color: Theme.textDim; font.pixelSize: 12 }
                    Text { Layout.fillWidth: true; text: modelData.nombre || ""; color: Theme.text; font.pixelSize: 12; elide: Text.ElideRight }
                    Text { text: modelData.voto; font { pixelSize: 12; weight: Font.DemiBold }
                           color: modelData.voto === "afirmativo" ? Theme.ok : modelData.voto === "negativo" ? Theme.live : Theme.amber }
                }
            }
            Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: Qt.rgba(1, 1, 1, 0.1) }
            RowLayout {
                spacing: 12
                Text { color: Theme.text; font.pixelSize: 13
                       text: vot.err.asistencia ? "Asistencia de la central: no arrancó (" + vot.err.asistencia + ")"
                             : "Asistencia de la central: " + (vot.asis.presentes !== undefined ? vot.asis.presentes + " presentes, " + (vot.asis.ausentes || 0) + " ausentes" : "sin datos") }
                Item { Layout.fillWidth: true }
                Switch { id: presQ; checked: pc.dcn ? pc.dcn.presentesDeLaCentral : false
                         onToggled: pc.dcn.presentesDeLaCentral = checked }
                Text { text: "Quórum usa estos presentes"; color: Theme.textDim; font.pixelSize: 12 }
            }
            Text {
                Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.amber; font.pixelSize: 12
                visible: !!vot.v.desde_presidente || !!(pc.e.nominal_turno && pc.e.nominal_turno.delegado)
                text: (vot.v.desde_presidente ? "El presidente maneja la votación desde su unidad. " : "")
                      + (pc.e.nominal_turno && pc.e.nominal_turno.delegado
                         ? "Llamado nominal: vota " + (pc.e.nominal_turno.banca ? "la banca " + pc.e.nominal_turno.banca : "el delegado " + pc.e.nominal_turno.delegado)
                           + (pc.e.nominal_turno.segundos_restantes ? " (" + pc.e.nominal_turno.segundos_restantes + " s)" : "") : "")
            }
            // --- Los celulares (sesión remota): votan igual, con o sin la central ---
            Rectangle {
                Layout.fillWidth: true
                implicitHeight: celu.implicitHeight + 20
                radius: 10
                color: Qt.rgba(1, 1, 1, 0.04)
                border.color: vot.vr.estado === "abierta" ? Theme.live : Qt.rgba(1, 1, 1, 0.08)
                ColumnLayout {
                    id: celu
                    anchors { left: parent.left; right: parent.right; top: parent.top; margins: 10 }
                    spacing: 6
                    RowLayout {
                        Layout.fillWidth: true
                        Text { color: Theme.text; font.pixelSize: 13; font.weight: Font.DemiBold
                               text: "Votación en los celulares" + (vot.vr.estado === "abierta" ? " · ABIERTA" : vot.vr.estado === "pausada" ? " · en pausa"
                                     : vot.vr.estado === "cerrada" ? " · cerrada" : "") }
                        Item { Layout.fillWidth: true }
                        Text { color: Theme.textDim; font.pixelSize: 12; text: "Tiempo para votar (s)" }
                        SpinBox { from: 0; to: 600; stepSize: 10; editable: true
                                  value: Studio.sesionRemota ? Studio.sesionRemota.votoSegundos : 60
                                  onValueModified: Studio.sesionRemota.votoSegundos = value }
                    }
                    Text {
                        Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.textDim; font.pixelSize: 12
                        text: (vot.vr.estado === "abierta" || vot.vr.estado === "pausada" || vot.vr.estado === "cerrada")
                              ? "«" + (vot.vr.tema || "") + "» — votaron por el celular " + (vot.vr.votos || []).length + ": " +
                                (vot.vr.opciones || []).map(o => o.texto + " " + ((vot.vr.totales || {})[o.id] || 0)).join(" · ")
                              : "Cuando la central abre una votación, a los habilitados por el celular les aparece el cartel rojo con el tema, " +
                                "las opciones y la cuenta regresiva. Sin la central, se abre desde acá. Al pasar a Quórum vale un voto por integrante."
                    }
                    RowLayout {
                        visible: vot.vr.estado !== "abierta" && vot.vr.estado !== "pausada"
                        TextField { id: temaCel; Layout.fillWidth: true; placeholderText: "Tema (votar sin la central)" }
                        ComboBox { id: menuCel; implicitWidth: 170; textRole: "t"
                                   model: [ { t: "Sí / No / Abst.", v: "si_no_abstencion" }, { t: "Sí / No", v: "si_no" },
                                            { t: "Sí / No / Abst. / NP", v: "si_no_abstencion_nppv" } ] }
                        GlassButton { text: "Abrir en los celulares"; enabled: temaCel.text.trim() !== ""
                                      onClicked: { const e = Studio.sesionRemota.abrirVotacionUi(temaCel.text, menuCel.model[menuCel.currentIndex].v,
                                                                                                Studio.sesionRemota.votoSegundos)
                                                   if (e) Studio.notify(e, "error") } }
                    }
                    RowLayout {
                        visible: (vot.vr.estado === "abierta" || vot.vr.estado === "pausada") && vot.vr.origen === "app"
                        GlassButton { text: vot.vr.estado === "pausada" ? "Reanudar" : "Pausar"
                                      onClicked: Studio.sesionRemota.pausarVotacionUi(vot.vr.estado !== "pausada") }
                        GlassButton { text: "Cerrar"; active: true; onClicked: Studio.sesionRemota.cerrarVotacionUi() }
                    }
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
                RowLayout {
                    visible: transp.currentIndex === 0
                    ComboBox {
                        id: velocidad
                        model: [9600, 19200, 57600, 115200]
                        currentIndex: Math.max(0, model.indexOf(conexion.cfg.velocidad || 115200))
                    }
                    ComboBox {
                        id: protocolo
                        model: [ { t: "Full (de fábrica)", v: "full" }, { t: "Simple", v: "simple" } ]
                        textRole: "t"
                        currentIndex: conexion.cfg.protocolo === "simple" ? 1 : 0
                    }
                    CheckBox { id: rtscts; text: "RTS/CTS"; checked: conexion.cfg.rtscts !== false
                               ToolTip.visible: hovered; ToolTip.text: "Control de flujo por hardware (la CCU lo usa; cable 1 a 1)" }
                }
                Text { visible: transp.currentIndex === 1; text: "Dirección"; color: Theme.textDim; font.pixelSize: 12 }
                RowLayout {
                    visible: transp.currentIndex === 1
                    TextField { id: host; implicitWidth: 200; placeholderText: "192.168.0.10"; text: conexion.cfg.host || "" }
                    TextField { id: puertoTcp; implicitWidth: 70; text: String(conexion.cfg.puerto_tcp || 9451) }
                }
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
                        if (v === "serie") { c.puerto = puerto.editText.trim(); c.velocidad = velocidad.model[velocidad.currentIndex]
                                             c.protocolo = protocolo.model[protocolo.currentIndex].v; c.rtscts = rtscts.checked }
                        if (v === "simulador") c.protocolo = protocolo.model[protocolo.currentIndex].v
                        if (v === "tcp") { c.host = host.text.trim(); c.puerto_tcp = parseInt(puertoTcp.text) || 9451 }
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
                text: "La central tiene que tener habilitada la licencia del Open Interface (LBB4187/00). Por serie: el puerto 1 de la CCU viene de fábrica en Full a 115200 (llaves S500); cable 1 a 1 con RTS/CTS, de hasta unos 2 m. Por red (CCU2): puerto 9451."
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

        // --- Avanzado: las funciones sin documentar (salen del software de Bosch) ----------
        ColumnLayout {
            id: avz
            spacing: 8
            property var sel: pc.funciones.length ? pc.funciones[Math.max(0, lista.currentIndex)] : null
            function plantilla(f) {
                if (!f) return "{}"
                const o = {}
                for (const p of f.parametros) o[p.nombre] = p.tipo === "bool" || p.tipo === "bandera_w" ? false
                        : p.tipo.indexOf("lista") === 0 || p.tipo === "pares_u8" || p.tipo === "textos11x4" ? []
                        : p.tipo === "resto" || p.tipo.indexOf("texto") === 0 ? "" : 0
                return JSON.stringify(o, null, 1)
            }
            onSelChanged: { args.text = plantilla(sel); confirmarF.checked = false; pc.resultadoFuncion = "" }
            // Se pide al abrir la pestaña (al crear el panel el plugin puede no estar corriendo todavía).
            onVisibleChanged: if (visible && !pc.funciones.length && pc.plugin === "corriendo") pc.pedir("funciones", "funciones", {})
            Text {
                Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.amber; font.pixelSize: 12
                text: "Funciones del Open Interface que no están en los manuales: salen del software de Bosch del DVD (docs/bosch/desensamblado.md). Ninguna está probada contra una CCU real; si la central no la conoce, contesta un error. Las de descarga de firmware a la CCU no están."
            }
            RowLayout {
                spacing: 8
                GlassButton { text: "Cargar la lista"; enabled: pc.plugin === "corriendo"; onClicked: pc.pedir("funciones", "funciones", {}) }
                Text { color: Theme.textFaint; font.pixelSize: 12; text: pc.funciones.length ? pc.funciones.length + " funciones" : "" }
            }
            RowLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 12
                ListView {
                    id: lista
                    Layout.preferredWidth: 330
                    Layout.fillHeight: true
                    clip: true
                    spacing: 2
                    model: pc.funciones
                    currentIndex: 0
                    delegate: Rectangle {
                        required property var modelData
                        required property int index
                        width: ListView.view.width; height: 34; radius: 6
                        color: ListView.isCurrentItem ? Qt.rgba(1, 1, 1, 0.14) : Qt.rgba(1, 1, 1, 0.04)
                        RowLayout {
                            anchors { fill: parent; leftMargin: 8; rightMargin: 6 }
                            Text { text: modelData.codigo; color: Theme.textFaint; font { family: Theme.mono; pixelSize: 11 } }
                            Text { Layout.fillWidth: true; elide: Text.ElideRight; font.pixelSize: 12
                                   color: modelData.riesgo === "peligrosa" ? Theme.live : Theme.text
                                   text: modelData.nombre + (modelData.clase === "aviso" ? "  (aviso)" : "") }
                            Text { visible: !modelData.verificado; text: "?"; color: Theme.amber; font.pixelSize: 12
                                   ToolTip.visible: ma.containsMouse; ToolTip.text: "Formato no del todo claro en el desensamblado" }
                        }
                        MouseArea { id: ma; anchors.fill: parent; hoverEnabled: true; onClicked: lista.currentIndex = index }
                    }
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    spacing: 6
                    Text { text: avz.sel ? avz.sel.bosch + " · " + avz.sel.codigo : "Elegí una función"; color: Theme.text
                           font { pixelSize: 13; weight: Font.DemiBold } }
                    Text { Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.textDim; font.pixelSize: 12
                           text: avz.sel ? avz.sel.para : "" }
                    Text { Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.textFaint; font.pixelSize: 11
                           visible: !!avz.sel
                           text: avz.sel ? "Parámetros: " + (avz.sel.parametros.map(p => p.nombre + " (" + p.tipo + (p.descripcion ? ": " + p.descripcion : "") + ")").join(", ") || "ninguno")
                                           + "\nRespuesta: " + (avz.sel.respuesta.map(r => r.nombre + " (" + r.tipo + ")").join(", ") || "nada") : "" }
                    ScrollView {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 110
                        visible: !!avz.sel && avz.sel.clase === "pedido"
                        TextArea { id: args; font.family: Theme.mono; font.pixelSize: 11; color: Theme.text; text: "{}" }
                    }
                    RowLayout {
                        visible: !!avz.sel && avz.sel.clase === "pedido"
                        spacing: 8
                        CheckBox { id: confirmarF; visible: !!avz.sel && avz.sel.riesgo === "peligrosa"
                                   text: "Entiendo: toca licencias o el visor de las unidades" }
                        GlassButton {
                            text: "Mandar a la central"; active: true
                            enabled: pc.ok && (!avz.sel || avz.sel.riesgo !== "peligrosa" || confirmarF.checked)
                            onClicked: {
                                let a
                                try { a = JSON.parse(args.text || "{}") } catch (err) { Studio.notify("Los parámetros no son JSON válido", "error"); return }
                                pc.pedir("funcion", "funcion", {nombre: avz.sel.nombre, args: a, confirmar: confirmarF.checked})
                            }
                        }
                    }
                    Text { visible: !!avz.sel && avz.sel.clase === "aviso"; color: Theme.textFaint; font.pixelSize: 12
                           text: "Es un aviso: lo manda la central sola (sale en el bus como plugin/bosch-dcn/dcn/aviso)." }
                    ScrollView {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        TextArea { readOnly: true; font.family: Theme.mono; font.pixelSize: 11; color: Theme.textDim
                                   text: pc.resultadoFuncion || "(la respuesta aparece acá)" }
                    }
                }
            }
        }
    }

    PanelCuerpo { id: panelCuerpo }
}
