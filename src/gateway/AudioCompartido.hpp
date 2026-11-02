// ============================================================================
// src/gateway/AudioCompartido.hpp — el audio de retorno se codifica una vez por lo que se OYE
//
// Cada invitado recibe su mix-minus (RetornoLugar.hpp): la mezcla de su salida sin su voz.
// Hasta acá eso eran N codificadores Opus, uno por invitado, aunque casi todos oyeran lo
// mismo: con 40 invitados y uno hablando, el mix-minus de los 39 que callan es la mezcla
// entera (su voz no suma nada), igual para todos. Medido el 2026-11-02 con 40 (hinchada,
// `sample` de ba-gateway): los 40 Opus eran ~68 % de un núcleo, el tramo que más pesaba del
// gateway, y crecía con cada invitado.
//
// Ahora es como el codificador compartido de una conferencia (FreeSWITCH lo hace igual):
//
//   - Por cada bloque de 10 ms, AgrupadorRetorno junta a los que oyen LO MISMO (sus bloques
//     no difieren en más de `kTope`, -60 dBFS de pico: la voz propia de alguien que habla
//     queda muy por encima, el ruido de piso de un micrófono que calla queda por debajo).
//     Cada grupo de dos o más se codifica UNA vez (AudioCompartido en Webrtc.hpp) y cada
//     conexión del grupo manda esos mismos bytes; el que queda solo (el que habla, el que
//     escucha al operador por el IFB, la prueba técnica) sigue con su propio codificador.
//   - Salir del grupo es inmediato (el que empieza a hablar no se oye a sí mismo: en la
//     conexión se reconstruye su paquete con su propio codificador). Entrar pide que oiga
//     lo mismo `kUnirseBloques` seguidos (500 ms): las pausas entre palabras no lo hacen
//     ir y venir, que en el navegador sería un cambio de flujo Opus por pausa.
//   - Cada grupo tiene un número estable (su «etiqueta»): mientras el que lo representaba
//     siga en el grupo, el grupo sigue siendo ese. Es lo que identifica al flujo Opus
//     compartido; cambiar de flujo es una costura en el decodificador del navegador, así
//     que el agrupador prefiere no mover a nadie.
//
// Este archivo es la parte sin libwebrtc (test_audio_compartido). El codificador de paso de
// cada conexión y el compartido están en Webrtc.cpp.
// ============================================================================
#pragma once

#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace ba::gateway {

/// Si dos bloques de audio suenan igual: ninguna muestra difiere en más de `tope`.
bool parecidos(const float* a, const float* b, std::size_t n, float tope);

// --- La marca: cómo sabe el codificador de una conexión de qué grupo es su bloque -----------
//
// libwebrtc no deja pasar nada junto con el audio hasta el codificador: le llegan las
// muestras y un sello RTP propio de la conexión. Así que el bloque lleva la marca ADENTRO:
// en el bit más bajo de sus primeros kCuadrosMarca cuadros, el grupo y el número de bloque
// (con un número mágico y una suma de control: un audio cualquiera no la imita). En esos
// cuadros los dos canales quedan iguales (el promedio), para que la mezcla a mono de
// libwebrtc ((L+R)/2, si el códec negoció mono) deje los bits como estaban. Es ruido a
// -90 dBFS durante 1,3 ms, y solo cuando el bloque es de un grupo.

/// Cuadros (a 48 kHz) que lleva la marca: 64 bits.
inline constexpr std::size_t kCuadrosMarca = 64;

/// Marca el bloque `pcm` (int16 intercalado de `canales`, `cuadros` por canal) como el
/// bloque `bloque` del grupo `grupo` (se guardan los 24 bits bajos del bloque).
void marcarBloque(std::int16_t* pcm, std::size_t cuadros, int canales, std::uint16_t grupo, std::uint64_t bloque);
/// Lee la marca. false si no tiene (o si no alcanza el bloque).
bool leerMarca(const std::int16_t* pcm, std::size_t cuadros, int canales, std::uint16_t& grupo, std::uint32_t& bloque24);
/// El bloque entero más cercano a `referencia` cuyos 24 bits bajos son `bloque24`.
std::uint64_t bloqueCompleto(std::uint32_t bloque24, std::uint64_t referencia);

/// Junta, bloque a bloque, a las sesiones que oyen lo mismo. Sin hilos: lo usa el hilo del
/// retorno de una salida (MediaPump::retornoAudioThread).
class AgrupadorRetorno {
   public:
    /// -60 dBFS de pico: lo que difieren dos mix-minus de dos que callan.
    static constexpr float kTope = 0.001f;
    /// Bloques seguidos oyendo lo mismo para entrar a un grupo (500 ms).
    static constexpr int kUnirseBloques = 50;
    /// Grupos a la vez (los que oyen el mix-minus de la salida y los que oyen el programa).
    static constexpr int kGrupos = 2;
    /// Cuántas sesiones se prueban como representante de un grupo nuevo.
    static constexpr int kCandidatos = 4;

    struct Entrada {
        int sesion = 0;
        const float* pcm = nullptr;
        bool libre = true; ///< false: oye algo propio este bloque (IFB, prueba): va sola
    };

    /// Agrupa el bloque (`n` floats por sesión). Después, grupo(i) dice el de la entrada i.
    void agrupar(const std::vector<Entrada>& entradas, std::size_t n);
    /// La etiqueta (1..kGrupos) del grupo que codifica por la entrada `i`, o 0: se codifica sola.
    int grupo(std::size_t i) const { return i < grupoDe_.size() ? grupoDe_[i] : 0; }
    /// El audio que se codifica por el grupo `etiqueta` en este bloque (nullptr: no está).
    const float* pcmDe(int etiqueta) const;

   private:
    struct Estado {
        int etiqueta = 0; ///< el grupo al que se parece (0: a ninguno)
        int racha = 0;    ///< bloques seguidos en ese grupo
    };
    std::vector<int> grupoDe_;
    int rep_[kGrupos + 1] = {};            ///< sesión que representa a cada grupo (0: ninguna)
    const float* pcm_[kGrupos + 1] = {};   ///< su audio de este bloque
    std::unordered_map<int, Estado> estado_; ///< por sesión
};

} // namespace ba::gateway
