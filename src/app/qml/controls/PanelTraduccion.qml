// Traducción en vivo (Studio.traductor, agent/Traductor): de una salida de texto
// a otra en otro idioma (cada una es una tarjeta «Traducir»), y el glosario de términos del evento. Lo usan
// Configuración → Traducción y el botón «Traducción…» de la página Audio.
import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
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
    // Se traduce desde el idioma del evento (el del guion; "" = el del operador): los destinos
    // son los que tienen paquete desde ese idioma. Se rehace cuando cambia el guion.
    readonly property string idiomaEvento: Studio.evento && Studio.evento.guion ? (Studio.evento.guion.idioma || "") : ""
    readonly property var delEvento: tr ? tr.idiomasDelEvento(idiomaEvento) : ({ destinos: [] })
    readonly property var destinos: delEvento.destinos || []
    // Las columnas del glosario: los idiomas además del de su clave (ver Traductor::idiomasGlosario).
    readonly property var idiomasGlosario: tr ? tr.idiomasGlosario : []

    Connections {
        target: panel.tr
        function onProbado(a, texto, ok) { panel.resultado = texto; panel.resultadoOk = ok }
    }

    Text {
        Layout.fillWidth: true
        wrapMode: Text.WordWrap
        color: Theme.textDim; font.pixelSize: 12
        text: qsTr("Toma lo que transcribe una tarjeta «Voz a texto» (una salida de texto, p. ej. «Orador») y lo publica traducido " +
              "en otra salida, que se usa igual: subtítulos en un rótulo, archivo, o una tarjeta «Texto a voz» con una voz del " +
              "idioma. Sin red, en esta máquina. Cada traducción es una tarjeta «Traducir» en el rack de audio de la fuente " +
              "(al lado de su voz a texto): se agrega acá o desde «+ Agregar» → Voz → Traducir.")
    }
    RowLayout {
        spacing: 8
        Rectangle {
            width: 10; height: 10; radius: 5
            color: !panel.tr ? Theme.textFaint : panel.tr.motor === "listo" ? Theme.ok
                 : panel.tr.motor.indexOf("error") === 0 ? Theme.live : panel.tr.motor === "arrancando" ? Theme.amber : Theme.textFaint
        }
        Text {
            text: !panel.tr ? "" : panel.tr.motor === "listo" ? qsTr("Motor listo") : panel.tr.motor === "apagado" ? qsTr("Motor apagado (arranca solo con la primera frase)") : panel.tr.motor
            color: Theme.textDim; font.pixelSize: 12
        }
    }
    Text {
        visible: panel.ml && (panel.ml.descargando || panel.ml.textoDescarga !== "")
        Layout.fillWidth: true; wrapMode: Text.WordWrap
        text: panel.ml ? panel.ml.textoDescarga : ""
        color: Theme.amber; font.pixelSize: 11
    }

    Text {
        Layout.fillWidth: true; wrapMode: Text.WordWrap
        color: Theme.textFaint; font.pixelSize: 11
        readonly property var c: Studio.voz ? Studio.voz.cortes : ({ pausa: 0.35, maximo: 6 })
        text: qsTr("Cuándo corta la transcripción (pausa %1 s, largo máximo %2 s) se ajusta en la tarjeta «Voz a texto» o en Configuración → Voz del sistema. " +
              "Lo de abajo es de cada traducción.")
              .arg(Number(c.pausa).toFixed(2).replace(".", ",")).arg(Number(c.maximo).toFixed(1).replace(".", ","))
    }
    Rectangle { Layout.fillWidth: true; height: 1; color: Theme.glassBorder }

    // --- Probar ---------------------------------------------------------------------
    Text { text: qsTr("Probar"); color: Theme.text; font { pixelSize: 14; weight: Font.DemiBold } }
    Text {
        Layout.fillWidth: true; wrapMode: Text.WordWrap
        text: panel.delEvento.error ? panel.delEvento.error
              : qsTr("Se traduce desde el idioma del evento: %1.").arg(panel.delEvento.nombre || "")
        color: panel.delEvento.error ? Theme.amber : Theme.textDim; font.pixelSize: 12
    }
    RowLayout {
        Layout.fillWidth: true
        spacing: 8
        TextField {
            id: prueba
            Layout.fillWidth: true
            placeholderText: panel.delEvento.ejemplo || ""
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
            compact: true; text: qsTr("Traducir"); active: true
            enabled: panel.destinos.length > 0
            onClicked: {
                panel.resultado = qsTr("Traduciendo…"); panel.resultadoOk = true
                panel.tr.probarUi(prueba.text !== "" ? prueba.text : prueba.placeholderText, panel.destinos[idiomaPrueba.currentIndex].codigo, panel.delEvento.origen)
            }
        }
        GlassButton {
            compact: true; text: qsTr("Bajar idioma"); icon: "⇩"
            enabled: !!panel.ml && panel.destinos.length > 0
            onClicked: panel.avisar(panel.tr.bajarUi(panel.delEvento.origen, panel.destinos[idiomaPrueba.currentIndex].codigo), "")
            ToolTip.visible: hovered; ToolTip.delay: 400
            ToolTip.text: qsTr("Baja los paquetes para traducir del idioma del evento a este (70 a 300 MB cada uno; si no hay uno directo, pasa por otro idioma, casi siempre el inglés)")
        }
    }
    Text {
        visible: panel.resultado !== ""
        Layout.fillWidth: true; wrapMode: Text.WordWrap
        text: panel.resultado
        color: panel.resultadoOk ? Theme.text : Theme.live; font.pixelSize: 13
    }

    // --- Traducciones ---------------------------------------------------------------
    Text { text: qsTr("Traducciones en vivo"); color: Theme.text; font { pixelSize: 14; weight: Font.DemiBold } }
    Text {
        visible: panel.tr && panel.tr.traducciones.length === 0
        text: qsTr("Todavía no hay ninguna.")
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
                              (fila.modelData.fuente ? qsTr("   · tarjeta en «%1»").arg(fila.modelData.fuente) : "")
                        color: Theme.text; font.pixelSize: 13
                    }
                    Text {
                        text: fila.modelData.estado
                        color: fila.modelData.estado === "lista" ? Theme.ok : Theme.amber; font.pixelSize: 11
                    }
                    GlassButton {
                        compact: true
                        visible: (fila.modelData.faltan || []).length > 0
                        enabled: !!panel.ml
                        text: qsTr("Bajar"); icon: "⇩"
                        onClicked: panel.avisar(panel.tr.bajarUi(fila.modelData.idioma_desde, fila.modelData.a), "")
                    }
                    GlassButton { compact: true; text: qsTr("Quitar"); onClicked: panel.avisar(panel.tr.quitarUi(fila.modelData.id), "") }
                }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8
                    Text { text: qsTr("Subtítulos en el rótulo"); color: Theme.textDim; font.pixelSize: 11 }
                    TextField {
                        Layout.preferredWidth: 200
                        text: fila.modelData.rotulo || ""
                        placeholderText: qsTr("nombre de la fuente de texto")
                        color: Theme.text; font.pixelSize: 11
                        background: Rectangle { color: Qt.rgba(1, 1, 1, 0.05); radius: 6 }
                        onEditingFinished: if (text !== (fila.modelData.rotulo || "")) panel.avisar(panel.tr.ajustarUi(fila.modelData.id, { rotulo: text }), "")
                    }
                    Text { text: qsTr("Archivo (.txt y .srt)"); color: Theme.textDim; font.pixelSize: 11 }
                    Toggle {
                        checked: fila.modelData.archivo === true
                        onToggled: (q) => panel.avisar(panel.tr.ajustarUi(fila.modelData.id, { archivo: q }), "")
                    }
                    Item { Layout.fillWidth: true }
                }
                TiemposTraduccion {
                    Layout.fillWidth: true
                    t: fila.modelData
                    onCambiar: (c) => panel.avisar(panel.tr.ajustarUi(fila.modelData.id, c), "")
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
        Text { text: qsTr("Traducir"); color: Theme.textDim; font.pixelSize: 12 }
        ComboBox {
            id: desde
            Layout.preferredWidth: 180
            // Las salidas de texto que hay (cambia con las tarjetas y las traducciones).
            model: (Studio.flujos ? Studio.flujos.flujos : []).map(f => f.nombre)
            displayText: count === 0 ? qsTr("no hay salidas de texto") : currentText
        }
        Text { text: qsTr("al"); color: Theme.textDim; font.pixelSize: 12 }
        ComboBox {
            id: hacia
            Layout.preferredWidth: 170
            model: panel.destinos.map(x => x.nombre)
        }
        TextField {
            id: nombreNueva
            Layout.fillWidth: true
            placeholderText: desde.currentText !== "" ? qsTr("se publica en «%1 · %2»").arg(desde.currentText).arg((panel.destinos[hacia.currentIndex] || {}).codigo) : qsTr("nombre de la salida traducida")
            color: Theme.text; font.pixelSize: 12
            background: Rectangle { color: Theme.field; radius: 6 }
        }
        GlassButton {
            compact: true; text: qsTr("Agregar"); active: true
            enabled: desde.currentText !== "" && panel.destinos.length > 0
            onClicked: {
                const e = panel.tr.agregarUi(desde.currentText, panel.destinos[hacia.currentIndex].codigo, nombreNueva.text, panel.delEvento.origen)
                panel.avisar(e, qsTr("Traducción agregada"))
                if (e === "") nombreNueva.text = ""
            }
        }
    }
    Text {
        visible: desde.count === 0
        Layout.fillWidth: true; wrapMode: Text.WordWrap
        text: qsTr("Para tener una salida de texto: en Audio, en el rack de una fuente, «+ Agregar» → «Voz a texto: transcripción continua».")
        color: Theme.textFaint; font.pixelSize: 11
    }

    // --- Corrección humana antes del aire -----------------------------------------------
    Rectangle { Layout.fillWidth: true; height: 1; color: Theme.glassBorder }
    ColumnLayout {
        id: corr
        Layout.fillWidth: true
        spacing: 6
        readonly property var fl: Studio.flujos
        property var pend: fl ? fl.pendientesUi() : []
        property bool activa: fl ? fl.correccionActivaUi() : false
        Connections {
            target: Studio.flujos
            function onPendientesCambio() { corr.pend = Studio.flujos.pendientesUi(); corr.activa = Studio.flujos.correccionActivaUi() }
        }
        Timer { interval: 1000; repeat: true; running: corr.activa && corr.pend.length > 0; onTriggered: corr.pend = Studio.flujos.pendientesUi() }
        RowLayout {
            Text {
                text: qsTr("Corrección antes del aire")
                color: Theme.text
                font.pixelSize: 14
                font.weight: Font.DemiBold
                Layout.fillWidth: true
            }
            Text { text: qsTr("Demora (s)"); color: Theme.textDim; font.pixelSize: 11 }
            SpinBox {
                id: demora; from: 1; to: 30; editable: true; Layout.preferredWidth: 96
                value: corr.fl ? Math.max(1, Math.round(corr.fl.demoraMsUi() / 1000)) : 4
                onValueModified: if (corr.activa) corr.fl.setCorreccionUi(true, value * 1000)
            }
            CheckBox {
                text: qsTr("Prendida")
                checked: corr.activa
                onToggled: corr.fl.setCorreccionUi(checked, demora.value * 1000)
            }
            // T44: el corrector puede estar en otra máquina (o en un celular): una página con su clave.
            GlassButton { compact: true; text: qsTr("Puesto de corrección…"); onClicked: puestoQr.open()
                          ToolTip.visible: hovered; ToolTip.delay: 400
                          ToolTip.text: qsTr("Un enlace (y QR) para corregir desde otra computadora o un celular: ve las frases con su cuenta regresiva, Enter aprueba, Esc descarta") }
        }
        QrDialog {
            id: puestoQr
            fuente: Studio.puestoCorreccion
            title: qsTr("Puesto de corrección de subtítulos")
            ofreceClave: false
            avisoConClave: qsTr("El enlace lleva la clave del puesto (cambia en cada arranque del servidor): quien lo tenga puede corregir lo que sale al aire.")
        }
        Text {
            Layout.fillWidth: true; wrapMode: Text.WordWrap
            color: Theme.textDim; font.pixelSize: 11
            text: qsTr("Cada frase de la transcripción espera antes de ir a los subtítulos, el CC, la traducción y los archivos. " +
                  "Editala y Enter para mandarla corregida, ✓ para aprobarla como está, ✕ para tirarla; si nadie la toca, sale sola.")
        }
        Repeater {
            model: corr.pend
            delegate: RowLayout {
                required property var modelData
                Layout.fillWidth: true
                spacing: 6
                Text { text: modelData.resta + " s"; color: modelData.resta <= 1 ? Theme.live : Theme.amber; font.pixelSize: 12; Layout.preferredWidth: 34 }
                TextField {
                    Layout.fillWidth: true
                    text: modelData.texto
                    color: Theme.text
                    onAccepted: corr.fl.corregirUi(modelData.id, text, false)
                }
                GlassButton { compact: true; text: "✓"; onClicked: corr.fl.corregirUi(modelData.id, "", false) }
                GlassButton { compact: true; text: "✕"; onClicked: corr.fl.corregirUi(modelData.id, "", true) }
            }
        }
        CheckBox {
            text: qsTr("Rotular «Traducción automática» mientras sale una traducción (capa DSK 4)")
            checked: Studio.rotuloTraduccionActivo()
            onToggled: Studio.setRotuloTraduccion(checked)
        }
    }

    // --- Glosario --------------------------------------------------------------------
    Rectangle { Layout.fillWidth: true; height: 1; color: Theme.glassBorder }
    RowLayout {
        Layout.fillWidth: true
        Text { text: qsTr("Glosario"); color: Theme.text; font { pixelSize: 14; weight: Font.DemiBold } Layout.fillWidth: true }
        GlassButton {
            compact: true; text: qsTr("Exportar para revisión")
            onClicked: {
                const r = panel.tr.glosarioExportarUi()
                if (r === "") Studio.notify(qsTr("No pude escribir el CSV"), "error")
                else { Studio.notify(qsTr("Glosario para revisar: %1").arg(r), "info"); Qt.openUrlExternally("file://" + r.substring(0, r.lastIndexOf("/"))) }
            }
        }
        TextField { id: revisor; Layout.preferredWidth: 150; placeholderText: qsTr("Revisado por"); color: Theme.text }
        GlassButton {
            compact: true; text: qsTr("Importar revisado…")
            enabled: revisor.text.trim() !== ""
            onClicked: dlgGlosario.open()
        }
    }
    FileDialog {
        id: dlgGlosario
        title: qsTr("Glosario revisado (CSV)")
        nameFilters: ["CSV (*.csv *.txt)"]
        onAccepted: {
            const e = panel.tr.glosarioImportarUi(selectedFile, revisor.text)
            Studio.notify(e === "" ? qsTr("Glosario revisado importado") : e, e === "" ? "ok" : "error")
        }
    }
    Text {
        Layout.fillWidth: true; wrapMode: Text.WordWrap
        color: Theme.textDim; font.pixelSize: 11
        text: qsTr("Cómo se traducen los términos del evento (sin esto, «cuarto intermedio» sale «middle room»). Antes de cada evento " +
              "sumá sus nombres propios, comisiones, cargos y programas. El asistente también lo puede editar (glosario_agregar) " +
              "o tocar el archivo: %1").arg(panel.tr ? panel.tr.archivoGlosario : "")
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
                ToolTip.visible: hovered; ToolTip.delay: 400; ToolTip.text: modelData.activo ? qsTr("Activo: tocá para apagar el grupo") : qsTr("Apagado: tocá para usarlo")
            }
        }
    }
    // Sumar un término.
    GridLayout {
        Layout.fillWidth: true
        columns: 4
        columnSpacing: 8; rowSpacing: 6
        Text { text: qsTr("Castellano"); color: Theme.textDim; font.pixelSize: 11 }
        TextField {
            id: tEs
            Layout.columnSpan: 3; Layout.fillWidth: true
            placeholderText: qsTr("formas separadas por coma: comisión de turismo, comisiones de turismo")
            color: Theme.text; font.pixelSize: 12
            background: Rectangle { color: Theme.field; radius: 6 }
        }
        Repeater {
            id: campos
            model: panel.idiomasGlosario
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
            compact: true; text: qsTr("Agregar al glosario"); active: true
            enabled: tEs.text.trim() !== ""
            onClicked: {
                const e = { es: tEs.text.split(",").map(x => x.trim()).filter(x => x !== "") }
                for (let i = 0; i < campos.count; ++i) {
                    const it = campos.itemAt(i)
                    if (it.texto.trim() !== "") e[panel.idiomasGlosario[i].codigo] = it.texto.trim()
                }
                const err = panel.tr.glosarioAgregarUi(e)
                panel.avisar(err, qsTr("Término agregado"))
                if (err === "") { tEs.text = ""; for (let i = 0; i < campos.count; ++i) campos.itemAt(i).texto = "" }
            }
        }
    }
    TextField {
        Layout.fillWidth: true
        placeholderText: qsTr("Buscar en el glosario…")
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
                text: term.modelData.origen === "operador" ? qsTr("propio") : ""
                color: Theme.accent; font.pixelSize: 10
            }
            GlassButton {
                compact: true; text: "✕"
                onClicked: panel.avisar(panel.tr.glosarioQuitarUi(Array.isArray(term.modelData.es) ? term.modelData.es[0] : term.modelData.es), "")
                ToolTip.visible: hovered; ToolTip.delay: 400
                ToolTip.text: term.modelData.origen === "operador" ? qsTr("Sacar este término") : qsTr("Apagar este término de fábrica")
            }
        }
    }

    // --- Subtítulos ocultos (CC) en la transmisión ---------------------------------
    Rectangle { Layout.fillWidth: true; height: 1; color: Theme.glassBorder }
    ColumnLayout {
        id: ccSec
        Layout.fillWidth: true
        spacing: 6
        readonly property var cc: Studio.cc
        property var e: cc ? cc.estado() : ({})
        Connections { target: Studio.cc; function onCambio() { ccSec.e = Studio.cc.estado() } }
        RowLayout {
            Text { text: qsTr("Subtítulos ocultos (CC)"); color: Theme.text; font { pixelSize: 14; weight: Font.DemiBold } Layout.fillWidth: true }
            StatusPill { text: ccSec.e.activo ? qsTr("CC prendido · %n línea", "", ccSec.e.enviadas || 0) : qsTr("CC apagado"); dot: ccSec.e.activo ? Theme.ok : Theme.textFaint }
        }
        Text {
            Layout.fillWidth: true; wrapMode: Text.WordWrap
            color: Theme.textDim; font.pixelSize: 11
            text: qsTr("El espectador los prende o apaga en su reproductor. 608: dentro del video de la transmisión (y de cada destino). " +
                  "YouTube: por la API de subtítulos del evento (la URL de «HTTP POST» de YouTube Studio).")
        }
        RowLayout {
            spacing: 6
            Text { text: qsTr("Salida de texto"); color: Theme.textDim; font.pixelSize: 12 }
            TextField { id: ccFlujo; Layout.preferredWidth: 160; text: ccSec.e.flujo || ""; placeholderText: qsTr("p. ej. Sala") }
            ComboBox {
                id: ccMetodo
                implicitWidth: 150
                model: [ { t: qsTr("608 en el video"), v: "608" }, { t: qsTr("API de YouTube"), v: "youtube_http" }, { t: qsTr("Los dos"), v: "ambos" } ]
                textRole: "t"
                currentIndex: Math.max(0, model.findIndex(o => o.v === (ccSec.e.metodo || "608")))
            }
        }
        TextField { id: ccUrl; Layout.fillWidth: true; visible: ccMetodo.currentIndex > 0
                    placeholderText: ccSec.e.url_youtube ? qsTr("(URL guardada)") : qsTr("URL de subtítulos de YouTube (http://upload.youtube.com/closedcaption?…)") }
        RowLayout {
            spacing: 6
            GlassButton {
                compact: true; active: !ccSec.e.activo; text: ccSec.e.activo ? qsTr("Aplicar") : qsTr("Prender CC")
                onClicked: {
                    const cfg = { flujo: ccFlujo.text, metodo: ccMetodo.model[ccMetodo.currentIndex].v }
                    if (ccUrl.text !== "") cfg.url = ccUrl.text
                    const r = ccSec.cc.activar(cfg); if (r !== "") Studio.notify(r, "error")
                }
            }
            GlassButton { compact: true; visible: !!ccSec.e.activo; text: qsTr("Apagar"); onClicked: ccSec.cc.desactivar() }
            GlassButton { compact: true; text: qsTr("Probar"); onClicked: { const r = ccSec.cc.probar(qsTr("Prueba de subtítulos")); if (r !== "") Studio.notify(r, "error") } }
        }
        Text {
            visible: (ccSec.e.ultimas || []).length > 0
            Layout.fillWidth: true; wrapMode: Text.WordWrap
            text: qsTr("Últimas: %1").arg((ccSec.e.ultimas || []).slice(-3).join(" · "))
            color: Theme.textFaint; font.pixelSize: 11
        }
    }

    // --- Vocabulario de la voz a texto ---------------------------------------------
    Rectangle { Layout.fillWidth: true; height: 1; color: Theme.glassBorder }
    ColumnLayout {
        id: vocSec
        Layout.fillWidth: true
        spacing: 6
        property var v: Studio.vocabularioUi()
        property string grupo: "operador"
        function refrescar() { v = Studio.vocabularioUi() }
        readonly property var grupos: Object.keys((v && v.grupos) || {})
        RowLayout {
            Text {
                text: qsTr("Vocabulario de la transcripción")
                color: Theme.text
                font.pixelSize: 14
                font.weight: Font.DemiBold
                Layout.fillWidth: true
            }
            GlassButton {
                compact: true; text: qsTr("Importar del Cuerpo")
                onClicked: { Studio.vocabularioDelCuerpoUi(); vocSec.refrescar() }
            }
        }
        Text {
            Layout.fillWidth: true; wrapMode: Text.WordWrap
            color: Theme.textDim; font.pixelSize: 11
            text: qsTr("Nombres, localidades y términos que la transcripción tiene que escribir bien. Se corrige por cómo suena " +
                  "(b/v, s/z/c, ll/y…) y solo si es casi igual; cada reemplazo queda registrado.")
        }
        Flow {
            Layout.fillWidth: true; spacing: 6
            Repeater {
                model: vocSec.grupos
                delegate: GlassButton {
                    required property string modelData
                    readonly property var g: vocSec.v.grupos[modelData]
                    compact: true
                    active: vocSec.grupo === modelData
                    text: (g.activo ? "● " : "○ ") + modelData + " (" + (g.terminos || []).length + ")"
                    onClicked: vocSec.grupo = modelData
                }
            }
            GlassButton {
                readonly property bool on: !!(((vocSec.v.grupos || {})[vocSec.grupo] || {}).activo)
                compact: true
                text: (on ? qsTr("Apagar «%1»") : qsTr("Prender «%1»")).arg(vocSec.grupo)
                onClicked: { Studio.vocabularioGrupoUi(vocSec.grupo, !on); vocSec.refrescar() }
            }
        }
        Flow {
            Layout.fillWidth: true; spacing: 4
            Repeater {
                model: ((vocSec.v.grupos || {})[vocSec.grupo] || {}).terminos || []
                delegate: GlassButton {
                    required property var modelData
                    compact: true
                    text: modelData.termino + ((modelData.variantes || []).length ? " ←" + modelData.variantes.join(", ") : "") + "  ✕"
                    onClicked: { Studio.vocabularioQuitarUi(modelData.termino); vocSec.refrescar() }
                    ToolTip.visible: hovered; ToolTip.delay: 400; ToolTip.text: qsTr("Sacar este término")
                }
            }
        }
        RowLayout {
            spacing: 6
            TextField { id: vocTerm; Layout.preferredWidth: 170; placeholderText: qsTr("Término bien escrito") }
            TextField { id: vocVar; Layout.fillWidth: true; placeholderText: qsTr("Cómo lo escribe mal (opcional, con comas)") }
            GlassButton {
                compact: true; text: qsTr("Agregar a %1").arg(vocSec.grupo)
                onClicked: {
                    const r = Studio.vocabularioAgregarUi(vocTerm.text, vocVar.text, vocSec.grupo)
                    if (r !== "") Studio.notify(r, "error"); else { vocTerm.text = ""; vocVar.text = ""; vocSec.refrescar() }
                }
            }
        }
        RowLayout {
            spacing: 6
            TextField { id: vocPrueba; Layout.fillWidth: true; placeholderText: qsTr("Probar: «llegamos a comodoro ribadabia»") }
            GlassButton {
                compact: true; text: qsTr("Probar")
                onClicked: {
                    const r = Studio.vocabularioProbarUi(vocPrueba.text)
                    vocRes.text = r.texto + ((r.reemplazos || []).length ? "   (" + r.reemplazos.map(x => x.original + " → " + x.reemplazo).join(", ") + ")" : "   " + qsTr("(sin cambios)"))
                }
            }
        }
        Text { id: vocRes; visible: text !== ""; Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.text; font.pixelSize: 12 }
        Text {
            visible: (vocSec.v.reemplazos || []).length > 0
            Layout.fillWidth: true; wrapMode: Text.WordWrap
            text: qsTr("Al aire: %1").arg((vocSec.v.reemplazos || []).slice(-4).map(x => x.original + " → " + x.reemplazo).join(" · "))
            color: Theme.textFaint; font.pixelSize: 11
        }
    }
}
