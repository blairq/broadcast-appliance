// El selector de herramientas de la Botonera: qué hace cada botón.
//
// Antes era un desplegable con 400 nombres técnicos («scene_switch»,
// «dsk_armar»…): había que saber de memoria cuál era cuál. Acá van por
// secciones (las categorías del MCP), cada una con su explicación y una captura
// chica de la página donde vive, y arriba una búsqueda de texto completo: el
// nombre, el título, la explicación y las palabras con que la gente la pide
// («que baje la música», «poner al aire»), sin importar acentos ni mayúsculas.
//
// La búsqueda es LA MISMA que la del asistente (help_search: el índice con pesos,
// etiquetas y prefijos), no una propia: lo que encuentra uno lo encuentra el
// otro, en el mismo orden. Con una sección elegida, busca adentro de esa.
//
// Fuente: Studio.botonera.catalogoUi() (McpCore::catalogo) y buscarUi()
// (McpCore::buscarHerramientas). Filtrar, contar y ordenar lo hace el modelo
// (FiltroCatalogo, el mismo de los catálogos de filtros y de módulos): si el buscador
// no devuelve nada, la búsqueda simple (todas las palabras, sin tildes).
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import BA

Popup {
    id: sel
    /// La que está puesta (se marca y la lista arranca ahí).
    property string actual: ""
    /// Eligió una: su nombre y, si tiene, unos argumentos de ejemplo (JSON).
    signal elegida(string nombre, string ejemplo)

    property var catalogo: []
    property string seccion: ""     // "" = todas
    readonly property var porSeccion: {
        const m = {}
        for (const c of sel.catalogo) m[c.titulo] = c
        return m
    }
    /// Lo que se ve: una fila por herramienta, con su sección (las cuentas, el orden y por
    /// qué salió, del modelo).
    readonly property alias filas: modelo
    FiltroCatalogo {
        id: modelo
        clave: "nombre"
        roles: ["secTitulo"]
        fuente: {
            const l = []
            for (const c of sel.catalogo)
                for (const h of c.herramientas)
                    l.push({ sec: c.id, secTitulo: c.titulo, nombre: h.nombre, titulo: h.titulo, descripcion: h.descripcion,
                             etiquetas: h.etiquetas, ejemplo: h.ejemplo || "", soloLectura: !!h.solo_lectura,
                             destructiva: !!h.destructiva })
            return l
        }
        campoCategoria: "sec"
        camposCabeza: ["titulo", "nombre"]
        campos: ["descripcion", "etiquetas", "secTitulo"]
        texto: buscar.text
        categoria: sel.seccion
        agrupar: true
        // El índice del asistente: cuáles y en qué orden (y por qué).
        puntajes: {
            const t = buscar.text.trim()
            if (t === "" || !Studio.botonera.buscarUi) return ({})
            const m = {}
            for (const r of Studio.botonera.buscarUi(t)) m[r.nombre] = r
            return m
        }
    }

    modal: true
    focus: true
    anchors.centerIn: Overlay.overlay
    width: Math.min(Overlay.overlay ? Overlay.overlay.width - 60 : 1100, 1180)
    height: Math.min(Overlay.overlay ? Overlay.overlay.height - 60 : 800, 860)
    implicitWidth: width
    implicitHeight: height
    padding: 18
    background: Rectangle { color: Theme.panel; radius: Theme.radiusSmall; border.color: Theme.glassBorder }

    function abrir(nombreActual, busqueda) {
        actual = nombreActual || ""
        catalogo = Studio.botonera.catalogoUi()
        buscar.text = busqueda || ""
        seccion = ""
        open()
        buscar.forceActiveFocus()
        // Arranca en la que está puesta (el modelo filtra al terminar la vuelta).
        Qt.callLater(function() {
            const i = modelo.indiceDe(actual)
            if (i >= 0) { lista.positionViewAtIndex(i, ListView.Center); lista.currentIndex = i; return }
            lista.positionViewAtBeginning()
        })
    }
    function elegir(i) {
        const f = modelo.fila(i)
        sel.elegida(f.nombre, f.ejemplo); sel.close()
    }

    function cuantasEn(id) {
        if (buscar.text.trim() !== "") return modelo.cuentas[id] || 0
        for (const c of catalogo) if (c.id === id) return c.herramientas.length
        return 0
    }

    contentItem: ColumnLayout {
        spacing: 12

        // --- Arriba: el título y la búsqueda -----------------------------------
        RowLayout {
            Layout.fillWidth: true
            spacing: 12
            Text {
                text: qsTr("Qué hace este botón")
                color: Theme.text
                font { pixelSize: 18; weight: Font.DemiBold }
            }
            TextField {
                id: buscar
                Layout.fillWidth: true
                color: Theme.text
                placeholderText: sel.seccion === "" ? qsTr("Buscar en todo: «poner al aire», «música», «grabar», «cámara 2»…")
                                                    : qsTr("Buscar en «%1» (o elegí «Todas»)").arg((sel.catalogo.find(c => c.id === sel.seccion) || {}).titulo || sel.seccion)
                Keys.onDownPressed: { lista.forceActiveFocus(); if (lista.currentIndex < 0) lista.currentIndex = 0 }
                Keys.onReturnPressed: if (modelo.count > 0) sel.elegir(Math.max(0, lista.currentIndex))
            }
            Text {
                text: qsTr("%n herramienta", "", modelo.count)
                color: Theme.textDim
                font.pixelSize: 12
            }
            GlassButton { compact: true; text: qsTr("Cerrar"); onClicked: sel.close() }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 12

            // --- Las secciones --------------------------------------------------
            ListView {
                id: secciones
                Layout.preferredWidth: 230
                Layout.fillHeight: true
                clip: true
                spacing: 2
                model: [{ id: "", titulo: qsTr("Todas") }].concat(sel.catalogo)
                delegate: Rectangle {
                    required property var modelData
                    width: secciones.width
                    height: 34
                    radius: 8
                    readonly property bool activa: sel.seccion === modelData.id
                    // Buscando, las secciones sin coincidencias quedan apagadas.
                    opacity: buscar.text.trim() !== "" && modelData.id !== "" && sel.cuantasEn(modelData.id) === 0 ? 0.4 : 1
                    color: activa ? Qt.rgba(1, 0.42, 0.17, 0.22) : maSec.containsMouse ? Theme.glassHighlight : "transparent"
                    RowLayout {
                        anchors { fill: parent; leftMargin: 10; rightMargin: 10 }
                        Text {
                            Layout.fillWidth: true
                            text: modelData.titulo
                            elide: Text.ElideRight
                            color: activa ? Theme.text : Theme.textDim
                            font.pixelSize: 13
                        }
                        Text {
                            visible: modelData.id !== ""
                            text: sel.cuantasEn(modelData.id)
                            color: Theme.textFaint
                            font.pixelSize: 11
                        }
                    }
                    MouseArea {
                        id: maSec
                        anchors.fill: parent
                        hoverEnabled: true
                        onClicked: {
                            // Lo escrito se queda: la sección acota la búsqueda.
                            sel.seccion = modelData.id
                            lista.positionViewAtBeginning()
                        }
                    }
                }
            }

            Rectangle { Layout.fillHeight: true; Layout.preferredWidth: 1; color: Theme.glassBorder }

            // --- Las herramientas, por sección ----------------------------------
            ListView {
                id: lista
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                spacing: 4
                currentIndex: -1
                model: modelo
                keyNavigationEnabled: true
                ScrollBar.vertical: ScrollBar {}
                Keys.onReturnPressed: if (currentIndex >= 0) sel.elegir(currentIndex)

                section.property: "secTitulo"
                section.criteria: ViewSection.FullString
                section.delegate: Item {
                    required property string section
                    readonly property var cat: sel.porSeccion[section] || ({})
                    width: lista.width
                    height: cabecera.implicitHeight + 18
                    RowLayout {
                        id: cabecera
                        anchors { left: parent.left; right: parent.right; top: parent.top; topMargin: 12 }
                        spacing: 12
                        // La captura de la página donde vive esta sección.
                        Rectangle {
                            visible: !!cat.captura
                            Layout.preferredWidth: 144
                            Layout.preferredHeight: 81
                            radius: 6
                            color: Theme.paletaOscura
                            border.color: Theme.glassBorder
                            clip: true
                            Image {
                                anchors { fill: parent; margins: 1 }
                                source: cat.captura || ""
                                fillMode: Image.PreserveAspectCrop
                                asynchronous: true
                                smooth: true
                            }
                        }
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 2
                            Text {
                                text: section
                                color: Theme.accentSoft
                                font { pixelSize: 15; weight: Font.DemiBold }
                            }
                            Text {
                                Layout.fillWidth: true
                                visible: (cat.resumen || "") !== ""
                                text: cat.resumen || ""
                                wrapMode: Text.WordWrap
                                color: Theme.textDim
                                font.pixelSize: 12
                            }
                        }
                    }
                }

                delegate: Rectangle {
                    id: fila
                    required property var datos
                    required property int index
                    width: lista.width - 12
                    height: col.implicitHeight + 16
                    radius: 8
                    readonly property bool puesta: datos.nombre === sel.actual
                    color: lista.currentIndex === index ? Qt.rgba(1, 0.42, 0.17, 0.16)
                         : maFila.containsMouse ? Theme.glassHighlight : Qt.rgba(1, 1, 1, 0.03)
                    border.color: puesta ? Theme.accent : "transparent"
                    ColumnLayout {
                        id: col
                        anchors { left: parent.left; right: parent.right; top: parent.top; margins: 8; leftMargin: 12; rightMargin: 12 }
                        spacing: 3
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 8
                            Text {
                                text: datos.titulo
                                color: Theme.text
                                font { pixelSize: 14; weight: Font.DemiBold }
                            }
                            Text {
                                text: datos.nombre
                                color: Theme.textFaint
                                font { family: Theme.mono; pixelSize: 11 }
                            }
                            Item { Layout.fillWidth: true }
                            Text {
                                visible: datos.soloLectura
                                text: qsTr("solo mira")
                                color: Theme.ok
                                font.pixelSize: 10
                            }
                            Text {
                                visible: datos.destructiva
                                text: qsTr("borra o cambia")
                                color: Theme.amber
                                font.pixelSize: 10
                            }
                            Text {
                                visible: fila.puesta
                                text: qsTr("la de ahora")
                                color: Theme.accentSoft
                                font.pixelSize: 10
                            }
                        }
                        // Por qué salió (las palabras que engancharon): para corregir la búsqueda, no probar a ciegas.
                        Text {
                            visible: (datos.porque || "") !== ""
                            text: qsTr("por: %1").arg(datos.porque)
                            color: Theme.textFaint
                            font.pixelSize: 10
                        }
                        Text {
                            Layout.fillWidth: true
                            text: datos.descripcion
                            wrapMode: Text.WordWrap
                            maximumLineCount: lista.currentIndex === index ? 12 : 3
                            elide: Text.ElideRight
                            color: Theme.textDim
                            font.pixelSize: 12
                        }
                    }
                    MouseArea {
                        id: maFila
                        anchors.fill: parent
                        hoverEnabled: true
                        onClicked: lista.currentIndex = index
                        onDoubleClicked: { sel.elegida(datos.nombre, datos.ejemplo); sel.close() }
                    }
                }

                Text {
                    anchors.centerIn: parent
                    visible: modelo.count === 0
                    text: qsTr("Nada con eso. Probá con otras palabras (lo que querés que pase, no el nombre técnico).")
                    color: Theme.textFaint
                    font.pixelSize: 13
                }
            }
        }

        // --- Abajo: la elegida ---------------------------------------------------
        RowLayout {
            Layout.fillWidth: true
            spacing: 10
            Text {
                Layout.fillWidth: true
                elide: Text.ElideRight
                color: Theme.textDim
                font.pixelSize: 12
                readonly property var actual: lista.currentItem ? lista.currentItem.datos : null
                text: actual
                      ? (actual.ejemplo ? qsTr("«%1» · trae argumentos de ejemplo · doble clic o Enter para usarla")
                                        : qsTr("«%1» · doble clic o Enter para usarla")).arg(actual.titulo)
                      : qsTr("Tocá una para leer todo lo que hace; doble clic o Enter para usarla.")
            }
            GlassButton {
                compact: true
                text: qsTr("Sin herramienta")
                onClicked: { sel.elegida("", ""); sel.close() }
            }
            GlassButton {
                compact: true
                active: true
                text: qsTr("Usar esta")
                enabled: lista.currentIndex >= 0 && lista.currentIndex < modelo.count
                onClicked: sel.elegir(lista.currentIndex)
            }
        }
    }
}
