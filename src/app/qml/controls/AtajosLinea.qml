// Los atajos de la línea de tiempo en la página (PLAN-LINEA-DE-TIEMPO 3.5 D): Espacio o
// F12 GO, H HOLD, F9/F10 el próximo, Mayús+F9 saltar, Ctrl+1…4 la salida, F1–F8 el
// estante. La lista sale de Studio.linea.teclas: si la persona le puso otra tecla a la
// acción en Botonera → Atajos (linea_go, linea_hold…), esa la atiende el atajo de la
// ventana y la de acá no aparece; si una de estas teclas ya es de otra acción, tampoco.
// Con el foco en un campo de texto la tecla se escribe: el campo se la queda.
import QtQuick
import BA

Instantiator {
    id: at
    property bool activos: true
    model: Studio.linea ? Studio.linea.teclas : []
    delegate: Shortcut {
        required property var modelData
        sequence: modelData.tecla
        enabled: at.activos
        onActivated: {
            const e = Studio.linea.atajo(modelData.accion)
            if (e !== "") Studio.notify(e, "error")
        }
    }
}
