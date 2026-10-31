// ============================================================================
// src/core/platform/Relanzador.hpp — cuándo relanzar un proceso hijo que se cayó
//
// Lo usan todos los que vigilan un proceso: el launcher (A, B, los emisores), y en la
// sala ba-web, cloudflared y ba-pinggy. Sin procesos ni relojes de verdad (el que llama
// pasa la hora): se prueba solo (src/launcher/tests/test_politica.cpp,
// src/gateway/tests/test_web_supervisor.cpp).
//
//   - ESPERA: cuánto esperar tras la caída número n seguida (la política la elige
//     quien vigila: la escalera del launcher, o duplicar con un tope).
//   - ESTABILIDAD: las caídas se olvidan recién cuando el hijo anduvo sano `estableMs`
//     desde que se lanzó. Olvidarlas apenas vive (ba-web lo hacía a los 250 ms) hace
//     que uno que muere a los 300 ms se relance cada medio segundo para siempre.
//   - CORTACIRCUITO: `cortarTras` caídas seguidas sin un rato estable en el medio y se
//     deja de insistir: se espera `esperaCortadoMs` (minutos) entre intentos y se
//     avisa (cortado()). Un rato estable lo cierra.
//   - MÉTRICAS: cuántas caídas hubo en total.
//
// Sin estado compartido: un Relanzador por hijo, de un solo hilo (el que vigila).
// ============================================================================
#pragma once

#include <cstdint>
#include <functional>
#include <optional>

namespace ba::platform {

struct PoliticaRelanzar {
    /// Espera tras `consecutivas` caídas seguidas (0 = la primera). Sin esto, 0.
    std::function<std::uint64_t(int consecutivas)> esperaMs;
    /// Cuánto tiene que andar sano para que se olviden las caídas (vigilar sin
    /// `estableMs` explícito).
    std::uint64_t estableMs = 10000;
    /// Caídas seguidas para abrir el cortacircuito (0 = nunca).
    int cortarTras = 0;
    /// Con el cortacircuito abierto, la espera entre intentos.
    std::uint64_t esperaCortadoMs = 5 * 60 * 1000;

    /// Duplicar desde `primeroMs` hasta `topeMs()` (el tope puede ser un ajuste en vivo).
    static std::function<std::uint64_t(int)> duplicando(std::uint64_t primeroMs, std::function<std::uint64_t()> topeMs);
};

class Relanzador {
   public:
    explicit Relanzador(PoliticaRelanzar p = {}) : p_(std::move(p)) {}

    /// Se cayó: cuenta la caída y agenda el relanzamiento con la espera que toca (o la
    /// del cortacircuito, si se abrió). :returns: la espera agendada, en ms.
    std::uint64_t caida(std::uint64_t ahoraMs);
    /// Agenda un relanzamiento a `esperaMs` sin contarlo como caída (p. ej. el hijo dijo
    /// que el entorno no le permite andar: se reintenta más tarde).
    void agendar(std::uint64_t ahoraMs, std::uint64_t esperaMs);
    bool agendado() const { return relanzarEn_.has_value(); }
    /// Hay un relanzamiento agendado y ya llegó la hora.
    bool toca(std::uint64_t ahoraMs) const { return relanzarEn_ && ahoraMs >= *relanzarEn_; }
    /// Cuánto falta para el agendado (0 si ya toca o no hay).
    std::uint64_t faltaMs(std::uint64_t ahoraMs) const {
        return relanzarEn_ && *relanzarEn_ > ahoraMs ? *relanzarEn_ - ahoraMs : 0;
    }
    /// Se lanzó (o se readoptó): sin agenda; desde acá se cuenta la estabilidad.
    void lanzado(std::uint64_t ahoraMs);
    /// Una vuelta del vigía con el hijo andando. Si lleva `estableMs` desde que se lanzó
    /// y está `sano`, las caídas anteriores se olvidan (y se cierra el cortacircuito).
    /// :returns: true si recién se olvidaron (para dejarlo en el registro).
    bool vigilar(std::uint64_t ahoraMs, bool sano, std::uint64_t estableMs);
    bool vigilar(std::uint64_t ahoraMs, bool sano) { return vigilar(ahoraMs, sano, p_.estableMs); }
    /// Desde cero (el operador forzó el reinicio).
    void olvidar();
    int consecutivas() const { return consecutivas_; }
    /// El cortacircuito está abierto: se cae seguido y se espera mucho entre intentos.
    bool cortado() const { return cortado_; }
    /// Todas las caídas desde que existe (para las métricas).
    std::uint64_t caidas() const { return caidas_; }

   private:
    PoliticaRelanzar p_;
    int consecutivas_ = 0;
    bool cortado_ = false;
    std::uint64_t caidas_ = 0;
    std::optional<std::uint64_t> relanzarEn_;
    std::optional<std::uint64_t> lanzadoEn_;
};

} // namespace ba::platform
