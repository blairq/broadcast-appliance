// Configuracion completa: secciones a la izquierda (agrupadas: Imagen, Salida,
// Sala de invitados, Asistente, Sistema), formulario generado desde el esquema
// (SettingsModel) y, en las que lo necesitan, una vista propia. Barra de
// Aplicar/Revertir cuando hay cambios.
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
    property string section: Studio.seccionConfig !== "" ? Studio.seccionConfig : "Video"
    readonly property string systemSection: "Sistema y datos"
    readonly property string aparienciaSection: "Apariencia"
    readonly property string modelosSection: "Modelos locales"
    readonly property string vozSection: "Voz del operador"
    readonly property string traduccionSection: "Traducción"
    readonly property string controlSection: "Control de Windows"
    readonly property string pinggySection: "Pinggy"
    /// Los avanzados de la sección Pinggy (los del TURN), al final de su página.
    readonly property var clavesAvanzadasPinggy: ["turn.maxAsignaciones", "turn.maxConexiones", "turn.vidaMaxS", "turn.nonceS", "turn.colaTcpKB", "turn.credencialS"]
    /// El orden de la lista de la izquierda. Las secciones del esquema
    /// (SettingsModel) y las de vista propia, juntas por tema.
    readonly property var grupos: [
        { titulo: "Imagen y sonido", secciones: ["Video", "Cámaras y entradas", "Audio", page.traduccionSection] },
        { titulo: "Salida", secciones: ["Transmisión", "Grabación", "Codificación"] },
        { titulo: "Sala de invitados", secciones: ["Invitados", "Sala", "Red de la sala"] },
        // Las tres formas de sacar la sala a internet, cada una con su página.
        { titulo: "Acceso desde internet", secciones: [page.pinggySection, "Cloudflare", "Túnel propio (WireGuard)"] },
        { titulo: "Asistente", secciones: ["Asistente (MCP)", page.vozSection, page.modelosSection, page.controlSection] },
        { titulo: "Sistema", secciones: [page.aparienciaSection, "Plugins y descargas", page.systemSection] }
    ]
    /// Los ajustes avanzados (antes fijos en el código): escondidos salvo que se pidan;
    /// una sección que solo tiene avanzados (Audio, Plugins y descargas) los muestra siempre.
    property bool verAvanzados: false

    /// Las que tienen vista propia (el formulario del esquema, si tienen campos, va adentro).
    readonly property var propias: [page.systemSection, page.aparienciaSection, page.modelosSection,
                                    page.vozSection, page.traduccionSection, page.controlSection, page.pinggySection]
    /// Las que reinician la sala al aplicar (y ofrecen reiniciarla a mano).
    readonly property var deLaSala: ["Invitados", "Sala", "Red de la sala", "Cloudflare", "Túnel propio (WireGuard)", "Pinggy"]
    /// Lo que va arriba de las páginas de túnel: qué es y cuándo conviene.
    readonly property var introducciones: ({
        "Cloudflare": "Saca la sala a internet por un túnel de Cloudflare, con tu dominio (p. ej. sala.miorganismo.gob.ar). Hace falta una cuenta de Cloudflare con el dominio cargado y el token del túnel (Zero Trust → Networks → Tunnels → el túnel → token). El video de los de afuera va por la conexión directa o el TURN. Se usa uno solo de los tres caminos: Pinggy, Cloudflare o túnel propio.",
        "Túnel propio (WireGuard)": "El túnel común a un servidor propio con IP pública (un VPS): este equipo se conecta por WireGuard y los invitados entran directo por la IP o el dominio del servidor, sin intermediarios. Es el mejor camino para el video, pero necesita el servidor configurado. Sin permisos de administrador ni drivers. Se usa uno solo de los tres caminos: Pinggy, Cloudflare o túnel propio."
    })
    /// Subtítulos dentro de una sección: van arriba del campo con esa clave.
    readonly property var subtitulos: ({
        "video.width": "Lienzo y salida", "video.formato": "Color", "transition.type": "Transiciones",
        "mejora.activa": "Mejorar imagen", "video.legalizar": "Entradas", "entradas.sinSenal": "Cortes de señal",
        "record.dir": "Archivo", "record.calidad": "Calidad y pistas", "record.copiaDir": "Copia y repetición", "record.colchon": "Si el disco se traba",
        "encoder.video": "Video", "encoder.audioBitrate": "Audio",
        "guests.count": "Lugares", "guests.video": "Qué mandan", "gateway.videoW": "Calidad del video",
        "gateway.room": "Entrada", "room.maxBackstage": "Modo oradores", "room.vistasRemotas": "Pantallas y celulares",
        "room.pinIntentos": "Seguridad", "room.maxUploadMB": "Archivos",
        "gateway.port": "Dónde escucha", "direct.https": "Conexión directa (HTTPS y WebRTC)", "room.adminPort": "Control",
        "gateway.tunnelHost": "Dirección", "wg.endpoint": "El servidor", "wg.httpPort": "Puertos", "turn.maxAsignaciones": "Avanzado",
        "mcp.approval": "Aprobaciones", "video.esperaCorteMs": "Avanzado", "entradas.congelarMs": "Avanzado", "audio.margenMezclaMs": "Avanzado", "stream.sinDatosS": "Avanzado", "record.colchonIsoS": "Avanzado", "room.oradorSeg": "Avanzado", "room.maxConexiones": "Avanzado", "gateway.tunelReintentoMaxS": "Avanzado", "asistente.demoraArranqueMs": "Avanzado", "modelo.maxTokens": "Avanzado", "traductor.maxCaidas": "Avanzado", "vozPrompter.consultaMs": "Avanzado", "descargas.simultaneas": "Avanzado", "launcher.primerLatidoS": "Avanzado", "modelo.contexto": "El chat sin internet (pestaña Asistente)", "launcher.cuelgue": "Si se cuelga o se cae",
    })
    /// Campos que dependen de otro (se ocultan si no aplican).
    function campoVisible(k) {
        const d = Studio.settings.draft
        if (k === "pinggy.host") return d["pinggy.dominioTipo"] !== "azar"
        if (k === "pinggy.cnameDestino") return d["pinggy.dominioTipo"] === "propio"
        if (k === "pinggy.turnUdpPuerto") return d["pinggy.turnUdp"] === true
        return true
    }
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
            title: "Configuración"
            ColumnLayout {
            anchors.fill: parent
            spacing: 6
            ListView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                spacing: 2
                // [{grupo}|{seccion}] en el orden de `grupos`.
                model: {
                    const out = []
                    for (const g of page.grupos) {
                        out.push({ grupo: g.titulo })
                        for (const s of g.secciones) out.push({ seccion: s })
                    }
                    return out
                }
                delegate: Item {
                    required property var modelData
                    required property int index
                    readonly property bool esGrupo: modelData.grupo !== undefined
                    width: ListView.view.width
                    height: esGrupo ? (index === 0 ? 14 : 20) : 26
                    Text {
                        visible: parent.esGrupo
                        anchors { left: parent.left; leftMargin: 8; bottom: parent.bottom; bottomMargin: 4 }
                        text: (modelData.grupo || "").toUpperCase()
                        color: Theme.textFaint
                        font { pixelSize: 10; weight: Font.DemiBold; letterSpacing: 1.0 }
                    }
                    Rectangle {
                        visible: !parent.esGrupo
                        anchors.fill: parent
                        radius: 10
                        color: modelData.seccion === page.section ? Qt.rgba(1, 0.42, 0.17, 0.25) : (sectMa.containsMouse ? Qt.rgba(1, 1, 1, 0.07) : "transparent")
                        Text { anchors { left: parent.left; leftMargin: 14; verticalCenter: parent.verticalCenter }
                               text: modelData.seccion || ""; color: Theme.text; font.pixelSize: 13 }
                        MouseArea { id: sectMa; anchors.fill: parent; hoverEnabled: true; onClicked: Studio.seccionConfig = modelData.seccion }
                    }
                }
            }
            }
        }

        Panel {
            Layout.fillWidth: true
            Layout.fillHeight: true
            title: page.section
            // Arriba a la derecha, al lado del título: los avanzados (antes fijos en el código).
            RowLayout {
                z: 10
                anchors { top: parent.top; right: parent.right; topMargin: -34; rightMargin: 4 }
                spacing: 6
                visible: Studio.settings.tieneBasicos(page.section) && Studio.settings.tieneAvanzados(page.section)
                Text { text: "Mostrar ajustes avanzados"; color: Theme.textDim; font.pixelSize: 12 }
                Switch { checked: page.verAvanzados; onToggled: page.verAvanzados = checked }
            }
            ScrollView {
                anchors.fill: parent
                clip: true
                contentWidth: availableWidth
                visible: page.propias.indexOf(page.section) < 0
                ColumnLayout {
                    width: parent.width
                    spacing: 14
                    Text {
                        Layout.fillWidth: true
                        visible: page.introducciones[page.section] !== undefined
                        wrapMode: Text.WordWrap; color: Theme.textDim; font.pixelSize: 12
                        text: page.introducciones[page.section] || ""
                    }
                    // Cloudflare: su token, cifrado en Credenciales/ (antes solo por variable de entorno).
                    Seccion {
                        visible: page.section === "Cloudflare"
                        titulo: "Token del túnel de Cloudflare"
                        subtitulo: "Sin token el túnel no arranca aunque esté prendido. Se guarda cifrado en este equipo y no se vuelve a mostrar. Rige cuando la sala arranca."
                        Text {
                            text: Studio.accesoRemoto && Studio.accesoRemoto.tokenCloudflareGuardado ? "● Guardado" : "● Sin token"
                            color: Studio.accesoRemoto && Studio.accesoRemoto.tokenCloudflareGuardado ? Theme.ok : Theme.warn; font.pixelSize: 12
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 8
                            TextField {
                                id: cfCampo
                                Layout.fillWidth: true
                                echoMode: TextInput.Password; inputMethodHints: Qt.ImhSensitiveData | Qt.ImhNoPredictiveText
                                placeholderText: "Pegá acá el token del túnel (empieza con eyJ…)"
                            }
                            GlassButton {
                                compact: true; text: "Guardar"; active: cfCampo.text !== ""; enabled: cfCampo.text !== ""
                                onClicked: {
                                    const e = Studio.accesoRemoto.guardarTokenCloudflare(cfCampo.text)
                                    if (e === "") cfCampo.text = ""
                                    Studio.notify(e === "" ? "Token de Cloudflare guardado (cifrado). Prendé el túnel y aplicá." : e, e === "" ? "ok" : "error")
                                }
                            }
                            GlassButton {
                                compact: true; text: "Borrar"
                                enabled: !!Studio.accesoRemoto && Studio.accesoRemoto.tokenCloudflareGuardado
                                onClicked: { const e = Studio.accesoRemoto.borrarTokenCloudflare(); Studio.notify(e === "" ? "Token de Cloudflare borrado." : e, e === "" ? "ok" : "error") }
                            }
                        }
                    }
                    CamposDeSeccion { Layout.fillWidth: true; seccion: page.section }
                    GlassButton {
                        Layout.topMargin: 10
                        text: "Valores por defecto de esta sección"; compact: true
                        onClicked: Studio.settings.resetSection(page.section)
                    }
                    // Las secciones de la sala: al aplicar se reinicia sola, y acá
                    // también a mano (sin cerrar el programa).
                    RowLayout {
                        visible: page.deLaSala.indexOf(page.section) >= 0
                        Layout.fillWidth: true
                        Layout.topMargin: 4
                        spacing: 10
                        GlassButton {
                            compact: true; text: "Reiniciar la sala ahora"; icon: "↻"
                            onClicked: ApplicationWindow.window.confirm({
                                title: "¿Reiniciar la sala?",
                                text: "Se reinicia solo el servidor de la sala (gateway) con la configuración guardada. Los invitados se cortan y vuelven a entrar.",
                                accept: "Reiniciar", reject: "Cancelar",
                                action: function() {
                                    const e = Studio.conference.reiniciarSala()
                                    Studio.notify(e === "" ? "La sala se está reiniciando." : e, e === "" ? "ok" : "error")
                                }
                            })
                        }
                        Text {
                            Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.textFaint; font.pixelSize: 11
                            text: "Los cambios marcados «reinicia la sala» la reinician solos al aplicarlos; el resto del programa sigue andando."
                        }
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
                active: page.section === page.aparienciaSection
                sourceComponent: rielComp
            }
            Loader {
                anchors.fill: parent
                active: page.section === page.modelosSection
                sourceComponent: modelosComp
            }
            Loader {
                anchors.fill: parent
                active: page.section === page.vozSection
                sourceComponent: vozComp
            }
            Loader {
                anchors.fill: parent
                active: page.section === page.traduccionSection
                sourceComponent: traduccionComp
            }
            Loader {
                anchors.fill: parent
                active: page.section === page.controlSection
                sourceComponent: controlComp
            }
            Loader {
                anchors.fill: parent
                active: page.section === page.pinggySection
                sourceComponent: pinggyComp
            }
        }
    }

    // --- Los campos del esquema de una sección (con sus subtítulos) -------------------
    // El formulario genérico; lo usan las secciones del esquema y las de vista
    // propia que además tienen campos (Apariencia: el tema; Sistema y datos: el arranque).
    component CamposDeSeccion: ColumnLayout {
        id: campos
        property string seccion: ""
        /// Si no está vacía, solo estas claves (para intercalar campos con otros bloques).
        property var claves: []
        spacing: 14
        Repeater {
            model: Studio.settings
            delegate: ColumnLayout {
                id: fila
                required property string key
                required property string section
                required property string label
                required property string kind
                required property var value
                required property var options
                required property var minimum
                required property var maximum
                required property string suffix
                required property string help
                required property bool modified
                required property string scope
                required property int index
                required property bool avanzado
                visible: section === campos.seccion && (campos.claves.length === 0 || campos.claves.indexOf(key) >= 0) && (!avanzado || page.verAvanzados || !Studio.settings.tieneBasicos(section)) && page.campoVisible(key)
                Layout.fillWidth: true
                Layout.preferredHeight: visible ? implicitHeight : 0
                spacing: 8
                Text {
                    visible: page.subtitulos[fila.key] !== undefined
                    Layout.fillWidth: true
                    Layout.topMargin: 6
                    text: page.subtitulos[fila.key] || ""
                    color: Theme.text
                    font { pixelSize: 15; weight: Font.DemiBold }
                }
                FormField {
                    Layout.fillWidth: true
                    label: fila.label
                    kind: fila.kind
                    value: fila.value
                    options: fila.options
                    minimum: fila.minimum
                    maximum: fila.maximum
                    suffix: fila.suffix
                    help: fila.help
                    modified: fila.modified
                    badge: fila.scope
                    onEdited: (v) => Studio.settings.setValue(fila.key, v)
                }
            }
        }
    }

    // --- Pinggy: acceso remoto por pinggy.io (sección propia) ---------------------------
    // Estado y direcciones, conexión, token, dirección pública y DNS, video de los
    // de afuera (TURN), seguridad y monitoreo. Los campos son los del esquema
    // (sección «Pinggy», con Aplicar); el token y la API key van solo a
    // Credenciales/ (cifrados): acá se guardan o se borran, nunca se muestran.
    Component {
        id: pinggyComp
        ScrollView {
            clip: true
            contentWidth: availableWidth
            ColumnLayout {
                id: pg
                width: parent.width
                spacing: 10
                readonly property var ar: Studio.accesoRemoto
                readonly property var st: pg.ar.estado
                readonly property var pk: pg.st.pinggy || ({})
                readonly property string estado: pg.pk.estado || (pg.st.disponible ? "apagado" : "")
                readonly property var borrador: Studio.settings.draft
                readonly property bool propio: pg.borrador["pinggy.dominioTipo"] === "propio"
                Component.onCompleted: pg.ar.siguiendo = true
                Component.onDestruction: pg.ar.siguiendo = false

                function colorEstado(e) {
                    return e === "listo" ? Theme.ok : e === "conectando" || e === "reconectando" ? Theme.amber
                         : e === "error" ? Theme.live : Theme.textFaint
                }
                function textoEstado(e) {
                    return e === "listo" ? "Túnel listo: se entra desde internet"
                         : e === "conectando" ? "Conectando con pinggy…"
                         : e === "reconectando" ? "Se cortó: reconectando…"
                         : e === "error" ? "El túnel no anda"
                         : e === "apagado" ? "Túnel apagado"
                         : "Estado desconocido"
                }
                function guardarSecreto(campo, esToken) {
                    const e = esToken ? pg.ar.guardarToken(campo.text) : pg.ar.guardarApiKey(campo.text)
                    if (e !== "") { Studio.notify(e, "error"); return }
                    campo.text = ""
                    if (!esToken) { Studio.notify("API key guardada (cifrada).", "ok"); return }
                    if (pg.ar.estado.sala && Studio.settings.value("pinggy.activo") === true && !Studio.settings.dirty) {
                        ApplicationWindow.window.confirm({
                            title: "Token guardado",
                            text: "La sala lo lee al arrancar. ¿La reinicio ahora? Los invitados se cortan y vuelven a entrar.",
                            accept: "Reiniciar la sala", reject: "Después",
                            action: function() {
                                const r = Studio.conference.reiniciarSala()
                                Studio.notify(r === "" ? "La sala se está reiniciando con el token nuevo." : r, r === "" ? "ok" : "error")
                            }
                        })
                    } else {
                        Studio.notify("Token guardado (cifrado). Rige cuando la sala arranca: prendé «Túnel de pinggy» y aplicá, o reiniciá la sala.", "ok")
                    }
                }


                Text {
                    Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.textDim; font.pixelSize: 12
                    text: "Saca las páginas de la sala a internet por pinggy.io (una dirección para todo: invitados, sesión remota, público) y el video de los de afuera por un TURN propio. Lo de la red local sigue igual."
                }
                // Gratis o Pro: qué se puede y qué no (lo de Pro, según pinggy: probado solo el gratis).
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 12
                    Repeater {
                        model: [
                            { titulo: "Sin token (gratis)", color: Theme.warn, items: [
                                "Para probar: anda sin cargar nada.",
                                "La dirección es al azar, lleva la IP pública del equipo y cambia cada vez que se prende (el enlace y el QR también).",
                                "El túnel dura 60 minutos: después hay que volver a prenderlo y la dirección cambia.",
                                "A cada navegador pinggy le muestra antes un aviso («Caution – you are about to visit…») que hay que aceptar con un botón: los invitados se confunden.",
                                "Sin subdominio fijo, sin dominio propio y sin puertos fijos.",
                                "El video de los de afuera anda (por el TURN por TCP)."
                            ] },
                            { titulo: "Con token (plan Pro)", color: Theme.ok, items: [
                                "Sin límite de tiempo y sin el aviso intermedio: se entra directo.",
                                "Dirección fija: un subdominio de pinggy o el dominio de la organización (con un CNAME; el certificado lo pone pinggy).",
                                "Puertos fijos para el TURN, si se reservan.",
                                "La API key permite ver desde acá los túneles activos de la cuenta.",
                                "Un túnel por token: cada equipo necesita su propio token.",
                                "Se configura en dashboard.pinggy.io: el token del plan y la API key van abajo, cada uno en su lugar."
                            ] }
                        ]
                        delegate: Rectangle {
                            required property var modelData
                            Layout.fillWidth: true
                            Layout.alignment: Qt.AlignTop
                            Layout.preferredHeight: col.implicitHeight + 20
                            radius: 10
                            color: Qt.rgba(1, 1, 1, 0.03)
                            border.color: modelData.color
                            border.width: 1
                            ColumnLayout {
                                id: col
                                anchors { left: parent.left; right: parent.right; top: parent.top; margins: 10 }
                                spacing: 4
                                Text { text: modelData.titulo; color: modelData.color; font { pixelSize: 14; weight: Font.DemiBold } }
                                Repeater {
                                    model: modelData.items
                                    delegate: Text {
                                        required property string modelData
                                        Layout.fillWidth: true; wrapMode: Text.WordWrap
                                        text: "• " + modelData; color: Theme.textDim; font.pixelSize: 12
                                    }
                                }
                            }
                        }
                    }
                }
                Text {
                    Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.textFaint; font.pixelSize: 11
                    text: "En los dos: el tráfico no tiene tope y todo pasa por los servidores de pinggy (desde Argentina, la región automática da São Paulo, unos 200 ms de ida y vuelta). La red local no cambia. Si hay un servidor propio con WireGuard, para el video sigue siendo mejor camino. Probado en este programa: el plan gratis; lo de Pro es lo que publica pinggy."
                }
                Text {
                    Layout.topMargin: 14
                    text: "Estado"; color: Theme.text; font { pixelSize: 16; weight: Font.DemiBold }
                }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 10
                    Rectangle { width: 10; height: 10; radius: 5; color: pg.colorEstado(pg.estado) }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 1
                        Text { text: pg.textoEstado(pg.estado); color: Theme.text; font { pixelSize: 14; weight: Font.DemiBold } }
                        Text {
                            visible: !pg.st.disponible && (pg.st.texto || "") !== ""
                            Layout.fillWidth: true; wrapMode: Text.WordWrap
                            text: pg.st.texto || ""; color: Theme.textDim; font.pixelSize: 11
                        }
                        Text {
                            visible: (pg.pk.error || "") !== ""
                            Layout.fillWidth: true; wrapMode: Text.WordWrap
                            text: "Último error: " + (pg.pk.error || ""); color: Theme.live; font.pixelSize: 11
                        }
                    }
                    GlassButton { compact: true; text: "Actualizar"; icon: "↻"; onClicked: pg.ar.refrescar() }
                }
                // La dirección pública y la de cada página, copiables.
                Repeater {
                    model: pg.ar.enlaces
                    delegate: RowLayout {
                        required property var modelData
                        Layout.fillWidth: true
                        spacing: 8
                        Text { Layout.preferredWidth: 190; text: modelData.nombre; color: Theme.textDim; font.pixelSize: 12; elide: Text.ElideRight }
                        TextEdit {
                            Layout.fillWidth: true; readOnly: true; selectByMouse: true; wrapMode: TextEdit.WrapAnywhere
                            text: modelData.url; color: Theme.text; font { pixelSize: 12; family: Theme.mono }
                        }
                        GlassButton { compact: true; text: "Copiar"; onClicked: Studio.copiar(modelData.url) }
                    }
                }
                Text {
                    visible: (pg.pk.urls || []).length > 1
                    Layout.fillWidth: true; wrapMode: Text.WrapAnywhere
                    text: "Direcciones del túnel: " + (pg.pk.urls || []).join("  ·  ")
                    color: Theme.textFaint; font.pixelSize: 11
                }
                Text {
                    visible: pg.pk.turn !== undefined && (pg.pk.turn.publico || "") !== ""
                    Layout.fillWidth: true; wrapMode: Text.WrapAnywhere
                    text: pg.pk.turn ? "TURN (video de los de afuera): " + pg.pk.turn.publico + (pg.pk.turn.tipo ? " · " + pg.pk.turn.tipo : "")
                                       + (typeof pg.pk.turn.udp === "string" && pg.pk.turn.udp !== "" ? " · UDP: " + pg.pk.turn.udp
                                          : pg.pk.turn.udp === true ? " · también UDP" : " · solo TCP") : ""
                    color: Theme.textDim; font.pixelSize: 11
                }

                Text {
                    Layout.topMargin: 14
                    text: "Conexión"; color: Theme.text; font { pixelSize: 16; weight: Font.DemiBold }
                }
                CamposDeSeccion { Layout.fillWidth: true; seccion: "Pinggy"; claves: ["pinggy.activo", "pinggy.region"] }
                // Las dos claves de pinggy, juntas y bien separadas: no son lo mismo y
                // pegar una en el lugar de la otra era el error de siempre.
                Text {
                    Layout.topMargin: 14
                    text: "Las dos claves de pinggy"; color: Theme.text; font { pixelSize: 16; weight: Font.DemiBold }
                }
                Text {
                    Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.textDim; font.pixelSize: 12
                    textFormat: Text.StyledText; linkColor: Theme.accent
                    onLinkActivated: (l) => Qt.openUrlExternally(l)
                    text: "Las dos salen de <a href=\"https://dashboard.pinggy.io\">dashboard.pinggy.io</a> pero son <b>distintas</b>: "
                          + "el <b>token</b> abre el túnel con tu plan; la <b>API key</b> deja que la app consulte y maneje tu cuenta por la API. "
                          + "Se guardan cifradas en este equipo y no se vuelven a mostrar."
                }
                Rectangle {
                    Layout.fillWidth: true
                    visible: pg.ar.clavesIguales
                    implicitHeight: igualesTxt.implicitHeight + 16
                    radius: 8; color: Qt.rgba(1, 0.23, 0.19, 0.12); border.color: Theme.live
                    Text {
                        id: igualesTxt
                        anchors { fill: parent; margins: 8 }
                        wrapMode: Text.WordWrap; color: Theme.live; font.pixelSize: 12
                        text: "El token y la API key guardados son la MISMA clave: casi seguro pegaste la API key en el lugar del token. Así el túnel no abre (pinggy contesta «no autenticado»). Pegá en «Token del plan» el token del comando del túnel."
                    }
                }
                GridLayout {
                    Layout.fillWidth: true
                    columns: width > 900 ? 2 : 1
                    columnSpacing: 12; rowSpacing: 12
                    // --- 1. El token del plan ---
                    Seccion {
                        Layout.alignment: Qt.AlignTop
                        titulo: "1 · Token del plan — abre el túnel"
                        subtitulo: "Con él, el túnel usa tu plan Pro: dirección fija (tu subdominio o tu dominio), sin el cartel de aviso y sin el límite de 60 minutos."
                        resaltado: pg.ar.clavesIguales ? Theme.live : "transparent"
                        Text {
                            text: pg.ar.tokenGuardado ? "● Guardado" : "● Sin token: modo gratis (60 minutos, dirección al azar, con el aviso de pinggy)"
                            color: pg.ar.tokenGuardado ? (pg.ar.clavesIguales ? Theme.live : Theme.ok) : Theme.warn; font.pixelSize: 12
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 8
                            TextField {
                                id: tokenCampo
                                Layout.fillWidth: true
                                echoMode: TextInput.Password; inputMethodHints: Qt.ImhSensitiveData | Qt.ImhNoPredictiveText
                                placeholderText: pg.ar.tokenGuardado ? "Pegá uno nuevo para reemplazarlo" : "Pegá acá el token del plan"
                                onAccepted: if (text !== "") pg.guardarSecreto(tokenCampo, true)
                            }
                            GlassButton { compact: true; text: "Guardar"; active: tokenCampo.text !== ""; enabled: tokenCampo.text !== ""; onClicked: pg.guardarSecreto(tokenCampo, true) }
                            GlassButton {
                                compact: true; text: "Borrar"; enabled: pg.ar.tokenGuardado
                                onClicked: ApplicationWindow.window.confirm({
                                    title: "¿Borrar el token de pinggy?",
                                    text: "Cuando la sala vuelva a arrancar, el túnel queda en modo gratis hasta que guardes otro.",
                                    accept: "Borrar", reject: "Cancelar",
                                    action: function() { const e = pg.ar.borrarToken(); Studio.notify(e === "" ? "Token borrado." : e, e === "" ? "ok" : "error") }
                                })
                            }
                        }
                        Text {
                            Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.textFaint; font.pixelSize: 11
                            text: "Dónde está: en el panel de pinggy, el token de acceso que aparece en el comando para abrir un túnel (ssh … <token>@pro.pinggy.io). Uno por equipo. Rige cuando la sala arranca."
                        }
                    }
                    // --- 2. La API key ---
                    Seccion {
                        Layout.alignment: Qt.AlignTop
                        titulo: "2 · API key — control de la cuenta por API"
                        subtitulo: "Deja que la app consulte tu cuenta de pinggy (qué túneles están activos, con qué dirección y puertos). No abre túneles: el túnel anda sin ella."
                        Text {
                            text: pg.ar.apiKeyGuardada ? "● Guardada" : "● Sin API key (opcional)"
                            color: pg.ar.apiKeyGuardada ? (pg.ar.clavesIguales ? Theme.live : Theme.ok) : Theme.textFaint; font.pixelSize: 12
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 8
                            TextField {
                                id: apiCampo
                                Layout.fillWidth: true
                                echoMode: TextInput.Password; inputMethodHints: Qt.ImhSensitiveData | Qt.ImhNoPredictiveText
                                placeholderText: pg.ar.apiKeyGuardada ? "Pegá una nueva para reemplazarla" : "Pegá acá la API key"
                                onAccepted: if (text !== "") pg.guardarSecreto(apiCampo, false)
                            }
                            GlassButton { compact: true; text: "Guardar"; active: apiCampo.text !== ""; enabled: apiCampo.text !== ""; onClicked: pg.guardarSecreto(apiCampo, false) }
                            GlassButton {
                                compact: true; text: "Borrar"; enabled: pg.ar.apiKeyGuardada
                                onClicked: { const e = pg.ar.borrarApiKey(); Studio.notify(e === "" ? "API key borrada." : e, e === "" ? "ok" : "error") }
                            }
                            GlassButton {
                                compact: true; text: "Ver en pinggy"; enabled: pg.ar.apiKeyGuardada && pg.ar.panel.estado !== "consultando"
                                onClicked: pg.ar.verEnPinggy()
                            }
                        }
                        Text {
                            Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.textFaint; font.pixelSize: 11
                            text: "Dónde está: en el panel de pinggy, en la parte de API keys (se crea ahí). «Ver en pinggy» muestra lo que la cuenta tiene activo, visto desde pinggy."
                        }
                        Text {
                            visible: (pg.ar.panel.texto || "") !== ""
                            Layout.fillWidth: true; wrapMode: Text.WrapAnywhere
                            text: pg.ar.panel.texto || ""
                            color: pg.ar.panel.estado === "error" ? Theme.live : Theme.textDim
                            font { pixelSize: 11; family: pg.ar.panel.estado === "ok" ? Theme.mono : Theme.font }
                        }
                    }
                }

                Text {
                    Layout.topMargin: 14
                    text: "Dirección pública"; color: Theme.text; font { pixelSize: 16; weight: Font.DemiBold }
                }
                CamposDeSeccion { Layout.fillWidth: true; seccion: "Pinggy"; claves: ["pinggy.dominioTipo", "pinggy.host", "pinggy.cnameDestino"] }
                Text {
                    Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.textFaint; font.pixelSize: 11
                    text: pg.borrador["pinggy.dominioTipo"] === "azar"
                          ? "Al azar cambia cada vez que arranca el túnel (y el QR con él). Para una dirección fija: subdominio fijo de pinggy o dominio propio (plan Pro)."
                          : pg.borrador["pinggy.dominioTipo"] === "pinggy"
                          ? "El subdominio se crea en dashboard.pinggy.io → Domains y se asigna al token (queda del estilo nombre.a.pinggy.link). Escribilo tal cual y probalo."
                          : "Tu dominio (p. ej. sala.miorganismo.gob.ar): se carga en dashboard.pinggy.io → Domains, se asigna al token y se apunta con un CNAME (abajo, las instrucciones)."
                }
                // Probar la dirección: si pinggy la da con este token (asignada y libre).
                RowLayout {
                    visible: pg.borrador["pinggy.dominioTipo"] !== "azar"
                    spacing: 8
                    GlassButton {
                        compact: true; text: "Probar dirección"; icon: "✓"
                        enabled: pg.ar.prueba.estado !== "probando"
                        onClicked: pg.ar.probarDireccion(pg.borrador["pinggy.dominioTipo"] || "", pg.borrador["pinggy.host"] || "")
                    }
                    Text {
                        Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.textFaint; font.pixelSize: 11
                        text: "Abre un túnel de prueba de unos segundos con tu token y se fija si pinggy te da esa dirección (no la tiene otro túnel y está asignada a tu token). Con el túnel de este equipo apagado."
                    }
                }
                Text {
                    visible: (pg.ar.prueba.texto || "") !== "" && pg.borrador["pinggy.dominioTipo"] !== "azar"
                    Layout.fillWidth: true; wrapMode: Text.WordWrap; font.pixelSize: 12
                    text: pg.ar.prueba.texto || ""
                    color: pg.ar.prueba.estado === "ok" ? Theme.ok : pg.ar.prueba.estado === "probando" ? Theme.textDim
                         : pg.ar.prueba.estado === "error" ? Theme.live : Theme.warn
                }
                ColumnLayout {
                    visible: pg.propio
                    Layout.fillWidth: true
                    spacing: 8
                    Text {
                        Layout.topMargin: 6
                        text: "DNS del dominio propio"; color: Theme.text; font { pixelSize: 14; weight: Font.DemiBold }
                    }
                    Text {
                        Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.textDim; font.pixelSize: 12
                        text: "Esto es lo que tiene que hacer quien administra el dominio. Se arma con lo que cargaste arriba: copialo y mandáselo."
                    }
                    TextArea {
                        id: instrucciones
                        Layout.fillWidth: true
                        readOnly: true; selectByMouse: true; wrapMode: TextEdit.Wrap
                        font { pixelSize: 12; family: Theme.mono }
                        color: Theme.text
                        background: Rectangle { color: Theme.field; radius: 6 }
                        text: pg.propio ? pg.ar.instruccionesDns(pg.borrador["pinggy.host"] || "", pg.borrador["pinggy.cnameDestino"] || "") : ""
                    }
                    RowLayout {
                        spacing: 8
                        GlassButton { compact: true; text: "Copiar instrucciones"; onClicked: Studio.copiar(instrucciones.text) }
                        GlassButton {
                            compact: true; text: "Verificar DNS"
                            enabled: pg.ar.dns.estado !== "verificando"
                            onClicked: pg.ar.verificarDns(pg.borrador["pinggy.host"] || "", pg.borrador["pinggy.cnameDestino"] || "")
                        }
                    }
                    Text {
                        visible: (pg.ar.dns.texto || "") !== ""
                        Layout.fillWidth: true; wrapMode: Text.WordWrap; font.pixelSize: 12
                        text: pg.ar.dns.texto || ""
                        color: pg.ar.dns.estado === "ok" ? Theme.ok : pg.ar.dns.estado === "verificando" ? Theme.textDim
                             : pg.ar.dns.estado === "error" ? Theme.live : Theme.warn
                    }
                }
                Text {
                    Layout.topMargin: 14
                    text: "Video de los invitados de afuera"; color: Theme.text; font { pixelSize: 16; weight: Font.DemiBold }
                }
                Text {
                    Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.textDim; font.pixelSize: 12
                    text: "Por el túnel solo pasa TCP o UDP hacia un TURN dentro de esta caja: el video de los de afuera entra por ahí (cifrado de punta a punta). Sin esto ven la página pero no se conectan con video."
                }
                CamposDeSeccion { Layout.fillWidth: true; seccion: "Pinggy"; claves: ["pinggy.turn", "pinggy.turnTcpPuerto", "pinggy.turnUdp", "pinggy.turnUdpPuerto"] }
                Text {
                    Layout.topMargin: 14
                    text: "Seguridad"; color: Theme.text; font { pixelSize: 16; weight: Font.DemiBold }
                }
                CamposDeSeccion { Layout.fillWidth: true; seccion: "Pinggy"; claves: ["pinggy.listaBlanca", "pinggy.puertoTunel"] }
                Text {
                    Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.textDim; font.pixelSize: 12
                    text: "Lo que entra por el túnel cuenta siempre como «de afuera»: la botonera no se abre desde internet y la API de control y el asistente nunca pasan por el túnel."
                }
                CamposDeSeccion { Layout.fillWidth: true; seccion: "Pinggy"; claves: page.clavesAvanzadasPinggy }
                RowLayout {
                    Layout.topMargin: 10
                    spacing: 10
                    GlassButton { compact: true; text: "Valores por defecto de esta sección"; onClicked: Studio.settings.resetSection("Pinggy") }
                    GlassButton {
                        compact: true; text: "Reiniciar la sala ahora"; icon: "↻"
                        onClicked: ApplicationWindow.window.confirm({
                            title: "¿Reiniciar la sala?",
                            text: "Se reinicia solo el servidor de la sala (y el túnel) con la configuración guardada. Los invitados se cortan y vuelven a entrar.",
                            accept: "Reiniciar", reject: "Cancelar",
                            action: function() {
                                const e = Studio.conference.reiniciarSala()
                                Studio.notify(e === "" ? "La sala se está reiniciando." : e, e === "" ? "ok" : "error")
                            }
                        })
                    }
                }
            }
        }
    }

    // --- Control de Windows --------------------------------------------------------
    // ba-control (src/app/escritorio): el servidor MCP con el que el asistente
    // maneja el escritorio de esta máquina. Los permisos viven en
    // Config/control-windows.json (agent/ControlWindows) y se aplican en la
    // llamada siguiente; prender o apagar todo, o «preguntar», al relanzar.
    Component {
        id: controlComp
        ScrollView {
            clip: true
            contentWidth: availableWidth
            ColumnLayout {
                id: ctl
                width: parent.width
                spacing: 12
                property var estado: Studio.agent.controlWindows()
                function cambiar(c) {
                    const e = Studio.agent.setControlWindows(c)
                    if (e !== "") Studio.notify(e, "error")
                    ctl.estado = Studio.agent.controlWindows()
                }
                Text {
                    Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.textDim; font.pixelSize: 12
                    text: "Le permite al asistente (Claude, Gemini, opencode) manejar el escritorio de esta máquina: ver la pantalla, "
                          + "mover el mouse, escribir, abrir programas, buscar archivos y acomodar ventanas. De fábrica está apagado."
                }
                Text {
                    visible: !ctl.estado.windows
                    Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.warn; font.pixelSize: 12
                    text: "Este sistema no es Windows: estos permisos se guardan pero el control no hace nada acá."
                }
                RowLayout {
                    Layout.fillWidth: true
                    Text { Layout.fillWidth: true; text: "Permitir el control de Windows"; color: Theme.text; font { pixelSize: 14; weight: Font.DemiBold } }
                    Toggle { checked: ctl.estado.habilitado === true; onToggled: (quiero) => ctl.cambiar({ habilitado: quiero }) }
                }
                RowLayout {
                    Layout.fillWidth: true
                    enabled: ctl.estado.habilitado === true
                    opacity: enabled ? 1 : 0.5
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 2
                        Text { text: "Preguntar antes de cada acción"; color: Theme.text; font.pixelSize: 13 }
                        Text {
                            Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.textFaint; font.pixelSize: 11
                            text: "El asistente le pide permiso al operador en su consola antes de cada clic, tecla o programa. "
                                  + "Apagado, actúa solo (opencode solo recibe el control si esto está apagado: no sabe preguntar)."
                        }
                    }
                    Toggle { checked: ctl.estado.preguntar === true; onToggled: (quiero) => ctl.cambiar({ preguntar: quiero }) }
                }
                Text {
                    Layout.topMargin: 6
                    text: "Qué puede hacer"; color: Theme.text; font { pixelSize: 13; weight: Font.DemiBold }
                }
                Repeater {
                    model: ctl.estado.capacidades || []
                    delegate: RowLayout {
                        required property var modelData
                        Layout.fillWidth: true
                        enabled: ctl.estado.habilitado === true
                        opacity: enabled ? 1 : 0.5
                        Text { Layout.fillWidth: true; text: modelData.etiqueta; color: Theme.text; font.pixelSize: 13; wrapMode: Text.WordWrap }
                        Toggle {
                            checked: ctl.estado.permisos !== undefined && ctl.estado.permisos[modelData.clave] === true
                            onToggled: (quiero) => { const p = {}; p[modelData.clave] = quiero; ctl.cambiar({ permisos: p }) }
                        }
                    }
                }
                Text {
                    Layout.topMargin: 6
                    Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.textFaint; font.pixelSize: 11
                    text: "Los permisos valen enseguida. Prender o apagar el control, o «preguntar», vale al relanzar el asistente."
                }
                GlassButton {
                    compact: true; text: "Relanzar el asistente"
                    onClicked: Studio.agent.restart()
                }
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
                        id: modeloFila
                        required property var modelData
                        // Con qué herramientas se entrenó (su modelo.json), contra las de hoy.
                        readonly property var ficha: modelData.tipo === "reactivo" ? (ml.fichas[modelData.repo] || null) : null
                        property bool verFicha: false
                        Component.onCompleted: if (modelData.tipo === "reactivo") ml.revisarHerramientas(modelData.repo, false)
                        // Las voces que contestan van en su propia sección.
                        visible: modelData.tipo !== "tts" && modelData.tipo !== "traduccion"
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
                                    text: modelData.nombre !== undefined ? modelData.nombre + "  ·  " + modelData.repo : modelData.repo
                                    color: Theme.text; font.pixelSize: 13
                                }
                                Text {
                                    Layout.fillWidth: true
                                    wrapMode: Text.WordWrap
                                    text: modelData.tipo === "voz"
                                          ? "Voz a texto · " + modelData.idiomas + " · " + modelData.tamano_gb + " GB · " +
                                            "error en castellano " + modelData.wer_es_fleurs + "% (FLEURS) · " + modelData.licencia
                                        : modelData.tipo === "vad"
                                          ? "Detector de voz (recorta los silencios) · " + modelData.licencia
                                        : modelData.tipo === "chat"
                                          ? "Para el chat sin internet (Asistente → Modelo local) · " + modelData.formato.toUpperCase() +
                                            " · " + modelData.tamano_gb + " GB · " + modelData.base + " · " + modelData.licencia
                                        : modelData.formato.toUpperCase() + " · " + modelData.tamano_gb + " GB · " +
                                          (modeloFila.ficha && modeloFila.ficha.estado === "ok" ? "" : modelData.herramientas + " herramientas (según el catálogo) · ") +
                                          (modelData.precision !== undefined
                                           ? "precisión " + modelData.precision + "% · examen " + modelData.examen + "% · "
                                           : "llamada nativa (v2) · ") + modelData.fecha
                                    color: Theme.textDim; font.pixelSize: 11
                                }
                                // --- Herramientas con las que se entrenó ---
                                RowLayout {
                                    visible: !!modeloFila.ficha
                                    Layout.fillWidth: true
                                    spacing: 8
                                    Text {
                                        Layout.fillWidth: true
                                        wrapMode: Text.WordWrap
                                        font.pixelSize: 11
                                        readonly property var f: modeloFila.ficha || ({})
                                        readonly property int problemas: f.estado === "ok" ? (f.cambiaron.length + f.no_estan.length) : 0
                                        color: f.estado === "ok" ? (problemas > 0 ? Theme.amber : Theme.ok)
                                             : f.estado === "error" ? Theme.live : Theme.textFaint
                                        text: {
                                            if (f.estado === "buscando") return "Revisando con qué herramientas se entrenó…"
                                            if (f.estado === "sin_ficha") return "Sin modelo.json: no se sabe con qué herramientas se entrenó" + (f.nota ? " — " + f.nota : ".")
                                            if (f.estado === "error") return "No se pudo revisar: " + (f.nota || "")
                                            if (f.estado !== "ok") return ""
                                            let t = "Entrenado con " + f.entrenadas + " herramientas"
                                            if (f.catalogo_dice !== undefined && String(f.catalogo_dice) !== String(f.entrenadas))
                                                t += " (el catálogo decía " + f.catalogo_dice + ")"
                                            t += " · " + f.iguales + " iguales a las de hoy"
                                            if (f.cambiaron.length) t += " · " + f.cambiaron.length + " cambiaron"
                                            if (f.no_estan.length) t += " · " + f.no_estan.length + " ya no existen"
                                            t += " · conoce " + f.iguales + " de las " + f.herramientas_app + " del sistema (" + Math.round(f.cobertura * 100) + " %)"
                                            return t
                                        }
                                    }
                                    GlassButton {
                                        compact: true
                                        visible: !!modeloFila.ficha && modeloFila.ficha.estado === "ok"
                                        text: modeloFila.verFicha ? "Ocultar" : "Ver cuáles"
                                        onClicked: modeloFila.verFicha = !modeloFila.verFicha
                                    }
                                    GlassButton {
                                        compact: true
                                        visible: !!modeloFila.ficha && modeloFila.ficha.estado !== "buscando"
                                        text: "↻"
                                        onClicked: ml.revisarHerramientas(modeloFila.modelData.repo, true)
                                        ToolTip.visible: hovered; ToolTip.delay: 400; ToolTip.text: "Volver a buscar el modelo.json en Hugging Face"
                                    }
                                }
                                ColumnLayout {
                                    visible: modeloFila.verFicha && !!modeloFila.ficha && modeloFila.ficha.estado === "ok"
                                    Layout.fillWidth: true
                                    spacing: 4
                                    readonly property var f: modeloFila.ficha && modeloFila.ficha.estado === "ok" ? modeloFila.ficha : null
                                    Text {
                                        Layout.fillWidth: true; wrapMode: Text.WordWrap
                                        visible: !!parent.f
                                        text: !parent.f ? "" : "Por tema: " + Object.keys(parent.f.por_categoria).sort((a, b) => parent.f.por_categoria[b] - parent.f.por_categoria[a])
                                              .map(k => k + " " + parent.f.por_categoria[k]).join(" · ")
                                        color: Theme.textDim; font.pixelSize: 11
                                    }
                                    Text {
                                        Layout.fillWidth: true; wrapMode: Text.WordWrap
                                        visible: !!parent.f && parent.f.cambiaron.length > 0
                                        text: !parent.f ? "" : "Cambiaron desde el entrenamiento (las puede llamar mal): " + parent.f.cambiaron.join(", ")
                                        color: Theme.amber; font.pixelSize: 11
                                    }
                                    Text {
                                        Layout.fillWidth: true; wrapMode: Text.WordWrap
                                        visible: !!parent.f && parent.f.no_estan.length > 0
                                        text: !parent.f ? "" : "Ya no existen en el sistema: " + parent.f.no_estan.join(", ")
                                        color: Theme.amber; font.pixelSize: 11
                                    }
                                    Text {
                                        Layout.fillWidth: true; wrapMode: Text.WordWrap
                                        visible: !!parent.f
                                        text: !parent.f ? "" : "Las que conoce tal cual: " + parent.f.lista.join(", ")
                                        color: Theme.textFaint; font { pixelSize: 10; family: Theme.mono }
                                    }
                                    Text {
                                        Layout.fillWidth: true; wrapMode: Text.WordWrap
                                        visible: !!parent.f
                                        text: !parent.f ? "" : "Versión " + (parent.f.build || "?") + " · " + (parent.f.fecha || "") +
                                              (parent.f.inventario_entrenamiento ? " · el sistema tenía " + parent.f.inventario_entrenamiento.cuantas + " herramientas al entrenarlo" : "") +
                                              " · de " + parent.f.de + (parent.f.nota ? ". " + parent.f.nota : "")
                                        color: Theme.textFaint; font.pixelSize: 10
                                    }
                                }
                            }
                            Text {
                                visible: modelData.instalado
                                text: modelData.activo ? "activo" : modelData.tipo !== "reactivo" ? "listo" : "bajado"
                                color: modelData.activo ? Theme.accent : Theme.ok
                                font.pixelSize: 11
                            }
                            GlassButton {
                                compact: true
                                visible: !modelData.instalado
                                enabled: ml.enCola.indexOf(modelData.repo) < 0
                                text: enabled ? "Bajar" : "En la cola"; icon: "⇩"
                                onClicked: Studio.notify(modelData.repo + ": " + ml.descargar(modelData.repo), "info")
                            }
                            GlassButton {
                                compact: true
                                visible: modelData.tipo === "reactivo" && modelData.instalado && ml.hayMotor
                                         && !(modelData.activo && (ml.estado === "listo" || ml.estado === "cargando"))
                                text: "Activar"; active: true
                                onClicked: ml.activar(modelData.repo)
                            }
                            GlassButton {
                                compact: true
                                visible: modelData.instalado
                                enabled: ml.enCola.indexOf(modelData.repo) < 0
                                text: "Actualizar"
                                onClicked: Studio.notify(modelData.repo + ": " + ml.descargar(modelData.repo), "info")
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
                        GlassButton { compact: true; text: "Descargas…"; onClicked: ApplicationWindow.window.abrirDescargas() }
                    }
                }

                Rectangle { Layout.fillWidth: true; height: 1; color: Theme.glassBorder }

                // EN EL DISCO: todo lo bajado, esté o no en el catálogo (un modelo que
                // salió del catálogo seguía ocupando gigas sin aparecer en ningún lado).
                ColumnLayout {
                    id: disco
                    Layout.fillWidth: true
                    spacing: 6
                    readonly property var viejos: ml.instalados.filter(m => m.viejo)
                    function gb(b) { return b >= 1073741824 ? (b / 1073741824).toFixed(2) + " GB" : Math.round(b / 1048576) + " MB" }
                    function suma(l) { let t = 0; for (const m of l) t += m.bytes; return t }
                    Component.onCompleted: ml.revisarDisco()
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 10
                        Text {
                            text: "En el disco"
                            color: Theme.text; font { pixelSize: 15; weight: Font.DemiBold }
                        }
                        Text {
                            Layout.fillWidth: true
                            text: ml.instalados.length + " modelos · " + disco.gb(disco.suma(ml.instalados))
                            color: Theme.textDim; font.pixelSize: 12
                        }
                        GlassButton { compact: true; text: "Revisar"; onClicked: ml.revisarDisco() }
                        GlassButton {
                            compact: true
                            visible: disco.viejos.length > 0
                            text: "Borrar los viejos (" + disco.viejos.length + " · " + disco.gb(disco.suma(disco.viejos)) + ")"
                            active: true
                            onClicked: ApplicationWindow.window.confirm({
                                title: "¿Borrar los modelos viejos?",
                                text: disco.viejos.map(m => m.repo + " — " + m.motivo).join("\n") +
                                      "\n\nSi siguen en el catálogo se pueden volver a bajar.",
                                accept: "Borrar", reject: "Cancelar",
                                action: function() { ml.limpiarViejos() }
                            })
                        }
                        GlassButton {
                            compact: true
                            visible: (ml.restos.cuantas || 0) > 0
                            text: "Borrar descargas cortadas (" + disco.gb(ml.restos.bytes || 0) + ")"
                            onClicked: ml.limpiarRestos()
                        }
                    }
                    Repeater {
                        model: ml.instalados
                        delegate: RowLayout {
                            required property var modelData
                            Layout.fillWidth: true
                            spacing: 8
                            Text {
                                Layout.preferredWidth: 70
                                text: disco.gb(modelData.bytes)
                                horizontalAlignment: Text.AlignRight
                                color: Theme.textDim; font.pixelSize: 12
                            }
                            Text {
                                Layout.fillWidth: true
                                elide: Text.ElideMiddle
                                text: modelData.repo
                                color: modelData.viejo ? Theme.textDim : Theme.text
                                font.pixelSize: 12
                            }
                            Text {
                                text: modelData.activo ? "en uso"
                                    : modelData.viejo ? "viejo: " + modelData.motivo
                                    : !modelData.enCatalogo ? "fuera del catálogo"
                                    : modelData.tipo
                                color: modelData.activo ? Theme.ok : modelData.viejo ? Theme.amber : Theme.textFaint
                                font.pixelSize: 11
                            }
                            GlassButton {
                                compact: true
                                text: "Borrar"
                                onClicked: ApplicationWindow.window.confirm({
                                    title: "¿Borrar «" + modelData.repo + "»?",
                                    text: disco.gb(modelData.bytes) + (modelData.activo ? ". Es el que está en uso: se apaga." : ".") +
                                          (modelData.enCatalogo ? " Se puede volver a bajar." : " Ya no está en el catálogo: no se puede volver a bajar desde acá."),
                                    accept: "Borrar", reject: "Cancelar",
                                    action: function() { ml.borrarCarpeta(modelData.carpeta) }
                                })
                            }
                        }
                    }
                }

                Rectangle { Layout.fillWidth: true; height: 1; color: Theme.glassBorder }

                // Ajustes
                // Los del esquema (el chat sin internet: contexto y capas en la GPU), con Aplicar.
                CamposDeSeccion { Layout.fillWidth: true; seccion: page.modelosSection }
                Rectangle { Layout.fillWidth: true; height: 1; color: Theme.glassBorder }
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
                RowLayout {
                    Layout.fillWidth: true
                    visible: ml.estado === "listo" && ml.info.vision === true
                    TextField {
                        id: preguntaF
                        Layout.fillWidth: true
                        placeholderText: "pregunta sobre una imagen: «¿cuántas personas hay?»"
                    }
                    GlassButton {
                        compact: true; text: "Elegir imagen…"; icon: "🖼"
                        onClicked: imagenDlg.open()
                    }
                    FileDialog {
                        id: imagenDlg
                        title: "Imagen para el modelo local"
                        nameFilters: ["Imágenes (*.png *.jpg *.jpeg *.webp *.bmp)"]
                        onAccepted: ml.probarVision(selectedFile, preguntaF.text)
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
                            Layout.maximumWidth: modelData.vision || modelData.resumen !== undefined ? parent.width * 0.6 : implicitWidth
                            wrapMode: modelData.vision || modelData.resumen !== undefined ? Text.WordWrap : Text.NoWrap
                            text: modelData.error !== undefined ? modelData.error
                                : modelData.vision ? modelData.texto
                                : modelData.derivar ? "→ derivar al grande"
                                : modelData.resumen !== undefined ? "→ " + modelData.resumen
                                : "→ " + modelData.herramienta
                            color: modelData.error !== undefined ? Theme.live
                                 : modelData.derivar ? Theme.amber : Theme.ok
                            font { pixelSize: 12; family: Theme.mono }
                        }
                        Text {
                            visible: modelData.prob !== undefined || modelData.vision === true
                            text: modelData.vision ? Math.round(modelData.ms_imagen || 0) + " ms"
                                : Math.round((modelData.prob || 0) * 100) + "% · " + Math.round(modelData.ms || 0) + " ms"
                            color: Theme.textFaint; font.pixelSize: 11
                        }
                    }
                }
            }
        }
    }

    // --- Voz del operador (micrófono, auriculares, voz del asistente) -----------------
    // El micrófono del operador (lo que oye el lobby y de donde se le dicta al
    // asistente), dónde hay voz a texto, la voz que contesta y lo transcripto.
    Component {
        id: traduccionComp
        ScrollView {
            clip: true
            contentWidth: availableWidth
            ColumnLayout {
                width: parent.width
                spacing: 14
                PanelTraduccion { Layout.fillWidth: true }
                CamposDeSeccion { Layout.fillWidth: true; seccion: page.traduccionSection }
            }
        }
    }
    Component {
        id: vozComp
        ScrollView {
            clip: true
            contentWidth: availableWidth
            ColumnLayout {
                width: parent.width
                spacing: 14
                readonly property var vz: Studio.voz

                Text { text: "Micrófono del operador"; color: Theme.text; font { pixelSize: 15; weight: Font.DemiBold } }
                RowLayout {
                    Layout.fillWidth: true
                    ComboBox {
                        id: micCombo
                        Layout.fillWidth: true
                        readonly property var nombres: ["— ninguno —"].concat(
                            (Studio.audio ? Studio.audio.sources : []).map(x => x.nombre))
                        model: nombres
                        currentIndex: Math.max(0, nombres.indexOf(Studio.voz.microfono))
                        onActivated: (i) => {
                            const e = Studio.voz.setMicrofono(i === 0 ? "" : nombres[i])
                            if (e !== "") Studio.notify(e, "error")
                        }
                    }
                }
                Text {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    color: Theme.textFaint
                    font.pixelSize: 11
                    text: "Se elige entre las entradas con audio (se agregan en Entradas). Es lo que te oye quien " +
                          "espera en el lobby y, si no hay otra fuente con «Voz a texto» en dictado, de donde se " +
                          "le dicta al asistente. Mientras apretás " + Studio.voz.tecla + ", el lobby no te oye."
                }

                Rectangle { Layout.fillWidth: true; height: 1; color: Theme.glassBorder }

                Text { text: "Salida de auriculares del operador"; color: Theme.text; font { pixelSize: 15; weight: Font.DemiBold } }
                Text {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    color: Theme.textDim; font.pixelSize: 12
                    text: "Por acá suenan la voz del asistente y la escucha de los racks (nunca al aire). Es una sola " +
                          "para toda la app: cambiarla acá la cambia también en Audio."
                }
                Flow {
                    Layout.fillWidth: true
                    spacing: 6
                    GlassButton {
                        compact: true; text: "La del sistema"
                        active: Studio.voz.auriculares === ""
                        onClicked: Studio.voz.setAuriculares("")
                    }
                    Repeater {
                        model: Studio.voz.salidasAudio
                        delegate: Repeater {
                            required property var modelData
                            model: modelData.salidas
                            delegate: GlassButton {
                                required property var modelData
                                compact: true; text: modelData.nombre
                                active: Studio.voz.auriculares === modelData.id
                                onClicked: Studio.voz.setAuriculares(modelData.id)
                            }
                        }
                    }
                }

                Rectangle { Layout.fillWidth: true; height: 1; color: Theme.glassBorder }

                Text { text: "Hablarle al asistente"; color: Theme.text; font { pixelSize: 15; weight: Font.DemiBold } }
                RowLayout {
                    Layout.fillWidth: true
                    visible: Studio.voz.disponible
                    spacing: 10
                    Text { text: "🎙"; font.pixelSize: 16 }
                    Text {
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        text: "Hablarle al asistente: " + Studio.voz.estado +
                              (Studio.voz.ultimo !== "" ? "  ·  último: «" + Studio.voz.ultimo + "»" : "")
                        color: Studio.voz.lista ? Theme.text : Theme.textDim
                        font.pixelSize: 12
                    }
                }



                // Dónde hay voz a texto
                Text {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    color: Theme.textDim; font.pixelSize: 12
                    text: Studio.voz.fuentesTexto.length === 0
                          ? "Ninguna fuente tiene «Voz a texto» todavía: al apretar " + Studio.voz.tecla +
                            " se le pone sola al micrófono del operador. En Audio se agrega a cualquier fuente " +
                            "(dictado o transcripción continua), como una salida."
                          : "Fuentes con voz a texto (se cambian en Audio):"
                }
                Repeater {
                    model: Studio.voz.fuentesTexto
                    delegate: RowLayout {
                        required property var modelData
                        Layout.fillWidth: true
                        spacing: 10
                        Text { text: "●"; color: "#22c55e"; font.pixelSize: 12 }
                        Text { Layout.fillWidth: true; text: modelData.fuente; color: Theme.text; font.pixelSize: 13; elide: Text.ElideRight }
                        Text {
                            text: modelData.modo === "continuo" ? "transcripción continua" : "dictado (" + Studio.voz.tecla + ")"
                            color: Theme.textDim; font.pixelSize: 12
                        }
                        GlassButton { compact: true; text: "Quitar"; onClicked: Studio.voz.setTexto(modelData.fuente, "") }
                    }
                }

                Rectangle { Layout.fillWidth: true; height: 1; color: Theme.glassBorder }


                Text { text: "Lo dictado y el modelo local"; color: Theme.text; font { pixelSize: 15; weight: Font.DemiBold } }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 10
                    Toggle { checked: Studio.voz.usarModeloLocal; onToggled: (w) => Studio.voz.usarModeloLocal = w }
                    Text {
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        color: Theme.text; font.pixelSize: 13
                        text: "Pasar lo dictado por el modelo local: elige la acción al instante y la propone en la " +
                              "barra del asistente para que la confirmes. Lo que no es suyo, o sin modelo listo, va " +
                              "al asistente como siempre."
                    }
                }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 10
                    Toggle { checked: Studio.voz.contestarConVoz; onToggled: (w) => Studio.voz.contestarConVoz = w }
                    Text {
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        color: Theme.text; font.pixelSize: 13
                        text: "Contestar con voz (solo en tus auriculares): «listo», lo que no se pudo y por qué, y " +
                              "la respuesta cuando preguntás algo («¿qué escena está al aire?»)."
                    }
                }
                Text {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    color: Theme.textDim; font.pixelSize: 12
                    text: Studio.voz.automaticas.length === 0
                          ? "Todavía no hay acciones que se hagan sin preguntar. Se agregan con «Hacer y no volver a " +
                            "preguntar» al confirmar una."
                          : "Se hacen sin preguntar (las que piden datos los completa el asistente):"
                }
                Repeater {
                    model: Studio.voz.automaticas
                    delegate: RowLayout {
                        required property var modelData
                        Layout.fillWidth: true
                        spacing: 10
                        Text { text: "⚡"; color: Theme.accent; font.pixelSize: 12 }
                        Text { Layout.fillWidth: true; text: modelData.titulo; color: Theme.text; font.pixelSize: 13; elide: Text.ElideRight }
                        Text { text: modelData.herramienta; color: Theme.textFaint; font.pixelSize: 11 }
                        GlassButton { compact: true; text: "Volver a preguntar"; onClicked: Studio.voz.setAutomatica(modelData.herramienta, false) }
                    }
                }

                Rectangle { Layout.fillWidth: true; height: 1; color: Theme.glassBorder }

                // La voz que contesta (solo por los auriculares del operador)
                Text {
                    visible: Studio.voz.disponible
                    text: "Voz que contesta"; color: Theme.text; font { pixelSize: 15; weight: Font.DemiBold }
                }
                Text {
                    visible: Studio.voz.disponible
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    color: Theme.textFaint
                    font.pixelSize: 11
                    text: "Suena SOLO por el monitoreo (los auriculares del operador), nunca al aire. Frase por " +
                          "frase, así empieza enseguida. Si apretás la tecla para hablar, se calla."
                }
                Repeater {
                    model: Studio.voz.disponible ? Studio.voz.voces : []
                    delegate: Rectangle {
                        required property var modelData
                        Layout.fillWidth: true
                        implicitHeight: filaVoz.implicitHeight + 16
                        radius: Theme.radiusSmall
                        color: modelData.elegida ? Qt.rgba(1, 0.42, 0.17, 0.12) : Theme.field
                        border.color: modelData.elegida ? Theme.accent : "transparent"
                        RowLayout {
                            id: filaVoz
                            anchors { fill: parent; margins: 8 }
                            spacing: 10
                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 1
                                Text { text: modelData.nombre; color: Theme.text; font.pixelSize: 13 }
                                Text {
                                    text: "calidad " + modelData.calidad + " · " + Math.round(modelData.tamano_gb * 1024) + " MB"
                                    color: Theme.textDim; font.pixelSize: 11
                                }
                            }
                            GlassButton {
                                compact: true
                                visible: !modelData.instalado
                                enabled: ml.enCola.indexOf(modelData.repo) < 0
                                text: enabled ? "Bajar" : "En la cola"; icon: "⇩"
                                onClicked: Studio.notify(modelData.repo + ": " + ml.descargar(modelData.repo), "info")
                            }
                            GlassButton {
                                compact: true
                                visible: modelData.instalado
                                text: modelData.elegida ? "Elegida" : "Elegir"
                                active: modelData.elegida
                                onClicked: Studio.voz.elegirVoz(modelData.repo)
                            }
                            GlassButton {
                                compact: true
                                visible: modelData.instalado
                                text: "Borrar"
                                onClicked: ml.borrar(modelData.repo)
                            }
                        }
                    }
                }
                RowLayout {
                    Layout.fillWidth: true
                    visible: Studio.voz.vozLista
                    spacing: 10
                    Repeater {
                        model: Studio.voz.hablantes > 1 ? Studio.voz.hablantes : 0
                        delegate: GlassButton {
                            required property int index
                            compact: true
                            text: "Voz " + (index + 1)
                            active: Studio.voz.hablante === index
                            onClicked: Studio.voz.hablante = index
                        }
                    }
                    Text { text: "más lento"; color: Theme.textDim; font.pixelSize: 11 }
                    Slider {
                        Layout.fillWidth: true
                        from: 0.7; to: 1.5; stepSize: 0.05
                        value: Studio.voz.velocidad
                        onMoved: Studio.voz.velocidad = value
                    }
                    Text { text: "más rápido"; color: Theme.textDim; font.pixelSize: 11 }
                }
                RowLayout {
                    Layout.fillWidth: true
                    visible: Studio.voz.vozLista
                    TextField {
                        id: decirF
                        Layout.fillWidth: true
                        placeholderText: "escribí algo para que lo diga (por los auriculares)"
                        onAccepted: { Studio.voz.decir(text); text = "" }
                    }
                    GlassButton {
                        compact: true; text: decirF.text.trim() !== "" ? "Decir" : "Probar"; icon: "🔊"; active: true
                        onClicked: {
                            if (decirF.text.trim() !== "") { Studio.voz.decir(decirF.text); decirF.text = "" }
                            else Studio.voz.probarVoz()
                        }
                    }
                    GlassButton {
                        compact: true; text: "Callar"
                        visible: Studio.voz.hablando
                        onClicked: Studio.voz.callar()
                    }
                }

                Rectangle { Layout.fillWidth: true; height: 1; color: Theme.glassBorder }


                // Lo transcripto en continuo
                // Subtítulos y traducción: cuándo corta una frase la transcripción continua.
                AjustesCorte { Layout.fillWidth: true }
                Rectangle { Layout.fillWidth: true; height: 1; color: Theme.glassBorder }

                Text {
                    visible: Studio.voz.transcripcion.length > 0
                    text: "Transcripción"; color: Theme.text; font { pixelSize: 15; weight: Font.DemiBold }
                }
                Repeater {
                    model: Studio.voz.transcripcion.slice(-15)
                    delegate: Text {
                        required property var modelData
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        textFormat: Text.StyledText
                        text: "<font color='" + Theme.textFaint + "'>" + modelData.hora + " · " + modelData.fuente + "</font>  " + modelData.texto
                        color: Theme.text; font.pixelSize: 12
                    }
                }
                GlassButton {
                    visible: Studio.voz.transcripcion.length > 0
                    compact: true; text: "Borrar la transcripción"
                    onClicked: Studio.voz.borrarTranscripcion()
                }
                // Los del esquema (la voz del prompter): avanzados.
                CamposDeSeccion { Layout.fillWidth: true; seccion: page.vozSection }
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

                CamposDeSeccion { Layout.fillWidth: true; seccion: page.aparienciaSection }
                Rectangle { Layout.fillWidth: true; height: 1; color: Theme.glassBorder }
                Text {
                    text: "Barra de navegación"
                    color: Theme.text
                    font { pixelSize: 17; weight: Font.DemiBold }
                }
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
                      "\ny vuelve a abrir sola. Configuración, credenciales, grabaciones y archivos NO se tocan (antes se respaldan). Si algo sale mal, podés volver a esta versión."
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
                Rectangle { Layout.fillWidth: true; height: 1; color: Theme.glassBorder }
                // Configuraciones con nombre (cada una con lo suyo).
                PanelConfiguraciones { Layout.fillWidth: true }
                Rectangle { Layout.fillWidth: true; height: 1; color: Theme.glassBorder }
                Text { text: "Arranque y recuperación"; color: Theme.text; font { pixelSize: 15; weight: Font.DemiBold } }
                // Volver solo después de un corte de luz.
                ColumnLayout {
                    id: inicioBox
                    Layout.fillWidth: true
                    spacing: 6
                    property var est: sys.inicioEstado()
                    property string error: ""
                    RowLayout {
                        Switch {
                            checked: inicioBox.est.activo === true
                            onToggled: { inicioBox.error = sys.inicioPoner(checked); inicioBox.est = sys.inicioEstado() }
                        }
                        Text { text: "Arrancar solo al encender el equipo"; color: Theme.text; font.pixelSize: 14 }
                    }
                    Text { Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.textDim; font.pixelSize: 12
                           text: "Si se corta la luz con la transmisión o la grabación andando, al volver el programa las retoma solo. Para que vuelva sin que nadie toque nada, además:" }
                    Repeater {
                        model: inicioBox.est.notas || []
                        delegate: Text { required property var modelData; Layout.fillWidth: true; wrapMode: Text.WordWrap
                                         color: Theme.textDim; font.pixelSize: 12; text: "• " + modelData }
                    }
                    Text { visible: inicioBox.error !== ""; text: inicioBox.error; color: Theme.live; font.pixelSize: 12 }
                }
                // Los del esquema (copia sombra, diálogo de recuperación): van con Aplicar.
                CamposDeSeccion { Layout.fillWidth: true; seccion: page.systemSection }
                Rectangle { Layout.fillWidth: true; height: 1; color: Theme.glassBorder }
                Text { text: "Carpetas"; color: Theme.text; font { pixelSize: 15; weight: Font.DemiBold } }
                GridLayout {
                    Layout.fillWidth: true
                    columns: 2
                    columnSpacing: 12
                    rowSpacing: 8
                    Repeater {
                        model: [
                            { n: "Config", d: "Configuración (escenas, ajustes, tablero)" },
                            { n: "Credenciales", d: "Tokens y claves, cifrados (AES-256-GCM). Una actualización nunca los toca." },
                            { n: "Recordings", d: "Grabaciones" },
                            { n: "Shares", d: "Archivos que comparten los invitados" },
                            { n: "Respaldos", d: "Copias de Config/ y Credenciales/ antes de cada actualización (quedan 5)" },
                            { n: "Logs", d: "Registros para diagnóstico" }
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
                       text: "Descomprimí la versión nueva en cualquier carpeta (fuera de esta) y elegila. Se reemplaza solo el programa; Config/, Credenciales/, Recordings/, Shares/ y Logs/ quedan como están." }
                RowLayout {
                    GlassButton { text: "Actualizar desde una carpeta…"; icon: "⇪"; active: true; onClicked: updateDlg.open() }
                    GlassButton { text: "Volver a la versión anterior"; visible: sys.canRollback; onClicked: page.confirmRollback() }
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
                Rectangle { Layout.fillWidth: true; height: 1; color: Theme.glassBorder }
                Text { text: "Diagnóstico"; color: Theme.text; font { pixelSize: 15; weight: Font.DemiBold } }
                // Los elementos de GStreamer que faltan (P0.3): que no sean silencio o negro sin explicación.
                Text {
                    readonly property var faltan: sys.elementosQueFaltan()
                    Layout.fillWidth: true; wrapMode: Text.WordWrap; font.pixelSize: 12
                    color: faltan.filter(e => e.imprescindible).length > 0 ? Theme.live : Theme.textDim
                    text: faltan.length === 0 ? "GStreamer: están todos los elementos que usa el aparato."
                          : "GStreamer: faltan " + faltan.map(e => e.elemento + " (" + e.para + ")").join(", ")
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
