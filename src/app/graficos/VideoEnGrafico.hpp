// ============================================================================
// src/app/graficos/VideoEnGrafico.hpp — video en vivo dentro de un gráfico
//
// La cámara del orador dentro del zócalo, el recuadro del invitado que entra con
// la placa. El gráfico se dibuja de antemano (GraficoSource.hpp: cuadros
// pre-renderizados en otro proceso), así que el video NO puede ir adentro del
// dibujo: va al lado, como otra capa de la escena, y lo que se anima es esa capa
// con la línea de tiempo del gráfico. Lo mueve el motor cuadro a cuadro
// (Composicion: Capa::claves), con su reloj, igual que una transición.
//
// LO QUE DECLARA LA FICHA (grafico.json, o adentro de la descripción para poder
// escribirlo desde el editor):
//
//   "escena": [{
//     "fuente": "{camara}",       // una fuente de la app, o un parámetro de tipo «fuente»
//     "donde": "arriba",          // arriba (de fábrica) o debajo del gráfico
//     "ajuste": "cubrir",         // cubrir (recorta para llenar), contener o estirar
//     "propia": false,            // true: siempre una capa aparte, aunque esa fuente ya esté
//     "claves": [{"ms": 0, "x": 20, "y": 30, "ancho": 160, "alto": 160, "alfa": 0},
//                {"ms": 450, "alfa": 1}],
//     "salida": [{"ms": 0}, {"ms": 300, "alfa": 0}]   // desde que arranca la salida
//   }]
//
// x, y, ancho y alto van en PÍXELES DEL GRÁFICO (los de su tamaño declarado): el
// recuadro se mueve con el gráfico si lo cambian de lugar en la escena. Lo que una
// clave no dice lo hereda de la anterior. `recorte` ({izq, arr, der, aba}) va en
// fracciones de la fuente. `suave` (de fábrica true): el tramo que llega a esa clave
// arranca y frena despacio.
//
// LA FUENTE: si ya está en la escena del gráfico, se anima ESA (y al terminar vuelve a
// como estaba); si no está, se agrega sola mientras dure el gráfico y se saca después.
//
// Esto es lo que se puede probar sin motor: leer la declaración, armar la línea de
// tiempo (entrada, lo quieto, la salida), recortarla al momento en que arranca lo que
// se dibuja y pasarla al lienzo. Quién la usa: el Emisor (al mostrar y sacar), el
// motor de la app (BroadcastEngine: en qué escenas y a qué capa) y el editor (el
// recuadro de la vista previa).
// ============================================================================
#pragma once

#include <nlohmann/json.hpp>

#include <string>
#include <vector>

#include <QJsonObject>
#include <QString>
#include <QStringList>

namespace ba::app::graficos::video {

/// Un punto de la animación, en fracciones del gráfico (0…1) y en su línea de tiempo.
struct Clave {
    int ms = 0;
    double x = 0, y = 0, ancho = 0.25, alto = 0.5;
    double alfa = 1.0;
    double recorte[4] = {0, 0, 0, 0}; ///< izq, arr, der, aba: fracciones de la fuente
    bool suave = true;
};

/// Una capa de video que acompaña al gráfico.
struct Capa {
    std::string fuente; ///< ya resuelta («{camara}» → «Cámara 1»)
    bool arriba = true;
    bool propia = false;
    std::string ajuste = "cubrir"; ///< cubrir | contener | estirar
    std::vector<Clave> claves;     ///< la entrada (desde que aparece)
    std::vector<Clave> salida;     ///< desde que arranca la salida (vacío = un fundido)
};

/// Lo que declara el gráfico: «escena» de la descripción del pedido (lo que está en el
/// editor) o, si no, de la ficha; con las fuentes resueltas con los valores del pedido
/// (`parametros`, que ya traen los por defecto). Vacío si no tiene video. Una capa sin
/// fuente (el parámetro vacío) no va, salvo con `sinFuente` (el editor la muestra igual);
/// si no se entiende, el motivo en `error`.
std::vector<Capa> leer(const QJsonObject& ficha, const QJsonObject& pedido, QString* error = nullptr,
                       bool sinFuente = false);

/// ¿Declara video? (aunque la fuente esté vacía todavía).
bool tieneVideo(const QJsonObject& ficha, const QJsonObject& pedido = {});

/// Los parámetros de tipo «fuente» (fuente de video) de una ficha.
QStringList parametrosDeFuente(const QJsonObject& ficha);

/// La línea de tiempo entera de una capa: la entrada; si se va (salidaEn > 0), quieta
/// hasta salidaEn y desde ahí la salida (la declarada, o un fundido de `salidaDur`).
std::vector<Clave> lineaDeTiempo(const Capa& c, int salidaEn, int salidaDur);

/// La misma, empezando en `ms` (lo que se dibuja arranca ahí: sacar() arranca ya entrado):
/// una clave justo en `ms` con lo que había ahí y las que siguen, corridas a 0.
std::vector<Clave> desde(const std::vector<Clave>& claves, int ms);

/// Dónde está y cómo se ve a los `ms` (la misma cuenta que el motor).
Clave en(const std::vector<Clave>& claves, double ms);

/// Para el motor de la app: [{fuente, arriba, propia, ajuste, claves: [...]}] con las
/// claves ya armadas (en fracciones del gráfico).
nlohmann::json aJson(const std::vector<Capa>& capas, int salidaEn, int salidaDur, int inicioMs);

/// Un rectángulo en píxeles del lienzo.
struct Caja {
    double x = 0, y = 0, ancho = 0, alto = 0;
};

/// Dónde se ve el gráfico en el lienzo: su imagen (`imgAncho`x`imgAlto`) adentro del
/// rectángulo del ítem, sin deformarse y centrada (así lo pone la escena).
Caja dondeSeVe(const Caja& item, int imgAncho, int imgAlto);

/// Las claves de una capa (aJson) para el motor: en píxeles del lienzo, con el recorte en
/// píxeles de la fuente. Con «cubrir» se recorta lo que sobra para llenar el recuadro sin
/// deformar (hace falta el tamaño de la fuente: sin él, se la contiene). `contener`: cómo
/// tiene que ir la capa en el motor.
nlohmann::json clavesEnLienzo(const nlohmann::json& capa, const Caja& grafico, int fuenteAncho, int fuenteAlto,
                              bool* contener);

} // namespace ba::app::graficos::video
