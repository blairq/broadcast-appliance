// ============================================================================
// src/app/mcp/McpFoto.hpp — las herramientas que contestan con la interfaz trabada (F3-03)
//
// Una herramienta de lectura corre en el pool (Hilo::Foto) si lee SOLO cosas seguras
// entre hilos. Acá están las de diagnóstico y estado que se arman con eso: status,
// salidas_listar, sistema_recursos, log_leer, logs_tail, eventos_temas,
// eventos_ultimo, mcp_aire_lista y gst_elementos; y las que leían objetos de la
// interfaz hasta que su dueño los dio sin ella: room_status y network_status (la sala,
// preguntada a B), modo_vivo_estado y supervisor_caidas (la foto de AppViewModel y el
// archivo del supervisor), system_info (archivos) y notificador_listar (con su candado).
// Son las que se piden cuando algo anda mal, que es justo cuando la interfaz puede estar
// trabada.
//
// Las fuentes vienen de afuera (FuentesDeFoto): McpService las arma con el runtime y
// la interfaz, y al lado de cada una dice de dónde sale y por qué se puede leer desde
// el pool. La prueba (test_plano_control) las arma con un estado sintético, traba la
// interfaz, llama cada una y comprueba que contestan en el acto, en el pool, y que no
// cambian nada (los archivos de la carpeta de datos, el documento de aire).
//
// Lo que SOLO sabe la interfaz (la sala, los relojes de las pastillas, la armada de
// cada salida) no se lee desde el pool: se le pide una copia (CopiaDeLaInterfaz) y se
// espera un rato corto. Si no llega, va la última con su edad (`interfaz.al_dia`,
// `interfaz.edad_ms`), y lo que sale al aire se pisa con lo seguro (el documento de
// aire, la foto de las salidas del motor): eso nunca es viejo.
// ============================================================================
#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>

#include <nlohmann/json.hpp>

namespace ba::app::mcp {

class McpCore;
struct Tool;
using json = nlohmann::json;

/// Una copia de algo que solo sabe la interfaz, para leerla desde otro hilo.
///
/// POR QUE PEDIDA Y NO PERIÓDICA. Copiar el estado entero cada segundo «por si alguien
/// pregunta» le cuesta a la interfaz aunque nadie pregunte. Así se copia cuando se
/// pide: con la interfaz libre llega en milisegundos y es la de ahora; trabada, la
/// pedida queda en su cola (una sola, aunque pregunten diez) y se contesta con la
/// última. Lo que se copia es una lectura: hacerla tarde no cambia nada.
///
/// Con argumentos (el pronóstico de una salida, la serie de un destino), una copia por
/// cada juego de argumentos: la última de ESOS argumentos (se guardan las de los
/// últimos kCopiasPorArgumentos juegos).
class CopiaDeLaInterfaz {
   public:
    /// `leer` corre en la interfaz.
    explicit CopiaDeLaInterfaz(std::function<json()> leer);
    explicit CopiaDeLaInterfaz(std::function<json(const json& args)> leer);
    struct Lectura {
        json valor;              ///< null si nunca se copió
        std::int64_t edadMs = -1; ///< cuánto hace que se copió (-1: nunca)
        bool alDia = false;      ///< la copió para este pedido
    };
    /// Desde cualquier hilo: la pide y espera hasta `esperaMs` (`esperaPrimeraMs` si
    /// nunca se copió: no hay una vieja que dar, así que vale la pena esperar más).
    Lectura leer(McpCore& core, int esperaMs, const json& args = json::object(), int esperaPrimeraMs = -1) const;
    /// EN LA INTERFAZ: la copia de `args`, ya. Para lo que no puede faltar recién arrancada
    /// la app (sin copia, la primera pregunta con la interfaz ocupada esperaba y fallaba)
    /// y para que su dueño la deje al día cuando cambia (la línea, al correr).
    void copiarYa(const json& args = json::object()) const;
    static constexpr std::size_t kCopiasPorArgumentos = 32;

