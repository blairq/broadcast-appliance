// El debate entre dos, moderado por la presidencia (asamblea/Debate): dos de la sala,
// dos remotos o uno y uno. Se eligen A y B, se empieza (la central queda en modo
// operador con dos micrófonos, los remotos toman la palabra por sus canales, las
// cámaras de los de la sala van a su banca y se arma la escena de ese tipo de
// debate) y la presidencia da el turno. Lo mismo en la consola web de la
// presidencia y por MCP (debate_*).
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import BA

ColumnLayout {
    id: pd
    spacing: 12
    readonly property var deb: Studio.debate
    readonly property var e: deb ? deb.estado : ({})
    readonly property bool activo: !!e.activo
    readonly property var cfg: e.config || ({})
    readonly property var integrantes: (Studio.cuerpo.integrantes || []).filter(i => (i.banca || "") !== "")
    readonly property var etiquetas: integrantes.map(i => "Banca " + i.banca + " · " + i.nombre + (pd.esRemoto(i.id) ? "  (remoto)" : ""))
    function esRemoto(id) {
        const s = Studio.sesionRemota
        if (!s) return false
        for (const p of s.participantes) if (p.id === id) return p.conectado && p.estado === "habilitado"
        return false
    }
    function avisar(e) { if (e) Studio.notify(e, "error") }
    function mmss(s) { const a = Math.abs(s); return (s < 0 ? "-" : "") + Math.floor(a / 60) + ":" + String(a % 60).padStart(2, "0") }

    // Las escenas para elegir.
    property int tick: 0
    Connections {
        target: Studio.scenes
        function onRowsInserted() { pd.tick++ }
        function onRowsRemoved() { pd.tick++ }
        function onModelReset() { pd.tick++ }
        function onDataChanged() { pd.tick++ }
    }
    readonly property var escenas: {
        pd.tick
        const m = Studio.scenes, l = ["—"]
        if (!m) return l
        for (let i = 0; i < m.rowCount(); ++i) l.push(m.data(m.index(i, 0), 257)) // NameRole
        return l
    }

    // --- Elegir y empezar -------------------------------------------------------------
    RowLayout {
        visible: !pd.activo
        Layout.fillWidth: true
        spacing: 10
        Text { text: "A"; color: Theme.textDim; font.pixelSize: 13 }
        ComboBox { id: elegirA; Layout.preferredWidth: 260; model: pd.etiquetas }
        Text { text: "contra B"; color: Theme.textDim; font.pixelSize: 13 }
        ComboBox { id: elegirB; Layout.preferredWidth: 260; model: pd.etiquetas; currentIndex: Math.min(1, pd.etiquetas.length - 1) }
        GlassButton {
            text: "Empezar el debate"; active: true
            enabled: pd.integrantes.length >= 2 && elegirA.currentIndex !== elegirB.currentIndex
            onClicked: pd.avisar(pd.deb.iniciarUi(pd.integrantes[elegirA.currentIndex].id, pd.integrantes[elegirB.currentIndex].id, "a"))
        }
        Item { Layout.fillWidth: true }
    }

    // --- En curso ------------------------------------------------------------------------
    RowLayout {
        visible: pd.activo
        Layout.fillWidth: true
        spacing: 12
        component Lado: Rectangle {
            property var l: null
            property bool habla: false
            property string letra: ""
            Layout.fillWidth: true
            implicitHeight: 70
            radius: 12
            color: habla ? Qt.rgba(0.18, 0.8, 0.44, 0.2) : Qt.rgba(1, 1, 1, 0.05)
            border.color: habla ? Theme.ok : Theme.glassBorder; border.width: habla ? 2 : 1
            ColumnLayout {
                anchors { fill: parent; margins: 10 }
                spacing: 2
                Text { text: letra + (parent.parent.habla ? "  ● HABLA" : ""); color: parent.parent.habla ? Theme.ok : Theme.textDim
                       font { pixelSize: 11; weight: Font.Bold; letterSpacing: 1 } }
                Text { text: l ? l.nombre : "—"; color: Theme.text; font { pixelSize: 16; weight: Font.DemiBold } }
                Text { text: !l ? "" : l.remoto ? "remoto · entra por «Remoto con la palabra" + (l.canal === 2 ? " 2" : "") + "»" : "banca " + l.banca + " (central)"
                       color: Theme.textFaint; font.pixelSize: 11 }
            }
        }
        Lado { l: pd.e.a || null; letra: "A"; habla: pd.e.turno === "a" || pd.e.turno === "ambos" }
        ColumnLayout {
            spacing: 2
            Text { Layout.alignment: Qt.AlignHCenter; color: Theme.text; font { pixelSize: 26; family: Theme.mono }
                   text: (pd.e.turno === "a" || pd.e.turno === "b") && pd.e.turno_s > 0 ? pd.mmss(pd.e.restante_s || 0) : "—" }
            Text { Layout.alignment: Qt.AlignHCenter; color: Theme.textDim; font.pixelSize: 11
                   text: pd.e.turno === "nadie" ? "nadie tiene el turno" : pd.e.turno === "ambos" ? "hablan los dos" : "turno de " + (pd.e.turno || "").toUpperCase() }
        }
        Lado { l: pd.e.b || null; letra: "B"; habla: pd.e.turno === "b" || pd.e.turno === "ambos" }
    }
    RowLayout {
        visible: pd.activo
        spacing: 8
        GlassButton { text: "Turno de A"; active: pd.e.turno === "a"; onClicked: pd.avisar(pd.deb.turnoUi("a")) }
        GlassButton { text: "Turno de B"; active: pd.e.turno === "b"; onClicked: pd.avisar(pd.deb.turnoUi("b")) }
        GlassButton { text: "Ambos"; active: pd.e.turno === "ambos"; onClicked: pd.avisar(pd.deb.turnoUi("ambos")) }
        GlassButton { text: "Nadie"; active: pd.e.turno === "nadie"; onClicked: pd.avisar(pd.deb.turnoUi("nadie")) }
        GlassButton { compact: true; text: "+30 s"; onClicked: pd.deb.sumarUi(30) }
        GlassButton { compact: true; text: "−30 s"; onClicked: pd.deb.sumarUi(-30) }
        Item { Layout.fillWidth: true }
        GlassButton { text: "🔇 Silenciar a todos"; onClicked: pd.avisar(Studio.silenciarTodos("operador")) }
        GlassButton { text: "Terminar el debate"; activeColor: Theme.live; onClicked: pd.deb.terminarUi() }
    }
    Text {
        visible: pd.activo && !!pd.e.aviso
        Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.amber; font.pixelSize: 12
        text: pd.e.aviso || ""
    }

    // --- Configuración -------------------------------------------------------------------
    Text { text: "La escena de cada tipo de debate"; color: Theme.text; font { pixelSize: 13; weight: Font.DemiBold } }
    GridLayout {
        columns: 2
        columnSpacing: 10
        rowSpacing: 6
        component ElegirEscena: ComboBox {
            property string clave
            Layout.preferredWidth: 260
            model: pd.escenas
            currentIndex: Math.max(0, pd.escenas.indexOf((pd.cfg.escenas || {})[clave] || "—"))
            onActivated: {
                const esc = {}
                esc[clave] = currentIndex <= 0 ? "" : currentText
                pd.avisar(pd.deb.configurarUi({ escenas: esc }))
            }
        }
        Text { text: "Dos de la sala"; color: Theme.textDim; font.pixelSize: 12 }
        ElegirEscena { clave: "sala_sala" }
        Text { text: "Uno de la sala y un remoto"; color: Theme.textDim; font.pixelSize: 12 }
        ElegirEscena { clave: "sala_remoto" }
        Text { text: "Dos remotos"; color: Theme.textDim; font.pixelSize: 12 }
        ElegirEscena { clave: "remoto_remoto" }
        Text { text: "Tiempo de cada turno"; color: Theme.textDim; font.pixelSize: 12 }
        RowLayout {
            SpinBox { from: 0; to: 1800; stepSize: 30; editable: true; value: pd.cfg.turno_s || 0
                      onValueModified: pd.avisar(pd.deb.configurarUi({ turno_s: value })) }
            Text { text: "segundos (0 = sin reloj)"; color: Theme.textFaint; font.pixelSize: 11 }
        }
        Text { text: "Al empezar"; color: Theme.textDim; font.pixelSize: 12 }
        CheckBox { text: "Cortar la escena al aire (si no, queda armada para el TAKE)"; checked: !!pd.cfg.directo
                   onToggled: pd.avisar(pd.deb.configurarUi({ directo: checked })) }
    }
    Text {
        Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.textFaint; font.pixelSize: 11
        text: "Los remotos entran como «Remoto con la palabra» (A, o el único remoto) y «Remoto con la palabra 2» (B): la escena de dos remotos tiene que mostrar las dos entradas; " +
              "la de sala y remoto, la cámara de la sala y «Remoto con la palabra». Con alguno de la sala, la central pasa a modo operador con dos micrófonos y al terminar vuelve como estaba. " +
              "La presidencia hace lo mismo desde su consola web."
    }
    Item { Layout.fillHeight: true }
}
