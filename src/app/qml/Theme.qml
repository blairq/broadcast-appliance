// Tema visual (solo presentacion). Colores y medidas en un unico lugar.
pragma Singleton
import QtQuick

QtObject {
    readonly property color bg: "#0a0a0c"
    readonly property color panel: "#141417"
    readonly property color accent: "#ff6a2b"
    readonly property color accentSoft: "#ff9a5c"
    readonly property color amber: "#f5b83d"
    readonly property color live: "#ff3b30"
    readonly property color ok: "#35c46b"
    readonly property color warn: "#f5b83d"
    // Las alarmas de las tarjetas (plan de resolución y latencias): se puede mejorar,
    // está mal armado, anda mal. Las mismas en el analizador de latencia y en el de Video.
    readonly property color alarmaMejorable: "#f5d33d"
    readonly property color alarmaMalArmado: "#ff8a2a"
    readonly property color alarmaMal: "#ff3b30"
    function colorAlarma(nivel) {
        return nivel === "mal" ? alarmaMal : nivel === "mal_armado" ? alarmaMalArmado : nivel === "mejorable" ? alarmaMejorable : "transparent"
    }
    function textoAlarma(nivel) {
        return nivel === "mal" ? qsTr("anda mal") : nivel === "mal_armado" ? qsTr("mal armado") : nivel === "mejorable" ? qsTr("se puede mejorar") : ""
    }
    readonly property color text: "#f4f4f6"
    readonly property color textDim: "#a2a2aa"
    readonly property color textFaint: "#6c6c74"
    readonly property color glassTint: Qt.rgba(0.08, 0.08, 0.09, 0.42)
    readonly property color glassBorder: Qt.rgba(1, 1, 1, 0.13)
    readonly property color glassHighlight: Qt.rgba(1, 1, 1, 0.07)
    readonly property color field: Qt.rgba(1, 1, 1, 0.07)
    // Los botones de un canal de audio, iguales en la tarjeta, la ventana del Mezclador
    // y la matriz (como en una consola: mute rojo, PFL amarillo).
    readonly property color canalMute: "#ff3b30"
    readonly property color canalPfl: "#facc15"
    readonly property color canalAfv: "#3b82f6"
    readonly property color canalSinc: "#8b5cf6"
    // La paleta de los controles de Qt (TextField, ComboBox, Menu...). Vive
    // ACA y no adentro de una ventana porque hay TRES que la necesitan: la
    // principal, la de una pagina suelta y la de un panel suelto. Estaba
    // escrita a mano solo en la principal, y por eso un buscador sacado a su
    // ventana salia con fondo BLANCO en una aplicacion oscura.
    readonly property color paletaBase: "#1b1b1f"
    readonly property color paletaAlterna: "#222226"
    readonly property color paletaBoton: "#26262b"
    readonly property color paletaMedio: "#3a3a40"
    readonly property color paletaOscura: "#101012"
    readonly property color paletaClara: "#44444b"
    readonly property int radius: 22
    readonly property int radiusSmall: 12
    readonly property int gap: 12
    // UN nombre (viene en data/fonts): font.family no acepta una lista con comas como
    // CSS; con «Inter, Noto Sans, …» caía a la letra del sistema, distinta en cada máquina.
    readonly property string font: "Noto Sans"
    // Para numeros que se leen en columna (tiempos, relojes, contadores): con
    // ancho fijo no bailan cuando cambian.
    readonly property string mono: "JetBrains Mono"

    /// Cuánto hace, legible: «12 s», «5 min», «3 h», «4 días» (antes salía «5826 min»).
    function hace(seg) {
        const s = Math.max(0, Math.round(seg))
        // Frases enteras (no número + unidad pegados): otro idioma puede ordenarlas a su modo.
        if (s < 90) return qsTr("%n s", "segundos, abreviado", s)
        if (s < 90 * 60) return qsTr("%n min", "minutos, abreviado", Math.round(s / 60))
        if (s < 36 * 3600) return qsTr("%n h", "horas, abreviado", Math.round(s / 3600))
        const d = Math.round(s / 86400)
        return qsTr("%n día", "", d)
    }

    /// Una duración en reloj: «4:05», «1:02:05», «-0:12» (para las cuentas y las columnas).
    function mmss(seg) {
        const neg = seg < 0
        const t = Math.abs(Math.round(seg || 0))
        const h = Math.floor(t / 3600), m = Math.floor(t % 3600 / 60), s = String(t % 60).padStart(2, "0")
        return (neg ? "-" : "") + (h > 0 ? h + ":" + String(m).padStart(2, "0") : m) + ":" + s
    }

    /// El color de cada tipo de cue o pieza (la franja de la fila, el chip, el bloque de la línea).
    function colorDeTipo(tipo) {
        switch (tipo) {
        case "escena": return accent
        case "orador": return "#5aa9e6"
        case "grafico": return "#c792ea"
        case "musica": return "#b98cf0"
        case "video": return "#b98cf0"
        case "teleprompter": return "#4fc3a1"
        case "pantalla": return "#4fc3a1"
        case "audio": return amber
        case "espera": return live
        case "dsk": return "#e6a15a"
        case "ptz": return "#5ad1e6"
        case "asamblea": return "#e6d35a"
        case "asistente": return "#8be65a"
        default: return textFaint
        }
    }
}
