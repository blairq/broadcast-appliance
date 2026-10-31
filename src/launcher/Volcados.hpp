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
// Se guardan los últimos 10 de cada proceso.
//
// En macOS y Linux (F1-12) no hay minidump: el manejador del hijo
// (diag/Caidas.cpp) escribe las pilas de TODOS los hilos, con su nombre, en el
// archivo que le pasa el launcher (Logs/dumps/<A|B>.pila, en BA_CAIDA_CANAL).
// Al ver que el hijo murió, el launcher lo renombra (A-<fecha>-caida.pila), le
// escribe el .json de siempre al lado y atribuye la caída (fault::atribuir):
// el módulo y la pieza que el modo seguro puede apagar (F1-10). Un cuelgue:
// SIGQUIT al hijo, que escribe sus pilas y se termina.
// ============================================================================
#pragma once

#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "fault/ModoSeguro.hpp"

namespace ba::launcher {

struct Volcado {
    std::filesystem::path dmp, json;
    std::string causa; ///< para el cartel: «corrupción del heap en gstd3d12.dll»
    /// De quién fue (el primer módulo que no es del sistema) y la pieza que el
    /// modo seguro puede apagar por eso (va a caidas.jsonl).
    ba::fault::Atribucion atribucion;
};

/// Qué quiere decir el código con que terminó un proceso (128+señal en POSIX), en
/// el idioma del operador: lo muestra la app en el cartel al volver.
std::string causaDeCodigo(std::optional<int> code);

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

    /// Lee un archivo de pilas (POSIX): la pila del hilo que se cayó (con los
    /// símbolos legibles) y su nombre, la señal y la atribución. Público para el test.
    struct Pilas {
        int senal = 0;
        std::string hilo; ///< el nombre del que se cayó
        std::vector<ba::fault::Marco> marcos;
        bool completa = false; ///< terminó con «FIN»
        ba::fault::Atribucion atribucion;
    };
    static Pilas leerPilas(const std::string& texto);

   private:
    std::filesystem::path dir_;
    mutable std::mutex mu_;
    std::map<std::string, std::shared_ptr<Canal>> canales_; ///< Windows
    std::map<std::string, std::filesystem::path> logs_;      ///< POSIX: el registro de cada hijo
    std::map<std::string, std::uint32_t> pids_;              ///< POSIX
};

} // namespace ba::launcher
