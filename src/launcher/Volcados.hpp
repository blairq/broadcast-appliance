// ============================================================================
// src/launcher/Volcados.hpp — los volcados de A y B, bien hechos y explicados
//
// Cada volcado va a Logs/dumps/ con un .json al lado que dice qué pasó, para
// que quien lo levante (tools/caidas/analizar.py) no tenga que adivinar:
//
//   {componente, tipo ("caída"/"colgado"), codigo, descripcion, modulo,
//    desplazamiento, hilo, hora, volcado, ultimas_lineas}
//
// - Caída: el hijo la avisa por el canal (diag/CaidaCompartida.hpp) y el
//   volcado sale CON la excepción, escrito desde acá (afuera del proceso roto).
// - Cuelgue: el supervisor lo pide cuando el hijo deja de latir (sin
//   excepción: se ve qué estaba haciendo cada hilo).
//
// Se guardan los últimos 10 de cada proceso. Fuera de Windows no hace nada
// (ahí la pila la escribe el manejador de señales al registro).
// ============================================================================
#pragma once

#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>

namespace ba::launcher {

struct Volcado {
    std::filesystem::path dmp, json;
    std::string causa; ///< para el cartel: «corrupción del heap en gstd3d12.dll»
};

class Volcados {
   public:
    explicit Volcados(std::filesystem::path logsDir);
    ~Volcados();
    Volcados(const Volcados&) = delete;
    Volcados& operator=(const Volcados&) = delete;

    /// El canal para el hijo `nombre` ("A"/"B"): las variables de entorno que
    /// hay que pasarle. Crea (o reusa) el canal y deja un hilo esperando.
    std::map<std::string, std::string> canalPara(const std::string& nombre, const std::filesystem::path& logDelHijo);
    /// Lanzado el hijo: de quién son los pedidos que lleguen por su canal.
    void lanzado(const std::string& nombre, std::uint32_t pid);
    /// La caída que avisó el hijo desde que se lanzó (y la olvida).
    std::optional<Volcado> tomarCaida(const std::string& nombre);
    /// ¿Avisó una caída que todavía no se tomó? (un «cuelgue» que es WER
    /// reteniendo al proceso caído no es un cuelgue).
    bool hayCaida(const std::string& nombre) const;
    /// Volcado por cuelgue, desde afuera.
    Volcado porCuelgue(const std::string& nombre, std::uint32_t pid, const std::filesystem::path& logDelHijo);

    struct Canal; // interno (Windows)

   private:
    std::filesystem::path dir_;
    mutable std::mutex mu_;
    std::map<std::string, std::shared_ptr<Canal>> canales_;
};

} // namespace ba::launcher
