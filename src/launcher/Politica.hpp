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

namespace ba::ipc {
class HealthBlock;
}

namespace ba::launcher {

/// Espera antes de relanzar tras `consecutivas` caídas seguidas: 0, 1, 2, 5 s y
/// después `launcher.relanzarMaxS` (10 s; nunca menos de 5) (spec 3.1.8).
int esperaRelanzarS(int consecutivas);

/// El relanzamiento de UN hijo. El backoff ya no se duerme dentro del bucle del
/// watchdog (mientras A esperaba sus 10 s, B se caía y nadie lo veía): se agenda
/// acá y en cada vuelta el bucle pregunta si ya toca. Y las caídas se olvidan:
/// un hijo que anduvo bien `launcher.estableS` vuelve a relanzarse sin espera
/// (antes, en un evento largo, la quinta caída aislada esperaba 10 s en negro).
class Relanzador {
   public:
    /// Se cayó: agenda el relanzamiento con el backoff que toca y cuenta la caída.
    /// :returns: la espera agendada, en ms.
    std::uint64_t caida(std::uint64_t ahoraMs);
    /// Agenda un relanzamiento a `esperaMs` sin contarlo como caída (p. ej. el
    /// hijo dijo que el entorno no le permite andar: se reintenta más tarde).
    void agendar(std::uint64_t ahoraMs, std::uint64_t esperaMs);
    bool agendado() const { return relanzarEn_.has_value(); }
    /// Hay un relanzamiento agendado y ya llegó la hora.
    bool toca(std::uint64_t ahoraMs) const { return relanzarEn_ && ahoraMs >= *relanzarEn_; }
    /// Se lanzó (o se readoptó): sin agenda; desde acá se cuenta la estabilidad.
    void lanzado(std::uint64_t ahoraMs);
    /// Una vuelta del watchdog con el hijo andando. Si lleva `estableMs` desde
    /// que se lanzó y su latido está fresco, las caídas anteriores se olvidan.
    /// :returns: true si recién se olvidaron (para dejarlo en el registro).
    bool vigilar(std::uint64_t ahoraMs, bool lateFresco, std::uint64_t estableMs);
    /// Desde cero (el operador forzó el reinicio).
    void olvidar();
    int consecutivas() const { return consecutivas_; }

   private:
    int consecutivas_ = 0;
    std::optional<std::uint64_t> relanzarEn_;
    std::optional<std::uint64_t> lanzadoEn_;
};

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
