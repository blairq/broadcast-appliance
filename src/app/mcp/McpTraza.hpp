// ============================================================================
// src/app/mcp/McpTraza.hpp — la traza completa de MCP: todo lo que entra y sale
//
// POR QUE EXISTE. El registro general anotaba de cada llamada el nombre y
// cuanto tardo: «rostro_guardar (155 ms)». Con eso no se puede contestar
// ninguna de las preguntas que uno realmente se hace cuando algo no funciono:
// con que argumentos la llamaron, que contesto, por que fallo. Un caso real:
// el asistente intentaba admitir a alguien con una herramienta que no existia
// —nunca se habia registrado— y el registro no decia UNA palabra: la llamada
// ni siquiera llegaba a anotarse, porque solo se anotaba al terminar una
// herramienta encontrada. Se veia como «acepte y no lo metio a la sala», sin
// rastro.
//
// POR QUE EN UN ARCHIVO APARTE. Por MCP pasa todo el plano de control, y hay
// clientes que preguntan el estado cada dos segundos. Metido en el registro
// general al nivel de siempre, eso lo ahoga: el operador abre el visor a ver
// por que se corto la transmision y ve mil lineas de `status`. Aca se guarda
// TODO, con su propio archivo y su propia rotacion, y el registro general
// sigue contando lo que le importa a una persona.
//
// LO QUE NO SE ESCRIBE. Los valores que parecen secretos (token, pin, clave)
// salen tapados, y lo largo se recorta: una captura de pantalla vuelve como
// medio megabyte de base64 y una cara son 128 numeros. La traza es para
// entender que paso, no para poder reproducir los datos.
// ============================================================================
#pragma once

#include <cstddef>
#include <filesystem>
#include <string>

#include <nlohmann/json.hpp>

namespace ba::app::mcp {

class Traza {
   public:
    /// La del proceso. Antes de `abrir` no escribe a ningun lado (no falla).
    static Traza& la();

    /// Empieza a escribir en `<dir>/mcp.log`, rotando a `mcp.1.log`.
    void abrir(const std::filesystem::path& dir);
    bool abierta() const;

    /// Una linea cruda, ya armada.
    void anotar(const std::string& texto);

    /// Un JSON listo para leer: sin secretos y recortado.
    ///
    /// :param tope: cuanto texto como mucho. Lo que sobra se resume en
    ///        «…(+N)» para que se vea que habia mas y no parezca completo.
    static std::string resumir(const nlohmann::json& j, std::size_t tope = 900);

   private:
    Traza() = default;
    struct Impl;
    Impl* d_ = nullptr;
};

} // namespace ba::app::mcp
