// ============================================================================
// src/gateway/Prompter.hpp — el teleprompter
//
// Tres vistas de la misma cosa:
//   /prompter          la tablet del orador: solo el texto, grande.
//   /prompter/control  el celular del operador: arrancar, velocidad, avisos.
//   (y el cue `teleprompter` del guion, que carga el discurso desde A)
//
// POR QUE LA POSICION VA EN PALABRAS. Podria ir en pixeles, pero entonces
// dependeria del tamano de letra y del tamano de la pantalla, y el control no
// sabe como es la tablet. En palabras, las dos vistas hablan del mismo lugar
// del texto aunque una sea un celular y la otra un televisor. Ademas sale
// gratis la estimacion de duracion, que es lo que el guion necesita para
// calcular los tiempos (130-150 palabras por minuto en castellano).
//
// POR QUE LA TABLET DESPLAZA SOLA. Si el servidor mandara la posicion cuadro a
// cuadro, cualquier hipo de la red se veria como un tiron en el texto, justo
// cuando alguien lo esta leyendo en vivo. En vez de eso el servidor manda
// DESDE DONDE y A QUE VELOCIDAD, con el momento en que eso empezo, y cada
// vista calcula sola donde va. Si se corta la red, el texto sigue andando.
//
// El estado es del servidor (no de la tablet) para que el orador pueda
// recargar la pagina, o que se le apague, y vuelva al mismo lugar.
//
// QUIEN ESTA CONECTADO. Las vistas piden el estado cada medio segundo, asi
// que no hace falta un canal aparte para saber quien hay: se anota quien
// pidio, cuando y desde donde. Un cliente "esta" si pidio hace poco (ver
// kVivoMs()). Sirve para lo unico que importa antes de un discurso: saber si la
// tablet del atril esta prendida y si alguien tiene el control en la mano.
// ============================================================================
#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>
#include "ajustes/Ajustes.hpp"

namespace ba::gateway {

class Prompter {
   public:
    /// Palabras por minuto de una lectura normal en castellano. Es el valor
    /// con el que se estima cuanto dura un discurso.
    static constexpr int kPpmPorDefecto = 140;

    /// Cuanto vale una visita: las vistas piden cada 500 ms, asi que cuatro
    /// segundos aguantan un par de pedidos perdidos sin que la lista parpadee.
    static std::int64_t kVivoMs() { return ba::ajustes::entero64("prompter.vivoMs", 4000); }
    /// Tope de clientes recordados. Mas que esto es alguien probando URLs.
    static constexpr std::size_t kMaxClientes = 32;

    /// Anota que una vista pidio el estado. `id` lo genera la pagina y lo
    /// guarda; `vista` es "orador" o "control"; `nombre` es opcional y lo
    /// escribe quien la usa ("Atril 1").
    void visto(const std::string& id, const std::string& vista, const std::string& nombre, std::uint32_t ip);

    /// Los que pidieron hace menos de kVivoMs(): [{id, vista, nombre, ip,
    /// haceMs}], los oradores primero.
    nlohmann::json clientes() const;

    /// Carga un discurso. Reinicia la posicion y lo deja PARADO: que un texto
    /// nuevo arranque a correr solo seria la peor sorpresa posible.
    void cargar(const std::string& titulo, const std::string& texto);

    /// Aplica los campos presentes de `cmd`. Devuelve false si no habia
    /// ninguno conocido.
    ///
    /// Campos: corriendo (bool), posicion (palabra, double), ppm (int),
    /// tamano (int, 1..10), espejo (bool), aviso (string), parrafo (int:
    /// saltar al principio de ese parrafo), presentador (lo de la línea de tiempo:
    /// {alAire: {nombre, hastaMs?, desdeMs}, proximo: {nombre}?} o null).
    bool comandar(const nlohmann::json& cmd);

    /// El boton de panico: para en seco donde este. No mueve la posicion, para
    /// que se pueda retomar exactamente ahi.
    void pararEnSeco();

    /// El estado, con `atMs` para que cada vista calcule su propia posicion.
    nlohmann::json estado() const;

    /// Cuanto dura leer lo que esta cargado, a la velocidad actual.
    int duracionSeg() const;

    /// Cuantas palabras tiene un texto (la misma cuenta que usa el prompter).
    static int contarPalabras(const std::string& texto);

   private:
    nlohmann::json estadoLocked() const;
    /// Congela la posicion calculada hasta ahora (antes de cambiar velocidad,
    /// parar o saltar): si no, cambiar la velocidad moveria el texto.
    void fijarPosicion(int64_t ahora);

    mutable std::mutex m_;
    std::string titulo_;
    std::string texto_;
    int palabras_ = 0;
    /// Indices de la primera palabra de cada parrafo (para saltar).
    std::vector<int> parrafos_;
    double posicion_ = 0; ///< palabra en la que va la linea de lectura
    int ppm_ = kPpmPorDefecto;
    bool corriendo_ = false;
    int64_t desdeMs_ = 0; ///< cuando empezo a correr desde `posicion_`
    int tamano_ = 5;      ///< 1..10, lo elige el orador en su tablet
    bool espejo_ = false; ///< para el vidrio del teleprompter
    std::string aviso_;   ///< "redondea", "te escuchamos bajo"
    int64_t avisoMs_ = 0;
    /// Lo del presentador (la línea de tiempo del evento, en la app): cuánto le queda
    /// al paso al aire y qué viene. Lo manda la app cuando cambia; la cuenta la hace
    /// la página con `hastaMs` y el reloj del servidor. null = no corre ninguna línea.
    nlohmann::json presentador_;
    struct Cliente {
        std::string vista;
        std::string nombre;
        std::uint32_t ip = 0;
        int64_t vistoMs = 0;
    };
    std::map<std::string, Cliente> clientes_;
    uint64_t version_ = 0; ///< sube con cada cambio: las vistas detectan saltos
};

/// VARIOS TELEPROMPTERS, cada uno con su texto, su velocidad y su página: el del
/// presidente y el del orador invitado leen cosas distintas al mismo tiempo.
/// «principal» es el de siempre (/prompter/); los demás, /prompter/<id>/. Se crean
/// al primer uso (la app carga un discurso, o alguien abre la página): así no hay
/// una lista que sincronizar con la app, que es la que les pone nombre. La clave
/// del control es la misma para todos.
class Prompters {
   public:
    static constexpr const char* kPrincipal = "principal";
    /// Tope: más que esto es alguien probando direcciones.
    static constexpr std::size_t kMax = 32;

    /// El id normalizado ("" = principal), o "" si no vale (minúsculas, números y
    /// guiones; de 1 a 32).
    static std::string idValido(const std::string& id);

    /// El de `id`, creándolo si no estaba; nullptr si el id no vale o ya hay kMax.
    /// Compartido: si otro hilo lo saca mientras se usa, sigue vivo hasta terminar.
    std::shared_ptr<Prompter> de(const std::string& id);
    /// [{id, titulo, palabras, corriendo, duracionSeg, oradores, controles}], el principal primero.
    nlohmann::json lista() const;
    /// Saca uno (el principal no se saca). false si no estaba.
    bool quitar(const std::string& id);
    /// Lo mismo a todos los que hay (lo del presentador va a cada atril).
    void comandarTodos(const nlohmann::json& cmd);

   private:
    mutable std::mutex m_;
    std::map<std::string, std::shared_ptr<Prompter>> todos_;
    nlohmann::json presentador_; ///< el último que fue a todos (para los que se abren después)
};

} // namespace ba::gateway
