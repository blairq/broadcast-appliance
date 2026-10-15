// La barra de tarjetas del Dashboard: un botón cuadrado por tipo de tarjeta.
//
//   pasar el mouse   se ilumina (y dice cuál es)
//   clic             la tarjeta en el tablero; si ya está (verde), la oculta y se
//                    acuerda dónde estaba para volver ahí
//   doble clic       la tarjeta en su propia ventana
//
// Los seis puntos rojos de arriba la despliegan: se lee el nombre de cada una y
// tiene dos botones, «Tarjeta» y «Ventana». Las que ya están en el tablero
// llevan un punto. La lista sale de Studio.dashboard.tiposDeTarjeta.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import BA

Rectangle {
    id: barra
    property bool desplegada: false
    readonly property int lado: 38
    readonly property int anchoCerrada: lado + 16
    width: desplegada ? 290 : anchoCerrada
    // Si no entran todas, se desplaza (Flickable) en vez de pasarse del borde de abajo.
    height: Math.min(columna.implicitHeight + 16 + lado + 6, parent ? parent.height - y - 12 : 9999)
    Behavior on width { NumberAnimation { duration: 160; easing.type: Easing.OutCubic } }
    radius: 14
    color: Qt.rgba(0.07, 0.07, 0.09, desplegada ? 0.94 : 0.72)
    border.color: Qt.rgba(1, 1, 1, 0.10)
    clip: true

    function abrirVentana(t) {
        const w = ApplicationWindow.window
        if (!w || w.sacarPanel === undefined) return
        // Las que tienen su panel propio (más completo que la tarjeta) van a ese.
        if (t.kind === "mixer") { w.sacarPanel("mezclador", "Mezclador"); return }
        if (t.kind === "carrusel") { w.sacarPanel("carrusel", "Carrusel"); return }
        if (t.kind === "ptz") {
            const cams = Studio.camaras ? Studio.camaras.camaras : []
            if (cams.length === 0) { Studio.notify("No hay cámaras con PTZ", "warn"); return }
            w.sacarPanel("ptz:" + cams[0].entrada, "PTZ — " + cams[0].entrada)
            return
        }
        w.sacarPanel("tarjeta:" + t.kind, t.titulo)
    }
    /// En el tablero (verde): se oculta; si no está, aparece donde estaba.
    function alternarTarjeta(t) { Studio.dashboard.alternarTarjeta(t.kind) }

    // Los seis puntos rojos: despliegan y repliegan. Fijos arriba: la lista se
    // desplaza debajo, ellos no.
    Rectangle {
        id: puntos
        x: 8; y: 8
        z: 2
        width: barra.lado
        height: barra.lado
        radius: 9
        color: puntosMa.containsMouse ? "#ff4d42" : Theme.live
        Grid {
            anchors.centerIn: parent
            columns: 2; rowSpacing: 4; columnSpacing: 6
            Repeater { model: 6; Rectangle { width: 5; height: 5; radius: 2.5; color: "white" } }
        }
        MouseArea {
            id: puntosMa
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: barra.desplegada = !barra.desplegada
        }
        ToolTip.visible: puntosMa.containsMouse && !barra.desplegada
        ToolTip.delay: 400
        ToolTip.text: "Ver los nombres y abrir como tarjeta o como ventana"
    }

    Flickable {
        anchors { fill: parent; margins: 8; topMargin: 8 + barra.lado + 6 }
        contentHeight: columna.implicitHeight
        clip: true
        boundsBehavior: Flickable.StopAtBounds

        ColumnLayout {
            id: columna
            width: parent.width
            spacing: 6

            Repeater {
                model: Studio.dashboard.tiposDeTarjeta
                delegate: RowLayout {
                    id: fila
                    required property var modelData
                    Layout.fillWidth: true
                    spacing: 8

                    Rectangle {
                        id: boton
                        Layout.preferredWidth: barra.lado
                        Layout.preferredHeight: barra.lado
                        radius: 9
                        readonly property bool encima: ma.containsMouse
                        // En el tablero: en verde (clic la oculta).
                        color: encima ? Qt.rgba(1, 0.42, 0.17, 0.32)
                                      : fila.modelData.abierta ? Qt.rgba(0.21, 0.77, 0.42, 0.20) : Qt.rgba(1, 1, 1, 0.07)
                        border.color: encima ? Theme.accent : fila.modelData.abierta ? Theme.ok : Qt.rgba(1, 1, 1, 0.12)
                        border.width: encima || fila.modelData.abierta ? 2 : 1
                        scale: encima ? 1.08 : 1
                        Behavior on scale { NumberAnimation { duration: 110 } }
                        Behavior on color { ColorAnimation { duration: 110 } }
                        // El brillo alrededor al pasar el mouse.
                        Rectangle {
                            anchors { fill: parent; margins: -4 }
                            radius: parent.radius + 4
                            color: "transparent"
                            border { color: Qt.rgba(1, 0.42, 0.17, 0.35); width: 3 }
                            visible: boton.encima
                        }
                        Text {
                            anchors.centerIn: parent
                            text: fila.modelData.icono
                            color: boton.encima ? "white" : Theme.text
                            font.pixelSize: 17
                        }
                        // Ya está en el tablero.
                        Rectangle {
                            visible: fila.modelData.abierta
                            width: 7; height: 7; radius: 3.5
                            color: Theme.ok
                            anchors { right: parent.right; bottom: parent.bottom; margins: 3 }
                        }
                        MouseArea {
                            id: ma
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            // El clic espera un instante: si llega el segundo, es doble (ventana).
                            onClicked: unClic.restart()
                            onDoubleClicked: { unClic.stop(); barra.abrirVentana(fila.modelData) }
                        }
                        Timer { id: unClic; interval: 240; onTriggered: barra.alternarTarjeta(fila.modelData) }
                        ToolTip.visible: ma.containsMouse && !barra.desplegada
                        ToolTip.delay: 250
                        ToolTip.text: fila.modelData.titulo + (fila.modelData.abierta ? " — clic: ocultarla" : " — clic: mostrarla") + " · doble clic: en ventana"
                    }
                    // Desplegada: el nombre y los dos botones.
                    Text {
                        visible: barra.desplegada
                        Layout.fillWidth: true
                        text: fila.modelData.titulo
                        elide: Text.ElideRight
                        color: Theme.text; font.pixelSize: 13
                    }
                    GlassButton {
                        visible: barra.desplegada
                        compact: true; text: fila.modelData.abierta ? "Ocultar" : "Tarjeta"
                        onClicked: barra.alternarTarjeta(fila.modelData)
                    }
                    GlassButton {
                        visible: barra.desplegada
                        compact: true; text: "Ventana"; icon: "⧉"
                        onClicked: barra.abrirVentana(fila.modelData)
                    }
                }
            }
        }
    }
}
