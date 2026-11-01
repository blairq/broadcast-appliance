// El joystick de pan/tilt de una cámara (la tarjeta PTZ, su ventana y la página
// Cámaras PTZ): mientras se tiene apretado, la cámara se mueve con esa velocidad; al
// soltar, para.
import QtQuick
import BA

Joystick {
    id: ptz
    /// La entrada que mueve y la velocidad máxima (0..1).
    property string entrada: ""
    property real velocidad: 0.6
    /// Lo que contestó el modelo al empezar a mover ("" si anduvo).
    signal aviso(string texto)

    readonly property var cc: Studio.camaras
    onApretado: (px, py) => ptz.aviso(ptz.cc.mover(ptz.entrada, px * ptz.velocidad, py * ptz.velocidad, 0))
    onMover: (px, py) => ptz.cc.mover(ptz.entrada, px * ptz.velocidad, py * ptz.velocidad, 0)
    onSoltado: ptz.cc.parar(ptz.entrada)
}
