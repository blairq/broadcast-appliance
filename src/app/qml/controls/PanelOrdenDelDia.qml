// El orden del día de la sesión: cargar los puntos (pegando el texto o un
// CSV), marcar cuál se trata, el resultado de cada uno y el uso de la palabra.
// El punto en tratamiento queda para los zócalos ({punto.texto}), los
// capítulos de la grabación y el acta. Fuente: Studio.ordenDelDia. Los documentos
// relacionados con cada punto (Studio.documentos) se abren desde su fila.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import BA

ColumnLayout {
    id: od
    spacing: 8
    readonly property var o: Studio.ordenDelDia
    readonly property var docs: Studio.documentos ? Studio.documentos.lista : []
    function docsDe(n) { return od.docs.filter(d => (d.puntos || []).indexOf(n) >= 0) }

    RowLayout {
        Layout.fillWidth: true
        spacing: 8
        Text {
            Layout.fillWidth: true
            elide: Text.ElideRight
            opacity: od.o.puntos.length > 0 ? 1 : 0
            text: od.o.actual && od.o.actual.n ? qsTr("En tratamiento: Punto %1 — %2").arg(od.o.actual.n).arg(od.o.actual.titulo)
                                               : (od.o.puntos.length > 0 ? qsTr("Ningún punto en tratamiento") : qsTr("Sin orden del día"))
            color: od.o.actual && od.o.actual.n ? Theme.text : Theme.textDim
            font.pixelSize: 14
            font.weight: Font.DemiBold
        }
        GlassButton {
            compact: true; text: qsTr("Siguiente punto ▶"); active: true
            visible: od.o.puntos.length > 0
            enabled: od.o.puntos.some(p => p.estado === "pendiente")
            onClicked: { const e = od.o.siguienteUi(); if (e !== "") Studio.notify(e, "error") }
        }
        Text {
            visible: od.o.puntos.length > 0
            color: Theme.textFaint; font.pixelSize: 12
            text: qsTr("%1 de %2 tratados").arg(od.o.puntos.filter(p => p.estado !== "pendiente" && p.estado !== "en_tratamiento").length).arg(od.o.puntos.length)
        }
        GlassButton { compact: true; text: qsTr("Cargar otro…"); visible: od.o.puntos.length > 0; onClicked: carga.open() }
        GlassButton {
            compact: true; text: qsTr("Capítulos"); visible: od.o.puntos.length > 0
            onClicked: {
                const t = Studio.capitulosPuntosUi()
                Studio.notify(t !== "" ? qsTr("Capítulos por punto: %1").arg(t.replace(/\n/g, " · ")) : qsTr("Hace falta estar grabando y haber tratado puntos."), "info")
            }
        }
    }

    ListView {
        Layout.fillWidth: true
        Layout.fillHeight: true
        clip: true
        spacing: 4
        model: od.o.puntos
        EstadoVacio {
            anchors.centerIn: parent
            width: parent.width
            visible: parent.count === 0
            icono: "☰"
            titulo: qsTr("Todavía no hay orden del día")
            texto: qsTr("Pegá los puntos de la sesión (uno por línea, o un CSV). El punto en tratamiento sale en los zócalos ({punto.texto}), "
                        + "arma los capítulos de la grabación y queda en el acta junto con su resultado.")
            accion: qsTr("Cargar el orden del día…")
            onActivado: carga.open()
        }
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
                        readonly property string estado: ({ pendiente: qsTr("pendiente"), en_tratamiento: qsTr("en tratamiento"), votado: qsTr("votado"),
                                                           comision: qsTr("a comisión"), retirado: qsTr("retirado") })[modelData.estado]
                        text: (modelData.expediente ? qsTr("Expte. %1 · %2").arg(modelData.expediente).arg(estado) : estado) +
                              (modelData.resultado ? " · " + modelData.resultado : "")
                        color: Theme.textFaint; font.pixelSize: 11
                    }
                }
                GlassButton {
                    id: btnDocs
                    readonly property var lista: od.docsDe(modelData.n)
                    compact: true
                    visible: lista.length > 0
                    text: "📄 " + lista.length
                    ToolTip.visible: hovered; ToolTip.delay: 400
                    ToolTip.text: qsTr("Documentos de este punto:\n%1").arg(lista.map(d => d.titulo).join("\n"))
                    onClicked: menuDocs.popup(btnDocs, 0, btnDocs.height)
                    Menu {
                        id: menuDocs
                        ItemsDeMenu {
                            menu: menuDocs
                            model: btnDocs.lista
                            delegate: MenuItem {
                                required property var modelData
                                text: modelData.titulo
                                onTriggered: Studio.documentos.abrirUi(modelData.id)
                            }
                        }
                    }
                }
                GlassButton {
                    compact: true; text: qsTr("Tratar")
                    visible: modelData.estado !== "en_tratamiento"
                    onClicked: od.o.puntoUi(modelData.n, "en_tratamiento", "")
                }
                ComboBox {
                    implicitWidth: 130
                    model: [ { t: qsTr("Pendiente"), v: "pendiente" }, { t: qsTr("Votado"), v: "votado" }, { t: qsTr("A comisión"), v: "comision" }, { t: qsTr("Retirado"), v: "retirado" } ]
                    textRole: "t"
                    displayText: qsTr("Estado…")
                    onActivated: (i) => od.o.puntoUi(modelData.n, model[i].v, "")
                }
                TextField {
                    Layout.preferredWidth: 170
                    placeholderText: qsTr("Resultado")
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
                text: qsTr("Pegá el orden del día")
                color: Theme.text
                font.pixelSize: 15
                font.weight: Font.DemiBold
            }
            Text {
                Layout.fillWidth: true; wrapMode: Text.WordWrap
                text: qsTr("Una línea por punto: «1. Expte. 123/24 – Título», «2) Título», o CSV «n;expediente;título». Reemplaza los puntos (el uso de la palabra sigue).")
                color: Theme.textDim; font.pixelSize: 11
            }
            ScrollView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                TextArea { id: textoOd; color: Theme.text; wrapMode: TextEdit.NoWrap; placeholderText: qsTr("1. Expte. 456/26 – Ley de ...") }
            }
            RowLayout {
                GlassButton {
                    compact: true; text: qsTr("Cargar"); active: true
                    onClicked: {
                        const e = od.o.cargarUi(textoOd.text)
                        if (e !== "") Studio.notify(e, "error"); else { carga.close(); textoOd.text = "" }
                    }
                }
                GlassButton {
                    compact: true; text: qsTr("Sesión nueva")
                    onClicked: ApplicationWindow.window.confirm({
                        title: qsTr("¿Empezar una sesión nueva?"), text: qsTr("Se borran los puntos y los tiempos de uso de la palabra."),
                        accept: qsTr("Borrar"), reject: qsTr("Cancelar"), action: () => { od.o.limpiarUi(); carga.close() }
                    })
                }
                Item { Layout.fillWidth: true }
                GlassButton { compact: true; text: qsTr("Cerrar"); onClicked: carga.close() }
            }
        }
    }
}
