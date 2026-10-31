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
//
// QUIÉN LO CORRE. Desde la línea de tiempo (Linea.hpp) el guion de cues es lo
// que se ve de ella: cada paso con sus piezas, leído como cues en fila. Lo corre
// la `Corrida` de la línea (Corrida.hpp): un solo ejecutor, con el aviso «entra
// solo en 0:05», el HOLD y la retoma tras un corte. Este archivo sigue siendo el
// formato de cues, su validación y su revisión previa.
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
    Carrusel,     ///< pone un carrusel en una lámina y lo muestra (fija los dos)
    Pantalla,     ///< manda contenido a una pantalla
    Audio,        ///< cambia el rack, silencia, hace ducking
    Pausa,        ///< espera N segundos y sigue sola
    Espera,       ///< espera a que el operador confirme
    Nota,         ///< no hace nada: es una indicación para el operador
};

const char* nombreDeTipo(Tipo t);
/// ¿El programa sabe ejecutar este tipo de cue? Una sola lista para el ensayo,
/// la revisión y la ejecución (antes estaban copiadas y no coincidían).
bool ejecutable(Tipo t);
/// La lámina de un cue «carrusel» (desde 1; 0 si no dice o no es un número).
int numeroDeLamina(const nlohmann::json& datos);
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
    bool automatico = false; ///< true: al cumplir su duración pasa solo al siguiente (sin duración, en el acto)

    /// Lo que este cue necesita para poder ejecutarse, por tipo. Vacío = está
    /// completo. Es la mitad barata de la revisión previa: se ve sin tocar el
    /// disco ni el motor.
    std::string queLeFalta() const;

    /// Deja `datos` con los nombres de siempre. Un gráfico lleva sus campos en
    /// `valores` (los lee la interfaz y el que lo ejecuta); el asistente a veces los
    /// manda como `params` o `parametros` (así se llaman en grafico_mostrar) y el
    /// zócalo salía vacío. Se pasan a `valores` (lo que ya estaba ahí gana), y
    /// `nombre` (el gráfico, en grafico_mostrar) a `grafico`.
    void normalizarDatos();
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
    /// El idioma del evento (ba::i18n, «es», «en», «pt-BR»): lo que ven y escuchan el
    /// público y los invitados (gráficos, tótems, voz, la web). Vacío = el del operador.
    /// Se guarda con el evento por lo mismo que el huso: el aparato viaja.
    std::string idioma;
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
    /// La línea de tiempo (v2, evento/Linea.hpp), tal como está en el archivo. Null =
    /// un evento escrito antes de la línea (v1): al abrirlo se migra (ver
    /// linea::migrar) y desde ahí manda la línea; `bloques` pasa a ser lo que se ve
    /// de ella como guion de cues (linea::proyectar), para la vista y las
    /// herramientas de siempre. Acá va como JSON a propósito: este archivo no sabe
    /// nada de la línea y lo que solo lee el guion (la web, el histórico) no la paga.
    nlohmann::json linea;

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

} // namespace ba::evento
