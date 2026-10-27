// Tarjeta «PDF»: el PDF que muestra una fuente PDF de la escena, y pasarle páginas.
// Arriba la fuente (si hay más de una), el documento y «página N de M» con las
// flechas; abajo los PDF de la documentación del evento (con su carátula) para
// elegir uno, u «Otro PDF…». El encuadre: la barra de zoom y un joystick (como el del
// PTZ) para moverse por la página ampliada. Con el teclado (un clic en la tarjeta):
// ← → páginas, Inicio / Fin. Sin ninguna fuente PDF, un botón arma la escena «PDF».
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
        title: qsTr("Un PDF para mostrar")
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
            text: qsTr("No hay ninguna fuente PDF en las escenas. Armá una escena para mostrar PDF: se elige el documento acá y se pasan las páginas.")
            color: Theme.textFaint; font.pixelSize: 12
        }
        GlassButton {
            Layout.alignment: Qt.AlignHCenter
            icon: "+"; text: qsTr("Armar la escena «%1»").arg("PDF"); active: true
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
            Text { text: qsTr("Fuente"); color: Theme.textDim; font.pixelSize: 11 }
            ComboBox {
                Layout.fillWidth: true
                model: tarjeta.fuentes.map(x => x.nombre)
                currentIndex: Math.max(0, model.indexOf(tarjeta.f ? tarjeta.f.nombre : ""))
                onActivated: (i) => tarjeta.elegida = model[i]
            }
        }

        // Lo que se ve, las páginas y el encuadre (zoom y joystick, como el PTZ).
        RowLayout {
            Layout.fillWidth: true
            spacing: 10
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 6
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
                                text: tarjeta.f ? (tarjeta.f.titulo || qsTr("Elegí un PDF de la lista")) : ""
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
                                text: tarjeta.f && tarjeta.f.paginas > 0 ? qsTr("Página %1 de %2").arg(tarjeta.f.pagina).arg(tarjeta.f.paginas) : "—"
                                color: Theme.text
                                font { family: Theme.mono; pixelSize: 16; weight: Font.DemiBold }
                            }
                            GlassButton { text: "▶"; active: true; onClicked: tarjeta.ok(tarjeta.pe.paso(tarjeta.f.nombre, 1)) }
                            GlassButton { compact: true; text: "⏭"; onClicked: tarjeta.ok(tarjeta.pe.irA(tarjeta.f.nombre, 100000)) }
                        }
                    }
                }
                // El zoom: 1 = la página entera.
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8
                    enabled: !!tarjeta.f && tarjeta.f.paginas > 0
                    Text { text: qsTr("Zoom"); color: Theme.textDim; font.pixelSize: 11 }
                    Slider {
                        id: zoom
                        Layout.fillWidth: true
                        from: 1; to: 8; stepSize: 0.05
                        onMoved: tarjeta.ok(tarjeta.pe.encuadre(tarjeta.f.nombre, value, -1, -1))
                    }
                    // Sigue al modelo salvo mientras se arrastra (lo cambian también el MCP y la botonera).
                    Binding { target: zoom; property: "value"; value: tarjeta.f ? (tarjeta.f.zoom || 1) : 1; when: !zoom.pressed }
                    Text {
                        text: (tarjeta.f ? (tarjeta.f.zoom || 1) : 1).toFixed(1) + "×"
                        color: Theme.text; font { family: Theme.mono; pixelSize: 12 }
                        Layout.preferredWidth: 36
                    }
                    GlassButton {
                        compact: true; text: qsTr("Entera")
                        onClicked: tarjeta.ok(tarjeta.pe.encuadre(tarjeta.f.nombre, 1, 0.5, 0.5))
                        ToolTip.visible: hovered; ToolTip.delay: 400; ToolTip.text: qsTr("La página entera, sin zoom")
                    }
                }
            }
            // El joystick: mantenerlo corrido mueve lo que se ve hacia ese lado (más
            // lejos del centro, más rápido); la rueda acerca y aleja.
            Rectangle {
                id: pad
                property real px: 0
                property real py: 0
                Layout.preferredWidth: 104; Layout.preferredHeight: 104
                Layout.alignment: Qt.AlignTop
                radius: 52
                // Mueve solo con zoom (entera no hay adónde ir); la rueda anda siempre.
                readonly property bool activo: !!tarjeta.f && tarjeta.f.paginas > 0 && (tarjeta.f.zoom || 1) > 1.001
                opacity: activo ? 1 : 0.45
                color: Qt.rgba(1, 1, 1, 0.05); border.color: Qt.rgba(1, 1, 1, 0.18)
                Rectangle {
                    width: 32; height: 32; radius: 16
                    color: padMa.pressed ? Theme.accent : Qt.rgba(1, 1, 1, 0.22)
                    x: pad.width / 2 - width / 2 + pad.px * (pad.width / 2 - 16)
                    y: pad.height / 2 - height / 2 - pad.py * (pad.height / 2 - 16)
                }
                Text {
                    anchors { horizontalCenter: parent.horizontalCenter; top: parent.bottom; topMargin: 2 }
                    text: pad.activo ? qsTr("mover · rueda: zoom") : qsTr("rueda: zoom")
                    color: Theme.textFaint; font.pixelSize: 9
                }
                Timer {
                    interval: 60; repeat: true; running: pad.activo && padMa.pressed && (pad.px !== 0 || pad.py !== 0)
                    onTriggered: tarjeta.pe.mover(tarjeta.f.nombre, pad.px * 0.06, -pad.py * 0.06)
                }
                MouseArea {
                    id: padMa
                    anchors.fill: parent
                    preventStealing: true
                    function leer(m) {
                        let dx = (m.x - pad.width / 2) / (pad.width / 2 - 16)
                        let dy = -(m.y - pad.height / 2) / (pad.height / 2 - 16)
                        const r = Math.sqrt(dx * dx + dy * dy)
                        if (r > 1) { dx /= r; dy /= r }
                        pad.px = Math.abs(dx) < 0.08 ? 0 : dx
                        pad.py = Math.abs(dy) < 0.08 ? 0 : dy
                    }
                    onPressed: (m) => { tarjeta.forceActiveFocus(); leer(m) }
                    onPositionChanged: (m) => leer(m)
                    onReleased: { pad.px = 0; pad.py = 0 }
                    onCanceled: { pad.px = 0; pad.py = 0 }
                    onWheel: (w) => {
                        if (!tarjeta.f || !(tarjeta.f.paginas > 0)) return
                        const z = Math.max(1, Math.min(8, (tarjeta.f.zoom || 1) * (w.angleDelta.y > 0 ? 1.15 : 1 / 1.15)))
                        tarjeta.ok(tarjeta.pe.encuadre(tarjeta.f.nombre, z, -1, -1))
                    }
                }
            }
        }

        // Los PDF del evento, para elegir.
        RowLayout {
            Layout.fillWidth: true
            Text { Layout.fillWidth: true; text: qsTr("Documentación del evento"); color: Theme.textDim; font.pixelSize: 11 }
            GlassButton { compact: true; text: qsTr("Otro PDF…"); enabled: !!tarjeta.f; onClicked: otro.open() }
        }
        ListView {
            id: lista
            Layout.fillWidth: true
            Layout.fillHeight: true
            // Las carátulas se tienen que ver aunque la tarjeta sea baja.
            Layout.minimumHeight: 96
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
                text: qsTr("Sin PDF en la documentación del evento: agregalos en Guion del evento → Documentos, o usá «Otro PDF…».")
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
                        text: qsTr("%1 pág.").arg(modelData.paginas)
                        color: Theme.textFaint; font.pixelSize: 9
                    }
                }
                MouseArea {
                    id: dma
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: { tarjeta.forceActiveFocus(); tarjeta.ok(tarjeta.pe.elegir(tarjeta.f ? tarjeta.f.nombre : "", modelData.id)) }
                    ToolTip.visible: containsMouse; ToolTip.delay: 500; ToolTip.text: qsTr("Mostrar «%1» (desde la página 1)").arg(modelData.titulo)
                }
            }
        }
    }
}
