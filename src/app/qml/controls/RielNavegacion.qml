// La barra de navegación: un riel de iconos que se eleva y se abre en árbol.
//
// EL PROBLEMA. Diecinueve páginas en cuatro grupos, todas dibujadas como un
// icono de 42 px sin nombre. Quien las usa todos los días las tiene en la mano;
// quien no, mira una columna de símbolos y prueba. Los nombres estaban en un
// tooltip, o sea a un segundo de espera y de a uno por vez: para encontrar algo
// había que recorrer diecinueve esperas.
//
// LA FORMA. En reposo es exactamente lo que era —la misma columna de iconos, el
// mismo lugar, el mismo tamaño—, así que el que ya sabe dónde está cada cosa no
// pierde nada. Al pasarle el puntero por encima, la barra SE ELEVA del fondo
// (sombra, fondo más sólido, borde más claro) y SE ABRE mostrando los grupos
// con sus nombres.
//
// Abierta FLOTA sobre la página, no la corre: el lugar reservado en Main.qml
// sale del grosor cerrado. Una barra que empuja el contenido al pasarle el
// mouse por encima hace saltar todo lo que uno estaba mirando.
//
// EL ICONO NO SE MUEVE al abrir. Es lo que hace que se sienta UNA cosa que se
// ensancha y no dos widgets distintos: el icono queda en la misma x, y el
// nombre aparece a su derecha, en el lugar que antes no existía.
//
// LOS GRUPOS QUEDAN TODOS ABIERTOS. Se probó lo otro —cerrar los demás al ir
// recorriendo, para que nunca hubiera diecinueve renglones a la vez— y en la
// mano no funciona: cerrar un grupo sube de golpe todo lo que tiene debajo, así
// que el renglón al que ibas se corre justo cuando vas a hacer clic. Los
// títulos de grupo alcanzan para separar, que era lo que hacía falta.
//
// EN HORIZONTAL NO HAY ÁRBOL. Con la barra arriba o abajo lo que sobra es
// ancho y lo que falta es alto: un árbol ahí sería un menú colgando. En esas
// dos posiciones sigue siendo la tira de iconos de siempre.
import QtQuick
import QtQuick.Controls
import QtQuick.Effects
import QtQuick.Layouts
import BA

