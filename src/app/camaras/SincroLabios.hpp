// ============================================================================
// src/app/camaras/SincroLabios.hpp — medir el retardo entre labios y voz
//
// EL PROBLEMA. Una cámara IP llega con 200 ms a 2 s de retardo (codificación +
// red + buffer), mientras que el micrófono por la placa de audio llega en 10 a
// 50 ms. Resultado: se escucha antes de que se vean los labios. Hay que
// atrasar el audio hasta que coincida — y el número depende de cada cámara, de
// la red y hasta del día.
//
// EL MÉTODO: EL APLAUSO. Es el claquetazo de toda la vida, pero automático.
// Alguien se para frente a la cámara y aplaude; en el audio eso es un
// TRANSITORIO (un salto de nivel muy corto, muy por encima del piso de ruido)
// y en el video un CAMBIO BRUSCO entre cuadros consecutivos (las manos se
// juntan). El retardo es la diferencia entre los dos instantes. Se repite
// varias veces y se toma la MEDIANA, que es lo que aguanta un aplauso mal
// detectado.
//
// Este archivo es C++ puro y no sabe de libobs: recibe MEDICIONES ya hechas
// (instante + intensidad) y devuelve el número. Así se puede probar con
// señales inventadas, que es la única forma de saber que la cuenta está bien
// sin pararse a aplaudir frente a una cámara.
//
// HONESTIDAD SOBRE LA PRECISIÓN. Las cámaras IP tienen jitter: el retardo no
// es una constante. Por eso no se devuelve solo la mediana sino también la
// DISPERSIÓN, y si es alta hay que decirlo en vez de dar un número que no es.
// ============================================================================
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace ba::app::camaras {

/// Una muestra de una señal: cuándo y cuánto. `nivel` es el que corresponda a
/// esa señal (dBFS en el audio, diferencia entre cuadros en el video); lo
/// único que importa es que suba cuando pasa algo.
struct Muestra {
    int64_t ms = 0;
    double nivel = 0;
};

/// Un golpe detectado en una señal.
struct Golpe {
    int64_t ms = 0;
    double fuerza = 0; ///< cuánto se despegó del piso, en unidades de la señal
};

/// Busca golpes: saltos cortos y muy por encima de lo que venía.
///
/// El umbral es RELATIVO al piso reciente, no absoluto: un micrófono de sala y
/// la diferencia entre cuadros de una cámara viven en escalas distintas, y el
/// piso de cada uno cambia con el ambiente.
///
/// :param s: las muestras, ordenadas por tiempo.
/// :param sobrePiso: cuánto tiene que despegarse del piso para contar.
/// :param ventanaMs: cuánto mirar hacia atrás para estimar el piso.
/// :param separacionMs: dos golpes más juntos que esto son el mismo (el eco de
///        un aplauso en una sala grande vuelve a los 30-80 ms).
std::vector<Golpe> buscarGolpes(const std::vector<Muestra>& s, double sobrePiso, int ventanaMs = 500,
                                int separacionMs = 250);

/// Lo que sale de medir.
struct Medicion {
    bool ok = false;
    /// Cuánto hay que ATRASAR el audio, en milisegundos. Positivo = el video
    /// llega después (lo normal con una cámara IP).
    int retardoMs = 0;
    /// Cuánto varían las mediciones entre sí (la mitad del rango entre el
    /// primer y el tercer cuartil). Si es grande, el número no es de fiar.
    int dispersionMs = 0;
    int aplausos = 0;      ///< cuántos se pudieron emparejar
    std::string problema;  ///< por qué no se pudo, o qué mirar con cuidado
    bool confiable = false;///< dispersión chica y suficientes aplausos
};

/// Empareja los golpes del audio con los del video y saca el retardo.
///
/// :param audio: golpes detectados en el audio.
/// :param video: golpes detectados en el video.
/// :param maxRetardoMs: más allá de esto no se considera el mismo aplauso (una
///        cámara IP rara vez pasa de 2 s; aceptar más sería emparejar
///        aplausos distintos y dar un número inventado).
/// :param dispersionMaxMs: por encima de esto la medición se marca como no
///        confiable.
Medicion medirRetardo(const std::vector<Golpe>& audio, const std::vector<Golpe>& video,
                      int maxRetardoMs = 2500, int dispersionMaxMs = 40);

/// La mediana de una lista (la copia y la ordena). 0 si está vacía.
int mediana(std::vector<int> v);

} // namespace ba::app::camaras
