// Configuracion completa: secciones a la izquierda, formulario generado desde
// el esquema (SettingsModel), barra de Aplicar/Revertir cuando hay cambios.
import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import BA
import "../controls"

Rectangle {
    id: page
    color: Theme.bg
    property real leftInset: 92
    // La seccion abierta vive en Studio: asi el asistente puede llevar al
    // operador derecho a una ("andá a la de audio") en vez de explicarle donde
    // hacer clic. Vacio = la primera.
    property string section: Studio.seccionConfig !== ""
        ? Studio.seccionConfig
        : (Studio.settings.sections.length > 0 ? Studio.settings.sections[0] : "")
    readonly property string systemSection: "Sistema y datos"
    readonly property string rielSection: "Barra de navegación"
    readonly property string modelosSection: "Modelos locales"
    readonly property var ml: Studio.modelos
    readonly property var sys: Studio.system
    property string updateError: ""

    // Actualizar o volver atras cierra la app: emitiendo, grabando o con gente en
    // la sala (lobby, programa o fila de oradores) eso corta todo, asi que se advierte y se pide una confirmacion explicita.
    readonly property bool onAir: Studio.streaming || Studio.recording || Studio.conference.peopleConnected > 0
    function onAirWarning(what) {
        const people = Studio.conference.peopleConnected > 0
        const head = Studio.streaming ? "Estás AL AIRE" : Studio.recording ? "Estás GRABANDO" : "Hay gente en la sala"
        const lost = []
        if (Studio.streaming) lost.push("corta la emisión")
        if (Studio.recording) lost.push("corta la grabación")
        if (people) lost.push(lost.length > 0 ? "desconecta a quienes están en la sala" : "la desconecta")
        if (lost.length === 0) return ""
        const last = lost.pop()
        return head + ": " + what + " cierra la aplicación y " + (lost.length > 0 ? lost.join(", ") + " y " : "") + last + "."
    }
    function confirmUpdate(folder) {
        ApplicationWindow.window.confirm({
            title: "¿Actualizar ahora?",
            text: page.onAirWarning("actualizar"),
            accept: "Actualizar igual",
            reject: "Cancelar",
            action: function() { page.updateError = sys.startUpdate(folder) }
        })
    }
    function confirmRollback() {
        const data = " Configuración, credenciales, grabaciones y archivos no se tocan."
        ApplicationWindow.window.confirm({
            title: "¿Volver a la versión anterior?",
            text: page.onAir ? page.onAirWarning("volver a la versión anterior") + " Después se restaura el programa anterior y la aplicación vuelve a abrir sola." + data
                             : "La aplicación se cierra, se restaura el programa anterior y vuelve a abrir sola." + data,
            accept: "Volver a la anterior",
            reject: "Cancelar",
            action: function() { page.updateError = sys.startRollback() }
        })
    }

    RowLayout {
        anchors { fill: parent; leftMargin: page.leftInset; margins: 20; bottomMargin: Studio.settings.dirty ? 84 : 20 }
        spacing: 16
        Behavior on anchors.bottomMargin { NumberAnimation { duration: 180 } }

        Panel {
            Layout.preferredWidth: 240
            Layout.fillHeight: true
            title: "Configuracion"
            ListView {
                anchors.fill: parent
                spacing: 4
                model: Studio.settings.sections.concat([page.rielSection, page.modelosSection, page.systemSection])
                delegate: Rectangle {
                    required property string modelData
                    width: ListView.view.width
                    height: 42
                    radius: 12
                    color: modelData === page.section ? Qt.rgba(1, 0.42, 0.17, 0.25) : (sectMa.containsMouse ? Qt.rgba(1, 1, 1, 0.07) : "transparent")
                    Text { anchors { left: parent.left; leftMargin: 14; verticalCenter: parent.verticalCenter }
                           text: modelData; color: Theme.text; font.pixelSize: 13 }
                    MouseArea { id: sectMa; anchors.fill: parent; hoverEnabled: true; onClicked: Studio.seccionConfig = modelData }
                }
            }
        }

        Panel {
            Layout.fillWidth: true
            Layout.fillHeight: true
            title: page.section
            ScrollView {
                anchors.fill: parent
                clip: true
                contentWidth: availableWidth
                visible: page.section !== page.systemSection && page.section !== page.rielSection
                         && page.section !== page.modelosSection
                ColumnLayout {
                    width: parent.width
                    spacing: 14
                    Repeater {
                        model: Studio.settings
                        delegate: FormField {
                            required property string key
                            required property string section
                            required label
                            required kind
                            required value
                            required options
                            required minimum
                            required maximum
                            required suffix
                            required help
                            required modified
                            required property string scope
                            badge: scope
                            visible: section === page.section
                            Layout.fillWidth: true
                            Layout.preferredHeight: visible ? implicitHeight : 0
                            onEdited: (v) => Studio.settings.setValue(key, v)
                        }
                    }
                    GlassButton {
                        visible: page.section !== page.systemSection && page.section !== page.rielSection
                        Layout.topMargin: 10
                        text: "Valores por defecto de esta seccion"; compact: true
                        onClicked: Studio.settings.resetSection(page.section)
                    }
                }
            }
            Loader {
                anchors.fill: parent
                active: page.section === page.systemSection
                sourceComponent: systemComp
            }
            Loader {
                anchors.fill: parent
                active: page.section === page.rielSection
                sourceComponent: rielComp
            }
            Loader {
                anchors.fill: parent
                active: page.section === page.modelosSection
                sourceComponent: modelosComp
            }
        }
    }

    // --- Modelos locales -----------------------------------------------------------
    // El modelo chico que corre en la máquina (agent/ModelosLocales.hpp): se baja
    // de Hugging Face a Modelos/ (portable, verificado), se activa y se prueba
    // acá mismo. Como la barra, se aplica al toque y se guarda solo.
    Component {
        id: modelosComp
        ScrollView {
            clip: true
            contentWidth: availableWidth
            ColumnLayout {
                width: parent.width
                spacing: 14

                // Estado del motor y del modelo cargado
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 10
                    Rectangle {
                        width: 10; height: 10; radius: 5
                        color: ml.estado === "listo" ? Theme.ok
                             : ml.estado === "cargando" ? Theme.amber
                             : ml.estado === "error" ? Theme.live : Theme.textFaint
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 1
                        Text {
                            text: ml.estado === "listo" ? "Modelo listo"
                                : ml.estado === "cargando" ? "Cargando…"
                                : ml.estado === "error" ? "El modelo no anda"
                                : "Apagado"
                            color: Theme.text
                            font { pixelSize: 15; weight: Font.DemiBold }
                        }
                        Text {
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                            text: (ml.detalle !== "" ? ml.detalle + " · " : "") + "Motor: " + ml.motor
                            color: ml.estado === "error" ? Theme.live : Theme.textDim
                            font.pixelSize: 11
                        }
                    }
                    GlassButton {
                        compact: true
                        visible: ml.estado === "listo" || ml.estado === "cargando"
                        text: "Apagar"
                        onClicked: ml.apagar()
                    }
                    GlassButton { compact: true; text: "Carpeta"; icon: "📁"; onClicked: ml.abrirCarpeta() }
                }
                Text {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    color: Theme.textFaint
                    font.pixelSize: 11
                    text: "Elige la herramienta para una frase corta de cabina en una fracción de segundo, sin salir " +
                          "de la máquina. Lo que no es suyo lo deriva al asistente grande, con la frase tal cual."
                }

                Rectangle { Layout.fillWidth: true; height: 1; color: Theme.glassBorder }

                // Catálogo
                Text { text: "Modelos para esta máquina"; color: Theme.text; font { pixelSize: 15; weight: Font.DemiBold } }
                Text {
                    visible: ml.catalogo.length === 0
                    text: "No hay modelos publicados para esta plataforma."
                    color: Theme.textDim; font.pixelSize: 12
                }
                Repeater {
                    model: ml.catalogo
                    delegate: Rectangle {
                        required property var modelData
                        Layout.fillWidth: true
                        implicitHeight: fila.implicitHeight + 20
                        radius: Theme.radiusSmall
                        color: modelData.activo ? Qt.rgba(1, 0.42, 0.17, 0.12) : Theme.field
                        border.color: modelData.activo ? Theme.accent : "transparent"
                        RowLayout {
                            id: fila
                            anchors { fill: parent; margins: 10 }
                            spacing: 10
                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 2
                                Text {
                                    Layout.fillWidth: true
                                    elide: Text.ElideMiddle
                                    text: modelData.repo
                                    color: Theme.text; font.pixelSize: 13
                                }
                                Text {
                                    Layout.fillWidth: true
                                    wrapMode: Text.WordWrap
                                    text: modelData.formato.toUpperCase() + " · " + modelData.tamano_gb + " GB · " +
                                          modelData.herramientas + " herramientas · precisión " + modelData.precision +
                                          "% · examen " + modelData.examen + "% · " + modelData.fecha
                                    color: Theme.textDim; font.pixelSize: 11
                                }
                            }
                            Text {
                                visible: modelData.instalado
                                text: modelData.activo ? "activo" : "bajado"
                                color: modelData.activo ? Theme.accent : Theme.ok
                                font.pixelSize: 11
                            }
                            GlassButton {
                                compact: true
                                visible: !modelData.instalado
                                enabled: !ml.descargando
                                text: "Bajar"; icon: "⇩"
                                onClicked: ml.descargar(modelData.repo)
                            }
                            GlassButton {
                                compact: true
                                visible: modelData.instalado && ml.hayMotor
                                         && !(modelData.activo && (ml.estado === "listo" || ml.estado === "cargando"))
                                text: "Activar"; active: true
                                onClicked: ml.activar(modelData.repo)
                            }
                            GlassButton {
                                compact: true
                                visible: modelData.instalado
                                enabled: !ml.descargando
                                text: "Actualizar"
                                onClicked: ml.descargar(modelData.repo)
                            }
                            GlassButton {
                                compact: true
                                visible: modelData.instalado
                                text: "Borrar"
                                onClicked: ApplicationWindow.window.confirm({
                                    title: "¿Borrar el modelo?",
                                    text: modelData.repo + " (" + modelData.tamano_gb + " GB). Se puede volver a bajar.",
                                    accept: "Borrar", reject: "Cancelar",
                                    action: function() { ml.borrar(modelData.repo) }
                                })
                            }
                        }
                    }
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    visible: ml.descargando || ml.textoDescarga !== ""
                    spacing: 4
                    ProgressBar { Layout.fillWidth: true; visible: ml.descargando; from: 0; to: 1; value: ml.progreso }
                    RowLayout {
                        Layout.fillWidth: true
                        Text {
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                            text: ml.textoDescarga
                            color: Theme.textDim; font.pixelSize: 12
                        }
                        GlassButton { compact: true; visible: ml.descargando; text: "Cancelar"; onClicked: ml.cancelarDescarga() }
                    }
                }

                Rectangle { Layout.fillWidth: true; height: 1; color: Theme.glassBorder }

                // Ajustes
                Text { text: "Cuánto deriva"; color: Theme.text; font { pixelSize: 15; weight: Font.DemiBold } }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 10
                    Text { text: "atiende más"; color: Theme.textDim; font.pixelSize: 11 }
                    Slider {
                        Layout.fillWidth: true
                        from: -4; to: 4; stepSize: 0.5
                        value: ml.sesgo
                        onMoved: ml.sesgo = value
                    }
                    Text { text: "deriva más"; color: Theme.textDim; font.pixelSize: 11 }
                    Text { text: (ml.sesgo > 0 ? "+" : "") + ml.sesgo.toFixed(1); color: Theme.text; font.pixelSize: 12 }
                }
                Text {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    color: Theme.textFaint
                    font.pixelSize: 11
                    text: "Derivar de más cuesta segundos; elegir mal cuesta al aire. Con +2 pasa al asistente grande " +
                          "más de 9 de cada 10 pedidos que no son suyos, y atiende un poco menos de lo propio."
                }

                Text { text: "Cabeza MTP (texto rápido)"; color: Theme.text; font { pixelSize: 15; weight: Font.DemiBold } }
                RowLayout {
                    spacing: 6
                    Repeater {
                        model: [ { v: "auto", t: "Automática" }, { v: "q8", t: "8 bits" },
                                 { v: "fp16", t: "fp16" }, { v: "no", t: "Sin MTP" } ]
                        delegate: GlassButton {
                            required property var modelData
                            compact: true
                            text: modelData.t
                            active: ml.mtp === modelData.v
                            onClicked: ml.mtp = modelData.v
                        }
                    }
                }
                Text {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    color: Theme.textFaint
                    font.pixelSize: 11
                    text: "La decodificación especulativa con la cabeza del propio modelo. Si acepta poco, el motor deja " +
                          "de usarla solo. Se aplica la próxima vez que se activa el modelo."
                }
                RowLayout {
                    spacing: 10
                    Toggle { checked: ml.alArrancar; onToggled: (w) => ml.alArrancar = w }
                    Text { text: "Cargar el modelo activo al abrir la aplicación"; color: Theme.text; font.pixelSize: 13 }
                }

                Rectangle { Layout.fillWidth: true; height: 1; color: Theme.glassBorder }

                // Probar
                Text { text: "Probar"; color: Theme.text; font { pixelSize: 15; weight: Font.DemiBold } }
                RowLayout {
                    Layout.fillWidth: true
                    TextField {
                        id: fraseF
                        Layout.fillWidth: true
                        enabled: ml.estado === "listo"
                        placeholderText: ml.estado === "listo" ? "una frase de cabina: «bajale al mic 2»" : "activá un modelo para probar"
                        onAccepted: { ml.probar(text); text = "" }
                    }
                    GlassButton {
                        compact: true; text: "Elegir"; active: true
                        enabled: ml.estado === "listo" && fraseF.text.trim() !== ""
                        onClicked: { ml.probar(fraseF.text); fraseF.text = "" }
                    }
                }
                Flow {
                    Layout.fillWidth: true
                    spacing: 6
                    visible: ml.estado === "listo"
                    Repeater {
                        model: ml.info.pruebas || []
                        delegate: GlassButton {
                            required property var modelData
                            compact: true
                            text: modelData.frase
                            onClicked: ml.probar(modelData.frase)
                        }
                    }
                }
                Repeater {
                    model: ml.pruebas
                    delegate: RowLayout {
                        required property var modelData
                        Layout.fillWidth: true
                        spacing: 10
                        Text {
                            Layout.fillWidth: true
                            elide: Text.ElideRight
                            text: "«" + modelData.frase + "»"
                            color: Theme.textDim; font.pixelSize: 12
                        }
                        Text {
                            text: modelData.error !== undefined ? modelData.error
                                : modelData.derivar ? "→ derivar al grande" : "→ " + modelData.herramienta
                            color: modelData.error !== undefined ? Theme.live
                                 : modelData.derivar ? Theme.amber : Theme.ok
                            font { pixelSize: 12; family: Theme.mono }
                        }
                        Text {
                            visible: modelData.prob !== undefined
                            text: Math.round((modelData.prob || 0) * 100) + "% · " + Math.round(modelData.ms || 0) + " ms"
                            color: Theme.textFaint; font.pixelSize: 11
                        }
                    }
                }
            }
        }
    }

    // --- La barra de navegación --------------------------------------------------
    // No usa el formulario genérico: son tres cosas de forma distinta (una
    // posición, un tamaño y una lista de páginas) y cada una se entiende mejor
    // con su propio control. El cambio se ve al toque, sin Aplicar: es
    // preferencia de interfaz, no configuración del aparato, y se guarda sola
    // en Config/riel.json.
    Component {
        id: rielComp
        ScrollView {
            clip: true
            contentWidth: availableWidth
            ColumnLayout {
                width: parent.width
                spacing: 14

                Text {
                    text: "Dónde va"
                    color: Theme.text
                    font { pixelSize: 15; weight: Font.DemiBold }
                }
                RowLayout {
                    spacing: 6
                    Repeater {
                        model: [
                            { v: "izquierda", t: "Izquierda", i: "◧" },
                            { v: "derecha",   t: "Derecha",   i: "◨" },
                            { v: "arriba",    t: "Arriba",    i: "⬒" },
                            { v: "abajo",     t: "Abajo",     i: "⬓" }
                        ]
                        delegate: GlassButton {
                            required property var modelData
                            text: modelData.t
                            icon: modelData.i
                            active: Studio.riel.posicion === modelData.v
                            onClicked: Studio.riel.posicion = modelData.v
                        }
                    }
                }
                Text {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    color: Theme.textFaint
                    font.pixelSize: 11
                    text: "Arriba o abajo conviene en pantallas anchas y bajas: la barra se lleva " +
                          "menos altura, que es la que falta cuando hay una previsualización grande."
                }

                Rectangle { Layout.fillWidth: true; height: 1; color: Theme.glassBorder }

                Text {
                    text: "Tamaño"
                    color: Theme.text
                    font { pixelSize: 15; weight: Font.DemiBold }
                }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 10
                    Repeater {
                        model: [ { v: 34, t: "Chico" }, { v: 42, t: "Normal" }, { v: 54, t: "Grande" } ]
                        delegate: GlassButton {
                            required property var modelData
                            compact: true
                            text: modelData.t
                            active: Studio.riel.tamano === modelData.v
                            onClicked: Studio.riel.tamano = modelData.v
                        }
                    }
                    Slider {
                        Layout.fillWidth: true
                        from: 30; to: 64; stepSize: 2
                        value: Studio.riel.tamano
                        onMoved: Studio.riel.tamano = Math.round(value)
                    }
                    Text {
                        text: Studio.riel.tamano + " px"
                        color: Theme.textDim
                        font.pixelSize: 12
                    }
                }
                RowLayout {
                    spacing: 10
                    Toggle {
                        checked: Studio.riel.separadores
                        onToggled: (w) => Studio.riel.separadores = w
                    }
                    Text {
                        text: "Separar los grupos con una línea"
                        color: Theme.text
                        font.pixelSize: 13
                    }
                }
                RowLayout {
                    spacing: 10
                    Toggle {
                        // Solo en vertical: arriba y abajo lo que falta es
                        // altura, y un árbol ahí sería un menú colgando.
                        enabled: Studio.riel.vertical
                        checked: Studio.riel.expandir
                        onToggled: (w) => Studio.riel.expandir = w
                    }
                    ColumnLayout {
                        spacing: 1
                        Text {
                            text: "Abrir en árbol al pasarle el puntero"
                            color: Studio.riel.vertical ? Theme.text : Theme.textFaint
                            font.pixelSize: 13
                        }
                        Text {
                            text: Studio.riel.vertical
                                  ? "Se eleva y muestra el nombre de cada página. Flota: no corre la página."
                                  : "Solo con la barra a la izquierda o a la derecha."
                            color: Theme.textDim
                            font.pixelSize: 11
                        }
                    }
                }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 10
                    visible: Studio.riel.vertical && Studio.riel.expandir
                    Text {
                        text: "Ancho abierta"
                        color: Theme.textDim
                        font.pixelSize: 12
                    }
                    Slider {
                        Layout.fillWidth: true
                        from: 180; to: 360; stepSize: 4
                        value: Studio.riel.anchoAbierto
                        onMoved: Studio.riel.anchoAbierto = Math.round(value)
                    }
                    Text {
                        text: Studio.riel.anchoAbierto + " px"
                        color: Theme.textDim
                        font.pixelSize: 12
                    }
                }

                Rectangle { Layout.fillWidth: true; height: 1; color: Theme.glassBorder }

                RowLayout {
                    Layout.fillWidth: true
                    Text {
                        text: "Qué mostrar"
                        color: Theme.text
                        font { pixelSize: 15; weight: Font.DemiBold }
                    }
                    Text {
                        visible: Studio.riel.escondidas > 0
                        text: Studio.riel.escondidas + " escondida" + (Studio.riel.escondidas === 1 ? "" : "s")
                        color: Theme.textDim
                        font.pixelSize: 12
                    }
                    Item { Layout.fillWidth: true }
                    GlassButton {
                        compact: true; text: "Mostrar todas"; icon: "↺"
                        enabled: Studio.riel.escondidas > 0
                        onClicked: Studio.riel.restaurar()
                    }
                }
                Text {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    color: Theme.textFaint
                    font.pixelSize: 11
                    text: "Una página escondida sigue existiendo: el asistente la sigue usando y se " +
                          "puede abrir en su propia ventana. Lo único que se esconde es su botón."
                }

                Repeater {
                    model: Studio.riel.paginas
                    delegate: RowLayout {
                        id: fila
                        required property var modelData
                        Layout.fillWidth: true
                        spacing: 10
                        Text {
                            Layout.preferredWidth: 26
                            text: fila.modelData.icon
                            color: Theme.text
                            font.pixelSize: 16
                        }
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 0
                            Text {
                                text: fila.modelData.titulo
                                color: Theme.text
                                font.pixelSize: 13
                            }
                            Text {
                                Layout.fillWidth: true
                                elide: Text.ElideRight
                                text: fila.modelData.grupo + " · " + fila.modelData.tip
                                color: Theme.textFaint
                                font.pixelSize: 10
                            }
                        }
                        Toggle {
                            checked: fila.modelData.visible
                            // Configuración no se puede esconder: es por donde
                            // se vuelve de cualquier cosa que uno se haya hecho.
                            enabled: !fila.modelData.fija
                            onToggled: (w) => {
                                if (!w && win.page === fila.modelData.page) win.page = 0
                                Studio.riel.setVisible(fila.modelData.page, w)
                            }
                        }
                    }
                }
            }
        }
    }

    // --- Sistema y datos -------------------------------------------------------------
    Component.onCompleted: sys.refresh()
    FolderDialog {
        id: updateDlg
        title: "Carpeta de la version nueva"
        onAccepted: {
            const err = sys.checkUpdate(selectedFolder)
            page.updateError = err
            if (err === "") { updConfirm.folder = selectedFolder; updConfirm.open() }
        }
    }
    Dialog {
        id: updConfirm
        property url folder
        anchors.centerIn: parent
        modal: true
        title: "Actualizar el sistema"
        standardButtons: Dialog.Ok | Dialog.Cancel
        Column {
            width: 460
            spacing: 12
            Label {
                width: parent.width
                wrapMode: Text.WordWrap
                visible: page.onAir
                color: Theme.live
                font { pixelSize: 15; weight: Font.DemiBold }
                text: page.onAirWarning("actualizar")
            }
            Label {
                width: parent.width
                wrapMode: Text.WordWrap
                text: "La app se cierra, se reemplaza el programa por el de\n" + updConfirm.folder.toString().replace("file://", "") +
                      "\ny vuelve a abrir sola. Configuracion, credenciales, grabaciones y archivos NO se tocan (antes se respaldan). Si algo sale mal, podes volver a esta version."
            }
        }
        // Al aire no alcanza con este OK. Se mira recien aca (y no al abrir) porque la
        // emision pudo empezar, o un invitado entrar, con el dialogo ya abierto.
        onAccepted: {
            if (page.onAir) page.confirmUpdate(folder)
            else page.updateError = sys.startUpdate(folder)
        }
    }
    Component {
        id: systemComp
        ScrollView {
            clip: true
            contentWidth: availableWidth
            ColumnLayout {
                width: parent.width
                spacing: 16
                Text { Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.textDim; font.pixelSize: 13
                       text: "Todo el sistema vive en una sola carpeta (sirve desde un pendrive): " + sys.dataRoot }
                Text { Layout.fillWidth: true; color: Theme.textDim; font.pixelSize: 13; visible: sys.version !== ""
                       text: "Versión instalada: " + sys.version }
                GridLayout {
                    Layout.fillWidth: true
                    columns: 2
                    columnSpacing: 12
                    rowSpacing: 8
                    Repeater {
                        model: [
                            { n: "Config", d: "Configuracion (escenas, ajustes, tablero)" },
                            { n: "Credenciales", d: "Tokens y claves, cifrados (AES-256-GCM). Una actualizacion nunca los toca." },
                            { n: "Recordings", d: "Grabaciones" },
                            { n: "Shares", d: "Archivos que comparten los invitados" },
                            { n: "Respaldos", d: "Copias de Config/ y Credenciales/ antes de cada actualizacion (quedan 5)" },
                            { n: "Logs", d: "Registros para diagnostico" }
                        ]
                        delegate: RowLayout {
                            required property var modelData
                            Layout.columnSpan: 2
                            Layout.fillWidth: true
                            GlassButton { compact: true; Layout.preferredWidth: 150; text: modelData.n + "/"; icon: "▸"; onClicked: sys.openFolder(modelData.n) }
                            Text { text: modelData.d; color: Theme.textDim; font.pixelSize: 12; Layout.fillWidth: true; wrapMode: Text.WordWrap }
                        }
                    }
                }
                Rectangle { Layout.fillWidth: true; height: 1; color: Theme.glassBorder }
                Text { text: "Credenciales guardadas (" + sys.credentials.length + ")"; color: Theme.text; font { pixelSize: 15; weight: Font.DemiBold } }
                Flow {
                    Layout.fillWidth: true
                    spacing: 6
                    Repeater {
                        model: sys.credentials
                        delegate: StatusPill { required property string modelData; text: modelData; dot: Theme.ok }
                    }
                }
                Text { Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.textFaint; font.pixelSize: 11
                       text: "Sin contraseña: el cifrado evita que se lean por accidente (un pendrive FAT/exFAT no tiene permisos), pero quien se lleve el pendrive entero tiene la clave del equipo (Credenciales/clave-equipo). Para mudar el equipo a otro pendrive, copia la carpeta Credenciales/ completa." }
                Rectangle { Layout.fillWidth: true; height: 1; color: Theme.glassBorder }
                Text { text: "Actualizar"; color: Theme.text; font { pixelSize: 15; weight: Font.DemiBold } }
                Text { Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.textDim; font.pixelSize: 12
                       text: "Descomprimi la version nueva en cualquier carpeta (fuera de esta) y elegila. Se reemplaza solo el programa; Config/, Credenciales/, Recordings/, Shares/ y Logs/ quedan como estan." }
                RowLayout {
                    GlassButton { text: "Actualizar desde una carpeta…"; icon: "⇪"; active: true; onClicked: updateDlg.open() }
                    GlassButton { text: "Volver a la version anterior"; visible: sys.canRollback; onClicked: page.confirmRollback() }
                }
                Text { Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.live; font.pixelSize: 12; text: page.updateError; visible: text !== "" }
                Text { visible: sys.backups.length > 0; text: "Respaldos"; color: Theme.text; font { pixelSize: 15; weight: Font.DemiBold } }
                Repeater {
                    model: sys.backups
                    delegate: RowLayout {
                        required property var modelData
                        Text { text: modelData.name; color: Theme.textDim; font.pixelSize: 12; Layout.fillWidth: true }
                        GlassButton { compact: true; text: "Abrir"; onClicked: sys.openFolder("Respaldos/" + modelData.name) }
                    }
                }
            }
        }
    }

    // --- Barra de cambios pendientes ---------------------------------------------
    Rectangle {
        anchors { left: parent.left; right: parent.right; bottom: parent.bottom; leftMargin: page.leftInset + 20; rightMargin: 20; bottomMargin: 16 }
        height: 56
        radius: 18
        visible: opacity > 0
        opacity: Studio.settings.dirty ? 1 : 0
        Behavior on opacity { NumberAnimation { duration: 160 } }
        color: Qt.rgba(1, 0.42, 0.17, 0.16)
        border.color: Qt.rgba(1, 0.6, 0.35, 0.6)
        RowLayout {
            anchors { fill: parent; leftMargin: 18; rightMargin: 12 }
            Text { text: "Hay cambios sin aplicar"; color: Theme.text; font.pixelSize: 13; Layout.fillWidth: true }
            GlassButton { text: "Revertir"; onClicked: Studio.settings.revert() }
            GlassButton { text: "Aplicar"; active: true; onClicked: Studio.settings.apply() }
        }
    }
}
