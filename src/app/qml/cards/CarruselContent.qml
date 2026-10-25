// Tarjeta «Carrusel»: el control, en vivo.
//
// EL CASO, otra vez: el orador dice «volvé a la del gráfico de barras». Con un
// botón de siguiente eso son veinte clics. Acá se ve la tira entera y se toca
// la que hay que poner.
//
// POR QUE EN PERSPECTIVA Y NO UNA GRILLA. No es decoración: en una grilla
// todas las tarjetas pesan lo mismo y hay que LEERLAS. Acá la del centro es la
// que está puesta —grande, derecha, al frente— y las de al lado se van
// acostando hacia el fondo. El ojo encuentra el centro sin buscarlo, y el
// movimiento al girar dice para qué lado se va sin ningún cartel.
//
// EL CUADRO DE BUSQUEDA filtra sobre el título y sobre lo que el que mira
// imágenes dijo de cada lámina. Con cincuenta láminas es la diferencia entre
// encontrarla y pasarlas de a una.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import BA
import "../controls"

Item {
    id: tarjeta

    readonly property var cc: Studio.carruseles
    /// Lo pone en true VentanaSuelta cuando esta tarjeta vive en su propia
    /// ventana. Es lo que decide si se muestra el testigo del aire.
    property bool enVentana: false
    /// La lámina del centro, que es la que Enter pone al aire.
    readonly property int centrada: tira.currentIndex

    // MIRAR NO ES PONER. Recorrer la tira —con las flechas, con la rueda, con
    // un clic— sólo mueve el centro; lo que SALE es doble clic, Enter o el
    // botón. Con la sala mirando, que buscar cambiara lo que se ve sería la
    // peor sorpresa posible, y es justo lo que uno hace mientras el orador
    // habla: ir adelantando para tener lista la que sigue.
    function poner() {
        if (tira.count === 0) return
        tarjeta.cc.ir(tarjeta.laminas[tira.currentIndex].indice)
    }

    // El foco se toma al tocar la tarjeta; de ahí en más el teclado es de
    // ella. Sin esto las flechas se las lleva la lista de escenas de al lado.
    focus: true
    Keys.onLeftPressed: tira.decrementCurrentIndex()
    Keys.onRightPressed: tira.incrementCurrentIndex()
    Keys.onReturnPressed: tarjeta.poner()
    Keys.onEnterPressed: tarjeta.poner()
    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.NoButton   // no roba clics: sólo el foco
        z: -1
        onPressed: (m) => { tarjeta.forceActiveFocus(); m.accepted = false }
    }
    readonly property var laminas: cc.laminas

    // SIGUE A LA ESCENA. Al cortar a otra escena, esta tarjeta pasa a manejar
    // el carrusel que ESA escena tenga; si no tiene ninguno, lo dice. El
    // operador no tiene que acordarse de elegirlo a mano justo cuando menos
    // tiempo tiene para acordarse de algo.
    //
    // Si igual elige otro en la lista de arriba, manda su elección hasta el
    // próximo corte: mirar otro carrusel mientras se prepara el bloque que
    // sigue es legítimo, y pisarle la elección a mitad sería peor.
    Connections {
        target: tarjeta.cc
        function onEnEscenaCambio() {
            if (tarjeta.cc.enEscena !== "") tarjeta.cc.actual = tarjeta.cc.enEscena
        }
    }
    Component.onCompleted: if (cc.enEscena !== "") cc.actual = cc.enEscena

    ColumnLayout {
        anchors.fill: parent
        spacing: 6

        // --- Qué carrusel, y buscar adentro ----------------------------------
        RowLayout {
            Layout.fillWidth: true
            spacing: 6

            ComboBox {
                id: cual
                Layout.preferredWidth: 150
                model: tarjeta.cc.carruseles
                textRole: "nombre"
                // Sigue al que este abierto, venga de donde venga: de la
                // pestaña Medios, del asistente o de esta misma tarjeta.
                currentIndex: {
                    const l = tarjeta.cc.carruseles
                    for (let i = 0; i < l.length; i++)
                        if (l[i].nombre === tarjeta.cc.actual) return i
                    return -1
                }
                onActivated: tarjeta.cc.actual = tarjeta.cc.carruseles[currentIndex].nombre
            }
            // Se dice si lo que se está manejando es lo que está en la escena
            // o algo que el operador eligió a mano: tocar una lámina de un
            // carrusel que NO está en el aire no cambia nada, y sin este aviso
            // eso parece que el aparato no responde.
            Rectangle {
                Layout.preferredWidth: 10
                Layout.preferredHeight: 10
                radius: 5
                color: tarjeta.cc.actual !== "" && tarjeta.cc.actual === tarjeta.cc.enEscena
                       ? Theme.accent : Qt.rgba(1, 1, 1, 0.18)
                ToolTip.visible: ratonPunto.containsMouse
                ToolTip.text: tarjeta.cc.actual === tarjeta.cc.enEscena
                              ? "Este carrusel está en la escena al aire"
                              : "Este carrusel NO está en la escena al aire: tocar una lámina no cambia nada"
                MouseArea { id: ratonPunto; anchors.fill: parent; anchors.margins: -4; hoverEnabled: true }
            }
            GlassButton {
                compact: true
                text: "⧉"
                // La misma tarjeta, en su propia ventana: en vivo el carrusel
                // se maneja con una mano mientras la principal tiene las
                // escenas. Ver VentanaSuelta.qml.
                visible: typeof ApplicationWindow !== "undefined" &&
                         ApplicationWindow.window && ApplicationWindow.window.sacarPanel !== undefined
                onClicked: ApplicationWindow.window.sacarPanel("carrusel", "Carrusel")
                ToolTip.visible: hovered
                ToolTip.delay: 400
                ToolTip.text: "Sacar el carrusel a su propia ventana"
            }
            TextField {
                id: busca
                Layout.fillWidth: true
                color: Theme.text
                placeholderText: "Buscar lámina…"
                onTextChanged: tarjeta.cc.consulta = text
                Keys.onEscapePressed: text = ""
            }
        }

        // --- La tira ----------------------------------------------------------
        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            PathView {
                id: tira
                anchors.fill: parent
                model: tarjeta.laminas
                // Tres a la vista y las demás plegadas: más que eso en una
                // tarjeta de dashboard son estampillas ilegibles.
                pathItemCount: 5
                preferredHighlightBegin: 0.5
                preferredHighlightEnd: 0.5
                highlightRangeMode: PathView.StrictlyEnforceRange
                clip: true
                // Sin rebote en los extremos: la tira da la vuelta, igual que
                // `siguiente` en el modelo.
                snapMode: PathView.SnapToItem

                path: Path {
                    startX: 0
                    startY: tira.height / 2
                    PathAttribute { name: "z"; value: 0 }
                    PathAttribute { name: "giro"; value: 62 }
                    PathAttribute { name: "escala"; value: 0.72 }
                    PathLine { x: tira.width * 0.38; y: tira.height / 2 }
                    PathAttribute { name: "z"; value: 1 }
                    PathAttribute { name: "giro"; value: 32 }
                    PathAttribute { name: "escala"; value: 0.84 }
                    PathLine { x: tira.width / 2; y: tira.height / 2 }
                    PathAttribute { name: "z"; value: 2 }
                    PathAttribute { name: "giro"; value: 0 }
                    PathAttribute { name: "escala"; value: 1.0 }
                    PathLine { x: tira.width * 0.62; y: tira.height / 2 }
                    PathAttribute { name: "z"; value: 1 }
                    PathAttribute { name: "giro"; value: -32 }
                    PathAttribute { name: "escala"; value: 0.84 }
                    PathLine { x: tira.width; y: tira.height / 2 }
                    PathAttribute { name: "z"; value: 0 }
                    PathAttribute { name: "giro"; value: -62 }
                    PathAttribute { name: "escala"; value: 0.72 }
                }

                delegate: Item {
                    id: hoja
                    required property var modelData
                    // `index` hay que PEDIRLO: con `required property` en el
                    // delegate, QML deja de inyectar los nombres sueltos y
                    // usarlo a secas es un ReferenceError en cada cuadro.
                    required property int index
                    // PathView entrega los atributos del camino como
                    // propiedades adjuntas; sólo existen mientras el ítem vive
                    // dentro de la vista, de ahí el `?? `.
                    readonly property real giro: PathView.giro ?? 0
                    readonly property real escala: PathView.escala ?? 1
                    // REDONDEADO. `z` sale interpolado del camino, asi que sin
                    // esto cambia en CADA cuadro; y cambiar el z reordena el
                    // grafo de escena, que es lo que rompe el batching del
                    // render. Con tres valores enteros el orden se recalcula
                    // dos veces por giro en vez de sesenta por segundo.
                    z: Math.round(PathView.z ?? 0)
                    // Apaisada, como las laminas. Antes era alta y angosta y
                    // cada lamina quedaba flotando en una franja negra.
                    //
                    // CON TECHO. Maximizada, cada tarjeta pasaba de 800 px y
                    // cinco tarjetas giradas a ese tamaño son mucho relleno de
                    // pixeles por cuadro: se arrastraba. Mas grande que esto no
                    // sirve para nada —ya se lee de sobra— y el que quiere
                    // verla en serio la pone al aire.
                    width: Math.min(tira.width * 0.42, tira.height * 1.4, 620)
                    height: Math.min(tira.height * 0.92, width * 0.66)
                    scale: escala

                    // CADA TARJETA SE DIBUJA UNA VEZ Y DESPUES SE MUEVE LA
                    // TEXTURA. Es la aceleracion que le faltaba: sin esto, en
                    // cada cuadro del giro se vuelven a dibujar la imagen, el
                    // borde redondeado y el rotulo de las cinco tarjetas. Con
                    // esto se dibujan cuando CAMBIAN, y girar es mover cinco
                    // cuadrilateros con textura, que es lo que la placa hace
                    // sin despeinarse.
                    layer.enabled: true
                    layer.smooth: true
                    // Tamaño FIJO de la textura: atado al de la tarjeta, cada
                    // cambio de tamaño de la ventana reasigna cinco buffers.
                    layer.textureSize: Qt.size(620, 410)

                    // SIN `Behavior` sobre el angulo. PathView ya interpola el
                    // giro mientras el item recorre el camino: poner una
                    // animacion encima arranca una animacion NUEVA en cada
                    // cuadro, y con la ventana maximizada eso se arrastra. Se
                    // noto enseguida y se saco.
                    transform: Rotation {
                        origin { x: hoja.width / 2; y: hoja.height / 2 }
                        axis { x: 0; y: 1; z: 0 }
                        angle: hoja.giro
                    }

                    Rectangle {
                        anchors.fill: parent
                        anchors.margins: 4
                        radius: 8
                        color: "#0b0e12"
                        // DOS MARCAS DISTINTAS, porque son dos cosas distintas:
                        // naranja = la que esta AL AIRE; borde claro = la del
                        // centro, la que pondria Enter. Con una sola no se sabe
                        // si lo que se esta mirando ya salio.
                        //
                        // Sale de `posicion` y NO del campo `actual` de la
                        // lista: esa lista solo se rearma cuando cambia la
                        // tira, asi que la marca se quedaba en la lamina
                        // anterior mientras el programa ya mostraba la nueva.
                        // Una marca que miente es peor que no tener marca.
                        readonly property bool puesta: hoja.modelData.indice === tarjeta.cc.posicion
                        border.width: puesta ? 3 : (hoja.index === tira.currentIndex ? 2 : 1)
                        border.color: puesta ? Theme.accent
                                    : (hoja.index === tira.currentIndex ? Qt.rgba(1, 1, 1, 0.65)
                                                                        : Theme.glassBorder)
                        // Sin `clip`: adentro de un item ROTADO obliga a
                        // stencil, y la imagen va PreserveAspectFit, no se sale.
                        antialiasing: false

                        Image {
                            anchors { fill: parent; margins: 3 }
                            source: hoja.modelData.url
                            fillMode: Image.PreserveAspectFit
                            asynchronous: true
                            sourceSize.width: 420
                        }

                        Rectangle {
                            anchors { left: parent.left; bottom: parent.bottom; margins: 5 }
                            width: rot.implicitWidth + 12
                            height: 20
                            radius: 10
                            color: Qt.rgba(0, 0, 0, 0.66)
                            Text {
                                id: rot
                                anchors.centerIn: parent
                                text: (hoja.modelData.indice + 1) + " · " + hoja.modelData.titulo
                                color: Theme.text
                                font.pixelSize: 10
                                elide: Text.ElideRight
                            }
                        }

                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            // Un toque la trae al centro y nada más. La pone el
                            // DOBLE clic, que es el gesto de «esta».
                            onClicked: {
                                tarjeta.forceActiveFocus()
                                tira.currentIndex = hoja.index
                            }
                            onDoubleClicked: {
                                tira.currentIndex = hoja.index
                                tarjeta.cc.ir(hoja.modelData.indice)
                            }
                        }
                    }
                }
            }

            Text {
                anchors.centerIn: parent
                visible: tira.count === 0
                width: parent.width - 40
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
                color: Theme.textFaint
                font.pixelSize: 12
                text: tarjeta.cc.actual === ""
                      ? (tarjeta.cc.enEscena === ""
                         ? "Esta escena no tiene carrusel.\n\nSe arman en Medios y se ponen en una\nescena desde la pestaña Escenas."
                         : "Elegí un carrusel arriba.")
                      : busca.text !== "" ? "Ninguna lámina coincide."
                                          : "Este carrusel está vacío."
            }
        }

        // --- Los botones de siempre -------------------------------------------
        RowLayout {
            Layout.fillWidth: true
            spacing: 6
            // El testigo del aire vive en esta esquina: se le deja el lugar en
            // vez de quedar tapando el boton de poner, que es el que mas se
            // usa de toda la tarjeta.
            Layout.rightMargin: testigo.visible ? testigo.width + 12 : 0
            GlassButton {
                compact: true; text: "◀"
                enabled: tira.count > 0
                onClicked: tira.decrementCurrentIndex()
            }
            GlassButton {
                compact: true; text: "▶"
                enabled: tira.count > 0
                onClicked: tira.incrementCurrentIndex()
            }
            Item { Layout.fillWidth: true }
            Text {
                visible: tira.count > 0
                text: (tira.currentIndex + 1) + " de " + tira.count
                color: Theme.textFaint
                font.pixelSize: 11
            }
            GlassButton {
                compact: true
                text: "Poner"
                active: true
                enabled: tira.count > 0
                onClicked: tarjeta.poner()
                ToolTip.visible: hovered
                ToolTip.text: "Pone la lámina del centro (Enter, o doble clic sobre ella).\n" +
                              "Recorrer la tira no cambia lo que se ve."
            }
        }
    }

    // --- El aire, de reojo ---------------------------------------------------
    //
    // En su propia ventana la tarjeta suele estar maximizada en el otro
    // monitor, y ahí el operador PIERDE DE VISTA lo que está saliendo: elige
    // láminas a ciegas. Este recuadro es lo mínimo para no tener que girar la
    // cabeza —o peor, poner una y darse cuenta después de que la escena al
    // aire era otra—.
    //
    // Sólo en ventana: adentro del dashboard, el monitor ya está al lado y
    // taparía media tarjeta para repetir lo mismo.
    Rectangle {
        id: testigo
        visible: tarjeta.enVentana
        anchors { right: parent.right; bottom: parent.bottom; margins: 14 }
        // Chico y 16:9. Mas grande le comeria lugar a las laminas, que son lo
        // que se vino a mirar aca.
        width: Math.min(320, Math.max(200, tarjeta.width * 0.2))
        height: Math.round(width * 9 / 16) + rotulo.height
        radius: 8
        color: "#000000"
        border.color: Theme.glassBorder
        z: 50

        VideoItem {
            anchors { left: parent.left; right: parent.right; top: parent.top }
            height: parent.height - rotulo.height
            source: "program"
            fillMode: VideoItem.Fit
        }
        Item {
            id: rotulo
            anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
            height: 20
            Rectangle {
                anchors.fill: parent
                color: Qt.rgba(0, 0, 0, 0.75)
                radius: 8
            }
            Row {
                anchors { left: parent.left; leftMargin: 8; verticalCenter: parent.verticalCenter }
                spacing: 6
                Rectangle {
                    width: 7; height: 7; radius: 4
                    anchors.verticalCenter: parent.verticalCenter
                    // Rojo solo cuando de verdad esta saliendo: en un aparato
                    // de television, un punto rojo que miente es peor que no
                    // tener punto.
                    color: Studio.streaming ? Theme.live : Theme.textFaint
                }
                Text {
                    text: (Studio.streaming ? "AL AIRE · " : "PROGRAMA · ") +
                          (Studio.currentScene || "—")
                    color: Theme.text
                    font { family: Theme.font; pixelSize: 10; weight: Font.DemiBold }
                }
            }
        }
    }

}
