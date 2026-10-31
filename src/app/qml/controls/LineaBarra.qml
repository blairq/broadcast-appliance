// La barra de abajo de la línea de tiempo (PLAN-LINEA-DE-TIEMPO 3.5 A, como la de
// Rundown Studio y Sofie): la hora, lo transcurrido, el over/under del evento, el
// próximo hard start con su cuenta y su gap u overlap, el presupuesto del bloque y
// «entra solo en…». Todo de Studio.linea.conduccion (el estado de la corrida).
import QtQuick
import QtQuick.Layouts
import BA

Rectangle {
    id: barra
    readonly property var c: Studio.linea ? Studio.linea.conduccion : ({})
    implicitHeight: 34
    radius: 6
    color: Qt.rgba(1, 1, 1, 0.05)

    component Dato: ColumnLayout {
        id: d
        property string titulo
        property string valor
        property color tinta: Theme.text
        spacing: 0
        Text { text: d.titulo; color: Theme.textFaint; font { pixelSize: 9; weight: Font.DemiBold; letterSpacing: 0.5 } }
        Text { text: d.valor; color: d.tinta; font { family: Theme.mono; pixelSize: 12; weight: Font.DemiBold } }
    }

    RowLayout {
        anchors { fill: parent; leftMargin: 10; rightMargin: 10 }
        spacing: 16
        Dato { titulo: qsTr("HORA"); valor: barra.c.hora || "—" }
        Dato {
            titulo: qsTr("TRANSCURRIDO")
            valor: barra.c.transcurridoSeg !== undefined ? Theme.mmss(barra.c.transcurridoSeg) : "—"
        }
        Dato {
            readonly property int ou: barra.c.overUnderSeg || 0
            titulo: qsTr("OVER/UNDER")
            valor: (ou > 0 ? "+" : "") + Theme.mmss(ou)
            tinta: ou > 30 ? Theme.live : ou < -30 ? Theme.ok : Theme.text
        }
        Dato {
            readonly property var h: barra.c.proximoHardStart
            visible: !!h
            titulo: h ? qsTr("HARD START · %1").arg(h.nombre || "") : ""
            valor: h ? qsTr("%1 en %2 · %3").arg(h.hora).arg(Theme.mmss(h.enSeg)).arg((h.gapSeg >= 0 ? "+" : "") + Theme.mmss(h.gapSeg)) : ""
            tinta: !h ? Theme.text : h.nivel === "overlap" ? Theme.live : h.nivel === "justo" ? Theme.amber : Theme.ok
        }
        Dato {
            readonly property var b: barra.c.bloque
            visible: !!b
            titulo: b ? qsTr("BLOQUE · %1").arg(b.nombre || "") : ""
            valor: b ? Theme.mmss(b.usadoSeg) + " / " + Theme.mmss(b.presupuestoSeg) : ""
            tinta: b && b.overUnderSeg > 0 ? Theme.live : Theme.text
        }
        Item { Layout.fillWidth: true }
        Text {
            readonly property int seg: barra.c.proximo ? (barra.c.proximo.entraSoloEnSeg || 0) : 0
            visible: barra.c.hold || seg > 0
            text: barra.c.hold ? qsTr("HOLD") : qsTr("entra solo en %1").arg(Theme.mmss(seg))
            color: barra.c.hold ? Theme.live : Theme.amber
            font { family: Theme.font; pixelSize: 13; weight: Font.Bold }
        }
    }
}
