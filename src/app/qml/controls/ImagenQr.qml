// El QR de un texto o un enlace: UNO para toda la app (el que pasa el asistente, el de
// la sala y el tablero, el del público, el de los rótulos, los de Enlaces). Lo dibuja
// el proveedor `image://qr` (ui/QrViewModel: QrImageProvider, con la zona silenciosa):
// acá no se arma ningún módulo. Negro sobre blanco (lo que siempre se lee): el fondo
// blanco lo pone quien lo usa.
import QtQuick

Image {
    /// Lo que codifica ("" = nada).
    property string texto: ""
    source: texto !== "" ? "image://qr/texto/" + encodeURIComponent(texto) : ""
    sourceSize: Qt.size(Math.max(64, Math.round(width * 2)), Math.max(64, Math.round(width * 2)))
    fillMode: Image.PreserveAspectFit
    smooth: false
    cache: false
}
