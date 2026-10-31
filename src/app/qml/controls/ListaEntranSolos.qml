// Quién entra solo si se lo reconoce, en el evento abierto. NO es una lista
// aparte: es el permiso «entra solo» del PADRÓN del evento (Asistente::
// dejarPasarSolo), el mismo que mira la sala para dejar pasar sola a la gente
// del lobby y la asamblea para habilitar al que espera en la sesión remota.
// Acá se lo muestra y se lo cambia sin tener que abrir el padrón entero. Por
// MCP: padron_ver {solos: true} y padron_cambiar {id, dejarPasarSolo}.
//
// Sin cara no hay con qué reconocer: solo se puede dar el permiso a quien
// tiene ficha en Personas (dejar pasar a quien ESCRIBIÓ un nombre es dejar
// pasar a cualquiera que sepa escribirlo).
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import BA

ColumnLayout {
    id: les
    spacing: 8
    readonly property var ev: Studio.evento
    readonly property bool hayEvento: !!ev && ev.abierto !== ""
    readonly property var conFicha: hayEvento ? ev.padron.filter(a => a.personaId) : []
    readonly property int solos: conFicha.filter(a => a.dejarPasarSolo).length

    // La lista cambia cada vez que alguien llega (se acredita): se actualiza
    // EN SU LUGAR, por id, para que no salte mientras se la mira.
    ListaConClave {
        id: filas
        clave: "id"
        roles: ["nombre", "personaId", "solo"]
        lista: les.conFicha.map(a => ({ id: a.id, nombre: a.nombre || "", personaId: a.personaId, solo: !!a.dejarPasarSolo }))
    }

    Text {
        Layout.fillWidth: true
        Layout.preferredWidth: 0
        wrapMode: Text.WordWrap
        color: Theme.textDim; font.pixelSize: 12
        text: !les.hayEvento
              ? qsTr("Abrí un evento: quién entra solo es de cada evento (su padrón).")
              : qsTr("Los que tienen el permiso entran solos si la cámara los reconoce: a la conferencia sin esperar, y en la "
                     + "asamblea quedan habilitados. Se avisa siempre. Sin ficha de rostro no se puede dar el permiso.")
    }
    ListView {
        Layout.fillWidth: true
        Layout.preferredHeight: Math.min(contentHeight, 260)
        visible: les.hayEvento
        clip: true
        spacing: 4
        model: filas
        delegate: RowLayout {
            required property string id
            required property string nombre
            required property bool solo
            width: ListView.view.width
            spacing: 8
            Toggle {
                checked: solo
                onToggled: (wanted) => Studio.evento.cambiarAsistente(id, { dejarPasarSolo: wanted })
            }
            Text { Layout.fillWidth: true; text: nombre || qsTr("(sin nombre)"); color: Theme.text; font.pixelSize: 13
                   elide: Text.ElideRight }
            Text { text: solo ? qsTr("entra solo") : qsTr("espera"); color: solo ? Theme.ok : Theme.textFaint; font.pixelSize: 11 }
        }
    }
    Text {
        visible: les.hayEvento && filas.count === 0
        color: Theme.textFaint; font.pixelSize: 12
        text: qsTr("Nadie del padrón tiene ficha de rostro todavía.")
    }
    // Sumar al padrón a alguien de la libreta, ya con el permiso.
    RowLayout {
        Layout.fillWidth: true
        visible: les.hayEvento && Studio.personas.disponible
        spacing: 8
        ComboBox {
            id: sumar
            Layout.fillWidth: true
            readonly property var opciones: Studio.personas.personas.filter(
                p => p.nombre && !les.conFicha.some(a => a.personaId === p.id))
            model: opciones
            textRole: "nombre"
            displayText: opciones.length === 0 ? qsTr("Todos los de Personas ya están") : (currentIndex < 0 ? qsTr("Elegí a alguien de Personas…") : currentText)
            currentIndex: -1
            enabled: opciones.length > 0
        }
        GlassButton {
            compact: true; text: qsTr("Sumar con permiso"); active: true
            enabled: sumar.currentIndex >= 0
            onClicked: {
                const p = sumar.opciones[sumar.currentIndex]
                const id = Studio.evento.anotar({ personaId: p.id, nombre: p.nombre, dejarPasarSolo: true,
                                                  papel: "panelista", estado: "confirmo" })
                if (id === "") Studio.notify(qsTr("No se pudo sumar a «%1» al padrón.").arg(p.nombre), "error")
                sumar.currentIndex = -1
            }
        }
    }
}
