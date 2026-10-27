// ============================================================================
// src/app/registro/AgenteRegistro.hpp — lo que entiende el modelo en un tótem de registro
//
// INICIATIVA MIXTA. El guion (registro/Conversacion) sigue llevando la estructura: pregunta
// campo por campo, confirma, pide la foto y el documento. Es lo que guía a quien no sabe qué
// contestar, y lo que se dice casi siempre (frases conocidas y cortas, al instante). Pero la
// gente no contesta como un formulario: dice todo de una («soy Jorge Ortega, mi correo es…,
// mi teléfono…»), corrige («no, el correo es con jota»), pregunta («¿a qué hora empieza?»).
// Eso lo entiende un modelo de lenguaje (el del rol «registro», agent/Roles; de fábrica el
// local), turno por turno:
//   - los datos los anota por HERRAMIENTAS que validan (`registro_poner`, una por dato, varias
//     en un mismo turno): pasan por los normalizadores del guion (Conversacion::proponer →
//     entender()) y, si no validan, el error vuelve al modelo para que repregunte. El modelo
//     no inventa: lo que no pasó por la validación no se guarda;
//   - `registro_confirmar` cuando la persona dijo que sí a lo que se le estaba confirmando;
//   - el TEXTO que escribe es lo que el guion no sabe decir (la respuesta a una pregunta sobre
//     el evento, con la ficha del tótem; un «perdón, ¿me repetís el correo?»): se dice en voz,
//     frase por frase mientras lo escribe (TrozadorFrases).
// Después de cada turno, el guion retoma (Conversacion::despuesDelModelo): confirma junto lo
// anotado, saltea lo que ya tiene dato y pregunta lo que falta.
//
// Esto es la parte PURA (sin Qt ni red): el troceo en frases, la lectura del streaming (SSE de
// /v1/chat/completions), el pedido de cada turno y las herramientas. Quién lo manda y con qué
// tiempos (y cuándo se cae al guion solo) es registro/AgenteTotem. Probado en
// test_registro_agente.
// ============================================================================
#pragma once

#include <string>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "registro/Totems.hpp"

namespace ba::registro {

class Conversacion;

/// Corta en frases lo que el modelo va escribiendo, para decir la primera mientras escribe
/// la segunda. Corta en . ? ! … ; : seguidos de un espacio (no en «11.222.333» ni en
/// «gmail.com») y en un salto de línea; una frase muy larga sin punto, en la última coma.
class TrozadorFrases {
   public:
    /// Lo nuevo que escribió; devuelve las frases que ya se pueden decir (enteras).
    std::vector<std::string> agregar(const std::string& delta);
    /// Terminó de escribir: lo que quedaba ("" si nada).
    std::string terminar();

   private:
    std::string buf_;
};

/// Saca lo que no se dice en voz alta (los asteriscos y numerales del markdown, emojis no:
/// la voz los saltea igual) y emprolija los espacios.
std::string paraHablar(const std::string& texto);

struct LlamadaHerramienta {
    std::string id;
    std::string nombre;
    std::string argumentos; ///< el JSON tal como vino (se arma de a pedazos en el streaming)
};

/// Una vuelta del modelo: lo que escribió y las herramientas que llamó.
struct TurnoModelo {
    std::string texto;
    std::vector<LlamadaHerramienta> llamadas;
    std::string fin;   ///< finish_reason ("stop", "tool_calls"…)
    std::string error; ///< lo que contestó el servidor si fue un error
};

/// Arma un TurnoModelo con lo que llega de /v1/chat/completions con `stream: true` (eventos
/// «data: {…}» y «data: [DONE]»), de a pedazos como vengan. Si el servidor contestó sin
/// streaming (un JSON entero), terminar() lo lee igual.
class LectorSse {
   public:
    /// Lo que llegó; devuelve el TEXTO nuevo de estos pedazos (para mostrar y decir).
    std::string agregar(const std::string& bytes);
    /// Terminó la respuesta (cerró la conexión): lee lo que haya quedado.
    void terminar();
    bool terminado() const { return fin_; }
    const TurnoModelo& turno() const { return t_; }
    /// Una respuesta sin streaming ({choices: [{message: {content, tool_calls}}]}).
    static TurnoModelo deRespuesta(const nlohmann::json& j);

