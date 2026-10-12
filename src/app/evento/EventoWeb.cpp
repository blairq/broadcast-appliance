// SPDX-License-Identifier: GPL-2.0-or-later
// src/app/evento/EventoWeb.cpp — ver EventoWeb.hpp
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

std::string mmss(int seg) {
    if (seg <= 0) return "";
    const int m = (seg + 59) / 60;
    return m >= 60 ? std::to_string(m / 60) + " h " + (m % 60 ? std::to_string(m % 60) + " min" : std::string())
                   : std::to_string(m) + " min";
}

// --- Plantillas -----------------------------------------------------------------
// Sin nada de afuera (ni fuentes ni scripts de internet): el sitio anda aunque la
// sala no tenga salida a internet y no le avisa a nadie quién lo mira.

const char* kEstilo = R"css(/* El estilo del sitio del evento. Cambialo a gusto: es tuyo (Web/estilo.css). */
:root {
  --color: {{color}};
  --fondo: {{fondo}};
  --texto: #f3f4f6;
  --tenue: #9aa0aa;
  --tarjeta: rgba(255, 255, 255, 0.05);
  --borde: rgba(255, 255, 255, 0.10);
}
* { box-sizing: border-box; }
html, body { margin: 0; min-height: 100%; }
body { background: var(--fondo); color: var(--texto);
  font-family: -apple-system, "SF Pro Display", "Segoe UI", Roboto, "Noto Sans", sans-serif; line-height: 1.5; }
main { max-width: 860px; margin: 0 auto; padding: 48px 20px 64px; }
.etiqueta { color: var(--color); font-weight: 700; letter-spacing: 1.5px; text-transform: uppercase; font-size: 13px; margin: 0 0 8px; }
h1 { font-size: clamp(30px, 6vw, 52px); line-height: 1.1; margin: 0 0 14px; }
h2 { font-size: 18px; margin: 36px 0 12px; color: var(--tenue); text-transform: uppercase; letter-spacing: 1px; }
.bajada { font-size: 19px; color: var(--tenue); margin: 0 0 24px; }
.tarjeta { background: var(--tarjeta); border: 1px solid var(--borde); border-radius: 16px; padding: 18px 20px; margin: 16px 0; }
.cuando { font-size: 18px; }
.cuenta { font-size: 34px; font-weight: 800; color: var(--color); font-variant-numeric: tabular-nums; }
.agenda { list-style: none; padding: 0; margin: 0; counter-reset: s; }
.agenda li { counter-increment: s; display: flex; gap: 14px; align-items: baseline; padding: 12px 14px; border-radius: 12px; }
.agenda li::before { content: counter(s); min-width: 26px; height: 26px; border-radius: 50%; display: inline-grid; place-items: center;
  font-size: 13px; font-weight: 700; background: var(--borde); }
.agenda li .dur { margin-left: auto; color: var(--tenue); font-size: 14px; white-space: nowrap; }
.agenda li a { color: inherit; text-decoration: none; }
.agenda li a:hover { color: var(--color); text-decoration: underline; }
.agenda li.hecha { opacity: 0.5; }
a { color: var(--color); }
.pasos { display: flex; gap: 10px; justify-content: space-between; margin: 32px 0 0; flex-wrap: wrap; }
.pasos a { padding: 10px 16px; border-radius: 12px; background: var(--tarjeta); border: 1px solid var(--borde);
  color: var(--texto); text-decoration: none; }
