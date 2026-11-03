// La vista previa de una cosa del catálogo en el selector (SelectorCosas): una
// escena, un video, una multivista en vivo; una página o un panel achicados (se arman
// al tamaño de una pantalla y se dibujan en miniatura, sin tocarlos); una distribución
// como su esquema (EsquemaDistribucion); lo que solo va a una TV, su ícono y qué es.
//
// La vista en vivo se arma un momento DESPUÉS de elegir (bajar con las flechas por la
// lista no arma y desarma una página por fila) y en un Loader asíncrono: la interfaz no
// espera a nada. Los cuadros del video llegan por FrameService como en cualquier vista.
//
// `cosa` es la elegida tal como la da el modelo (BuscadorCatalogo.actual): si cambia
// (una escena renombrada) o desaparece, esto cambia solo.
import QtQuick
import BA

Rectangle {
    id: pv
    property var cosa: null
    /// La ventana principal (le da los componentes de páginas y paneles a VistaCatalogo).
    property var principal: null
    /// La vista en vivo se terminó de armar (quien la usa puede devolverle el foco a su búsqueda).
    signal armada()

    readonly property string itemId: pv.cosa && pv.cosa.itemId ? pv.cosa.itemId : ""
    readonly property string modo: pv.cosa && pv.cosa.dibujo && pv.cosa.dibujo.modo ? pv.cosa.dibujo.modo : ""
    readonly property bool enVivo: pv.modo === "video" || pv.modo === "multiview" || pv.modo === "panel" || pv.modo === "pagina"
    /// Lo que se arma en vivo: la elegida, después de la pausa.
    property string idEnVivo: ""

    color: "black"
    radius: 8
    clip: true

    Timer { id: pausa; interval: 280; onTriggered: pv.idEnVivo = pv.itemId }
    onItemIdChanged: { pv.idEnVivo = ""; pausa.restart() }

    // --- En vivo -------------------------------------------------------------------------
    Loader {
        id: vivo
        anchors { fill: parent; margins: 1 }
        active: pv.enVivo && pv.idEnVivo !== "" && pv.idEnVivo === pv.itemId
        asynchronous: true
        onLoaded: pv.armada()
        sourceComponent: FocusScope {
            id: marco
            // Una página o un panel no entran en un recuadro chico: se arman al tamaño de una
            // pantalla y se achican; un video se dibuja al tamaño que hay.
            readonly property bool achicar: pv.modo === "panel" || pv.modo === "pagina"
            readonly property real anchoBase: 1280
            // Sin foco propio: lo de adentro no le saca el teclado a la búsqueda.
            focus: false
            VistaCatalogo {
                width: marco.achicar ? marco.anchoBase : marco.width
                height: marco.achicar ? marco.anchoBase * marco.height / Math.max(1, marco.width) : marco.height
                scale: marco.achicar ? marco.width / marco.anchoBase : 1
                transformOrigin: Item.TopLeft
                itemId: pv.idEnVivo
                principal: pv.principal
                conMarco: false
                // Se mira, no se toca.
                enabled: false
            }
        }
    }

    // --- Una distribución: su esquema ---------------------------------------------------------
    EsquemaDistribucion {
        anchors { fill: parent; margins: 10 }
        visible: pv.modo === "distribucion"
        dibujo: pv.modo === "distribucion" ? pv.cosa.dibujo : null
    }

    // --- Sin vista: nada elegido, armándose, o solo para la TV ---------------------------------
    Column {
        anchors.centerIn: parent
        width: parent.width - 30
        spacing: 6
        visible: pv.modo !== "distribucion" && (vivo.status !== Loader.Ready)
        Text {
            width: parent.width; horizontalAlignment: Text.AlignHCenter
            text: pv.cosa ? (pv.cosa.icono || "•") : ""
            color: Theme.textDim; font.pixelSize: 42
        }
        Text {
            width: parent.width; horizontalAlignment: Text.AlignHCenter; wrapMode: Text.WordWrap
            text: !pv.cosa ? qsTr("Elegí algo de la lista para verlo acá.")
                 : pv.enVivo ? qsTr("Armando la vista previa…")
                 : pv.cosa.red ? qsTr("Solo para una TV de la red: se ve cuando se la manda.")
                 : qsTr("Sin vista previa.")
            color: Theme.textFaint; font.pixelSize: 12
        }
    }
}
