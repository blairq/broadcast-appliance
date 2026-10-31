// El recinto: las bancas como se ven desde la cabina, para tocar. Fuente:
// Studio.cuerpo.recinto (asamblea/Cuerpo), Studio.bancas (la fila de
// oradores presenciales), Studio.dcn (la central) y Studio.sesionRemota (los que
// entran desde el navegador, en la sala o afuera).
//
// TRES DISTRIBUCIONES (se elige arriba y queda guardada): la GRILLA de filas y
// columnas, el HEMICICLO (cada fila es un arco alrededor de la presidencia) y la
// LISTA. Los lugares se editan sobre la grilla o el hemiciclo (la lista, sobre la
// grilla). En el hemiciclo una banca también se arrastra a donde va de verdad
// (aparece una grilla y al soltarla se acomoda a ella); la de la presidencia va en
// la mesa: se elige y se toca «PRESIDENCIA».
//
// CADA BANCA muestra la cara de quien la ocupa: en vivo si está conectado por el
// navegador (la foto de su cámara), o su foto del Cuerpo si no. Encima, lo que
// pasa: micrófono abierto en rojo («● AIRE»), pidió la palabra en ámbar con su
// turno, «le toca» con el borde rojo titilando, presidente con borde dorado,
// conectado remoto («REMOTO»/«CEL») y, si espera que lo habiliten, borde ámbar:
// un toque abre el menú con «Habilitar».
//
// TOCAR UNA BANCA abre lo que se puede hacer con ella: apuntar la cámara, ponerla
// al aire, sumarla a la fila, abrir o cerrar su micrófono en la central,
// habilitarla o sacarla de la sesión remota. Con `tocarAbreMicrofono` (la página
// Conferencia) un toque abre o cierra el micrófono; el menú sigue en el toque largo.
import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import BA

