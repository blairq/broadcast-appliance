// ============================================================================
// src/app/agent/Roles.cpp — ver Roles.hpp
// ============================================================================
#include "agent/Roles.hpp"

#include <algorithm>
#include <fstream>

#include <QDir>
#include <QString>

#include "i18n/I18n.hpp"

namespace ba::app::agent {
namespace {

using json = nlohmann::json;

// Los prompts son archivos del idioma (assets/idiomas/<código>/prompts/roles/<rol>.md): se
// leen en el del operador, así el agente trabaja y contesta en su idioma. `comun.md` es lo
// que comparten TODOS (cómo se trabaja de a un tiro) y va al final de cada uno.
//
// El del tótem de registro (registro/AgenteRegistro) es corto a propósito: va entero en cada
// turno de una conversación hablada, contra el modelo local, y lo que importa es la primera
// palabra rápido. {evento}, {campos} y {ficha} los pone la app en cada conversación.
std::string promptDe(const std::string& rol) { return ba::i18n::leer("prompts/roles/" + rol + ".md"); }

std::vector<Rol> deFabrica() {
    std::vector<Rol> r;
    {
        // Los ojos del sistema. Ver prompts/roles/vision.md: existe para que las imagenes NO
        // entren al hilo del asistente, que aguanta muy pocas y se cuelga.
        Rol x;
        x.id = "vision";
        x.titulo = ba::i18n::traducir("asistente", "Mirar una imagen");
        x.descripcion = ba::i18n::traducir("asistente", "Mira UNA imagen y contesta UNA pregunta concreta. No opera nada.");
        // SOLO imagenes. Es la contencion: aunque el modelo quiera hacer otra
        // cosa, no tiene con que. Un rol que mira no necesita poder tocar.
        x.categorias = {"imagenes"};
        x.disparadores = {}; // no se dispara solo: lo llama `imagen_preguntar`
        x.prompt = promptDe("vision");
        // PRENDIDO de fabrica, y es la excepcion a la regla. Los demas roles
        // arrancan apagados porque actuan solos ante un evento; este no tiene
        // disparadores: no hace nada hasta que alguien le da una imagen, y lo
        // unico que puede hacer es mirarla. Apagado, la unica via segura de
        // analizar una imagen no existiria y el asistente volveria a meterselas
        // en su propio contexto, que es el problema que esto resuelve.
        x.activo = true;
        r.push_back(std::move(x));
    }
    {
        Rol x;
        x.id = "escenografo";
        x.titulo = ba::i18n::traducir("asistente", "Escenógrafo");
        x.descripcion = ba::i18n::traducir("asistente", "Diseña cómo se ve el programa: escenas, fuentes y gráficos. No sale al aire.");
        x.categorias = {"escenas",  "fuentes",  "medios", "graficos", "laminas",
                        "imagenes", "entradas", "equipo", "operador"};
        x.disparadores = {}; // no se dispara solo: se le encarga un diseño
        x.prompt = promptDe("escenografo");
        // CON las herramientas propias del agente, y es el unico que las lleva:
        // bajar una imagen, convertirla y dejarla en Medios/ no se puede hacer
        // con herramientas del aparato. Ver `Rol::consola` para lo que implica.
        x.consola = true;
        // Apagado de fabrica, como los demas que actuan: prenderlo es decidir
        // que un agente puede tocar el disco y la red de esta maquina.
        x.activo = false;
        x.agruparMs = 0;
        x.agruparMax = 1;
        r.push_back(std::move(x));
    }
    {
        Rol x;
        x.id = "organizador";
        x.titulo = ba::i18n::traducir("asistente", "Organizador");
        x.descripcion = ba::i18n::traducir("asistente", "Arma el evento entero contandoselo: fecha, bloques con tiempos y el padron.");
        // Lo suyo y nada mas. NO lleva audio, ni plataformas, ni salidas: un rol
        // que arma un guion no tiene por que poder sacar algo al aire, y no
        // poder hacerlo es la contencion — igual que en `vision`.
        // `laminas` si: la presentacion del orador es parte de armar el evento,
        // y desarmar el .pptx que alguien mando por mail es de lo primero que
        // hay que hacer. No saca nada al aire: arma la tira y queda ahi.
        // `totems`: qué se le pregunta a la gente al registrarse es parte de armar el evento.
        x.categorias = {"evento",   "padron", "preguntas", "medios",  "prompter",
                        "graficos", "qr",     "operador",  "rostros", "laminas", "totems"};
        x.disparadores = {}; // no se dispara solo: lo llama quien organiza
        x.prompt = promptDe("organizador");
        // PRENDIDO de fabrica, por la misma razon que `vision`: no tiene
        // disparadores, asi que no hace nada hasta que alguien le encarga algo.
        // Apagado, el boton de «Armar contandolo» de la pagina de Evento no
        // tendria a quien encargarle, y el que lo aprieta no tiene por que
        // saber que antes hay que ir a prender un rol.
        x.activo = true;
        x.agruparMs = 0;
        x.agruparMax = 1;
        r.push_back(std::move(x));
    }
    {
        Rol x;
        x.id = "recepcion";
        x.titulo = ba::i18n::traducir("asistente", "Recepción");
        x.descripcion = ba::i18n::traducir("asistente", "Atiende a quien llega al lobby: completa su ficha y la deja lista para pasar.");
        // Con `rostros` y `padron`: recibir a alguien es darle ficha y dejar
        // constancia de que esta. Sin `rostros` no podia completar la ficha que
        // su propia descripcion promete, y sin `padron` el que entra por la
        // sala quedaba sin anotar mientras que el que entra por la puerta si.
        // Y `sesion`: el que espera en la sesión remota de la asamblea también
        // llega a la recepción (ui/AtencionViewModel), y habilitarlo es su parte.
        // Y `totems`: el registro por tótem es la otra puerta por donde llega la gente.
        // Y `identidad`: leer el DNI de quien llega es parte de recibirlo.
        x.categorias = {"sala", "oradores", "imagenes", "operador", "rostros", "padron", "sesion", "totems", "identidad"};
        x.disparadores = {"lobby", "hand", "guest_drop", "guest_message"};
        x.prompt = promptDe("recepcion");
        x.agruparMs = 0;
        x.agruparMax = 1;
        r.push_back(std::move(x));
    }
    {
        Rol x;
        x.id = "tecnico";
        x.titulo = ba::i18n::traducir("asistente", "Técnico");
        x.descripcion = ba::i18n::traducir("asistente", "Mide y arregla el audio y las entradas de video cuando algo suena o se ve mal.");
        x.categorias = {"audio", "audio_rack", "entradas", "fuentes", "operador"};
        x.disparadores = {"audio_peak", "audio_silence", "stream_health"};
        x.prompt = promptDe("tecnico");
        // Dos picos seguidos son un problema, no dos: se juntan.
        x.agruparMs = 3000;
        x.agruparMax = 5;
        r.push_back(std::move(x));
    }
    {
        // Los disparadores que no toma nadie más. Con el modelo local de fábrica:
        // es instantáneo y no necesita internet; las dice en voz alta, contesta
        // lo que es leer y propone (nunca hace solo) lo que sea una acción.
        Rol x;
        x.id = "vigilante";
        x.titulo = ba::i18n::traducir("asistente", "Vigilante");
        x.descripcion = ba::i18n::traducir("asistente", "Atiende los disparadores que no toma otro agente (fin de un video, tiempo de un "
                                                        "orador, el chat, el disco, los temporizadores): las avisa en voz alta y propone "
                                                        "qué hacer.");
        x.categorias = {"estado", "escenas", "audio", "sala", "oradores", "operador"};
        x.disparadores = {"media_end", "speaker_time", "chat", "disk", "timer"};
        x.prompt.clear(); // solo las reglas comunes
        x.proveedor = "local";
        r.push_back(std::move(x));
    }
    {
        // El tótem de registro: entiende lo que dice la persona (varios datos de una, una
        // corrección, una pregunta sobre el evento) mientras el guion lleva las preguntas.
        // No lo despacha el Despachador: la app le habla directo por chat-completions, una
        // conversación por tótem (agent/ChatEnVivo). Ver registro/AgenteRegistro.
        Rol x;
        x.id = "registro";
        x.titulo = ba::i18n::traducir("asistente", "Registro (tótem)");
        x.descripcion = ba::i18n::traducir("asistente", "Entiende a quien se registra en un tótem: datos dichos de una, correcciones y preguntas "
                                                        "sobre el evento (con la ficha del tótem). El guion sigue llevando las preguntas.");
        x.categorias = {}; // no usa el MCP: sus únicas herramientas son las del registro, en la app
        x.disparadores = {};
        x.prompt = promptDe("registro");
        // El modelo LOCAL de fábrica: contesta en el acto y la voz de la persona (sus datos) no
        // sale de la máquina. Otro motor solo si el organizador lo elige (y se le avisa).
        x.proveedor = "local";
        // Prendido de fábrica, como `vision`: no actúa solo, solo entiende lo que se le dice a
        // un tótem. Apagado, los tótems usan el guion solo.
        x.activo = true;
        x.enLaApp = true;
        r.push_back(std::move(x));
    }
    return r;
}

} // namespace

const std::set<std::string>& Roles::juegoDelAsistente() {
    // Lo que se usa operando en vivo. Ver el comentario del header.
    static const std::set<std::string> c{
        // Los nombres salen de `addCategory`, NO de la intuicion: «media» y
        // «sistema» parecian razonables y las categorias se llaman «medios» y
        // «equipo». El asistente se quedo sin poder poner musica ni mirar el
        // disco, y no fallaba nada: simplemente esas herramientas no existian
        // para el. Ver el aviso de McpCore::setRoles.
        "escenas", "fuentes", "imagenes", "sala", "oradores", "salidas", "graficos", "rostros", "interfaz",
        "ventanas", "operador", "medios", "recetas", "audio", "audio_rack", "config", "sistema", "equipo",
        "entradas", "alertas", "pantallas", "evento", "prompter", "qr", "cuentas", "asistente", "youtube",
        "twitch", "facebook", "padron", "preguntas", "laminas", "totems", "identidad",
        // El grafo de video (tarjetas, módulos de GStreamer, controladores,
        // compositor, relojes y sincronía) y las cámaras PTZ: armar bien un
        // pipeline es justo lo que el operador le va a pedir. Faltaban, y la
        // ayuda igual se las describía (probado: terminaba metiendo cadenas de
        // GStreamer a mano en fuentes sueltas).
        "video", "camaras",
        // Los plugins de Python: cuando un equipo no se puede manejar con lo que
        // hay, el asistente escribe uno (no se queda en «no puedo»).
        "plugins",
        // Los equipos: detectar y manejar (red, esta máquina, USB), con acciones declaradas.
        "equipos",
        // La botonera (tablets, Stream Deck, MIDI) y el bus de eventos: el
        // operador le pide botones y plugins de superficies al asistente.
        "botonera", "dsk",
        // La traducción en vivo y su glosario de términos del evento.
        "traduccion",
        // La central de conferencia del recinto (micrófonos de banca): las que
        // abren o cierran micrófonos pasan por las guardas de aire.
        "conferencia",
        // La banca virtual (integrantes desde el navegador, en la sala o afuera).
        "sesion",
        // La asamblea (revisar que esté lista, sus escenas), las configuraciones con
        // nombre (crear, cambiar, la demo) y el histórico de los eventos. Faltaban
        // y no fallaba nada: no existían para el asistente (ni en help_search).
        // McpCore::verificarJuegos avisa en el registro si queda otra afuera.
        "asamblea", "configuraciones", "historico",
        // Los eventos (crear uno nuevo desde cero, copia, anterior o plantilla) y su
        // documentación (PDF por punto y sección). Quedaron afuera al agregarlas: el
        // asistente decía que no tenía cómo crear un evento.
        "eventos", "documentos",
        // Los tiempos del pipeline (qué pieza agrega cuánto, la sincronía) y el control
        // web (emparejar una tablet o un teléfono con el QR): el registro avisaba que
        // quedaban afuera.
        "tiempos", "control_web"};
    return c;
}

const std::set<std::string>& Roles::comunes() {
    static const std::set<std::string> c{"ayuda", "estado", "agentes", "registro"};
    return c;
}

Roles::Roles(std::filesystem::path archivo) : archivo_(std::move(archivo)), roles_(deFabrica()) { leer(); }

const Rol* Roles::porId(const std::string& id) const {
    const auto it = std::find_if(roles_.begin(), roles_.end(), [&](const Rol& r) { return r.id == id; });
    return it == roles_.end() ? nullptr : &*it;
}

const Rol* Roles::porDisparador(const std::string& tipo) const {
    for (const Rol& r : roles_) {
        if (!r.activo) continue;
        if (std::find(r.disparadores.begin(), r.disparadores.end(), tipo) != r.disparadores.end()) return &r;
    }
    return nullptr;
}

bool Roles::setActivo(const std::string& id, bool activo) {
    auto* r = const_cast<Rol*>(porId(id));
    if (r == nullptr) return false;
    if (r->activo != activo) {
        r->activo = activo;
        guardar();
    }
    return true;
}

bool Roles::setConsola(const std::string& id, bool consola) {
    auto* r = const_cast<Rol*>(porId(id));
    if (r == nullptr) return false;
    if (r->consola != consola) {
        r->consola = consola;
        guardar();
    }
    return true;
}

bool Roles::setProveedor(const std::string& id, const std::string& proveedor) {
    auto* r = const_cast<Rol*>(porId(id));
    if (r == nullptr) return false;
    if (r->proveedor != proveedor) {
        r->proveedor = proveedor;
        r->modelo.clear(); // el modelo es del proveedor: cambiar uno invalida el otro
        guardar();
    }
    return true;
}

std::string Roles::instrucciones(const Rol& r) {
    // Las reglas comunes van SIEMPRE (el aire no es tuyo, el resumen al final…): el operador
    // edita lo propio del rol, no las guardas.
    const std::string& propio = r.promptPropio.empty() ? r.prompt : r.promptPropio;
    return propio + promptDe("comun");
}

bool Roles::setPrompt(const std::string& id, const std::string& texto) {
    auto* r = const_cast<Rol*>(porId(id));
    if (r == nullptr) return false;
    // Igual al de fábrica (o vacío) = el de fábrica: así una actualización que lo mejora llega.
    const std::string propio = texto == r->prompt ? std::string() : texto;
    if (r->promptPropio != propio) {
        r->promptPropio = propio;
        guardar();
    }
    return true;
}

bool Roles::setModelo(const std::string& id, const std::string& modelo) {
    auto* r = const_cast<Rol*>(porId(id));
    if (r == nullptr) return false;
    if (r->modelo != modelo) {
        r->modelo = modelo;
        guardar();
    }
    return true;
}

std::map<std::string, std::set<std::string>> Roles::categoriasPorRol() const {
    std::map<std::string, std::set<std::string>> out;
    for (const Rol& r : roles_)
        out[r.id] = std::set<std::string>(r.categorias.begin(), r.categorias.end());
    return out;
}

json Roles::aJson() const {
    json out = json::array();
    for (const Rol& r : roles_)
        out.push_back(json{{"id", r.id},
                           {"titulo", r.titulo},
                           {"descripcion", r.descripcion},
                           {"categorias", r.categorias},
                           {"disparadores", r.disparadores},
                           {"agruparMs", r.agruparMs},
                           {"agruparMax", r.agruparMax},
                           {"activo", r.activo},
                           {"consola", r.consola},
                           {"enLaApp", r.enLaApp},
                           {"proveedor", r.proveedor},
                           {"modelo", r.modelo},
                           {"prompt", r.promptPropio.empty() ? r.prompt : r.promptPropio},
                           {"promptFabrica", r.prompt},
                           {"promptEditado", !r.promptPropio.empty()}});
    return out;
}

// Solo lo que elige el operador. Categorias, disparadores y el prompt de fábrica son del
// sistema: si un archivo viejo los trae, se ignoran (el operador tiene el suyo, promptPropio).
void Roles::leer() {
    if (archivo_.empty()) return;
    std::ifstream in(archivo_, std::ios::binary);
    if (!in) return;
    const json o = json::parse(in, nullptr, false);
    if (!o.is_object()) return;
    for (Rol& r : roles_) {
        const json e = o.value(r.id, json::object());
        if (!e.is_object()) continue;
        r.activo = e.value("activo", r.activo);
        r.consola = e.value("consola", r.consola);
        r.proveedor = e.value("proveedor", r.proveedor);
        r.modelo = e.value("modelo", r.modelo);
        r.promptPropio = e.value("promptPropio", r.promptPropio);
    }
}

void Roles::guardar() const {
    if (archivo_.empty()) return;
    QDir().mkpath(QString::fromStdString(archivo_.parent_path().string()));
    json o = json::object();
    for (const Rol& r : roles_)
        o[r.id] = json{
            {"activo", r.activo}, {"consola", r.consola}, {"proveedor", r.proveedor}, {"modelo", r.modelo},
            {"promptPropio", r.promptPropio}};
    std::ofstream out(archivo_, std::ios::binary | std::ios::trunc);
    out << o.dump(2, ' ', false, nlohmann::json::error_handler_t::replace) << "\n";
}

} // namespace ba::app::agent
