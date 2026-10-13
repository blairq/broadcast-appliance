// Tarjeta «PDF»: el PDF que muestra una fuente PDF de la escena, y pasarle páginas.
// Arriba la fuente (si hay más de una), el documento y «página N de M» con las
// flechas; abajo los PDF de la documentación del evento (con su carátula) para
// elegir uno, u «Otro PDF…». Con el teclado (un clic en la tarjeta): ← → páginas,
// Inicio / Fin. Sin ninguna fuente PDF, un botón arma la escena «PDF».
// Modelo: Studio.pdfEscena (ui/PdfEscena); lo mismo por MCP: pdf_escena_*.
import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import BA
import "../controls"

Item {
    id: tarjeta
    focus: true
    readonly property var pe: Studio.pdfEscena
    readonly property var fuentes: pe ? pe.fuentes : []
    property string elegida: ""
    /// La fuente que se maneja: la elegida, o la primera.
    readonly property var f: {
        for (const x of fuentes) if (x.nombre === elegida) return x
        return fuentes.length ? fuentes[0] : null
    }
    function ok(e) { if (e !== "") Studio.notify(e, "error") }

    Keys.onPressed: (e) => {
        if (!tarjeta.f || Studio.focoEnTexto()) return
        if (e.key === Qt.Key_Right || e.key === Qt.Key_PageDown || e.key === Qt.Key_Space) { tarjeta.ok(tarjeta.pe.paso(tarjeta.f.nombre, 1)); e.accepted = true }
        else if (e.key === Qt.Key_Left || e.key === Qt.Key_PageUp) { tarjeta.ok(tarjeta.pe.paso(tarjeta.f.nombre, -1)); e.accepted = true }
        else if (e.key === Qt.Key_Home) { tarjeta.ok(tarjeta.pe.irA(tarjeta.f.nombre, 1)); e.accepted = true }
        else if (e.key === Qt.Key_End) { tarjeta.ok(tarjeta.pe.irA(tarjeta.f.nombre, 100000)); e.accepted = true }
    }

    FileDialog {
        id: otro
        title: "Un PDF para mostrar"
        nameFilters: ["PDF (*.pdf)"]
        onAccepted: tarjeta.ok(tarjeta.pe.elegirArchivo(tarjeta.f ? tarjeta.f.nombre : "", selectedFile.toString()))
    }

    // Sin fuente PDF: armar la escena.
    ColumnLayout {
        anchors.centerIn: parent
        width: parent.width - 40
        visible: tarjeta.fuentes.length === 0
        spacing: 10
        Text {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            horizontalAlignment: Text.AlignHCenter
            text: "No hay ninguna fuente PDF en las escenas. Armá una escena para mostrar PDF: se elige el documento acá y se pasan las páginas."
            color: Theme.textFaint; font.pixelSize: 12
        }
        GlassButton {
            Layout.alignment: Qt.AlignHCenter
            icon: "+"; text: "Armar la escena «PDF»"; active: true
            onClicked: tarjeta.ok(tarjeta.pe.crearEscena("PDF"))
        }
    }

    ColumnLayout {
        anchors.fill: parent
        visible: tarjeta.fuentes.length > 0
        spacing: 8

        RowLayout {
            Layout.fillWidth: true
            visible: tarjeta.fuentes.length > 1
            Text { text: "Fuente"; color: Theme.textDim; font.pixelSize: 11 }
            ComboBox {
                Layout.fillWidth: true
                model: tarjeta.fuentes.map(x => x.nombre)
                currentIndex: Math.max(0, model.indexOf(tarjeta.f ? tarjeta.f.nombre : ""))
                onActivated: (i) => tarjeta.elegida = model[i]
            }
        }

        // Lo que se ve y las páginas.
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 92
            radius: 10
            color: Qt.rgba(1, 1, 1, 0.05)
            border.color: Theme.glassBorder
            ColumnLayout {
                anchors { fill: parent; margins: 10 }
                spacing: 6
                RowLayout {
                    Layout.fillWidth: true
                    Text {
                        Layout.fillWidth: true
                        elide: Text.ElideRight
                        text: tarjeta.f ? (tarjeta.f.titulo || "Elegí un PDF de la lista") : ""
                        color: tarjeta.f && tarjeta.f.titulo ? Theme.text : Theme.textFaint
                        font { pixelSize: 14; weight: Font.DemiBold }
                    }
                    Text {
                        visible: !!tarjeta.f && !!tarjeta.f.error
                        text: tarjeta.f && tarjeta.f.error ? "⚠ " + tarjeta.f.error : ""
                        color: Theme.amber; font.pixelSize: 11
                    }
                }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 6
                    enabled: !!tarjeta.f && tarjeta.f.paginas > 0
                    GlassButton { compact: true; text: "⏮"; onClicked: tarjeta.ok(tarjeta.pe.irA(tarjeta.f.nombre, 1)) }
                    GlassButton { text: "◀"; onClicked: tarjeta.ok(tarjeta.pe.paso(tarjeta.f.nombre, -1)) }
                    Text {
                        Layout.fillWidth: true
                        horizontalAlignment: Text.AlignHCenter
                        text: tarjeta.f && tarjeta.f.paginas > 0 ? "Página " + tarjeta.f.pagina + " de " + tarjeta.f.paginas : "—"
                        color: Theme.text
                        font { family: Theme.mono; pixelSize: 16; weight: Font.DemiBold }
                    }
                    GlassButton { text: "▶"; active: true; onClicked: tarjeta.ok(tarjeta.pe.paso(tarjeta.f.nombre, 1)) }
                    GlassButton { compact: true; text: "⏭"; onClicked: tarjeta.ok(tarjeta.pe.irA(tarjeta.f.nombre, 100000)) }
                }
            }
        }

        // Los PDF del evento, para elegir.
        RowLayout {
            Layout.fillWidth: true
            Text { Layout.fillWidth: true; text: "Documentación del evento"; color: Theme.textDim; font.pixelSize: 11 }
            GlassButton { compact: true; text: "Otro PDF…"; enabled: !!tarjeta.f; onClicked: otro.open() }
        }
        ListView {
            id: lista
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            orientation: ListView.Horizontal
            spacing: 8
            model: tarjeta.pe ? tarjeta.pe.documentos : []
            Text {
                anchors.centerIn: parent
                visible: lista.count === 0
                width: parent.width - 20
                wrapMode: Text.WordWrap
                horizontalAlignment: Text.AlignHCenter
                text: "Sin PDF en la documentación del evento: agregalos en Guion del evento → Documentos, o usá «Otro PDF…»."
                color: Theme.textFaint; font.pixelSize: 11
            }
            delegate: Rectangle {
                required property var modelData
                readonly property bool actual: !!tarjeta.f && tarjeta.f.documento === modelData.id
                width: Math.min(130, lista.height * 0.72)
                height: lista.height
                radius: 8
                color: actual ? Qt.rgba(1, 0.42, 0.17, 0.18) : (dma.containsMouse ? Qt.rgba(1, 1, 1, 0.08) : Qt.rgba(1, 1, 1, 0.04))
                border.color: actual ? Theme.accent : Theme.glassBorder
                ColumnLayout {
                    anchors { fill: parent; margins: 6 }
                    spacing: 3
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        radius: 3
                        color: "white"
                        clip: true
                        Image {
                            anchors.fill: parent
                            source: modelData.url_miniatura || ""
                            fillMode: Image.PreserveAspectFit
                            asynchronous: true
                        }
                    }
                    Text {
                        Layout.fillWidth: true
                        text: modelData.titulo
                        color: Theme.text; font.pixelSize: 10
                        elide: Text.ElideRight
                    }
                    Text {
                        text: modelData.paginas + " pág."
                        color: Theme.textFaint; font.pixelSize: 9
                    }
                }
                MouseArea {
                    id: dma
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: { tarjeta.forceActiveFocus(); tarjeta.ok(tarjeta.pe.elegir(tarjeta.f ? tarjeta.f.nombre : "", modelData.id)) }
                    ToolTip.visible: containsMouse; ToolTip.delay: 500; ToolTip.text: "Mostrar «" + modelData.titulo + "» (desde la página 1)"
                }
            }
        }
    }
}
