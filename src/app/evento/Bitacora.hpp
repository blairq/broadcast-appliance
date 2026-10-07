// ============================================================================
// src/app/evento/Bitacora.hpp — todo lo que pasa, en la carpeta del evento en curso
//
// El «histórico total» (docs/PLAN-EVENTOS-HISTORICO.md, H2): una sola línea de
// tiempo por evento, en `<evento>/bitacora.jsonl`, de solo agregar. Una línea
// por hecho:
//
//     {"t": 1791386947849, "tipo": "mcp", "origen": "asistente", "texto": "…", …}
//
// Tipos que escribe la app (los cuelga StudioViewModel, sin tocar cada módulo):
//
//     bus        lo que publica el BusEventos (escena al aire, grabación,
//                transmisión, oradores, votación, alarmas…), sin lo que se
//                repite muchas veces por segundo (niveles de audio)
//     mcp        cada herramienta que modifica algo (quién, qué, resultado)
//     botonera   cada botón apretado y lo que pasa con los aparatos
//     guion      cada cue lanzado, pausas, saltos
//     aviso      avisos y errores del registro, de cualquier componente
//     evento     el evento empieza, termina, la app arranca o se cierra
//     config     un archivo de Config/ cambió (con el JSON Patch del cambio;
//                ver FotosConfig)
//
// UN SOLO ESCRITOR, en su hilo: `anotar` no toca el disco (se puede llamar
// desde cualquier hilo, también desde el oyente del registro). El hilo escribe
// en tandas y hace fsync cada segundo: un corte de luz deja la bitácora entera
// hasta el último segundo, y al volver el evento sigue en curso y sigue
// escribiendo en el mismo archivo.
//
// SIN SECRETOS: lo que entra pasa por el limpiador (en la app, el mismo que usa
// la traza del MCP: claves de transmisión, tokens y contraseñas, tapados).
//
// NO REGISTRA NADA EN EL REGISTRO: el registro le avisa a la bitácora, y si la
// bitácora le avisara al registro sería un bucle.
// ============================================================================
#pragma once

#include <condition_variable>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <functional>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include <nlohmann/json.hpp>

namespace ba::evento {

class Bitacora {
   public:
    using json = nlohmann::json;

    /// Las categorías que se pueden apagar (Config/historico.json, «registrar»).
    static const std::vector<std::string>& categorias();

    Bitacora();
    ~Bitacora();
    Bitacora(const Bitacora&) = delete;
    Bitacora& operator=(const Bitacora&) = delete;

    /// A qué archivo va (el bitacora.jsonl del evento en curso). Vacío = a
    /// ninguno: lo que llega se descarta. Lo pendiente se escribe en el archivo
    /// anterior antes de cambiar.
    void setArchivo(const std::filesystem::path& archivo);
    std::filesystem::path archivo() const;

    /// Qué se limpia antes de escribir (secretos, base64 largos). Sin uno, pasa tal cual.
    void setLimpiador(std::function<json(const json&)> fn);

    /// Prende o apaga una categoría (`tipo`). Todas prendidas de fábrica.
    void setRegistrar(const std::string& tipo, bool si);
    bool registra(const std::string& tipo) const;

    /// Agrega un hecho. `datos` se suma a la línea (lo que no sea objeto va en
    /// «valor»). `t`: reloj de pared en ms; 0 = ahora. Cualquier hilo.
    void anotar(const std::string& tipo, const std::string& origen, const std::string& texto,
                const json& datos = json::object(), std::int64_t t = 0);

    /// Espera a que lo pendiente esté en el disco (tests, cerrar la app).
    void vaciar();

    /// Cuántas líneas se escribieron desde que arrancó.
    std::uint64_t escritas() const;

    /// Lee una bitácora del disco. Filtros: desde/hasta (ms, 0 = sin límite),
    /// `tipos` (vacío = todos), `texto` (contenido, sin distinguir mayúsculas),
    /// `limite` (las ÚLTIMAS n). Las líneas rotas (un corte a mitad) se saltean.
    static json leer(const std::filesystem::path& archivo, std::int64_t desdeMs = 0, std::int64_t hastaMs = 0,
                     const std::set<std::string>& tipos = {}, const std::string& texto = {},
                     std::size_t limite = 500);

   private:
    void hilo();

    mutable std::mutex mu_;
    std::condition_variable cv_, cvVacio_;
    std::deque<std::string> cola_;
    std::deque<std::filesystem::path> archivosCola_; ///< uno por cada marca de cambio en la cola
    std::filesystem::path archivo_;       ///< el del hilo escritor
    std::filesystem::path archivoPedido_; ///< el último pedido (lo ven los que anotan)
    bool escribiendo_ = false;
    bool fin_ = false;
    std::set<std::string> apagadas_;
    std::function<json(const json&)> limpiar_;
    std::uint64_t escritas_ = 0;
    std::thread th_;
};

} // namespace ba::evento
