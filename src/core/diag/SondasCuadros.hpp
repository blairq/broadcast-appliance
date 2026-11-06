// ============================================================================
// src/core/diag/SondasCuadros.hpp — las sondas del camino de un cuadro, solo en un build de diagnóstico
//
// Para encontrar DÓNDE un video pierde la fluidez hay que anotar el ritmo en cada tramo
// (docs/local/camino-del-cuadro.md): lo que llega de la red, lo que sale del decodificador,
// lo que se guarda para la grilla, lo que se publica, lo que llega a la interfaz… Eso, con
// cada cuadro de cada persona, no va en lo que se usa en un evento: va detrás de la opción
// de compilación BA_SONDAS_CUADROS (apagada de fábrica; `make app SONDAS=1`). Apagada,
// BA_SONDA(…) no genera NADA: ni la llamada, ni el contador, ni el campo, ni la rama.
//
//   BA_SONDA(r->ritmoRed.cuadro(us);)            // una instrucción
//   BA_SONDA(ba::diag::RitmoCuadros ritmoRed;)   // un campo (el struct cambia con la opción:
//                                                //  la opción es de TODO el programa, ba_core PUBLIC)
//   if constexpr (kSondasCuadros) { … }          // un bloque más largo
//
// Lo que queda SIEMPRE es lo del final del camino (lo que se dibuja: VideoItem, el mosaico
// pintado de la grilla y la ventana), para que la app se dé cuenta sola de que un video viene a
// saltos (ui/Fluidez): un intercambio atómico por cuadro dibujado.
// ============================================================================
#pragma once

namespace ba::diag {
#if defined(BA_SONDAS_CUADROS)
inline constexpr bool kSondasCuadros = true;
#define BA_SONDA(...) __VA_ARGS__
#else
inline constexpr bool kSondasCuadros = false;
#define BA_SONDA(...)
#endif
} // namespace ba::diag
