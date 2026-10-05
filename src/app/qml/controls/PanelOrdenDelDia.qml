// El orden del día de la sesión: cargar los puntos (pegando el texto o un
// CSV), marcar cuál se trata, el resultado de cada uno y el uso de la palabra.
// El punto en tratamiento queda para los zócalos ({punto.texto}), los
// capítulos de la grabación y el acta. Fuente: Studio.ordenDelDia.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import BA

ColumnLayout {
    id: od
    spacing: 8
    readonly property var o: Studio.ordenDelDia

    RowLayout {
        Layout.fillWidth: true
        spacing: 8
        Text {
            Layout.fillWidth: true
            elide: Text.ElideRight
            text: od.o.actual && od.o.actual.n ? "En tratamiento: Punto " + od.o.actual.n + " — " + od.o.actual.titulo
                                               : (od.o.puntos.length > 0 ? "Ningún punto en tratamiento" : "Sin orden del día")
            color: od.o.actual && od.o.actual.n ? Theme.text : Theme.textDim
            font.pixelSize: 14
            font.weight: Font.DemiBold
        }
        GlassButton {
            compact: true; text: "Siguiente punto ▶"; active: true
            enabled: od.o.puntos.some(p => p.estado === "pendiente")
            onClicked: { const e = od.o.siguienteUi(); if (e !== "") Studio.notify(e, "error") }
        }
        GlassButton { compact: true; text: "Cargar…"; onClicked: carga.open() }
        GlassButton {
            compact: true; text: "Capítulos"
            onClicked: {
                const t = Studio.capitulosPuntosUi()
                Studio.notify(t !== "" ? "Capítulos por punto: " + t.replace(/\n/g, " · ") : "Hace falta estar grabando y haber tratado puntos.", "info")
            }
        }
    }

    ListView {
        Layout.fillWidth: true
        Layout.fillHeight: true
        clip: true
        spacing: 4
        model: od.o.puntos
        delegate: Rectangle {
            required property var modelData
            width: ListView.view.width
            height: 46
            radius: 8
            color: modelData.estado === "en_tratamiento" ? Qt.rgba(0.78, 0.16, 0.16, 0.22) : Qt.rgba(1, 1, 1, 0.04)
            border.color: modelData.estado === "en_tratamiento" ? Theme.live : Theme.glassBorder
            RowLayout {
                anchors.fill: parent
                anchors.margins: 8
                spacing: 8
                Text { text: modelData.n + "."; color: Theme.text; font.pixelSize: 14; font.weight: Font.DemiBold }
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 0
                    Text { Layout.fillWidth: true; elide: Text.ElideRight; text: modelData.titulo; color: Theme.text; font.pixelSize: 13 }
                    Text {
                        Layout.fillWidth: true; elide: Text.ElideRight
                        text: (modelData.expediente ? "Expte. " + modelData.expediente + " · " : "") +
                              ({ pendiente: "pendiente", en_tratamiento: "en tratamiento", votado: "votado",
                                 comision: "a comisión", retirado: "retirado" })[modelData.estado] +
                              (modelData.resultado ? " · " + modelData.resultado : "")
                        color: Theme.textFaint; font.pixelSize: 11
                    }
                }
                GlassButton {
                    compact: true; text: "Tratar"
                    visible: modelData.estado !== "en_tratamiento"
                    onClicked: od.o.puntoUi(modelData.n, "en_tratamiento", "")
                }
                ComboBox {
                    implicitWidth: 130
                    model: [ "pendiente", "votado", "comision", "retirado" ]
                    displayText: "Estado…"
                    onActivated: (i) => od.o.puntoUi(modelData.n, model[i], "")
                }
                TextField {
                    Layout.preferredWidth: 170
                    placeholderText: "Resultado"
                    text: modelData.resultado || ""
                    color: Theme.text
                    onEditingFinished: if (text !== (modelData.resultado || "")) od.o.puntoUi(modelData.n, modelData.estado, text)
                }
            }
        }
    }

    Popup {
        id: carga
        modal: true
        width: 560
        height: 420
        padding: 14
        anchors.centerIn: Overlay.overlay
        background: Rectangle { color: Theme.bg; border.color: Theme.glassBorder; radius: 12 }
        contentItem: ColumnLayout {
            spacing: 8
            Text {
                text: "Pegá el orden del día"
                color: Theme.text
                font.pixelSize: 15
                font.weight: Font.DemiBold
            }
            Text {
                Layout.fillWidth: true; wrapMode: Text.WordWrap
                text: "Una línea por punto: «1. Expte. 123/24 – Título», «2) Título», o CSV «n;expediente;título». Reemplaza los puntos (el uso de la palabra sigue)."
                color: Theme.textDim; font.pixelSize: 11
            }
            ScrollView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                TextArea { id: textoOd; color: Theme.text; wrapMode: TextEdit.NoWrap; placeholderText: "1. Expte. 456/26 – Ley de ..." }
            }
            RowLayout {
                GlassButton {
                    compact: true; text: "Cargar"; active: true
                    onClicked: {
                        const e = od.o.cargarUi(textoOd.text)
                        if (e !== "") Studio.notify(e, "error"); else { carga.close(); textoOd.text = "" }
                    }
                }
                GlassButton {
                    compact: true; text: "Sesión nueva"
                    onClicked: ApplicationWindow.window.confirm({
                        title: "¿Empezar una sesión nueva?", text: "Se borran los puntos y los tiempos de uso de la palabra.",
                        accept: "Borrar", reject: "Cancelar", action: () => { od.o.limpiarUi(); carga.close() }
                    })
                }
                Item { Layout.fillWidth: true }
                GlassButton { compact: true; text: "Cerrar"; onClicked: carga.close() }
            }
        }
    }
}
