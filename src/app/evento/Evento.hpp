// ============================================================================
// src/app/evento/Evento.hpp — el guion de un evento (storyboard)
//
// Hasta acá el sistema sabía EMITIR. Lo que no sabía es cómo va a ser el
// evento: qué pasa, en qué orden, cuánto dura y quién habla. Eso es un
// `Evento`: una lista de BLOQUES (apertura, himno, discurso, video, cierre) y
// dentro de cada uno una lista de CUES, que son las cosas concretas que pasan
// (poner una escena, arrancar un tema, dar la palabra).
//
// Este archivo es C++ puro: dice QUÉ hay que hacer y CUÁNDO, no lo hace. Quien
// ejecuta (libobs, el rack de audio, la sala) vive arriba. Esa costura es a
// propósito: el guion se puede probar entero sin prender una cámara, que es
// justo lo que un ensayo necesita.
//
// UN EVENTO ES UNA CARPETA, no un registro suelto:
//
//     Eventos/<nombre>/
//     ├── evento.json         este guion
//     ├── Medios/             música, videos y placas propias del evento
//     ├── Discursos/          los textos del teleprompter
//     └── Respaldos/
//
// Así se lo lleva entero a otra máquina (o al equipo de respaldo) y se lo
// puede versionar.
//
// LOS TIEMPOS. Cada cue tiene una duración PREVISTA; la del bloque es la suma
// y la del evento también. Con la hora de arranque, cada bloque y cada cue
// tienen su hora prevista. Mientras corre, lo que importa no es el reloj sino
// el DESVÍO: «vamos 3:20 tarde». Eso es lo que mira el que conduce.
//
// AUTOMÁTICO O MANUAL. Un cue automático encadena con el que sigue; uno manual
// espera el clic. Por defecto los cues son MANUALES: en vivo, que algo salga
// solo cuando no lo esperabas es peor que llegar tarde.
// ============================================================================
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace ba::evento {

/// Lo que un cue hace. El motor de arriba traduce cada tipo a acciones; acá
/// solo se nombran y se validan, para que un guion mal armado se vea ANTES.
enum class Tipo {
    Escena,       ///< pone una escena al aire (con su transición)
    Musica,       ///< arranca o para un tema, con fundido y volumen objetivo
    Video,        ///< reproduce un video y qué hacer al terminar
    Teleprompter, ///< carga un texto y lo deja listo o lo arranca
    Orador,       ///< pone a alguien al aire, con su tiempo asignado
    Grafico,      ///< saca al aire un grafico animado (o lo esconde)
    Pantalla,     ///< manda contenido a una pantalla
    Audio,        ///< cambia el rack, silencia, hace ducking
    Pausa,        ///< espera N segundos
    Espera,       ///< espera a que el operador confirme
    Nota,         ///< no hace nada: es una indicación para el operador
};

const char* nombreDeTipo(Tipo t);
/// ¿El programa sabe ejecutar este tipo de cue? Una sola lista para el ensayo,
/// la revisión y la ejecución (antes estaban copiadas y no coincidían).
bool ejecutable(Tipo t);
/// Devuelve false si el texto no nombra ningún tipo conocido.
bool tipoDeNombre(const std::string& s, Tipo& out);

/// Una cosa que pasa. `datos` lleva lo propio de cada tipo (la escena, el
/// archivo, el participante); se guarda tal cual para que agregar un tipo
/// nuevo no obligue a migrar los eventos ya escritos.
struct Cue {
    std::string id;
    Tipo tipo = Tipo::Nota;
    std::string texto; ///< lo que lee el operador en la vista de conducción
    nlohmann::json datos = nlohmann::json::object();
    int duracionSeg = 0;     ///< previsto; 0 = no aporta al cálculo
    bool automatico = false; ///< true: encadena solo con el siguiente

    /// Lo que este cue necesita para poder ejecutarse, por tipo. Vacío = está
    /// completo. Es la mitad barata de la revisión previa: se ve sin tocar el
    /// disco ni el motor.
    std::string queLeFalta() const;
};

/// Un tramo del evento con nombre propio.
struct Bloque {
    std::string id;
    std::string nombre;
    std::string nota; ///< para el operador (no sale al aire)
    std::vector<Cue> cues;

    int duracionSeg() const;
};

/// El guion entero.
/// Alguien que entra SOLO, sin que el operador tenga que aprobarlo.
///
/// POR QUE VA EN EL EVENTO Y NO EN UN AJUSTE GENERAL. Los que tienen que
/// entrar son los de ESTA transmisión. Una lista global haría que el panelista
/// del programa de ayer entre solo al de mañana, y nadie se acordaría de que
/// esa lista existía.
///
/// SE IDENTIFICA POR LA FICHA DE ROSTRO, no por el nombre que escribe al
/// entrar: el nombre lo escribe cualquiera, y dejar pasar solo a quien «se
/// anotó como Ana Pérez» es dejar pasar a cualquiera que sepa escribir eso.
/// `personaId` es el id en la libreta de personas; el reconocimiento tiene que
/// decir que es ella con el margen de siempre.
struct Invitado {
    std::string personaId; ///< id en la libreta de personas (rostros)
    std::string nombre;    ///< copia legible, para poder leer la lista
    std::string nota;      ///< por qué está acá ("panelista", "camarógrafo")
};

struct Evento {
    std::string nombre;
    /// Día previsto, "YYYY-MM-DD" (vacío = no hay día fijo).
    ///
    /// Sin esto el evento no se puede convocar ni archivar: "empieza 15:00" no
    /// ubica a nadie. Y con gente conectándose de otro huso, tampoco alcanza
    /// la hora sola (ver `huso`).
    std::string fecha;
    /// Hora prevista de arranque, "HH:MM" (vacío = no hay hora fija y los
    /// tiempos se cuentan desde que se arranca).
    std::string inicio;
    /// Huso horario del evento: nombre IANA ("America/Argentina/Buenos_Aires").
    /// Vacío = el del aparato.
    ///
    /// SE GUARDA CON EL EVENTO, no se toma del reloj de la máquina. Este
    /// aparato es portátil y viaja: un evento armado acá y corrido desde otra
    /// máquina en otro lado tiene que seguir diciendo la misma hora, y el
    /// invitado remoto tiene que poder saber qué hora es ESO para él.
    ///
    /// Se guarda el NOMBRE de la zona y no el desfasaje (-03:00): el desfasaje
    /// cambia con el horario de verano, así que uno escrito hoy miente para un
    /// evento de dentro de tres meses.
    std::string huso;
    /// Lo que usa este evento (lo leen la página web, el asistente y la interfaz):
    /// `invitados` = hay invitados en la sala de videoconferencia (la página ofrece «Entrar
    /// a la sala»); `preguntas` = el público puede preguntar y votar (/publico/).
    bool invitados = false;
    bool preguntas = false;
    /// Es una sesión de asamblea (un cuerpo con bancas): la página muestra integrantes y
    /// presentes, quórum, orden del día y los resultados oficiales de las votaciones.
    bool asamblea = false;
    std::string nota;
    std::vector<Bloque> bloques;
    /// A quiénes dejar pasar sin preguntar (ver `Invitado`). Vacía = a nadie,
    /// que es lo que corresponde por defecto: hacer pasar gente sola es una
    /// decisión que alguien tiene que tomar a propósito.
    std::vector<Invitado> autoAceptar;

    int duracionSeg() const;
    /// Segundos desde el arranque del evento hasta el principio del bloque.
    int offsetDeBloqueSeg(std::size_t bloque) const;
    /// Ídem hasta el principio de un cue. Fuera de rango: devuelve -1.
    int offsetDeCueSeg(std::size_t bloque, std::size_t cue) const;
    /// Cuántos cues tiene en total.
    std::size_t cantidadDeCues() const;

    nlohmann::json aJson() const;
    static Evento deJson(const nlohmann::json& j);
};

/// true si el texto es un día que existe de verdad, en formato "YYYY-MM-DD".
/// Un vacío NO es válido: quien pregunta ya decidió que hay fecha.
bool fechaValida(const std::string& s);
/// true si el texto es "HH:MM" con hora 0-23 y minuto 0-59.
bool horaValida(const std::string& s);

/// Un problema encontrado en la revisión previa. `bloque`/`cue` ubican dónde
/// (con -1 para lo que es del evento entero) para poder llevar al operador
/// justo ahí en vez de mostrarle una lista que no sabe dónde mirar.
struct Reparo {
    int bloque = -1;
    int cue = -1;
    bool grave = false; ///< true: así no se puede correr; false: conviene mirarlo
    std::string texto;
};

/// La mitad de la revisión previa que se puede hacer sin tocar nada: cues
/// incompletos, bloques vacíos, tiempos que no cierran. La otra mitad (que los
/// archivos existan, que las escenas existan, que los invitados estén
/// conectados) necesita el motor y va aparte.
std::vector<Reparo> revisar(const Evento& e);

/// Dónde está parado el guion. Un cue por vez, identificado por bloque y cue.
struct Posicion {
    int bloque = -1;
    int cue = -1;

    bool valida() const { return bloque >= 0 && cue >= 0; }
    bool operator==(const Posicion& o) const { return bloque == o.bloque && cue == o.cue; }
    bool operator!=(const Posicion& o) const { return !(*this == o); }
};

/// El guion corriendo: dónde está parado, desde cuándo, y cuánto se desvió de
/// lo previsto. No ejecuta nada — dice qué cue toca y quien sabe hacerlo lo
/// hace. Eso permite el ENSAYO: el mismo recorrido, sin salir al aire.
class Guion {
   public:
    enum class Estado { Parado, Corriendo, Pausado, Terminado };

    explicit Guion(Evento e) : e_(std::move(e)) {}

    const Evento& evento() const { return e_; }
    Estado estado() const { return estado_; }
    Posicion posicion() const { return pos_; }
    /// El cue donde está parado, o nullptr si no está en ninguno.
    const Cue* cueActual() const;
    /// El que sigue (para mostrarlo antes de que toque), o nullptr si no hay.
    const Cue* cueSiguiente() const;
    Posicion siguienteDe(Posicion p) const;

    /// Arranca desde el principio. `nowMs` es el reloj de pared.
    void arrancar(int64_t nowMs);
    /// Pasa al siguiente cue. Devuelve false si ya no hay más (y termina).
    bool avanzar(int64_t nowMs);
    /// Vuelve al anterior, para cuando hubo que repetir algo.
    bool retroceder(int64_t nowMs);
    /// Salta a un cue concreto (el operador lo eligió en la lista).
    bool irA(Posicion p, int64_t nowMs);
    /// Pausa y reanuda el guion entero: el tiempo pausado NO cuenta como
    /// desvío, porque el operador paró a propósito.
    void pausar(int64_t nowMs);
    void reanudar(int64_t nowMs);

    /// Segundos en el cue actual.
    int enCueSeg(int64_t nowMs) const;
    /// Desvío acumulado: positivo = vamos tarde, negativo = vamos adelantados.
    /// Se mide contra lo previsto para el cue donde estamos parados.
    int desvioSeg(int64_t nowMs) const;
    /// Cuánto falta para que termine el cue actual según lo previsto
    /// (negativo si ya se pasó). Sin duración prevista: 0.
    int restanteSeg(int64_t nowMs) const;

    /// Lo que realmente pasó: un renglón por cue ejecutado, con su duración
    /// real. Es la materia prima del informe posterior.
    struct Paso {
        Posicion pos;
        int64_t desdeMs = 0;
        int64_t hastaMs = 0;
        int previstoSeg = 0;
        int realSeg() const { return static_cast<int>((hastaMs - desdeMs) / 1000); }
    };
    const std::vector<Paso>& recorrido() const { return recorrido_; }

   private:
    void entrarA(Posicion p, int64_t nowMs);
    void cerrarPaso(int64_t nowMs);

    Evento e_;
    Estado estado_ = Estado::Parado;
    Posicion pos_;
    int64_t arranqueMs_ = 0;   ///< cuándo arrancó el evento
    int64_t enCueDesdeMs_ = 0; ///< cuándo se entró al cue actual
    int64_t pausadoDesdeMs_ = 0;
    int64_t pausadoMs_ = 0;      ///< total pausado, que no cuenta como desvío
    int64_t pausadoEnCueMs_ = 0; ///< lo pausado dentro del cue actual
    std::vector<Paso> recorrido_;
};

} // namespace ba::evento
