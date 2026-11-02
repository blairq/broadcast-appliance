// «En la conferencia» (ConferencePage): la gente que está adentro, en grilla o en
// lista (solo presentación; todo sale del modelo `conf.inside` y las acciones van
// a `conf`).
//
// Grilla: celdas de video que llenan el lugar con pocos y, con muchos, quedan a un
// tamaño en que se ve la cara y se lee el nombre, y se desliza (GrillaAdaptable).
// En cada celda, solo lo que avisa algo, como íconos chicos (micrófono o cámara
// apagados, mano, pantalla, retorno apagado, semáforo de su red). Los controles no
// ocupan la celda: al pasar el mouse aparecen «Hablarle» (se mantiene apretado) y
// «⋯»; tocar la celda abre el menú de acciones de esa persona.
// Lista: una fila por persona con miniatura, nombre, cargo, estado y acciones.
// Fuera de la parte que se ve, los videos no piden cuadros (VideoItem.enVista).
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import BA

Item {
    id: sala
    /// El modelo de la gente adentro (ConferenceViewModel::inside).
    property var modelo: null
    /// Quién atiende las acciones (Studio.conference).
    property var conf: null
    /// "grilla" o "lista": lo elige el selector de quien la usa.
    property string modo: "grilla"
    /// Cuántos hay (del modelo: no cambia al pasar de grilla a lista).
    readonly property int count: modelo ? modelo.count : 0
    /// La persona cuyo menú de acciones está abierto ("" ninguna).
    property string elegido: ""
    signal mensaje(string uid)
    signal datos(string uid, string name, string info, string contact)
    signal sacar(string uid, string name)

    function colorSemaforo(s) {
        return s === "rojo" ? Theme.live : s === "ambar" ? Theme.amber : s === "verde" ? Theme.ok : Theme.textFaint
    }
    // El menú de acciones de una persona (una celda o una fila): `p` es su delegado.
    function abrirAcciones(p, donde, x, y) {
        acciones.p = p
        sala.elegido = p.uid
        acciones.popup(donde, x, y)
    }

    // --- Íconos de estado (los mismos en la grilla y en la lista) ---------------------
    component Insignia: Rectangle {
        id: ins
        property string glifo: ""
        property color fondo: Qt.rgba(0, 0, 0, 0.6)
        property string ayuda: ""
        implicitWidth: 20; implicitHeight: 20; radius: 10
        color: fondo
        border.color: Qt.rgba(1, 1, 1, 0.25)
        Text { anchors.centerIn: parent; text: ins.glifo; color: "white"; font.pixelSize: 11 }
        HoverHandler { id: insHov }
        ToolTip.visible: insHov.hovered && ins.ayuda !== ""; ToolTip.delay: 300; ToolTip.text: ins.ayuda
    }
    component Estados: Row {
        id: est
        property var p: null
        spacing: 3
        Insignia { visible: est.p.prueba; glifo: "P"; fondo: Theme.amber; ayuda: qsTr("De la prueba de sala: fuera del padrón, la votación y el quórum") }
        Insignia { visible: est.p.speaker === "onair"; glifo: "●"; fondo: Theme.live; ayuda: qsTr("Orador al aire") }
        Insignia { visible: est.p.presenter; glifo: "★"; fondo: "#7c5cd6"; ayuda: qsTr("Presentador") }
        Insignia { visible: est.p.hand; glifo: "✋"; fondo: Theme.amber; ayuda: qsTr("Levantó la mano") }
        Insignia { visible: est.p.screen; glifo: "▭"; fondo: Theme.accent; ayuda: qsTr("Comparte pantalla") }
        Insignia { visible: est.p.talk; glifo: "🎙"; fondo: Theme.ok; ayuda: qsTr("Le estás hablando") }
        Insignia { visible: !est.p.mic; glifo: "🔇"; fondo: Theme.live; ayuda: qsTr("Micrófono apagado") }
        Insignia { visible: !est.p.cam; glifo: "⊘"; fondo: Theme.live; ayuda: qsTr("Cámara apagada") }
        Insignia { visible: est.p.sinRetorno; glifo: "🎧"; fondo: Theme.live
                   ayuda: qsTr("Retorno apagado: no escucha nada (su mezcla sin su voz no llega a la conferencia)") }
        Insignia { visible: !est.p.connected || !est.p.mediaUp; glifo: "!"; fondo: !est.p.connected ? Theme.live : Theme.amber
                   ayuda: !est.p.connected ? qsTr("Reconectando") : qsTr("Conectado, sin medios todavía") }
    }
    // El semáforo de su red (el color; las cifras, en Prueba técnica).
    component Semaforo: Rectangle {
        property string estado: ""
        visible: estado !== ""
        implicitWidth: 12; implicitHeight: 12; radius: 6
        color: sala.colorSemaforo(estado)
        border.color: "white"; border.width: 1
        HoverHandler { id: semHov }
        ToolTip.visible: semHov.hovered; ToolTip.delay: 300
        ToolTip.text: estado === "rojo" ? qsTr("Red: mala") : estado === "ambar" ? qsTr("Red: con problemas") : qsTr("Red: bien")
    }
    // «Hablarle»: se mantiene apretado (no puede ir en un menú).
    component BotonHablar: GlassButton {
        property var p: null
        compact: true
        text: p.talk ? qsTr("🎙 Hablando…") : qsTr("🎙 Hablarle")
        active: p.talk; activeColor: Theme.ok
        onPressed2: sala.conf.talk(p.uid, true)
        onReleased2: sala.conf.talk(p.uid, false)
        onCanceled2: sala.conf.talk(p.uid, false)
        ToolTip.visible: hovered && !pressed; ToolTip.delay: 400
        ToolTip.text: qsTr("Mantené apretado para hablarle (le llega tu voz por su retorno; no sale al aire)")
    }

    // --- Grilla ----------------------------------------------------------------------
    GrillaAdaptable {
        id: grilla
        anchors.fill: parent
        visible: sala.modo !== "lista"
        model: sala.modo !== "lista" ? sala.modelo : null
        celdaMinima: 160
        delegate: Item {
            id: celda
            required property string uid
            required property string name
            required property int slot
            required property bool connected
            required property bool mediaUp
            required property bool mic
            required property bool cam
            required property bool screen
            required property bool hand
            required property bool presenter
            required property string speaker
            required property string info
            required property string contact
            required property bool talk
            required property bool prueba
            required property string semaforo
            required property bool sinRetorno
            width: GridView.view.cellWidth
            height: GridView.view.cellHeight
            readonly property bool chica: width < 300
            readonly property bool mostrarControles: hov.hovered || sala.elegido === uid
            Rectangle {
                id: caja
                anchors { fill: parent; margins: grilla.margen }
                radius: celda.chica ? 10 : 14
                color: "#000000"
                clip: true
                border.color: sala.elegido === celda.uid ? Theme.accent : celda.presenter ? "#a78bfa" : celda.hand ? Theme.amber : Theme.glassBorder
                border.width: sala.elegido === celda.uid || celda.presenter || celda.hand ? 2 : 1
                // El video que B publica para ese lugar (lo mismo que ve el motor). Chica,
                // recortado para que se vea la cara; grande, entero.
                VideoItem {
                    anchors { fill: parent; margins: 1 }
                    visible: celda.slot > 0
                    source: "guest:" + celda.slot
                    fillMode: celda.chica ? VideoItem.Cover : VideoItem.Fit
                    opacity: celda.connected ? 1 : 0.4
                    enVista: grilla.enVista(celda.y, celda.height)
                }
                Text {
                    anchors.centerIn: parent
                    visible: celda.slot <= 0
                    width: parent.width - 16
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.WordWrap
                    text: qsTr("sin lugar en el programa")
                    color: Theme.textFaint; font.pixelSize: celda.chica ? 10 : 12
                }
                Rectangle {
                    anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
                    height: Math.min(parent.height * 0.6, 48)
                    gradient: Gradient {
                        GradientStop { position: 0; color: "transparent" }
                        GradientStop { position: 1; color: Qt.rgba(0, 0, 0, 0.85) }
                    }
                }
                // Nombre y lugar, abajo.
                RowLayout {
                    anchors { left: parent.left; right: parent.right; bottom: parent.bottom; margins: celda.chica ? 5 : 8 }
                    spacing: 5
                    Rectangle {
                        implicitWidth: celda.chica ? 18 : 22; implicitHeight: implicitWidth; radius: 6
                        color: celda.slot > 0 ? Theme.accent : Qt.rgba(1, 1, 1, 0.18)
                        Text { anchors.centerIn: parent; text: celda.slot > 0 ? celda.slot : "–"; color: celda.slot > 0 ? "#140a04" : "white"
                               font { pixelSize: celda.chica ? 10 : 12; weight: Font.Bold } }
                    }
                    Text {
                        Layout.fillWidth: true
                        text: celda.name
                        color: "white"; elide: Text.ElideRight
                        font { pixelSize: celda.chica ? 11 : 14; weight: Font.DemiBold }
                    }
                    Semaforo { estado: celda.semaforo }
                }
                // Lo que avisa algo, arriba a la izquierda.
                Estados {
                    anchors { left: parent.left; top: parent.top; margins: celda.chica ? 5 : 8 }
                    p: celda
                    visible: !celda.mostrarControles || !celda.chica
                }
                // Los controles, solo al pasar el mouse o con su menú abierto.
                Row {
                    anchors { right: parent.right; top: parent.top; margins: celda.chica ? 4 : 8 }
                    spacing: 4
                    visible: celda.mostrarControles
                    BotonHablar { p: celda }
                    GlassButton {
                        id: masBtn
                        compact: true; text: "⋯"
                        onClicked: sala.abrirAcciones(celda, masBtn, 0, masBtn.height + 4)
                        ToolTip.visible: hovered; ToolTip.delay: 400; ToolTip.text: qsTr("Acciones")
                    }
                }
                HoverHandler { id: hov }
                // Tocarla (o el botón derecho) abre sus acciones donde se tocó.
                TapHandler {
                    acceptedButtons: Qt.LeftButton | Qt.RightButton
                    onTapped: (ev) => sala.abrirAcciones(celda, caja, ev.position.x, ev.position.y)
                }
            }
        }
    }

    // --- Lista -----------------------------------------------------------------------
    ListView {
        id: lista
        anchors.fill: parent
        visible: sala.modo === "lista"
        model: sala.modo === "lista" ? sala.modelo : null
        clip: true
        spacing: 4
        cacheBuffer: 0
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
        delegate: Rectangle {
            id: fila
            required property string uid
            required property string name
            required property int slot
            required property bool connected
            required property bool mediaUp
            required property bool mic
            required property bool cam
            required property bool screen
            required property bool hand
            required property bool presenter
            required property string speaker
            required property string info
            required property string contact
            required property bool talk
            required property bool prueba
            required property string semaforo
            required property bool sinRetorno
            width: ListView.view.width - 10
            height: 52
            radius: 10
            color: sala.elegido === uid ? Qt.rgba(1, 0.42, 0.17, 0.12) : filaHov.hovered ? Qt.rgba(1, 1, 1, 0.07) : Qt.rgba(1, 1, 1, 0.03)
            border.color: sala.elegido === uid ? Theme.accent : presenter ? "#a78bfa" : hand ? Theme.amber : Theme.glassBorder
            HoverHandler { id: filaHov }
            TapHandler {
                acceptedButtons: Qt.RightButton
                onTapped: (ev) => sala.abrirAcciones(fila, fila, ev.position.x, ev.position.y)
            }
            RowLayout {
                anchors { fill: parent; leftMargin: 6; rightMargin: 8 }
                spacing: 8
                Rectangle {
                    implicitWidth: 72; implicitHeight: 40; radius: 6
                    color: "#000000"; clip: true
                    VideoItem {
                        anchors.fill: parent
                        visible: fila.slot > 0
                        source: "guest:" + fila.slot
                        fillMode: VideoItem.Cover
                        opacity: fila.connected ? 1 : 0.4
                        enVista: fila.y + fila.height > lista.contentY && fila.y < lista.contentY + lista.height
                    }
                }
                Rectangle {
                    implicitWidth: 22; implicitHeight: 22; radius: 6
                    color: fila.slot > 0 ? Theme.accent : Qt.rgba(1, 1, 1, 0.18)
                    Text { anchors.centerIn: parent; text: fila.slot > 0 ? fila.slot : "–"; color: fila.slot > 0 ? "#140a04" : "white"
                           font { pixelSize: 11; weight: Font.Bold } }
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 1
                    Text { Layout.fillWidth: true; text: fila.name; color: Theme.text; elide: Text.ElideRight
                           font { pixelSize: 13; weight: Font.DemiBold } }
                    Text { Layout.fillWidth: true; visible: text !== ""; text: fila.info; color: Theme.textDim; elide: Text.ElideRight; font.pixelSize: 11 }
                }
                Estados { p: fila }
                Semaforo { estado: fila.semaforo }
                BotonHablar { p: fila }
                GlassButton {
                    compact: true
                    // El mismo ancho con los dos textos: las filas quedan alineadas.
                    Layout.preferredWidth: 136
                    text: fila.mic ? qsTr("Silenciar") : qsTr("Activar micrófono")
                    onClicked: sala.conf.request(fila.uid, fila.mic ? "mute-mic" : "unmute-mic")
                }
                GlassButton {
                    id: filaMas
                    compact: true; text: "⋯"
                    onClicked: sala.abrirAcciones(fila, filaMas, 0, filaMas.height + 4)
                    ToolTip.visible: hovered; ToolTip.delay: 400; ToolTip.text: qsTr("Acciones")
                }
            }
        }
    }

    // --- Las acciones de una persona (un solo menú para todas) -------------------------
    Menu {
        id: acciones
        property var p: null
        readonly property bool hay: p !== null
        onClosed: sala.elegido = ""
        MenuItem { enabled: false; text: acciones.hay ? acciones.p.name : "" }
        MenuSeparator {}
        MenuItem { text: acciones.hay && acciones.p.mic ? qsTr("Silenciar") : qsTr("Activar micrófono")
                   onTriggered: sala.conf.request(acciones.p.uid, acciones.p.mic ? "mute-mic" : "unmute-mic") }
        MenuItem { text: acciones.hay && acciones.p.cam ? qsTr("Apagar cámara") : qsTr("Encender cámara")
                   onTriggered: sala.conf.request(acciones.p.uid, acciones.p.cam ? "mute-cam" : "unmute-cam") }
        MenuItem { text: qsTr("Prueba técnica")
                   onTriggered: { sala.conf.pruebaTecnica(acciones.p.uid); Studio.notify(qsTr("Probando a %1: un tono por su retorno durante 4 s…").arg(acciones.p.name), "info") } }
        MenuItem { visible: acciones.hay && acciones.p.screen; height: visible ? implicitHeight : 0
                   text: qsTr("Cortar pantalla"); onTriggered: sala.conf.request(acciones.p.uid, "stop-screen") }
        MenuItem { visible: acciones.hay && acciones.p.hand; height: visible ? implicitHeight : 0
                   text: qsTr("Bajar mano"); onTriggered: sala.conf.request(acciones.p.uid, "lower-hand") }
        MenuItem { visible: acciones.hay && !acciones.p.presenter && acciones.p.speaker !== "queued"; height: visible ? implicitHeight : 0
                   text: qsTr("→ Oradores"); onTriggered: sala.conf.speakerRequeue(acciones.p.uid) }
        MenuItem { text: acciones.hay && acciones.p.presenter ? qsTr("Quitar presentador") : qsTr("★ Presentador")
                   onTriggered: sala.conf.setPresenter(acciones.p.uid, !acciones.p.presenter) }
        MenuItem { text: qsTr("✉ Mensaje"); onTriggered: sala.mensaje(acciones.p.uid) }
        MenuItem { text: qsTr("✎ Datos"); onTriggered: sala.datos(acciones.p.uid, acciones.p.name, acciones.p.info, acciones.p.contact) }
        MenuSeparator {}
        MenuItem { text: qsTr("Sacar"); onTriggered: sala.sacar(acciones.p.uid, acciones.p.name) }
    }
}
