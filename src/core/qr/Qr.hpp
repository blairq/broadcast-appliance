// ============================================================================
// src/core/qr/Qr.hpp — códigos QR con estilo, en un solo lugar
//
// UN SOLO GENERADOR, A PROPOSITO. Un mismo QR de este aparato tiene que salir
// por cinco lados: la pantalla del operador, un televisor por Chromecast (que
// no corre nuestro JavaScript), las páginas del celular, un archivo para
// imprimir, y eventualmente un gráfico al aire. Con dos generadores —uno para
// la vista previa y otro para lo que se entrega— el QR que se diseña no es el
// que sale, y eso se descubre tarde y en papel.
//
// Por eso esto vive en el núcleo (sin Qt): lo usan la app y el
// gateway, y lo prueban los tests sin levantar nada.
//
// SVG Y NO PNG. Un QR son cuadraditos: en SVG escala a un televisor de 55
// pulgadas o a una hoja A4 sin verse borroso, y pesa menos. Donde hace falta
// un mapa de bits (exportar un PNG, la vista previa), se rasteriza ESTE mismo
// SVG: así lo que se ve y lo que se entrega salen de la misma geometría.
//
// LA CORRECCION DE ERRORES no es un lujo: un QR con logo tapa módulos, y sin
// margen de corrección deja de leerse. Por eso poner un logo sube el nivel
// solo (ver `svg`), y no se deja elegir uno más bajo con logo puesto.
// ============================================================================
#pragma once

#include <string>

namespace ba::qr {

/// La forma de los módulos (los cuadraditos del cuerpo).
enum class Puntos { Cuadrados, Redondeados, Circulos, Separados, MuyRedondeados };
/// La forma de los tres ojos de las esquinas. Solo dos, y no por falta de
/// ganas: un ojo CIRCULAR no se lee. El lector encuentra el código barriendo
/// líneas y buscando la proporción 1:1:3:1:1; en un círculo esa proporción
/// sale solo por las líneas del medio. Se probó, decodificando: no pasa. Un
/// estilo que se ve lindo y no escanea es peor que no tenerlo.
enum class Ojos { Cuadrados, Redondeados };
/// El marco alrededor del código.
enum class Marco { Ninguno, Simple, Redondeado };
/// Cuánto daño aguanta antes de dejar de leerse: L ~7%, M ~15%, Q ~25%, H ~30%.
enum class Correccion { Baja, Media, Alta, Maxima };

struct Estilo {
    std::string primario = "#000000";
    /// Vacío = un color liso. Si está, degradado lineal a 45° de primario a
    /// secundario (es lo que hace qr-beaconos, y se ve bien en pantalla).
    std::string secundario;
    /// Vacío o "transparent" = sin fondo. Ojo: un QR sin fondo sobre algo
    /// oscuro no lo lee ningún teléfono.
    std::string fondo = "#FFFFFF";
    Puntos puntos = Puntos::Cuadrados;
    Ojos ojos = Ojos::Cuadrados;
    Marco marco = Marco::Ninguno;
    Correccion correccion = Correccion::Media;
    /// Módulos de silencio alrededor. Menos de 4 y muchos lectores no enganchan.
    int zonaSilenciosa = 4;
    /// Un logo al centro, como fracción del lado (0 = ninguno; tope 0.30).
    /// Se deja el hueco Y se sube la corrección a Máxima.
    double logo = 0;
    /// La imagen del logo, ya lista para incrustar ("data:image/png;base64,…"
    /// o una ruta que el que dibuje sepa resolver). Vacío = solo el hueco.
    std::string logoSrc;
    /// O un ícono del set propio, por su nombre (ver Iconos.hpp). Se guarda
    /// POR NOMBRE, así cambiarlo después es cambiar una palabra. Si hay
    /// ícono, gana sobre `logoSrc`: es lo que se elige en la interfaz.
    std::string logoIcono;
    /// El color del ícono. Vacío = el primario del código.
    std::string logoColor;
    /// Al centro, el logo del evento abierto (su tema, PLAN-TEMA-EVENTO): lo pone en
    /// `logoSrc` quien dibuja (la app con el archivo de Tema/, el gateway con el suyo),
    /// porque la imagen no viaja en la dirección (aConsulta). Un ícono le gana.
    bool logoDelEvento = false;
};

/// El QR como SVG, listo para incrustar o servir. Vacío y el motivo en
/// `error` si el texto no entra (un QR tiene un tope, y con corrección alta
/// entra menos).
std::string svg(const std::string& texto, const Estilo& e, std::string* error = nullptr);

// --- El estilo, de ida y de vuelta por una URL -------------------------------
// Un televisor por Chromecast no corre nuestro código: pide una dirección y
// dibuja lo que le devuelvan. Así que para que el QR que se DISEÑA sea el que
// SALE, el estilo tiene que viajar en la URL.
//
// Las dos mitades viven juntas acá a propósito. Estuvieron separadas —la app
// armaba la dirección y el gateway la leía por su cuenta— y lo que pasó fue
// peor que un error: el gateway ni miraba el estilo y dibujaba un QR negro
// sobre blanco. Se guardaba un código con su ícono y sus esquinas redondeadas,
// se proyectaba, y salía otro. Un desacuerdo entre dos lados no se nota
// cuando el resultado sigue siendo un QR válido.

/// El estilo como parámetros de consulta ("&primario=%23000000&puntos=…").
/// No incluye `texto`: eso lo pone el que arma la dirección.
std::string aConsulta(const Estilo& e);

/// El estilo leído de los parámetros. `buscar` devuelve el valor de una clave
/// (ya des-escapada) o vacío si no está. Lo que falte queda en su valor de
/// fábrica, así una dirección vieja o escrita a mano sigue dando un QR legible.
std::string consultaValor(const std::string& query, const std::string& clave);
Estilo desdeConsulta(const std::string& query);

/// Cuántos módulos de lado tiene el código de ese texto (sin la zona
/// silenciosa), o 0 si no entra. Sirve para avisar "esto va a quedar denso".
int lado(const std::string& texto, Correccion c);

// --- Las cargas: el texto que va ADENTRO del código -------------------------
// Cada aparato que lee QR entiende ciertos formatos. Estos son los que
// entienden todos; inventar uno propio hace que el teléfono muestre texto
// suelto en vez de ofrecer "conectarse a la red".

/// "WIFI:S:<red>;T:WPA;P:<clave>;;" — el teléfono ofrece conectarse solo.
std::string wifi(const std::string& red, const std::string& clave, const std::string& seguridad = "WPA",
                 bool oculta = false);
/// Una tarjeta de contacto (vCard 3.0): el teléfono ofrece agregarlo a la agenda.
std::string contacto(const std::string& nombre, const std::string& telefono, const std::string& email,
                     const std::string& organizacion = {}, const std::string& cargo = {},
                     const std::string& web = {});
/// Una reunión: `plataforma` es "meet", "zoom" o "teams".
std::string reunion(const std::string& plataforma, const std::string& codigo, const std::string& clave = {});
std::string telefono(const std::string& numero);
std::string sms(const std::string& numero, const std::string& texto = {});
std::string correo(const std::string& direccion, const std::string& asunto = {},
                   const std::string& cuerpo = {});

} // namespace ba::qr
