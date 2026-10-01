// ============================================================================
// src/app/graficos/GraficoSource.hpp — un gráfico animado como fuente de escena
//
// POR QUÉ CUADROS PRE-RENDERIZADOS Y NO DIBUJO EN VIVO. El renderizador vive
// en otro proceso (a propósito: un gráfico roto no puede colgar el aire), y
// mandarle cuadros de 1080p a 30 por segundo entre procesos pide memoria
// compartida y sincronización fina. Pero un gráfico dura segundos y su reloj
// es DETERMINISTA: se puede dibujar entero de antemano y reproducirlo.
//
// Sale más simple, sin nada que pueda trabarse en vivo, y el costo es memoria
// mientras el gráfico está en escena: un zócalo de 900x220 a 30 cuadros por
// segundo son unos 24 MB por segundo de animación. Aceptable para lo que dura
// un zócalo; por eso se le pone un tope y no se permite pre-renderizar cosas
// largas (para eso está un video).
//
// El último cuadro QUEDA: una animación que entra y se sostiene se reproduce
// una vez y después se congela, que es lo que hace un zócalo de verdad.
// ============================================================================
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <QImage>

namespace ba::app::graficos {

constexpr const char* kSourceId = "ba_grafico";

/// Cuánto se puede pre-renderizar. Más que esto no es un gráfico animado, es
/// un video, y hay que usar un video.
constexpr int kMaxSegundos = 15;

struct InstanciaInfo {
    std::uint64_t id = 0;
    std::string nombre;  ///< nombre de la fuente en la escena
    std::string grafico; ///< qué gráfico muestra
    int ancho = 900;
    int alto = 220;
    bool reproduciendo = false;
    int cuadros = 0;
};

/// Registra el tipo de fuente en libobs. Se llama una vez al arrancar.
void registerSourceType();

/// Las instancias que hay en las escenas.
std::vector<InstanciaInfo> instancias();

/// Le da a una instancia los cuadros ya dibujados y arranca la reproducción.
///
/// :param fps: a cuántos cuadros por segundo se dibujaron.
/// :param repetir: para un ticker; para un zócalo va en false y el último
///        cuadro queda fijo.
/// :returns: false si esa instancia no existe.
bool reproducir(std::uint64_t id, std::vector<QImage> cuadros, int fps, bool repetir);

/// Saca el gráfico de pantalla (deja de emitir).
bool ocultar(std::uint64_t id);

/// Deja anotado en la FUENTE qué gráfico muestra.
///
/// Se guarda en los ajustes de libobs, o sea que viaja con la escena y
/// sobrevive a cerrar el programa. Sin esto, elegir un gráfico en Rótulos
/// valía solo hasta el próximo arranque: la fuente volvía al que tuviera
/// escrito y había que elegirlo de nuevo cada vez.
///
/// :returns: false si esa instancia no existe.
bool setGrafico(std::uint64_t id, const std::string& grafico);

} // namespace ba::app::graficos
