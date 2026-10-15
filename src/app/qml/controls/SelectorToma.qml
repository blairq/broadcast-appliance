// La toma de una banca: qué cámara y qué posición de esa cámara la enfocan.
// Dos listas (de Studio.camaras: no se escribe a mano, así no queda un nombre que
// no existe). Al elegir la posición avisa elegido(camara, posicion); «sin toma»
// avisa elegido("", "").
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import BA

RowLayout {
    id: sel
    property string camara: ""
    property string posicion: ""
    property int anchoCamara: 130
    property int anchoPosicion: 120
    signal elegido(string camara, string posicion)
    spacing: 4

    readonly property var cc: Studio.camaras
    readonly property var camaras: cc ? cc.camaras.map(c => c.entrada) : []
    // Las posiciones de la cámara elegida (por nombre: es lo que se guarda y se dice).
    property var posiciones: []
    function leer() { sel.posiciones = sel.cc && sel.camara !== "" ? sel.cc.posiciones(sel.camara).map(p => p.nombre) : [] }
    onCamaraChanged: leer()
    Component.onCompleted: leer()
    Connections { target: sel.cc; function onCambio() { sel.leer() } }

    ComboBox {
        id: cam
        Layout.preferredWidth: sel.anchoCamara
        font.pixelSize: 11
        model: ["— sin toma —"].concat(sel.camaras)
        currentIndex: sel.camara === "" ? 0 : Math.max(0, sel.camaras.indexOf(sel.camara) + 1)
        displayText: sel.camara !== "" && sel.camaras.indexOf(sel.camara) < 0 ? sel.camara + " (no está)" : currentText
        onActivated: (i) => {
            if (i === 0) { sel.elegido("", ""); return }
            sel.camara = sel.camaras[i - 1]
            // La posición se elige después (con la cámara sola no hay toma).
            sel.leer()
            if (sel.posiciones.length === 1) sel.elegido(sel.camara, sel.posiciones[0])
        }
    }
    ComboBox {
        id: pos
        Layout.preferredWidth: sel.anchoPosicion
        font.pixelSize: 11
        enabled: sel.camara !== ""
        model: sel.posiciones
        currentIndex: sel.posiciones.indexOf(sel.posicion)
        displayText: sel.posicion !== "" && sel.posiciones.indexOf(sel.posicion) < 0
                     ? sel.posicion + " (no está)" : (currentIndex < 0 ? "elegí la posición" : currentText)
        onActivated: (i) => sel.elegido(sel.camara, sel.posiciones[i])
    }
}
