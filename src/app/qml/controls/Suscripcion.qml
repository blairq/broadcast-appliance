// Suscripcion.qml — el contrato único para que una vista use un modelo que trabaja
// solo mientras alguien lo mira (consulta al gateway, mide, pide una vista al motor).
//
//   Suscripcion { objetivo: Studio.publico; activa: panel.visible }
//   Suscripcion { objetivo: Studio.frames; que: "entrada:" + cam; activa: visible && cam !== "" }
//
// Llama a `objetivo.usar(esta, si[, que])` (comun/UsoPorVistas en C++): la cuenta es
// POR VISTA, así que cerrar una no le apaga el trabajo a otra que mira lo mismo, y al
// destruirse se suelta sola. Cambiar `objetivo` o `que` suelta lo anterior y toma lo nuevo.
import QtQuick

QtObject {
    id: s
    property var objetivo: null
    /// Si la vista lo está usando ahora (lo normal: si se ve).
    property bool activa: true
    /// Qué parte del modelo ("" = el modelo entero; una clave de video, «multipista»…).
    property string que: ""

    property var _objetivo: null
    property string _que: ""
    function _usar(o, q, si) {
        if (q === "") o.usar(s, si)
        else o.usar(s, si, q)
    }
    function _aplicar() {
        const o = s.activa ? s.objetivo : null
        if (o === s._objetivo && s.que === s._que) return
        if (s._objetivo) s._usar(s._objetivo, s._que, false)
        s._objetivo = o
        s._que = s.que
        if (o) s._usar(o, s._que, true)
    }
    onObjetivoChanged: _aplicar()
    onActivaChanged: _aplicar()
    onQueChanged: _aplicar()
    Component.onCompleted: _aplicar()
    Component.onDestruction: if (s._objetivo) s._usar(s._objetivo, s._que, false)
}
