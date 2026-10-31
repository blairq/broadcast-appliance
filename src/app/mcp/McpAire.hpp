// SPDX-License-Identifier: GPL-2.0-or-later
//
// Herramientas "de aire": las que cambian lo que sale (cortar, cambiar de
// escena, sacar a alguien, parar la transmisión…). Mientras hay aire
// (transmitiendo, grabando o la sesión en vivo), una de estas pedida por algo
// AUTOMÁTICO (el asistente, un plugin, una receta, un disparador, la voz)
// espera SIEMPRE la confirmación de una persona, sin importar la política de
// aprobación, el rol ni si la sesión leyó contenido de invitados.
//
// DE DÓNDE SALE CUÁLES SON (F3-02). Cada herramienta lo declara al registrarse:
// `t.clase = Clase::Aire`, con `t.porQueAire` (lo que muestra mcp_aire_lista) y,
// si cuenta solo con ciertos argumentos, `t.aireSi`. Acá no hay una lista de
// nombres: estaba, y la misma herramienta figuraba en cuatro lugares (la lista
// fija, la de las condicionales, la condición por nombre y la de lo reversible);
// agregar una de aire era acordarse de las cuatro. Ahora McpAire le pregunta al
// registro (McpCore) y lo único que vive acá es lo que no es de una herramienta:
// los agregados de Config/mcp-aire.json y qué quiere decir cada condición.
//
// Las declaradas no se pueden bajar; se pueden sumar otras (Config/mcp-aire.json).
#pragma once

#include <nlohmann/json.hpp>

#include <filesystem>
#include <functional>
#include <map>
#include <mutex>
#include <set>
#include <string>

namespace ba::app::mcp {

struct Tool;
class McpCore;

enum class ClaseAire { Normal, Aire, AireSi };

/// Cuándo cuenta como de aire una herramienta de clase `aire` (Tool::aireSi). Cada
/// valor es una regla sobre los argumentos; el resto del tiempo la herramienta es
/// de escritura común.
enum class AireSi {
    Siempre,
    AjustesQueCortan, ///< settings_set: gateway.*, video.*, encoder.*, stream.* (rearman o cortan)
    RelojNuevo,       ///< video_reloj: cambiar tipo/servidor/puerto/dominio rearma el motor
    MasterOPrograma,  ///< audio_set: el máster o el programa (o sin fuente)
    SacarDelAire,     ///< speaker_on_air: sacar a alguien (on/al_aire = false)
    AlActivar,        ///< aire_ftb, aire_placa: ponerla (sacarla no es de aire)
    ChatEnFlujo,      ///< chat_overlay_add en modo flujo (sin elegir los mensajes)
    BancaDirecto,     ///< banca_al_aire en modo directo (corta la escena)
    CamaraForzada,    ///< PTZ: con `forzar` y la cámara al aire (el interlock frena el resto)
};

class McpAire {
   public:
    /// `registro`: el McpCore dueño (de ahí sale la clase declarada de cada herramienta).
    explicit McpAire(const McpCore& registro) : registro_(registro) {}

    /// Lee los agregados de `config` (Config/mcp-aire.json).
    void cargar(const std::filesystem::path& config);

    /// La clase de `herramienta` (declarada o agregada).
    ClaseAire clase(const std::string& herramienta) const;
    /// ¿Esta llamada cambia el aire? (resuelve las "aire_si" con sus argumentos).
    bool esDeAire(const std::string& herramienta, const nlohmann::json& args) const;

    /// [{herramienta, clase, fija, por_que}] (las que no son normales).
    nlohmann::json lista() const;
    /// Agrega (clase "aire") o quita (clase "normal") una herramienta. Las
    /// declaradas de aire no bajan. "" o el error.
    std::string ajustar(const std::string& herramienta, const std::string& clase);

    /// Lo que hace que una herramienta AireSi::CamaraForzada cuente (la cámara está
    /// en la escena al aire): lo pone la app.
    void setCondicion(std::function<bool(const std::string&, const nlohmann::json&)> fn) {
        condicion_ = std::move(fn);
    }

    /// El nombre de una regla (para mcp.json y la ayuda): "siempre", "ajustes_que_cortan"…
    static const char* nombre(AireSi cuando);

   private:
    /// La herramienta declarada de aire con ese nombre, o nullptr.
    const Tool* declarada(const std::string& herramienta) const;

    const McpCore& registro_;
    std::filesystem::path archivo_;
    mutable std::mutex mu_;
    std::set<std::string> agregadas_;
    std::function<bool(const std::string&, const nlohmann::json&)> condicion_;
};

} // namespace ba::app::mcp
