// Un formulario armado por esquema (la lista de campos que arma el modelo): las
// propiedades de un elemento de GStreamer (VideoGrafos.propiedades/propiedadesPad/
// propiedadesModulo), la configuración de un plugin (Plugins.esquemaConfig) o los
// parámetros de la acción de un equipo (Dispositivos: `parametros`). Cada campo lo
// dibuja CampoFormulario (el formato está ahí).
// Avisa cada cambio con `cambio(nombre, valor)`; no guarda nada por su cuenta.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import BA

ColumnLayout {
    id: form
    property var props: []
    /// Solo estas (por nombre); vacío = todas.
    property var soloEstas: []
    /// Para los rangos enormes de GStreamer (ver CampoFormulario).
    property real piso: -1e7
    property real tope: 1e9
    property int tamano: 12
    property bool etiquetaAlCostado: false
    property bool alEscribir: false
    /// Qué decir cuando no hay ningún campo ("" = nada).
    property string vacio: qsTr("Este elemento no tiene propiedades para ajustar.")
    signal cambio(string nombre, var valor)
    spacing: 6

    Text {
        visible: form.props.length === 0 && form.vacio !== ""
        text: form.vacio
        color: Theme.textFaint; font.pixelSize: 11
    }
    // Por nombre y en su lugar: un valor nuevo (lo que se va escribiendo, lo que cambió en el
    // modelo) actualiza su campo sin rearmar los demás ni sacarle el foco al que se escribe.
    ListaConClave {
        id: campos
        clave: "name"
        lista: form.soloEstas.length > 0 ? form.props.filter(p => form.soloEstas.indexOf(p.name) >= 0) : form.props
    }
    Repeater {
        model: campos
        delegate: CampoFormulario {
            required property var datos
            Layout.fillWidth: true
            c: datos
            piso: form.piso
            tope: form.tope
            tamano: form.tamano
            etiquetaAlCostado: form.etiquetaAlCostado
            alEscribir: form.alEscribir
            onCambio: (valor) => form.cambio(datos.name, valor)
        }
    }
}