.pasos a:hover { border-color: var(--color); }
nav.arriba { display: flex; gap: 16px; font-size: 14px; margin-bottom: 24px; }
nav.arriba a { color: var(--tenue); text-decoration: none; }
nav.arriba a:hover { color: var(--color); }
.agenda li.actual { background: var(--tarjeta); outline: 2px solid var(--color); }
.agenda li.actual::before { background: var(--color); color: #fff; }
.progreso { height: 8px; border-radius: 4px; background: var(--borde); overflow: hidden; }
.progreso > div { height: 100%; width: 0; background: var(--color); transition: width 0.6s ease; }
.vivo { display: inline-flex; align-items: center; gap: 8px; font-weight: 700; }
.vivo::before { content: ""; width: 10px; height: 10px; border-radius: 50%; background: var(--tenue); }
.vivo.en_vivo::before { background: #ff3b30; box-shadow: 0 0 10px #ff3b30; animation: latido 1.2s infinite; }
@keyframes latido { 50% { opacity: 0.35; } }
.reproductor { position: relative; aspect-ratio: 16 / 9; border-radius: 16px; overflow: hidden; background: #000; margin: 12px 0; }
.reproductor iframe { position: absolute; inset: 0; width: 100%; height: 100%; border: 0; }
.plataformas { display: flex; gap: 10px; flex-wrap: wrap; }
.plataforma { padding: 10px 16px; border-radius: 12px; font-weight: 700; text-decoration: none; color: #fff; }
.plataforma.youtube { background: #ff0000; }
.plataforma.twitch { background: #9146ff; }
.plataforma.facebook { background: #1877f2; }
footer { margin-top: 48px; color: var(--tenue); font-size: 13px; }
@media (prefers-reduced-motion: reduce) { .vivo.en_vivo::before { animation: none; } }
)css";

// Lo de arriba de cada página. vivo.js (lo sirve la sala) completa lo que lleva
// data-vivo y marca la agenda; sin él la página se ve igual, con lo que dice acá.
const char* kCabeza = R"html(<!doctype html>
<html lang="es">
<head>
  <meta charset="utf-8" />
  <meta name="viewport" content="width=device-width, initial-scale=1" />
  <title>{{titulo}}</title>
  <link rel="stylesheet" href="estilo.css" />
  <!-- El estado en vivo (en qué sección va, si está al aire): /evento/_/vivo.js lo
       pone en los elementos con data-vivo="…" y avisa con el evento «evento» de
       window (detail = el estado). Ver la ayuda al final de esta página. -->
  <script src="/evento/_/vivo.js" defer></script>
</head>
<body>
)html";

const char* kAyuda = R"html(
<!--
  ESTA PÁGINA ES TUYA: cambiá el texto, agregá imágenes (ponelas en la carpeta Web/
  del evento y usalas como <img src="foto.jpg">), videos, links, lo que quieras.
  Se muestra mientras se trata esta sección y cambia sola a la siguiente.

  Datos en vivo (los completa vivo.js):
    data-vivo="estado"       En vivo / Próximamente / En pausa / Terminó
    data-vivo="seccion"      la sección que se está tratando
    data-vivo="siguiente"    la que sigue
    data-vivo="cuenta"       cuánto falta para empezar (en la portada)
    data-vivo="transcurrido" cuánto lleva el evento
    data-vivo="agenda"       una <ol> con todas las secciones (marca la actual y
                             se rehace sola si cambia el guion)
    data-vivo="programa"     se esconde mientras el guion no tiene secciones
    data-vivo="vivos"        botones «Ver en YouTube / Twitch / Facebook» (los que haya)
    data-vivo="reproductor"  el vivo embebido; data-plataforma="youtube" para elegir cuál
    data-vivo="donde"        se esconde si no hay dónde verlo

  Un YouTube embebido fijo, sin la API (cambiá ID_DEL_VIDEO por el del vivo):
    <div class="reproductor"><iframe src="https://www.youtube.com/embed/ID_DEL_VIDEO?autoplay=1&mute=1"
      allow="autoplay; encrypted-media; picture-in-picture; fullscreen" allowfullscreen></iframe></div>
  Dónde verlo, en JSON: GET /api/evento/vivos → {vivos: [{plataforma, nombre, url, embed, enVivo}]}

  La API del evento, para dibujar lo que quieras en vivo (docs/funciones/EVENTO-WEB.md):
    BAEvento.on((e) => { … })  te llama con el estado al cargar y cada vez que cambia
    BAEvento.estado()          el último estado      BAEvento.ahora()  la hora de la sala
    GET /api/evento            el mismo estado en JSON (para fetch)
  El estado: { nombre, bajada, fecha, inicio, huso, inicioMs, arrancoMs, estado
  (proximo|en_vivo|pausa|terminado), seccion {id, nombre, pagina, inicioPrevistoMs},
  siguiente, secciones [{id, nombre, duracionSeg, pagina, inicioPrevistoMs, estado
  (hecha|actual|pendiente)}], vivos [{plataforma, nombre, url, embed, enVivo}], pagina, color }
-->
</body>
</html>
)html";

std::string agendaHtml(const Evento& e) {
    std::ostringstream o;
    o << "    <ol class=\"agenda\" data-vivo=\"agenda\">\n";
    for (std::size_t i = 0; i < e.bloques.size(); ++i) {
        const Bloque& b = e.bloques[i];
        o << "      <li data-seccion=\"" << escapar(b.id.empty() ? std::to_string(i) : b.id) << "\"><a href=\""
          << paginaDeBloque(b, i) << "\">" << escapar(b.nombre.empty() ? "Sección " + std::to_string(i + 1) : b.nombre)
          << "</a>";
        const std::string d = mmss(b.duracionSeg());
        if (!d.empty()) o << "<span class=\"dur\">" << d << "</span>";
        o << "</li>\n";
    }
    o << "    </ol>\n";
    return o.str();
}

std::string cuandoTexto(const Evento& e) {
    std::string t;
    if (!e.fecha.empty()) {
        // «2026-10-15» → «15/10/2026»
        if (e.fecha.size() == 10) t = e.fecha.substr(8, 2) + "/" + e.fecha.substr(5, 2) + "/" + e.fecha.substr(0, 4);
        else t = e.fecha;
    }
    if (!e.inicio.empty()) t += (t.empty() ? "" : " · ") + e.inicio + " h";
    if (!e.huso.empty()) t += " (" + e.huso + ")";
    return t;
}

} // namespace

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
    return j;
}

bool SitioWeb::guardar(const fs::path& carpetaWeb) const {
    return escribir(carpetaWeb / "sitio.json", aJson().dump(2, ' ', false, json::error_handler_t::replace) + "\n");
}

std::string subdominioDe(const std::string& nombre) {
    // Las vocales con acento (UTF-8 de dos bytes) a la letra sola; el resto a guion.
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
        else if (!s.empty() && s.back() != '-') s += '-';
    }
    while (!s.empty() && s.back() == '-') s.pop_back();
    if (s.size() > 40) s.resize(40);
    while (!s.empty() && s.back() == '-') s.pop_back();
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
        r.error = "no se pudo crear " + dir.string();
        return r;
    }
    SitioWeb s = SitioWeb::leer(dir, e);
    // Lo que escribió la plantilla, por archivo: si sigue igual (nadie lo tocó), se
    // actualiza con la plantilla y el guion de ahora; si lo editaron, no se toca.
    json huellas = s.resto.value("plantillas", json::object());
    if (!huellas.is_object()) huellas = json::object();
    const std::string titulo = s.titulo.empty() ? (e.nombre.empty() ? std::string("El evento") : e.nombre) : s.titulo;
    const auto poner = [&](const std::string& rel, std::string texto) {
        reemplazar(texto, "{{color}}", s.color);
        reemplazar(texto, "{{fondo}}", s.fondo);
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
            r.error = "no se pudo escribir " + rel;
        }
    };
    poner("estilo.css", kEstilo);

    const std::string cuando = cuandoTexto(e);
    const std::string bajada = s.bajada.empty() ? std::string() : "    <p class=\"bajada\">" + escapar(s.bajada) + "</p>\n";
    const auto cabeza = [&](const std::string& t) {
        std::string c = kCabeza;
        reemplazar(c, "{{titulo}}", escapar(t));
        return c;
    };

    // Portada.
    {
        std::ostringstream o;
        o << cabeza(titulo) << "  <main>\n"
          << "    <p class=\"etiqueta\"><span class=\"vivo\" data-vivo=\"estado\">Próximamente</span></p>\n"
          << "    <h1>" << escapar(titulo) << "</h1>\n"
          << bajada;
        if (!cuando.empty()) o << "    <div class=\"tarjeta\"><div class=\"cuando\">" << escapar(cuando) << "</div>\n"
                               << "      <div class=\"cuenta\" data-vivo=\"cuenta\"></div></div>\n";
        o     << "    <section class=\"en-vivo\" data-vivo=\"donde\">\n"
          << "      <h2>Miralo en vivo</h2>\n"
          << "      <!-- El reproductor del vivo (YouTube primero) y los botones de cada plataforma: los completa vivo.js. -->\n"
          << "      <div class=\"reproductor\" data-vivo=\"reproductor\" data-plataforma=\"youtube\"></div>\n"
          << "      <nav class=\"plataformas\" data-vivo=\"vivos\"></nav>\n"
          << "    </section>\n";
        o << "    <section data-vivo=\"programa\">\n    <h2>Programa</h2>\n" << agendaHtml(e) << "    </section>\n";
        o << "    <div class=\"tarjeta\"><p>¿Te invitaron a participar? <a href=\"/invitados/\">Entrá a la sala</a>.</p></div>\n"
          << "    <footer>Esta página se actualiza sola cuando empieza el evento.</footer>\n  </main>\n" << kAyuda;
        poner("portada.html", o.str());
    }
    // Una por sección.
    for (std::size_t i = 0; i < e.bloques.size(); ++i) {
        const Bloque& b = e.bloques[i];
        const std::string nombre = b.nombre.empty() ? "Sección " + std::to_string(i + 1) : b.nombre;
        std::ostringstream o;
        o << cabeza(titulo + " — " + nombre) << "  <main>\n"
          << "    <nav class=\"arriba\"><a href=\"portada.html\">← Programa completo</a></nav>\n"
          << "    <p class=\"etiqueta\"><span class=\"vivo\" data-vivo=\"estado\">En vivo</span> · Sección " << (i + 1)
          << " de " << e.bloques.size() << "</p>\n"
          << "    <h1>" << escapar(nombre) << "</h1>\n"
          << "    <p class=\"bajada\">" << escapar(titulo) << "</p>\n"
          << "    <div class=\"tarjeta\">\n"
          << "      <!-- Contá acá de qué se trata esta parte: quién habla, el tema, links, imágenes. -->\n"
          << "      <p>Ahora: <strong>" << escapar(nombre) << "</strong>.</p>\n"
          << "      <p>Sigue: <span data-vivo=\"siguiente\">"
          << escapar(i + 1 < e.bloques.size() ? (e.bloques[i + 1].nombre.empty() ? "Sección " + std::to_string(i + 2)
                                                                                   : e.bloques[i + 1].nombre)
                                              : std::string("el cierre"))
          << "</span></p>\n"
          << "    </div>\n"
<< "    <section class=\"en-vivo\" data-vivo=\"donde\">\n"
          << "      <h2>Miralo en vivo</h2>\n"
          << "      <!-- El reproductor del vivo (YouTube primero) y los botones de cada plataforma: los completa vivo.js. -->\n"
          << "      <div class=\"reproductor\" data-vivo=\"reproductor\" data-plataforma=\"youtube\"></div>\n"
          << "      <nav class=\"plataformas\" data-vivo=\"vivos\"></nav>\n"
          << "    </section>\n"
          << "    <div class=\"tarjeta\"><div class=\"progreso\"><div id=\"barraProgreso\"></div></div>\n"
          << "      <p id=\"textoProgreso\"></p></div>\n"
          << "    <script>\n"
          << "      // Ejemplo de la API del evento: BAEvento.on(fn) llama con el estado y en cada cambio.\n"
          << "      window.addEventListener(\"DOMContentLoaded\", () => BAEvento.on((e) => {\n"
          << "        const n = e.secciones.length, i = e.secciones.findIndex((s) => s.estado === \"actual\");\n"
          << "        document.getElementById(\"barraProgreso\").style.width = (n && i >= 0 ? 100 * (i + 1) / n : 0) + \"%\";\n"
          << "        document.getElementById(\"textoProgreso\").textContent = i >= 0 ? `Vamos por la sección ${i + 1} de ${n}` : \"\";\n"
          << "      }));\n"
          << "    </script>\n"
          << "    <section data-vivo=\"programa\">\n    <h2>Programa</h2>\n" << agendaHtml(e) << "    </section>\n";
        // Anterior / siguiente: se puede recorrer el evento aunque no esté en vivo.
        o << "    <nav class=\"pasos\">\n";
        o << "      <a href=\"" << (i == 0 ? std::string("portada.html") : paginaDeBloque(e.bloques[i - 1], i - 1)) << "\">← "
          << (i == 0 ? std::string("Portada") : escapar(e.bloques[i - 1].nombre.empty() ? "Anterior" : e.bloques[i - 1].nombre))
          << "</a>\n";
        o << "      <a href=\"" << (i + 1 < e.bloques.size() ? paginaDeBloque(e.bloques[i + 1], i + 1) : std::string("cierre.html"))
          << "\">" << (i + 1 < e.bloques.size() ? escapar(e.bloques[i + 1].nombre.empty() ? "Siguiente" : e.bloques[i + 1].nombre)
                                                 : std::string("Cierre"))
          << " →</a>\n    </nav>\n";
        o << "    <footer>En vivo · <span data-vivo=\"transcurrido\"></span></footer>\n  </main>\n" << kAyuda;
        poner(paginaDeBloque(b, i), o.str());
    }
    // Cierre.
    {
        std::ostringstream o;
        o << cabeza(titulo) << "  <main>\n"
          << "    <p class=\"etiqueta\"><span class=\"vivo\" data-vivo=\"estado\">Terminó</span></p>\n"
          << "    <h1>¡Gracias por acompañarnos!</h1>\n"
          << "    <p class=\"bajada\">" << escapar(titulo) << "</p>\n";
        o << "    <section data-vivo=\"programa\">\n    <h2>Lo que pasó</h2>\n" << agendaHtml(e) << "    </section>\n";
        o << "  </main>\n" << kAyuda;
        poner("cierre.html", o.str());
    }
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
    json secciones = json::array();
    std::int64_t previsto = inicioMs; // la hora prevista de cada sección, sumando duraciones
    for (std::size_t i = 0; i < e.bloques.size(); ++i) {
        const Bloque& b = e.bloques[i];
        const int bi = static_cast<int>(i);
        std::string est = "pendiente";
        if (estado == "terminado") est = "hecha";
        else if (bloque >= 0) est = bi < bloque ? "hecha" : bi == bloque ? "actual" : "pendiente";
        secciones.push_back({{"id", b.id.empty() ? std::to_string(i) : b.id},
                             {"nombre", b.nombre.empty() ? "Sección " + std::to_string(i + 1) : b.nombre},
                             {"duracionSeg", b.duracionSeg()},
                             {"pagina", paginaDeBloque(b, i)},
                             {"inicioPrevistoMs", inicioMs > 0 ? previsto : 0},
                             {"estado", est}});
        previsto += static_cast<std::int64_t>(b.duracionSeg()) * 1000;
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
            {"vivos", vivos.is_array() ? vivos : json::array()}};
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
