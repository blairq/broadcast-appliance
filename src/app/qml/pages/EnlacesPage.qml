// Página Enlaces: todos los servicios web del aparato en un lugar.
//
// ¿Hace falta un enlace? Está acá: cada servicio con su LED de estado, el
// botón para prenderlo (si se puede), el enlace de la red local y el de afuera
// (con el túnel), para copiar, abrir o mostrar en QR. Los que son uno por
// persona (integrantes, televisores, invitaciones) llevan a su página.
//
// Todo sale de Studio.enlaces (EnlacesViewModel): la misma lista que el MCP
// (enlaces_listar / enlace_activar).
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import BA
import "../controls"

Rectangle {
    id: page
    color: Theme.bg
    property real leftInset: 92
    readonly property var en: Studio.enlaces

    Component.onCompleted: page.en.refrescar()

    function colorLed(led) {
        return led === "ok" ? Theme.ok : led === "aviso" ? Theme.warn : led === "error" ? Theme.live : Theme.textFaint
    }
    function grupos() {
        const out = []
        for (const s of page.en.servicios)
            if (out.indexOf(s.grupo) < 0) out.push(s.grupo)
        return out
    }
    function irA(id) { ApplicationWindow.window.showPage(Studio.riel.paginaDeId(id)) }
    function verQr(url, nombre, remoto) {
        Studio.mostrarQr(url, nombre, remoto ? "Desde cualquier lugar (por el túnel)." : "Desde la red local de la sala.",
                         remoto ? "azul" : "verde")
    }

    ColumnLayout {
        anchors { fill: parent; leftMargin: page.leftInset; margins: 20 }
        spacing: 12

        RowLayout {
            Layout.fillWidth: true
            spacing: 10
            Text { text: "Enlaces"; color: Theme.text; font { pixelSize: 20; weight: Font.DemiBold } }
            StatusPill {
                text: page.en.hayTunel ? "Con salida a internet" : "Solo red local"
                dot: page.en.hayTunel ? Theme.ok : Theme.textFaint
                on: page.en.hayTunel
            }
            Item { Layout.fillWidth: true }
            Text {
                text: "Para copiar, abrir o mostrar en QR. Verde: andando · ámbar: con un problema · rojo: no anda · gris: apagado."
                color: Theme.textFaint
                font.pixelSize: 11
                elide: Text.ElideRight
                Layout.maximumWidth: 560
            }
            GlassButton { compact: true; text: "Actualizar"; icon: "↻"; onClicked: page.en.refrescar() }
        }

        ScrollView {
            id: scroll
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            contentWidth: availableWidth

            ColumnLayout {
                width: scroll.availableWidth
                spacing: 16

                Repeater {
                    model: page.grupos()
                    ColumnLayout {
                        id: grupo
                        required property string modelData
                        Layout.fillWidth: true
                        spacing: 8
                        Text {
                            text: grupo.modelData.toUpperCase()
                            color: Theme.textDim
                            font { pixelSize: 11; weight: Font.DemiBold; letterSpacing: 1.2 }
                        }
                        Repeater {
                            model: page.en.servicios.filter(s => s.grupo === grupo.modelData)
                            delegate: Rectangle {
                                id: fila
                                required property var modelData
                                readonly property var s: modelData
                                Layout.fillWidth: true
                                implicitHeight: cuerpo.implicitHeight + 24
                                radius: 12
                                color: Theme.panel
                                border.color: Qt.rgba(1, 1, 1, 0.06)

                                ColumnLayout {
                                    id: cuerpo
                                    anchors { left: parent.left; right: parent.right; top: parent.top; margins: 12 }
                                    spacing: 8

                                    RowLayout {
                                        Layout.fillWidth: true
                                        spacing: 10
                                        // El LED: el estado de verdad del servicio.
                                        Rectangle {
                                            Layout.alignment: Qt.AlignTop
                                            Layout.topMargin: 4
                                            width: 12; height: 12; radius: 6
                                            color: page.colorLed(fila.s.led)
                                            border.color: Qt.rgba(0, 0, 0, 0.4)
                                            SequentialAnimation on opacity {
                                                running: fila.s.led === "aviso"
                                                loops: Animation.Infinite
                                                NumberAnimation { to: 0.35; duration: 600 }
                                                NumberAnimation { to: 1; duration: 600 }
                                                onStopped: parent.opacity = 1
                                            }
                                        }
                                        ColumnLayout {
                                            Layout.fillWidth: true
                                            spacing: 2
                                            Text { text: fila.s.nombre; color: Theme.text; font { pixelSize: 14; weight: Font.DemiBold } }
                                            Text {
                                                Layout.fillWidth: true
                                                text: fila.s.para
                                                color: Theme.textDim
                                                font.pixelSize: 12
                                                wrapMode: Text.WordWrap
                                            }
                                            Text {
                                                Layout.fillWidth: true
                                                text: fila.s.estado
                                                color: page.colorLed(fila.s.led)
                                                font.pixelSize: 12
                                                wrapMode: Text.WordWrap
                                            }
                                        }
                                        GlassButton {
                                            visible: fila.s.accion === "Abrir"
                                            compact: true; text: "Abrir"; icon: "↗"
                                            onClicked: page.en.accion(fila.s.id)
                                        }
                                        GlassButton {
                                            visible: fila.s.pagina !== "" && (fila.s.porPersona || fila.s.accion === "Emparejar")
                                            compact: true
                                            text: fila.s.accion === "Emparejar" ? "Emparejar" : "Ir a darlos"
                                            icon: "→"
                                            onClicked: page.irA(fila.s.pagina)
                                        }
                                        // Activar: solo lo que de verdad se prende y se apaga.
                                        RowLayout {
                                            visible: fila.s.activable
                                            spacing: 6
                                            Text {
                                                text: fila.s.activo ? "Activo" : "Apagado"
                                                color: fila.s.activo ? Theme.ok : Theme.textFaint
                                                font.pixelSize: 12
                                            }
                                            Toggle {
                                                checked: fila.s.activo
                                                onToggled: (quiere) => {
                                                    const e = page.en.activar(fila.s.id, quiere)
                                                    if (e !== "") Studio.notify(e, "error")
                                                }
                                            }
                                        }
                                    }

                                    // Los enlaces: el de la red local y el de afuera.
                                    // (Los de una persona por vez no tienen enlace acá: se dan en su página.)
                                    Repeater {
                                        model: fila.s.porPersona ? [] : [
                                            { remoto: false, url: fila.s.local || "",
                                              etiqueta: (fila.s.local || "").indexOf("://127.0.0.1") >= 0 ? "Esta máquina" : "Red local" },
                                            { remoto: true, etiqueta: "Desde afuera", url: fila.s.remoto || "" }]
                                        delegate: RowLayout {
                                            id: enl
                                            required property var modelData
                                            readonly property bool hay: enl.modelData.url !== ""
                                            // Sin enlace, solo se muestra la línea de afuera para decir que falta el
                                            // túnel (y solo en lo que la sala sirve hacia afuera).
                                            visible: hay || (enl.modelData.remoto && !page.en.hayTunel && (fila.s.local || "") !== ""
                                                             && ["botonera", "companion", "consola"].indexOf(fila.s.id) < 0)
                                            Layout.fillWidth: true
                                            Layout.leftMargin: 22
                                            spacing: 8
                                            Text {
                                                text: enl.modelData.etiqueta
                                                color: Theme.textFaint
                                                font.pixelSize: 11
                                                Layout.preferredWidth: 84
                                            }
                                            // Miniatura del QR (se agranda con el botón).
                                            Rectangle {
                                                visible: enl.hay
                                                width: 34; height: 34; radius: 4
                                                color: "white"
                                                Image {
                                                    anchors { fill: parent; margins: 2 }
                                                    source: enl.hay ? "image://qr/texto/" + encodeURIComponent(enl.modelData.url) : ""
                                                    sourceSize: Qt.size(68, 68)
                                                    smooth: false
                                                    cache: false
                                                }
                                                MouseArea {
                                                    anchors.fill: parent
                                                    cursorShape: Qt.PointingHandCursor
                                                    onClicked: page.verQr(enl.modelData.url, fila.s.nombre, enl.modelData.remoto)
                                                }
                                            }
                                            TextField {
                                                Layout.fillWidth: true
                                                readOnly: true
                                                selectByMouse: true
                                                text: enl.hay ? enl.modelData.url
                                                              : "Prendé la salida a internet (abajo, «Internet») para tener este enlace"
                                                color: enl.hay ? Theme.text : Theme.textFaint
                                                font { pixelSize: 12; family: Theme.mono }
                                            }
                                            GlassButton {
                                                visible: enl.hay; compact: true; text: "Copiar"
                                                onClicked: { Studio.copiar(enl.modelData.url); Studio.notify("Copiado: " + fila.s.nombre, "ok") }
                                            }
                                            GlassButton {
                                                visible: enl.hay; compact: true; text: "Abrir"
                                                onClicked: Qt.openUrlExternally(enl.modelData.url)
                                            }
                                            GlassButton {
                                                visible: enl.hay; compact: true; text: "QR"
                                                onClicked: page.verQr(enl.modelData.url, fila.s.nombre, enl.modelData.remoto)
                                            }
                                        }
                                    }
                                    Text {
                                        visible: (fila.s.nota || "") !== "" && fila.s.nota !== fila.s.estado
                                        Layout.fillWidth: true
                                        Layout.leftMargin: 22
                                        text: fila.s.nota || ""
                                        color: Theme.textFaint
                                        font.pixelSize: 11
                                        wrapMode: Text.WordWrap
                                    }
                                }
                            }
                        }
                    }
                }
                Item { Layout.preferredHeight: 8 }
            }
        }
    }
}