   private:
    std::string evento(const std::string& linea);
    std::string pend_;
    std::string crudo_; ///< todo, por si no era SSE (ver terminar)
    bool vioData_ = false;
    bool fin_ = false;
    TurnoModelo t_;
};

/// El mensaje de sistema: el prompt del rol con {evento}, {campos} (los del tótem, uno por
/// línea) y {ficha} (la del tótem, o que no hay) reemplazados.
std::string sistemaRegistro(const std::string& prompt, const Totem& t, const std::string& evento);
/// Las herramientas: registro_poner {campo (las claves del tótem), valor}, registro_confirmar y
/// registro_decir {texto} (lo que se le dice a la persona).
nlohmann::json herramientasRegistro(const Totem& t);
/// Hace lo que pidió el modelo contra la conversación. Devuelve el resultado para el modelo
/// ({ok, anotado} o {error}); `rechazo` = no se pudo.
nlohmann::json ejecutarHerramienta(Conversacion& conv, const LlamadaHerramienta& ll, bool& rechazo);

/// Los turnos de una conversación con el modelo. Cada turno arranca con lo que dijo la persona
/// y el estado del guion; si alguna herramienta no validó y el modelo no dijo nada, hay UNA
/// vuelta más con los resultados (para que repregunte con sus palabras).
class AgenteRegistro {
   public:
    /// `prompt`: el del rol (sin reemplazar); `modelo`: el id que pide el servidor.
    AgenteRegistro(std::string prompt, std::string modelo);

    /// El cuerpo del pedido de un turno nuevo: `dicho` es lo que dijo la persona (o un aviso
    /// de la pantalla). Marca en la conversación el comienzo del turno.
    nlohmann::json empezarTurno(Conversacion& conv, const std::string& dicho);

    struct Vuelta {
        bool otraVuelta = false;  ///< mandar `cuerpo` (con los resultados de las herramientas)
        nlohmann::json cuerpo;
        bool dijoAlgo = false;    ///< en todo el turno, el modelo escribió algo (o llamó registro_decir)
        std::string decir;        ///< lo que pidió decir con registro_decir en esta vuelta
        bool rechazos = false;    ///< en todo el turno, alguna herramienta no validó
        nlohmann::json resultados = nlohmann::json::array(); ///< de esta vuelta
    };
    /// Terminó una vuelta del modelo: ejecuta sus herramientas contra la conversación.
    Vuelta alTerminarVuelta(Conversacion& conv, const TurnoModelo& t);

    /// Lo que se habló en el turno (lo que dijo la persona y lo que se le contestó, del
    /// modelo y del guion): va como historia corta en los turnos siguientes, CON las
    /// herramientas que llamó el modelo y sus resultados. Sin ellas el modelo ve una charla en
    /// la que «el asistente» solo habla y nunca anota, y desde el segundo turno lo imita.
    void anotarIntercambio(const std::string& persona, const std::string& asistente);

    const std::string& modelo() const { return modelo_; }

   private:
    nlohmann::json cuerpo(const nlohmann::json& mensajes) const;
    static void conHerramientas(nlohmann::json& cuerpo, const Totem& t);

    std::string prompt_;
    std::string modelo_;
    std::string sistema_; ///< armado en el primer turno (el tótem no cambia en una conversación)
    std::vector<nlohmann::json> historia_; ///< por intercambio, sus mensajes (persona, herramientas, respuesta)
    nlohmann::json mensajes_ = nlohmann::json::array(); ///< los del turno en curso
    nlohmann::json herramientas_ = nlohmann::json::array(); ///< llamadas y resultados del turno en curso
    int vueltas_ = 0;
    bool dijoAlgo_ = false;
    bool rechazos_ = false;
};

} // namespace ba::registro
