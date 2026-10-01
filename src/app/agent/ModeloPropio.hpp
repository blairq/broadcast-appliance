// ============================================================================
// src/app/agent/ModeloPropio.hpp — un modelo servido por vos, por API OpenAI
//
// QUE ES: la dirección de un servidor propio que habla el protocolo de OpenAI
// (`/v1/chat/completions`), su clave, qué modelo pedirle y los parámetros
// extra que ese servidor entienda.
//
// POR QUE NO HAY UN CLIENTE HTTP PROPIO PARA ESTO. Este aparato no le habla a
// ningún modelo: lanza un agente de consola (`claude`, `opencode`) y le
// conecta el MCP. Escribir acá un cliente de OpenAI sería reimplementar el
// lazo del agente, las llamadas a herramientas, el streaming y el puente al
// MCP —todo lo que ya hace opencode—. Entonces esto NO habla con tu servidor:
// escribe la configuración para que opencode lo haga.
//
// POR QUE LOS PARAMETROS EXTRA SON UN JSON LIBRE Y NO CAMPOS. Cada servidor
// propio acepta lo suyo, y una lista fija de casillas se queda corta el primer
// día. Lo que se ponga acá viaja tal cual a las `options` del modelo. Si ese
// JSON está mal, el que se queja es opencode: acá no se inventa una validación
// que después contradiga a quien de verdad lo usa.
//
// LA CLAVE NO VIVE ACA. Va en `Credenciales/`, como la de cualquier otro
// proveedor, y este archivo solo guarda lo que no es secreto.
// ============================================================================
#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace ba::app::agent {

/// Una forma de pedirle el mismo modelo al mismo servidor.
///
/// POR QUE CANALES Y NO UN MODELO SOLO. Un servidor propio suele servir UN
/// modelo, y lo que cambia entre un uso y otro son los parámetros: cuánto
/// puede pensar, qué temperatura, qué presupuesto. Eso no es «otro modelo»
/// para el servidor, pero sí es otra cosa para quien lo usa —no se atiende un
/// lobby con los mismos parámetros con los que se escribe código—. Cada canal
/// se ofrece como si fuera un modelo distinto, que es como se elige en la
/// interfaz y por rol.
struct Canal {
    std::string id;        ///< corto, para el nombre del proveedor ("primary")
    std::string nombre;    ///< cómo mostrarlo (vacío = el id)
    nlohmann::json extra;  ///< las opciones, tal cual, al proveedor de opencode
    nlohmann::json limite; ///< {context, output}, si tu servidor los declara
    /// Lo que hay que decirle A ESTE modelo, además del prompt del sistema.
    ///
    /// POR QUE POR CANAL. El prompt del sistema describe el aparato y es el
    /// mismo para todos. Esto es para lo que necesita UN modelo en particular:
    /// uno chico o local suele necesitar las reglas más cortas y más
    /// terminantes que uno grande, y lo que a uno le sobra a otro le falta.
    /// Se agrega al final del prompt, no lo reemplaza.
    std::string instrucciones;
    /// Si este canal razona antes de contestar. Va a `reasoning` del modelo:
    /// opencode necesita saberlo para no esperar un bloque de pensamiento que
    /// nunca llega. Tiene que coincidir con `enable_thinking` de `extra`.
    bool piensa = true;
};

struct ModeloPropio {
    std::string url;    ///< baseURL, con /v1 incluido si tu servidor lo usa
    std::string modelo; ///< el id del modelo tal como lo pide tu servidor
    std::vector<Canal> canales;

    /// Si alcanza para armar la configuración: sin dirección, modelo ni un
    /// canal no hay nada que configurar, y ofrecerlo en la lista sería
    /// ofrecer algo roto.
    bool completo() const { return !url.empty() && !modelo.empty() && !canales.empty(); }
    /// Los modelos tal como los nombra opencode: "propio-<canal>/<modelo>".
    std::vector<std::string> comoLosLlamaOpencode() const;

    static ModeloPropio leer(const std::filesystem::path& archivo);
    bool guardar(const std::filesystem::path& archivo) const;
};

} // namespace ba::app::agent
