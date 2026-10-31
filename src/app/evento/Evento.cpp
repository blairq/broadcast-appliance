// ============================================================================
// src/app/evento/Evento.cpp — ver Evento.hpp
// ============================================================================
#include "evento/Evento.hpp"

#include "evento/LeerJson.hpp"
#include "i18n/I18n.hpp"

#include <algorithm>

namespace ba::evento {
namespace {

using nlohmann::json;

struct NombreTipo {
    Tipo tipo;
    const char* nombre;
};

// El nombre que va al archivo. Se escribe en castellano porque el guion lo
// edita gente, no solo el programa: un evento.json tiene que poder abrirse y
// entenderse con un editor de texto el dia que algo falle.
constexpr NombreTipo kTipos[] = {
    {Tipo::Escena, "escena"},   {Tipo::Musica, "musica"},
    {Tipo::Video, "video"},     {Tipo::Teleprompter, "teleprompter"},
    {Tipo::Orador, "orador"},   {Tipo::Pantalla, "pantalla"},
    {Tipo::Audio, "audio"},     {Tipo::Pausa, "pausa"},
    {Tipo::Espera, "espera"},   {Tipo::Nota, "nota"},
    {Tipo::Grafico, "grafico"}, {Tipo::Carrusel, "carrusel"},
};

/// Un dato de texto del cue esta presente y no vacio.
bool tieneTexto(const json& datos, const char* clave) {
    return datos.contains(clave) && datos[clave].is_string() && !datos[clave].get<std::string>().empty();
}

} // namespace

int numeroDeLamina(const json& datos) {
    // Número o texto con el número: la interfaz guarda lo que elige en una lista como texto.
    const auto l = datos.find("lamina");
    if (l == datos.end()) return 0;
    if (l->is_number_integer()) return l->get<int>();
    if (l->is_string()) {
        try {
            return std::stoi(l->get<std::string>());
        } catch (...) {
            return 0;
        }
    }
    return 0;
}

bool ejecutable(Tipo t) {
    switch (t) {
        case Tipo::Escena:
        case Tipo::Musica:
        case Tipo::Video:
        case Tipo::Teleprompter:
        case Tipo::Orador:
        case Tipo::Grafico:
        case Tipo::Carrusel:
        case Tipo::Pantalla:
        case Tipo::Audio:
        case Tipo::Pausa:
        case Tipo::Espera:
        case Tipo::Nota: return true;
    }
    return false;
}

const char* nombreDeTipo(Tipo t) {
    for (const NombreTipo& n : kTipos)
        if (n.tipo == t) return n.nombre;
    return "nota";
}

void Cue::normalizarDatos() {
    if (tipo != Tipo::Grafico || !datos.is_object()) return;
    // `nombre` es como grafico_mostrar llama al gráfico; acá es `grafico`.
    if (!datos.contains("grafico") && datos.contains("nombre") && datos["nombre"].is_string()) {
        datos["grafico"] = datos["nombre"];
        datos.erase("nombre");
    }
    for (const char* alias : {"params", "parametros"}) {
        if (!datos.contains(alias)) continue;
        const nlohmann::json v = datos[alias];
        datos.erase(alias);
        if (!v.is_object()) continue;
        if (!datos.contains("valores") || !datos["valores"].is_object()) datos["valores"] = nlohmann::json::object();
        for (const auto& [k, x] : v.items())
            if (!datos["valores"].contains(k)) datos["valores"][k] = x;
    }
}

bool tipoDeNombre(const std::string& s, Tipo& out) {
    for (const NombreTipo& n : kTipos) {
        if (s == n.nombre) {
            out = n.tipo;
            return true;
        }
    }
    return false;
}

// --- Cue --------------------------------------------------------------------

std::string Cue::queLeFalta() const {
    switch (tipo) {
        case Tipo::Escena:
            if (!tieneTexto(datos, "escena")) return ba::i18n::traducir("evento", "no dice que escena poner");
            return {};
        case Tipo::Musica:
        case Tipo::Video:
            // Parar la musica no necesita archivo; arrancarla si.
            if (datos.value("accion", std::string("arrancar")) == "parar") return {};
            if (!tieneTexto(datos, "archivo")) return ba::i18n::traducir("evento", "no dice que archivo reproducir");
            return {};
        case Tipo::Teleprompter:
            if (!tieneTexto(datos, "discurso")) return ba::i18n::traducir("evento", "no dice que texto cargar");
            return {};
        case Tipo::Orador:
            if (!tieneTexto(datos, "participante")) return ba::i18n::traducir("evento", "no dice quien habla");
            if (duracionSeg <= 0) return ba::i18n::traducir("evento", "no dice cuanto tiempo tiene para hablar");
            return {};
        case Tipo::Grafico: {
            const std::string accion = datos.value("accion", std::string("mostrar"));
            // Prender o apagar un rotulo automatico no necesita fuente: el
            // vinculo ya sabe en cual se ve.
            if (accion == "prender" || accion == "apagar") {
                if (!tieneTexto(datos, "vinculo")) return ba::i18n::traducir("evento", "no dice que vinculo prender o apagar");
                return {};
            }
            // La fuente hace falta siempre: es DONDE se ve. Un grafico sin
            // fuente no esta en ninguna escena y no lo ve nadie.
            if (!tieneTexto(datos, "fuente")) return ba::i18n::traducir("evento", "no dice en que fuente de escena se ve");
            if (accion == "ocultar") return {};
            if (!tieneTexto(datos, "grafico")) return ba::i18n::traducir("evento", "no dice que grafico sacar al aire");
            return {};
        }
        case Tipo::Carrusel:
            if (!tieneTexto(datos, "carrusel")) return ba::i18n::traducir("evento", "no dice qué carrusel poner");
            if (numeroDeLamina(datos) < 1) return ba::i18n::traducir("evento", "no dice en qué lámina (desde 1)");
            return {};
        case Tipo::Pantalla:
            if (!tieneTexto(datos, "pantalla")) return ba::i18n::traducir("evento", "no dice a que pantalla va");
            if (!tieneTexto(datos, "contenido")) return ba::i18n::traducir("evento", "no dice que mandarle");
            return {};
        case Tipo::Audio: {
            if (!tieneTexto(datos, "accion")) return ba::i18n::traducir("evento", "no dice que hacer con el audio");
            if (!tieneTexto(datos, "fuente")) return ba::i18n::traducir("evento", "no dice sobre que fuente de audio actuar");
            const std::string a = datos.value("accion", std::string());
            if (a != "silenciar" && a != "activar" && a != "volumen")
                return ba::i18n::llenar(ba::i18n::traducir("evento", "la accion \"%1\" no existe (silenciar, activar o volumen)"), {a});
            return {};
        }
        case Tipo::Pausa:
            if (duracionSeg <= 0) return ba::i18n::traducir("evento", "una pausa sin duracion no espera nada");
            return {};
        case Tipo::Espera:
        case Tipo::Nota:
            // Los dos existen para decirle algo al operador: sin texto, no
            // dicen nada y lo unico que hacen es frenar el guion.
            if (texto.empty()) return ba::i18n::traducir("evento", "no dice nada: al operador le va a aparecer un cartel vacio");
            return {};
    }
    return {};
}

// --- Bloque y Evento --------------------------------------------------------

int Bloque::duracionSeg() const {
    int total = 0;
    for (const Cue& c : cues)
        total += std::max(0, c.duracionSeg);
    return total;
}

int Evento::duracionSeg() const {
    int total = 0;
    for (const Bloque& b : bloques)
        total += b.duracionSeg();
    return total;
}

int Evento::offsetDeBloqueSeg(std::size_t bloque) const {
    int total = 0;
    for (std::size_t i = 0; i < bloques.size() && i < bloque; ++i)
        total += bloques[i].duracionSeg();
    return total;
}

int Evento::offsetDeCueSeg(std::size_t bloque, std::size_t cue) const {
    if (bloque >= bloques.size() || cue >= bloques[bloque].cues.size()) return -1;
    int total = offsetDeBloqueSeg(bloque);
    for (std::size_t i = 0; i < cue; ++i)
        total += std::max(0, bloques[bloque].cues[i].duracionSeg);
    return total;
}

std::size_t Evento::cantidadDeCues() const {
    std::size_t n = 0;
    for (const Bloque& b : bloques)
        n += b.cues.size();
    return n;
}

json Evento::aJson() const {
    json bs = json::array();
    for (const Bloque& b : bloques) {
        json cs = json::array();
        for (const Cue& c : b.cues) {
            cs.push_back(json{{"id", c.id},
                              {"tipo", nombreDeTipo(c.tipo)},
                              {"texto", c.texto},
                              {"datos", c.datos},
                              {"duracionSeg", c.duracionSeg},
                              {"automatico", c.automatico}});
        }
        bs.push_back(json{{"id", b.id}, {"nombre", b.nombre}, {"nota", b.nota}, {"cues", cs}});
    }
    json inv = json::array();
    for (const Invitado& i : autoAceptar)
        inv.push_back(json{{"personaId", i.personaId}, {"nombre", i.nombre}, {"nota", i.nota}});
    json j{{"nombre", nombre}, {"fecha", fecha}, {"inicio", inicio},  {"huso", huso},
           {"idioma", idioma}, {"nota", nota},     {"bloques", bs},  {"autoAceptar", inv},
           {"invitados", invitados}, {"preguntas", preguntas}, {"asamblea", asamblea}};
    if (linea.is_object()) j["linea"] = linea;
    return j;
}

Evento Evento::deJson(const json& j) {
    Evento e;
    if (!j.is_object()) return e;
    e.nombre = leerTexto(j, "nombre");
    e.fecha = leerTexto(j, "fecha");
    e.inicio = leerTexto(j, "inicio");
    e.huso = leerTexto(j, "huso");
    e.idioma = leerTexto(j, "idioma");
    e.nota = leerTexto(j, "nota");
    if (j.contains("invitados") && j["invitados"].is_boolean()) e.invitados = j["invitados"].get<bool>();
    if (j.contains("preguntas") && j["preguntas"].is_boolean()) e.preguntas = j["preguntas"].get<bool>();
    if (j.contains("asamblea") && j["asamblea"].is_boolean()) e.asamblea = j["asamblea"].get<bool>();
    if (j.contains("linea") && j["linea"].is_object()) e.linea = j["linea"];
    if (j.contains("autoAceptar") && j["autoAceptar"].is_array()) {
        for (const json& ji : j["autoAceptar"]) {
            if (!ji.is_object()) continue;
            Invitado i;
            i.personaId = leerTexto(ji, "personaId");
            i.nombre = leerTexto(ji, "nombre");
            i.nota = leerTexto(ji, "nota");
            // Sin ficha de rostro no se puede reconocer a nadie: una entrada
            // asi dejaria pasar por nombre, que es justo lo que no se quiere.
            if (!i.personaId.empty()) e.autoAceptar.push_back(std::move(i));
        }
    }
    if (!j.contains("bloques") || !j["bloques"].is_array()) return e;
    for (const json& jb : j["bloques"]) {
        if (!jb.is_object()) continue;
        Bloque b;
        b.id = leerTexto(jb, "id");
        b.nombre = leerTexto(jb, "nombre");
        b.nota = leerTexto(jb, "nota");
        if (jb.contains("cues") && jb["cues"].is_array()) {
            for (const json& jc : jb["cues"]) {
                if (!jc.is_object()) continue;
                Cue c;
                c.id = leerTexto(jc, "id");
                // Un tipo que este programa no conoce (un evento escrito con
                // una version mas nueva) queda como nota: se ve en la lista y
                // no se ejecuta, en vez de desaparecer sin avisar.
                if (!tipoDeNombre(leerTexto(jc, "tipo"), c.tipo)) c.tipo = Tipo::Nota;
                c.texto = leerTexto(jc, "texto");
                if (jc.contains("datos") && jc["datos"].is_object()) c.datos = jc["datos"];
                c.duracionSeg = static_cast<int>(std::max<int64_t>(0, leerNumero(jc, "duracionSeg")));
                c.automatico = leerBool(jc, "automatico");
                c.normalizarDatos();
                b.cues.push_back(std::move(c));
            }
        }
        e.bloques.push_back(std::move(b));
    }
    return e;
}

// --- Revision previa (la mitad barata) --------------------------------------

namespace {

/// Lee `n` dígitos y los devuelve como número. `i` avanza. -1 si no eran
/// dígitos (o si se acabó el texto), que es lo que corta toda validación.
int digitos(const std::string& s, std::size_t& i, int n) {
    int v = 0;
    for (int k = 0; k < n; ++k) {
        if (i >= s.size() || s[i] < '0' || s[i] > '9') return -1;
        v = v * 10 + (s[i] - '0');
        ++i;
    }
    return v;
}

bool bisiesto(int a) { return (a % 4 == 0 && a % 100 != 0) || a % 400 == 0; }

int diasDelMes(int a, int m) {
    static const int d[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (m < 1 || m > 12) return 0;
    if (m == 2 && bisiesto(a)) return 29;
    return d[m - 1];
}

} // namespace

bool fechaValida(const std::string& s) {
    if (s.size() != 10 || s[4] != '-' || s[7] != '-') return false;
    std::size_t i = 0;
    const int a = digitos(s, i, 4);
    ++i;
    const int m = digitos(s, i, 2);
    ++i;
    const int d = digitos(s, i, 2);
    if (a < 0 || m < 0 || d < 0) return false;
    // El día tiene que EXISTIR: un 31 de febrero pasa cualquier chequeo de
    // rango y después no hay hora prevista que valga.
    return a >= 1970 && a <= 9999 && d >= 1 && d <= diasDelMes(a, m);
}

bool horaValida(const std::string& s) {
    if (s.size() != 5 || s[2] != ':') return false;
    std::size_t i = 0;
    const int h = digitos(s, i, 2);
    ++i;
    const int m = digitos(s, i, 2);
    if (h < 0 || m < 0) return false;
    return h <= 23 && m <= 59;
}

std::vector<Reparo> revisar(const Evento& e) {
    std::vector<Reparo> rs;
    if (e.bloques.empty()) {
        rs.push_back({-1, -1, true, ba::i18n::traducir("evento", "el evento no tiene ningun bloque")});
        return rs;
    }
    if (e.duracionSeg() == 0)
        rs.push_back({-1, -1, false, ba::i18n::traducir("evento", "ningun cue tiene duracion prevista: no se puede calcular el desvio")});
    // Un formato mal escrito es GRAVE y no un aviso: una fecha que no se
    // entiende no se ignora sola, se lee mal. "03/09" es el 3 de septiembre
    // para uno y el 9 de marzo para otro.
    if (!e.fecha.empty() && !fechaValida(e.fecha))
        rs.push_back({-1, -1, true,
                      ba::i18n::llenar(ba::i18n::traducir("evento", "la fecha no es un dia valido en formato YYYY-MM-DD: %1"), {e.fecha})});
    if (!e.inicio.empty() && !horaValida(e.inicio))
        rs.push_back({-1, -1, true,
                      ba::i18n::llenar(ba::i18n::traducir("evento", "la hora de arranque no esta en formato HH:MM: %1"), {e.inicio})});
    // Lo que sigue son avisos: el evento corre igual, pero no se puede convocar.
    if (e.fecha.empty())
        rs.push_back({-1, -1, false, ba::i18n::traducir("evento", "el evento no tiene fecha: no se lo puede convocar ni archivar")});
    else if (e.inicio.empty())
        rs.push_back(
            {-1, -1, false, ba::i18n::traducir("evento", "hay dia pero no hora de arranque: las horas previstas no se pueden calcular")});
    if (!e.inicio.empty() && e.huso.empty())
        rs.push_back(
            {-1, -1, false, ba::i18n::traducir("evento", "no hay huso horario: el que se conecta de otro huso no sabe a que hora es")});

    for (std::size_t bi = 0; bi < e.bloques.size(); ++bi) {
        const Bloque& b = e.bloques[bi];
        const int ib = static_cast<int>(bi);
        if (b.nombre.empty()) rs.push_back({ib, -1, false, ba::i18n::traducir("evento", "el bloque no tiene nombre")});
        if (b.cues.empty()) {
            rs.push_back({ib, -1, false, ba::i18n::traducir("evento", "el bloque esta vacio")});
            continue;
        }
        for (std::size_t ci = 0; ci < b.cues.size(); ++ci) {
            const Cue& c = b.cues[ci];
            const std::string falta = c.queLeFalta();
            if (!falta.empty()) rs.push_back({ib, static_cast<int>(ci), true, falta});
        }
        // Una cadena larga de cues automaticos es donde el operador pierde el
        // control: si algo sale mal, el guion sigue solo. No es un error, pero
        // se avisa.
        int seguidos = 0;
        for (std::size_t ci = 0; ci < b.cues.size(); ++ci) {
            seguidos = b.cues[ci].automatico ? seguidos + 1 : 0;
            if (seguidos == 6) {
                rs.push_back({ib, static_cast<int>(ci), false,
                              ba::i18n::traducir("evento", "seis cues automaticos seguidos: si algo sale mal el guion sigue solo")});
                break;
            }
        }
    }
    return rs;
}

} // namespace ba::evento
