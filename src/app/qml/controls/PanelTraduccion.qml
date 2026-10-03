// Traducción en vivo (Studio.traductor, agent/Traductor): de una salida de texto
// a otra en otro idioma, y el glosario de términos del evento. Lo usan
// Configuración → Traducción y el botón «Traducción…» de la página Audio.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import BA

ColumnLayout {
    id: panel
    readonly property var tr: Studio.traductor
    readonly property var ml: Studio.modelos
    property string resultado: ""
    property bool resultadoOk: true
    property string buscar: ""
    spacing: 14

    function nombreIdioma(c) {
        const l = tr ? tr.idiomas : []
        for (const x of l) if (x.codigo === c) return x.nombre
        return c
    }
    function avisar(e, ok) { if (e !== "") Studio.notify(e, "error"); else if (ok) Studio.notify(ok, "info") }
    readonly property var destinos: (tr ? tr.idiomas : []).filter(x => x.codigo !== "es")

    Connections {
        target: panel.tr
        function onProbado(a, texto, ok) { panel.resultado = texto; panel.resultadoOk = ok }
    }

    Text {
        Layout.fillWidth: true
        wrapMode: Text.WordWrap
        color: Theme.textDim; font.pixelSize: 12
        text: "Toma lo que transcribe una tarjeta «Voz a texto» (una salida de texto, p. ej. «Orador») y lo publica traducido " +
              "en otra salida, que se usa igual: subtítulos en un rótulo, archivo, o una tarjeta «Texto a voz» con una voz del " +
              "idioma. Sin red, en esta máquina. Cada traducción es una tarjeta «Traducir» en el rack de audio de la fuente " +
              "(al lado de su voz a texto): se agrega acá o desde «+ Agregar» → Voz → Traducir."
    }
    RowLayout {
        spacing: 8
        Rectangle {
            width: 10; height: 10; radius: 5
            color: !panel.tr ? Theme.textFaint : panel.tr.motor === "listo" ? Theme.ok
                 : panel.tr.motor.indexOf("error") === 0 ? Theme.live : panel.tr.motor === "arrancando" ? Theme.amber : Theme.textFaint
        }
        Text {
            text: !panel.tr ? "" : panel.tr.motor === "listo" ? "Motor listo" : panel.tr.motor === "apagado" ? "Motor apagado (arranca solo con la primera frase)" : panel.tr.motor
            color: Theme.textDim; font.pixelSize: 12
        }
    }
    Text {
        visible: panel.ml && (panel.ml.descargando || panel.ml.textoDescarga !== "")
        Layout.fillWidth: true; wrapMode: Text.WordWrap
        text: panel.ml ? panel.ml.textoDescarga : ""
        color: Theme.amber; font.pixelSize: 11
    }

    AjustesCorte { Layout.fillWidth: true }
    Rectangle { Layout.fillWidth: true; height: 1; color: Theme.glassBorder }

    // --- Probar ---------------------------------------------------------------------
    Text { text: "Probar"; color: Theme.text; font { pixelSize: 14; weight: Font.DemiBold } }
    RowLayout {
        Layout.fillWidth: true
        spacing: 8
        TextField {
            id: prueba
            Layout.fillWidth: true
            placeholderText: "Señor presidente, solicito pasar a cuarto intermedio."
            color: Theme.text; font.pixelSize: 12
            background: Rectangle { color: Theme.field; radius: 6 }
            onAccepted: probar.clicked()
        }
        ComboBox {
            id: idiomaPrueba
            Layout.preferredWidth: 170
            model: panel.destinos.map(x => x.nombre)
        }
        GlassButton {
            id: probar
            compact: true; text: "Traducir"; active: true
            onClicked: {
                panel.resultado = "Traduciendo…"; panel.resultadoOk = true
                panel.tr.probarUi(prueba.text !== "" ? prueba.text : prueba.placeholderText, panel.destinos[idiomaPrueba.currentIndex].codigo)
            }
        }
        GlassButton {
            compact: true; text: "Bajar idioma"; icon: "⇩"
            enabled: panel.ml && !panel.ml.descargando
            onClicked: panel.avisar(panel.tr.bajarUi("es", panel.destinos[idiomaPrueba.currentIndex].codigo), "")
            ToolTip.visible: hovered; ToolTip.delay: 400
            ToolTip.text: "Baja los paquetes para traducir del castellano a este idioma (70 a 300 MB; los que no son inglés ni portugués pasan por el inglés)"
        }
    }
    Text {
        visible: panel.resultado !== ""
        Layout.fillWidth: true; wrapMode: Text.WordWrap
        text: panel.resultado
        color: panel.resultadoOk ? Theme.text : Theme.live; font.pixelSize: 13
    }

    // --- Traducciones ---------------------------------------------------------------
    Text { text: "Traducciones en vivo"; color: Theme.text; font { pixelSize: 14; weight: Font.DemiBold } }
    Text {
        visible: panel.tr && panel.tr.traducciones.length === 0
        text: "Todavía no hay ninguna."
        color: Theme.textFaint; font.pixelSize: 12
    }
    Repeater {
        model: panel.tr ? panel.tr.traducciones : []
        delegate: Rectangle {
            id: fila
            required property var modelData
            Layout.fillWidth: true
            implicitHeight: filaCol.implicitHeight + 16
            radius: Theme.radiusSmall
            color: Theme.field
            ColumnLayout {
                id: filaCol
                anchors { fill: parent; margins: 8 }
                spacing: 4
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8
                    Toggle {
                        checked: fila.modelData.activa !== false
                        onToggled: (q) => panel.avisar(panel.tr.ajustarUi(fila.modelData.id, { activa: q }), "")
                    }
                    Text {
                        Layout.fillWidth: true; elide: Text.ElideRight
                        text: "«" + (fila.modelData.desde || "—") + "» → " + panel.nombreIdioma(fila.modelData.a) + " → «" + fila.modelData.nombre + "»" +
                              (fila.modelData.fuente ? "   · tarjeta en «" + fila.modelData.fuente + "»" : "")
                        color: Theme.text; font.pixelSize: 13
                    }
                    Text {
                        text: fila.modelData.estado
                        color: fila.modelData.estado === "lista" ? Theme.ok : Theme.amber; font.pixelSize: 11
                    }
                    GlassButton {
                        compact: true
                        visible: (fila.modelData.faltan || []).length > 0
                        enabled: panel.ml && !panel.ml.descargando
                        text: "Bajar"; icon: "⇩"
                        onClicked: panel.avisar(panel.tr.bajarUi(fila.modelData.idioma_desde, fila.modelData.a), "")
                    }
                    GlassButton { compact: true; text: "Quitar"; onClicked: panel.avisar(panel.tr.quitarUi(fila.modelData.id), "") }
                }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8
                    Text { text: "Subtítulos en el rótulo"; color: Theme.textDim; font.pixelSize: 11 }
                    TextField {
                        Layout.preferredWidth: 200
                        text: fila.modelData.rotulo || ""
                        placeholderText: "nombre de la fuente de texto"
                        color: Theme.text; font.pixelSize: 11
                        background: Rectangle { color: Qt.rgba(1, 1, 1, 0.05); radius: 6 }
                        onEditingFinished: if (text !== (fila.modelData.rotulo || "")) panel.avisar(panel.tr.ajustarUi(fila.modelData.id, { rotulo: text }), "")
                    }
                    Text { text: "Archivo (.txt y .srt)"; color: Theme.textDim; font.pixelSize: 11 }
                    Toggle {
                        checked: fila.modelData.archivo === true
                        onToggled: (q) => panel.avisar(panel.tr.ajustarUi(fila.modelData.id, { archivo: q }), "")
                    }
                    Item { Layout.fillWidth: true }
                }
                Text {
                    visible: !!fila.modelData.ultima
                    Layout.fillWidth: true; wrapMode: Text.WordWrap
                    text: !fila.modelData.ultima ? "" : fila.modelData.ultima.error ? "✕ " + fila.modelData.ultima.error
                          : fila.modelData.ultima.hora + " · " + fila.modelData.ultima.ms + " ms · " + fila.modelData.ultima.texto
                    color: fila.modelData.ultima && fila.modelData.ultima.error ? Theme.live : Theme.textFaint; font.pixelSize: 11
                }
            }
        }
    }
    RowLayout {
        Layout.fillWidth: true
        spacing: 8
        Text { text: "Traducir"; color: Theme.textDim; font.pixelSize: 12 }
        ComboBox {
            id: desde
            Layout.preferredWidth: 180
            // Las salidas de texto que hay (cambia con las tarjetas y las traducciones).
            model: (Studio.flujos ? Studio.flujos.flujos : []).map(f => f.nombre)
            displayText: count === 0 ? "no hay salidas de texto" : currentText
        }
        Text { text: "al"; color: Theme.textDim; font.pixelSize: 12 }
        ComboBox {
            id: hacia
            Layout.preferredWidth: 170
            model: panel.destinos.map(x => x.nombre)
        }
        TextField {
            id: nombreNueva
            Layout.fillWidth: true
            placeholderText: desde.currentText !== "" ? "se publica en «" + desde.currentText + " · " + (panel.destinos[hacia.currentIndex] || {}).codigo + "»" : "nombre de la salida traducida"
            color: Theme.text; font.pixelSize: 12
            background: Rectangle { color: Theme.field; radius: 6 }
        }
        GlassButton {
            compact: true; text: "Agregar"; active: true
            enabled: desde.currentText !== ""
            onClicked: {
                const e = panel.tr.agregarUi(desde.currentText, panel.destinos[hacia.currentIndex].codigo, nombreNueva.text)
                panel.avisar(e, "Traducción agregada")
                if (e === "") nombreNueva.text = ""
            }
        }
    }
    Text {
        visible: desde.count === 0
        Layout.fillWidth: true; wrapMode: Text.WordWrap
        text: "Para tener una salida de texto: en Audio, en el rack de una fuente, «+ Agregar» → «Voz a texto: transcripción continua»."
        color: Theme.textFaint; font.pixelSize: 11
    }

    // --- Glosario --------------------------------------------------------------------
    Rectangle { Layout.fillWidth: true; height: 1; color: Theme.glassBorder }
    Text { text: "Glosario"; color: Theme.text; font { pixelSize: 14; weight: Font.DemiBold } }
    Text {
        Layout.fillWidth: true; wrapMode: Text.WordWrap
        color: Theme.textDim; font.pixelSize: 11
        text: "Cómo se traducen los términos del evento (sin esto, «cuarto intermedio» sale «middle room»). Antes de cada evento " +
              "sumá sus nombres propios, comisiones, cargos y programas. El asistente también lo puede editar (glosario_agregar) " +
              "o tocar el archivo: " + (panel.tr ? panel.tr.archivoGlosario : "")
    }
    Flow {
        Layout.fillWidth: true
        spacing: 6
        Repeater {
            model: panel.tr ? panel.tr.grupos : []
            delegate: GlassButton {
                required property var modelData
                compact: true
                text: modelData.nombre + " (" + modelData.cuantos + ")"
                active: modelData.activo
                onClicked: panel.avisar(panel.tr.glosarioGrupoUi(modelData.grupo, !modelData.activo), "")
                ToolTip.visible: hovered; ToolTip.delay: 400; ToolTip.text: modelData.activo ? "Activo: tocá para apagar el grupo" : "Apagado: tocá para usarlo"
            }
        }
    }
    // Sumar un término.
    GridLayout {
        Layout.fillWidth: true
        columns: 4
        columnSpacing: 8; rowSpacing: 6
        Text { text: "Castellano"; color: Theme.textDim; font.pixelSize: 11 }
        TextField {
            id: tEs
            Layout.columnSpan: 3; Layout.fillWidth: true
            placeholderText: "formas separadas por coma: comisión de turismo, comisiones de turismo"
            color: Theme.text; font.pixelSize: 12
            background: Rectangle { color: Theme.field; radius: 6 }
        }
        Repeater {
            id: campos
            model: panel.destinos
            delegate: RowLayout {
                required property var modelData
                property alias texto: campo.text
                Layout.columnSpan: 2; Layout.fillWidth: true
                spacing: 6
                Text { text: modelData.codigo; color: Theme.textDim; font { pixelSize: 11; family: Theme.mono } Layout.preferredWidth: 22 }
                TextField {
                    id: campo
                    Layout.fillWidth: true
                    placeholderText: modelData.nombre
                    color: Theme.text; font.pixelSize: 12
                    background: Rectangle { color: Theme.field; radius: 6 }
                }
            }
        }
    }
    RowLayout {
        Layout.fillWidth: true
        Item { Layout.fillWidth: true }
        GlassButton {
            compact: true; text: "Agregar al glosario"; active: true
            enabled: tEs.text.trim() !== ""
            onClicked: {
                const e = { es: tEs.text.split(",").map(x => x.trim()).filter(x => x !== "") }
                for (let i = 0; i < campos.count; ++i) {
                    const it = campos.itemAt(i)
                    if (it.texto.trim() !== "") e[panel.destinos[i].codigo] = it.texto.trim()
                }
                const err = panel.tr.glosarioAgregarUi(e)
                panel.avisar(err, "Término agregado")
                if (err === "") { tEs.text = ""; for (let i = 0; i < campos.count; ++i) campos.itemAt(i).texto = "" }
            }
        }
    }
    TextField {
        Layout.fillWidth: true
        placeholderText: "Buscar en el glosario…"
        color: Theme.text; font.pixelSize: 12
        background: Rectangle { color: Theme.field; radius: 6 }
        onTextChanged: panel.buscar = text.toLowerCase()
    }
    Repeater {
        model: (panel.tr ? panel.tr.glosario : []).filter(e => panel.buscar === "" || JSON.stringify(e).toLowerCase().indexOf(panel.buscar) >= 0)
        delegate: RowLayout {
            id: term
            required property var modelData
            Layout.fillWidth: true
            spacing: 8
            opacity: modelData.activo ? 1 : 0.45
            Text {
                Layout.preferredWidth: 220; elide: Text.ElideRight
                text: (Array.isArray(term.modelData.es) ? term.modelData.es[0] : term.modelData.es)
                color: Theme.text; font.pixelSize: 12
                ToolTip.visible: tHover.hovered; ToolTip.text: Array.isArray(term.modelData.es) ? term.modelData.es.join(" · ") : ""
                HoverHandler { id: tHover }
            }
            Text {
                Layout.fillWidth: true; elide: Text.ElideRight
                text: ["en", "pt", "fr", "de", "ru", "zh"].filter(k => !!term.modelData[k]).map(k => k + ": " + term.modelData[k]).join("  ·  ")
                color: Theme.textDim; font.pixelSize: 11
            }
            Text {
                text: term.modelData.origen === "operador" ? "propio" : ""
                color: Theme.accent; font.pixelSize: 10
            }
            GlassButton {
                compact: true; text: "✕"
                onClicked: panel.avisar(panel.tr.glosarioQuitarUi(Array.isArray(term.modelData.es) ? term.modelData.es[0] : term.modelData.es), "")
                ToolTip.visible: hovered; ToolTip.delay: 400
                ToolTip.text: term.modelData.origen === "operador" ? "Sacar este término" : "Apagar este término de fábrica"
            }
        }
    }
}
