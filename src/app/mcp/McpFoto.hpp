// ============================================================================
// src/app/mcp/McpFoto.hpp — las herramientas que contestan con la interfaz trabada (F3-03)
//
// Una herramienta de lectura corre en el pool (Hilo::Foto) si lee SOLO cosas seguras
// entre hilos. Acá están las de diagnóstico y estado que se arman con eso: status,
// salidas_listar, sistema_recursos, log_leer, logs_tail, eventos_temas,
// eventos_ultimo, mcp_aire_lista y gst_elementos. Son las que se piden cuando algo anda
// mal, que es justo cuando la interfaz puede estar trabada.
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
#include <string>

#include <nlohmann/json.hpp>

namespace ba::app::mcp {

class McpCore;
using json = nlohmann::json;

/// Una copia de algo que solo sabe la interfaz, para leerla desde otro hilo.
///
/// POR QUE PEDIDA Y NO PERIÓDICA. Copiar el estado entero cada segundo «por si alguien
/// pregunta» le cuesta a la interfaz aunque nadie pregunte. Así se copia cuando se
/// pide: con la interfaz libre llega en milisegundos y es la de ahora; trabada, la
/// pedida queda en su cola (una sola, aunque pregunten diez) y se contesta con la
/// última. Lo que se copia es una lectura: hacerla tarde no cambia nada.
class CopiaDeLaInterfaz {
   public:
    /// `leer` corre en la interfaz.
    explicit CopiaDeLaInterfaz(std::function<json()> leer);
    struct Lectura {
        json valor;              ///< null si nunca se copió
        std::int64_t edadMs = -1; ///< cuánto hace que se copió (-1: nunca)
        bool alDia = false;      ///< la copió para este pedido
    };
    /// Desde cualquier hilo: la pide y espera hasta `esperaMs`.
    Lectura leer(McpCore& core, int esperaMs) const;

   private:
    struct Estado;
    std::function<json()> leer_;
    std::shared_ptr<Estado> e_;
};

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
};

/// Registra las herramientas de la foto (ver arriba) en `core`.
void registrarHerramientasDeFoto(McpCore& core, FuentesDeFoto f);

} // namespace ba::app::mcp
