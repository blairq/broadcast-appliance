// Personas → Registro: los tótems de registro del evento abierto.
//
// Un tótem es una página web en una TV con cámara y micrófono (o el celular de
// quien escanea su QR): la persona pone la cara, el asistente la saluda con
// voz y le pide hablando lo que se configura acá. Lo que contesta va al padrón
// del evento (sus «datos») y queda acreditada. Ver docs/funciones/REGISTRO-TOTEM.md.
//
// Tres columnas: los tótems y quién se está registrando ahora; el que se edita
// (textos, opciones y campos); los registrados de este evento con sus datos.
// Todo sale de Studio.totems (ui/TotemsViewModel), el mismo modelo que el MCP.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import BA
import "."

Item {
    id: raiz
    readonly property var tv: Studio.totems
    property string elegido: ""
    readonly property var porId: {
        const m = {}
        for (const t of raiz.tv.totems) m[t.id] = t
        return m
    }
    readonly property var totem: raiz.porId[raiz.elegido] || null
    readonly property var porConn: {
        const m = {}
        for (const c of raiz.tv.conexiones) m[c.conn] = c
        return m
    }
    readonly property var porRegistrado: {
        const m = {}
        for (const r of raiz.tv.registrados) m[r.id] = r
        return m
    }

    // Las listas que cambian seguido van en ListModel con clave y se actualizan
    // en su lugar: cada fila lee lo suyo del mapa de arriba (como VistaGrillas).
    function sincronizar(modelo, lista, clave) {
        for (let i = modelo.count - 1; i >= 0; --i)
            if (!lista.some(x => x[clave] === modelo.get(i)[clave])) modelo.remove(i)
        for (let j = 0; j < lista.length; ++j) {
            let k = -1
            for (let i = 0; i < modelo.count; ++i) if (modelo.get(i)[clave] === lista[j][clave]) { k = i; break }
            if (k < 0) modelo.insert(Math.min(j, modelo.count), lista[j])
            else if (k !== j && j < modelo.count) modelo.move(k, j, 1)
        }
    }
    ListModel { id: filasTotems }
    ListModel { id: filasConexiones }
    ListModel { id: filasRegistrados }
    function sincronizarTodo() {
        raiz.sincronizar(filasTotems, raiz.tv.totems.map(t => ({ clave: t.id })), "clave")
        raiz.sincronizar(filasConexiones, raiz.tv.conexiones.map(c => ({ conn: c.conn })), "conn")
        raiz.sincronizar(filasRegistrados, raiz.tv.registrados.map(r => ({ clave: r.id })), "clave")
        if (raiz.elegido === "" || !raiz.porId[raiz.elegido])
            raiz.elegido = raiz.tv.totems.length > 0 ? raiz.tv.totems[0].id : ""
    }
    Connections {
        target: raiz.tv
        function onCambio() { raiz.sincronizarTodo() }
        function onVivoCambio() { raiz.sincronizarTodo() }
        function onRegistradosCambio() { raiz.sincronizarTodo() }
    }
    Component.onCompleted: raiz.sincronizarTodo()

    function cambiar(clave, valor) {
        if (!raiz.totem) return
        const c = {}
        c[clave] = valor
        const e = raiz.tv.cambiar(raiz.elegido, c)
        if (e !== "") Studio.notify(e, "error")
    }
    function cambiarCampo(i, clave, valor) {
        const c = {}
        c[clave] = valor
        const e = raiz.tv.cambiarCampo(raiz.elegido, i, c)
        if (e !== "") Studio.notify(e, "error")
    }
    function resultado(e) { if (e !== "") Studio.notify(e, "error") }

    // Sin evento no hay dónde guardar: se dice, en vez de una lista vacía.
    Text {
        anchors.centerIn: parent
        visible: !raiz.tv.hayEvento
        width: Math.min(parent.width - 40, 520)
        wrapMode: Text.WordWrap
        horizontalAlignment: Text.AlignHCenter
        color: Theme.textFaint; font.pixelSize: 13
        text: qsTr("Los tótems de registro son de un evento: abrí o creá uno en la página Evento y armalos acá.")
    }

    RowLayout {
        anchors.fill: parent
        visible: raiz.tv.hayEvento
        spacing: 14

        // --- Los tótems y lo vivo ---------------------------------------------
        Panel {
            Layout.preferredWidth: 290
            Layout.fillHeight: true
            padding: 12
            ColumnLayout {
                anchors.fill: parent
                spacing: 8
                RowLayout {
                    Layout.fillWidth: true
                    Text { text: qsTr("Tótems"); color: Theme.text; font { pixelSize: 14; weight: Font.DemiBold } }
                    Item { Layout.fillWidth: true }
                    GlassButton {
                        compact: true; text: qsTr("Nuevo"); icon: "＋"
                        onClicked: {
                            const id = raiz.tv.crear(qsTr("Registro %1").arg(raiz.tv.totems.length + 1))
                            if (id !== "") raiz.elegido = id
                        }
                    }
                }
                Text {
                    Layout.fillWidth: true
                    visible: raiz.tv.totems.length === 0
                    wrapMode: Text.WordWrap
                    color: Theme.textFaint; font.pixelSize: 11
                    text: qsTr("Todavía no hay ninguno. «Nuevo» arma uno que pide nombre y correo.")
                }
                Repeater {
                    model: filasTotems
                    delegate: Rectangle {
                        id: filaT
                        required property string clave
                        readonly property var t: raiz.porId[filaT.clave] || ({})
                        Layout.fillWidth: true
                        implicitHeight: 46
                        radius: 10
                        color: raiz.elegido === filaT.clave ? Qt.rgba(1, 1, 1, 0.09) : Qt.rgba(1, 1, 1, 0.04)
                        border.color: raiz.elegido === filaT.clave ? Theme.accentSoft : Qt.rgba(1, 1, 1, 0.08)
                        MouseArea { anchors.fill: parent; onClicked: raiz.elegido = filaT.clave }
                        RowLayout {
                            anchors { fill: parent; margins: 8 }
                            spacing: 8
                            Rectangle {
                                implicitWidth: 8; implicitHeight: 8; radius: 4
                                color: !filaT.t.activo ? Theme.textFaint : filaT.t.conectados > 0 ? Theme.ok : Theme.accent
                            }
                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 0
                                Text { text: filaT.t.nombre || filaT.clave; color: Theme.text; font.pixelSize: 13
                                       elide: Text.ElideRight; Layout.fillWidth: true }
                                Text {
                                    color: Theme.textFaint; font.pixelSize: 10
                                    text: !filaT.t.activo ? qsTr("apagado")
                                          : (filaT.t.conectados || 0) === 0 ? qsTr("nadie conectado")
                                          : qsTr("%n conexión", "", filaT.t.conectados)
                                }
                            }
                        }
                    }
                }

                // --- La atención remota: alguien del equipo, por web, mira, atiende y completa ---
                Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: Qt.rgba(1, 1, 1, 0.08); visible: raiz.tv.totems.length > 0 }
                RowLayout {
                    Layout.fillWidth: true
                    visible: raiz.tv.totems.length > 0
                    spacing: 4
                    Text {
                        Layout.fillWidth: true
                        text: qsTr("Atención remota"); color: Theme.textDim; font { pixelSize: 12; weight: Font.DemiBold }
                    }
                    GlassButton {
                        compact: true; text: qsTr("Copiar link")
                        onClicked: { Studio.copiar(raiz.tv.urlAtencion); Studio.notify(qsTr("Link de atención copiado"), "ok") }
                        ToolTip.visible: hovered
                        ToolTip.text: qsTr("Quien lo abre ve todas las conversaciones, las mira y escucha, las atiende y " +
                                      "completa datos. Pasalo solo a gente del equipo.")
                    }
                    GlassButton {
                        compact: true; text: "QR"
                        onClicked: Studio.mostrarQr(raiz.tv.urlAtencion, qsTr("Atención del registro"),
                                                    qsTr("Para el equipo: mirar y atender los tótems"), "azul")
                    }
                    GlassButton {
                        compact: true; text: "⟳"
                        onClicked: {
                            const hoja = raiz
                            ApplicationWindow.window.confirm({
                                title: qsTr("¿Cambiar el link de atención?"),
                                text: qsTr("El link de ahora deja de andar y se corta a quien lo tenga abierto."),
                                accept: qsTr("Cambiar"),
                                action: () => { hoja.tv.renovarAtencion() }
                            })
                        }
                        ToolTip.visible: hovered; ToolTip.text: qsTr("Cambiar el link (el viejo deja de andar)")
                    }
                }
                Text {
                    Layout.fillWidth: true
                    visible: raiz.tv.totems.length > 0
                    wrapMode: Text.WordWrap
                    color: raiz.tv.atienden.length > 0 ? Theme.ok : Theme.textFaint; font.pixelSize: 11
                    text: raiz.tv.atienden.length === 0 ? qsTr("Nadie con la página de atención abierta.")
                          : qsTr("Atendiendo: %1").arg(raiz.tv.atienden.map(a => a.atiende ? qsTr("%1 (con una persona)").arg(a.nombre) : a.nombre).join(", "))
                }

                Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: Qt.rgba(1, 1, 1, 0.08) }
                Text { text: qsTr("Registrándose ahora"); color: Theme.textDim; font { pixelSize: 12; weight: Font.DemiBold } }
                Text {
                    Layout.fillWidth: true
                    visible: raiz.tv.hablarFalta !== ""
                    wrapMode: Text.WordWrap
                    color: Theme.warn; font.pixelSize: 11
                    text: qsTr("%1 Sin eso, la página muestra el texto y se puede tipear.").arg(raiz.tv.hablarFalta)
                }
                Text {
                    visible: filasConexiones.count === 0
                    color: Theme.textFaint; font.pixelSize: 11
                    text: raiz.tv.enLinea ? qsTr("Nadie.") : qsTr("El servidor no responde.")
                }
                ScrollView {
                    id: scConn
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    contentWidth: availableWidth
                    clip: true
                    ColumnLayout {
                        width: scConn.availableWidth
                        spacing: 6
                        Repeater {
                            model: filasConexiones
                            delegate: Rectangle {
                                id: filaC
                                required property string conn
                                readonly property var c: raiz.porConn[filaC.conn] || ({})
                                Layout.fillWidth: true
                                implicitHeight: colC.implicitHeight + 14
                                radius: 8
                                color: Qt.rgba(1, 1, 1, 0.04)
                                ColumnLayout {
                                    id: colC
                                    anchors { left: parent.left; right: parent.right; top: parent.top; margins: 7 }
                                    spacing: 2
                                    // La cámara del tótem en vivo (llega por WebRTC, como el
                                    // lobby): la misma imagen en la que se reconoce la cara.
                                    // Alto fijo: nada de alto según el ancho dentro de un layout.
                                    Rectangle {
                                        visible: filaC.c.canal > 0
                                        Layout.fillWidth: true
                                        Layout.preferredHeight: 140
                                        color: "black"; radius: 6; clip: true
                                        VideoItem {
                                            anchors.fill: parent
                                            source: filaC.c.canal > 0 ? "registro:" + filaC.c.canal : ""
                                            fillMode: VideoItem.Fit
                                        }
                                    }
                                    RowLayout {
                                        Layout.fillWidth: true
                                        Text {
                                            Layout.fillWidth: true
                                            elide: Text.ElideRight
                                            color: Theme.text; font.pixelSize: 12
                                            text: (filaC.c.totem || "") + (filaC.c.quien ? " · " + filaC.c.quien : "")
                                        }
                                        Text {
                                            color: filaC.c.canal > 0 ? Theme.ok : Theme.textFaint; font.pixelSize: 10
                                            text: filaC.c.canal > 0 ? (filaC.c.audio ? qsTr("canal %1 · audio") : qsTr("canal %1")).arg(filaC.c.canal)
                                                                    : qsTr("sin canal")
                                        }
                                        GlassButton {
                                            compact: true; text: "✕"
                                            onClicked: raiz.tv.cortar(filaC.conn)
                                            ToolTip.visible: hovered; ToolTip.text: qsTr("Cortar esta conexión")
                                        }
                                    }
                                    Text { color: filaC.c.atendida ? Theme.ok : Theme.accent; font.pixelSize: 11; text: filaC.c.paso || "" }
                                    // Atenderla desde acá (sin voz): el asistente se calla y deja de
                                    // preguntar; se completan los datos y se guarda o se le devuelve.
                                    Flow {
                                        Layout.fillWidth: true
                                        spacing: 4
                                        visible: filaC.c.canal > 0
                                        GlassButton {
                                            compact: true; visible: !filaC.c.atendida; text: qsTr("Atender")
                                            onClicked: raiz.resultado(raiz.tv.atender(filaC.conn))
                                            ToolTip.visible: hovered
                                            ToolTip.text: qsTr("El asistente se calla; en el tótem dice «Te atiende una persona del equipo»")
                                        }
                                        GlassButton {
                                            compact: true; text: qsTr("Datos…")
                                            onClicked: datosDlg.abrir(filaC.conn)
                                        }
                                        GlassButton {
                                            compact: true; visible: filaC.c.atendida === true; text: qsTr("Devolver")
                                            onClicked: raiz.resultado(raiz.tv.terminarAtencion(filaC.conn, "asistente"))
                                            ToolTip.visible: hovered; ToolTip.text: qsTr("Que siga el asistente, desde el próximo dato que falta")
                                        }
                                        GlassButton {
                                            compact: true; visible: filaC.c.atendida === true; text: qsTr("Guardar")
                                            onClicked: raiz.resultado(raiz.tv.terminarAtencion(filaC.conn, "guardar"))
                                        }
                                        GlassButton {
                                            compact: true; visible: filaC.c.atendida === true; text: qsTr("Cancelar")
                                            onClicked: raiz.resultado(raiz.tv.terminarAtencion(filaC.conn, "cancelar"))
                                        }
                                    }
                                    Text {
                                        Layout.fillWidth: true; elide: Text.ElideRight
                                        visible: !!filaC.c.oido
                                        color: Theme.textDim; font.pixelSize: 11
                                        text: qsTr("Se oyó: «%1»").arg(filaC.c.oido || "")
                                    }
                                    Text {
                                        Layout.fillWidth: true; elide: Text.ElideRight
                                        visible: !!filaC.c.dice
                                        color: Theme.textFaint; font.pixelSize: 11
                                        text: qsTr("Dijo: «%1»").arg(filaC.c.dice || "")
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }

        // --- El tótem que se edita ---------------------------------------------
        Panel {
            Layout.fillWidth: true
            Layout.fillHeight: true
            padding: 12
            Text {
                anchors.centerIn: parent
                visible: !raiz.totem
                color: Theme.textFaint; font.pixelSize: 12
                text: qsTr("Elegí un tótem o creá uno.")
            }
            ScrollView {
                id: scEditor
                anchors.fill: parent
                visible: !!raiz.totem
                contentWidth: availableWidth
                clip: true
                ColumnLayout {
                    width: scEditor.availableWidth
                    spacing: 8

                    // Lo que se hace con el tótem: llevarlo a la gente.
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 6
                        Toggle {
                            checked: !!raiz.totem && raiz.totem.activo
                            onToggled: (wanted) => raiz.cambiar("activo", wanted)
                        }
                        Text { text: raiz.totem && raiz.totem.activo ? qsTr("Prendido") : qsTr("Apagado"); color: Theme.textDim
                               font.pixelSize: 12 }
                        Item { Layout.fillWidth: true }
                        GlassButton {
                            compact: true; text: qsTr("Copiar url")
                            onClicked: { Studio.copiar(raiz.tv.url(raiz.elegido)); Studio.notify(qsTr("Url copiada"), "ok") }
                            ToolTip.visible: hovered
                            ToolTip.text: qsTr("La url lleva un secreto: pasala solo a quien vaya a abrir el tótem.")
                        }
                        GlassButton {
                            compact: true; text: "QR"
                            onClicked: Studio.mostrarQr(raiz.tv.url(raiz.elegido), qsTr("Registro — %1").arg(raiz.totem.nombre),
                                                        qsTr("Escaneá para registrarte"), "verde")
                        }
                        GlassButton { compact: true; text: qsTr("Abrir"); onClicked: Qt.openUrlExternally(raiz.tv.url(raiz.elegido)) }
                        GlassButton { compact: true; text: qsTr("Proyectar"); icon: "⧉"; onClicked: proyectarDlg.open() }
                        GlassButton {
                            compact: true; text: "✕"
                            onClicked: {
                                const hoja = raiz
                                const id = raiz.elegido
                                const nombre = raiz.totem.nombre
                                ApplicationWindow.window.confirm({
                                    title: qsTr("¿Borrar el tótem «%1»?").arg(nombre),
                                    text: qsTr("Su url deja de andar y se corta a los que estén conectados. Los que ya se " +
                                          "registraron siguen en el padrón."),
                                    accept: qsTr("Borrar"),
                                    action: () => { hoja.tv.borrar(id) }
                                })
                            }
                        }
                    }

                    GridLayout {
                        Layout.fillWidth: true
                        columns: 2
                        columnSpacing: 10
                        rowSpacing: 6
                        Text { text: qsTr("Nombre"); color: Theme.textDim; font.pixelSize: 12 }
                        TextField {
                            Layout.fillWidth: true
                            text: raiz.totem ? raiz.totem.nombre : ""
                            placeholderText: qsTr("Para vos: «Entrada principal»")
                            onEditingFinished: if (raiz.totem && text !== raiz.totem.nombre) raiz.cambiar("nombre", text)
                        }
                        Text { text: qsTr("Título"); color: Theme.textDim; font.pixelSize: 12 }
                        TextField {
                            Layout.fillWidth: true
                            text: raiz.totem ? raiz.totem.titulo : ""
                            placeholderText: "{evento}"
                            onEditingFinished: if (raiz.totem && text !== raiz.totem.titulo) raiz.cambiar("titulo", text)
                        }
                        Text { text: qsTr("Subtítulo"); color: Theme.textDim; font.pixelSize: 12 }
                        TextField {
                            Layout.fillWidth: true
                            text: raiz.totem ? raiz.totem.subtitulo : ""
                            onEditingFinished: if (raiz.totem && text !== raiz.totem.subtitulo) raiz.cambiar("subtitulo", text)
                        }
                        Text { text: qsTr("Saludo"); color: Theme.textDim; font.pixelSize: 12 }
                        TextField {
                            Layout.fillWidth: true
                            text: raiz.totem ? raiz.totem.saludo : ""
                            onEditingFinished: if (raiz.totem && text !== raiz.totem.saludo) raiz.cambiar("saludo", text)
                        }
                        Text { text: qsTr("Despedida"); color: Theme.textDim; font.pixelSize: 12 }
                        TextField {
                            Layout.fillWidth: true
                            text: raiz.totem ? raiz.totem.despedida : ""
                            onEditingFinished: if (raiz.totem && text !== raiz.totem.despedida) raiz.cambiar("despedida", text)
                        }
                        Text { text: qsTr("Ya registrado"); color: Theme.textDim; font.pixelSize: 12 }
                        TextField {
                            Layout.fillWidth: true
                            text: raiz.totem ? raiz.totem.yaRegistrado : ""
                            onEditingFinished: if (raiz.totem && text !== raiz.totem.yaRegistrado) raiz.cambiar("yaRegistrado", text)
                        }
                    }
                    Text {
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        color: Theme.textFaint; font.pixelSize: 10
                        text: qsTr("Se dicen en voz alta: frases cortas. {evento} es el nombre del evento; {hola} es «Hola» " +
                              "o «Hola Ana» si la reconoce; {nombre}, el nombre completo.")
                    }
                    RowLayout {
                        spacing: 8
                        Toggle { checked: !!raiz.totem && raiz.totem.confirmar
                                 onToggled: (wanted) => raiz.cambiar("confirmar", wanted) }
                        Text { text: qsTr("Repetir cada dato y preguntar «¿está bien?»"); color: Theme.text; font.pixelSize: 12 }
                    }
                    RowLayout {
                        spacing: 8
                        Toggle { checked: !!raiz.totem && raiz.totem.reconocer
                                 onToggled: (wanted) => raiz.cambiar("reconocer", wanted) }
                        Text { text: qsTr("Saludar por el nombre a quien reconoce la cara (y no preguntárselo)")
                               color: Theme.text; font.pixelSize: 12 }
                    }
                    RowLayout {
                        spacing: 8
                        Toggle { checked: !!raiz.totem && raiz.totem.foto
                                 onToggled: (wanted) => raiz.cambiar("foto", wanted) }
                        Text { text: qsTr("Al final, pedirle que mire a la cámara para una foto de frente")
                               color: Theme.text; font.pixelSize: 12 }
                    }

                    // --- El modelo que entiende (el rol «registro» de la pestaña Agentes) -------
                    RowLayout {
                        spacing: 8
                        Toggle { checked: !!raiz.totem && raiz.totem.conModelo
                                 onToggled: (wanted) => raiz.cambiar("conModelo", wanted) }
                        Text { text: qsTr("Entender con el modelo: respuestas de corrido, correcciones y preguntas sobre el evento")
                               color: Theme.text; font.pixelSize: 12 }
                    }
                    Text {
                        Layout.fillWidth: true
                        visible: !!raiz.totem && raiz.totem.conModelo
                        wrapMode: Text.WordWrap
                        color: raiz.tv.agenteAfuera ? Theme.amber : Theme.textFaint; font.pixelSize: 10
                        text: qsTr("%1 El guion sigue haciendo las preguntas; si el modelo no contesta a tiempo, sigue el guion solo.").arg(raiz.tv.agente)
                    }
                    Text {
                        visible: !!raiz.totem && raiz.totem.conModelo
                        text: qsTr("Ficha del evento"); color: Theme.textDim; font.pixelSize: 12
                    }
                    TextArea {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 90
                        visible: !!raiz.totem && raiz.totem.conModelo
                        wrapMode: TextEdit.Wrap
                        text: raiz.totem ? raiz.totem.ficha : ""
                        placeholderText: qsTr("Lo que puede contestar si le preguntan: horarios, lugar, salas, baños, wifi…")
                        color: Theme.text; font.pixelSize: 12
                        background: Rectangle { color: Theme.field; radius: 8 }
                        onEditingFinished: if (raiz.totem && text !== raiz.totem.ficha) raiz.cambiar("ficha", text)
                    }

                    // --- Los campos -----------------------------------------------
                    RowLayout {
                        Layout.fillWidth: true
                        Layout.topMargin: 6
                        Text { text: qsTr("Lo que se le pide"); color: Theme.text; font { pixelSize: 14; weight: Font.DemiBold } }
                        Item { Layout.fillWidth: true }
                        ComboBox {
                            id: tipoNuevo
                            implicitWidth: 190
                            model: raiz.tv.tipos
                            textRole: "etiqueta"
                            valueRole: "id"
                        }
                        GlassButton {
                            compact: true; text: qsTr("Agregar"); icon: "＋"
                            onClicked: raiz.resultado(raiz.tv.agregarCampo(raiz.elegido, tipoNuevo.currentValue))
                        }
                    }
                    Text {
                        visible: !!raiz.totem && raiz.totem.campos.length === 0
                        color: Theme.textFaint; font.pixelSize: 11
                        text: qsTr("Sin campos: saluda, la acredita y la despide.")
                    }
                    // El modelo es la CANTIDAD: las filas no se rearman al cambiar un texto.
                    Repeater {
                        model: raiz.totem ? raiz.totem.campos.length : 0
                        delegate: Rectangle {
                            id: filaCampo
                            required property int index
                            readonly property var c: raiz.totem && raiz.totem.campos[filaCampo.index]
                                                     ? raiz.totem.campos[filaCampo.index] : ({})
                            Layout.fillWidth: true
                            implicitHeight: gridCampo.implicitHeight + 16
                            radius: 10
                            color: Qt.rgba(1, 1, 1, 0.04)
                            border.color: Qt.rgba(1, 1, 1, 0.08)
                            GridLayout {
                                id: gridCampo
                                anchors { left: parent.left; right: parent.right; top: parent.top; margins: 8 }
                                columns: 4
                                columnSpacing: 6
                                rowSpacing: 4
                                ComboBox {
                                    implicitWidth: 170
                                    model: raiz.tv.tipos
                                    textRole: "etiqueta"
                                    valueRole: "id"
                                    currentIndex: Math.max(0, raiz.tv.tipos.findIndex(x => x.id === filaCampo.c.tipo))
                                    onActivated: raiz.cambiarCampo(filaCampo.index, "tipo", currentValue)
                                }
                                TextField {
                                    Layout.fillWidth: true
                                    text: filaCampo.c.etiqueta || ""
                                    placeholderText: qsTr("Etiqueta en la pantalla")
                                    onEditingFinished: if (text !== filaCampo.c.etiqueta) raiz.cambiarCampo(filaCampo.index, "etiqueta", text)
                                }
                                TextField {
                                    implicitWidth: 130
                                    text: filaCampo.c.clave || ""
                                    placeholderText: qsTr("clave")
                                    onEditingFinished: if (text !== filaCampo.c.clave) raiz.cambiarCampo(filaCampo.index, "clave", text)
                                    ToolTip.visible: hovered; ToolTip.text: qsTr("La clave en los datos del padrón (minúsculas, sin espacios)")
                                }
                                RowLayout {
                                    spacing: 2
                                    GlassButton { compact: true; text: "↑"; enabled: filaCampo.index > 0
                                                  onClicked: raiz.resultado(raiz.tv.moverCampo(raiz.elegido, filaCampo.index, filaCampo.index - 1)) }
                                    GlassButton { compact: true; text: "↓"; enabled: !!raiz.totem && filaCampo.index < raiz.totem.campos.length - 1
                                                  onClicked: raiz.resultado(raiz.tv.moverCampo(raiz.elegido, filaCampo.index, filaCampo.index + 1)) }
                                    GlassButton { compact: true; text: "✕"
                                                  onClicked: raiz.resultado(raiz.tv.quitarCampo(raiz.elegido, filaCampo.index)) }
                                }
                                TextField {
                                    Layout.columnSpan: 3
                                    Layout.fillWidth: true
                                    text: filaCampo.c.pregunta || ""
                                    placeholderText: qsTr("La pregunta en voz alta (vacía = la de fábrica)")
                                    onEditingFinished: if (text !== (filaCampo.c.pregunta || "")) raiz.cambiarCampo(filaCampo.index, "pregunta", text)
                                }
                                RowLayout {
                                    spacing: 4
                                    Toggle { checked: !!filaCampo.c.obligatorio
                                             onToggled: (wanted) => raiz.cambiarCampo(filaCampo.index, "obligatorio", wanted) }
                                    Text { text: qsTr("Obligatorio"); color: Theme.textDim; font.pixelSize: 11 }
                                }
                                // Documento: qué tipos se aceptan (los de fábrica y los de los plugins
                                // que corren). Ninguno marcado = cualquiera.
                                Flow {
                                    Layout.columnSpan: 4
                                    Layout.fillWidth: true
                                    visible: filaCampo.c.tipo === "documento"
                                    spacing: 8
                                    Text {
                                        text: raiz.tv.lectorDisponible ? qsTr("Acepta:") : qsTr("Esta compilación no trae el lector de códigos: solo se puede tipear el número.")
                                        color: raiz.tv.lectorDisponible ? Theme.textDim : Theme.warn
                                        font.pixelSize: 11
                                        height: 26; verticalAlignment: Text.AlignVCenter
                                    }
                                    Repeater {
                                        model: raiz.tv.tiposDocumento
                                        delegate: CheckBox {
                                            required property var modelData
                                            readonly property var aceptados: filaCampo.c.documentos || []
                                            text: modelData.origen === "plugin" ? qsTr("%1 (plugin)").arg(modelData.titulo) : modelData.titulo
                                            checked: aceptados.indexOf(modelData.id) >= 0
                                            onToggled: {
                                                const l = aceptados.filter(x => x !== modelData.id)
                                                if (checked) l.push(modelData.id)
                                                raiz.cambiarCampo(filaCampo.index, "documentos", l)
                                            }
                                        }
                                    }
                                    Text {
                                        visible: (filaCampo.c.documentos || []).length === 0
                                        text: qsTr("(ninguno marcado: cualquiera)")
                                        color: Theme.textFaint; font.pixelSize: 11
                                        height: 26; verticalAlignment: Text.AlignVCenter
                                    }
                                }
                                // El dorso: si el código no sale, la parte de atrás con un modelo de visión.
                                // Una línea y recortada, en su propia fila: con salto de línea adentro del
                                // Flow, su alto dependía del ancho y el del layout de su texto, y la
                                // interfaz se quedaba acomodándose para siempre (el vigilante la mataba).
                                Text {
                                    Layout.columnSpan: 4
                                    Layout.fillWidth: true
                                    Layout.preferredWidth: 1
                                    visible: filaCampo.c.tipo === "documento" && text !== ""
                                    elide: Text.ElideRight
                                    text: raiz.tv.dorso
                                    color: raiz.tv.dorsoDisponible ? Theme.textDim : Theme.textFaint
                                    font.pixelSize: 11
                                }
                                TextField {
                                    Layout.columnSpan: 4
                                    Layout.fillWidth: true
                                    visible: filaCampo.c.tipo === "opcion"
                                    text: (filaCampo.c.opciones || []).join(", ")
                                    placeholderText: qsTr("Las opciones, separadas por comas")
                                    onEditingFinished: raiz.cambiarCampo(filaCampo.index, "opciones",
                                                                         text.split(",").map(s => s.trim()).filter(s => s !== ""))
                                }
                            }
                        }
                    }
                }
            }
        }

        // --- Los registrados -----------------------------------------------------
        Panel {
            Layout.preferredWidth: 320
            Layout.fillHeight: true
            padding: 12
            ColumnLayout {
                anchors.fill: parent
                spacing: 8
                Text {
                    text: qsTr("Registrados en este evento (%1)").arg(filasRegistrados.count)
                    color: Theme.text; font { pixelSize: 14; weight: Font.DemiBold }
                }
                Text {
                    Layout.fillWidth: true
                    visible: filasRegistrados.count === 0
                    wrapMode: Text.WordWrap
                    color: Theme.textFaint; font.pixelSize: 11
                    text: qsTr("Nadie todavía. Quedan en el padrón del evento, con lo que contestaron.")
                }
                ScrollView {
                    id: scReg
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    contentWidth: availableWidth
                    clip: true
                    ColumnLayout {
                        width: scReg.availableWidth
                        spacing: 6
                        Repeater {
                            model: filasRegistrados
                            delegate: Rectangle {
                                id: filaR
                                required property string clave
                                readonly property var r: raiz.porRegistrado[filaR.clave] || ({})
                                readonly property var claves: Object.keys(filaR.r.datos || {})
                                Layout.fillWidth: true
                                implicitHeight: colR.implicitHeight + 14
                                radius: 8
                                color: Qt.rgba(1, 1, 1, 0.04)
                                ColumnLayout {
                                    id: colR
                                    anchors { left: parent.left; right: parent.right; top: parent.top; margins: 7 }
                                    spacing: 1
                                    RowLayout {
                                        Layout.fillWidth: true
                                        Text { Layout.fillWidth: true; elide: Text.ElideRight
                                               text: filaR.r.nombre || ""; color: Theme.text
                                               font { pixelSize: 13; weight: Font.DemiBold } }
                                        Text {
                                            color: Theme.textFaint; font.pixelSize: 10
                                            text: filaR.r.llegoMs > 0 ? new Date(filaR.r.llegoMs).toLocaleTimeString(Qt.locale(), "HH:mm") : ""
                                        }
                                    }
                                    Text { color: Theme.textFaint; font.pixelSize: 10; text: filaR.r.totem || "" }
                                    Repeater {
                                        model: filaR.claves
                                        delegate: Text {
                                            required property string modelData
                                            Layout.fillWidth: true
                                            elide: Text.ElideRight
                                            color: Theme.textDim; font.pixelSize: 11
                                            readonly property var v: filaR.r.datos[modelData]
                                            // Un documento (objeto) viene ya escrito por el modelo en datosTexto.
                                            readonly property var vt: (filaR.r.datosTexto || {})[modelData]
                                            text: qsTr("%1: %2").arg(modelData).arg(vt !== undefined ? vt
                                                                      : v === true ? qsTr("sí") : v === false ? qsTr("no") : String(v))
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    // A qué televisor: la lista es la de Pantallas (un solo modelo); la TV
    // recibe la página del tótem con ?pantalla=1, que muestra su QR grande.
    Dialog {
        id: proyectarDlg
        anchors.centerIn: Overlay.overlay
        width: 420
        modal: true
        title: qsTr("Proyectar el registro")
        standardButtons: Dialog.Close
        ColumnLayout {
            spacing: 8
            Text {
                Layout.preferredWidth: 380
                wrapMode: Text.WordWrap
                text: qsTr("La TV muestra el QR grande con «Escaneá para registrarte»: cada celular que lo escanea se " +
                      "registra hablando. Para que la TV misma registre (con cámara y micrófono), abrí la url en ella.")
                color: Theme.textDim
                font.pixelSize: 12
            }
            Repeater {
                model: Studio.pantallas.pantallas
                delegate: GlassButton {
                    required property var modelData
                    Layout.preferredWidth: 380
                    text: modelData.nombre
                    onClicked: {
                        const err = Studio.pantallas.mostrar(modelData.id, "url", { url: raiz.tv.urlPantalla(raiz.elegido) })
                        if (err) Studio.notify(err, "error")
                        proyectarDlg.close()
                    }
                }
            }
            Text {
                Layout.preferredWidth: 380
                visible: Studio.pantallas.pantallas.length === 0
                wrapMode: Text.WordWrap
                text: qsTr("No hay ninguna pantalla. Buscalas en la página de Pantallas.")
                color: Theme.textFaint
                font.pixelSize: 12
            }
        }
    }

    // Completar o corregir los datos de una conversación (atendida o no): una foto de los
    // campos al abrir (un ListModel propio: lo que cambia mientras se tipea no pisa lo que se
    // está escribiendo); cada uno se manda al terminar de editarlo, como si lo hubiera tipeado.
    Dialog {
        id: datosDlg
        property string conn: ""
        anchors.centerIn: Overlay.overlay
        width: 460
        modal: true
        title: qsTr("Los datos de esta conversación")
        standardButtons: Dialog.Close
        ListModel { id: camposDlg }
        function abrir(conn) {
            datosDlg.conn = conn
            camposDlg.clear()
            const c = raiz.porConn[conn]
            for (const f of (c && c.campos) || [])
                camposDlg.append({ clave: f.clave, etiqueta: f.etiqueta || f.clave, valor: String(f.valor || "") })
            datosDlg.open()
        }
        ColumnLayout {
            spacing: 6
            Text {
                Layout.preferredWidth: 420
                wrapMode: Text.WordWrap
                color: Theme.textDim; font.pixelSize: 12
                text: qsTr("Lo que pongas se ve en la pantalla del tótem. Vacío borra el dato.")
            }
            Text {
                Layout.preferredWidth: 420
                visible: camposDlg.count === 0
                color: Theme.textFaint; font.pixelSize: 12
                text: qsTr("Todavía no hay una conversación: «Atender» para empezarla.")
            }
            Repeater {
                model: camposDlg
                delegate: RowLayout {
                    required property string clave
                    required property string etiqueta
                    required property string valor
                    Layout.preferredWidth: 420
                    Text { Layout.preferredWidth: 110; text: etiqueta; color: Theme.textDim; font.pixelSize: 12; elide: Text.ElideRight }
                    TextField {
                        Layout.fillWidth: true
                        text: valor
                        onEditingFinished: if (text !== valor) raiz.resultado(raiz.tv.ponerDato(datosDlg.conn, clave, text))
                    }
                }
            }
        }
    }
}
