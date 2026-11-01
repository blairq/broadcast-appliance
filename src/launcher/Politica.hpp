// Politica.hpp
//
// Proposito:
//   Lo que el supervisor y el actualizador DECIDEN, sin procesos ni relojes de
//   verdad: cuánto esperar para relanzar un hijo (y cuándo olvidarse de las
//   caídas viejas), si un hijo vivo se readopta y si hay aire. Así se prueba
//   sola (tests/test_politica.cpp) y el bucle del watchdog solo pregunta.
//
// Modelo de threading:
//   Sin estado compartido: un Relanzador por hijo, de un solo hilo (el del
//   watchdog). Las funciones libres leen archivos y BA.Health, nada más.
//
// Estrategia de errores:
//   Nunca lanza. Un al-aire.json ilegible con A vivo cuenta como aire: ante la
//   duda, no se corta nada.

#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>

#include "platform/Relanzador.hpp"

namespace ba::ipc {
class HealthBlock;
}

namespace ba::launcher {

/// Espera antes de relanzar tras `consecutivas` caídas seguidas: 0, 1, 2, 5 s y
/// después `launcher.relanzarMaxS` (10 s; nunca menos de 5) (spec 3.1.8).
int esperaRelanzarS(int consecutivas);

/// El relanzamiento de UN hijo (platform/Relanzador.hpp, la misma pieza que usan los
/// vigías de la sala) con la escalera del launcher. El backoff no se duerme dentro del
/// bucle del watchdog (mientras A esperaba sus 10 s, B se caía y nadie lo veía): se
/// agenda y en cada vuelta el bucle pregunta si ya toca. Y las caídas se olvidan: un
/// hijo que anduvo bien `launcher.estableS` vuelve a relanzarse sin espera (antes, en
/// un evento largo, la quinta caída aislada esperaba 10 s en negro).
class Relanzador : public ba::platform::Relanzador {
   public:
    Relanzador()
        : ba::platform::Relanzador(ba::platform::PoliticaRelanzar{
              .esperaMs = [](int n) { return static_cast<std::uint64_t>(esperaRelanzarS(n)) * 1000; }}) {}
};

/// Cómo se vigila el latido de un hijo. A y B laten desde un hilo propio y arrancan
/// pesado (QML, la sala): se les da `graciaArranqueMs` desde que se lanzan y un cuelgue
/// es `cuelgueMs` (5 s) de latido quieto. El emisor late POR SERVICIO desde el primer
/// instante (src/emisor/LatidoServicio.hpp: solo si su bucle atiende, y también mientras
/// arranca) y lleva el aire: la gracia termina con su primer latido y un cuelgue es
/// `emisor.cuelgueS` (2 s). Antes, un emisor frenado se veía recién a los 15-20 s (la
/// gracia de 20 s del recién relanzado, más 5 s) y la transmisión quedaba en negro.
struct VigiaLatido {
    std::uint64_t cuelgueMs = 5000;
    std::uint64_t graciaArranqueMs = 20000;
    bool graciaHastaElPrimerLatido = false; ///< latido de servicio: latió = ya atiende

    /// ¿Todavía no se mira su latido? `pasadoMs` desde que se lanzó; `latio`: ya latió
    /// alguna vez desde entonces; `primerLatidoMs`: lo más que se espera el primero.
    bool enGracia(std::uint64_t pasadoMs, bool latio, std::uint64_t primerLatidoMs) const {
        if (graciaHastaElPrimerLatido) return !latio && pasadoMs < primerLatidoMs;
        if (pasadoMs < graciaArranqueMs) return true;
        return !latio && pasadoMs < primerLatidoMs;
    }
};
/// La del emisor: gracia hasta el primer latido y `emisor.cuelgueS` (2 s; entre 1 y 120).
VigiaLatido vigiaEmisor();

/// Qué hacer con un hijo que puede seguir vivo de un launcher anterior (spec 3.1.9).
enum class Readopcion {
    Lanzar,    ///< no hay nadie vivo (o no late): se lanza uno nuevo
    Adoptar,   ///< vivo, late y es de esta versión
    Reemplazar,///< vivo pero de OTRA versión y sin aire: cierre ordenado y relanzamiento
    Posponer,  ///< de otra versión con salidas activas: se adopta por ahora y se avisa
};
Readopcion decidirReadopcion(bool vivoYLate, std::uint32_t versionPublicada, std::uint32_t versionEsperada,
                             bool hayAire);

/// ¿Runtime/al-aire.json (el documento de aire de la app, en dos generaciones:
/// persist/Generaciones.hpp) dice que alguna salida transmite o graba? El formato
/// es el de la app (salidas::aJson: los campos sueltos de la salida por omisión
/// y `salidas` con todas); acá solo se mira si hay algo prendido. Sin archivo,
/// o de una corrida cerrada en orden, no hay aire; ilegible, sí (ante la duda).
bool salidasActivas(const std::filesystem::path& alAire);

/// Aire de VERDAD: A vivo y latiendo en BA.Health y al-aire.json con salidas
/// activas. Con A abajo, el archivo dice lo que se va a retomar, no lo que sale.
/// :param runtime: la carpeta Runtime/ de los datos.
bool hayAire(const ba::ipc::HealthBlock& salud, const std::filesystem::path& runtime);

} // namespace ba::launcher
