// ============================================================================
// src/gateway/Superficies.hpp — quién está vivo del otro lado
//
// POR QUE EXISTE ESTO. Mandarle una página a un televisor y que el comando
// salga bien NO quiere decir que la página se esté viendo. Pasó de verdad: el
// aparato aceptó la orden, abrió el receptor, y ahí quedó —"force loading"— sin
// cargar nunca; del lado del operador todo decía «mostrando». Un estado que
// miente es peor que no tener estado.
//
// La única forma de saberlo es que lo diga QUIEN LA MUESTRA. Cada superficie
// web del aparato (la pantalla, el carrusel, el teleprompter, los rótulos)
// manda un latido cada pocos segundos con qué es y qué está mostrando. Si deja
// de latir, no está.
//
// ES DE SOLO INFORMAR. No lleva clave porque no hay nada que proteger: lo que
// dice es «existo y muestro esto», que es lo que ya se ve en una pantalla de
// la sala. Y lo que llega de afuera no se cree a ciegas: el texto se recorta,
// la tabla tiene tope, y lo que se guarda son datos, no ordenes.
// ============================================================================
#pragma once

#include <cstdint>
#include <map>
#include <mutex>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace ba::gateway {

class Superficies {
  public:
    /// Cuánto vale un latido. Las páginas laten cada 5 s, así que 15 aguanta
    /// dos perdidos sin declarar muerta una que está bien.
    static constexpr std::int64_t kVivaMs = 15000;
    static constexpr std::size_t kMax = 64;

    /// Una superficie CONECTADA por WebSocket: además de decir que está viva,
    /// se le puede mandar algo. Es la diferencia entre mirar y poder actuar:
    /// con el latido solo sabemos que una pantalla está mostrando algo viejo;
    /// con el canal abierto se le cambia sin volver a castear nada.
    struct Conectada {
        std::string pid; ///< la conexión (para mandarle)
        std::string tipo;
        std::string que;
    };

    /// Una página dice que está viva. `tipo` es qué es ("carrusel",
    /// "pantalla", "prompter"...), `id` la identifica entre recargas, `que`
    /// es lo que está mostrando ahora mismo.
    void latido(const std::string& tipo, const std::string& id, const std::string& que,
                std::uint32_t ip);
    /// Las que latieron hace menos de kVivaMs.
    nlohmann::json vivas() const;

    // --- Las que están conectadas por el canal ---------------------------------
    /// Entra una. `pid` es su conexión.
    void conectar(const std::string& pid, const std::string& tipo, const std::string& id);
    void desconectar(const std::string& pid);
    /// Qué está mostrando (lo dice ella misma por el canal).
    void dice(const std::string& pid, const std::string& que);
    /// La conexión de esa superficie por su `id`, o vacío.
    std::string conexionDe(const std::string& id) const;
    /// Todas las conexiones de ese tipo (para mandarle a todas las pantallas).
    std::vector<std::string> conexionesDe(const std::string& tipo) const;

  private:
    struct Viva {
        std::string tipo, que, ip;
        std::int64_t desdeMs = 0; ///< cuándo se la vio por primera vez
        std::int64_t vistaMs = 0;
        std::uint64_t latidos = 0;
    };
    mutable std::mutex m_;
    std::map<std::string, Viva> por_;
    /// pid -> lo conectado por canal.
    std::map<std::string, Conectada> canal_;
};

} // namespace ba::gateway
