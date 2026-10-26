// Página Dispositivos y equipos: todo lo que se puede encontrar y manejar
// (Studio.dispositivos, dispositivos/Dispositivos).
//
// Es EL lugar de los dispositivos de audio y video de la máquina (cámaras,
// capturadoras, micrófonos, placas de sonido, NDI, cámaras de red): acá se les
// pone el NOMBRE con que los conoce el operador (de la máquina, no del evento),
// se configuran (nota, formato preferido) y se ve si están conectados. Las
// entradas de video y de audio, el mezclador y las salidas eligen de esta misma
// lista. Los que la máquina ya vio y hoy no están siguen acá, como ausentes.
//
// Es GENÉRICA a propósito:
//   - los detectores salen de la lista que registra la app (red, esta máquina, USB);
//   - las acciones de cada equipo, con sus parámetros, salen de lo que se registró
//     para su tipo (dispositivos/Registro.cpp).
// El botón y su formulario se arman solos. Sumar una acción no toca esta página.
//
// «Drivers»: los drivers y proveedores de dispositivos de la máquina con su
// salud (dispositivos/Drivers, un modelo de lista que se actualiza en su lugar):
// anda / en cuarentena (motivo, fallas) / probando, y «Probar de nuevo».
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import BA
import "../controls"

Rectangle {
    id: page
    color: Theme.bg
    property real leftInset: 92
    readonly property var dd: Studio.dispositivos
    property string sel: ""
    property string filtro: ""
    property var accionAbierta: null     // la acción cuyo formulario se ve
    property var valores: ({})
    property var resultado: null         // {accion, texto|error, datos}
    property bool corriendo: false
    property string msgNombre: ""        // lo que contestó el último renombre/configuración
    readonly property var drv: dd ? dd.drivers : null
    property bool verDrivers: false      // a la derecha, la lista de drivers (en vez de un equipo)
    property string msgDriver: ""        // lo que contestó el último «Probar de nuevo»
    function colorDriver(e) {
        return e === "anda" ? Theme.ok : e === "cuarentena" ? Theme.live : e === "falla" || e === "probando" ? Theme.amber : Theme.textFaint
    }
    // Al entrar se buscan de nuevo los de la máquina (la red y el USB, a pedido).
    onVisibleChanged: if (visible) Studio.inputs.refresh()

    /// Los tipos a los que se les pone nombre (Conocidos::seRecuerda).
    function conNombre(t) { return ["camara_local", "microfono", "salida_audio", "ndi", "camara_red"].indexOf(t) >= 0 }
    /// Las entradas que usan este dispositivo (del mismo modelo que las páginas de entradas).
    readonly property var usos: {
        if (!equipo) return []
        const l = Studio.inputs.inputs
        const out = []
        for (let i = 0; i < l.length; ++i) if (l[i].dispositivoId === equipo.id) out.push(l[i].name)
        return out
    }
    // Los modos de la cámara elegida se le preguntan UNA vez por cámara (tarda):
    // no en cada aviso del registro, que rehace `equipo`.
    readonly property string camaraElegida: equipo && equipo.tipo === "camara_local" && equipo.conectado ? equipo.valor : ""
    property var modosCamara: []
    onCamaraElegidaChanged: modosCamara = camaraElegida !== "" ? Studio.inputs.modes(camaraElegida) : []
    function cargarCampos() {
        nombreCampo.text = equipo && equipo.nombre_propio ? equipo.nombre : ""
        notaCampo.text = equipo && equipo.config && equipo.config.nota ? equipo.config.nota : ""
        msgNombre = ""
    }

    readonly property var equipo: {
        const l = dd ? dd.equipos : []
        for (let i = 0; i < l.length; ++i) if (l[i].id === sel) return l[i]
        return null
    }
    readonly property var lista: {
        const l = dd ? dd.equipos : []
        const f = filtro.toLowerCase()
        return f === "" ? l : l.filter(e => (e.nombre + " " + (e.tecnico || "") + " " + e.detalle + " " + e.tipo).toLowerCase().indexOf(f) >= 0)
    }
    function nombreTipo(t) {
        return ({ camara_red: "Cámaras de la red", equipo_red: "Otros equipos de la red", camara_local: "Cámaras y capturadoras",
                  microfono: "Micrófonos y entradas de audio", salida_audio: "Salidas de audio (placas de sonido)", ndi: "NDI®",
                  usb: "USB", hid: "HID", serie: "Puertos serie" })[t] || t
    }
    function correr(a) {
        page.corriendo = true
        page.resultado = null
        page.dd.accionUi(page.sel, a.id, page.valores)
    }
    Connections {
        target: page.dd
        function onResultado(id, accion, r) {
            if (id !== page.sel) return
            page.corriendo = false
            page.resultado = Object.assign({ accion: accion }, r)
        }
    }
    onSelChanged: { accionAbierta = null; resultado = null; valores = ({}); cargarCampos(); if (sel !== "") verDrivers = false }

    RowLayout {
        anchors { fill: parent; leftMargin: page.leftInset; margins: 20 }
        spacing: 16

        // --- Detectores y lista -------------------------------------------------------
        // Ancho FIJO: un layout con hijos que llenan a lo ancho se vuelve él
        // mismo «fillWidth», y con los detalles largos de la red se comía la
        // pantalla y empujaba el equipo elegido afuera.
        ColumnLayout {
            Layout.fillWidth: false
            Layout.preferredWidth: 380
            Layout.maximumWidth: 380
            Layout.fillHeight: true
            spacing: 10
            Text { text: "Dispositivos y equipos"; color: Theme.text; font { pixelSize: 20; weight: Font.DemiBold } }
            Text {
                // El ancho de la columna, fijo: con fillWidth su ancho natural era el texto entero en
                // un renglón y, al partirse, el alto cambiaba y el RowLayout de la página no paraba.
                Layout.preferredWidth: 380; Layout.maximumWidth: 380; wrapMode: Text.WordWrap
                text: "Los de audio y video de esta máquina, con el nombre que les pongas (vale para todos los eventos). Las entradas, el mezclador y las salidas los muestran así."
                color: Theme.textFaint; font.pixelSize: 11
            }
            Repeater {
                model: page.dd ? page.dd.detectores : []
                delegate: RowLayout {
                    required property var modelData
                    Layout.fillWidth: true
                    spacing: 8
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 0
                        Text { text: modelData.nombre; color: Theme.text; font.pixelSize: 13 }
                        Text {
                            Layout.fillWidth: true
                            text: modelData.escaneando ? "buscando…"
                                : modelData.error ? "✕ " + modelData.error
                                : modelData.cuando ? modelData.cuantos + " encontrados (" + modelData.cuando + ")" : modelData.descripcion
                            color: modelData.error ? Theme.warn : Theme.textFaint; font.pixelSize: 10; elide: Text.ElideRight
                            ToolTip.visible: dHover.hovered; ToolTip.text: modelData.descripcion
                            HoverHandler { id: dHover }
                        }
                    }
                    GlassButton {
                        compact: true; text: modelData.escaneando ? "…" : "Buscar"; icon: "⌕"
                        enabled: !modelData.escaneando
                        onClicked: page.dd.escanearUi(modelData.id)
                    }
                }
            }
            // Los drivers: una fila como la de los detectores; la lista, a la derecha.
            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                visible: page.drv !== null
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 0
                    Text { text: "Drivers"; color: Theme.text; font.pixelSize: 13 }
                    Text {
                        Layout.fillWidth: true
                        text: page.drv ? page.drv.resumen : ""
                        color: page.drv && page.drv.enCuarentena > 0 ? Theme.warn : Theme.textFaint
                        font.pixelSize: 10; elide: Text.ElideRight
                    }
                }
                GlassButton {
                    compact: true; text: "Ver"; active: page.verDrivers
                    onClicked: { page.sel = ""; page.verDrivers = true }
                }
            }
            TextField {
                Layout.fillWidth: true
                placeholderText: "Filtrar (nombre, IP, marca, nombre del sistema…)"
                color: Theme.text; font.pixelSize: 12
                onTextChanged: page.filtro = text
            }
            ListView {
                id: lv
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                spacing: 4
                model: page.lista
                section.property: "tipo"
                section.delegate: Text {
                    required property string section
                    text: page.nombreTipo(section)
                    color: Theme.textDim; font { pixelSize: 11; capitalization: Font.AllUppercase; letterSpacing: 0.5 }
                    topPadding: 8; bottomPadding: 2
                }
                Text {
                    anchors.centerIn: parent; width: parent.width - 20; visible: lv.count === 0
                    wrapMode: Text.WordWrap; horizontalAlignment: Text.AlignHCenter
                    text: "Todavía no hay equipos: tocá «Buscar» en un detector."; color: Theme.textFaint; font.pixelSize: 12
                }
                delegate: Rectangle {
                    required property var modelData
                    width: lv.width
                    height: col.implicitHeight + 12
                    radius: 8
                    color: modelData.id === page.sel ? Qt.rgba(1, 0.42, 0.17, 0.16) : Qt.rgba(1, 1, 1, 0.04)
                    border.color: modelData.id === page.sel ? Theme.accent : "transparent"
                    ColumnLayout {
                        id: col
                        anchors { left: parent.left; right: parent.right; verticalCenter: parent.verticalCenter; margins: 8 }
                        spacing: 1
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 6
                            // Conectado (verde), ausente (rojo) o todavía sin buscar (gris).
                            Rectangle {
                                width: 7; height: 7; radius: 3.5
                                color: modelData.estado === "ausente" ? Theme.live : modelData.estado === "sin_buscar" ? Theme.textFaint : Theme.ok
                            }
                            Text { Layout.fillWidth: true; text: modelData.nombre; color: modelData.conectado ? Theme.text : Theme.textDim
                                   font.pixelSize: 12; elide: Text.ElideRight }
                        }
                        Text { Layout.fillWidth: true; color: Theme.textFaint; font.pixelSize: 10; elide: Text.ElideRight
                               text: (modelData.nombre_propio ? modelData.tecnico + " · " : "") + modelData.detalle }
                    }
                    MouseArea { anchors.fill: parent; onClicked: { page.verDrivers = false; page.sel = modelData.id } }
                }
            }
        }

        // --- El equipo elegido ------------------------------------------------------------
        ScrollView {
            id: sv
            Layout.fillWidth: true
            Layout.fillHeight: true
            contentWidth: availableWidth
            clip: true
            visible: page.equipo !== null && !page.verDrivers

            ColumnLayout {
                width: sv.availableWidth
                spacing: 12

                Panel {
                    Layout.fillWidth: true
                    padding: 12
                    implicitHeight: cab.implicitHeight + 2 * padding
                    ColumnLayout {
                        id: cab
                        anchors.fill: parent
                        spacing: 4
                        RowLayout {
                            Layout.fillWidth: true
                            Text { Layout.fillWidth: true; text: page.equipo ? page.equipo.nombre : ""; elide: Text.ElideRight
                                   color: Theme.text; font { pixelSize: 16; weight: Font.DemiBold } }
                            StatusPill {
                                visible: !!page.equipo && page.conNombre(page.equipo.tipo)
                                text: !page.equipo ? "" : page.equipo.estado === "ausente" ? "AUSENTE"
                                      : page.equipo.estado === "sin_buscar" ? "SIN BUSCAR" : "CONECTADO"
                                dot: page.equipo && page.equipo.estado === "ausente" ? Theme.live
                                     : page.equipo && page.equipo.estado === "sin_buscar" ? Theme.textFaint : Theme.ok
                            }
                        }
                        Text { Layout.fillWidth: true; wrapMode: Text.WordWrap; text: page.equipo ? page.equipo.detalle : ""; color: Theme.textDim; font.pixelSize: 12 }
                        Text { text: page.equipo ? page.nombreTipo(page.equipo.tipo) + " · " + page.equipo.id : ""; color: Theme.textFaint; font.pixelSize: 10
                               Layout.fillWidth: true; elide: Text.ElideMiddle }
                    }
                }

                // Nombre y configuración: solo los de audio y video (los que usan las entradas).
                Panel {
                    Layout.fillWidth: true
                    visible: !!page.equipo && page.conNombre(page.equipo.tipo)
                    padding: 12
                    implicitHeight: nomCol.implicitHeight + 2 * padding
                    ColumnLayout {
                        id: nomCol
                        anchors.fill: parent
                        spacing: 8
                        Text { text: "Nombre y configuración"; color: Theme.text; font { pixelSize: 14; weight: Font.DemiBold } }
                        Text {
                            Layout.fillWidth: true; wrapMode: Text.WordWrap
                            text: "El nombre es de esta máquina: lo ven todos los eventos, las entradas, el mezclador y las salidas. Vacío: el del sistema."
                            color: Theme.textFaint; font.pixelSize: 11
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 8
                            Text { text: "Nombre"; color: Theme.textDim; font.pixelSize: 12; Layout.preferredWidth: 110 }
                            TextField {
                                id: nombreCampo
                                Layout.fillWidth: true
                                placeholderText: page.equipo ? page.equipo.tecnico : ""
                                onAccepted: guardarNombre.clicked()
                            }
                            GlassButton {
                                id: guardarNombre
                                compact: true; text: "Guardar"; active: true
                                onClicked: {
                                    const e = page.dd.renombrar(page.sel, nombreCampo.text)
                                    page.msgNombre = e !== "" ? "✕ " + e : "✓ Guardado"
                                }
                            }
                        }
                        Text {
                            visible: !!page.equipo && page.equipo.nombre_propio
                            text: page.equipo ? "Del sistema: " + page.equipo.tecnico : ""
                            Layout.fillWidth: true; elide: Text.ElideRight
                            color: Theme.textFaint; font.pixelSize: 11
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 8
                            Text { text: "Nota"; color: Theme.textDim; font.pixelSize: 12; Layout.preferredWidth: 110 }
                            TextField {
                                id: notaCampo
                                Layout.fillWidth: true
                                placeholderText: "Dónde está, para qué se usa…"
                                onAccepted: guardarNota.clicked()
                            }
                            GlassButton {
                                id: guardarNota
                                compact: true; text: "Guardar"
                                onClicked: {
                                    const e = page.dd.configurar(page.sel, { nota: notaCampo.text })
                                    page.msgNombre = e !== "" ? "✕ " + e : "✓ Guardado"
                                }
                            }
                        }
                        // Una cámara: el formato que usa una entrada nueva si no se le pide otro.
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 8
                            visible: !!page.equipo && page.equipo.tipo === "camara_local" && page.equipo.conectado
                            Text { text: "Formato preferido"; color: Theme.textDim; font.pixelSize: 12; Layout.preferredWidth: 110; elide: Text.ElideRight }
                            ComboBox {
                                id: modoBox
                                Layout.fillWidth: true
                                model: page.modosCamara
                                textRole: "label"
                                currentIndex: {
                                    const m = page.equipo && page.equipo.config ? (page.equipo.config.modo || "") : ""
                                    const l = model || []
                                    for (let i = 0; i < l.length; ++i) if (l[i].modo === m) return i
                                    return 0
                                }
                                onActivated: (i) => {
                                    const e = page.dd.configurar(page.sel, { modo: model[i].modo })
                                    page.msgNombre = e !== "" ? "✕ " + e : "✓ Guardado"
                                }
                            }
                        }
                        Text {
                            visible: page.msgNombre !== ""
                            text: page.msgNombre; font.pixelSize: 12
                            color: page.msgNombre.indexOf("✕") === 0 ? Theme.warn : Theme.ok
                            Layout.fillWidth: true; wrapMode: Text.WordWrap
                        }
                        Text {
                            Layout.fillWidth: true; wrapMode: Text.WordWrap; font.pixelSize: 12
                            color: Theme.textDim
                            text: page.usos.length > 0 ? "Lo usan: " + page.usos.join(", ") : "Ninguna entrada lo usa todavía."
                        }
                        GlassButton {
                            visible: !!page.equipo && page.equipo.estado === "ausente"
                            compact: true; text: "Olvidar este dispositivo"
                            ToolTip.visible: hovered; ToolTip.delay: 400
                            ToolTip.text: "Lo saca de la lista con su nombre y su configuración (las entradas que lo usan quedan como están)"
                            onClicked: {
                                const dd = page.dd
                                const id = page.sel
                                const nombre = page.equipo.nombre
                                ApplicationWindow.window.confirm({
                                    title: "¿Olvidar «" + nombre + "»?",
                                    text: "Se borran su nombre y su configuración de esta máquina. Si se vuelve a enchufar, aparece con el nombre del sistema.",
                                    accept: "Olvidar",
                                    reject: "Cancelar",
                                    action: function() { const e = dd.olvidar(id); if (e !== "") Studio.notify(e, "error") }
                                })
                            }
                        }
                    }
                }

                // Las acciones: un botón por cada una; con parámetros, su formulario.
                Panel {
                    Layout.fillWidth: true
                    visible: !!page.equipo && page.equipo.conectado
                    padding: 12
                    implicitHeight: acc.implicitHeight + 2 * padding
                    ColumnLayout {
                        id: acc
                        anchors.fill: parent
                        spacing: 8
                        Text { text: "Acciones"; color: Theme.text; font { pixelSize: 14; weight: Font.DemiBold } }
                        Text {
                            visible: page.equipo && page.equipo.acciones.length === 0
                            text: "Este tipo de equipo todavía no tiene acciones."; color: Theme.textFaint; font.pixelSize: 12
                        }
                        Flow {
                            Layout.fillWidth: true
                            spacing: 6
                            Repeater {
                                model: page.equipo ? page.equipo.acciones : []
                                delegate: GlassButton {
                                    required property var modelData
                                    compact: true
                                    text: modelData.titulo
                                    active: page.accionAbierta && page.accionAbierta.id === modelData.id
                                    enabled: !page.corriendo
                                    ToolTip.visible: hovered; ToolTip.delay: 400; ToolTip.text: modelData.descripcion
                                    onClicked: {
                                        page.resultado = null
                                        if (modelData.parametros.length === 0) { page.accionAbierta = null; page.valores = ({}); page.correr(modelData) }
                                        else {
                                            const v = {}
                                            for (const p of modelData.parametros) if (p.defecto !== undefined) v[p.clave] = p.defecto
                                            page.valores = v
                                            page.accionAbierta = modelData
                                        }
                                    }
                                }
                            }
                        }
                        // El formulario, armado con los parámetros declarados.
                        ColumnLayout {
                            visible: page.accionAbierta !== null
                            Layout.fillWidth: true
                            spacing: 6
                            Text {
                                Layout.fillWidth: true; wrapMode: Text.WordWrap
                                text: page.accionAbierta ? page.accionAbierta.descripcion : ""; color: Theme.textDim; font.pixelSize: 11
                            }
                            Repeater {
                                model: page.accionAbierta ? page.accionAbierta.parametros : []
                                delegate: RowLayout {
                                    id: fila
                                    required property var modelData
                                    Layout.fillWidth: true
                                    spacing: 8
                                    Text {
                                        text: fila.modelData.etiqueta + (fila.modelData.obligatorio ? " *" : "")
                                        color: Theme.textDim; font.pixelSize: 12; Layout.preferredWidth: 150; wrapMode: Text.WordWrap
                                    }
                                    TextField {
                                        visible: fila.modelData.tipo === "texto" || fila.modelData.tipo === "clave" || fila.modelData.tipo === "numero"
                                        Layout.fillWidth: true
                                        echoMode: fila.modelData.tipo === "clave" ? TextInput.Password : TextInput.Normal
                                        placeholderText: fila.modelData.ayuda || ""
                                        text: page.valores[fila.modelData.clave] !== undefined ? String(page.valores[fila.modelData.clave]) : ""
                                        onTextEdited: { const v = page.valores; v[fila.modelData.clave] = fila.modelData.tipo === "numero" ? Number(text) : text; page.valores = v }
                                    }
                                    ScrollView {
                                        visible: fila.modelData.tipo === "json"
                                        Layout.fillWidth: true; Layout.preferredHeight: 80
                                        TextArea {
                                            font.family: "Menlo"; font.pixelSize: 11; color: Theme.text
                                            onTextChanged: { const v = page.valores; try { v[fila.modelData.clave] = JSON.parse(text) } catch (e) {} ; page.valores = v }
                                        }
                                    }
                                    ComboBox {
                                        visible: fila.modelData.tipo === "opcion"
                                        Layout.fillWidth: true
                                        model: fila.modelData.opciones || []
                                        currentIndex: Math.max(0, (fila.modelData.opciones || []).indexOf(page.valores[fila.modelData.clave]))
                                        onActivated: (i) => { const v = page.valores; v[fila.modelData.clave] = fila.modelData.opciones[i]; page.valores = v }
                                    }
                                    Switch {
                                        visible: fila.modelData.tipo === "si_no"
                                        checked: page.valores[fila.modelData.clave] === true
                                        onToggled: { const v = page.valores; v[fila.modelData.clave] = checked; page.valores = v }
                                    }
                                }
                            }
                            RowLayout {
                                Item { Layout.fillWidth: true }
                                GlassButton {
                                    compact: true; text: page.corriendo ? "…" : page.accionAbierta ? page.accionAbierta.titulo : ""; active: true
                                    enabled: !page.corriendo
                                    onClicked: page.correr(page.accionAbierta)
                                }
                            }
                        }
                        Text {
                            visible: page.corriendo
                            text: "Trabajando…"; color: Theme.amber; font.pixelSize: 12
                        }
                    }
                }

                // Lo que contestó.
                Panel {
                    Layout.fillWidth: true
                    visible: page.resultado !== null
                    padding: 12
                    implicitHeight: res.implicitHeight + 2 * padding
                    ColumnLayout {
                        id: res
                        anchors.fill: parent
                        spacing: 6
                        Text {
                            Layout.fillWidth: true; wrapMode: Text.WordWrap; font.pixelSize: 13
                            text: !page.resultado ? "" : page.resultado.error ? "✕ " + page.resultado.error : "✓ " + (page.resultado.texto || "Hecho")
                            color: page.resultado && page.resultado.error ? Theme.warn : Theme.ok
                        }
                        ScrollView {
                            visible: page.resultado && page.resultado.datos !== undefined
                            Layout.fillWidth: true
                            Layout.preferredHeight: 320
                            TextArea {
                                readOnly: true; selectByMouse: true
                                text: page.resultado && page.resultado.datos !== undefined ? JSON.stringify(page.resultado.datos, null, 2) : ""
                                color: Theme.text; font { pixelSize: 11; family: "Menlo" }
                                background: Rectangle { color: Theme.field; radius: 6 }
                            }
                        }
                    }
                }

                // Lo que se sabe del equipo.
                Panel {
                    Layout.fillWidth: true
                    visible: !!page.equipo && page.equipo.conectado
                    padding: 12
                    implicitHeight: datosCol.implicitHeight + 2 * padding
                    ColumnLayout {
                        id: datosCol
                        anchors.fill: parent
                        spacing: 6
                        Text { text: "Lo que se sabe"; color: Theme.text; font { pixelSize: 14; weight: Font.DemiBold } }
                        TextArea {
                            Layout.fillWidth: true
                            readOnly: true; selectByMouse: true; wrapMode: TextEdit.Wrap
                            text: page.equipo ? JSON.stringify(page.equipo.datos, null, 2) : ""
                            color: Theme.textDim; font { pixelSize: 11; family: "Menlo" }
                            background: Rectangle { color: Theme.field; radius: 6 }
                        }
                    }
                }
            }
        }
        // --- Los drivers de la máquina -------------------------------------------------
        // Filas de alto fijo (sin texto que se parta): la lista no se acomoda sola.
        ColumnLayout {
            visible: page.verDrivers && page.drv !== null
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 8
            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                Text { text: "Drivers de esta máquina"; color: Theme.text; font { pixelSize: 16; weight: Font.DemiBold } }
                Item { Layout.fillWidth: true }
                GlassButton {
                    compact: true; text: "Revisar todo"
                    enabled: page.drv && !page.drv.revisando
                    ToolTip.visible: hovered; ToolTip.delay: 400
                    ToolTip.text: "Vuelve a probar todo lo instalado, de a uno y fuera del programa (los que están en cuarentena, no: esos con «Probar de nuevo»)"
                    onClicked: page.drv.revisarTodo()
                }
            }
            Text {
                Layout.fillWidth: true; elide: Text.ElideRight
                text: page.drv ? page.drv.maquina + " · " + page.drv.resumen : ""
                color: page.drv && page.drv.revisando ? Theme.amber : Theme.textDim; font.pixelSize: 12
            }
            Text {
                Layout.fillWidth: true; elide: Text.ElideRight
                text: page.drv ? "Cada driver se prueba aparte: si se cuelga o se cae, el programa sigue. A las " + page.drv.umbral + " fallas seguidas queda en cuarentena y no se vuelve a cargar solo." : ""
                color: Theme.textFaint; font.pixelSize: 11
                ToolTip.visible: dExpl.hovered; ToolTip.text: text
                HoverHandler { id: dExpl }
            }
            Text {
                visible: page.msgDriver !== ""
                Layout.fillWidth: true; elide: Text.ElideRight
                text: page.msgDriver; color: Theme.warn; font.pixelSize: 12
            }
            ListView {
                id: lvDrivers
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                spacing: 4
                model: page.drv
                Text {
                    anchors.centerIn: parent; visible: lvDrivers.count === 0
                    text: "Todavía no se revisaron los drivers."; color: Theme.textFaint; font.pixelSize: 12
                }
                delegate: Rectangle {
                    id: filaDriver
                    required property var model
                    width: lvDrivers.width
                    height: 46
                    radius: 8
                    color: Qt.rgba(1, 1, 1, 0.04)
                    RowLayout {
                        anchors { fill: parent; leftMargin: 10; rightMargin: 8 }
                        spacing: 8
                        Rectangle { width: 8; height: 8; radius: 4; color: page.colorDriver(filaDriver.model.estado) }
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 1
                            Text {
                                Layout.fillWidth: true; elide: Text.ElideRight
                                text: filaDriver.model.nombre + (filaDriver.model.tipo === "asio" ? "  · ASIO" : "")
                                color: filaDriver.model.estado === "ausente" ? Theme.textDim : Theme.text; font.pixelSize: 12
                            }
                            Text {
                                Layout.fillWidth: true; elide: Text.ElideRight
                                text: filaDriver.model.texto
                                color: filaDriver.model.estado === "cuarentena" ? Theme.live
                                     : filaDriver.model.estado === "falla" ? Theme.warn : Theme.textFaint
                                font.pixelSize: 10
                                ToolTip.visible: dTexto.hovered && truncated; ToolTip.text: text
                                HoverHandler { id: dTexto }
                            }
                        }
                        GlassButton {
                            visible: filaDriver.model.estado === "cuarentena" || filaDriver.model.estado === "falla"
                                     || filaDriver.model.estado === "probando"
                            compact: true
                            text: filaDriver.model.estado === "probando" ? "Probando…" : "Probar de nuevo"
                            enabled: filaDriver.model.estado !== "probando"
                            ToolTip.visible: hovered; ToolTip.delay: 400
                            ToolTip.text: "Lo prueba una vez más, aparte. Si anda, sale de la cuarentena; si no, se queda."
                            onClicked: { const e = page.drv.reintentar(filaDriver.model.id); page.msgDriver = e !== "" ? "✕ " + e : "" }
                        }
                    }
                }
            }
        }
        Text {
            visible: page.equipo === null && !page.verDrivers
            Layout.fillWidth: true
            text: "Elegí un dispositivo de la lista para ponerle nombre, configurarlo, ver lo que se sabe de él y qué se le puede hacer."
            color: Theme.textFaint; font.pixelSize: 13; wrapMode: Text.WordWrap
        }
    }
}
