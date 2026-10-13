// La documentación del evento (PDF, sobre todo): las carátulas en una grilla (con ◀ ▶
// se elige qué página del PDF hace de carátula), agregar
// (o arrastrar) archivos, y al elegir uno, su título, descripción, si es público y a
// qué puntos del orden del día y secciones del guion corresponde. Sin relaciones es del
// evento en general. La página web del evento la muestra y deja bajar.
// Modelo: Studio.documentos (ui/DocumentosViewModel); lo mismo por MCP: documento_*.
import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import BA

Item {
    id: pd
    readonly property var dv: Studio.documentos
    readonly property var docs: dv ? dv.lista : []
    readonly property var puntos: Studio.ordenDelDia ? Studio.ordenDelDia.puntos : []
    /// Las secciones del guion: la clave de cada una (el id del bloque, o su número).
    property var bloques: []
    property string elegido: ""
    readonly property var doc: { for (const d of docs) if (d.id === elegido) return d; return null }

    function clave(b, i) { return b.id && b.id !== "" ? b.id : String(i + 1) }
    function tamano(b) { return b > 1e6 ? (b / 1e6).toFixed(1) + " MB" : Math.round((b || 0) / 1e3) + " kB" }
    function relaciones(d) {
        const l = []
        for (const n of (d.puntos || [])) l.push("Punto " + n)
        for (const s of (d.secciones || [])) {
            let nom = "Sección " + s
            for (let i = 0; i < pd.bloques.length; ++i) if (pd.clave(pd.bloques[i], i) === s) nom = pd.bloques[i].nombre
            l.push(nom)
        }
        return l.length ? l : ["General"]
    }
    function cambiar(c) { const e = pd.dv.editarUi(pd.elegido, c); if (e !== "") Studio.notify(e, "error") }
    function agregar(urls) {
        const e = pd.dv.agregarUi(urls)
        if (e !== "") Studio.notify("No se pudo agregar: " + e, "error")
    }

    FileDialog {
        id: elegir
        title: "Documentos para el evento"
        fileMode: FileDialog.OpenFiles
        nameFilters: ["PDF e imágenes (*.pdf *.png *.jpg *.jpeg *.webp)", "Todos (*)"]
        onAccepted: pd.agregar(selectedFiles.map(u => u.toString()))
    }

    RowLayout {
        anchors.fill: parent
        spacing: 12

        // --- Las carátulas -------------------------------------------------------
        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 8
            RowLayout {
                Layout.fillWidth: true
                GlassButton { icon: "+"; text: "Agregar documentos…"; onClicked: elegir.open() }
                GlassButton { compact: true; text: "Carpeta docs/"; onClicked: pd.dv.abrirCarpetaUi()
                              ToolTip.visible: hovered; ToolTip.delay: 400; ToolTip.text: "La carpeta de la documentación de este evento" }
                Item { Layout.fillWidth: true }
                Text { text: pd.docs.length + (pd.docs.length === 1 ? " documento" : " documentos"); color: Theme.textDim; font.pixelSize: 12 }
            }
            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                radius: 10
                color: soltar.containsDrag ? Qt.rgba(1, 0.42, 0.17, 0.12) : Qt.rgba(1, 1, 1, 0.03)
                border.color: soltar.containsDrag ? Theme.accent : Theme.glassBorder
                DropArea {
                    id: soltar
                    anchors.fill: parent
                    onDropped: (d) => { if (d.hasUrls) pd.agregar(d.urls.map(u => u.toString())) }
                }
                Text {
                    anchors.centerIn: parent
                    visible: pd.docs.length === 0
                    width: parent.width - 60
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.WordWrap
                    color: Theme.textFaint
                    text: "Arrastrá acá los PDF del evento (o «Agregar documentos…»). Cada uno guarda su carátula, y se puede "
                          + "relacionar con puntos del orden del día y secciones del guion: la página web del evento los muestra."
                }
                GridView {
                    id: grilla
                    anchors { fill: parent; margins: 10 }
                    clip: true
                    cellWidth: 172
                    cellHeight: 258
                    model: pd.docs
                    delegate: Item {
                        required property var modelData
                        width: grilla.cellWidth
                        height: grilla.cellHeight
                        Rectangle {
                            anchors { fill: parent; margins: 6 }
                            radius: 10
                            color: pd.elegido === modelData.id ? Qt.rgba(1, 0.42, 0.17, 0.16) : (ma.containsMouse ? Qt.rgba(1, 1, 1, 0.07) : Qt.rgba(1, 1, 1, 0.04))
                            border.color: pd.elegido === modelData.id ? Theme.accent : Theme.glassBorder
                            ColumnLayout {
                                anchors { fill: parent; margins: 8 }
                                spacing: 4
                                Rectangle {
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: 150
                                    radius: 4
                                    color: "white"
                                    clip: true
                                    Image {
                                        anchors.fill: parent
                                        source: modelData.url_miniatura || ""
                                        fillMode: Image.PreserveAspectFit
                                        cache: false
                                        asynchronous: true
                                        visible: !!modelData.url_miniatura
                                    }
                                    Text { anchors.centerIn: parent; visible: !modelData.url_miniatura; text: "PDF"; color: "#888"; font.pixelSize: 22 }
                                    Rectangle {
                                        visible: modelData.publico === false
                                        anchors { right: parent.right; top: parent.top; margins: 4 }
                                        radius: 4; color: Qt.rgba(0, 0, 0, 0.7); width: priv.implicitWidth + 8; height: 18
                                        Text { id: priv; anchors.centerIn: parent; text: "privado"; color: "white"; font.pixelSize: 10 }
                                    }
                                }
                                Text { Layout.fillWidth: true; text: modelData.titulo; color: Theme.text; font.pixelSize: 12; font.weight: Font.DemiBold
                                       elide: Text.ElideRight; maximumLineCount: 2; wrapMode: Text.WordWrap }
                                Text { Layout.fillWidth: true; color: Theme.textFaint; font.pixelSize: 10; elide: Text.ElideRight
                                       text: (modelData.paginas ? modelData.paginas + " pág. · " : "") + pd.tamano(modelData.bytes) }
                                Text { Layout.fillWidth: true; color: Theme.accent; font.pixelSize: 10; elide: Text.ElideRight
                                       text: pd.relaciones(modelData).join(" · ") }
                                Item { Layout.fillHeight: true }
                            }
                            MouseArea {
                                id: ma
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: pd.elegido = modelData.id
                                onDoubleClicked: pd.dv.abrirUi(modelData.id)
                            }
                            ToolTip.visible: ma.containsMouse; ToolTip.delay: 600
                            ToolTip.text: (modelData.descripcion || modelData.titulo) + "\n(doble clic: abrirlo)"
                        }
                    }
                }
            }
        }

        // --- El elegido --------------------------------------------------------------
        Rectangle {
            Layout.preferredWidth: 330
            Layout.fillHeight: true
            radius: 10
            color: Qt.rgba(1, 1, 1, 0.03)
            border.color: Theme.glassBorder
            Text {
                anchors.centerIn: parent
                visible: !pd.doc
                width: parent.width - 40
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
                color: Theme.textFaint
                text: "Elegí un documento para describirlo y decir a qué punto o sección corresponde."
            }
            ScrollView {
                id: edScroll
                anchors { fill: parent; margins: 12 }
                visible: !!pd.doc
                contentWidth: availableWidth
                clip: true
                ColumnLayout {
                    width: edScroll.availableWidth
                    spacing: 8
                    // La carátula: con ◀ ▶ se elige otra página del PDF (rehace la miniatura de la web).
                    Rectangle {
                        Layout.alignment: Qt.AlignHCenter
                        Layout.preferredWidth: 170
                        Layout.preferredHeight: 220
                        radius: 4
                        color: "white"
                        clip: true
                        Image {
                            anchors.fill: parent
                            source: pd.doc && pd.doc.url_miniatura ? pd.doc.url_miniatura : ""
                            fillMode: Image.PreserveAspectFit
                            cache: false
                            asynchronous: true
                        }
                    }
                    RowLayout {
                        readonly property bool esPdf: !!pd.doc && /\.pdf$/i.test(pd.doc.archivo || "") && (pd.doc.paginas || 0) > 1
                        readonly property int pag: pd.doc ? (pd.doc.pagina_tapa || 1) : 1
                        visible: esPdf
                        Layout.alignment: Qt.AlignHCenter
                        spacing: 6
                        GlassButton {
                            compact: true; text: "◀"; enabled: parent.pag > 1
                            onClicked: pd.cambiar({ pagina_tapa: parent.pag - 1 })
                        }
                        Text {
                            text: "Carátula: página " + parent.pag + " de " + (pd.doc ? pd.doc.paginas : 0)
                            color: Theme.textDim; font.pixelSize: 11
                        }
                        GlassButton {
                            compact: true; text: "▶"; enabled: !!pd.doc && parent.pag < pd.doc.paginas
                            onClicked: pd.cambiar({ pagina_tapa: parent.pag + 1 })
                        }
                    }
                    Text { text: "Título"; color: Theme.textDim; font.pixelSize: 11 }
                    TextField {
                        Layout.fillWidth: true
                        text: pd.doc ? pd.doc.titulo : ""
                        color: Theme.text
                        onEditingFinished: if (pd.doc && text !== pd.doc.titulo) pd.cambiar({ titulo: text })
                    }
                    Text { text: "Descripción (la ve el público)"; color: Theme.textDim; font.pixelSize: 11 }
                    TextArea {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 90
                        wrapMode: TextEdit.Wrap
                        text: pd.doc ? pd.doc.descripcion : ""
                        color: Theme.text
                        placeholderText: "De qué se trata, para qué sirve…"
                        onEditingFinished: if (pd.doc && text !== pd.doc.descripcion) pd.cambiar({ descripcion: text })
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        Text { Layout.fillWidth: true; text: "Público en la web del evento"; color: Theme.text; font.pixelSize: 13 }
                        Toggle { checked: !!pd.doc && pd.doc.publico !== false; onToggled: (q) => pd.cambiar({ publico: q }) }
                    }
                    Text { text: "Puntos del orden del día"; color: Theme.textDim; font.pixelSize: 11; Layout.topMargin: 6 }
                    Text { visible: pd.puntos.length === 0; text: "Sin orden del día cargado."; color: Theme.textFaint; font.pixelSize: 11 }
                    Repeater {
                        model: pd.puntos
                        delegate: CheckBox {
                            required property var modelData
                            Layout.fillWidth: true
                            text: modelData.n + ". " + (modelData.titulo || modelData.expediente || "")
                            checked: !!pd.doc && (pd.doc.puntos || []).indexOf(modelData.n) >= 0
                            onToggled: {
                                let l = (pd.doc.puntos || []).filter(x => x !== modelData.n)
                                if (checked) l.push(modelData.n)
                                pd.cambiar({ puntos: l })
                            }
                        }
                    }
                    Text { text: "Secciones del guion"; color: Theme.textDim; font.pixelSize: 11; Layout.topMargin: 6 }
                    Text { visible: pd.bloques.length === 0; text: "El guion no tiene secciones."; color: Theme.textFaint; font.pixelSize: 11 }
                    Repeater {
                        model: pd.bloques
                        delegate: CheckBox {
                            required property var modelData
                            required property int index
                            readonly property string k: pd.clave(modelData, index)
                            Layout.fillWidth: true
                            text: modelData.nombre || ("Sección " + (index + 1))
                            checked: !!pd.doc && (pd.doc.secciones || []).indexOf(k) >= 0
                            onToggled: {
                                let l = (pd.doc.secciones || []).filter(x => x !== k)
                                if (checked) l.push(k)
                                pd.cambiar({ secciones: l })
                            }
                        }
                    }
                    Text { Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.textFaint; font.pixelSize: 11
                           text: "Sin puntos ni secciones: es documentación del evento en general (la portada de la web)." }
                    RowLayout {
                        Layout.topMargin: 8
                        GlassButton { compact: true; text: "Abrir"; onClicked: pd.dv.abrirUi(pd.elegido) }
                        Item { Layout.fillWidth: true }
                        GlassButton {
                            compact: true; text: "Quitar"
                            onClicked: {
                                const id = pd.elegido, t = pd.doc ? pd.doc.titulo : ""
                                ApplicationWindow.window.confirm({
                                    title: "¿Quitar «" + t + "»?",
                                    text: "Sale de la documentación del evento (el archivo va a la papelera del evento).",
                                    accept: "Quitar", reject: "Cancelar",
                                    action: function() { const e = pd.dv.quitarUi(id); if (e !== "") Studio.notify(e, "error"); else pd.elegido = "" } })
                            }
                        }
                    }
                }
            }
        }
    }
}
