// La página web del evento en vivo (evento/EventoWeb.hpp): la dirección pública,
// cómo se ve y sus páginas (una por sección, portada y cierre), que viven en la
// carpeta Web/ del evento. El que las personaliza de verdad es el asistente
// (evento_web); la plantilla que se genera sola es la base y el ejemplo.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import BA

ScrollView {
    id: pw
    contentWidth: availableWidth
    clip: true
    readonly property var ev: Studio.evento
    readonly property var w: ev.web || ({})
    readonly property var sitio: w.sitio || ({})
    readonly property var ar: Studio.accesoRemoto
    // La dirección pública la decide la sala (sala/Direcciones): el túnel que esté listo.
    readonly property var dirEventoSala: Studio.direcciones && Studio.direcciones.porId.evento ? Studio.direcciones.porId.evento : ({})
    readonly property string urlPublica: dirEventoSala.remoto || ""
    readonly property string dirEvento: sitio.subdominio ? sitio.subdominio + ".a.pinggy.link" : ""
    readonly property bool esLaDelEvento: dirEvento !== "" && urlPublica.indexOf("//" + dirEvento) >= 0
    /// Lo pide la página (EventoPage.pedirAsistente).
    signal pedirAsistente(string texto)

    function guardar(campos) {
        const e = pw.ev.setWebSitio(campos)
        if (e !== "") Studio.notify(e, "error")
    }

    ColumnLayout {
        width: pw.availableWidth - 4
        spacing: 12

        Text {
            Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.textDim; font.pixelSize: 12
            text: qsTr("La página que ve el público en la dirección del evento: la de la sección que se está tratando, que cambia sola "
                       + "cuando avanza el guion, con una franja «en vivo» arriba. Son archivos HTML en la carpeta Web/ del evento (quedan "
                       + "en el historial). Se genera una plantilla básica sola; el asistente la personaliza (textos, imágenes, diseño).")
        }

        EstadoVacio {
            Layout.fillWidth: true
            Layout.topMargin: 20
            visible: !pw.w.hay
            icono: "◍"
            titulo: qsTr("Este evento todavía no tiene página web")
            texto: qsTr("Se genera una página por sección del guion, más una portada y un cierre. Después las personaliza el asistente o las editás a mano.")
            accion: qsTr("Generar la página web")
            onActivado: { const r = pw.ev.generarWeb(false); if (r.error) Studio.notify(r.error, "error") }
        }

        // --- La dirección -------------------------------------------------------------
        Seccion {
            visible: !!pw.w.hay
            titulo: qsTr("La dirección del evento")
            subtitulo: qsTr("Con la API key de pinggy, la dirección pública pasa a ser la del evento: la raíz muestra esta página y los invitados entran por /invitados/.")
            extra: GlassButton {
                compact: true; text: "QR"; enabled: pw.urlPublica !== ""
                onClicked: Studio.mostrarQr(pw.urlPublica, qsTr("La página del evento en vivo"), "", "verde")
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                Text { text: qsTr("Ahora:"); color: Theme.textDim; font.pixelSize: 12 }
                Text {
                    Layout.fillWidth: true; elide: Text.ElideRight; font.pixelSize: 13
                    color: pw.urlPublica !== "" ? Theme.text : Theme.textFaint
                    text: pw.urlPublica !== "" ? pw.urlPublica : (pw.dirEventoSala.motivo_remoto || qsTr("sin dirección pública (prendé el acceso remoto en Configuración)"))
                }
                GlassButton { compact: true; text: qsTr("Abrir"); enabled: pw.urlPublica !== ""; onClicked: Qt.openUrlExternally(pw.urlPublica) }
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                Text { text: qsTr("Subdominio del evento"); color: Theme.textDim; font.pixelSize: 12 }
                TextField {
                    id: subCampo
                    Layout.preferredWidth: 240
                    text: pw.sitio.subdominio || ""
                    placeholderText: qsTr("entrevistapepito")
                    inputMethodHints: Qt.ImhNoAutoUppercase | Qt.ImhNoPredictiveText
                    onEditingFinished: if (text !== (pw.sitio.subdominio || "")) pw.guardar({ subdominio: text })
                }
                Text { text: ".a.pinggy.link"; color: Theme.textFaint; font.pixelSize: 12 }
                Item { Layout.fillWidth: true }
                GlassButton {
                    text: pw.esLaDelEvento ? qsTr("Ya es la del evento") : qsTr("Usar esta dirección ahora")
                    active: !pw.esLaDelEvento
                    enabled: !pw.esLaDelEvento && pw.dirEvento !== "" && pw.ar && pw.ar.apiKeyGuardada
                    onClicked: ApplicationWindow.window.confirm({
                        title: qsTr("¿Pasar la dirección a %1?").arg(pw.dirEvento),
                        text: qsTr("Se cambia en tu cuenta de pinggy y la conferencia se reinicia (los que estén conectados vuelven a entrar). "
                                   + "Los enlaces repartidos con la dirección anterior dejan de andar."),
                        accept: qsTr("Cambiar"), reject: qsTr("Cancelar"),
                        action: function() { pw.ar.configurarConApiKey(pw.sitio.subdominio) } })
                }
            }
            RowLayout {
                spacing: 8
                Toggle { checked: pw.sitio.direccion_al_iniciar !== false; onToggled: (v) => pw.guardar({ direccion_al_iniciar: v }) }
                Text { text: qsTr("Cambiarla sola al poner el evento en curso"); color: Theme.text; font.pixelSize: 12 }
            }
            Text {
                Layout.fillWidth: true; wrapMode: Text.WordWrap; font.pixelSize: 11
                visible: !pw.ar || !pw.ar.apiKeyGuardada || !!(pw.ar.cuenta && pw.ar.cuenta.texto)
                color: pw.ar && pw.ar.cuenta && pw.ar.cuenta.estado === "error" ? Theme.live : Theme.textFaint
                text: !pw.ar || !pw.ar.apiKeyGuardada ? qsTr("Falta la API key de pinggy (Configuración → Pinggy).") : ((pw.ar.cuenta && pw.ar.cuenta.texto) || "")
            }
        }

        // --- En el momento: el aviso y la participación ---------------------------------------
        Seccion {
            visible: !!pw.w.hay
            titulo: qsTr("En el momento")
            subtitulo: qsTr("Un aviso arriba de la página (se ve ya, en todas las secciones) y lo que usa este evento.")
            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                TextField {
                    id: avisoCampo
                    Layout.fillWidth: true
                    placeholderText: qsTr("Arrancamos en 5 minutos · Hay un corte técnico, ya volvemos…")
                    text: (pw.w.aviso || {}).texto || ""
                    onAccepted: pw.ev.setAvisoWeb(text, urgente.checked ? "urgente" : "info")
                }
                CheckBox { id: urgente; text: qsTr("Urgente"); checked: (pw.w.aviso || {}).nivel === "urgente" }
                GlassButton { compact: true; text: qsTr("Publicar"); active: avisoCampo.text !== ""
                              onClicked: pw.ev.setAvisoWeb(avisoCampo.text, urgente.checked ? "urgente" : "info") }
                GlassButton { compact: true; text: qsTr("Sacar"); enabled: !!(pw.w.aviso || {}).texto
                              onClicked: { avisoCampo.text = ""; pw.ev.setAvisoWeb("", "info") } }
            }
            // Lo que usa el evento (está en su definición: lo leen la página y el asistente).
            RowLayout {
                spacing: 8
                Toggle { checked: pw.ev.guion.invitados === true; onToggled: (v) => { pw.ev.setInvitados(v); pw.ev.guardar() } }
                Text { text: qsTr("Usa invitados en la conferencia (la página ofrece «Entrar a la conferencia»)"); color: Theme.text; font.pixelSize: 12 }
            }
            RowLayout {
                spacing: 8
                Toggle { checked: pw.ev.guion.asamblea === true; onToggled: (v) => { pw.ev.setAsamblea(v); pw.ev.guardar() } }
                Text { text: qsTr("Es una sesión de asamblea (integrantes y presentes, quórum, orden del día y votaciones oficiales)"); color: Theme.text; font.pixelSize: 12 }
            }
            RowLayout {
                spacing: 8
                Toggle { checked: pw.ev.guion.preguntas === true || pw.sitio.participacion === true
                         onToggled: (v) => { pw.ev.setPreguntas(v); pw.ev.guardar(); if (!v && pw.sitio.participacion) pw.guardar({ participacion: false }) } }
                Text { text: qsTr("El público puede preguntar y votar (la página muestra el enlace y las preguntas moderadas)"); color: Theme.text; font.pixelSize: 12 }
            }
        }

        // --- Dónde se ve en vivo ----------------------------------------------------------------
        Seccion {
            visible: !!pw.w.hay
            titulo: qsTr("Dónde se ve en vivo")
            subtitulo: qsTr("La página muestra el vivo embebido (YouTube primero) y botones para abrirlo en cada plataforma. Se toman solos de las cuentas "
                            + "conectadas (el vivo que transmite la app, el canal de Twitch, el vivo de la página de Facebook); acá se pueden fijar para este evento, "
                            + "p. ej. un vivo programado de YouTube. También en /api/v1/evento/vivos.")
            component CampoVivo: RowLayout {
                property string clave
                property string etiqueta
                property string ayuda
                Layout.fillWidth: true
                spacing: 8
                Text { text: etiqueta; color: Theme.textDim; font.pixelSize: 12; Layout.preferredWidth: 80 }
                TextField {
                    Layout.fillWidth: true
                    text: (pw.sitio.vivos || {})[parent.clave] || ""
                    placeholderText: parent.ayuda
                    inputMethodHints: Qt.ImhNoAutoUppercase | Qt.ImhNoPredictiveText | Qt.ImhUrlCharactersOnly
                    onEditingFinished: {
                        if (text === ((pw.sitio.vivos || {})[parent.clave] || "")) return
                        const v = {}
                        v[parent.clave] = text
                        pw.guardar({ vivos: v })
                    }
                }
            }
            CampoVivo { clave: "youtube"; etiqueta: "YouTube"; ayuda: qsTr("vacío = el vivo de la cuenta; o el link del vivo (youtu.be/…) o del canal") }
            CampoVivo { clave: "twitch"; etiqueta: "Twitch"; ayuda: qsTr("vacío = el canal de la cuenta; o el nombre del canal") }
            CampoVivo { clave: "facebook"; etiqueta: "Facebook"; ayuda: qsTr("vacío = el vivo de la página; o el link del vivo") }
        }

        // --- Cómo se ve -------------------------------------------------------------------
        Seccion {
            visible: !!pw.w.hay
            titulo: qsTr("Cómo se ve")
            subtitulo: qsTr("El título y la bajada de la plantilla. El acento es el del tema del evento, que vale para todas sus páginas (el resto, en Servicios → Aspecto de las páginas web).")
            GridLayout {
                Layout.fillWidth: true
                columns: 2
                columnSpacing: 10; rowSpacing: 8
                Text { text: qsTr("Título"); color: Theme.textDim; font.pixelSize: 12 }
                TextField { Layout.fillWidth: true; text: pw.sitio.titulo || ""; placeholderText: pw.ev.guion.nombre || qsTr("el nombre del evento")
                            onEditingFinished: if (text !== (pw.sitio.titulo || "")) pw.guardar({ titulo: text }) }
                Text { text: qsTr("Bajada"); color: Theme.textDim; font.pixelSize: 12 }
                TextField { Layout.fillWidth: true; text: pw.sitio.bajada || ""; placeholderText: qsTr("una línea abajo del título")
                            onEditingFinished: if (text !== (pw.sitio.bajada || "")) pw.guardar({ bajada: text }) }
                Text { text: qsTr("Acento"); color: Theme.textDim; font.pixelSize: 12 }
                RowLayout {
                    spacing: 6
                    Repeater {
                        model: ["#ff4d2e", "#e53935", "#43a047", "#1e88e5", "#8e24aa", "#fbc02d", "#00acc1", "#ffffff"]
                        delegate: Rectangle {
                            required property string modelData
                            implicitWidth: 26; implicitHeight: 26; radius: 13; color: modelData
                            // El acento del TEMA del evento (TemaEventoViewModel): uno solo para todas las páginas.
                            readonly property bool elegido: ((Studio.temaEvento.resuelto.colores || {}).acento || "") === modelData
                            border.width: elegido ? 3 : 1
                            border.color: elegido ? Theme.text : Qt.rgba(1, 1, 1, 0.2)
                            MouseArea {
                                anchors.fill: parent; cursorShape: Qt.PointingHandCursor
                                onClicked: {
                                    const e = Studio.temaEvento.cambiar({ colores: { acento: parent.modelData } })
                                    if (e !== "") Studio.notify(e, "error")
                                }
                            }
                        }
                    }
                }
                Text { text: qsTr("Franja en vivo"); color: Theme.textDim; font.pixelSize: 12 }
                RowLayout {
                    Toggle { checked: pw.sitio.barra !== false; onToggled: (v) => pw.guardar({ barra: v }) }
                    Text { text: qsTr("Arriba de cada página: qué pasa ahora, qué sigue y cuánto lleva"); color: Theme.textFaint; font.pixelSize: 11 }
                }
            }
            Text {
                Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.textFaint; font.pixelSize: 11
                text: qsTr("Los colores se aplican al regenerar la plantilla (las páginas que ya editaste no se tocan).")
            }
        }

        // --- Las páginas ---------------------------------------------------------------------
        Seccion {
            visible: !!pw.w.hay
            titulo: qsTr("Las páginas")
            subtitulo: qsTr("Una por sección del guion. Al agregar secciones se crean solas; lo editado no se pisa.")
            extra: [
                GlassButton {
                    compact: true; text: qsTr("✦ Que las arme el asistente"); active: true
                    onClicked: pw.pedirAsistente(qsTr("Personalizá la página web de este evento con `evento_web`: leé el guion, "
                        + "y escribí la portada, cada sección y el cierre con contenido de verdad (de qué se trata cada parte, "
                        + "quién habla, horarios), un diseño cuidado y los datos en vivo de vivo.js. Después pasame la dirección con qr_mostrar."))
                },
                GlassButton { compact: true; text: qsTr("Abrir la carpeta"); onClicked: Qt.openUrlExternally("file://" + pw.w.carpeta) }
            ]
            Repeater {
                model: pw.w.paginas || []
                delegate: RowLayout {
                    required property var modelData
                    required property int index
                    Layout.fillWidth: true
                    spacing: 8
                    Text { text: (index + 1) + "."; color: Theme.textFaint; font.pixelSize: 12; Layout.preferredWidth: 22 }
                    Text { Layout.fillWidth: true; elide: Text.ElideRight; color: modelData.existe ? Theme.text : Theme.amber; font.pixelSize: 12
                           text: modelData.existe ? (modelData.nombre || modelData.pagina) : qsTr("%1  (falta)").arg(modelData.nombre || modelData.pagina) }
                    Text { text: modelData.pagina; color: Theme.textFaint; font { pixelSize: 11; family: Theme.mono } }
                    GlassButton { compact: true; text: qsTr("Ver"); enabled: pw.urlPublica !== "" && modelData.existe
                                  onClicked: Qt.openUrlExternally(pw.urlPublica + "/evento/" + modelData.pagina) }
                    GlassButton { compact: true; text: "✦"
                                  onClicked: pw.pedirAsistente(qsTr("Personalizá la página «%1» del sitio del evento con `evento_web` (pagina: %2).")
                                      .arg(modelData.nombre || modelData.pagina).arg(modelData.pagina))
                                  ToolTip.visible: hovered; ToolTip.delay: 400; ToolTip.text: qsTr("Pedile al asistente que la personalice") }
                }
            }
            RowLayout {
                spacing: 8
                GlassButton { compact: true; text: qsTr("Crear las que faltan")
                              onClicked: { const r = pw.ev.generarWeb(false); if (r.error) Studio.notify(r.error, "error")
                                           else Studio.notify(qsTr("%n creada", "", (r.escritas || []).length), "ok") } }
                GlassButton { compact: true; text: qsTr("Rehacer todo con la plantilla…")
                              onClicked: ApplicationWindow.window.confirm({
                                  title: qsTr("¿Rehacer todas las páginas con la plantilla?"),
                                  text: qsTr("Se pisan las páginas editadas (las tuyas o las del asistente). Las imágenes y otros archivos de la carpeta quedan."),
                                  accept: qsTr("Rehacer"), reject: qsTr("Cancelar"),
                                  action: function() { const r = pw.ev.generarWeb(true); if (r.error) Studio.notify(r.error, "error") } }) }
            }
        }
        Item { Layout.preferredHeight: 4 }
    }
}