   private:
    struct Estado;
    struct Copias;
    /// La copia de esos argumentos (la crea; olvida las de los que hace más que no se piden).
    static std::shared_ptr<Estado> estadoDe(Copias& c, const json& args);
    std::function<json(const json&)> leer_;
    std::shared_ptr<Copias> c_;
};

/// Cuánto espera una herramienta la copia de la interfaz (ajustes `mcp.esperaCopiaMs`,
/// 250, y la primera vez `mcp.topeInterfazMs`, 5000: lo mismo que esperaba antes).
struct EsperaDeLaCopia {
    int ms = 250;
    int primeraMs = 5000;
    static EsperaDeLaCopia deLosAjustes();
};

/// Lo que da una lectura hecha en la interfaz: el dato, o el error de la herramienta.
struct LeidoEnLaInterfaz {
    json dato;
    std::string error; ///< no vacío: la herramienta contesta este error
};

/// Pasa `t` (de lectura) al pool: su dato lo copia `leer` en la interfaz, una copia por
/// argumentos (CopiaDeLaInterfaz), y va con `interfaz` {al_dia, edad_ms}: trabada, la
/// última con su edad. Para lo que es SOLO de la interfaz y no tiene foto propia (el
/// pronóstico de la línea, la salud de los destinos, los monitores del sistema).
/// Devuelve la copia (para sembrarla con copiarYa).
std::shared_ptr<CopiaDeLaInterfaz> copiarDeLaInterfaz(McpCore& core, Tool& t, std::function<LeidoEnLaInterfaz(const json& args)> leer,
                                                      EsperaDeLaCopia espera = EsperaDeLaCopia::deLosAjustes());
/// {al_dia, edad_ms} de una copia (lo que va en `interfaz`).
json estadoDeLaCopia(const CopiaDeLaInterfaz::Lectura& c);

/// Lo que leen las herramientas de la foto. Todo desde cualquier hilo, salvo lo que
/// dice «en la interfaz» (eso pasa por CopiaDeLaInterfaz). Lo que falta, no se informa.
struct FuentesDeFoto {
    /// El documento de aire (salidas::aJson de su foto): qué sale de cada salida, el negro
    /// y la placa, la línea.
    std::function<json()> aire;
    std::function<bool()> hayAire;
    /// El latido de lo que sale (LatidoAire::estado).
    std::function<json()> latido;
    /// El modo seguro del proceso: {activo, nivel, apagadas, escenas}.
    std::function<json()> modoSeguro;
    /// La foto que publica el hilo de salidas del motor (BroadcastEngine::fotoSalidas).
    std::function<json()> salidasMotor;
    /// [{id, nombre, por_omision}] (AppRuntime::salidasPublicadas).
    std::function<json()> salidasPublicadas;
    /// diag::Recursos::informe (historial en segundos, paso).
    std::function<json(int historialS, int pasoS)> recursos;
    /// Lo que copia la vuelta de los recursos (en la interfaz, una por segundo):
    /// {transporte: [...], termica: {...}}.
    std::function<json()> servicio;
    /// La deriva de cada placa de audio (motor::derivaDePlacas).
    std::function<json()> derivaPlacas;
    /// Los elementos de GStreamer que hacen falta (video::revisarElementosGst).
    std::function<json()> elementosGst;
    /// El bus de eventos (control::BusEventos: temas y último valor).
    std::function<json(const std::string& patron)> temas;
    std::function<json(const std::string& tema)> ultimoTema;
    /// La carpeta de los registros (<datos>/Logs).
    std::filesystem::path logs;
    /// En la interfaz: el estado que solo ella sabe (el `status` de siempre) y el
    /// resumen de cada salida (resumenSalidas).
    std::function<json()> statusInterfaz;
    std::function<json()> salidasInterfaz;
    /// Cuánto se espera la copia de la interfaz antes de contestar con la última.
    int esperaInterfazMs = 250;

    // --- Lo que la interfaz tenía y ahora da su dueño sin ella ---------------------------
    /// Un GET a la API de B sin pasar por la interfaz (ConferenceClient::pedirSinInterfaz):
    /// el cuerpo, o nada si B no contesta. La sala es de B: la copia de la interfaz
    /// (ConferenceClient::snapshot) es la misma cosa, pero se queda quieta si ella se traba.
    std::function<std::optional<json>(const std::string& ruta)> sala;
    /// El modo en vivo (AppViewModel::modoVivoEstado: su foto y el motor, desde cualquier hilo).
    std::function<json()> modoVivo;
    /// Las últimas `n` caídas (Runtime/caidas.jsonl, AppViewModel::caidas) y la de recién
    /// (la foto de AppViewModel: {} si no hay).
    std::function<json(int n)> caidas;
    std::function<json()> caidaReciente;
    /// La carpeta del sistema, las credenciales y los respaldos (SystemViewModel::info: archivos).
    std::function<json()> sistema;
    /// Los notificadores (agent::Notificador::lista, con su candado).
    std::function<json()> notificadores;
};

/// Registra las herramientas de la foto (ver arriba) en `core`.
void registrarHerramientasDeFoto(McpCore& core, FuentesDeFoto f);

} // namespace ba::app::mcp
