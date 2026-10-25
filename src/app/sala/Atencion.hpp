// ============================================================================
// src/app/sala/Atencion.hpp — la atención a quien se conecta (reglas, sin Qt)
//
// QUE ES. Las reglas de «Los atiende el asistente» y «Hablando» (Sala y
// Asamblea), sin red, sin cámaras y sin modelos: quién atiende, de qué forma,
// cuándo termina, qué nombre cuenta como «dio su nombre» para anotarlo solo en
// Personas, y los ajustes que se guardan. La orquestación (pedirle a la sala
// que suba su voz, pasarla a texto, decir la respuesta) vive en
// ui/AtencionViewModel; acá está lo que se puede probar entero sin prender nada
// (tests/test_atencion.cpp). Ver docs/funciones/ATENCION.md.
//
// QUIÉN ATIENDE (decidido así, 2026-10-25):
//   1. el rol «recepcion» si está prendido: es un agente de un tiro con su
//      juego de herramientas, hecho para esto;
//   2. si no, el asistente de AUTOMATIZACIÓN (trabaja solo, en segundo plano);
//   3. NUNCA el asistente del evento: ese es la charla con el operador, y
//      meterle encargos del lobby le ensucia la conversación a quien está
//      trabajando.
//   Sin ninguno de los dos, nadie: la opción se apaga sola y se avisa.
//
// CUÁNDO TERMINA: al admitirla (o al irse, o al rechazarla). La atención es
// del LOBBY: adentro ya está en el programa, la voz que le llega es el
// retorno, y una voz sintetizada hablándole ahí sonaría para todos los que
// oyen el retorno. Lo que pasa adentro es del operador y del asistente del
// evento.
//
// HABLANDO, DE A UNO. Hay un solo canal de voz del que atiende hacia la sala
// (BA.Audio.Atendedor): el primero que llega con «Hablando» prendido toma la
// voz; mientras tanto, los demás se atienden por chat. Cuando el de la voz
// termina, la toma el siguiente que se estaba atendiendo por chat.
// ============================================================================
#pragma once

#include <cstdint>
#include <functional>
#include <set>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace ba::app::sala {

/// Quién atiende a los que llegan.
enum class Atendedor { Nadie, Recepcion, Automatizacion };

/// La regla de arriba. `recepcionActiva`: el rol «recepcion» existe y está
/// prendido; `automatizacionCorriendo`: el asistente de automatización está andando.
Atendedor quienAtiende(bool recepcionActiva, bool automatizacionCorriendo);
/// "recepcion" | "automatizacion" | "" (para la interfaz y el MCP).
const char* claveDe(Atendedor a);
/// Dicho para una persona: «el rol de recepción», «el asistente de automatización».
std::string nombreDe(Atendedor a);
/// Por qué no atiende nadie (vacío si alguien atiende).
std::string porQueNadie(Atendedor a);

/// Dónde está quien se conecta.
enum class Ambito { Sala, Asamblea };
const char* claveDe(Ambito a);

/// Por dónde se lo atiende.
enum class Modo { Chat, Voz };
const char* claveDe(Modo m);

/// Cómo terminó una atención.
enum class Fin { Admitido, SeFue };
const char* claveDe(Fin f);

/// Los ajustes que se guardan (Config/atencion.json). «Los atiende el
/// asistente» de la SALA no está acá: vive en la configuración del gateway
/// (la sala la necesita aunque arranque después que el programa; ver
/// ControlSala::atencionAutomatica). La lista de quién entra solo tampoco: es
/// del padrón del evento (`Asistente::dejarPasarSolo`).
struct AjustesAtencion {
    bool salaHablando = false;
    bool asambleaAtiende = false;
    /// La voz con la que atiende (un repo del catálogo de modelos, tipo «tts»;
    /// vacío = la primera bajada), su hablante y su velocidad (0,7 … 1,5).
    std::string vozRepo;
    int vozHablante = 0;
    double vozVelocidad = 1.0;

    nlohmann::json aJson() const;
    /// Lo que no viene, o viene mal, queda en su valor de fábrica.
    static AjustesAtencion deJson(const nlohmann::json& j);
};

/// Los que se están atendiendo y cómo.
class Turnos {
   public:
    struct Turno {
        std::string id;
        std::string nombre;
        Ambito ambito = Ambito::Sala;
        Atendedor quien = Atendedor::Nadie;
        Modo modo = Modo::Chat;
        /// La voz no se pudo armar para esta persona (la sala no la deja):
        /// sigue por chat y no se le vuelve a ofrecer, para no reintentar en
        /// cada vuelta de la sala.
        bool sinVoz = false;
    };
    struct Terminado {
        std::string id;
        Fin fin = Fin::SeFue;
        bool teniaLaVoz = false;
    };

    /// Empieza a atender a `id`. Con `hablando`, toma la voz si está libre
    /// (una sola por vez, y solo en la sala: en la asamblea no hay canal de
    /// voz para el que espera). Si ya se lo estaba atendiendo, devuelve ese turno.
    Turno empezar(const std::string& id, const std::string& nombre, Ambito ambito, Atendedor quien, bool hablando);
    /// Lo que dice la sala ahora: los que esperan y los que están adentro.
    /// Termina los turnos de quien ya no espera: Admitido si está adentro,
    /// SeFue si no está en ninguna de las dos. Solo mira los de `ambito`.
    std::vector<Terminado> actualizar(Ambito ambito, const std::set<std::string>& esperando,
                                      const std::set<std::string>& adentro);
    /// La voz quedó libre: la toma el primero que se atiende por chat en la
    /// sala (en el orden en que llegaron) y `puede` (p. ej. que tenga lugar en el
    /// lobby, por donde sube su voz). Devuelve su id, o "" si no hay nadie.
    std::string pasarLaVoz(const std::function<bool(const std::string& id)>& puede = {});
    /// No se le pudo armar la voz a `id`: vuelve a chat y queda sin voz (ver `sinVoz`).
    void sinVoz(const std::string& id);
    /// «Hablando» se apagó: todos pasan a chat. Devuelve quién tenía la voz ("" si nadie).
    std::string soltarLaVoz();
    /// Termina todos los de un ámbito («Los atiende» se apagó). Devuelve los ids.
    std::vector<std::string> terminarTodos(Ambito ambito);

    /// Quién tiene la voz ("" = nadie).
    std::string conLaVoz() const;
    const Turno* turno(const std::string& id) const;
    const std::vector<Turno>& todos() const { return turnos_; }

   private:
    std::vector<Turno> turnos_; ///< en el orden en que llegaron
};

/// El nombre que dio quien se conecta, si cuenta como nombre: lo escribió él
/// al entrar o se lo corrigió quien lo atendió. NO cuentan los que pone el
/// navegador o un apuro («Invitado 3», «iPhone», una letra, un número):
/// anotar a alguien en Personas con un nombre que no es el suyo es
/// peor que no anotarlo. Devuelve el nombre prolijo (espacios de más
/// afuera) o "".
std::string nombreDado(const std::string& nombre);

} // namespace ba::app::sala
