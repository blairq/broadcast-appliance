// Los ítems de un Menu que salen de un modelo. Un Repeater adentro de un Menu no
// anda (Qt pide un Instantiator): cuando el modelo cambiaba, los ítems nuevos podían
// quedar en otro lugar del menú y Qt avisaba «QQuickItem::stackBefore: Cannot stack».
// Este los inserta en el menú, a continuación de `despuesDe` (o al principio).
import QtQuick
import QtQuick.Controls

Instantiator {
    id: ins
    required property Menu menu
    /// El ítem fijo del menú después del cual van (null: al principio).
    property Item despuesDe: null

    function lugar(i) {
        if (!ins.despuesDe) return i
        for (let k = 0; k < ins.menu.count; ++k)
            if (ins.menu.itemAt(k) === ins.despuesDe) return k + 1 + i
        return ins.menu.count
    }
    onObjectAdded: (i, o) => ins.menu.insertItem(ins.lugar(i), o)
    onObjectRemoved: (i, o) => ins.menu.removeItem(o)
}
