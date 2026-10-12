// ============================================================================
// src/app/evento/Padron.hpp — la gente de un evento
//
// El guion (`Evento`) sabe QUÉ va a pasar. Esto sabe QUIÉN va a estar: la lista
// del organizador, que es el artefacto central de cualquier evento y el único
// que el sistema no tenía.
//
// LO QUE HABÍA NO ALCANZA. `Evento::autoAceptar` es una lista blanca del lobby
// indexada por rostro: sirve para DEJAR PASAR, no para organizar. No dice quién
// falta, ni quién es público y quién orador, ni quién viene en persona, ni
// quién llegó.
//
// DOS ENTIDADES, A PROPÓSITO:
//
//     Persona    (la libreta, permanente)   id, nombre, cargo, foto, caras…
//        ↓
//     Asistente  (de ESTE evento)           papel, canal, estado, contacto
//
// La misma persona es panelista en un evento y público en el siguiente. Si eso
// se guardara como campos sueltos en la libreta, a la tercera transmisión la
// libreta sería un depósito de estados viejos y nadie sabría cuál valía. Los
// datos del evento CADUCAN CON EL EVENTO; la libreta no.
//
// El vínculo es opcional: se puede anotar a alguien que todavía no tiene ficha
// de rostro (casi siempre, cuando se arma la lista antes del evento), y la
// ficha se le engancha después. Por eso cada asistente tiene su PROPIO id.
//
// ARCHIVO APARTE, no adentro de evento.json. Los dos viven en la carpeta del
// evento, pero tienen ritmos distintos: el guion se escribe antes y casi no
// cambia mientras corre; el padrón cambia todo el tiempo DURANTE —cada persona
// que entra por la puerta es una escritura—. Guardarlos juntos haría que cada
// acreditación reescriba el guion entero y deje un respaldo, que es como
// perder el historial de lo que de verdad se editó.
//
//     Eventos/<nombre>/
//     ├── evento.json     el guion
//     └── padron.json     esto
//
// C++ puro, como `Evento`: dice quién es quién, no lo reconoce ni lo hace
// pasar. Así se prueba entero sin prender una cámara.
// ============================================================================
#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace ba::evento {

/// Con qué está en el evento. Ordena la lista y decide qué se le ofrece: a un
/// orador se le pide el discurso, al público no.
enum class Papel { Orador, Panelista, Publico, Prensa, Staff };

/// Por dónde asiste. `SinDefinir` es el estado honesto de casi toda lista
/// recién armada, y vale la pena poder decirlo en vez de suponer.
enum class Canal { SinDefinir, Presencial, Online };

/// En qué punto está. Es el recorrido entero, del primer mail a la salida.
enum class Estado { Invitado, Confirmo, Llego, SeFue };

const char* nombreDePapel(Papel p);
const char* nombreDeCanal(Canal c);
const char* nombreDeEstado(Estado e);
bool papelDeNombre(const std::string& s, Papel& out);
bool canalDeNombre(const std::string& s, Canal& out);
bool estadoDeNombre(const std::string& s, Estado& out);

/// Una persona en ESTE evento.
struct Asistente {
    /// Propio y estable. No es el de la libreta: se puede anotar a alguien
    /// antes de que tenga ficha de rostro, que es lo normal al armar la lista.
    std::string id;
    /// Su ficha en la libreta de personas, o vacío si todavía no tiene. Es lo
    /// que permite reconocerlo en la puerta.
    std::string personaId;
    /// Copia legible, para poder leer la lista sin cruzarla con la libreta.
    std::string nombre;
    Papel papel = Papel::Publico;
    Canal canal = Canal::SinDefinir;
    Estado estado = Estado::Invitado;
    /// Mail o teléfono. Para convocar; no lo usa el sistema.
    std::string contacto;
    /// Para el organizador ("entra por la puerta lateral", "trae su notebook").
    std::string nota;
    /// Si entra solo a la sala sin que el operador apruebe. Necesita
    /// `personaId`: dejar pasar a quien ESCRIBIÓ un nombre es dejar pasar a
    /// cualquiera que sepa escribirlo.
    bool dejarPasarSolo = false;
    /// Si cuenta para el quórum y puede votar. No sale del papel: en una
    /// asamblea un propietario es «público» y vota, y la prensa no.
    bool vota = false;
    /// Su código de votante: corto y para leer en voz alta.
    ///
    /// EN UNA VOTACIÓN FORMAL EL TELÉFONO NO ALCANZA —se borra el sitio y se
    /// vota de nuevo—, así que lo que identifica es esto. Se le da al
    /// acreditarlo. Sin ambigüedades al dictarlo: no lleva O ni 0, ni I ni 1,
    /// que al leerlos en voz alta son la misma letra.
    std::string codigo;
    /// Cuándo se lo acreditó (ms desde la época; 0 = no llegó).
    std::int64_t llegoMs = 0;
    /// Por dónde se lo acreditó: "rostro", "qr", "mostrador", "lobby". Sirve
    /// para saber en qué confiar cuando dos registros se contradicen.
    std::string porDonde;

