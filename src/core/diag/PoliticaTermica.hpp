// ============================================================================
// src/core/diag/PoliticaTermica.hpp — «priorizar programa» cuando la máquina se calienta
//
// Si la máquina se calienta, el sistema baja la frecuencia y lo primero que se
// nota son cuadros perdidos en el aire. Antes de llegar ahí se apaga lo que no
// es el programa, EN ORDEN, de lo que menos se extraña a lo que más
// (04-plan-unificado.md, F4-05):
//
//   1. superres     la superresolución de las entradas (pasa a modo barato);
//   2. modelos      los modelos locales que no están atendiendo (se sueltan);
//   3. vistas       las vistas previas que nadie está mirando;
//   4. invitados    la resolución del video de los invitados (baja).
//
// NUNCA el programa ni la grabación: no están en la lista.
//
// Cuánto se apaga depende del nivel (platform/Termica.hpp) SOSTENIDO: templado
// apaga el primer paso; serio, hasta el tercero; crítico, todo. Se sube de a
// un paso cada `escalonS` (crítico: de a uno por vuelta) y se vuelve de a uno,
// en orden inverso, recién cuando el nivel lleva `recuperacionS` por debajo:
// sin ir y venir con cada medición. Sin Qt ni reloj propio: la app la llama
// cada segundo con la hora y aplica lo que cambió (los tests, con estados falsos).
//
// QUIÉN ATIENDE CADA PASO (GanchosTermicos): la app pone un gancho por paso
// (superres: la superresolución a barato; modelos: los modelos locales que no
// atienden se sueltan; vistas: las vistas del motor que nadie mira se duermen;
// invitados: se les pide menos resolución) y los ganchos llevan lo aplicado: de
// a un paso, apagando en orden y devolviendo al revés. Un paso sin gancho queda
// en `sinGancho` (lo dice `sistema_recursos`); si el gancho llega después, se
// le aplica lo que ya estaba apagado.
// ============================================================================
#pragma once

#include "platform/Termica.hpp"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace ba::diag {

class PoliticaTermica {
   public:
    struct Config {
        bool activa = true;      ///< ajustes «termica.priorizarPrograma»
        int sostenidoS = 10;     ///< el nivel tiene que durar esto antes del primer paso
        int escalonS = 20;       ///< entre un paso y el siguiente (salvo en crítico)
        int recuperacionS = 60;  ///< por debajo esto antes de devolver un paso
    };

    /// Los pasos, en el orden en que se apagan (claves: no se traducen).
    static const std::vector<std::string>& pasos();
    /// Hasta qué paso se apaga en cada nivel (0: nada).
    static int objetivo(platform::NivelTermico n);

    PoliticaTermica() = default;
    explicit PoliticaTermica(Config c) : cfg_(c) {}
    void configurar(const Config& c) { cfg_ = c; }

    /// Una medición. Devuelve cuántos pasos quedan apagados (0…pasos().size()).
    int actualizar(platform::NivelTermico n, std::int64_t ahoraMs);
    int apagados() const { return apagados_; }
    /// ¿Está apagado ese paso?
    bool apagado(const std::string& paso) const;

    /// {activa, nivel, apagados: [..], siguiente, desde_s}
    nlohmann::json informe(std::int64_t ahoraMs) const;

   private:
    Config cfg_;
    platform::NivelTermico nivel_ = platform::NivelTermico::Desconocido;
    std::int64_t nivelDesdeMs_ = -1;   ///< desde cuándo está en este nivel
    std::int64_t ultimoCambioMs_ = -1; ///< el último paso que se apagó o se devolvió
    int apagados_ = 0;
};

class GanchosTermicos {
   public:
    /// Apaga (`apagar`) o devuelve su paso. true si lo atendió.
    using Gancho = std::function<bool(bool apagar)>;
    /// El gancho de un paso (reemplaza al anterior). Si el paso ya estaba apagado, se lo
    /// apaga ya.
    void poner(const std::string& paso, Gancho g);
    /// Lleva lo aplicado a `apagados` (PoliticaTermica::actualizar), de a un paso. Lo que
    /// cambió, en orden: [(paso, apagar)].
    std::vector<std::pair<std::string, bool>> aplicar(int apagados);
    int aplicados() const { return aplicados_; }
    /// Los pasos apagados que nadie atendió.
    const std::vector<std::string>& sinGancho() const { return sinGancho_; }

   private:
    std::map<std::string, Gancho> ganchos_;
    int aplicados_ = 0;
    std::vector<std::string> sinGancho_;
};

} // namespace ba::diag
