// Configuracion completa: secciones a la izquierda (agrupadas: Imagen, Salida,
// Conferencia, Asistente, Sistema), formulario generado desde el esquema
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
    readonly property string vozSection: "Voz del sistema"
    readonly property string traduccionSection: "Traducción"
    readonly property string controlSection: "Control del sistema"
    readonly property string pinggySection: "Pinggy"
    /// Los avanzados de la sección Pinggy (los del TURN), al final de su página.
    readonly property var clavesAvanzadasPinggy: ["turn.maxAsignaciones", "turn.maxConexiones", "turn.vidaMaxS", "turn.nonceS", "turn.colaTcpKB", "turn.credencialS"]
    /// El orden de la lista de la izquierda. Las secciones del esquema
    /// (SettingsModel) y las de vista propia, juntas por tema.
    readonly property var grupos: [
        // El idioma, primero y a la vista («Idioma · Language»: también en inglés).
        { titulo: qsTr("General"), secciones: ["Idioma"] },
        { titulo: qsTr("Imagen y sonido"), secciones: ["Video", "Cámaras y entradas", "Audio", page.traduccionSection] },
        { titulo: qsTr("Salida"), secciones: ["Transmisión", "Grabación", "Codificación"] },
        { titulo: qsTr("Conferencia"), secciones: ["Invitados", "Conferencia", "Red de la conferencia"] },
        { titulo: qsTr("Asamblea"), secciones: ["Sesión remota"] },
        { titulo: qsTr("Personas"), secciones: ["Caras", "Tótems de registro"] },
        { titulo: qsTr("Control y pantallas"), secciones: ["Botonera y superficies", "Teleprompter", "Pantallas", "Página del evento"] },
        // Las tres formas de sacar la conferencia a internet, cada una con su página.
        // Cloudflare y el túnel propio (WireGuard) están escondidos hasta probarlos: el código y sus
        // ajustes siguen, para volver a mostrarlos sumalos acá ("Cloudflare", "Túnel propio (WireGuard)").
        { titulo: qsTr("Acceso desde internet"), secciones: [page.pinggySection] },
        { titulo: qsTr("Asistente"), secciones: ["Asistente (MCP)", page.vozSection, page.modelosSection, page.controlSection] },
        { titulo: qsTr("Sistema"), secciones: [page.aparienciaSection, "Plugins y descargas", "Dispositivos y drivers", page.systemSection] }
    ]
    /// Los ajustes avanzados (antes fijos en el código): escondidos salvo que se pidan;
    /// una sección que solo tiene avanzados (Audio, Plugins y descargas) los muestra siempre.
    property bool verAvanzados: false

    /// Las que tienen vista propia (el formulario del esquema, si tienen campos, va adentro).
    readonly property var propias: [page.systemSection, page.aparienciaSection, page.modelosSection,
                                    page.vozSection, page.traduccionSection, page.controlSection, page.pinggySection]
    /// Las que tienen ajustes que reinician el servidor (gateway) al aplicar, y ofrecen reiniciarlo a mano.
    readonly property var deLaSala: ["Invitados", "Conferencia", "Red de la conferencia", "Cloudflare", "Túnel propio (WireGuard)", "Pinggy",
                                     "Sesión remota", "Botonera y superficies", "Teleprompter", "Pantallas", "Página del evento"]
    /// Lo que va arriba de las páginas de túnel: qué es y cuándo conviene.
    readonly property var introducciones: ({
        "Cloudflare": qsTr("Saca la conferencia a internet por un túnel de Cloudflare, con tu dominio (p. ej. sala.miorganismo.gob.ar). Hace falta una cuenta de Cloudflare con el dominio cargado y el token del túnel (Zero Trust → Networks → Tunnels → el túnel → token). El video de los de afuera va por la conexión directa o el TURN. Se usa uno solo de los tres caminos: Pinggy, Cloudflare o túnel propio."),
        "Túnel propio (WireGuard)": qsTr("El túnel común a un servidor propio con IP pública (un VPS): este equipo se conecta por WireGuard y los invitados entran directo por la IP o el dominio del servidor, sin intermediarios. Es el mejor camino para el video, pero necesita el servidor configurado. Sin permisos de administrador ni drivers. Se usa uno solo de los tres caminos: Pinggy, Cloudflare o túnel propio.")
    })
    /// Subtítulos dentro de una sección: van arriba del campo con esa clave.
    readonly property var subtitulos: ({
        "video.width": qsTr("Lienzo y salida"), "video.formato": qsTr("Color"), "transition.type": qsTr("Transiciones"),
        "mejora.activa": qsTr("Mejorar imagen"), "video.legalizar": qsTr("Entradas"), "entradas.sinSenal": qsTr("Cortes de señal"),
        "record.dir": qsTr("Archivo"), "record.calidad": qsTr("Calidad y pistas"), "record.copiaDir": qsTr("Copia y repetición"), "record.colchon": qsTr("Si el disco se traba"),
        "encoder.video": qsTr("Video"), "encoder.audioBitrate": qsTr("Audio"),
        "guests.count": qsTr("Lugares"), "guests.video": qsTr("Qué mandan"), "gateway.videoW": qsTr("Calidad del video"),
        "gateway.room": qsTr("Entrada"), "room.maxBackstage": qsTr("Modo oradores"), "room.vistasRemotas": qsTr("Pantallas y celulares"),
        "room.pinIntentos": qsTr("Seguridad"), "room.maxUploadMB": qsTr("Archivos"),
        "gateway.port": qsTr("Dónde escucha"), "direct.https": qsTr("Conexión directa (HTTPS y WebRTC)"), "room.adminPort": qsTr("Control"),
        "gateway.tunnelHost": qsTr("Dirección"), "wg.endpoint": qsTr("El servidor"), "wg.httpPort": qsTr("Puertos"), "turn.maxAsignaciones": qsTr("Avanzado"),
        "mcp.approval": qsTr("Aprobaciones"), "video.esperaCorteMs": qsTr("Avanzado"), "entradas.congelarMs": qsTr("Avanzado"), "audio.margenMezclaMs": qsTr("Avanzado"), "stream.sinDatosS": qsTr("Avanzado"), "record.colchonIsoS": qsTr("Avanzado"), "room.oradorSeg": qsTr("Avanzado"), "room.maxConexiones": qsTr("Avanzado"), "gateway.tunelReintentoMaxS": qsTr("Avanzado"), "asistente.demoraArranqueMs": qsTr("Avanzado"), "modelo.maxTokens": qsTr("Avanzado"), "traductor.maxCaidas": qsTr("Avanzado"), "vozPrompter.consultaMs": qsTr("Avanzado"), "descargas.simultaneas": qsTr("Avanzado"), "launcher.primerLatidoS": qsTr("Avanzado"), "modelo.contexto": qsTr("El chat sin internet (pestaña Asistente)"), "launcher.cuelgue": qsTr("Si se cuelga o se cae"),
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
    // la conferencia o en la sesion remota eso corta todo, asi que se advierte y se pide una
    // confirmacion explicita. Lo que se corta lo dice Studio (Main.qml usa lo mismo).
    readonly property bool onAir: Studio.app.impactoDeCerrar.hay
    function onAirWarning(what) { return Studio.app.avisoDeCerrar(what) }
    function confirmUpdate(folder) {
        ApplicationWindow.window.confirm({
            title: qsTr("¿Actualizar ahora?"),
            text: page.onAirWarning("actualizar"),
            accept: qsTr("Actualizar igual"),
            reject: qsTr("Cancelar"),
            action: function() { page.updateError = sys.startUpdate(folder) }
        })
    }
    function confirmRollback() {
        const data = qsTr("Configuración, credenciales, grabaciones y archivos no se tocan.")
        ApplicationWindow.window.confirm({
            title: qsTr("¿Volver a la versión anterior?"),
            text: page.onAir ? qsTr("%1 Después se restaura el programa anterior y la aplicación vuelve a abrir sola. %2")
                                   .arg(page.onAirWarning("volver")).arg(data)
                             : qsTr("La aplicación se cierra, se restaura el programa anterior y vuelve a abrir sola. %1").arg(data),
            accept: qsTr("Volver a la anterior"),
            reject: qsTr("Cancelar"),
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
            title: qsTr("Configuración")
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
                               text: Studio.settings.seccionVisible(modelData.seccion || ""); color: Theme.text; font.pixelSize: 13 }
                        MouseArea { id: sectMa; anchors.fill: parent; hoverEnabled: true; onClicked: Studio.seccionConfig = modelData.seccion }
                    }
                }
            }
            }
        }

        Panel {
            Layout.fillWidth: true
            Layout.fillHeight: true
            title: Studio.settings.seccionVisible(page.section)
            // Arriba a la derecha, al lado del título: los avanzados (antes fijos en el código).
            RowLayout {
                z: 10
                anchors { top: parent.top; right: parent.right; topMargin: -34; rightMargin: 4 }
                spacing: 6
                visible: Studio.settings.tieneBasicos(page.section) && Studio.settings.tieneAvanzados(page.section)
                Text { text: qsTr("Mostrar ajustes avanzados"); color: Theme.textDim; font.pixelSize: 12 }
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
                        titulo: qsTr("Token del túnel de Cloudflare")
                        subtitulo: qsTr("Sin token el túnel no arranca aunque esté prendido. Se guarda cifrado en este equipo y no se vuelve a mostrar. Rige cuando la conferencia arranca.")
                        Text {
                            text: Studio.accesoRemoto && Studio.accesoRemoto.tokenCloudflareGuardado ? qsTr("● Guardado") : qsTr("● Sin token")
                            color: Studio.accesoRemoto && Studio.accesoRemoto.tokenCloudflareGuardado ? Theme.ok : Theme.warn; font.pixelSize: 12
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 8
                            TextField {
                                id: cfCampo
                                Layout.fillWidth: true
                                echoMode: TextInput.Password; inputMethodHints: Qt.ImhSensitiveData | Qt.ImhNoPredictiveText
                                placeholderText: qsTr("Pegá acá el token del túnel (empieza con eyJ…)")
                            }
                            GlassButton {
                                compact: true; text: qsTr("Guardar"); active: cfCampo.text !== ""; enabled: cfCampo.text !== ""
                                onClicked: {
                                    const e = Studio.accesoRemoto.guardarTokenCloudflare(cfCampo.text)
                                    if (e === "") cfCampo.text = ""
                                    Studio.notify(e === "" ? qsTr("Token de Cloudflare guardado (cifrado). Prendé el túnel y aplicá.") : e, e === "" ? "ok" : "error")
                                }
                            }
                            GlassButton {
                                compact: true; text: qsTr("Borrar")
                                enabled: !!Studio.accesoRemoto && Studio.accesoRemoto.tokenCloudflareGuardado
                                onClicked: { const e = Studio.accesoRemoto.borrarTokenCloudflare(); Studio.notify(e === "" ? qsTr("Token de Cloudflare borrado.") : e, e === "" ? "ok" : "error") }
                            }
                        }
                    }
                    CamposDeSeccion { Layout.fillWidth: true; seccion: page.section }
                    GlassButton {
                        Layout.topMargin: 10
                        text: qsTr("Valores por defecto de esta sección"); compact: true
                        onClicked: Studio.settings.resetSection(page.section)
                    }
                    // Las secciones de la conferencia: al aplicar se reinicia sola, y acá
                    // también a mano (sin cerrar el programa).
                    RowLayout {
                        visible: page.deLaSala.indexOf(page.section) >= 0
                        Layout.fillWidth: true
                        Layout.topMargin: 4
                        spacing: 10
                        GlassButton {
                            compact: true; text: qsTr("Reiniciar el servidor ahora"); icon: "↻"
                            onClicked: ApplicationWindow.window.confirm({
                                title: qsTr("¿Reiniciar el servidor?"),
                                text: qsTr("Se reinicia solo el servidor (gateway: la conferencia y las páginas web) con la configuración guardada. Los invitados y lo que esté conectado se cortan y vuelven a entrar."),
                                accept: qsTr("Reiniciar"), reject: qsTr("Cancelar"),
                                action: function() {
                                    const e = Studio.conference.reiniciarSala()
                                    Studio.notify(e === "" ? qsTr("El servidor se está reiniciando.") : e, e === "" ? "ok" : "error")
                                }
                            })
                        }
                        Text {
                            Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.textFaint; font.pixelSize: 11
                            text: qsTr("Los cambios marcados «reinicia el servidor» lo reinician solos al aplicarlos; el resto del programa sigue andando.")
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
    // Column y no ColumnLayout (y cada fila, también): con los textos que se parten, cada
    // nivel de layout anidado pedía una pasada más de acomodo («Layout polish loop»).
    // Column saltea las ocultas solo.
    component CamposDeSeccion: Column {
        id: campos
        property string seccion: ""
        /// Si no está vacía, solo estas claves (para intercalar campos con otros bloques).
        property var claves: []
        spacing: 14
        Repeater {
            model: Studio.settings
            delegate: Column {
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
                width: campos.width
                spacing: 8
                Text {
                    visible: page.subtitulos[fila.key] !== undefined
                    width: fila.width
                    topPadding: 6
                    text: page.subtitulos[fila.key] || ""
                    color: Theme.text
                    font { pixelSize: 15; weight: Font.DemiBold }
                }
                FormField {
                    width: fila.width
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
                    return e === "listo" ? qsTr("Túnel listo: se entra desde internet")
                         : e === "conectando" ? qsTr("Conectando con pinggy…")
                         : e === "reconectando" ? qsTr("Se cortó: reconectando…")
                         : e === "error" ? qsTr("El túnel no anda")
                         : e === "apagado" ? qsTr("Túnel apagado")
                         : qsTr("Estado desconocido")
                }
                function guardarSecreto(campo, esToken) {
                    if (!esToken) {
                        const e = pg.ar.guardarApiKey(campo.text)
                        if (e !== "") { Studio.notify(e, "error"); return }
                        campo.text = ""
                        Studio.notify(qsTr("API key guardada (cifrada)."), "ok")
                        return
                    }
                    // Si hay que reiniciar la conferencia para que rija lo decide el modelo.
                    const r = pg.ar.guardarTokenTunel(campo.text, Studio.settings.dirty)
                    if (r.error !== "") { Studio.notify(r.error, "error"); return }
                    campo.text = ""
                    if (r.reiniciarAhora) {
                        ApplicationWindow.window.confirm({
                            title: qsTr("Token guardado"),
                            text: qsTr("La conferencia lo lee al arrancar. ¿La reinicio ahora? Los invitados se cortan y vuelven a entrar."),
                            accept: qsTr("Reiniciar la conferencia"), reject: qsTr("Después"),
                            action: function() {
                                const e = Studio.conference.reiniciarSala()
                                Studio.notify(e === "" ? qsTr("La conferencia se está reiniciando con el token nuevo.") : e, e === "" ? "ok" : "error")
                            }
                        })
                    } else {
                        Studio.notify(qsTr("Token guardado (cifrado). Rige cuando la conferencia arranca: prendé «Túnel de pinggy» y aplicá, o reiniciá la conferencia."), "ok")
                    }
                }


                Text {
                    Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.textDim; font.pixelSize: 12
                    text: qsTr("Saca las páginas de la conferencia a internet por pinggy.io (una dirección para todo: invitados, sesión remota, público) y el video de los de afuera por un TURN propio. Lo de la red local sigue igual.")
                }
                // Gratis o Pro: qué se puede y qué no (lo de Pro, según pinggy: probado solo el gratis).
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 12
                    Repeater {
                        model: [
                            { titulo: qsTr("Sin token (gratis)"), color: Theme.warn, items: [
                                qsTr("Para probar: anda sin cargar nada."),
                                qsTr("La dirección es al azar, lleva la IP pública del equipo y cambia cada vez que se prende (el enlace y el QR también)."),
                                qsTr("El túnel dura 60 minutos: después hay que volver a prenderlo y la dirección cambia."),
                                qsTr("A cada navegador pinggy le muestra antes un aviso («Caution – you are about to visit…») que hay que aceptar con un botón: los invitados se confunden."),
                                qsTr("Sin subdominio fijo, sin dominio propio y sin puertos fijos."),
                                qsTr("El video de los de afuera anda (por el TURN por TCP).")
                            ] },
                            { titulo: qsTr("Con token (plan Pro)"), color: Theme.ok, items: [
                                qsTr("Sin límite de tiempo y sin el aviso intermedio: se entra directo."),
                                qsTr("Dirección fija: un subdominio de pinggy o el dominio de la organización (con un CNAME; el certificado lo pone pinggy)."),
                                qsTr("Puertos fijos para el TURN, si se reservan."),
                                qsTr("La API key permite ver desde acá los túneles activos de la cuenta."),
                                qsTr("Un túnel por token: cada equipo necesita su propio token."),
                                qsTr("Se configura en dashboard.pinggy.io: el token del plan y la API key van abajo, cada uno en su lugar.")
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
                    text: qsTr("En los dos: el tráfico no tiene tope y todo pasa por los servidores de pinggy (desde Argentina, la región automática da São Paulo, unos 200 ms de ida y vuelta). La red local no cambia. Si hay un servidor propio con WireGuard, para el video sigue siendo mejor camino. Probado en este programa: el plan gratis; lo de Pro es lo que publica pinggy.")
                }
                Text {
                    Layout.topMargin: 14
                    text: qsTr("Estado"); color: Theme.text; font { pixelSize: 16; weight: Font.DemiBold }
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
                            text: qsTr("Último error: %1").arg(pg.pk.error || ""); color: Theme.live; font.pixelSize: 11
                        }
                    }
                    GlassButton { compact: true; text: qsTr("Actualizar"); icon: "↻"; onClicked: pg.ar.refrescar() }
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
                        GlassButton { compact: true; text: qsTr("Copiar"); onClicked: Studio.copiar(modelData.url) }
                    }
                }
                Text {
                    visible: (pg.pk.urls || []).length > 1
                    Layout.fillWidth: true; wrapMode: Text.WrapAnywhere
                    text: qsTr("Direcciones del túnel: %1").arg((pg.pk.urls || []).join("  ·  "))
                    color: Theme.textFaint; font.pixelSize: 11
                }
                Text {
                    visible: pg.pk.turn !== undefined && (pg.pk.turn.publico || "") !== ""
                    Layout.fillWidth: true; wrapMode: Text.WrapAnywhere
                    text: pg.pk.turn ? qsTr("TURN (video de los de afuera): %1").arg(pg.pk.turn.publico + (pg.pk.turn.tipo ? " · " + pg.pk.turn.tipo : "")
                                       + (typeof pg.pk.turn.udp === "string" && pg.pk.turn.udp !== "" ? " · " + qsTr("UDP: %1").arg(pg.pk.turn.udp)
                                          : pg.pk.turn.udp === true ? " · " + qsTr("también UDP") : " · " + qsTr("solo TCP"))) : ""
                    color: Theme.textDim; font.pixelSize: 11
                }

                Text {
                    Layout.topMargin: 14
                    text: qsTr("Conexión"); color: Theme.text; font { pixelSize: 16; weight: Font.DemiBold }
                }
                CamposDeSeccion { Layout.fillWidth: true; seccion: "Pinggy"; claves: ["pinggy.activo", "pinggy.region"] }
                // Las dos claves de pinggy, juntas y bien separadas: no son lo mismo y
                // pegar una en el lugar de la otra era el error de siempre.
                Text {
                    Layout.topMargin: 14
                    text: qsTr("Las dos claves de pinggy"); color: Theme.text; font { pixelSize: 16; weight: Font.DemiBold }
                }
                Text {
                    Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.textDim; font.pixelSize: 12
                    textFormat: Text.StyledText; linkColor: Theme.accent
                    onLinkActivated: (l) => Qt.openUrlExternally(l)
                    text: qsTr("<b>Lo más fácil: pegá solo la API key y tocá «Armar todo».</b> La app le pide a tu cuenta el token del túnel, "
                          + "tu subdominio fijo y tu puerto, y prende el túnel; si querés otro nombre, escribilo y lo cambia en la cuenta. "
                          + "No hace falta entrar a <a href=\"https://dashboard.pinggy.io\">dashboard.pinggy.io</a>. "
                          + "Las claves se guardan cifradas en este equipo y no se vuelven a mostrar.")
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
                        text: qsTr("El token y la API key guardados son la MISMA clave: casi seguro pegaste la API key en el lugar del token. Así el túnel no abre (pinggy contesta «no autenticado»). Pegá en «Token del plan» el token del comando del túnel.")
                    }
                }
                GridLayout {
                    Layout.fillWidth: true
                    columns: pg.width > 900 ? 2 : 1 // por el ancho de la página, no el suyo (lo pone el layout)
                    columnSpacing: 12; rowSpacing: 12
                    // --- 1. El token del plan ---
                    Seccion {
                        Layout.alignment: Qt.AlignTop
                        titulo: qsTr("Token del plan — abre el túnel (lo trae «Armar todo»)")
                        subtitulo: qsTr("Con él, el túnel usa tu plan Pro: dirección fija (tu subdominio o tu dominio), sin el cartel de aviso y sin el límite de 60 minutos.")
                        resaltado: pg.ar.clavesIguales ? Theme.live : "transparent"
                        Text {
                            text: pg.ar.tokenGuardado ? qsTr("● Guardado") : qsTr("● Sin token: modo gratis (60 minutos, dirección al azar, con el aviso de pinggy)")
                            color: pg.ar.tokenGuardado ? (pg.ar.clavesIguales ? Theme.live : Theme.ok) : Theme.warn; font.pixelSize: 12
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 8
                            TextField {
                                id: tokenCampo
                                Layout.fillWidth: true
                                echoMode: TextInput.Password; inputMethodHints: Qt.ImhSensitiveData | Qt.ImhNoPredictiveText
                                placeholderText: pg.ar.tokenGuardado ? qsTr("Pegá uno nuevo para reemplazarlo") : qsTr("Pegá acá el token del plan")
                                onAccepted: if (text !== "") pg.guardarSecreto(tokenCampo, true)
                            }
                            GlassButton { compact: true; text: qsTr("Guardar"); active: tokenCampo.text !== ""; enabled: tokenCampo.text !== ""; onClicked: pg.guardarSecreto(tokenCampo, true) }
                            GlassButton {
                                compact: true; text: qsTr("Borrar"); enabled: pg.ar.tokenGuardado
                                onClicked: ApplicationWindow.window.confirm({
                                    title: qsTr("¿Borrar el token de pinggy?"),
                                    text: qsTr("Cuando la conferencia vuelva a arrancar, el túnel queda en modo gratis hasta que guardes otro."),
                                    accept: qsTr("Borrar"), reject: qsTr("Cancelar"),
                                    action: function() { const e = pg.ar.borrarToken(); Studio.notify(e === "" ? qsTr("Token borrado.") : e, e === "" ? "ok" : "error") }
                                })
                            }
                        }
                        Text {
                            Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.textFaint; font.pixelSize: 11
                            text: qsTr("Dónde está: en el panel de pinggy, el token de acceso que aparece en el comando para abrir un túnel (ssh … <token>@pro.pinggy.io). Uno por equipo. Rige cuando la conferencia arranca.")
                        }
                    }
                    // --- 2. La API key ---
                    Seccion {
                        Layout.alignment: Qt.AlignTop
                        titulo: qsTr("API key — maneja tu cuenta de pinggy")
                        subtitulo: qsTr("Con ella la app arma todo sola (token, subdominio, puerto), cambia el subdominio y corta túneles de la cuenta. Es lo único que hace falta pegar.")
                        Text {
                            text: pg.ar.apiKeyGuardada ? qsTr("● Guardada") : qsTr("● Sin API key (opcional)")
                            color: pg.ar.apiKeyGuardada ? (pg.ar.clavesIguales ? Theme.live : Theme.ok) : Theme.textFaint; font.pixelSize: 12
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 8
                            TextField {
                                id: apiCampo
                                Layout.fillWidth: true
                                echoMode: TextInput.Password; inputMethodHints: Qt.ImhSensitiveData | Qt.ImhNoPredictiveText
                                placeholderText: pg.ar.apiKeyGuardada ? qsTr("Pegá una nueva para reemplazarla") : qsTr("Pegá acá la API key")
                                onAccepted: if (text !== "") pg.guardarSecreto(apiCampo, false)
                            }
                            GlassButton { compact: true; text: qsTr("Guardar"); active: apiCampo.text !== ""; enabled: apiCampo.text !== ""; onClicked: pg.guardarSecreto(apiCampo, false) }
                            GlassButton {
                                compact: true; text: qsTr("Borrar"); enabled: pg.ar.apiKeyGuardada
                                onClicked: { const e = pg.ar.borrarApiKey(); Studio.notify(e === "" ? qsTr("API key borrada.") : e, e === "" ? "ok" : "error") }
                            }
                            GlassButton {
                                compact: true; text: qsTr("Ver en pinggy"); enabled: pg.ar.apiKeyGuardada && pg.ar.panel.estado !== "consultando"
                                onClicked: pg.ar.verEnPinggy()
                            }
                        }
                        Text {
                            Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.textFaint; font.pixelSize: 11
                            text: qsTr("Dónde está: en el panel de pinggy, en la parte de API keys (se crea ahí). «Ver en pinggy» muestra lo que la cuenta tiene activo, visto desde pinggy.")
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
                // Armar todo con la API key: token, subdominio (y cambiarlo), puerto, y prender.
                Seccion {
                    titulo: qsTr("Armar todo con la API key")
                    subtitulo: pg.ar.apiKeyGuardada ? qsTr("Trae el token del túnel, tu subdominio y tu puerto TCP fijo desde la cuenta, y prende el túnel (la conferencia se reinicia).")
                                                    : qsTr("Primero guardá la API key (arriba).")
                    resaltado: pg.ar.cuenta.estado === "error" ? Theme.live : pg.ar.cuenta.estado === "ok" ? Theme.ok : "transparent"
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8
                        Text { text: qsTr("Subdominio (opcional)"); color: Theme.textDim; font.pixelSize: 12 }
                        TextField {
                            id: subPedido
                            Layout.preferredWidth: 220
                            placeholderText: qsTr("dejalo vacío o p. ej. salaconcejo")
                            inputMethodHints: Qt.ImhNoAutoUppercase | Qt.ImhNoPredictiveText
                        }
                        Text { text: ".a.pinggy.link"; color: Theme.textFaint; font.pixelSize: 12 }
                        Item { Layout.fillWidth: true }
                        GlassButton {
                            text: pg.ar.cuenta.estado === "trabajando" ? qsTr("Armando…") : qsTr("Armar todo"); active: true
                            enabled: pg.ar.apiKeyGuardada && pg.ar.cuenta.estado !== "trabajando"
                            onClicked: pg.ar.configurarConApiKey(subPedido.text)
                        }
                        GlassButton {
                            compact: true; text: qsTr("Cortar túneles de la cuenta")
                            enabled: pg.ar.apiKeyGuardada && pg.ar.cuenta.estado !== "trabajando"
                            onClicked: ApplicationWindow.window.confirm({
                                title: qsTr("¿Cortar los túneles de la cuenta?"),
                                text: qsTr("Corta todos los túneles activos de tu cuenta de pinggy (p. ej. otro equipo con el mismo token). El de este equipo vuelve a conectarse solo si está prendido."),
                                accept: qsTr("Cortar"), reject: qsTr("Cancelar"), action: function() { pg.ar.cortarTunelesDeLaCuenta() } })
                        }
                    }
                    Text {
                        Layout.fillWidth: true; wrapMode: Text.WordWrap; font.pixelSize: 12
                        visible: (pg.ar.cuenta.texto || "") !== ""
                        color: pg.ar.cuenta.estado === "error" ? Theme.live : pg.ar.cuenta.estado === "ok" ? Theme.ok : Theme.textDim
                        text: pg.ar.cuenta.texto || ""
                    }
                }

                Text {
                    Layout.topMargin: 14
                    text: qsTr("Dirección pública"); color: Theme.text; font { pixelSize: 16; weight: Font.DemiBold }
                }
                CamposDeSeccion { Layout.fillWidth: true; seccion: "Pinggy"; claves: ["pinggy.dominioTipo", "pinggy.host", "pinggy.cnameDestino"] }
                Text {
                    Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.textFaint; font.pixelSize: 11
                    text: pg.borrador["pinggy.dominioTipo"] === "azar"
                          ? qsTr("Al azar cambia cada vez que arranca el túnel (y el QR con él). Para una dirección fija: subdominio fijo de pinggy o dominio propio (plan Pro).")
                          : pg.borrador["pinggy.dominioTipo"] === "pinggy"
                          ? qsTr("El subdominio se crea en dashboard.pinggy.io → Domains y se asigna al token (queda del estilo nombre.a.pinggy.link). Escribilo tal cual y probalo.")
                          : qsTr("Tu dominio (p. ej. sala.miorganismo.gob.ar): se carga en dashboard.pinggy.io → Domains, se asigna al token y se apunta con un CNAME (abajo, las instrucciones).")
                }
                // Probar la dirección: si pinggy la da con este token (asignada y libre).
                RowLayout {
                    visible: pg.borrador["pinggy.dominioTipo"] !== "azar"
                    spacing: 8
                    GlassButton {
                        compact: true; text: qsTr("Probar dirección"); icon: "✓"
                        enabled: pg.ar.prueba.estado !== "probando"
                        onClicked: pg.ar.probarDireccion(pg.borrador["pinggy.dominioTipo"] || "", pg.borrador["pinggy.host"] || "")
                    }
                    Text {
                        Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.textFaint; font.pixelSize: 11
                        text: qsTr("Abre un túnel de prueba de unos segundos con tu token y se fija si pinggy te da esa dirección (no la tiene otro túnel y está asignada a tu token). Con el túnel de este equipo apagado.")
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
                        text: qsTr("DNS del dominio propio"); color: Theme.text; font { pixelSize: 14; weight: Font.DemiBold }
                    }
                    Text {
                        Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.textDim; font.pixelSize: 12
                        text: qsTr("Esto es lo que tiene que hacer quien administra el dominio. Se arma con lo que cargaste arriba: copialo y mandáselo.")
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
                        GlassButton { compact: true; text: qsTr("Copiar instrucciones"); onClicked: Studio.copiar(instrucciones.text) }
                        GlassButton {
                            compact: true; text: qsTr("Verificar DNS")
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
                    text: qsTr("Video de los invitados de afuera"); color: Theme.text; font { pixelSize: 16; weight: Font.DemiBold }
                }
                Text {
                    Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.textDim; font.pixelSize: 12
                    text: qsTr("Por el túnel solo pasa TCP o UDP hacia un TURN dentro de esta caja: el video de los de afuera entra por ahí (cifrado de punta a punta). Sin esto ven la página pero no se conectan con video.")
                }
                CamposDeSeccion { Layout.fillWidth: true; seccion: "Pinggy"; claves: ["pinggy.turn", "pinggy.turnTcpPuerto", "pinggy.turnUdp", "pinggy.turnUdpPuerto"] }
                Text {
                    Layout.topMargin: 14
                    text: qsTr("Seguridad"); color: Theme.text; font { pixelSize: 16; weight: Font.DemiBold }
                }
                CamposDeSeccion { Layout.fillWidth: true; seccion: "Pinggy"; claves: ["pinggy.listaBlanca", "pinggy.puertoTunel"] }
                Text {
                    Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.textDim; font.pixelSize: 12
                    text: qsTr("Lo que entra por el túnel cuenta siempre como «de afuera»: la botonera no se abre desde internet y la API de control y el asistente nunca pasan por el túnel.")
                }
                CamposDeSeccion { Layout.fillWidth: true; seccion: "Pinggy"; claves: page.clavesAvanzadasPinggy }
                RowLayout {
                    Layout.topMargin: 10
                    spacing: 10
                    GlassButton { compact: true; text: qsTr("Valores por defecto de esta sección"); onClicked: Studio.settings.resetSection("Pinggy") }
                    GlassButton {
                        compact: true; text: qsTr("Reiniciar la conferencia ahora"); icon: "↻"
                        onClicked: ApplicationWindow.window.confirm({
                            title: qsTr("¿Reiniciar la conferencia?"),
                            text: qsTr("Se reinicia solo el servidor de la conferencia (y el túnel) con la configuración guardada. Los invitados se cortan y vuelven a entrar."),
                            accept: qsTr("Reiniciar"), reject: qsTr("Cancelar"),
                            action: function() {
                                const e = Studio.conference.reiniciarSala()
                                Studio.notify(e === "" ? qsTr("La conferencia se está reiniciando.") : e, e === "" ? "ok" : "error")
                            }
                        })
                    }
                }
            }
        }
    }

    // --- Control del sistema --------------------------------------------------------
    // ba-control (src/app/escritorio): el servidor MCP con el que el asistente
    // maneja el escritorio de esta máquina. Los permisos viven en
    // Config/control-sistema.json (agent/ControlSistema) y se aplican en la
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
                property var estado: Studio.agent.controlSistema()
                function cambiar(c) {
                    const e = Studio.agent.setControlSistema(c)
                    if (e !== "") Studio.notify(e, "error")
                    ctl.estado = Studio.agent.controlSistema()
                }
                Text {
                    Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.textDim; font.pixelSize: 12
                    text: qsTr("Le permite al asistente (Claude, Gemini, opencode) manejar el escritorio de esta máquina: ver la pantalla, "
                          + "mover el mouse, escribir, abrir programas, buscar archivos y acomodar ventanas. De fábrica está apagado.")
                }
                Text {
                    visible: ctl.estado.sistema === "macOS"
                    Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.textDim; font.pixelSize: 12
                    text: qsTr("En macOS el control pide dos permisos del sistema, la primera vez que se usan: «Accesibilidad» (mouse, "
                          + "teclado y ventanas) y «Grabación de pantalla» (capturas y títulos de las ventanas), en Ajustes del "
                          + "Sistema › Privacidad y seguridad.")
                }
                Text {
                    visible: ctl.estado.soportado === false
                    Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.warn; font.pixelSize: 12
                    text: qsTr("En %1 el control todavía está en camino: los permisos se guardan, "
                          + "pero las herramientas contestan que aún no están hechas acá (las mismas que en Windows).").arg(ctl.estado.sistema || qsTr("este sistema"))
                }
                RowLayout {
                    Layout.fillWidth: true
                    Text { Layout.fillWidth: true; text: qsTr("Permitir el control del sistema"); color: Theme.text; font { pixelSize: 14; weight: Font.DemiBold } }
                    Toggle { checked: ctl.estado.habilitado === true; onToggled: (quiero) => ctl.cambiar({ habilitado: quiero }) }
                }
                RowLayout {
                    Layout.fillWidth: true
                    enabled: ctl.estado.habilitado === true
                    opacity: enabled ? 1 : 0.5
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 2
                        Text { text: qsTr("Preguntar antes de cada acción"); color: Theme.text; font.pixelSize: 13 }
                        Text {
                            Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.textFaint; font.pixelSize: 11
                            text: qsTr("El asistente le pide permiso al operador en su consola antes de cada clic, tecla o programa. "
                                  + "Apagado, actúa solo (opencode solo recibe el control si esto está apagado: no sabe preguntar).")
                        }
                    }
                    Toggle { checked: ctl.estado.preguntar === true; onToggled: (quiero) => ctl.cambiar({ preguntar: quiero }) }
                }
                Text {
                    Layout.topMargin: 6
                    text: qsTr("Qué puede hacer"); color: Theme.text; font { pixelSize: 13; weight: Font.DemiBold }
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
                    text: qsTr("Los permisos valen enseguida. Prender o apagar el control, o «preguntar», vale al relanzar el asistente.")
                }
                GlassButton {
                    compact: true; text: qsTr("Relanzar el asistente")
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
                            text: ml.estado === "listo" ? qsTr("Modelo listo")
                                : ml.estado === "cargando" ? qsTr("Cargando…")
                                : ml.estado === "error" ? qsTr("El modelo no anda")
                                : qsTr("Apagado")
                            color: Theme.text
                            font { pixelSize: 15; weight: Font.DemiBold }
                        }
                        Text {
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                            text: (ml.detalle !== "" ? ml.detalle + " · " : "") + qsTr("Motor: %1").arg(ml.motor)
                            color: ml.estado === "error" ? Theme.live : Theme.textDim
                            font.pixelSize: 11
                        }
                    }
                    GlassButton {
                        compact: true
                        visible: ml.estado === "listo" || ml.estado === "cargando"
                        text: qsTr("Apagar")
                        onClicked: ml.apagar()
                    }
                    GlassButton { compact: true; text: qsTr("Carpeta"); icon: "📁"; onClicked: ml.abrirCarpeta() }
                }
                Text {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    color: Theme.textFaint
                    font.pixelSize: 11
                    text: qsTr("Elige la herramienta para una frase corta de cabina en una fracción de segundo, sin salir " +
                          "de la máquina. Lo que no es suyo lo deriva al asistente grande, con la frase tal cual.")
                }

                Rectangle { Layout.fillWidth: true; height: 1; color: Theme.glassBorder }

                // Catálogo
                Text { text: qsTr("Modelos para esta máquina"); color: Theme.text; font { pixelSize: 15; weight: Font.DemiBold } }
                Text {
                    visible: ml.catalogo.length === 0
                    text: qsTr("No hay modelos publicados para esta plataforma.")
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
                                          ? qsTr("Voz a texto · %1 · %2 GB · error en castellano %3% (FLEURS) · %4")
                                            .arg(modelData.idiomas).arg(modelData.tamano_gb).arg(modelData.wer_es_fleurs).arg(modelData.licencia)
                                        : modelData.tipo === "voz_streaming"
                                          ? qsTr("Voz a texto en streaming (muchas voces a la vez; se elige en Agentes) · %1 · %2 GB · error en castellano %3% (FLEURS) · %4")
                                            .arg(modelData.idiomas).arg(modelData.tamano_gb).arg(modelData.wer_es_fleurs).arg(modelData.licencia)
                                        : modelData.tipo === "vad"
                                          ? qsTr("Detector de voz (recorta los silencios) · %1").arg(modelData.licencia)
                                        : modelData.tipo === "vision"
                                          ? qsTr("Visión para el modelo local (%1): leer el dorso de un documento en el tótem, " +
                                                 "sin que la foto salga de la máquina · %2 GB · %3").arg(modelData.base).arg(modelData.tamano_gb).arg(modelData.licencia)
                                        : modelData.tipo === "chat"
                                          ? qsTr("Para el chat sin internet (Asistente → Modelo local) · %1 · %2 GB · %3 · %4")
                                            .arg(modelData.formato.toUpperCase()).arg(modelData.tamano_gb).arg(modelData.base).arg(modelData.licencia)
                                        : modelData.formato.toUpperCase() + " · " + modelData.tamano_gb + " GB · " +
                                          (modeloFila.ficha && modeloFila.ficha.estado === "ok" ? "" : qsTr("%1 herramientas (según el catálogo)").arg(modelData.herramientas) + " · ") +
                                          (modelData.precision !== undefined
                                           ? qsTr("precisión %1% · examen %2%").arg(modelData.precision).arg(modelData.examen)
                                           : qsTr("llamada nativa (v2)")) + " · " + modelData.fecha
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
                                            if (f.estado === "buscando") return qsTr("Revisando con qué herramientas se entrenó…")
                                            if (f.estado === "sin_ficha") return f.nota ? qsTr("Sin modelo.json: no se sabe con qué herramientas se entrenó — %1").arg(f.nota)
                                                                                        : qsTr("Sin modelo.json: no se sabe con qué herramientas se entrenó.")
                                            if (f.estado === "error") return qsTr("No se pudo revisar: %1").arg(f.nota || "")
                                            if (f.estado !== "ok") return ""
                                            let t = qsTr("Entrenado con %n herramienta", "", f.entrenadas)
                                            if (f.catalogo_dice !== undefined && String(f.catalogo_dice) !== String(f.entrenadas))
                                                t += " " + qsTr("(el catálogo decía %1)").arg(f.catalogo_dice)
                                            t += " · " + qsTr("%n igual a las de hoy", "", f.iguales)
                                            if (f.cambiaron.length) t += " · " + qsTr("%n cambió", "", f.cambiaron.length)
                                            if (f.no_estan.length) t += " · " + qsTr("%n ya no existe", "", f.no_estan.length)
                                            t += " · " + qsTr("conoce %1 de las %2 del sistema (%3 %)").arg(f.iguales).arg(f.herramientas_app).arg(Math.round(f.cobertura * 100))
                                            return t
                                        }
                                    }
                                    GlassButton {
                                        compact: true
                                        visible: !!modeloFila.ficha && modeloFila.ficha.estado === "ok"
                                        text: modeloFila.verFicha ? qsTr("Ocultar") : qsTr("Ver cuáles")
                                        onClicked: modeloFila.verFicha = !modeloFila.verFicha
                                    }
                                    GlassButton {
                                        compact: true
                                        visible: !!modeloFila.ficha && modeloFila.ficha.estado !== "buscando"
                                        text: "↻"
                                        onClicked: ml.revisarHerramientas(modeloFila.modelData.repo, true)
                                        ToolTip.visible: hovered; ToolTip.delay: 400; ToolTip.text: qsTr("Volver a buscar el modelo.json en Hugging Face")
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
                                        text: !parent.f ? "" : qsTr("Por tema: %1").arg(Object.keys(parent.f.por_categoria).sort((a, b) => parent.f.por_categoria[b] - parent.f.por_categoria[a])
                                              .map(k => k + " " + parent.f.por_categoria[k]).join(" · "))
                                        color: Theme.textDim; font.pixelSize: 11
                                    }
                                    Text {
                                        Layout.fillWidth: true; wrapMode: Text.WordWrap
                                        visible: !!parent.f && parent.f.cambiaron.length > 0
                                        text: !parent.f ? "" : qsTr("Cambiaron desde el entrenamiento (las puede llamar mal): %1").arg(parent.f.cambiaron.join(", "))
                                        color: Theme.amber; font.pixelSize: 11
                                    }
                                    Text {
                                        Layout.fillWidth: true; wrapMode: Text.WordWrap
                                        visible: !!parent.f && parent.f.no_estan.length > 0
                                        text: !parent.f ? "" : qsTr("Ya no existen en el sistema: %1").arg(parent.f.no_estan.join(", "))
                                        color: Theme.amber; font.pixelSize: 11
                                    }
                                    Text {
                                        Layout.fillWidth: true; wrapMode: Text.WordWrap
                                        visible: !!parent.f
                                        text: !parent.f ? "" : qsTr("Las que conoce tal cual: %1").arg(parent.f.lista.join(", "))
                                        color: Theme.textFaint; font { pixelSize: 10; family: Theme.mono }
                                    }
                                    Text {
                                        Layout.fillWidth: true; wrapMode: Text.WordWrap
                                        visible: !!parent.f
                                        text: !parent.f ? "" : qsTr("Versión %1").arg(parent.f.build || "?") + " · " + (parent.f.fecha || "") +
                                              (parent.f.inventario_entrenamiento ? " · " + qsTr("el sistema tenía %n herramienta al entrenarlo", "", parent.f.inventario_entrenamiento.cuantas) : "") +
                                              " · " + qsTr("de %1").arg(parent.f.de) + (parent.f.nota ? ". " + parent.f.nota : "")
                                        color: Theme.textFaint; font.pixelSize: 10
                                    }
                                }
                            }
                            Text {
                                visible: modelData.instalado
                                text: modelData.activo ? qsTr("activo") : modelData.tipo !== "reactivo" ? qsTr("listo") : qsTr("bajado")
                                color: modelData.activo ? Theme.accent : Theme.ok
                                font.pixelSize: 11
                            }
                            GlassButton {
                                compact: true
                                visible: !modelData.instalado
                                enabled: ml.enCola.indexOf(modelData.repo) < 0
                                text: enabled ? qsTr("Bajar") : qsTr("En la cola"); icon: "⇩"
                                onClicked: Studio.notify(modelData.repo + ": " + ml.descargar(modelData.repo), "info")
                            }
                            GlassButton {
                                compact: true
                                visible: modelData.tipo === "reactivo" && modelData.instalado && ml.hayMotor
                                         && !(modelData.activo && (ml.estado === "listo" || ml.estado === "cargando"))
                                text: qsTr("Activar"); active: true
                                onClicked: ml.activar(modelData.repo)
                            }
                            GlassButton {
                                compact: true
                                visible: modelData.instalado
                                enabled: ml.enCola.indexOf(modelData.repo) < 0
                                text: qsTr("Actualizar")
                                onClicked: Studio.notify(modelData.repo + ": " + ml.descargar(modelData.repo), "info")
                            }
                            GlassButton {
                                compact: true
                                visible: modelData.instalado
                                text: qsTr("Borrar")
                                onClicked: ApplicationWindow.window.confirm({
                                    title: qsTr("¿Borrar el modelo?"),
                                    text: qsTr("%1 (%2 GB). Se puede volver a bajar.").arg(modelData.repo).arg(modelData.tamano_gb),
                                    accept: qsTr("Borrar"), reject: qsTr("Cancelar"),
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
                        GlassButton { compact: true; visible: ml.descargando; text: qsTr("Cancelar"); onClicked: ml.cancelarDescarga() }
                        GlassButton { compact: true; text: qsTr("Descargas…"); onClicked: ApplicationWindow.window.abrirDescargas() }
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
                            text: qsTr("En el disco")
                            color: Theme.text; font { pixelSize: 15; weight: Font.DemiBold }
                        }
                        Text {
                            Layout.fillWidth: true
                            text: qsTr("%n modelo · %1", "", ml.instalados.length).arg(disco.gb(disco.suma(ml.instalados)))
                            color: Theme.textDim; font.pixelSize: 12
                        }
                        GlassButton { compact: true; text: qsTr("Revisar"); onClicked: ml.revisarDisco() }
                        GlassButton {
                            compact: true
                            visible: disco.viejos.length > 0
                            text: qsTr("Borrar los viejos (%1 · %2)").arg(disco.viejos.length).arg(disco.gb(disco.suma(disco.viejos)))
                            active: true
                            onClicked: ApplicationWindow.window.confirm({
                                title: qsTr("¿Borrar los modelos viejos?"),
                                text: disco.viejos.map(m => m.repo + " — " + m.motivo).join("\n") +
                                      "\n\n" + qsTr("Si siguen en el catálogo se pueden volver a bajar."),
                                accept: qsTr("Borrar"), reject: qsTr("Cancelar"),
                                action: function() { ml.limpiarViejos() }
                            })
                        }
                        GlassButton {
                            compact: true
                            visible: (ml.restos.cuantas || 0) > 0
                            text: qsTr("Borrar descargas cortadas (%1)").arg(disco.gb(ml.restos.bytes || 0))
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
                                text: modelData.activo ? qsTr("en uso")
                                    : modelData.viejo ? qsTr("viejo: %1").arg(modelData.motivo)
                                    : !modelData.enCatalogo ? qsTr("fuera del catálogo")
                                    : modelData.tipo
                                color: modelData.activo ? Theme.ok : modelData.viejo ? Theme.amber : Theme.textFaint
                                font.pixelSize: 11
                            }
                            GlassButton {
                                compact: true
                                text: qsTr("Borrar")
                                onClicked: ApplicationWindow.window.confirm({
                                    title: qsTr("¿Borrar «%1»?").arg(modelData.repo),
                                    text: (modelData.activo ? qsTr("%1. Es el que está en uso: se apaga.").arg(disco.gb(modelData.bytes)) : disco.gb(modelData.bytes) + ".") + " " +
                                          (modelData.enCatalogo ? qsTr("Se puede volver a bajar.") : qsTr("Ya no está en el catálogo: no se puede volver a bajar desde acá.")),
                                    accept: qsTr("Borrar"), reject: qsTr("Cancelar"),
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
                Text { text: qsTr("Cuánto deriva"); color: Theme.text; font { pixelSize: 15; weight: Font.DemiBold } }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 10
                    Text { text: qsTr("atiende más"); color: Theme.textDim; font.pixelSize: 11 }
                    Slider {
                        Layout.fillWidth: true
                        from: -4; to: 4; stepSize: 0.5
                        value: ml.sesgo
                        onMoved: ml.sesgo = value
                    }
                    Text { text: qsTr("deriva más"); color: Theme.textDim; font.pixelSize: 11 }
                    Text { text: (ml.sesgo > 0 ? "+" : "") + ml.sesgo.toFixed(1); color: Theme.text; font.pixelSize: 12 }
                }
                Text {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    color: Theme.textFaint
                    font.pixelSize: 11
                    text: qsTr("Derivar de más cuesta segundos; elegir mal cuesta al aire. Con +2 pasa al asistente grande " +
                          "más de 9 de cada 10 pedidos que no son suyos, y atiende un poco menos de lo propio.")
                }

                Text { text: qsTr("Cabeza MTP (texto rápido)"); color: Theme.text; font { pixelSize: 15; weight: Font.DemiBold } }
                RowLayout {
                    spacing: 6
                    Repeater {
                        model: [ { v: "auto", t: qsTr("Automática") }, { v: "q8", t: qsTr("8 bits") },
                                 { v: "fp16", t: "fp16" }, { v: "no", t: qsTr("Sin MTP") } ]
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
                    text: qsTr("La decodificación especulativa con la cabeza del propio modelo. Si acepta poco, el motor deja " +
                          "de usarla solo. Se aplica la próxima vez que se activa el modelo.")
                }
                RowLayout {
                    spacing: 10
                    Toggle { checked: ml.alArrancar; onToggled: (w) => ml.alArrancar = w }
                    Text { text: qsTr("Cargar el modelo activo al abrir la aplicación"); color: Theme.text; font.pixelSize: 13 }
                }

                Rectangle { Layout.fillWidth: true; height: 1; color: Theme.glassBorder }

                // Probar
                Text { text: qsTr("Probar"); color: Theme.text; font { pixelSize: 15; weight: Font.DemiBold } }
                RowLayout {
                    Layout.fillWidth: true
                    TextField {
                        id: fraseF
                        Layout.fillWidth: true
                        enabled: ml.estado === "listo"
                        placeholderText: ml.estado === "listo" ? qsTr("una frase de cabina: «bajale al mic 2»") : qsTr("activá un modelo para probar")
                        onAccepted: { ml.probar(text); text = "" }
                    }
                    GlassButton {
                        compact: true; text: qsTr("Elegir"); active: true
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
                        placeholderText: qsTr("pregunta sobre una imagen: «¿cuántas personas hay?»")
                    }
                    GlassButton {
                        compact: true; text: qsTr("Elegir imagen…"); icon: "🖼"
                        onClicked: imagenDlg.open()
                    }
                    FileDialog {
                        id: imagenDlg
                        title: qsTr("Imagen para el modelo local")
                        nameFilters: [qsTr("Imágenes (*.png *.jpg *.jpeg *.webp *.bmp)")]
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
                                : modelData.derivar ? qsTr("→ derivar al grande")
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
    // «Voz del sistema»: dos pestañas. «Voz del operador» es lo de siempre (su
    // micrófono, sus auriculares, la voz que le contesta a él); «Voz del
    // atendedor», la voz con la que el asistente le habla a quien atiende en el
    // lobby (PanelVozAtendedor). Lo de la primera pestaña no se reindentó al
    // meterlo en la pestaña, para no mover todo el bloque.
    Component {
        id: vozComp
        ColumnLayout {
        spacing: 10
        TabBar {
            id: vozTabs
            Layout.fillWidth: true
            TabButton { text: qsTr("Voz del operador") }
            TabButton { text: qsTr("Voz del atendedor") }
        }
        StackLayout {
        Layout.fillWidth: true
        Layout.fillHeight: true
        currentIndex: vozTabs.currentIndex
        ScrollView {
            clip: true
            contentWidth: availableWidth
            ColumnLayout {
                width: parent.width
                spacing: 14
                readonly property var vz: Studio.voz

                Text { text: qsTr("Micrófono del operador"); color: Theme.text; font { pixelSize: 15; weight: Font.DemiBold } }
                RowLayout {
                    Layout.fillWidth: true
                    ComboBox {
                        id: micCombo
                        Layout.fillWidth: true
                        readonly property var nombres: [qsTr("— ninguno —")].concat(
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
                    text: qsTr("Se elige entre las entradas con audio (se agregan en Entradas). Es lo que te oye quien " +
                          "espera en el lobby y, si no hay otra fuente con «Voz a texto» en dictado, de donde se " +
                          "le dicta al asistente. Mientras apretás %1, el lobby no te oye.").arg(Studio.voz.tecla)
                }

                Rectangle { Layout.fillWidth: true; height: 1; color: Theme.glassBorder }

                Text { text: qsTr("Salida de auriculares del operador"); color: Theme.text; font { pixelSize: 15; weight: Font.DemiBold } }
                Text {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    color: Theme.textDim; font.pixelSize: 12
                    text: qsTr("Por acá suenan la voz del asistente y la escucha de los racks (nunca al aire). Es una sola " +
                          "para toda la app: cambiarla acá la cambia también en Audio.")
                }
                Flow {
                    Layout.fillWidth: true
                    spacing: 6
                    GlassButton {
                        compact: true; text: qsTr("La del sistema")
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

                Text { text: qsTr("Hablarle al asistente"); color: Theme.text; font { pixelSize: 15; weight: Font.DemiBold } }
                RowLayout {
                    Layout.fillWidth: true
                    visible: Studio.voz.disponible
                    spacing: 10
                    Text { text: "🎙"; font.pixelSize: 16 }
                    Text {
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        text: qsTr("Hablarle al asistente: %1").arg(Studio.voz.estado) +
                              (Studio.voz.ultimo !== "" ? "  ·  " + qsTr("último: «%1»").arg(Studio.voz.ultimo) : "")
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
                          ? qsTr("Ninguna fuente tiene «Voz a texto» todavía: al apretar %1" +
                            " se le pone sola al micrófono del operador. En Audio se agrega a cualquier fuente " +
                            "(dictado o transcripción continua), como una salida.").arg(Studio.voz.tecla)
                          : qsTr("Fuentes con voz a texto (se cambian en Audio):")
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
                            text: modelData.modo === "continuo" ? qsTr("transcripción continua") : qsTr("dictado (%1)").arg(Studio.voz.tecla)
                            color: Theme.textDim; font.pixelSize: 12
                        }
                        GlassButton { compact: true; text: qsTr("Quitar"); onClicked: Studio.voz.setTexto(modelData.fuente, "") }
                    }
                }

                Rectangle { Layout.fillWidth: true; height: 1; color: Theme.glassBorder }


                Text { text: qsTr("Lo dictado y el modelo local"); color: Theme.text; font { pixelSize: 15; weight: Font.DemiBold } }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 10
                    Toggle { checked: Studio.voz.usarModeloLocal; onToggled: (w) => Studio.voz.usarModeloLocal = w }
                    Text {
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        color: Theme.text; font.pixelSize: 13
                        text: qsTr("Pasar lo dictado por el modelo local: elige la acción al instante y la propone en la " +
                              "barra del asistente para que la confirmes. Lo que no es suyo, o sin modelo listo, va " +
                              "al asistente como siempre.")
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
                        text: qsTr("Contestar con voz (solo en tus auriculares): «listo», lo que no se pudo y por qué, y " +
                              "la respuesta cuando preguntás algo («¿qué escena está al aire?»).")
                    }
                }
                Text {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    color: Theme.textDim; font.pixelSize: 12
                    text: Studio.voz.automaticas.length === 0
                          ? qsTr("Todavía no hay acciones que se hagan sin preguntar. Se agregan con «Hacer y no volver a " +
                            "preguntar» al confirmar una.")
                          : qsTr("Se hacen sin preguntar (las que piden datos los completa el asistente):")
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
                        GlassButton { compact: true; text: qsTr("Volver a preguntar"); onClicked: Studio.voz.setAutomatica(modelData.herramienta, false) }
                    }
                }

                Rectangle { Layout.fillWidth: true; height: 1; color: Theme.glassBorder }

                // La voz que contesta (solo por los auriculares del operador)
                Text {
                    visible: Studio.voz.disponible
                    text: qsTr("Voz que contesta"); color: Theme.text; font { pixelSize: 15; weight: Font.DemiBold }
                }
                Text {
                    visible: Studio.voz.disponible
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    color: Theme.textFaint
                    font.pixelSize: 11
                    text: qsTr("Suena SOLO por el monitoreo (los auriculares del operador), nunca al aire. Frase por " +
                          "frase, así empieza enseguida. Si apretás la tecla para hablar, se calla.")
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
                                    text: qsTr("calidad %1").arg(modelData.calidad) + " · " + Math.round(modelData.tamano_gb * 1024) + " MB"
                                    color: Theme.textDim; font.pixelSize: 11
                                }
                            }
                            GlassButton {
                                compact: true
                                visible: !modelData.instalado
                                enabled: ml.enCola.indexOf(modelData.repo) < 0
                                text: enabled ? qsTr("Bajar") : qsTr("En la cola"); icon: "⇩"
                                onClicked: Studio.notify(modelData.repo + ": " + ml.descargar(modelData.repo), "info")
                            }
                            GlassButton {
                                compact: true
                                visible: modelData.instalado
                                text: modelData.elegida ? qsTr("Elegida") : qsTr("Elegir")
                                active: modelData.elegida
                                onClicked: Studio.voz.elegirVoz(modelData.repo)
                            }
                            GlassButton {
                                compact: true
                                visible: modelData.instalado
                                text: qsTr("Borrar")
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
                            text: qsTr("Voz %1").arg(index + 1)
                            active: Studio.voz.hablante === index
                            onClicked: Studio.voz.hablante = index
                        }
                    }
                    Text { text: qsTr("más lento"); color: Theme.textDim; font.pixelSize: 11 }
                    Slider {
                        Layout.fillWidth: true
                        from: 0.7; to: 1.5; stepSize: 0.05
                        value: Studio.voz.velocidad
                        onMoved: Studio.voz.velocidad = value
                    }
                    Text { text: qsTr("más rápido"); color: Theme.textDim; font.pixelSize: 11 }
                }
                RowLayout {
                    Layout.fillWidth: true
                    visible: Studio.voz.vozLista
                    TextField {
                        id: decirF
                        Layout.fillWidth: true
                        placeholderText: qsTr("escribí algo para que lo diga (por los auriculares)")
                        onAccepted: { Studio.voz.decir(text); text = "" }
                    }
                    GlassButton {
                        compact: true; text: decirF.text.trim() !== "" ? qsTr("Decir") : qsTr("Probar"); icon: "🔊"; active: true
                        onClicked: {
                            if (decirF.text.trim() !== "") { Studio.voz.decir(decirF.text); decirF.text = "" }
                            else Studio.voz.probarVoz()
                        }
                    }
                    GlassButton {
                        compact: true; text: qsTr("Callar")
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
                    text: qsTr("Transcripción"); color: Theme.text; font { pixelSize: 15; weight: Font.DemiBold }
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
                    compact: true; text: qsTr("Borrar la transcripción")
                    onClicked: Studio.voz.borrarTranscripcion()
                }
                // Los del esquema (la voz del prompter): avanzados.
                CamposDeSeccion { Layout.fillWidth: true; seccion: page.vozSection }
            }
        }
        PanelVozAtendedor {}
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
                    text: qsTr("Barra de navegación")
                    color: Theme.text
                    font { pixelSize: 17; weight: Font.DemiBold }
                }
                Text {
                    text: qsTr("Dónde va")
                    color: Theme.text
                    font { pixelSize: 15; weight: Font.DemiBold }
                }
                RowLayout {
                    spacing: 6
                    Repeater {
                        model: [
                            { v: "izquierda", t: qsTr("Izquierda"), i: "◧" },
                            { v: "derecha",   t: qsTr("Derecha"),   i: "◨" },
                            { v: "arriba",    t: qsTr("Arriba"),    i: "⬒" },
                            { v: "abajo",     t: qsTr("Abajo"),     i: "⬓" }
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
                    text: qsTr("Arriba o abajo conviene en pantallas anchas y bajas: la barra se lleva " +
                          "menos altura, que es la que falta cuando hay una previsualización grande.")
                }

                Rectangle { Layout.fillWidth: true; height: 1; color: Theme.glassBorder }

                Text {
                    text: qsTr("Tamaño")
                    color: Theme.text
                    font { pixelSize: 15; weight: Font.DemiBold }
                }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 10
                    Repeater {
                        model: [ { v: 34, t: qsTr("Chico") }, { v: 42, t: qsTr("Normal") }, { v: 54, t: qsTr("Grande") } ]
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
                        text: qsTr("Separar los grupos con una línea")
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
                            text: qsTr("Abrir en árbol al pasarle el puntero")
                            color: Studio.riel.vertical ? Theme.text : Theme.textFaint
                            font.pixelSize: 13
                        }
                        Text {
                            text: Studio.riel.vertical
                                  ? qsTr("Se eleva y muestra el nombre de cada página. Flota: no corre la página.")
                                  : qsTr("Solo con la barra a la izquierda o a la derecha.")
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
                        text: qsTr("Ancho abierta")
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
                        text: qsTr("Qué mostrar")
                        color: Theme.text
                        font { pixelSize: 15; weight: Font.DemiBold }
                    }
                    Text {
                        visible: Studio.riel.escondidas > 0
                        text: qsTr("%n escondida", "", Studio.riel.escondidas)
                        color: Theme.textDim
                        font.pixelSize: 12
                    }
                    Item { Layout.fillWidth: true }
                    GlassButton {
                        compact: true; text: qsTr("Mostrar todas"); icon: "↺"
                        enabled: Studio.riel.escondidas > 0
                        onClicked: Studio.riel.restaurar()
                    }
                }
                Text {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    color: Theme.textFaint
                    font.pixelSize: 11
                    text: qsTr("Una página escondida sigue existiendo: el asistente la sigue usando y se " +
                          "puede abrir en su propia ventana. Lo único que se esconde es su botón.")
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
        title: qsTr("Carpeta de la version nueva")
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
        title: qsTr("Actualizar el sistema")
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
                text: qsTr("La app se cierra, se reemplaza el programa por el de\n%1" +
                           "\ny vuelve a abrir sola. Configuración, credenciales, grabaciones y archivos NO se tocan (antes se respaldan). Si algo sale mal, podés volver a esta versión.")
                      .arg(updConfirm.folder.toString().replace("file://", ""))
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
                       text: qsTr("Todo el sistema vive en una sola carpeta (sirve desde un pendrive): %1").arg(sys.dataRoot) }
                Text { Layout.fillWidth: true; color: Theme.textDim; font.pixelSize: 13; visible: sys.version !== ""
                       text: qsTr("Versión instalada: %1").arg(sys.version) }
                Rectangle { Layout.fillWidth: true; height: 1; color: Theme.glassBorder }
                // Configuraciones con nombre (cada una con lo suyo).
                PanelConfiguraciones { Layout.fillWidth: true }
                Rectangle { Layout.fillWidth: true; height: 1; color: Theme.glassBorder }
                Text { text: qsTr("Arranque y recuperación"); color: Theme.text; font { pixelSize: 15; weight: Font.DemiBold } }
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
                        Text { text: qsTr("Arrancar solo al encender el equipo"); color: Theme.text; font.pixelSize: 14 }
                    }
                    Text { Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.textDim; font.pixelSize: 12
                           text: qsTr("Si se corta la luz con la transmisión o la grabación andando, al volver el programa las retoma solo. Para que vuelva sin que nadie toque nada, la máquina tiene que estar preparada:") }
                    // La tarjeta de preparación (F1-11): lo que se leyó del sistema, punto por
                    // punto. No se cambia nada: cada uno dice cómo arreglarlo (suele pedir administrador).
                    Repeater {
                        model: sys.preparacion
                        delegate: RowLayout {
                            required property var modelData
                            Layout.fillWidth: true
                            spacing: 8
                            Rectangle {
                                Layout.alignment: Qt.AlignTop
                                Layout.topMargin: 5
                                width: 8; height: 8; radius: 4
                                color: modelData.estado === "ok" ? Theme.ok
                                     : modelData.estado === "falta" ? Theme.live
                                     : modelData.estado === "revisar" ? Theme.warn : Theme.textDim
                            }
                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 2
                                Text { Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.text; font.pixelSize: 13
                                       text: modelData.titulo + (modelData.estado === "revisar" ? "  " + qsTr("(revisalo a mano)")
                                                               : modelData.estado === "no_aplica" ? "  " + qsTr("(no hace falta)") : "") }
                                Text { Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.textDim; font.pixelSize: 12
                                       text: modelData.detalle }
                                Text { Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.textDim; font.pixelSize: 12
                                       visible: (modelData.como || "") !== "" && modelData.estado !== "ok"
                                       text: "→ " + modelData.como }
                            }
                        }
                    }
                    Button { text: qsTr("Volver a revisar"); onClicked: sys.refresh() }
                    Text { visible: inicioBox.error !== ""; text: inicioBox.error; color: Theme.live; font.pixelSize: 12 }
                }
                // Los del esquema (copia sombra, diálogo de recuperación): van con Aplicar.
                CamposDeSeccion { Layout.fillWidth: true; seccion: page.systemSection }
                Rectangle { Layout.fillWidth: true; height: 1; color: Theme.glassBorder }
                Text { text: qsTr("Carpetas"); color: Theme.text; font { pixelSize: 15; weight: Font.DemiBold } }
                GridLayout {
                    Layout.fillWidth: true
                    columns: 2
                    columnSpacing: 12
                    rowSpacing: 8
                    Repeater {
                        model: [
                            { n: "Config", d: qsTr("Configuración (escenas, ajustes, tablero)") },
                            { n: "Credenciales", d: qsTr("Tokens y claves, cifrados (AES-256-GCM). Una actualización nunca los toca.") },
                            { n: "Recordings", d: qsTr("Grabaciones") },
                            { n: "Shares", d: qsTr("Archivos que comparten los invitados") },
                            { n: "Respaldos", d: qsTr("Copias de Config/ y Credenciales/ antes de cada actualización (quedan 5)") },
                            { n: "Logs", d: qsTr("Registros para diagnóstico") }
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
                Text { text: qsTr("Credenciales guardadas (%1)").arg(sys.credentials.length); color: Theme.text; font { pixelSize: 15; weight: Font.DemiBold } }
                Flow {
                    Layout.fillWidth: true
                    spacing: 6
                    Repeater {
                        model: sys.credentials
                        delegate: StatusPill { required property string modelData; text: modelData; dot: Theme.ok }
                    }
                }
                Text { Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.textFaint; font.pixelSize: 11
                       text: qsTr("Sin contraseña: el cifrado evita que se lean por accidente (un pendrive FAT/exFAT no tiene permisos), pero quien se lleve el pendrive entero tiene la clave del equipo (Credenciales/clave-equipo). Para mudar el equipo a otro pendrive, copia la carpeta Credenciales/ completa.") }
                Rectangle { Layout.fillWidth: true; height: 1; color: Theme.glassBorder }
                Text { text: qsTr("Actualizar"); color: Theme.text; font { pixelSize: 15; weight: Font.DemiBold } }
                Text { Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.textDim; font.pixelSize: 12
                       text: qsTr("Descomprimí la versión nueva en cualquier carpeta (fuera de esta) y elegila. Se reemplaza solo el programa; Config/, Credenciales/, Recordings/, Shares/ y Logs/ quedan como están.") }
                RowLayout {
                    GlassButton { text: qsTr("Actualizar desde una carpeta…"); icon: "⇪"; active: true; onClicked: updateDlg.open() }
                    GlassButton { text: qsTr("Volver a la versión anterior"); visible: sys.canRollback; onClicked: page.confirmRollback() }
                }
                Text { Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.live; font.pixelSize: 12; text: page.updateError; visible: text !== "" }
                Text { visible: sys.backups.length > 0; text: qsTr("Respaldos"); color: Theme.text; font { pixelSize: 15; weight: Font.DemiBold } }
                Repeater {
                    model: sys.backups
                    delegate: RowLayout {
                        required property var modelData
                        Text { text: modelData.name; color: Theme.textDim; font.pixelSize: 12; Layout.fillWidth: true }
                        GlassButton { compact: true; text: qsTr("Abrir"); onClicked: sys.openFolder("Respaldos/" + modelData.name) }
                    }
                }
                Rectangle { Layout.fillWidth: true; height: 1; color: Theme.glassBorder }
                Text { text: qsTr("Diagnóstico"); color: Theme.text; font { pixelSize: 15; weight: Font.DemiBold } }
                // Los elementos de GStreamer que faltan (P0.3): que no sean silencio o negro sin explicación.
                Text {
                    readonly property var faltan: sys.elementosQueFaltan()
                    Layout.fillWidth: true; wrapMode: Text.WordWrap; font.pixelSize: 12
                    color: faltan.filter(e => e.imprescindible).length > 0 ? Theme.live : Theme.textDim
                    text: faltan.length === 0 ? qsTr("GStreamer: están todos los elementos que usa el aparato.")
                          : qsTr("GStreamer: faltan %1").arg(faltan.map(e => e.elemento + " (" + e.para + ")").join(", "))
                }
                // Los indicadores de servicio (F4-01): los arma RecursosViewModel cada segundo
                // con las métricas del proceso y lo que mide el gateway (también en GET /metrics).
                Text { text: qsTr("Salud del servicio"); color: Theme.text; font { pixelSize: 13; weight: Font.DemiBold } }
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 4
                    Repeater {
                        model: Studio.recursos ? Studio.recursos.slis : []
                        delegate: RowLayout {
                            required property var modelData
                            Layout.fillWidth: true
                            spacing: 8
                            Rectangle {
                                width: 8; height: 8; radius: 4
                                color: modelData.estado === "critico" ? Theme.live
                                     : modelData.estado === "aviso" ? Theme.warn
                                     : modelData.estado === "normal" ? Theme.ok : Theme.textDim
                            }
                            Text { text: modelData.titulo; color: Theme.textDim; font.pixelSize: 12; Layout.preferredWidth: 260; elide: Text.ElideRight }
                            Text { text: modelData.texto; color: Theme.text; font.pixelSize: 12; Layout.fillWidth: true; wrapMode: Text.WordWrap }
                        }
                    }
                }
            }
        }
    }

    // --- El idioma cambia al reiniciar --------------------------------------------
    Rectangle {
        id: avisoIdioma
        property bool masTarde: false
        property string error: ""
        anchors { left: parent.left; right: parent.right; bottom: parent.bottom; leftMargin: page.leftInset + 20; rightMargin: 20
                  bottomMargin: Studio.settings.dirty ? 16 + 56 + 10 : 16 }
        height: 56
        radius: 18
        visible: Studio.settings.idiomaPendiente && !masTarde
        color: Qt.rgba(0.25, 0.5, 1, 0.16)
        border.color: Qt.rgba(0.45, 0.65, 1, 0.6)
        RowLayout {
            anchors { fill: parent; leftMargin: 18; rightMargin: 12 }
            Text {
                text: avisoIdioma.error !== "" ? avisoIdioma.error : qsTr("El idioma cambia al reiniciar el programa")
                color: Theme.text; font.pixelSize: 13; Layout.fillWidth: true; elide: Text.ElideRight
            }
            GlassButton { text: qsTr("Más tarde"); onClicked: avisoIdioma.masTarde = true }
            GlassButton {
                text: qsTr("Reiniciar ahora"); active: true
                onClicked: avisoIdioma.error = Studio.app.reiniciarPrograma(qsTr("Cambiando el idioma: la app se reinicia"))
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
            Text { text: qsTr("Hay cambios sin aplicar"); color: Theme.text; font.pixelSize: 13; Layout.fillWidth: true }
            GlassButton { text: qsTr("Revertir"); onClicked: Studio.settings.revert() }
            GlassButton { text: qsTr("Aplicar"); active: true; onClicked: Studio.settings.apply() }
        }
    }
}
