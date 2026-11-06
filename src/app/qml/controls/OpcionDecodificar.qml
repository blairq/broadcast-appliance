// Cuánto del video que VUELVE decodifica el probador (hinchada --decodificar): «nada (solo
// envía)», «una muestra» (el %) o «todo». Lo usan la Prueba de sala y la Capacidad; la regla
// (y por qué la muestra es la de fábrica) está en sala/PruebaSalaReglas (Decodificar).
// Solo presentación: quien lo usa lee `modo` y `pct` al arrancar.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import BA

ColumnLayout {
    id: od
    /// "nada" | "muestra" | "todo"
    property string modo: "muestra"
    property int pct: 5
    property int pctMin: 1
    property int pctMax: 50
    spacing: 4

    RowLayout {
        Layout.fillWidth: true
        spacing: 6
        Text { text: qsTr("Decodificar lo que vuelve"); color: Theme.textDim; font.pixelSize: 12 }
        GlassButton { compact: true; text: qsTr("Nada (solo envía)"); active: od.modo === "nada"; onClicked: od.modo = "nada" }
        GlassButton { compact: true; text: qsTr("Una muestra"); active: od.modo === "muestra"; onClicked: od.modo = "muestra" }
        GlassButton { compact: true; text: qsTr("Todo"); active: od.modo === "todo"; onClicked: od.modo = "todo" }
        SpinBox {
            visible: od.modo === "muestra"
            from: od.pctMin; to: od.pctMax; value: od.pct; editable: true
            onValueModified: od.pct = value
            textFromValue: function(v) { return v + " %" }
            valueFromText: function(t) { return parseInt(t) || od.pct }
        }
    }
    Text {
        Layout.fillWidth: true
        wrapMode: Text.WordWrap
        color: Theme.textFaint
        font.pixelSize: 11
        text: od.modo === "nada"
              ? qsTr("Lo más liviano: el probador recibe el retorno y lo cuenta (cuadros, pérdida) pero no comprueba que se pueda ver.")
              : od.modo === "todo"
                ? qsTr("Cada participante de prueba decodifica lo suyo: con muchos, la máquina que genera se satura y manda a destiempo (la medición se ensucia).")
                : qsTr("De fábrica: decodifica el %1 % (al menos 3), lo justo para ver que el retorno llega entero y a su ritmo sin cargar la máquina que genera.").arg(od.pct)
    }
}
