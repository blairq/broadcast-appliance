// ============================================================================
// src/app/agent/Roles.hpp — los roles automaticos: quien hace que, con que
//
// El asistente interactivo (pestaña ✦) es UNA conversacion con la persona que
// dirige. Todo lo que llegaba de afuera a deshora —alguien en el lobby, un
// canal que satura— se le escribia en esa misma conversacion y le rompia lo
// que estuviera haciendo. Los roles son la salida: cada tipo de trabajo que
// necesita criterio y llega solo tiene su rol, con su juego de herramientas y
// su prompt, y el Despachador lo lanza como un agente de UN SOLO TIRO cuando
// hace falta. El interactivo no se entera.
//
// Un rol es:
//   * un juego de herramientas MCP (categorias): lo que NO esta, no lo puede
//     tocar ni por accidente, y no lo paga en tokens;
//   * los disparadores que lo despiertan (tipos de disparador del AlertEngine);
//   * su prompt, que es el CLAUDE.md/AGENTS.md del espacio de trabajo del rol;
//   * como agrupa: una corrida por evento (lobby) o una cada tanto (chat).
// Todo eso lo fija el sistema. Lo que elige el operador —si esta activo, con
// que motor y que modelo— va en Config/roles.json.
//
// Motores mezclados a proposito: recepcion en OpenCode Go, el interactivo en
// la suscripcion de Claude, el tecnico en un modelo por API. No comparten
// techo de uso, que es la estrategia de costo.
// ============================================================================
#pragma once

#include <filesystem>
#include <map>
#include <set>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace ba::app::agent {

struct Rol {
    std::string id;
    std::string titulo;
    std::string descripcion;
    std::vector<std::string> categorias;   ///< las suyas (mas las comunes de Roles::comunes)
    std::vector<std::string> disparadores; ///< tipos de disparador que atiende
    std::string prompt;                    ///< lo propio del rol, de fábrica (sin las reglas comunes: ver Roles::instrucciones)
    int agruparMs = 0;                     ///< 0: una corrida por evento; N: junta lo que llegue en N ms
    int agruparMax = 1;                    ///< tope de eventos por corrida
    // --- Lo que elige el operador --------------------------------------------
    bool activo = false;   ///< apagado de fabrica: nada cambia hasta que alguien lo prende
    std::string proveedor; ///< id de proveedor (ver AgentViewModel); vacio = el del asistente
    std::string modelo;    ///< vacio = el de fabrica del proveedor
    std::string promptPropio; ///< lo que escribió el operador en vez de `prompt`; vacío = el de fábrica
    /// Si además de su juego MCP tiene las HERRAMIENTAS PROPIAS del agente:
    /// consola, leer y escribir archivos, salir a internet.
    ///
    /// APAGADO DE FÁBRICA, y no por desconfianza sino porque es otra cosa: el
    /// juego MCP es lo que el rol sabe hacer del aparato, y esto es la máquina
    /// entera. Un rol que mide audio o atiende el lobby no necesita ni una;
    /// uno que diseña escenas —que baja una imagen, la convierte, la deja en
    /// Medios/— las necesita todas.
    ///
    /// LO QUE HAY QUE SABER AL PRENDERLO: la salvaguarda de aprobaciones
    /// (`mcp.approval`) cubre las herramientas del MCP, no estas. Un rol con
    /// esto prendido toca el disco y la red sin pasar por ahí, en la máquina
    /// del estudio.
    bool consola = false;
};

class Roles {
   public:
    explicit Roles(std::filesystem::path archivo);

    /// El juego de herramientas del ASISTENTE INTERACTIVO.
    ///
    /// NO ES UN ROL: no se despacha, no tiene disparadores y no aparece en la
    /// pestaña Agentes. Es solo un nombre para el mismo filtro por categorias
    /// que ya usan los roles, aplicado al asistente.
    ///
    /// POR QUE EXISTE. El asistente veia las 264 herramientas: 154 KB, ~39.000
    /// tokens de esquemas en CADA pedido, y cada llamada a herramienta es un
    /// pedido nuevo. Medido contra el modelo local: 14,8 s de prefill en frio
    /// con todas, 1,3 s con veinte. Y con 264 opciones elige peor —se lo vio
    /// disparar veinte herramientas juntas para una pregunta simple—.
    ///
    /// QUE QUEDA AFUERA, y por que se puede: el rack de audio (que es del rol
    /// tecnico), las tres plataformas de streaming, el guion del evento, el
    /// prompter y las pantallas externas. Nada de eso se toca mientras se
    /// opera, y cuando hace falta se llega por `help_search`, que encuentra
    /// por sintoma y no solo por nombre.
    static const std::set<std::string>& juegoDelAsistente();

    /// Categorias que ven TODOS los roles: ayuda, estado y las de los propios
    /// agentes (para pedirle algo a otro rol y para dejar constancia).
    static const std::set<std::string>& comunes();

    const std::vector<Rol>& todos() const { return roles_; }
    const Rol* porId(const std::string& id) const;
    /// El primer rol ACTIVO que reclama ese tipo de disparador (o nullptr).
    const Rol* porDisparador(const std::string& tipo) const;

    bool setActivo(const std::string& id, bool activo);
    bool setConsola(const std::string& id, bool consola);
    bool setProveedor(const std::string& id, const std::string& proveedor);
    bool setModelo(const std::string& id, const std::string& modelo);
    /// Lo propio del rol que escribió el operador ("" o igual al de fábrica = el de fábrica).
    bool setPrompt(const std::string& id, const std::string& texto);
    /// El archivo de instrucciones de una corrida: lo propio (el del operador o el de fábrica)
    /// más las reglas comunes, que no se editan.
    static std::string instrucciones(const Rol& r);

    /// rol -> categorias, para McpCore::setRoles.
    std::map<std::string, std::set<std::string>> categoriasPorRol() const;
    /// Todos los roles como JSON (lo que ve la interfaz y `agente_roles`).
    nlohmann::json aJson() const;

   private:
    void leer();
    void guardar() const;

    std::filesystem::path archivo_;
    std::vector<Rol> roles_;
};

} // namespace ba::app::agent
