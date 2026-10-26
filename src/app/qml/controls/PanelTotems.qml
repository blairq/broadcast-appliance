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
        text: "Los tótems de registro son de un evento: abrí o creá uno en la página Evento y armalos acá."
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
                    Text { text: "Tótems"; color: Theme.text; font { pixelSize: 14; weight: Font.DemiBold } }
                    Item { Layout.fillWidth: true }
                    GlassButton {
                        compact: true; text: "Nuevo"; icon: "＋"
                        onClicked: {
                            const id = raiz.tv.crear("Registro " + (raiz.tv.totems.length + 1))
                            if (id !== "") raiz.elegido = id
                        }
                    }
                }
                Text {
                    Layout.fillWidth: true
                    visible: raiz.tv.totems.length === 0
                    wrapMode: Text.WordWrap
                    color: Theme.textFaint; font.pixelSize: 11
                    text: "Todavía no hay ninguno. «Nuevo» arma uno que pide nombre y correo."
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
                                    text: !filaT.t.activo ? "apagado"
                                          : (filaT.t.conectados || 0) === 0 ? "nadie conectado"
                                          : filaT.t.conectados === 1 ? "1 conexión" : filaT.t.conectados + " conexiones"
                                }
                            }
                        }
                    }
                }

                Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: Qt.rgba(1, 1, 1, 0.08) }
                Text { text: "Registrándose ahora"; color: Theme.textDim; font { pixelSize: 12; weight: Font.DemiBold } }
                Text {
                    Layout.fillWidth: true
                    visible: raiz.tv.hablarFalta !== ""
                    wrapMode: Text.WordWrap
                    color: Theme.warn; font.pixelSize: 11
                    text: raiz.tv.hablarFalta + " Sin eso, la página muestra el texto y se puede tipear."
                }
                Text {
                    visible: filasConexiones.count === 0
                    color: Theme.textFaint; font.pixelSize: 11
                    text: raiz.tv.enLinea ? "Nadie." : "La sala no responde."
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
                                            text: filaC.c.canal > 0 ? "canal " + filaC.c.canal + (filaC.c.audio ? " · audio" : "")
                                                                    : "sin canal"
                                        }
                                        GlassButton {
                                            compact: true; text: "✕"
                                            onClicked: raiz.tv.cortar(filaC.conn)
                                            ToolTip.visible: hovered; ToolTip.text: "Cortar esta conexión"
                                        }
                                    }
                                    Text { color: Theme.accent; font.pixelSize: 11; text: filaC.c.paso || "" }
                                    Text {
                                        Layout.fillWidth: true; elide: Text.ElideRight
                                        visible: !!filaC.c.oido
                                        color: Theme.textDim; font.pixelSize: 11
                                        text: "Se oyó: «" + (filaC.c.oido || "") + "»"
                                    }
                                    Text {
                                        Layout.fillWidth: true; elide: Text.ElideRight
                                        visible: !!filaC.c.dice
                                        color: Theme.textFaint; font.pixelSize: 11
                                        text: "Dijo: «" + (filaC.c.dice || "") + "»"
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
                text: "Elegí un tótem o creá uno."
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
                        Text { text: raiz.totem && raiz.totem.activo ? "Prendido" : "Apagado"; color: Theme.textDim
                               font.pixelSize: 12 }
                        Item { Layout.fillWidth: true }
                        GlassButton {
                            compact: true; text: "Copiar url"
                            onClicked: { Studio.copiar(raiz.tv.url(raiz.elegido)); Studio.notify("Url copiada", "ok") }
                            ToolTip.visible: hovered
                            ToolTip.text: "La url lleva un secreto: pasala solo a quien vaya a abrir el tótem."
                        }
                        GlassButton {
                            compact: true; text: "QR"
                            onClicked: Studio.mostrarQr(raiz.tv.url(raiz.elegido), "Registro — " + raiz.totem.nombre,
                                                        "Escaneá para registrarte", "verde")
                        }
                        GlassButton { compact: true; text: "Abrir"; onClicked: Qt.openUrlExternally(raiz.tv.url(raiz.elegido)) }
                        GlassButton { compact: true; text: "Proyectar"; icon: "⧉"; onClicked: proyectarDlg.open() }
                        GlassButton {
                            compact: true; text: "✕"
                            onClicked: {
                                const hoja = raiz
                                const id = raiz.elegido
                                const nombre = raiz.totem.nombre
                                ApplicationWindow.window.confirm({
                                    title: "¿Borrar el tótem «" + nombre + "»?",
                                    text: "Su url deja de andar y se corta a los que estén conectados. Los que ya se " +
                                          "registraron siguen en el padrón.",
                                    accept: "Borrar",
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
                        Text { text: "Nombre"; color: Theme.textDim; font.pixelSize: 12 }
                        TextField {
                            Layout.fillWidth: true
                            text: raiz.totem ? raiz.totem.nombre : ""
                            placeholderText: "Para vos: «Entrada principal»"
                            onEditingFinished: if (raiz.totem && text !== raiz.totem.nombre) raiz.cambiar("nombre", text)
                        }
                        Text { text: "Título"; color: Theme.textDim; font.pixelSize: 12 }
                        TextField {
                            Layout.fillWidth: true
                            text: raiz.totem ? raiz.totem.titulo : ""
                            placeholderText: "{evento}"
                            onEditingFinished: if (raiz.totem && text !== raiz.totem.titulo) raiz.cambiar("titulo", text)
                        }
                        Text { text: "Subtítulo"; color: Theme.textDim; font.pixelSize: 12 }
                        TextField {
                            Layout.fillWidth: true
                            text: raiz.totem ? raiz.totem.subtitulo : ""
                            onEditingFinished: if (raiz.totem && text !== raiz.totem.subtitulo) raiz.cambiar("subtitulo", text)
                        }
                        Text { text: "Saludo"; color: Theme.textDim; font.pixelSize: 12 }
                        TextField {
                            Layout.fillWidth: true
                            text: raiz.totem ? raiz.totem.saludo : ""
                            onEditingFinished: if (raiz.totem && text !== raiz.totem.saludo) raiz.cambiar("saludo", text)
                        }
                        Text { text: "Despedida"; color: Theme.textDim; font.pixelSize: 12 }
                        TextField {
                            Layout.fillWidth: true
                            text: raiz.totem ? raiz.totem.despedida : ""
                            onEditingFinished: if (raiz.totem && text !== raiz.totem.despedida) raiz.cambiar("despedida", text)
                        }
                        Text { text: "Ya registrado"; color: Theme.textDim; font.pixelSize: 12 }
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
                        text: "Se dicen en voz alta: frases cortas. {evento} es el nombre del evento; {hola} es «Hola» " +
                              "o «Hola Ana» si la reconoce; {nombre}, el nombre completo."
                    }
                    RowLayout {
                        spacing: 8
                        Toggle { checked: !!raiz.totem && raiz.totem.confirmar
                                 onToggled: (wanted) => raiz.cambiar("confirmar", wanted) }
                        Text { text: "Repetir cada dato y preguntar «¿está bien?»"; color: Theme.text; font.pixelSize: 12 }
                    }
                    RowLayout {
                        spacing: 8
                        Toggle { checked: !!raiz.totem && raiz.totem.reconocer
                                 onToggled: (wanted) => raiz.cambiar("reconocer", wanted) }
                        Text { text: "Saludar por el nombre a quien reconoce la cara (y no preguntárselo)"
                               color: Theme.text; font.pixelSize: 12 }
                    }
                    RowLayout {
                        spacing: 8
                        Toggle { checked: !!raiz.totem && raiz.totem.foto
                                 onToggled: (wanted) => raiz.cambiar("foto", wanted) }
                        Text { text: "Al final, pedirle que mire a la cámara para una foto de frente"
                               color: Theme.text; font.pixelSize: 12 }
                    }

                    // --- Los campos -----------------------------------------------
                    RowLayout {
                        Layout.fillWidth: true
                        Layout.topMargin: 6
                        Text { text: "Lo que se le pide"; color: Theme.text; font { pixelSize: 14; weight: Font.DemiBold } }
                        Item { Layout.fillWidth: true }
                        ComboBox {
                            id: tipoNuevo
                            implicitWidth: 190
                            model: raiz.tv.tipos
                            textRole: "etiqueta"
                            valueRole: "id"
                        }
                        GlassButton {
                            compact: true; text: "Agregar"; icon: "＋"
                            onClicked: raiz.resultado(raiz.tv.agregarCampo(raiz.elegido, tipoNuevo.currentValue))
                        }
                    }
                    Text {
                        visible: !!raiz.totem && raiz.totem.campos.length === 0
                        color: Theme.textFaint; font.pixelSize: 11
                        text: "Sin campos: saluda, la acredita y la despide."
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
                                    placeholderText: "Etiqueta en la pantalla"
                                    onEditingFinished: if (text !== filaCampo.c.etiqueta) raiz.cambiarCampo(filaCampo.index, "etiqueta", text)
                                }
                                TextField {
                                    implicitWidth: 130
                                    text: filaCampo.c.clave || ""
                                    placeholderText: "clave"
                                    onEditingFinished: if (text !== filaCampo.c.clave) raiz.cambiarCampo(filaCampo.index, "clave", text)
                                    ToolTip.visible: hovered; ToolTip.text: "La clave en los datos del padrón (minúsculas, sin espacios)"
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
                                    placeholderText: "La pregunta en voz alta (vacía = la de fábrica)"
                                    onEditingFinished: if (text !== (filaCampo.c.pregunta || "")) raiz.cambiarCampo(filaCampo.index, "pregunta", text)
                                }
                                RowLayout {
                                    spacing: 4
                                    Toggle { checked: !!filaCampo.c.obligatorio
                                             onToggled: (wanted) => raiz.cambiarCampo(filaCampo.index, "obligatorio", wanted) }
                                    Text { text: "Obligatorio"; color: Theme.textDim; font.pixelSize: 11 }
                                }
                                // Documento: qué tipos se aceptan (los de fábrica y los de los plugins
                                // que corren). Ninguno marcado = cualquiera.
                                Flow {
                                    Layout.columnSpan: 4
                                    Layout.fillWidth: true
                                    visible: filaCampo.c.tipo === "documento"
                                    spacing: 8
                                    Text {
                                        text: raiz.tv.lectorDisponible ? "Acepta:" : "Esta compilación no trae el lector de códigos: solo se puede tipear el número."
                                        color: raiz.tv.lectorDisponible ? Theme.textDim : Theme.warn
                                        font.pixelSize: 11
                                        height: 26; verticalAlignment: Text.AlignVCenter
                                    }
                                    Repeater {
                                        model: raiz.tv.tiposDocumento
                                        delegate: CheckBox {
                                            required property var modelData
                                            readonly property var aceptados: filaCampo.c.documentos || []
                                            text: modelData.titulo + (modelData.origen === "plugin" ? " (plugin)" : "")
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
                                        text: "(ninguno marcado: cualquiera)"
                                        color: Theme.textFaint; font.pixelSize: 11
                                        height: 26; verticalAlignment: Text.AlignVCenter
                                    }
                                }
                                TextField {
                                    Layout.columnSpan: 4
                                    Layout.fillWidth: true
                                    visible: filaCampo.c.tipo === "opcion"
                                    text: (filaCampo.c.opciones || []).join(", ")
                                    placeholderText: "Las opciones, separadas por comas"
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
                    text: "Registrados en este evento (" + filasRegistrados.count + ")"
                    color: Theme.text; font { pixelSize: 14; weight: Font.DemiBold }
                }
                Text {
                    Layout.fillWidth: true
                    visible: filasRegistrados.count === 0
                    wrapMode: Text.WordWrap
                    color: Theme.textFaint; font.pixelSize: 11
                    text: "Nadie todavía. Quedan en el padrón del evento, con lo que contestaron."
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
                                            text: modelData + ": " + (vt !== undefined ? vt
                                                                      : v === true ? "sí" : v === false ? "no" : String(v))
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
        title: "Proyectar el registro"
        standardButtons: Dialog.Close
        ColumnLayout {
            spacing: 8
            Text {
                Layout.preferredWidth: 380
                wrapMode: Text.WordWrap
                text: "La TV muestra el QR grande con «Escaneá para registrarte»: cada celular que lo escanea se " +
                      "registra hablando. Para que la TV misma registre (con cámara y micrófono), abrí la url en ella."
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
                text: "No hay ninguna pantalla. Buscalas en la página de Pantallas."
                color: Theme.textFaint
                font.pixelSize: 12
            }
        }
    }
}