// SIN ANCHORS CONDICIONALES, que es la lección cara de este riel: poner
// `anchors.right: cond ? parent.right : undefined` parece que alterna, pero al
// volver de horizontal a vertical el ancla vieja no se suelta, el item queda
// atado a dos lados opuestos y se estira hasta ocupar la ventana entera (se
// vio: el botón del asistente convertido en una mancha naranja de pared a
// pared). Para cambiar anclas en vivo hay que usar estados con AnchorChanges;
// acá directamente no hacen falta: con x/y/ancho/alto calculados, cada posición
// es una cuenta y no hay estado que limpiar.
Item {
    id: riel

    /// La página que está a la vista (índice del StackLayout de la ventana).
    property int paginaActual: 0
    /// Alguien eligió una página.
    signal elegida(int pagina)
    /// Clic derecho: que se vaya a su propia ventana.
    signal sacada(int pagina, string titulo)

    readonly property var cfg: Studio.riel
    readonly property bool vert: cfg.vertical
    readonly property int margen: 14
    /// El catálogo (qué página, qué icono, en qué grupo) vive en C++ —ver
    /// ui/RielConfig.hpp— porque Configuración también lo necesita para ofrecer
    /// qué mostrar. Repetido en dos lados se desincroniza.
    readonly property var grupos: cfg.grupos

    /// Abierta solo en vertical, y solo si el operador lo dejó prendido.
    readonly property bool puedeAbrir: vert && cfg.expandir
    property bool abierta: false
    readonly property int anchoCerrado: cfg.grosor

    // --- Geometría -----------------------------------------------------------
    x: cfg.posicion === "derecha" ? parent.width - width - margen : margen
    y: cfg.posicion === "abajo" ? parent.height - height - margen : margen
    width: vert ? (abierta ? cfg.anchoAbierto : anchoCerrado) : parent.width - margen * 2
    height: vert ? parent.height - margen * 2 : cfg.grosor
    // Por encima de TODO lo que flota sobre las páginas (avisos, barra del
    // operador, píldoras de estado): es como se navega, y una barra tapada deja
    // al operador sin salida justo cuando necesita cambiar de pantalla. Lo
    // único que puede ponerse arriba es un diálogo modal, que es una
    // interrupción deliberada y se cierra.
    z: 100

    Behavior on width { NumberAnimation { duration: 170; easing.type: Easing.OutCubic } }

    // --- Abrir y cerrar ------------------------------------------------------
    // El cierre espera un momento: al bajar por la barra el puntero cruza
    // huecos de un pixel entre renglones, y cerrarse en cada uno la haría
    // parpadear. Abrir, en cambio, es inmediato.
    HoverHandler {
        id: hover
        onHoveredChanged: {
            if (hovered && riel.puedeAbrir) { cerrarTimer.stop(); riel.abierta = true }
            else if (!hovered) cerrarTimer.restart()
        }
    }
    Timer {
        id: cerrarTimer
        interval: 220
        onTriggered: riel.abierta = false
    }
    // Si se apaga la expansión (o se pasa a horizontal) con la barra abierta,
    // se cierra sola: si no, queda ancha para siempre y sin manera de cerrarla.
    onPuedeAbrirChanged: if (!puedeAbrir) abierta = false

    // --- La elevación --------------------------------------------------------
    // Qt 6.6 no tiene RectangularShadow: la sombra se dibuja con MultiEffect
    // sobre un rectángulo del mismo tamaño, como en GlassCard.
    Rectangle {
        id: sombraSrc
        anchors.fill: parent
        radius: fondo.radius
        color: "black"
        visible: false
    }
    MultiEffect {
        anchors.fill: sombraSrc
        source: sombraSrc
        autoPaddingEnabled: true
        shadowEnabled: true
        shadowBlur: riel.abierta ? 1.0 : 0.45
        shadowVerticalOffset: riel.abierta ? 14 : 4
        shadowColor: Qt.rgba(0, 0, 0, riel.abierta ? 0.55 : 0.30)
        opacity: 0.9
        Behavior on shadowBlur { NumberAnimation { duration: 170 } }
        Behavior on shadowVerticalOffset { NumberAnimation { duration: 170 } }
    }

    Rectangle {
        id: fondo
        anchors.fill: parent
        radius: riel.vert && riel.abierta ? Theme.radius : riel.cfg.grosor / 2
        // Abierta se despega del fondo: más sólida, para poder leer los nombres
        // sobre lo que sea que esté debajo.
        color: Qt.rgba(0.06, 0.06, 0.07, riel.abierta ? 0.94 : 0.72)
        border.color: riel.abierta ? Qt.rgba(1, 1, 1, 0.20) : Theme.glassBorder
        Behavior on color { ColorAnimation { duration: 170 } }
        Behavior on radius { NumberAnimation { duration: 170 } }
    }

    // El cepo: se come TODO clic que caiga en la barra y no haya agarrado nadie
    // más arriba.
    //
    // Un Rectangle tapa a la vista pero no al mouse, así que sin esto un clic
    // en el título de un grupo, en el hueco entre dos renglones o en el aire
    // que queda abajo atravesaba la barra y apretaba lo que hubiera en la
    // página de atrás. Abierta tapa un pedazo grande de pantalla, así que
    // apretabas un botón que ni estabas viendo.
    //
    // Va DEBAJO de todo lo demás: los botones de arriba agarran lo suyo
    // primero y acá cae solo lo que nadie quiso. No escucha el hover, para no
    // pisarle el abrir y cerrar a los handlers de arriba.
    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.AllButtons
        onWheel: (w) => w.accepted = true
    }

    // --- Contenido -----------------------------------------------------------
    GridLayout {
        anchors.fill: parent
        anchors.margins: 11
        flow: riel.vert ? GridLayout.TopToBottom : GridLayout.LeftToRight
        columns: riel.vert ? 1 : 4
        rows: riel.vert ? 4 : 1
        rowSpacing: 12
        columnSpacing: 12

        // 1) El logotipo. Abierta, se le suma el nombre del aparato.
        Item {
            Layout.preferredHeight: riel.cfg.tamano
            Layout.preferredWidth: riel.vert ? -1 : riel.cfg.tamano
            Layout.fillWidth: riel.vert
            Layout.alignment: riel.vert ? Qt.AlignLeft : Qt.AlignCenter
            Rectangle {
                id: logo
                width: riel.cfg.tamano
                height: riel.cfg.tamano
                anchors.verticalCenter: parent.verticalCenter
                // En vertical queda pegado a la izquierda, para que no se mueva
                // al abrir; en horizontal, centrado en su celda. Por `x` y no
                // por anclas condicionales: ver la nota de arriba del archivo.
                x: riel.vert ? 0 : Math.round((parent.width - width) / 2)
                radius: width / 2
                color: Theme.accent
                Text {
                    anchors.centerIn: parent
                    text: "BA"
                    color: "white"
                    font { pixelSize: Math.round(riel.cfg.tamano * 0.35); weight: Font.Black }
                }
            }
            Text {
                anchors { left: logo.right; leftMargin: 12; verticalCenter: parent.verticalCenter }
                visible: riel.abierta
                opacity: riel.abierta ? 1 : 0
                text: "Broadcast Appliance"
                color: Theme.text
                elide: Text.ElideRight
                width: parent.width - logo.width - 12
                font { family: Theme.font; pixelSize: 13; weight: Font.DemiBold }
                Behavior on opacity { NumberAnimation { duration: 140 } }
            }
        }

        // 2) Las páginas. Cerrada es la tira de iconos de siempre, que se
        // desplaza cuando no entra; abierta es el árbol.
        Flickable {
            id: tira
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            contentHeight: riel.vert ? filas.implicitHeight : height
            contentWidth: riel.vert ? width : filas.implicitWidth
            boundsBehavior: Flickable.StopAtBounds
            // Sin barra de desplazamiento: cerrada el riel es angosto y se
            // comería el lugar del icono. Los degradados ya dicen que hay más.
            interactive: riel.vert ? contentHeight > height : contentWidth > width

            GridLayout {
                id: filas
                flow: riel.vert ? GridLayout.TopToBottom : GridLayout.LeftToRight
                rowSpacing: riel.abierta ? 2 : 8
                columnSpacing: 8
                // El layout se mide solo, y la tira le fija ÚNICAMENTE el lado
                // cruzado. Escribirlo como `width: vert ? tira.width :
                // implicitWidth` arma un bucle de bindings (el ancho dependía de
                // su propio ancho implícito, que depende de cómo quedaron los
                // hijos, que dependen del ancho). Con `Binding on ... when` solo
                // hay una atadura activa por vez y al soltarla vuelve sola a
                // medirse.
                width: implicitWidth
                height: implicitHeight
                Binding on width { when: riel.vert; value: tira.width }
                Binding on height { when: !riel.vert; value: tira.height }

                Repeater {
                    model: riel.grupos
                    delegate: ColumnLayout {
                        id: grupo
                        required property var modelData
                        required property int index
                        Layout.fillWidth: riel.vert
                        Layout.fillHeight: !riel.vert
                        spacing: 0

                        // --- El encabezado del grupo --------------------------
                        // Cerrada es el separador fino de siempre; abierta es el
                        // nombre del grupo. No se aprieta: solo separa.
                        Item {
                            Layout.fillWidth: riel.vert
                            Layout.preferredWidth: riel.vert ? -1 : (visible ? 9 : 0)
                            Layout.preferredHeight: riel.abierta ? 30
                                                  : (riel.vert ? (visible ? 9 : 0) : riel.cfg.tamano)
                            visible: riel.abierta ? riel.vert
                                                  : (grupo.index !== 0 && riel.cfg.separadores)

                            // La rayita: solo cerrada.
                            Rectangle {
                                anchors.centerIn: parent
                                visible: !riel.abierta
                                width: riel.vert ? 22 : 1
                                height: riel.vert ? 1 : 22
                                color: Qt.rgba(1, 1, 1, 0.14)
                            }

                            Row {
                                anchors { left: parent.left; verticalCenter: parent.verticalCenter }
                                visible: riel.abierta
                                spacing: 6
                                Text {
                                    text: grupo.modelData.titulo
                                    color: Theme.textDim
                                    font { family: Theme.font; pixelSize: 11; weight: Font.DemiBold
                                           capitalization: Font.AllUppercase; letterSpacing: 0.6 }
                                    anchors.verticalCenter: parent.verticalCenter
                                }
                            }
                        }

                        // --- Las páginas del grupo ----------------------------
                        // Todas a la vista, siempre. Se probó cerrando las
                        // demás al recorrer —para que nunca hubiera diecinueve
                        // renglones juntos— y en la mano no funciona: cerrar un
                        // grupo sube de golpe todo lo que tiene debajo, así que
                        // el renglón al que ibas se corre justo cuando vas a
                        // hacer clic. Diecinueve renglones quietos se leen
                        // mejor que ocho que se mueven.
                        Item {
                            Layout.fillWidth: riel.vert
                            implicitWidth: items.implicitWidth
                            implicitHeight: items.implicitHeight

                            GridLayout {
                                id: items
                                width: parent.width
                                flow: riel.vert ? GridLayout.TopToBottom : GridLayout.LeftToRight
                                rowSpacing: riel.abierta ? 2 : 8
                                columnSpacing: 8

                                Repeater {
                                    model: grupo.modelData.items
                                    delegate: Item {
                                        id: celda
                                        required property var modelData
                                        readonly property bool actual: riel.paginaActual === celda.modelData.page
                                        // Cerrada, la celda mide lo mismo que el
                                        // botón y todas las de todos los grupos
                                        // miden igual, así que las columnas
                                        // quedan alineadas. Abierta ocupa el
                                        // ancho y el icono se queda donde estaba.
                                        Layout.fillWidth: riel.vert
                                        Layout.preferredWidth: riel.vert ? -1 : riel.cfg.tamano
                                        Layout.preferredHeight: riel.abierta ? Math.max(34, riel.cfg.tamano * 0.8)
                                                                             : riel.cfg.tamano
                                        Layout.alignment: riel.vert ? Qt.AlignLeft : Qt.AlignCenter

                                        // El realce del renglón entero, abierta.
                                        Rectangle {
                                            anchors.fill: parent
                                            anchors.leftMargin: -4
                                            anchors.rightMargin: -4
                                            visible: riel.abierta
                                            radius: Theme.radiusSmall
                                            color: celda.actual ? Qt.rgba(1, 1, 1, 0.13)
                                                 : (navMa.containsMouse ? Qt.rgba(1, 1, 1, 0.07) : "transparent")
                                            Behavior on color { ColorAnimation { duration: 140 } }
                                        }

                                        Rectangle {
                                            id: burbuja
                                            width: riel.cfg.tamano
                                            height: riel.cfg.tamano
                                            // El icono NO se mueve al abrir:
                                            // pegado a la izquierda en vertical,
                                            // centrado en horizontal.
                                            x: riel.vert ? 0 : Math.round((parent.width - width) / 2)
                                            anchors.verticalCenter: parent.verticalCenter
                                            scale: riel.abierta ? 0.78 : 1.0
                                            Behavior on scale { NumberAnimation { duration: 160; easing.type: Easing.OutCubic } }
                                            radius: width / 2
                                            color: celda.actual ? "white"
                                                 : (!riel.abierta && navMa.containsMouse ? Qt.rgba(1, 1, 1, 0.12)
                                                                                         : "transparent")
                                            border.color: riel.abierta && !celda.actual ? "transparent"
                                                                                        : Qt.rgba(1, 1, 1, 0.16)
                                            Behavior on color { ColorAnimation { duration: 150 } }
                                            // Candado: en vivo, esta página pide confirmar para entrar.
                                            Text {
                                                visible: Studio.modoVivo && Studio.paginaBloqueada(Studio.riel.idDePagina(celda.modelData.page))
                                                anchors { right: parent.right; top: parent.top; rightMargin: -2; topMargin: -2 }
                                                text: "🔒"
                                                font.pixelSize: Math.max(9, parent.width * 0.3)
                                                z: 2
                                            }
                                            Text {
                                                anchors.centerIn: parent
                                                text: celda.modelData.icon
                                                color: celda.actual ? "#111" : Theme.text
                                                // El icono acompaña al tamaño del
                                                // botón: si no, agrandar la barra
                                                // deja un icono chico en el medio.
                                                font.pixelSize: Math.round(riel.cfg.tamano * 0.43)
                                            }
                                        }

                                        Text {
                                            anchors { left: burbuja.right; leftMargin: 6
                                                      right: parent.right
                                                      verticalCenter: parent.verticalCenter }
                                            visible: riel.abierta && riel.vert
                                            opacity: riel.abierta ? 1 : 0
                                            text: celda.modelData.titulo
                                            color: celda.actual ? Theme.text : Theme.textDim
                                            elide: Text.ElideRight
                                            font { family: Theme.font; pixelSize: 13
                                                   weight: celda.actual ? Font.DemiBold : Font.Normal }
                                            Behavior on opacity { NumberAnimation { duration: 140 } }
                                        }

                                        MouseArea {
                                            id: navMa
                                            anchors.fill: parent
                                            hoverEnabled: true
                                            cursorShape: Qt.PointingHandCursor
                                            acceptedButtons: Qt.LeftButton | Qt.RightButton
                                            onClicked: (m) => {
                                                if (m.button === Qt.RightButton)
                                                    riel.sacada(celda.modelData.page, celda.modelData.titulo)
                                                else riel.elegida(celda.modelData.page)
                                            }
                                        }
                                        // Abierta el nombre ya está a la vista:
                                        // el tooltip solo repetiría, tapando el
                                        // renglón de al lado. Queda la aclaración
                                        // larga, que no entra en el renglón.
                                        ToolTip.visible: navMa.containsMouse
                                        ToolTip.text: riel.abierta
                                                      ? celda.modelData.tip
                                                      : celda.modelData.tip + "\n(clic derecho: sacar a su propia ventana)"
                                        ToolTip.delay: riel.abierta ? 900 : 400
                                    }
                                }
                            }
                        }
                    }
                }
            }

            // Los degradados que avisan que la tira sigue. Solo cuando hay algo
            // más para ese lado: si no, son manchas que no dicen nada.
            Rectangle {
                parent: tira
                x: 0; y: 0
                width: riel.vert ? tira.width : 18
                height: riel.vert ? 18 : tira.height
                visible: riel.vert ? tira.contentY > 2 : tira.contentX > 2
                gradient: Gradient {
                    orientation: riel.vert ? Gradient.Vertical : Gradient.Horizontal
                    GradientStop { position: 0; color: Qt.rgba(0.06, 0.06, 0.07, 0.95) }
                    GradientStop { position: 1; color: "transparent" }
                }
            }
            Rectangle {
                parent: tira
                x: riel.vert ? 0 : tira.width - width
                y: riel.vert ? tira.height - height : 0
                width: riel.vert ? tira.width : 18
                height: riel.vert ? 18 : tira.height
                visible: riel.vert ? tira.contentHeight - tira.contentY - tira.height > 2
                                   : tira.contentWidth - tira.contentX - tira.width > 2
                gradient: Gradient {
                    orientation: riel.vert ? Gradient.Vertical : Gradient.Horizontal
                    GradientStop { position: 0; color: "transparent" }
                    GradientStop { position: 1; color: Qt.rgba(0.06, 0.06, 0.07, 0.95) }
                }
            }
        }

        // 3) El asistente. Fijo, fuera de la tira: es el que se busca a las
        // apuradas y no puede moverse con el desplazamiento.
        Item {
            Layout.preferredHeight: riel.cfg.tamano
            Layout.preferredWidth: riel.vert ? -1 : riel.cfg.tamano
            Layout.fillWidth: riel.vert
            Layout.alignment: riel.vert ? Qt.AlignLeft : Qt.AlignCenter

            Rectangle {
                id: aiBtn
                width: riel.cfg.tamano
                height: riel.cfg.tamano
                x: riel.vert ? 0 : Math.round((parent.width - width) / 2)
                anchors.verticalCenter: parent.verticalCenter
                radius: width / 2
                gradient: Gradient {
                    GradientStop { position: 0; color: riel.paginaActual === 7 ? "#ffffff" : "#ff8a3d" }
                    GradientStop { position: 1; color: riel.paginaActual === 7 ? "#ffe2cf" : "#c2410c" }
                }
                border.color: Qt.rgba(1, 1, 1, 0.3)
                Text {
                    anchors.centerIn: parent
                    text: "✦"
                    color: riel.paginaActual === 7 ? "#c2410c" : "white"
                    font.pixelSize: Math.round(riel.cfg.tamano * 0.48)
                }
                Rectangle { // corriendo
                    visible: Studio.agent.running
                    width: 10; height: 10; radius: 5; color: Theme.ok; border.color: "#111"
                    anchors { right: parent.right; top: parent.top }
                }
                MouseArea {
                    id: aiMa
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    acceptedButtons: Qt.LeftButton | Qt.RightButton
                    // Clic derecho: a su propia ventana, como los botones de la tira.
                    onClicked: (m) => m.button === Qt.RightButton ? riel.sacada(7, "Asistente") : riel.elegida(7)
                }
                ToolTip.visible: aiMa.containsMouse && !riel.abierta
                ToolTip.text: "Asistente (chat con la IA que maneja la app)\n(clic derecho: sacar a su propia ventana)"
                ToolTip.delay: 400
            }
            Text {
                anchors { left: aiBtn.right; leftMargin: 12; right: parent.right
                          verticalCenter: parent.verticalCenter }
                visible: riel.abierta && riel.vert
                opacity: riel.abierta ? 1 : 0
                text: Studio.agent.running ? "Asistente · corriendo" : "Asistente"
                color: Theme.text
                elide: Text.ElideRight
                font { family: Theme.font; pixelSize: 13; weight: Font.DemiBold }
                Behavior on opacity { NumberAnimation { duration: 140 } }
            }
        }

        // 4) Indicadores rápidos: al aire, grabando, sala. Abiertos dicen qué
        // son, que es justo lo que un punto de color no dice.
        Item {
            Layout.preferredHeight: riel.vert ? (riel.abierta ? luces.implicitHeight : luces.implicitHeight) : 10
            Layout.preferredWidth: riel.vert ? -1 : luces.implicitWidth
            Layout.fillWidth: riel.vert
            Layout.alignment: riel.vert ? Qt.AlignLeft : Qt.AlignCenter

            GridLayout {
                id: luces
                x: riel.vert ? 0 : Math.round((parent.width - width) / 2)
                anchors.verticalCenter: parent.verticalCenter
                width: riel.vert ? parent.width : implicitWidth
                columns: riel.vert ? 1 : 3
                rowSpacing: riel.abierta ? 6 : 10
                columnSpacing: 10

                Repeater {
                    model: [
                        { on: Studio.streaming, c: Theme.live,   si: "Al aire",       no: "Fuera del aire" },
                        { on: Studio.recording, c: Theme.accent, si: "Grabando",      no: "Sin grabar" },
                        { on: Studio.gatewayAlive, c: Theme.ok,  si: "Sala en línea", no: "Sala sin conexión" }
                    ]
                    delegate: Item {
                        id: luz
                        required property var modelData
                        Layout.preferredWidth: riel.abierta && riel.vert ? -1 : 10
                        Layout.fillWidth: riel.abierta && riel.vert
                        Layout.preferredHeight: 10
                        Layout.alignment: riel.vert ? Qt.AlignLeft : Qt.AlignCenter

                        Rectangle {
                            id: punto
                            width: 10; height: 10; radius: 5
                            anchors.verticalCenter: parent.verticalCenter
                            // Cerrada, el punto se corre hasta el medio del
                            // riel; abierta se alinea con los iconos, que es
                            // donde empieza la columna. Una cuenta, no anclas
                            // que se alternan.
                            x: !riel.vert ? Math.round((parent.width - width) / 2)
                             : (riel.abierta ? 0 : Math.round((riel.cfg.tamano - width) / 2))
                            color: luz.modelData.on ? luz.modelData.c : Theme.textFaint
                            Behavior on x { NumberAnimation { duration: 170 } }
                        }
                        Text {
                            anchors { left: punto.right; leftMargin: 10; right: parent.right
                                      verticalCenter: parent.verticalCenter }
                            visible: riel.abierta && riel.vert
                            opacity: riel.abierta ? 1 : 0
                            text: luz.modelData.on ? luz.modelData.si : luz.modelData.no
                            color: luz.modelData.on ? Theme.text : Theme.textFaint
                            elide: Text.ElideRight
                            font { family: Theme.font; pixelSize: 11 }
                            Behavior on opacity { NumberAnimation { duration: 140 } }
                        }
                        ToolTip.visible: puntoMa.containsMouse && !riel.abierta
                        ToolTip.text: luz.modelData.on ? luz.modelData.si : luz.modelData.no
                        MouseArea {
                            id: puntoMa
                            anchors.fill: parent
                            anchors.margins: -6
                            hoverEnabled: true
                        }
                    }
                }
            }
        }
    }
}
