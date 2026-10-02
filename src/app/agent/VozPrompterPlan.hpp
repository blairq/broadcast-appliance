// src/app/agent/VozPrompterPlan.hpp — las cuentas de la voz del teleprompter
//
// Lo que no tiene audio ni red, para probarlo solo (test_voz_prompter):
//
//   - las palabras, contadas IGUAL que el gateway (src/gateway/Prompter.cpp:
//     cualquier racha de espacios separa), así la posición del teleprompter
//     y la de la voz son la misma palabra;
//   - las frases: hasta un punto, signo o párrafo, y si son largas, cortadas
//     en una coma o cada `maxPalabras` (cada frase es un punto de
//     resincronización: cuanto más cortas, más fiel al teleprompter);
//   - dónde va el teleprompter en un momento dado (posición + lo que corrió a
//     su ppm desde que la informó) y cuándo llega a una palabra;
//   - a qué velocidad decir una frase para que dure lo que el teleprompter
//     tarda en pasarla.

#pragma once

#include <string>
#include <vector>

namespace ba::app::agent::prompter_voz {

/// Una palabra: dónde empieza y termina en el texto (bytes).
struct Palabra {
    std::size_t ini = 0, fin = 0;
};
/// Una frase: las palabras [desde, hasta).
struct Frase {
    int desde = 0, hasta = 0;
};

std::vector<Palabra> palabras(const std::string& texto);
std::vector<Frase> frases(const std::string& texto, const std::vector<Palabra>& ps, int maxPalabras = 22);
/// El texto de las palabras [desde, hasta), tal cual está (con su puntuación).
std::string textoDe(const std::string& texto, const std::vector<Palabra>& ps, int desde, int hasta);

/// El estado del teleprompter que informa el gateway.
struct Estado {
    double posicion = 0; ///< palabra (con decimales) en `desdeMs`
    int ppm = 0;         ///< palabras por minuto
    bool corriendo = false;
    double desdeMs = 0; ///< reloj del gateway
};
/// Dónde va el teleprompter en `ms` (reloj del gateway).
double posicionEn(const Estado& e, double ms);
/// En qué momento (reloj del gateway) llega a la palabra `w`.
double llegaEn(const Estado& e, double w);
/// La velocidad (1 = la natural de la voz) para que `nPalabras` duren lo que
/// el teleprompter tarda en pasarlas, si la voz dice `naturalPps` palabras
/// por segundo a velocidad 1. Acotada a lo que la voz banca sin deformarse.
double velocidadPara(int nPalabras, int ppm, double naturalPps);

} // namespace ba::app::agent::prompter_voz
