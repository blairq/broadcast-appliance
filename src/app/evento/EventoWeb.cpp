// SPDX-License-Identifier: GPL-2.0-or-later
// src/app/evento/EventoWeb.cpp — ver EventoWeb.hpp
#include "i18n/I18n.hpp"
#include "evento/EventoWeb.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>

namespace ba::evento {

using json = nlohmann::json;
namespace fs = std::filesystem;

namespace {

std::string escapar(const std::string& s) {
    std::string o;
    o.reserve(s.size());
    for (char c : s) {
        switch (c) {
            case '&': o += "&amp;"; break;
            case '<': o += "&lt;"; break;
            case '>': o += "&gt;"; break;
            case '"': o += "&quot;"; break;
            default: o += c;
        }
    }
    return o;
}

void reemplazar(std::string& s, const std::string& de, const std::string& a) {
    for (std::size_t p = s.find(de); p != std::string::npos; p = s.find(de, p + a.size())) s.replace(p, de.size(), a);
}

bool escribir(const fs::path& p, const std::string& texto) {
    std::error_code ec;
    fs::create_directories(p.parent_path(), ec);
    const fs::path tmp = p.string() + ".tmp";
    {
        std::ofstream f(tmp, std::ios::binary);
        if (!f) return false;
        f << texto;
        if (!f) return false;
    }
    fs::rename(tmp, p, ec);
    return !ec;
}

/// FNV-1a de 64 bits en hex: para reconocer una página que sigue tal cual la dejó la plantilla.
std::string huella(const std::string& t) {
    std::uint64_t h = 1469598103934665603ULL;
    for (unsigned char c : t) { h ^= c; h *= 1099511628211ULL; }
    char b[17];
    std::snprintf(b, sizeof b, "%016llx", static_cast<unsigned long long>(h));
    return b;
}

std::string leerArchivo(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    std::ostringstream o;
    o << f.rdbuf();
    return o.str();
}


// --- Plantillas -----------------------------------------------------------------
// Sin nada de afuera (ni fuentes ni scripts de internet): el sitio anda aunque la
// sala no tenga salida a internet y no le avisa a nadie quién lo mira.

/// «3 secciones · 1 h 10 min»: para la descripción de la página (buscadores, redes).
std::string duracionTexto(int seg) {
    if (seg <= 0) return "";
    const int m = (seg + 59) / 60;
    return m >= 60 ? std::to_string(m / 60) + " h" + (m % 60 ? " " + std::to_string(m % 60) + " min" : std::string())
                   : std::to_string(m) + " min";
}
/// El idioma de la página: el del evento; si no tiene, el del programa (como «idioma» en el estado).
ba::i18n::En idiomaDe(const Evento& e) { return ba::i18n::En(e.idioma.empty() ? ba::i18n::actual().codigo : e.idioma); }
/// El nombre de una sección (lo ve el público: en el idioma del evento).
std::string nombreDe(const Bloque& b, std::size_t i, const ba::i18n::En& en) {
    return b.nombre.empty() ? ba::i18n::llenar(en.traducir("evento", "Sección %1"), {std::to_string(i + 1)}) : b.nombre;
}

fs::path& carpetaPlantillasRef() {
    static fs::path p;
    return p;
}

} // namespace

void setCarpetaPlantillas(const fs::path& dir) { carpetaPlantillasRef() = dir; }
fs::path carpetaPlantillas() { return carpetaPlantillasRef(); }

// --- sitio.json -----------------------------------------------------------------

SitioWeb SitioWeb::leer(const fs::path& carpetaWeb, const Evento& e) {
    SitioWeb s;
    s.subdominio = subdominioDe(e.nombre);
    std::ifstream f(carpetaWeb / "sitio.json");
    const json j = f ? json::parse(f, nullptr, false) : json();
    if (!j.is_object()) return s;
    s.resto = j;
    s.titulo = j.value("titulo", s.titulo);
    s.bajada = j.value("bajada", s.bajada);
    s.color = j.value("color", s.color);
    s.fondo = j.value("fondo", s.fondo);
    s.subdominio = j.value("subdominio", s.subdominio);
    s.barra = j.value("barra", s.barra);
    s.direccionAlIniciar = j.value("direccion_al_iniciar", s.direccionAlIniciar);
    if (j.contains("vivos") && j["vivos"].is_object()) s.vivos = j["vivos"];
    s.participacion = j.value("participacion", s.participacion);
    return s;
}

json SitioWeb::aJson() const {
    json j = resto.is_object() ? resto : json::object();
    j["titulo"] = titulo;
    j["bajada"] = bajada;
    j["color"] = color;
    j["fondo"] = fondo;
    j["subdominio"] = subdominio;
    j["barra"] = barra;
    j["direccion_al_iniciar"] = direccionAlIniciar;
    j["vivos"] = vivos.is_object() ? vivos : json::object();
    j["participacion"] = participacion;
    return j;
}

bool SitioWeb::guardar(const fs::path& carpetaWeb) const {
    return escribir(carpetaWeb / "sitio.json", aJson().dump(2, ' ', false, json::error_handler_t::replace) + "\n");
}

std::string subdominioDe(const std::string& nombre) {
    // Pinggy acepta solo minúsculas y números en el subdominio (ni guiones ni puntos): las
    // vocales con acento (UTF-8 de dos bytes) pasan a la letra sola y el resto se saca.
    static const std::pair<const char*, char> kAcentos[] = {
        {"á", 'a'}, {"é", 'e'}, {"í", 'i'}, {"ó", 'o'}, {"ú", 'u'}, {"ü", 'u'}, {"ñ", 'n'},
        {"Á", 'a'}, {"É", 'e'}, {"Í", 'i'}, {"Ó", 'o'}, {"Ú", 'u'}, {"Ü", 'u'}, {"Ñ", 'n'}};
    std::string s;
    for (std::size_t i = 0; i < nombre.size();) {
        bool hecho = false;
        for (const auto& [a, c] : kAcentos) {
            const std::size_t n = std::char_traits<char>::length(a);
            if (nombre.compare(i, n, a) == 0) { s += c; i += n; hecho = true; break; }
        }
        if (hecho) continue;
        const unsigned char c = static_cast<unsigned char>(nombre[i++]);
        if (std::isalnum(c) && c < 128) s += static_cast<char>(std::tolower(c));
    }
    if (s.size() > 40) s.resize(40);
    return s;
}

std::string paginaDeBloque(const Bloque& b, std::size_t indice) {
    // El id del bloque va en el nombre del archivo: solo lo que es seguro en una ruta.
    std::string id;
    for (char c : b.id)
        if (std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_') id += c;
    return "seccion-" + (id.empty() ? std::to_string(indice + 1) : id) + ".html";
}

// --- Generar ----------------------------------------------------------------------

ResultadoWeb generar(const Evento& e, const fs::path& dir, bool sobrescribir) {
    ResultadoWeb r;
    std::error_code ec;
    fs::create_directories(dir, ec);
    if (ec) {
        r.error = ba::i18n::llenar(ba::i18n::traducir("evento", "no se pudo crear %1"), {dir.string()});
        return r;
    }
    SitioWeb s = SitioWeb::leer(dir, e);
    // Lo que escribió la plantilla, por archivo: si sigue igual (nadie lo tocó), se
    // actualiza con la plantilla y el guion de ahora; si lo editaron, no se toca.
    json huellas = s.resto.value("plantillas", json::object());
    if (!huellas.is_object()) huellas = json::object();
    const ba::i18n::En en = idiomaDe(e);
    const std::string titulo = s.titulo.empty() ? (e.nombre.empty() ? en.traducir("evento", "El evento") : e.nombre) : s.titulo;
    const auto poner = [&](const std::string& rel, const std::string& texto) {
        if (fs::exists(dir / rel) && !sobrescribir) {
            const std::string actual = leerArchivo(dir / rel);
            const bool intacta = huellas.contains(rel) && huellas[rel] == huella(actual);
            if (!intacta || actual == texto) {
                if (!intacta) r.salteadas.push_back(rel);
                return;
            }
        }
        if (escribir(dir / rel, texto)) {
            r.escritas.push_back(rel);
            huellas[rel] = huella(texto);
        } else {
            r.error = ba::i18n::llenar(ba::i18n::traducir("evento", "no se pudo escribir %1"), {rel});
        }
    };
    // Las plantillas del sistema (assets/web/evento/plantilla): genéricas, sin datos del
    // evento adentro; la página se arma sola con /api/v1/evento (vivo.js). Acá solo va lo que
    // tiene que estar en el HTML aunque no corra JavaScript: el título, la descripción y,
    // en la página de cada sección, cuál es.
    const fs::path pl = carpetaPlantillas();
    const auto plantilla = [&](const char* nombre) -> std::string {
        std::error_code ec2;
        if (pl.empty() || !fs::exists(pl / nombre, ec2)) return {};
        return leerArchivo(pl / nombre);
    };
    const std::string tPortada = plantilla("portada.html"), tSeccion = plantilla("seccion.html"),
                      tCierre = plantilla("cierre.html"), tEstilo = plantilla("estilo.css");
    if (tPortada.empty() || tSeccion.empty() || tCierre.empty() || tEstilo.empty()) {
        r.error = ba::i18n::llenar(ba::i18n::traducir("evento", "no encontré las plantillas del sitio en %1"), {pl.string()});
        return r;
    }
    std::size_t nSec = e.bloques.size();
    int totalSeg = 0;
    for (const Bloque& b : e.bloques) totalSeg += b.duracionSeg();
    // La descripción del sitio (buscadores, redes): en el idioma del evento (idiomaDe, el
    // mismo que da ba::i18n::idiomaDelEvento() con el evento abierto), en frases enteras. Queda
    // escrita en el HTML: si cambia el idioma del evento, se rehace al volver a generar el sitio
    // (en la página, vivo.js la vuelve a poner en el idioma de ese momento).
    const std::string nSecciones = en.traducir("evento", "%n sección", nullptr, static_cast<int>(nSec));
    const std::string descripcion =
        !s.bajada.empty() ? s.bajada
        : !nSec           ? en.traducir("evento", "En vivo por internet.")
        : totalSeg ? ba::i18n::llenar(en.traducir("evento", "%1 · %2 · en vivo por internet."), {nSecciones, duracionTexto(totalSeg)})
                   : ba::i18n::llenar(en.traducir("evento", "%1 · en vivo por internet."), {nSecciones});
    const auto llenar = [&](std::string t, const std::string& tituloPagina, const std::string& seccion) {
        reemplazar(t, "{{titulo}}", escapar(tituloPagina));
        reemplazar(t, "{{evento}}", escapar(titulo));
        reemplazar(t, "{{descripcion}}", escapar(descripcion));
        reemplazar(t, "{{seccion}}", escapar(seccion));
        reemplazar(t, "{{color}}", s.color);
        reemplazar(t, "{{fondo}}", s.fondo);
        return t;
    };
    poner("estilo.css", llenar(tEstilo, titulo, ""));
    poner("portada.html", llenar(tPortada, titulo, ""));
    for (std::size_t i = 0; i < nSec; ++i) {
        const Bloque& b = e.bloques[i];
        poner(paginaDeBloque(b, i), llenar(tSeccion, nombreDe(b, i, en) + " — " + titulo, b.id.empty() ? std::to_string(i) : b.id));
    }
    poner("cierre.html", llenar(tCierre, titulo, ""));
    s.resto["plantillas"] = huellas;
    s.guardar(dir);
    return r;
}

// --- Dónde se ve en vivo ---------------------------------------------------------------

std::string idDeYouTube(const std::string& t) {
    const auto valido = [](const std::string& id) {
        if (id.size() != 11) return false;
        for (char c : id)
            if (!std::isalnum(static_cast<unsigned char>(c)) && c != '-' && c != '_') return false;
        return true;
    };
    if (valido(t)) return t;
    for (const char* m : {"v=", "youtu.be/", "/live/", "/embed/", "/shorts/"}) {
        const auto p = t.find(m);
        if (p == std::string::npos) continue;
        const std::string id = t.substr(p + std::char_traits<char>::length(m), 11);
        if (valido(id)) return id;
    }
    return {};
}

namespace {
std::string urlCodificada(const std::string& s) {
    static const char* hex = "0123456789ABCDEF";
    std::string o;
    for (unsigned char c : s) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') o += static_cast<char>(c);
        else { o += '%'; o += hex[c >> 4]; o += hex[c & 15]; }
    }
    return o;
}
std::string loginDeTwitch(std::string t) {
    for (const char* m : {"twitch.tv/"}) {
        const auto p = t.find(m);
        if (p != std::string::npos) t = t.substr(p + std::char_traits<char>::length(m));
    }
    std::string l;
    for (char c : t) {
        if (c == '/' || c == '?' || c == '#') break;
        if (std::isalnum(static_cast<unsigned char>(c)) || c == '_') l += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return l;
}
} // namespace

json armarVivos(const SitioWeb& s, const json& pl) {
    json out = json::array();
    const json man = s.vivos.is_object() ? s.vivos : json::object();
    const json yt = pl.value("youtube", json::object()), tw = pl.value("twitch", json::object()),
               fb = pl.value("facebook", json::object());
    // YouTube: lo fijado (un video/vivo o un canal), el vivo que transmite la app, o el del canal.
    {
        const std::string m = man.value("youtube", std::string());
        std::string id = idDeYouTube(m);
        std::string canal;
        if (id.empty() && m.find("/channel/") != std::string::npos) {
            canal = m.substr(m.find("/channel/") + 9, 24);
        }
        bool enVivo = false;
        if (id.empty() && canal.empty()) {
            id = yt.value("liveId", std::string());
            enVivo = !id.empty();
            if (id.empty()) canal = yt.value("canalId", std::string());
        }
        if (!id.empty())
            out.push_back({{"plataforma", "youtube"}, {"nombre", "YouTube"}, {"enVivo", enVivo},
                           {"url", "https://www.youtube.com/watch?v=" + id},
                           {"embed", "https://www.youtube.com/embed/" + id + "?autoplay=1&mute=1&playsinline=1"}});
        else if (!canal.empty())
            out.push_back({{"plataforma", "youtube"}, {"nombre", "YouTube"}, {"enVivo", false},
                           {"url", "https://www.youtube.com/channel/" + canal + "/live"},
                           {"embed", "https://www.youtube.com/embed/live_stream?channel=" + canal + "&autoplay=1&mute=1"}});
    }
    // Twitch: el canal (fijado o el de la cuenta).
    {
        std::string login = loginDeTwitch(man.value("twitch", std::string()));
        if (login.empty()) login = tw.value("login", std::string());
        if (!login.empty())
            out.push_back({{"plataforma", "twitch"}, {"nombre", "Twitch"}, {"enVivo", tw.value("enVivo", false)},
                           {"url", "https://www.twitch.tv/" + login},
                           {"embed", "https://player.twitch.tv/?channel=" + login + "&muted=true"}});
    }
    // Facebook: el vivo (fijado, o el que creó la app en la página).
    {
        std::string url = man.value("facebook", std::string());
        bool enVivo = false;
        if (url.empty()) {
            url = fb.value("url", std::string());
            const std::string live = fb.value("liveId", std::string()), page = fb.value("pageId", std::string());
            if (url.empty() && !live.empty() && !page.empty()) url = "https://www.facebook.com/" + page + "/videos/" + live + "/";
            enVivo = !live.empty();
        }
        if (url.rfind("http", 0) != 0 && !url.empty()) url = "https://www.facebook.com/" + url;
        if (!url.empty())
            out.push_back({{"plataforma", "facebook"}, {"nombre", "Facebook"}, {"enVivo", enVivo}, {"url", url},
                           {"embed", "https://www.facebook.com/plugins/video.php?href=" + urlCodificada(url) +
                                         "&show_text=false&autoplay=true&mute=true"}});
    }
    return out;
}

// --- Estado público ------------------------------------------------------------------

json estadoPublico(const Evento& e, const SitioWeb& s, const std::string& estado, int bloque, std::int64_t inicioMs,
                   std::int64_t arrancoMs, std::int64_t ahoraMs, const json& vivos) {
    ExtrasVivo x;
    x.vivos = vivos;
    return estadoPublico(e, s, estado, bloque, inicioMs, arrancoMs, ahoraMs, x);
}

json estadoPublico(const Evento& e, const SitioWeb& s, const std::string& estado, int bloque, std::int64_t inicioMs,
                   std::int64_t arrancoMs, std::int64_t ahoraMs, const ExtrasVivo& x) {
    const json& vivos = x.vivos;
    // La sala puede no haber llegado todavía (null): value() sobre null tira una excepción.
    const json sala = x.sala.is_object() ? x.sala : json::object();
    json secciones = json::array();
    std::int64_t previsto = inicioMs; // la hora prevista de cada sección, sumando duraciones
    for (std::size_t i = 0; i < e.bloques.size(); ++i) {
        const Bloque& b = e.bloques[i];
        const int bi = static_cast<int>(i);
        std::string est = "pendiente";
        if (estado == "terminado") est = "hecha";
        else if (bloque >= 0) est = bi < bloque ? "hecha" : bi == bloque ? "actual" : "pendiente";
        secciones.push_back({{"id", b.id.empty() ? std::to_string(i) : b.id},
                             {"nombre", nombreDe(b, i, idiomaDe(e))},
                             {"duracionSeg", b.duracionSeg()},
                             {"pagina", paginaDeBloque(b, i)},
                             {"inicioPrevistoMs", inicioMs > 0 ? previsto : 0},
                             {"inicioRealMs", x.inicioReal.count(bi) ? x.inicioReal.at(bi) : 0},
                             {"inicioEstimadoMs", 0},
                             {"estado", est}});
        previsto += static_cast<std::int64_t>(b.duracionSeg()) * 1000;
    }
    // Horario estimado: en vivo, lo que falta de la actual más lo previsto de las que
    // siguen, desde ahora (si se va atrasado, se corre todo); antes de empezar, lo previsto.
    if ((estado == "en_vivo" || estado == "pausa") && bloque >= 0 && ahoraMs > 0) {
        std::int64_t t = ahoraMs + static_cast<std::int64_t>(std::max(0, x.restanteActualSeg)) * 1000;
        for (std::size_t i = 0; i < secciones.size(); ++i) {
            const int bi = static_cast<int>(i);
            if (bi < bloque) secciones[i]["inicioEstimadoMs"] = secciones[i]["inicioRealMs"];
            else if (bi == bloque) secciones[i]["inicioEstimadoMs"] = x.inicioReal.count(bi) ? x.inicioReal.at(bi) : ahoraMs;
            else {
                secciones[i]["inicioEstimadoMs"] = t;
                t += static_cast<std::int64_t>(secciones[i].value("duracionSeg", 0)) * 1000;
            }
        }
    } else {
        for (auto& sec : secciones) sec["inicioEstimadoMs"] = sec["inicioPrevistoMs"];
    }
    std::string pagina = "portada.html";
    json actual = nullptr, siguiente = nullptr;
    if (estado == "terminado") pagina = "cierre.html";
    else if ((estado == "en_vivo" || estado == "pausa") && bloque >= 0 && bloque < static_cast<int>(secciones.size())) {
        actual = secciones[static_cast<std::size_t>(bloque)];
        pagina = actual["pagina"].get<std::string>();
        if (bloque + 1 < static_cast<int>(secciones.size())) siguiente = secciones[static_cast<std::size_t>(bloque + 1)];
    }
    return {{"nombre", s.titulo.empty() ? e.nombre : s.titulo},
            {"bajada", s.bajada},
            {"fecha", e.fecha},
            {"inicio", e.inicio},
            {"huso", e.huso},
            // El idioma de la página (el del evento; si no tiene, el del programa).
            {"idioma", e.idioma.empty() ? ba::i18n::actual().codigo : e.idioma},
            {"inicioMs", inicioMs},
            {"arrancoMs", arrancoMs},
            {"ahoraMs", ahoraMs},
            {"estado", estado},
            {"seccion", actual},
            {"siguiente", siguiente},
            {"secciones", secciones},
            {"pagina", pagina},
            {"barra", s.barra},
            {"color", s.color},
            {"vivos", vivos.is_array() ? vivos : json::array()},
            {"aviso", x.aviso.is_object() && !x.aviso.value("texto", std::string()).empty() ? x.aviso : json(nullptr)},
            {"habla", (estado == "en_vivo" || estado == "pausa") ? x.habla : std::string()},
            {"participacion", s.participacion || e.preguntas},
            {"preguntas", x.preguntas.is_array() ? x.preguntas : json::array()},
            {"enLaSala", x.enLaSala},
            {"invitados", e.invitados},
            {"preguntasPublico", e.preguntas},
            {"aire", sala.value("aire", json(nullptr))},
            {"oradores", sala.value("oradores", json(nullptr))},
            {"ordenDelDia", sala.value("ordenDelDia", json::array())},
            {"quorum", sala.value("quorum", json(nullptr))},
            {"votacion", sala.value("votacion", json(nullptr))},
            {"asistencia", sala.value("asistencia", json(nullptr))},
            // La asamblea: solo si el evento lo es (cuerpo, integrantes y presentes, quórum,
            // punto en tratamiento y los resultados oficiales).
            {"asamblea", e.asamblea ? sala.value("asamblea", json(nullptr)) : json(nullptr)}};
}

std::string resolverPagina(const Evento& e, const std::string& pedidaCruda) {
    std::string p = pedidaCruda;
    std::transform(p.begin(), p.end(), p.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (p == "portada" || p == "portada.html") return "portada.html";
    if (p == "cierre" || p == "cierre.html") return "cierre.html";
    if (p == "estilo" || p == "estilo.css") return "estilo.css";
    for (std::size_t i = 0; i < e.bloques.size(); ++i) {
        const Bloque& b = e.bloques[i];
        std::string n = b.nombre;
        std::transform(n.begin(), n.end(), n.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (p == b.id || p == n || p == std::to_string(i + 1) || p == paginaDeBloque(b, i)) return paginaDeBloque(b, i);
    }
    // Otro archivo propio del sitio: solo un nombre simple (sin carpetas ni «..»).
    const bool simple = !p.empty() && p.find('/') == std::string::npos && p.find('\\') == std::string::npos &&
                        p.find("..") == std::string::npos && p.front() != '.';
    if (simple && (p.size() > 5 && (p.ends_with(".html") || p.ends_with(".css") || p.ends_with(".js")))) return pedidaCruda;
    return {};
}

} // namespace ba::evento
