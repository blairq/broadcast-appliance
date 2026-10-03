// Página Equipos: todo lo que se puede encontrar y manejar (Studio.dispositivos,
// dispositivos/Dispositivos).
//
// Es GENÉRICA a propósito:
//   - los detectores salen de la lista que registra la app (red, esta máquina, USB);
//   - las acciones de cada equipo, con sus parámetros, salen de lo que se registró
//     para su tipo (dispositivos/Registro.cpp).
// El botón y su formulario se arman solos. Sumar una acción no toca esta página.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import BA
import "../controls"

Rectangle {
    id: page
    color: Theme.bg
    property real leftInset: 92
    readonly property var dd: Studio.dispositivos
    property string sel: ""
    property string filtro: ""
    property var accionAbierta: null     // la acción cuyo formulario se ve
    property var valores: ({})
    property var resultado: null         // {accion, texto|error, datos}
    property bool corriendo: false

    readonly property var equipo: {
        const l = dd ? dd.equipos : []
        for (let i = 0; i < l.length; ++i) if (l[i].id === sel) return l[i]
        return null
    }
    readonly property var lista: {
        const l = dd ? dd.equipos : []
        const f = filtro.toLowerCase()
        return f === "" ? l : l.filter(e => (e.nombre + " " + e.detalle + " " + e.tipo).toLowerCase().indexOf(f) >= 0)
    }
    function nombreTipo(t) {
        return ({ camara_red: "Cámaras de la red", equipo_red: "Otros equipos de la red", camara_local: "Cámaras de esta máquina",
                  microfono: "Micrófonos", usb: "USB", hid: "HID", serie: "Puertos serie" })[t] || t
    }
    function correr(a) {
        page.corriendo = true
        page.resultado = null
        page.dd.accionUi(page.sel, a.id, page.valores)
    }
    Connections {
        target: page.dd
        function onResultado(id, accion, r) {
            if (id !== page.sel) return
            page.corriendo = false
            page.resultado = Object.assign({ accion: accion }, r)
        }
    }
    onSelChanged: { accionAbierta = null; resultado = null; valores = ({}) }

    RowLayout {
        anchors { fill: parent; leftMargin: page.leftInset; margins: 20 }
        spacing: 16

        // --- Detectores y lista -------------------------------------------------------
        ColumnLayout {
            Layout.preferredWidth: 380
            Layout.fillHeight: true
            spacing: 10
            Text { text: "Equipos"; color: Theme.text; font { pixelSize: 20; weight: Font.DemiBold } }
            Repeater {
                model: page.dd ? page.dd.detectores : []
                delegate: RowLayout {
                    required property var modelData
                    Layout.fillWidth: true
                    spacing: 8
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 0
                        Text { text: modelData.nombre; color: Theme.text; font.pixelSize: 13 }
                        Text {
                            Layout.fillWidth: true
                            text: modelData.escaneando ? "buscando…"
                                : modelData.error ? "✕ " + modelData.error
                                : modelData.cuando ? modelData.cuantos + " encontrados (" + modelData.cuando + ")" : modelData.descripcion
                            color: modelData.error ? Theme.warn : Theme.textFaint; font.pixelSize: 10; elide: Text.ElideRight
                            ToolTip.visible: dHover.hovered; ToolTip.text: modelData.descripcion
                            HoverHandler { id: dHover }
                        }
                    }
                    GlassButton {
                        compact: true; text: modelData.escaneando ? "…" : "Buscar"; icon: "⌕"
                        enabled: !modelData.escaneando
                        onClicked: page.dd.escanearUi(modelData.id)
                    }
                }
            }
            TextField {
                Layout.fillWidth: true
                placeholderText: "Filtrar (nombre, IP, marca…)"
                color: Theme.text; font.pixelSize: 12
                onTextChanged: page.filtro = text
            }
            ListView {
                id: lv
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                spacing: 4
                model: page.lista
                section.property: "tipo"
                section.delegate: Text {
                    required property string section
                    text: page.nombreTipo(section)
                    color: Theme.textDim; font { pixelSize: 11; capitalization: Font.AllUppercase; letterSpacing: 0.5 }
                    topPadding: 8; bottomPadding: 2
                }
                Text {
                    anchors.centerIn: parent; width: parent.width - 20; visible: lv.count === 0
                    wrapMode: Text.WordWrap; horizontalAlignment: Text.AlignHCenter
                    text: "Todavía no hay equipos: tocá «Buscar» en un detector."; color: Theme.textFaint; font.pixelSize: 12
                }
                delegate: Rectangle {
                    required property var modelData
                    width: lv.width
                    height: col.implicitHeight + 12
                    radius: 8
                    color: modelData.id === page.sel ? Qt.rgba(1, 0.42, 0.17, 0.16) : Qt.rgba(1, 1, 1, 0.04)
                    border.color: modelData.id === page.sel ? Theme.accent : "transparent"
                    ColumnLayout {
                        id: col
                        anchors { left: parent.left; right: parent.right; verticalCenter: parent.verticalCenter; margins: 8 }
                        spacing: 1
                        Text { Layout.fillWidth: true; text: modelData.nombre; color: Theme.text; font.pixelSize: 12; elide: Text.ElideRight }
                        Text { Layout.fillWidth: true; text: modelData.detalle; color: Theme.textFaint; font.pixelSize: 10; elide: Text.ElideRight }
                    }
                    MouseArea { anchors.fill: parent; onClicked: page.sel = modelData.id }
                }
            }
        }

        // --- El equipo elegido ------------------------------------------------------------
        ScrollView {
            id: sv
            Layout.fillWidth: true
            Layout.fillHeight: true
            contentWidth: availableWidth
            clip: true
            visible: page.equipo !== null

            ColumnLayout {
                width: sv.availableWidth
                spacing: 12

                Panel {
                    Layout.fillWidth: true
                    padding: 12
                    implicitHeight: cab.implicitHeight + 2 * padding
                    ColumnLayout {
                        id: cab
                        anchors.fill: parent
                        spacing: 4
                        Text { text: page.equipo ? page.equipo.nombre : ""; color: Theme.text; font { pixelSize: 16; weight: Font.DemiBold } }
                        Text { Layout.fillWidth: true; wrapMode: Text.WordWrap; text: page.equipo ? page.equipo.detalle : ""; color: Theme.textDim; font.pixelSize: 12 }
                        Text { text: page.equipo ? page.nombreTipo(page.equipo.tipo) + " · " + page.equipo.id : ""; color: Theme.textFaint; font.pixelSize: 10 }
                    }
                }

                // Las acciones: un botón por cada una; con parámetros, su formulario.
                Panel {
                    Layout.fillWidth: true
                    padding: 12
                    implicitHeight: acc.implicitHeight + 2 * padding
                    ColumnLayout {
                        id: acc
                        anchors.fill: parent
                        spacing: 8
                        Text { text: "Acciones"; color: Theme.text; font { pixelSize: 14; weight: Font.DemiBold } }
                        Text {
                            visible: page.equipo && page.equipo.acciones.length === 0
                            text: "Este tipo de equipo todavía no tiene acciones."; color: Theme.textFaint; font.pixelSize: 12
                        }
                        Flow {
                            Layout.fillWidth: true
                            spacing: 6
                            Repeater {
                                model: page.equipo ? page.equipo.acciones : []
                                delegate: GlassButton {
                                    required property var modelData
                                    compact: true
                                    text: modelData.titulo
                                    active: page.accionAbierta && page.accionAbierta.id === modelData.id
                                    enabled: !page.corriendo
                                    ToolTip.visible: hovered; ToolTip.delay: 400; ToolTip.text: modelData.descripcion
                                    onClicked: {
                                        page.resultado = null
                                        if (modelData.parametros.length === 0) { page.accionAbierta = null; page.valores = ({}); page.correr(modelData) }
                                        else {
                                            const v = {}
                                            for (const p of modelData.parametros) if (p.defecto !== undefined) v[p.clave] = p.defecto
                                            page.valores = v
                                            page.accionAbierta = modelData
                                        }
                                    }
                                }
                            }
                        }
                        // El formulario, armado con los parámetros declarados.
                        ColumnLayout {
                            visible: page.accionAbierta !== null
                            Layout.fillWidth: true
                            spacing: 6
                            Text {
                                Layout.fillWidth: true; wrapMode: Text.WordWrap
                                text: page.accionAbierta ? page.accionAbierta.descripcion : ""; color: Theme.textDim; font.pixelSize: 11
                            }
                            Repeater {
                                model: page.accionAbierta ? page.accionAbierta.parametros : []
                                delegate: RowLayout {
                                    id: fila
                                    required property var modelData
                                    Layout.fillWidth: true
                                    spacing: 8
                                    Text {
                                        text: fila.modelData.etiqueta + (fila.modelData.obligatorio ? " *" : "")
                                        color: Theme.textDim; font.pixelSize: 12; Layout.preferredWidth: 150; wrapMode: Text.WordWrap
                                    }
                                    TextField {
                                        visible: fila.modelData.tipo === "texto" || fila.modelData.tipo === "clave" || fila.modelData.tipo === "numero"
                                        Layout.fillWidth: true
                                        echoMode: fila.modelData.tipo === "clave" ? TextInput.Password : TextInput.Normal
                                        placeholderText: fila.modelData.ayuda || ""
                                        text: page.valores[fila.modelData.clave] !== undefined ? String(page.valores[fila.modelData.clave]) : ""
                                        onTextEdited: { const v = page.valores; v[fila.modelData.clave] = fila.modelData.tipo === "numero" ? Number(text) : text; page.valores = v }
                                    }
                                    ScrollView {
                                        visible: fila.modelData.tipo === "json"
                                        Layout.fillWidth: true; Layout.preferredHeight: 80
                                        TextArea {
                                            font.family: "Menlo"; font.pixelSize: 11; color: Theme.text
                                            onTextChanged: { const v = page.valores; try { v[fila.modelData.clave] = JSON.parse(text) } catch (e) {} ; page.valores = v }
                                        }
                                    }
                                    ComboBox {
                                        visible: fila.modelData.tipo === "opcion"
                                        Layout.fillWidth: true
                                        model: fila.modelData.opciones || []
                                        currentIndex: Math.max(0, (fila.modelData.opciones || []).indexOf(page.valores[fila.modelData.clave]))
                                        onActivated: (i) => { const v = page.valores; v[fila.modelData.clave] = fila.modelData.opciones[i]; page.valores = v }
                                    }
                                    Switch {
                                        visible: fila.modelData.tipo === "si_no"
                                        checked: page.valores[fila.modelData.clave] === true
                                        onToggled: { const v = page.valores; v[fila.modelData.clave] = checked; page.valores = v }
                                    }
                                }
                            }
                            RowLayout {
                                Item { Layout.fillWidth: true }
                                GlassButton {
                                    compact: true; text: page.corriendo ? "…" : page.accionAbierta ? page.accionAbierta.titulo : ""; active: true
                                    enabled: !page.corriendo
                                    onClicked: page.correr(page.accionAbierta)
                                }
                            }
                        }
                        Text {
                            visible: page.corriendo
                            text: "Trabajando…"; color: Theme.amber; font.pixelSize: 12
                        }
                    }
                }

                // Lo que contestó.
                Panel {
                    Layout.fillWidth: true
                    visible: page.resultado !== null
                    padding: 12
                    implicitHeight: res.implicitHeight + 2 * padding
                    ColumnLayout {
                        id: res
                        anchors.fill: parent
                        spacing: 6
                        Text {
                            Layout.fillWidth: true; wrapMode: Text.WordWrap; font.pixelSize: 13
                            text: !page.resultado ? "" : page.resultado.error ? "✕ " + page.resultado.error : "✓ " + (page.resultado.texto || "Hecho")
                            color: page.resultado && page.resultado.error ? Theme.warn : Theme.ok
                        }
                        ScrollView {
                            visible: page.resultado && page.resultado.datos !== undefined
                            Layout.fillWidth: true
                            Layout.preferredHeight: 320
                            TextArea {
                                readOnly: true; selectByMouse: true
                                text: page.resultado && page.resultado.datos !== undefined ? JSON.stringify(page.resultado.datos, null, 2) : ""
                                color: Theme.text; font { pixelSize: 11; family: "Menlo" }
                                background: Rectangle { color: Theme.field; radius: 6 }
                            }
                        }
                    }
                }

                // Lo que se sabe del equipo.
                Panel {
                    Layout.fillWidth: true
                    padding: 12
                    implicitHeight: datosCol.implicitHeight + 2 * padding
                    ColumnLayout {
                        id: datosCol
                        anchors.fill: parent
                        spacing: 6
                        Text { text: "Lo que se sabe"; color: Theme.text; font { pixelSize: 14; weight: Font.DemiBold } }
                        TextArea {
                            Layout.fillWidth: true
                            readOnly: true; selectByMouse: true; wrapMode: TextEdit.Wrap
                            text: page.equipo ? JSON.stringify(page.equipo.datos, null, 2) : ""
                            color: Theme.textDim; font { pixelSize: 11; family: "Menlo" }
                            background: Rectangle { color: Theme.field; radius: 6 }
                        }
                    }
                }
            }
        }
        Text {
            visible: page.equipo === null
            Layout.fillWidth: true
            text: "Elegí un equipo de la lista para ver lo que se sabe de él y qué se le puede hacer."
            color: Theme.textFaint; font.pixelSize: 13; wrapMode: Text.WordWrap
        }
    }
}