    nlohmann::json aJson() const;
    static Asistente deJson(const nlohmann::json& j);
};

/// Un código de votante nuevo. Público para poder probarlo.
std::string nuevoCodigo();

/// Cuántos hay de cada cosa. Es lo que contesta «¿quiénes están?» sin obligar
/// a contar a mano una lista de cuarenta.
struct Resumen {
    int total = 0;
    int presentes = 0;    ///< estado Llego
    int presenciales = 0; ///< de los presentes, por la puerta
    int enLinea = 0;      ///< de los presentes, por la sala
    int confirmados = 0;
    int faltan = 0;       ///< invitados o confirmados que todavía no llegaron
    int conVoto = 0;      ///< del padrón entero
    int votoPresente = 0; ///< de los presentes: el quórum
};

class Padron {
   public:
    const std::vector<Asistente>& gente() const { return gente_; }

    /// Agrega a alguien y devuelve su id (se genera si viene vacío).
    std::string agregar(Asistente a);
    /// Cambia lo que venga con valor. Devuelve false si no existe ese id.
    bool cambiar(const std::string& id, const Asistente& campos, const std::vector<std::string>& queCampos);
    bool quitar(const std::string& id);

    const Asistente* porId(const std::string& id) const;
    /// El primero que tenga esa ficha de rostro, o nullptr. Es como la puerta
    /// pregunta «¿quién es este?».
    const Asistente* porPersona(const std::string& personaId) const;
    /// Por su código de votante. Es como llega el que escaneó su invitación.
    const Asistente* porCodigo(const std::string& codigo) const;
    /// El primero cuyo nombre coincida sin distinguir mayúsculas ni acentos.
    /// Para el mostrador, donde se tipea.
    const Asistente* porNombre(const std::string& nombre) const;

    /// Lo marca como llegado. Devuelve false si no existe.
    ///
    /// Acreditar dos veces NO es un error ni mueve la hora: en la puerta se
    /// escanea de nuevo por las dudas todo el tiempo, y la hora que importa es
    /// la de la primera vez.
    bool acreditar(const std::string& id, const std::string& porDonde, std::int64_t nowMs);
    /// Lo marca como que se fue.
    bool despedir(const std::string& id);

    Resumen resumen() const;

    /// Quiénes votan en una votación formal: los que tienen derecho a voto Y ya
    /// llegaron (el que tiene derecho y no vino cuenta para `podian`, el
    /// denominador, pero no vota desde su casa).
    struct Votantes {
        struct Votante {
            std::string codigo;
            std::string nombre;
        };
        std::vector<Votante> habilitados;
        int podian = 0; ///< los que tienen derecho a voto, vinieran o no
    };
    Votantes votantes() const;

    nlohmann::json aJson() const;
    static Padron deJson(const nlohmann::json& j);

   private:
    std::vector<Asistente> gente_;
    Asistente* buscar(const std::string& id);
};

/// Lee `padron.json` de la carpeta del evento. Si no está —que es lo normal
/// hasta que alguien anota al primero— devuelve uno vacío con `ok` en true:
/// no tener padrón todavía no es un error.
Padron cargarPadron(const std::filesystem::path& carpeta, bool* ok = nullptr, std::string* err = nullptr);
/// Lo escribe. Sin respaldos, a diferencia del guion: esto se escribe muchas
/// veces por minuto durante el evento y un respaldo por acreditación llenaría
/// la carpeta de ruido.
bool guardarPadron(const std::filesystem::path& carpeta, const Padron& p, std::string* err = nullptr);

} // namespace ba::evento