ColumnLayout {
    id: rec
    spacing: 10
    readonly property var r: Studio.cuerpo.recinto
    readonly property var b: Studio.bancas.estado
    readonly property int filas: r.filas || 0
    readonly property int columnas: r.columnas || 0
    readonly property string distribucion: rec.editando && (r.distribucion || "grilla") === "lista" ? "grilla" : (r.distribucion || "grilla")
    readonly property var pres: r.presidencia || null
    readonly property bool hayAMano: (r.bancas || []).some(l => l.x !== undefined)
    property bool editando: false
    property bool tocarAbreMicrofono: false
    readonly property var d: Studio.dcn ? Studio.dcn.estado : ({})
    readonly property bool conCentral: !!(Studio.dcn && Studio.dcn.conectada)
    readonly property var sr: Studio.sesionRemota
    // id → participante de la sesión remota (conectado, estado, lugar…).
    readonly property var remotos: {
        const m = {}
        for (const p of (rec.sr ? rec.sr.participantes : [])) m[p.id] = p
        return m
    }
    property int fotosVersion: 0
    // Titileo de 1 Hz para «le toca» y los que esperan que los habiliten.
    property bool fase: false
    Timer { interval: 500; repeat: true; running: rec.visible && (rec.conCentral || rec.hayEsperando); onTriggered: rec.fase = !rec.fase }
    readonly property bool hayEsperando: (rec.sr ? rec.sr.participantes : []).some(p => p.estado === "esperando")
    // Las caras de los conectados, cada 3 s mientras se ve.
    Timer {
        interval: 3000; repeat: true; triggeredOnStart: true
        running: rec.visible && !!rec.sr && rec.sr.conectados > 0
        onTriggered: { for (const p of rec.sr.participantes) if (p.conectado) rec.sr.pedirFoto(p.id) }
    }
    Connections {
        target: rec.sr
        enabled: !!rec.sr
        function onFotoLista(id) { rec.fotosVersion++ }
    }

    function unidadDe(banca) {
        const l = rec.d.unidades || []
        for (let i = 0; i < l.length; ++i) if (l[i].banca === banca) return l[i]
        return null
    }
    function turnoCentral(banca) {
        const l = rec.d.pedidos || []
        for (let i = 0; i < l.length; ++i) if (l[i].banca === banca) return l[i].turno
        return 0
    }
    function microfono(banca, abrir, confirmar) {
        Studio.dcn.pedirUi("microfono", "microfono", {banca: banca, abierto: abrir, confirmar: !!confirmar})
    }
    /// Sin integrantes cargados: la página abre el panel del cuerpo.
    signal pedirCuerpo()
    property string elegida: ""
    property string aviso: ""
    /// Nadie cargado: en vez de una grilla vacía, qué hacer.
    readonly property bool sinNadie: !rec.editando && (Studio.cuerpo.integrantes || []).length === 0

    function lugarDe(banca) {
        const l = r.bancas || []
        for (let i = 0; i < l.length; ++i) if (l[i].banca === banca) return l[i]
        return null
    }
    // Tocar la mesa de la presidencia editando: la elegida va ahí.
    function tocarPresidencia() {
        if (!rec.editando || rec.elegida === "") return
        rec.editar({presidencia: rec.elegida})
        rec.elegida = ""
    }
    function lugar(f, c) {
        const l = r.bancas || []
        for (let i = 0; i < l.length; ++i) if (l[i].fila === f && l[i].columna === c) return l[i]
        return null
    }
    function turno(id) {
        const c = b.cola || []
        for (let i = 0; i < c.length; ++i) if (c[i].id === id) return i + 1
        return 0
    }
    // Un color estable por bloque (sin tabla que mantener).
    function colorBloque(bloque) {
        if (!bloque) return Qt.rgba(1, 1, 1, 0.25)
        let h = 0
        for (let i = 0; i < bloque.length; ++i) h = (h * 31 + bloque.charCodeAt(i)) % 360
        return Qt.hsla(h / 360, 0.55, 0.55, 1)
    }
    function editar(cambios) {
        const e = Studio.cuerpo.editarRecinto(cambios)
        rec.aviso = e
        return e === ""
    }
    // La foto de cada posición PTZ ("cámara/posición" -> url), para las bancas sin foto.
    readonly property var fotosPtz: {
        const m = {}
        for (const c of (Studio.camaras ? Studio.camaras.camaras : []))
            for (const p of (c.posiciones || []))
                if (p.miniatura_url) {
                    m[c.entrada + "/" + p.id] = p.miniatura_url
                    if (p.nombre) m[c.entrada + "/" + p.nombre] = p.miniatura_url
                }
        return m
    }
    // La cara: en vivo si está conectado por el navegador; si no, su foto del Cuerpo;
    // si no tiene, lo que muestra la cámara de su banca (la foto de su posición PTZ).
    function cara(i) {
        if (!i) return ""
        const p = rec.remotos[i.id]
        if (p && p.conectado && rec.fotosVersion >= 0) {
            const u = rec.sr.fotoUrl(i.id)
            if (u !== "") return u
        }
        if (i.foto_url) return i.foto_url
        const pp = i.preset_ptz || {}
        return pp.camara && pp.posicion ? (rec.fotosPtz[pp.camara + "/" + pp.posicion] || "") : ""
    }
    // El resultado de cada foto (la de una banca o la tanda de las que faltan).
    Connections {
        target: Studio.cuerpo
        function onFotoLista(id, ok, texto) { if (!ok) rec.aviso = texto }
    }
    FileDialog {
        id: subirFoto
        property string deId: ""
        title: qsTr("La foto del integrante")
        nameFilters: [qsTr("Imágenes (*.jpg *.jpeg *.png *.webp *.heic *.bmp)")]
        onAccepted: { const e = Studio.cuerpo.subirFoto(deId, selectedFile.toString()); rec.aviso = e }
    }
    function tocar(celda) {
        rec.aviso = ""
        if (rec.tocarAbreMicrofono && !rec.editando && celda.l !== null && rec.conCentral) {
            if (celda.micAbierto && celda.presidente) {
                const bb = celda.l.banca
                ApplicationWindow.window.confirm({ title: qsTr("¿Cerrar el micrófono del presidente?"),
                    text: qsTr("Banca %1").arg(bb), accept: qsTr("Cerrar"), reject: qsTr("Cancelar"),
                    action: function() { rec.microfono(bb, false, true) } })
            } else {
                rec.microfono(celda.l.banca, !celda.micAbierto, false)
            }
            return
        }
        if (rec.editando) {
            if (celda.l !== null) rec.elegida = rec.elegida === celda.l.banca ? "" : celda.l.banca
            else if (rec.elegida !== "") {
                rec.editar({mover: {banca: rec.elegida, fila: celda.fila, columna: celda.columna}})
                rec.elegida = ""
            }
            return
        }
        if (celda.l !== null) menu.abrir(celda.l, celda)
    }

    // --- Cabecera --------------------------------------------------------------------
    RowLayout {
        Layout.fillWidth: true
        visible: !rec.sinNadie
        spacing: 8
        ComboBox {
            id: dist
            implicitWidth: 140
            model: [ { t: qsTr("Hemiciclo"), v: "hemiciclo" }, { t: qsTr("Grilla"), v: "grilla" }, { t: qsTr("Lista"), v: "lista" } ]
            textRole: "t"
            enabled: !rec.editando
            currentIndex: Math.max(0, model.findIndex(o => o.v === (rec.r.distribucion || "grilla")))
            onActivated: (i) => rec.editar({distribucion: model[i].v})
            ToolTip.visible: hovered; ToolTip.delay: 400; ToolTip.text: qsTr("Cómo se dibujan las bancas")
        }
        Text { text: qsTr("Filas"); color: Theme.textDim; font.pixelSize: 12; visible: rec.editando || rec.distribucion !== "lista" }
        SpinBox { from: 1; to: 40; value: Math.max(1, rec.filas); editable: true; visible: rec.editando || rec.distribucion !== "lista"
                  onValueModified: rec.editar({filas: value, columnas: Math.max(1, rec.columnas)}) }
        Text { text: qsTr("Columnas"); color: Theme.textDim; font.pixelSize: 12; visible: rec.editando || rec.distribucion !== "lista" }
        SpinBox { from: 1; to: 60; value: Math.max(1, rec.columnas); editable: true; visible: rec.editando || rec.distribucion !== "lista"
                  onValueModified: rec.editar({filas: Math.max(1, rec.filas), columnas: value}) }
        GlassButton {
            compact: true; text: qsTr("Acomodar solas")
            onClicked: {
                const n = (Studio.cuerpo.integrantes || []).filter(i => (i.banca || "") !== "").length
                const col = Math.max(rec.columnas, 1)
                rec.editar({filas: Math.max(rec.filas, Math.ceil(n / col), 1), columnas: col, auto: true})
            }
        }
        Item { Layout.fillWidth: true }
        // Las caras que faltan, con la cámara de cada banca (una por una).
        GlassButton {
            compact: true
            visible: !rec.editando
            readonly property int pendientes: Studio.cuerpo.fotosPendientes
            text: pendientes > 0 ? qsTr("Sacando fotos… faltan %1 (parar)").arg(pendientes) : qsTr("Sacar las fotos que faltan")
            active: pendientes > 0
            onClicked: {
                if (pendientes > 0) { Studio.cuerpo.cancelarFaltantes(); return }
                const n = Studio.cuerpo.capturarFaltantes()
                rec.aviso = n === 0 ? qsTr("No falta ninguna foto de los que tienen cámara en su banca (las demás se suben a mano: tocá la banca).") : ""
            }
            ToolTip.visible: hovered; ToolTip.delay: 400
            ToolTip.text: qsTr("Lleva la cámara de cada banca a su posición y saca la foto (al aire, la cámara no se mueve)")
        }
        Text { visible: !!rec.sr && rec.sr.conectados > 0; color: Theme.textDim; font.pixelSize: 12
               text: rec.sr ? qsTr("Remotos: %1 presentes de %2 conectados").arg(rec.sr.presentes).arg(rec.sr.conectados) : "" }
        GlassButton { compact: true; text: rec.editando ? qsTr("Listo") : qsTr("Editar lugares"); active: rec.editando
                      onClicked: { rec.editando = !rec.editando; rec.elegida = "" } }
    }
    Text {
        Layout.fillWidth: true; wrapMode: Text.WordWrap; font.pixelSize: 12
        visible: rec.aviso !== "" || (rec.filas === 0 && !rec.sinNadie) || rec.editando
        color: rec.aviso !== "" ? Theme.live : Theme.textFaint
        text: rec.aviso !== "" ? rec.aviso
              : rec.filas === 0 ? qsTr("Todavía no hay grilla. A cada integrante ponele su banca (botón «Integrantes» de arriba), y tocá «Acomodar solas».")
              : rec.elegida !== "" ? qsTr("Banca %1: tocá el lugar vacío donde va, o «PRESIDENCIA» (o «Sacar»).").arg(rec.elegida)
              : rec.distribucion === "hemiciclo"
                ? qsTr("Tocá una banca y después el lugar vacío donde va, o arrastrala a donde está en la sala (aparece una grilla). Las filas son los arcos (la primera, la más cerca de la presidencia). La de la presidencia: elegila y tocá «PRESIDENCIA».")
                : qsTr("Tocá una banca y después el lugar donde va. La de la presidencia: elegila y tocá «PRESIDENCIA».")
    }
    // Sin nadie cargado la grilla queda vacía y no dice nada: lo que hay que hacer.
    EstadoVacio {
        Layout.fillWidth: true
        Layout.topMargin: 70
        Layout.bottomMargin: 40
        visible: rec.sinNadie
        icono: "▦"
        titulo: qsTr("Todavía no hay integrantes en el recinto")
        texto: qsTr("Cargá quién se sienta en cada banca (a mano o pegando una planilla): nombre, bloque y número de banca. "
               + "Con eso aparecen las bancas acá, con su cara, y se pueden acomodar como está la sala de verdad.")
        accion: qsTr("Cargar integrantes…")
        onActivado: rec.pedirCuerpo()
    }
    // Las que todavía no tienen lugar.
    Flow {
        Layout.fillWidth: true
        spacing: 6
        visible: rec.editando && ((rec.r.sin_lugar || []).length > 0 || rec.elegida !== "" || (rec.hayAMano && rec.distribucion === "hemiciclo"))
        Text { text: qsTr("Sin lugar:"); color: Theme.textDim; font.pixelSize: 12; visible: (rec.r.sin_lugar || []).length > 0 }
        Repeater {
            model: rec.r.sin_lugar || []
            delegate: GlassButton {
                required property var modelData
                compact: true; text: modelData; active: rec.elegida === modelData
                onClicked: rec.elegida = modelData
            }
        }
        GlassButton { compact: true; text: qsTr("A la presidencia"); visible: rec.elegida !== "" && !(rec.pres && rec.pres.banca === rec.elegida)
                      onClicked: rec.tocarPresidencia() }
        GlassButton { compact: true; text: qsTr("A su arco"); visible: rec.distribucion === "hemiciclo" && rec.elegida !== "" && !!rec.lugarDe(rec.elegida) && rec.lugarDe(rec.elegida).x !== undefined
                      onClicked: { rec.editar({posicion: {banca: rec.elegida, x: null}}); rec.elegida = "" } }
        GlassButton { compact: true; text: qsTr("Sacar"); visible: rec.elegida !== ""
                      onClicked: { rec.editar({mover: {banca: rec.elegida, fila: -1}}); rec.elegida = "" } }
        GlassButton { compact: true; text: qsTr("Todas a su arco"); visible: rec.distribucion === "hemiciclo" && rec.hayAMano
                      onClicked: { rec.editar({posiciones_auto: true}); rec.elegida = "" } }
    }

    // --- Una banca (la misma en las tres distribuciones) ---------------------------------
    component Celda: Rectangle {
        id: celda
        property var l: null
        property int fila: -1
        property int columna: -1
        property bool ancha: false // en la lista: una fila larga
        // Editando el hemiciclo: se arrastra (corrido, mientras dura) y al soltar avisa.
        property bool arrastrable: false
        readonly property point corrido: arrastre.active ? arrastre.activeTranslation : Qt.point(0, 0)
        readonly property bool arrastrando: arrastre.active
        signal soltada()
        // Chica (hemiciclo de una sala grande): la cara, el número y lo que pasa; sin nombre.
        readonly property bool chica: !ancha && width < 104
        readonly property var i: l && l.integrante ? l.integrante : null
        readonly property bool alAire: i !== null && rec.b.al_aire && rec.b.al_aire.id === i.id
        readonly property int turno: i !== null ? rec.turno(i.id) : 0
        // La central de conferencia.
        readonly property var uni: rec.conCentral && l !== null ? rec.unidadDe(l.banca) : null
        readonly property bool micAbierto: rec.conCentral && l !== null && (rec.d.abiertos || []).indexOf(l.banca) >= 0
        readonly property int turnoCcu: rec.conCentral && l !== null ? rec.turnoCentral(l.banca) : 0
        readonly property bool leToca: rec.conCentral && l !== null && rec.d.siguiente === l.banca
        readonly property bool presidente: uni !== null && uni.tipo === "presidente"
        readonly property bool sinUnidad: rec.conCentral && l !== null && (uni === null || !uni.conectada)
        // La sesión remota.
        readonly property var rem: i !== null ? rec.remotos[i.id] : undefined
        readonly property bool conectado: !!rem && rem.conectado
        readonly property bool esperando: !!rem && rem.estado === "esperando"
        readonly property bool habilitado: !!rem && rem.estado === "habilitado"
        readonly property string fotoCara: rec.cara(i)
        radius: 8
        color: l === null ? "transparent"
               : micAbierto ? Qt.rgba(0.78, 0.16, 0.16, 0.85)
               : (alAire ? Qt.rgba(0.9, 0.15, 0.15, 0.35) : turnoCcu > 0 ? Qt.rgba(1, 0.7, 0.1, 0.18) : Qt.rgba(1, 1, 1, 0.06))
        // Apagada si no tiene unidad en la central y tampoco está conectada por el navegador.
        opacity: sinUnidad && !conectado ? 0.45 : 1
        border.width: leToca || alAire || presidente || esperando || (l && rec.elegida === l.banca) ? 2 : 1
        border.color: leToca ? (rec.fase ? Theme.live : "transparent")
                      : esperando ? (rec.fase ? Theme.amber : "transparent")
                      : alAire ? Theme.live : (l && rec.elegida === l.banca) ? Theme.accent
                      : presidente ? "#d4af37" : turnoCcu > 0 ? Theme.amber
                      : l === null ? (rec.editando ? Qt.rgba(1, 1, 1, 0.15) : "transparent") : Qt.rgba(1, 1, 1, 0.1)
        clip: true
        Rectangle { anchors { left: parent.left; top: parent.top; bottom: parent.bottom } width: 4
                    visible: celda.i !== null; color: rec.colorBloque(celda.i ? celda.i.bloque : "") }
        RowLayout {
            anchors { fill: parent; leftMargin: 8; rightMargin: 5; topMargin: 4; bottomMargin: 4 }
            visible: celda.l !== null
            spacing: 6
            // La cara.
            Rectangle {
                Layout.preferredWidth: celda.ancha ? 44 : Math.min(celda.height - 8, 56)
                Layout.preferredHeight: Layout.preferredWidth
                Layout.alignment: Qt.AlignVCenter
                radius: width / 2
                color: Qt.rgba(1, 1, 1, 0.08)
                clip: true
                visible: celda.i !== null
                border.width: celda.conectado ? 2 : 0
                border.color: celda.habilitado ? Theme.ok : Theme.amber
                Image { anchors.fill: parent; anchors.margins: parent.border.width; source: celda.fotoCara
                        fillMode: Image.PreserveAspectCrop; asynchronous: true; cache: false; visible: celda.fotoCara !== "" }
                Text { anchors.centerIn: parent; visible: celda.fotoCara === ""; color: Theme.textFaint; font.pixelSize: 16
                       text: celda.i ? celda.i.nombre.split(" ").map(p => p.charAt(0)).slice(0, 2).join("") : "" }
            }
            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 0
                RowLayout {
                    Layout.fillWidth: true
                    Text { text: celda.l ? celda.l.banca : ""; color: Theme.text; font { pixelSize: 14; weight: Font.Black } }
                    Text { visible: celda.ancha && celda.i !== null; Layout.fillWidth: true; elide: Text.ElideRight
                           text: celda.i ? celda.i.nombre + (celda.i.bloque ? " · " + celda.i.bloque : "") : ""; color: Theme.text; font.pixelSize: 12 }
                    Item { Layout.fillWidth: !celda.ancha }
                    Text { visible: celda.micAbierto; text: qsTr("● AIRE"); color: "white"; font { pixelSize: 10; weight: Font.Black } }
                    Text { visible: celda.turnoCcu > 0 && !celda.micAbierto; text: qsTr("pidió %1").arg(celda.turnoCcu); color: Theme.amber; font { pixelSize: 10; weight: Font.DemiBold } }
                    Text { visible: celda.conectado
                           text: celda.esperando ? qsTr("ESPERA") : (celda.rem && celda.rem.lugar === "sala" ? qsTr("CEL") : qsTr("REMOTO"))
                           color: celda.esperando ? Theme.amber : Theme.ok; font { pixelSize: 9; weight: Font.Black }
                           ToolTip.visible: hvR.hovered; ToolTip.text: celda.esperando ? qsTr("Entró por el navegador y espera que lo habiliten (tocá la banca)")
                                                                       : celda.rem && celda.rem.lugar === "sala" ? qsTr("Conectado con el celular desde la sala") : qsTr("Conectado desde afuera")
                           HoverHandler { id: hvR } }
                    Text { visible: celda.sinUnidad && !celda.conectado; text: "⏻"; color: Theme.textFaint; font.pixelSize: 11
                           ToolTip.visible: hvU.hovered; ToolTip.text: celda.uni === null ? qsTr("Sin unidad de la central (Asamblea → Conferencia → Unidades)") : qsTr("Unidad desconectada")
                           HoverHandler { id: hvU } }
                    Text { visible: celda.turno > 0; text: "#" + celda.turno; color: Theme.amber; font { pixelSize: 11; weight: Font.DemiBold } }
                    Text { visible: celda.i !== null && !celda.i.validado; text: "⚠"; color: Theme.amber; font.pixelSize: 11
                           ToolTip.visible: hv.hovered; ToolTip.text: qsTr("Nombre sin validar (botón «Integrantes» de arriba)")
                           HoverHandler { id: hv } }
                }
                Text { visible: !celda.ancha && !celda.chica; Layout.fillWidth: true; elide: Text.ElideRight; maximumLineCount: 2; wrapMode: Text.WordWrap
                       text: celda.i ? celda.i.nombre : qsTr("(nadie con esta banca)")
                       color: celda.i ? Theme.text : Theme.textFaint; font.pixelSize: 11 }
                Item { Layout.fillHeight: true; visible: !celda.ancha }
                Text { Layout.fillWidth: true; elide: Text.ElideRight; font.pixelSize: 10
                       visible: celda.ancha || (celda.height > 70 && !celda.chica)
                       // La toma es de la banca (también de una vacía).
                       text: celda.l && celda.l.toma ? "🎥 " + celda.l.toma.posicion : qsTr("sin toma")
                       color: celda.l && celda.l.toma ? Theme.textDim : Theme.amber }
            }
        }
        MouseArea {
            anchors.fill: parent
            enabled: !celda.arrastrable
            pressAndHoldInterval: 600
            onPressAndHold: if (!rec.editando && celda.l !== null) menu.abrir(celda.l, celda)
            onClicked: rec.tocar(celda)
        }
        Item {
            anchors.fill: parent
            enabled: celda.arrastrable
            TapHandler { onTapped: rec.tocar(celda) }
            DragHandler {
                id: arrastre
                target: null
                onActiveChanged: if (!active) celda.soltada()
            }
        }
    }

    // La mesa de la presidencia: el cartel y, si hay, su banca encima. Editando,
    // tocarla con una banca elegida la pone ahí.
    component Mesa: Rectangle {
        id: mesa
        property real anchoBanca: 150
        property real altoBanca: 74
        implicitHeight: rec.pres ? altoBanca + 26 : 26
        radius: 6
        color: Qt.rgba(1, 1, 1, 0.05)
        border.width: rec.editando && rec.elegida !== "" ? 1 : 0
        border.color: Theme.accent
        Text { anchors { horizontalCenter: parent.horizontalCenter; top: parent.top; topMargin: rec.pres ? 3 : 0 }
               height: rec.pres ? implicitHeight : parent.height; verticalAlignment: Text.AlignVCenter
               text: qsTr("PRESIDENCIA"); color: Theme.textFaint
               font { pixelSize: 11; letterSpacing: 2; weight: Font.DemiBold } }
        MouseArea { anchors.fill: parent; enabled: rec.editando && rec.elegida !== ""; onClicked: rec.tocarPresidencia() }
        Celda {
            visible: !!rec.pres
            l: rec.pres
            width: mesa.anchoBanca; height: mesa.altoBanca
            anchors { horizontalCenter: parent.horizontalCenter; bottom: parent.bottom; bottomMargin: 4 }
        }
    }

    // --- Presidencia -----------------------------------------------------------------------
    Mesa {
        Layout.fillWidth: true
        visible: rec.filas > 0 && rec.distribucion === "grilla" && !rec.sinNadie
        anchoBanca: Math.min(220, Math.max(120, width / Math.max(1, rec.columnas)))
    }

    // --- Grilla ----------------------------------------------------------------------------
    GridLayout {
        Layout.fillWidth: true
        visible: rec.filas > 0 && rec.distribucion === "grilla" && !rec.sinNadie
        columns: Math.max(1, rec.columnas)
        rowSpacing: 6; columnSpacing: 6
        Repeater {
            model: rec.distribucion === "grilla" ? rec.filas * rec.columnas : 0
            delegate: Celda {
                required property int index
                fila: Math.floor(index / rec.columnas)
                columna: index % rec.columnas
                l: rec.lugar(fila, columna)
                Layout.fillWidth: true
                Layout.preferredHeight: 74
            }
        }
    }

    // --- Hemiciclo: cada fila un arco alrededor de la presidencia (abajo, al centro) ----------
    Item {
        id: hemi
        Layout.fillWidth: true
        visible: rec.filas > 0 && rec.distribucion === "hemiciclo" && !rec.sinNadie
        // Las medidas salen de que entren las bancas sin pisarse: en el arco de
        // adentro, la distancia entre dos bancas (radio · π / columnas) tiene que
        // ser más que una banca; cada fila de más agrega una banca y algo de radio.
        // Tarjetas casi cuadradas; entre dos vecinas (en el arco o entre arcos) va al
        // menos la diagonal de una tarjeta, si no se pisan en las diagonales.
        readonly property real kCols: 1.4 * Math.max(1, rec.columnas) / Math.PI
        readonly property real kFila: 1.32
        readonly property real cw: Math.max(52, Math.min(120, (width - 8) / (2 * (kCols + kFila * Math.max(0, rec.filas - 1)) + 1)))
        readonly property real ch: cw * 0.9
        readonly property real r0: Math.max(cw * kCols, cw * 1.4)
        readonly property real paso: cw * kFila
        readonly property real r1: r0 + paso * Math.max(0, rec.filas - 1)
        readonly property real cx: width / 2
        readonly property real cy: r1 + ch / 2 + 6
        readonly property real paso4: cw / 4 // la grilla de ubicar a mano: un cuarto de banca
        property bool arrastrando: false
        implicitHeight: cy + 4 + mesaHemi.height + 10
        // Mientras se arrastra una banca: la grilla a la que se acomoda al soltarla.
        Item {
            anchors.fill: parent
            visible: hemi.arrastrando
            Repeater {
                model: hemi.arrastrando ? Math.ceil(hemi.width / hemi.paso4) + 1 : 0
                delegate: Rectangle {
                    required property int index
                    readonly property int k: index - Math.floor(hemi.cx / hemi.paso4)
                    x: hemi.cx + k * hemi.paso4; width: 1; height: hemi.height
                    color: k % 4 === 0 ? Qt.rgba(1, 1, 1, 0.16) : Qt.rgba(1, 1, 1, 0.06)
                }
            }
            Repeater {
                model: hemi.arrastrando ? Math.ceil(hemi.height / hemi.paso4) + 1 : 0
                delegate: Rectangle {
                    required property int index
                    readonly property int k: index - Math.floor(hemi.cy / hemi.paso4)
                    y: hemi.cy + k * hemi.paso4; height: 1; width: hemi.width
                    color: k % 4 === 0 ? Qt.rgba(1, 1, 1, 0.16) : Qt.rgba(1, 1, 1, 0.06)
                }
            }
        }
        Mesa {
            id: mesaHemi
            x: hemi.cx - width / 2; y: hemi.cy + 4
            width: Math.max(Math.min(260, hemi.r0 * 1.2), rec.pres ? hemi.cw + 24 : 0)
            anchoBanca: hemi.cw; altoBanca: hemi.ch
        }
        // Editando: los lugares vacíos de los arcos, para tocarlos.
        Repeater {
            model: rec.editando && rec.distribucion === "hemiciclo" ? rec.filas * rec.columnas : 0
            delegate: Celda {
                required property int index
                fila: Math.floor(index / rec.columnas)
                columna: index % rec.columnas
                readonly property real radio: hemi.r0 + hemi.paso * fila
                readonly property real ang: Math.PI * (1 - (columna + 0.5) / Math.max(1, rec.columnas))
                visible: rec.lugar(fila, columna) === null
                width: hemi.cw; height: hemi.ch
                x: hemi.cx + radio * Math.cos(ang) - width / 2
                y: hemi.cy - radio * Math.sin(ang) - height / 2
            }
        }
        Repeater {
            model: rec.distribucion === "hemiciclo" ? (rec.r.bancas || []) : []
            delegate: Celda {
                id: enArco
                required property var modelData
                l: modelData
                // La fila 0 (adelante) en el arco de adentro; las columnas de izquierda a derecha.
                // Si se ubicó a mano, donde la dejaron (en anchos de banca desde la presidencia).
                readonly property real radio: hemi.r0 + hemi.paso * modelData.fila
                readonly property real ang: Math.PI * (1 - (modelData.columna + 0.5) / Math.max(1, rec.columnas))
                readonly property bool aMano: modelData.x !== undefined
                readonly property real centroX: aMano ? hemi.cx + modelData.x * hemi.cw : hemi.cx + radio * Math.cos(ang)
                readonly property real centroY: aMano ? hemi.cy + modelData.y * hemi.cw : hemi.cy - radio * Math.sin(ang)
                width: hemi.cw; height: hemi.ch
                x: centroX - width / 2 + corrido.x
                y: centroY - height / 2 + corrido.y
                z: arrastrando ? 2 : 1
                arrastrable: rec.editando
                onArrastrandoChanged: hemi.arrastrando = arrastrando
                // Al soltarla, a la grilla (un cuarto de banca), sin salir del dibujo.
                onSoltada: {
                    const cx = Math.max(width / 2, Math.min(hemi.width - width / 2, x + width / 2))
                    const cy = Math.max(height / 2, Math.min(hemi.cy - height / 2, y + height / 2))
                    const q = v => Math.round(v * 4) / 4
                    rec.editar({posicion: {banca: modelData.banca, x: q((cx - hemi.cx) / hemi.cw), y: q((cy - hemi.cy) / hemi.cw)}})
                }
            }
        }
    }

    // --- Lista ------------------------------------------------------------------------------
    ColumnLayout {
        Layout.fillWidth: true
        visible: rec.distribucion === "lista" && !rec.sinNadie
        spacing: 4
        Repeater {
            model: rec.distribucion === "lista"
                   ? (rec.r.bancas || []).slice().sort((x, y) => String(x.banca).localeCompare(String(y.banca), undefined, {numeric: true}))
                   : []
            delegate: Celda {
                required property var modelData
                l: modelData
                ancha: true
                Layout.fillWidth: true
                Layout.preferredHeight: 52
            }
        }
    }
    Item { Layout.fillHeight: true }

    // «Apretar para hablar» a un remoto conectado: mientras se mantiene apretado, oye
    // tu micrófono de la sala en su celular (Studio.asamblea.hablarARemoto; por MCP sesion_hablar).
    Popup {
        id: ptt
        property var i: null
        property bool hablando: false
        function abrir(integrante) { ptt.i = integrante; ptt.hablando = false; open() }
        function poner(on) {
            if (!ptt.i || on === ptt.hablando) return
            const e = Studio.asamblea.hablarARemoto(ptt.i.id, on)
            if (e !== "") { rec.aviso = e; ptt.hablando = false; return }
            ptt.hablando = on
        }
        onClosed: poner(false)
        modal: true
        anchors.centerIn: Overlay.overlay
        padding: 18
        background: Rectangle { color: Theme.panel; radius: 14; border.color: Theme.glassBorder }
        contentItem: ColumnLayout {
            spacing: 12
            Text { text: ptt.i ? qsTr("Hablarle a %1").arg(ptt.i.nombre) : ""; color: Theme.text; font { pixelSize: 15; weight: Font.DemiBold } }
            Rectangle {
                Layout.alignment: Qt.AlignHCenter
                width: 200; height: 200; radius: 100
                color: ptt.hablando ? Theme.live : Qt.rgba(1, 1, 1, 0.08)
                border.color: ptt.hablando ? "white" : Theme.glassBorder; border.width: 3
                Text { anchors.centerIn: parent; horizontalAlignment: Text.AlignHCenter; color: "white"
                       font { pixelSize: 16; weight: Font.Bold }
                       text: ptt.hablando ? qsTr("🎙\nTE ESTÁ\nESCUCHANDO") : qsTr("🎙\nMANTENÉ\nAPRETADO") }
                MouseArea {
                    anchors.fill: parent
                    onPressed: ptt.poner(true)
                    onReleased: ptt.poner(false)
                    onCanceled: ptt.poner(false)
                }
            }
            Text { Layout.preferredWidth: 260; wrapMode: Text.WordWrap; color: Theme.textFaint; font.pixelSize: 11
                   text: qsTr("Te oye con tu micrófono de la sala (el de hablarle a los invitados), mezclado con el sonido que recibe en su celular.") }
            GlassButton { Layout.alignment: Qt.AlignRight; compact: true; text: qsTr("Cerrar"); onClicked: ptt.close() }
        }
    }

    Menu {
        id: menu
        property var l: null
        readonly property var i: menu.l && menu.l.integrante ? menu.l.integrante : null
        readonly property var rem: menu.i ? rec.remotos[menu.i.id] : undefined
        function abrir(l, item) { menu.l = l; popup(item, 0, item.height) }
        // La sesión remota primero: si alguien espera, es lo que se busca.
        MenuItem { text: qsTr("Habilitar (lo vi por la cámara)"); visible: !!menu.rem && menu.rem.estado === "esperando"; height: visible ? implicitHeight : 0
                   onTriggered: { const e = rec.sr.habilitarUi(menu.i.id, true); if (e) rec.aviso = e } }
        MenuItem { text: qsTr("Deshabilitar (sesión remota)"); visible: !!menu.rem && menu.rem.estado === "habilitado"; height: visible ? implicitHeight : 0
                   onTriggered: rec.sr.habilitarUi(menu.i.id, false) }
        MenuItem { text: qsTr("Sacar de la sesión remota"); visible: !!menu.rem && menu.rem.conectado; height: visible ? implicitHeight : 0
                   onTriggered: rec.sr.expulsarUi(menu.i.id, "") }
        MenuItem { text: qsTr("🎙 Hablarle (apretar para hablar)…"); visible: !!menu.rem && menu.rem.conectado; height: visible ? implicitHeight : 0
                   onTriggered: ptt.abrir(menu.i) }
        MenuItem { text: qsTr("Abrir el micrófono (central)"); visible: rec.conCentral; height: visible ? implicitHeight : 0
                   onTriggered: rec.microfono(menu.l.banca, true, false) }
        MenuItem { text: qsTr("Cerrar el micrófono (central)"); visible: rec.conCentral; height: visible ? implicitHeight : 0
                   onTriggered: rec.microfono(menu.l.banca, false, true) }
        MenuItem { text: qsTr("Sacar su foto con la cámara"); enabled: menu.i !== null && !!(menu.i.preset_ptz && menu.i.preset_ptz.camara)
                   onTriggered: { rec.aviso = ""; Studio.cuerpo.capturarFoto(menu.i.id) } }
        MenuItem { text: qsTr("Subir su foto…"); enabled: menu.i !== null
                   onTriggered: { subirFoto.deId = menu.i.id; subirFoto.open() } }
        MenuItem { text: qsTr("Toma de la banca (cámara y posición)…")
                   onTriggered: tomaBanca.abrir(menu.l) }
        MenuItem { text: qsTr("Apuntar la cámara"); enabled: !!(menu.l && menu.l.toma)
                   onTriggered: rec.aviso = Studio.bancas.enfocar("banca " + menu.l.banca, false) }
        MenuItem { text: qsTr("Al aire (arma su cámara)"); enabled: menu.i !== null
                   onTriggered: { const e = Studio.bancas.alAire(menu.l.banca, "armar"); rec.aviso = e !== "" ? e : Studio.bancas.ultimoAviso() } }
        MenuItem { text: qsTr("Sumar a la fila"); enabled: menu.i !== null
                   onTriggered: rec.aviso = Studio.bancas.agregar(menu.l.banca) }
        MenuItem { text: qsTr("Sacar de la fila"); enabled: menu.i !== null && rec.turno(menu.i.id) > 0
                   onTriggered: Studio.bancas.quitar(menu.l.banca) }
        MenuItem { text: qsTr("Terminar (cierra su micrófono)")
                   enabled: menu.i !== null && rec.b.al_aire && rec.b.al_aire.id === menu.i.id
                   onTriggered: Studio.bancas.terminar() }
    }

    // La toma de una banca: la cámara y la posición que la enfocan (es de la banca,
    // aunque esté vacía o cambie quién se sienta).
    Popup {
        id: tomaBanca
        property var l: null
        function abrir(l) { tomaBanca.l = l; open() }
        anchors.centerIn: Overlay.overlay
        modal: true
        padding: 16
        background: Rectangle { radius: 12; color: Theme.panel; border.color: Qt.rgba(1, 1, 1, 0.12) }
        ColumnLayout {
            spacing: 10
            Text {
                text: qsTr("Toma de la banca %1").arg(tomaBanca.l ? tomaBanca.l.banca : "")
                      + (tomaBanca.l && tomaBanca.l.integrante ? " — " + tomaBanca.l.integrante.nombre : "")
                color: Theme.text; font { pixelSize: 14; weight: Font.DemiBold }
            }
            Text {
                Layout.maximumWidth: 360
                wrapMode: Text.WordWrap
                text: qsTr("La cámara y la posición que la enfocan. Las usan dar la palabra, el debate, «enfocar banca N» y la fuente «Banca con la palabra». Las posiciones se guardan en Cámaras PTZ.")
                color: Theme.textDim; font.pixelSize: 11
            }
            SelectorToma {
                camara: tomaBanca.l && tomaBanca.l.toma ? tomaBanca.l.toma.camara : ""
                posicion: tomaBanca.l && tomaBanca.l.toma ? tomaBanca.l.toma.posicion : ""
                anchoCamara: 170; anchoPosicion: 160
                onElegido: (c, p) => {
                    const e = Studio.cuerpo.setToma(tomaBanca.l.banca, c, p)
                    if (e !== "") rec.aviso = e
                }
            }
            GlassButton { Layout.alignment: Qt.AlignRight; compact: true; text: qsTr("Listo"); onClicked: tomaBanca.close() }
        }
    }
}
