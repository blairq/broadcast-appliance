// Servicios → Aspecto de las páginas web → Tema del evento: la cara del evento abierto en
// TODAS las páginas web (la del evento, invitados, la sesión remota, el público, el tótem,
// las pantallas de la sala y, con el alcance que se elija, las consolas). Ver
// docs/planes/PLAN-TEMA-EVENTO.md.
//
// Todo sale de Studio.temaEvento (TemaEventoViewModel, el mismo que usa evento_tema del
// MCP): la vista lee sus propiedades (avisan con `cambio`) y le pide los cambios; nada se
// guarda acá. La muestra de arriba usa los colores YA resueltos (los que van a salir,
// con el contraste calculado), no los que se escribieron.
import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import BA

ColumnLayout {
    id: raiz
    spacing: 12

    readonly property var te: Studio.temaEvento
    readonly property var tema: te ? te.tema : ({})
    readonly property var res: te ? te.resuelto : ({})
    readonly property var col: res.colores || ({})
    readonly property var colTema: tema.colores || ({})
    readonly property var marca: tema.marca || ({})
    readonly property var tipo: tema.tipografia || ({})
    readonly property var alcance: res.alcance || ({})
    /// Qué archivo se está eligiendo: logo | logoClaro | favicon | fuente.
    property string eligiendo: "logo"

    function hecho(error) { if (error !== "") Studio.notify(error, "error") }
    function cambiar(campos) { hecho(raiz.te.cambiar(campos)) }
    function colores(clave, valor) { const c = {}; c[clave] = valor; raiz.cambiar({ colores: c }) }

    FileDialog {
        id: elegir
        title: raiz.eligiendo === "fuente" ? qsTr("La fuente del evento") : qsTr("El logo del evento")
        nameFilters: raiz.eligiendo === "fuente" ? [qsTr("Fuentes (*.woff2 *.woff *.ttf *.otf)")]
                                                 : [qsTr("Imágenes (*.svg *.png *.webp)")]
        onAccepted: {
            if (raiz.eligiendo === "fuente") {
                // La familia: la que esté escrita o, si no, el nombre del archivo.
                const nombre = familiaNueva.text.trim() !== "" ? familiaNueva.text.trim()
                             : decodeURIComponent(selectedFile.toString().split("/").pop().replace(/\.[^.]+$/, ""))
                raiz.hecho(raiz.te.subirFuente(selectedFile.toString(), nombre, "400", "normal"))
                familiaNueva.text = ""
            } else {
                raiz.hecho(raiz.te.subirLogo(selectedFile.toString(), raiz.eligiendo))
            }
        }
    }

    Text {
        Layout.fillWidth: true
        text: qsTr("La cara del evento abierto en todas las páginas web: la del evento, la de invitados, la sesión remota, las consultas del público, el tótem y las pantallas de la sala. Las consolas de los operadores toman lo que diga el alcance.")
        color: Theme.textDim
        font.pixelSize: 13
        wrapMode: Text.WordWrap
    }

    Text {
        visible: !raiz.te || !raiz.te.hayEvento
        Layout.fillWidth: true
        text: qsTr("No hay ningún evento abierto: el tema es de cada evento (abrí uno en Guion).")
        color: Theme.warn
        font.pixelSize: 13
        wrapMode: Text.WordWrap
    }

    ColumnLayout {
        visible: !!raiz.te && raiz.te.hayEvento
        Layout.fillWidth: true
        spacing: 12

        Text {
            visible: raiz.te && raiz.te.delSitio
            Layout.fillWidth: true
            text: qsTr("Por ahora el acento y el fondo salen de los colores del sitio del evento. Al cambiar algo acá, el evento pasa a tener su tema propio.")
            color: Theme.textDim
            font.pixelSize: 12
            wrapMode: Text.WordWrap
        }

        // --- La muestra, con los colores que van a salir -----------------------------------
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 150
            radius: 12
            color: raiz.col.fondo || "#0b0b0c"
            border.color: Theme.glassBorder
            Rectangle {
                anchors { fill: parent; margins: 18 }
                radius: raiz.tema.forma && raiz.tema.forma.radio >= 0 ? raiz.tema.forma.radio : 12
                color: raiz.col.superficie || "#19191a"
                RowLayout {
                    anchors { fill: parent; margins: 16 }
                    spacing: 16
                    Image {
                        Layout.preferredHeight: 48
                        Layout.preferredWidth: 96
                        fillMode: Image.PreserveAspectFit
                        source: raiz.te ? (raiz.res.modo !== "claro" && raiz.te.logoClaroUrl !== "" ? raiz.te.logoClaroUrl : raiz.te.logoUrl) : ""
                        visible: source.toString() !== ""
                        sourceSize.height: 96
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 4
                        Text {
                            Layout.fillWidth: true
                            text: raiz.marca.nombre || (Studio.evento.guion ? Studio.evento.guion.nombre : "") || qsTr("El evento")
                            color: raiz.col.texto || "#f4f4f5"
                            font { pixelSize: 18; weight: Font.DemiBold; family: raiz.tipo.titulos || raiz.tipo.texto || Qt.application.font.family }
                            elide: Text.ElideRight
                        }
                        Text {
                            Layout.fillWidth: true
                            text: qsTr("Así se ven las páginas del público")
                            color: raiz.col.textoSuave || "#9b9b9c"
                            font { pixelSize: 13; family: raiz.tipo.texto || Qt.application.font.family }
                            elide: Text.ElideRight
                        }
                    }
                    Rectangle {
                        implicitWidth: botonMuestra.implicitWidth + 28
                        implicitHeight: 40
                        radius: raiz.tema.forma && raiz.tema.forma.radio >= 0 ? raiz.tema.forma.radio : 10
                        color: raiz.col.acento || "#ff4d2e"
                        Text {
                            id: botonMuestra
                            anchors.centerIn: parent
                            text: qsTr("Entrar")
                            color: raiz.col.acentoTexto || "#0b0b0c"
                            font { pixelSize: 14; weight: Font.DemiBold }
                        }
                    }
                }
            }
        }

        // Los avisos de contraste (los calcula el modelo, igual que el gateway).
        Repeater {
            model: raiz.te ? raiz.te.avisos : []
            delegate: Text {
                required property string modelData
                Layout.fillWidth: true
                text: "⚠ " + modelData
                color: Theme.warn
                font.pixelSize: 12
                wrapMode: Text.WordWrap
            }
        }

        // --- Colores ---------------------------------------------------------------------
        Seccion {
            titulo: qsTr("Colores")
            subtitulo: qsTr("Vacío = se calcula (el texto encima del acento sale del contraste). En #rrggbb.")
            RowLayout {
                spacing: 6
                Text { text: qsTr("Acento"); color: Theme.textDim; font.pixelSize: 12; Layout.preferredWidth: 110 }
                Repeater {
                    model: ["#ff4d2e", "#e53935", "#43a047", "#1e88e5", "#8e24aa", "#fbc02d", "#00acc1", "#ffffff"]
                    delegate: Rectangle {
                        required property string modelData
                        implicitWidth: 24; implicitHeight: 24; radius: 12; color: modelData
                        border.width: (raiz.col.acento || "") === modelData ? 3 : 1
                        border.color: (raiz.col.acento || "") === modelData ? Theme.text : Qt.rgba(1, 1, 1, 0.2)
                        MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: raiz.colores("acento", parent.modelData) }
                    }
                }
            }
            GridLayout {
                Layout.fillWidth: true
                columns: 3
                columnSpacing: 10; rowSpacing: 6
                Repeater {
                    model: [
                        { clave: "acento", nombre: qsTr("Acento") },
                        { clave: "acentoTexto", nombre: qsTr("Texto sobre el acento") },
                        { clave: "fondo", nombre: qsTr("Fondo") },
                        { clave: "superficie", nombre: qsTr("Superficie") },
                        { clave: "texto", nombre: qsTr("Texto") },
                        { clave: "textoSuave", nombre: qsTr("Texto suave") }
                    ]
                    delegate: RowLayout {
                        required property var modelData
                        Layout.fillWidth: true
                        spacing: 6
                        Rectangle {
                            implicitWidth: 20; implicitHeight: 20; radius: 4
                            color: raiz.col[parent.modelData.clave] || "transparent"
                            border.color: Qt.rgba(1, 1, 1, 0.25)
                        }
                        Text { text: parent.modelData.nombre; color: Theme.textDim; font.pixelSize: 12; Layout.preferredWidth: 120 }
                        TextField {
                            Layout.preferredWidth: 110
                            text: raiz.colTema[parent.modelData.clave] || ""
                            placeholderText: raiz.col[parent.modelData.clave] || ""
                            onEditingFinished: if (text !== (raiz.colTema[parent.modelData.clave] || "")) raiz.colores(parent.modelData.clave, text)
                        }
                    }
                }
            }
            RowLayout {
                spacing: 6
                Text { text: qsTr("Modo"); color: Theme.textDim; font.pixelSize: 12; Layout.preferredWidth: 110 }
                GlassButton { compact: true; text: qsTr("Según el fondo"); active: !raiz.tema.modo; onClicked: raiz.cambiar({ modo: "" }) }
                GlassButton { compact: true; text: qsTr("Oscuro"); active: raiz.tema.modo === "oscuro"; onClicked: raiz.cambiar({ modo: "oscuro" }) }
                GlassButton { compact: true; text: qsTr("Claro"); active: raiz.tema.modo === "claro"; onClicked: raiz.cambiar({ modo: "claro" }) }
            }
        }

        // --- Marca -----------------------------------------------------------------------
        Seccion {
            titulo: qsTr("Nombre y logo")
            subtitulo: qsTr("SVG, PNG o WebP de hasta 2 MB. El logo claro es para los fondos oscuros; el favicon, la pestaña del navegador (si no hay, va el logo).")
            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                Text { text: qsTr("Nombre"); color: Theme.textDim; font.pixelSize: 12; Layout.preferredWidth: 110 }
                TextField {
                    Layout.fillWidth: true
                    text: raiz.marca.nombre || ""
                    placeholderText: qsTr("el del evento")
                    onEditingFinished: if (text !== (raiz.marca.nombre || "")) raiz.cambiar({ marca: { nombre: text } })
                }
            }
            Repeater {
                model: [
                    { cual: "logo", nombre: qsTr("Logo") },
                    { cual: "logoClaro", nombre: qsTr("Logo claro") },
                    { cual: "favicon", nombre: qsTr("Favicon") }
                ]
                delegate: RowLayout {
                    required property var modelData
                    Layout.fillWidth: true
                    spacing: 8
                    Text { text: parent.modelData.nombre; color: Theme.textDim; font.pixelSize: 12; Layout.preferredWidth: 110 }
                    Text {
                        Layout.fillWidth: true
                        text: raiz.marca[parent.modelData.cual] || qsTr("(ninguno)")
                        color: raiz.marca[parent.modelData.cual] ? Theme.text : Theme.textFaint
                        font.pixelSize: 12
                        elide: Text.ElideMiddle
                    }
                    GlassButton {
                        compact: true; text: qsTr("Elegir…")
                        onClicked: { raiz.eligiendo = parent.modelData.cual; elegir.open() }
                    }
                    GlassButton {
                        compact: true; text: qsTr("Quitar"); visible: !!raiz.marca[parent.modelData.cual]
                        onClicked: raiz.hecho(raiz.te.quitar(parent.modelData.cual))
                    }
                }
            }
        }

        // --- Tipografía ----------------------------------------------------------------
        Seccion {
            titulo: qsTr("Letra")
            subtitulo: qsTr("Fuentes propias (WOFF2, WOFF, TTF u OTF de hasta 4 MB): viajan con el evento, sin internet. Lo que la fuente no tiene (chino, árabe…) sale con la del sistema.")
            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                Text { text: qsTr("Texto"); color: Theme.textDim; font.pixelSize: 12; Layout.preferredWidth: 110 }
                TextField {
                    Layout.fillWidth: true
                    text: raiz.tipo.texto || ""
                    placeholderText: qsTr("la de cada página")
                    onEditingFinished: if (text !== (raiz.tipo.texto || "")) raiz.cambiar({ tipografia: { texto: text } })
                }
                Text { text: qsTr("Títulos"); color: Theme.textDim; font.pixelSize: 12 }
                TextField {
                    Layout.fillWidth: true
                    text: raiz.tipo.titulos || ""
                    placeholderText: qsTr("la del texto")
                    onEditingFinished: if (text !== (raiz.tipo.titulos || "")) raiz.cambiar({ tipografia: { titulos: text } })
                }
            }
            Text {
                Layout.fillWidth: true
                visible: (raiz.tipo.archivos || []).length > 0
                text: qsTr("Subidas: %1").arg((raiz.tipo.archivos || []).map(a => a.familia + " (" + a.peso + ")").join(" · "))
                color: Theme.text
                font.pixelSize: 12
                wrapMode: Text.WordWrap
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                TextField {
                    id: familiaNueva
                    Layout.fillWidth: true
                    placeholderText: qsTr("cómo se llama la familia (si no, el nombre del archivo)")
                }
                GlassButton { compact: true; text: qsTr("Agregar fuente…"); onClicked: { raiz.eligiendo = "fuente"; elegir.open() } }
                GlassButton {
                    compact: true; text: qsTr("Quitar las fuentes"); visible: (raiz.tipo.archivos || []).length > 0
                    onClicked: raiz.hecho(raiz.te.quitar("fuentes"))
                }
            }
            RowLayout {
                spacing: 8
                Text { text: qsTr("Esquinas"); color: Theme.textDim; font.pixelSize: 12; Layout.preferredWidth: 110 }
                SpinBox {
                    from: -1; to: 32
                    value: raiz.tema.forma && raiz.tema.forma.radio !== undefined ? raiz.tema.forma.radio : -1
                    textFromValue: (v) => v < 0 ? qsTr("las de cada página") : v + " px"
                    onValueModified: raiz.cambiar({ radio: value })
                }
            }
        }

        // --- Alcance ----------------------------------------------------------------------
        Seccion {
            titulo: qsTr("Dónde se aplica")
            subtitulo: qsTr("Nunca cambian los colores con significado: tally, al aire y armado, vúmetros, sí y no de la votación.")
            Repeater {
                model: [
                    { clave: "publico", nombre: qsTr("Público"), ayuda: qsTr("evento, invitados, sesión remota, consultas, tótem, pantallas") },
                    { clave: "control", nombre: qsTr("Consolas"), ayuda: qsTr("control, botonera, presidencia, rótulos, corrección, atención del tótem") }
                ]
                delegate: RowLayout {
                    required property var modelData
                    Layout.fillWidth: true
                    spacing: 6
                    Text { text: parent.modelData.nombre; color: Theme.text; font.pixelSize: 12; Layout.preferredWidth: 110 }
                    GlassButton {
                        compact: true; text: qsTr("Nada"); active: raiz.alcance[parent.modelData.clave] === "nada"
                        onClicked: { const a = {}; a[parent.modelData.clave] = "nada"; raiz.cambiar({ alcance: a }) }
                    }
                    GlassButton {
                        compact: true; text: qsTr("Acento y logo"); active: raiz.alcance[parent.modelData.clave] === "acento"
                        onClicked: { const a = {}; a[parent.modelData.clave] = "acento"; raiz.cambiar({ alcance: a }) }
                    }
                    GlassButton {
                        compact: true; text: qsTr("Todo"); active: raiz.alcance[parent.modelData.clave] === "todo"
                        onClicked: { const a = {}; a[parent.modelData.clave] = "todo"; raiz.cambiar({ alcance: a }) }
                    }
                    Text {
                        Layout.fillWidth: true
                        text: parent.modelData.ayuda
                        color: Theme.textFaint
                        font.pixelSize: 11
                        elide: Text.ElideRight
                    }
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            GlassButton {
                compact: true
                text: qsTr("Pasar el sitio al tema")
                ToolTip.visible: hovered; ToolTip.delay: 400
                ToolTip.text: qsTr("Para un sitio generado antes del tema: sus páginas toman el tema común (lo que cambiaste a mano en estilo.css se respeta)")
                onClicked: {
                    const r = raiz.te.migrarSitio()
                    if (r.error) Studio.notify(r.error, "error")
                    else Studio.notify(r.cambiados.length > 0 ? qsTr("Sitio actualizado: %1").arg(r.cambiados.join(", "))
                                                              : qsTr("El sitio ya usaba el tema del evento."), "info")
                }
            }
            Item { Layout.fillWidth: true }
            GlassButton {
                compact: true
                text: qsTr("Restablecer")
                visible: raiz.te && raiz.te.existe && !raiz.te.delSitio
                onClicked: raiz.hecho(raiz.te.restablecer())
            }
        }
    }
}
