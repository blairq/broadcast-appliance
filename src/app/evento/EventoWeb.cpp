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


// --- Plantillas -----------------------------------------------------------------
// Sin nada de afuera (ni fuentes ni scripts de internet): el sitio anda aunque la
// sala no tenga salida a internet y no le avisa a nadie quién lo mira.

const char* kEstilo = R"css(/* Lo propio de este evento, encima del estilo del sitio (/evento/_/sitio.css).
   Es tuyo: cambiá el acento, el fondo, las tipografías o lo que quieras pisar. */
:root {
  --accent: {{color}};
  --bg: {{fondo}};
}
)css";

// Lo que va al final de cada página: la ayuda para el que la edita (el asistente o
// una persona). No se ve en el navegador.
const char* kAyuda = R"html(
  <!--
    ESTA PÁGINA ES TUYA. Cambiá el texto, agregá imágenes (ponelas en la carpeta Web/ del
    evento y usalas como <img src="foto.jpg" alt="…" loading="lazy">), videos, links.
    El estilo base es /evento/_/sitio.css (clases: contenedor, seccion, ceja, h1, h2, bajada,
    boton primario/secundario, tarjeta, columnas, programa, numeros, faq, franja, cuenta…);
    lo propio del evento va en estilo.css.

    Datos en vivo (los completa /evento/_/vivo.js):
      data-vivo="estado" | seccion | siguiente | cuenta | cuenta-bloques | transcurrido
      data-vivo="agenda"       una <ol class="programa"> con <li data-seccion="id">
      data-vivo="reproductor"  el vivo embebido con pestañas YouTube / Twitch / Facebook
                               (data-plataforma="youtube|twitch|facebook" elige cuál va primero)
      data-vivo="vivos"        botones «Ver en YouTube / Twitch / Facebook»
      data-vivo="donde" | programa   se esconden si no hay vivos / secciones
      data-vivo="progreso" (ancho de una barra) | progreso-texto
    Componentes: data-acordeon (preguntas con aria-expanded), data-menu (menú del celular).

    Un YouTube embebido fijo, sin la API (cambiá ID_DEL_VIDEO):
      <div class="reproductor"><iframe src="https://www.youtube.com/embed/ID_DEL_VIDEO?autoplay=1&mute=1"
        allow="autoplay; encrypted-media; picture-in-picture; fullscreen" allowfullscreen></iframe></div>
    Twitch: https://player.twitch.tv/?channel=CANAL&parent=EL_HOST_DE_ESTA_PAGINA
    Facebook: https://www.facebook.com/plugins/video.php?href=URL_DEL_VIVO_CODIFICADA

    La API: BAEvento.on((e) => { … }) te llama con el estado al cargar y cada vez que cambia;
    BAEvento.estado(), BAEvento.ahora(), BAEvento.hora(ms). En JSON: GET /api/evento y
    GET /api/evento/vivos. El estado: { nombre, bajada, fecha, inicio, huso, inicioMs, arrancoMs,
    estado (proximo|en_vivo|pausa|terminado), seccion, siguiente, secciones [{id, nombre,
    duracionSeg, pagina, inicioPrevistoMs, estado (hecha|actual|pendiente)}], vivos [{plataforma,
    nombre, url, embed, enVivo}], pagina, color }. Es público: nada privado acá.
  -->
</body>
</html>
)html";

const char* kMeses[] = {"ENE", "FEB", "MAR", "ABR", "MAY", "JUN", "JUL", "AGO", "SEP", "OCT", "NOV", "DIC"};
const char* kMesesLargos[] = {"enero", "febrero", "marzo", "abril", "mayo", "junio", "julio",
                              "agosto", "septiembre", "octubre", "noviembre", "diciembre"};
const char* kDias[] = {"DOM", "LUN", "MAR", "MIÉ", "JUE", "VIE", "SÁB"};
const char* kDiasLargos[] = {"domingo", "lunes", "martes", "miércoles", "jueves", "viernes", "sábado"};

struct Fecha {
    int anio = 0, mes = 0, dia = 0, diaSemana = -1;
    bool valida() const { return mes >= 1 && mes <= 12 && dia >= 1; }
};
Fecha fechaDe(const std::string& f) {
    Fecha r;
    if (f.size() != 10) return r;
    try {
        r.anio = std::stoi(f.substr(0, 4));
        r.mes = std::stoi(f.substr(5, 2));
        r.dia = std::stoi(f.substr(8, 2));
    } catch (...) {
        return {};
    }
    // Sakamoto: el día de la semana (0 = domingo).
    static const int t[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
    int y = r.anio - (r.mes < 3 ? 1 : 0);
    r.diaSemana = (y + y / 4 - y / 100 + y / 400 + t[r.mes - 1] + r.dia) % 7;
    return r;
}

int minutosDe(const std::string& hhmm) {
    if (hhmm.size() < 4) return -1;
    try {
        const auto p = hhmm.find(':');
        if (p == std::string::npos) return -1;
        return std::stoi(hhmm.substr(0, p)) * 60 + std::stoi(hhmm.substr(p + 1));
    } catch (...) {
        return -1;
    }
}
std::string hhmm(int min) {
    min = ((min % 1440) + 1440) % 1440;
    char b[8];
    std::snprintf(b, sizeof b, "%02d:%02d", min / 60, min % 60);
    return b;
}
std::string dos(std::size_t n) {
    char b[8];
    std::snprintf(b, sizeof b, "%02zu", n);
    return b;
}
std::string duracionTexto(int seg) {
    if (seg <= 0) return "";
    const int m = (seg + 59) / 60;
    return m >= 60 ? std::to_string(m / 60) + " h" + (m % 60 ? " " + std::to_string(m % 60) + " min" : std::string())
                   : std::to_string(m) + " min";
}
std::string nombreDe(const Bloque& b, std::size_t i) { return b.nombre.empty() ? "Sección " + std::to_string(i + 1) : b.nombre; }
/// «America/Argentina/Buenos_Aires» → «Buenos Aires».
std::string husoCorto(const std::string& h) {
    std::string c = h.substr(h.rfind('/') == std::string::npos ? 0 : h.rfind('/') + 1);
    for (char& ch : c) if (ch == '_') ch = ' ';
    return c;
}

/// El programa: una <ol class="programa"> con la hora prevista de cada sección.
std::string agendaHtml(const Evento& e) {
    std::ostringstream o;
    o << "        <ol class=\"programa\" data-vivo=\"agenda\">\n";
    int t = minutosDe(e.inicio);
    for (std::size_t i = 0; i < e.bloques.size(); ++i) {
        const Bloque& b = e.bloques[i];
        std::string meta;
        if (t >= 0) meta = hhmm(t);
        const std::string d = duracionTexto(b.duracionSeg());
        if (!d.empty()) meta += (meta.empty() ? "" : " · ") + d;
        o << "          <li data-seccion=\"" << escapar(b.id.empty() ? std::to_string(i) : b.id) << "\"><a href=\""
          << paginaDeBloque(b, i) << "\"><span class=\"nombre\">" << escapar(nombreDe(b, i)) << "</span><span class=\"meta\">"
          << escapar(meta) << "</span></a></li>\n";
        if (t >= 0) t += (b.duracionSeg() + 59) / 60;
    }
    o << "        </ol>\n";
    return o.str();
}

std::string cuandoTexto(const Evento& e) {
    const Fecha f = fechaDe(e.fecha);
    std::string t;
    if (f.valida()) t = std::string(kDias[f.diaSemana]) + " " + std::to_string(f.dia) + " " + kMeses[f.mes - 1];
    if (!e.inicio.empty()) t += (t.empty() ? "" : " · ") + e.inicio;
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

    // --- Lo común ----------------------------------------------------------------------
    const Fecha f = fechaDe(e.fecha);
    const std::string cuando = cuandoTexto(e);
    const std::string husoTxt = e.huso.empty() ? std::string() : husoCorto(e.huso);
    const std::size_t nSec = e.bloques.size();
    int totalSeg = 0;
    for (const Bloque& b : e.bloques) totalSeg += b.duracionSeg();
    const std::string resumen = (nSec ? std::to_string(nSec) + (nSec == 1 ? " sección" : " secciones") : std::string("El programa se arma"))
                                + (totalSeg ? " · " + duracionTexto(totalSeg) : std::string()) + " · en vivo por internet.";
    const std::string bajada = s.bajada.empty() ? resumen : s.bajada;
    // La palabra de fondo del hero: la primera palabra del título (o dos, si es corta).
    std::string marca = titulo.substr(0, titulo.find(' '));
    if (marca.size() < 5 && titulo.find(' ') != std::string::npos) {
        const auto p2 = titulo.find(' ', titulo.find(' ') + 1);
        marca = titulo.substr(0, p2);
    }
    const std::string primera = nSec ? paginaDeBloque(e.bloques[0], 0) : std::string("portada.html");

    const auto cabeza = [&](const std::string& t, const std::string& desc) {
        std::ostringstream o;
        o << "<!doctype html>\n<html lang=\"es\">\n<head>\n"
          << "  <meta charset=\"utf-8\" />\n"
          << "  <meta name=\"viewport\" content=\"width=device-width, initial-scale=1, viewport-fit=cover\" />\n"
          << "  <title>" << escapar(t) << "</title>\n"
          << "  <meta name=\"description\" content=\"" << escapar(desc) << "\" />\n"
          << "  <meta name=\"theme-color\" content=\"#0b0b0c\" />\n"
          << "  <meta property=\"og:type\" content=\"website\" />\n"
          << "  <meta property=\"og:title\" content=\"" << escapar(t) << "\" />\n"
          << "  <meta property=\"og:description\" content=\"" << escapar(desc) << "\" />\n"
          << "  <!-- El estilo del sitio (lo trae la sala) y lo propio de este evento (estilo.css). -->\n"
          << "  <link rel=\"stylesheet\" href=\"/evento/_/sitio.css\" />\n"
          << "  <link rel=\"stylesheet\" href=\"estilo.css\" />\n"
          << "  <script src=\"/evento/_/vivo.js\" defer></script>\n";
        // Datos estructurados (schema.org Event): los buscadores entienden qué y cuándo.
        json ld{{"@context", "https://schema.org"}, {"@type", "Event"}, {"name", titulo}, {"description", desc},
                {"eventAttendanceMode", "https://schema.org/OnlineEventAttendanceMode"},
                {"eventStatus", "https://schema.org/EventScheduled"},
                {"location", {{"@type", "VirtualLocation"}, {"url", "/"}}}};
        if (f.valida()) ld["startDate"] = e.fecha + (e.inicio.empty() ? std::string() : "T" + e.inicio);
        std::string ldTxt = ld.dump(-1, ' ', false, json::error_handler_t::replace);
        reemplazar(ldTxt, "</", "<\\/");
        o << "  <script type=\"application/ld+json\">" << ldTxt << "</script>\n"
          << "</head>\n<body>\n  <a class=\"saltar\" href=\"#contenido\">Saltar al contenido</a>\n";
        return o.str();
    };
    // La barra de arriba. `base`: «» en la portada (anclas locales) o «portada.html».
    const auto nav = [&](const std::string& base) {
        std::ostringstream o;
        o << "  <header class=\"nav\">\n    <div class=\"contenedor\">\n"
          << "      <a class=\"marca\" href=\"portada.html\">" << escapar(titulo) << "</a>\n"
          << "      <nav class=\"nav-links\" aria-label=\"Secciones del sitio\">\n"
          << "        <a href=\"" << base << "#vivo\">En vivo</a>\n"
          << "        <a href=\"" << base << "#programa\">Programa</a>\n"
          << "        <a href=\"" << base << "#como\">Cómo seguirlo</a>\n"
          << "        <a href=\"" << base << "#preguntas\">Preguntas</a>\n"
          << "      </nav>\n"
          << "      <div class=\"nav-acciones\">\n"
          << "        <span class=\"estado\" data-vivo=\"estado\">Próximamente</span>\n"
          << "        <a class=\"boton primario chico\" href=\"" << base << "#vivo\">Ver en vivo</a>\n"
          << "        <button class=\"hamburguesa\" type=\"button\" data-menu aria-controls=\"menu-movil\" aria-expanded=\"false\""
             " aria-label=\"Abrir el menú\"><span></span></button>\n"
          << "      </div>\n    </div>\n  </header>\n"
          << "  <nav class=\"menu-movil\" id=\"menu-movil\" hidden aria-label=\"Menú\">\n"
          << "    <a href=\"" << base << "#vivo\">En vivo</a>\n    <a href=\"" << base << "#programa\">Programa</a>\n"
          << "    <a href=\"" << base << "#como\">Cómo seguirlo</a>\n    <a href=\"" << base << "#preguntas\">Preguntas</a>\n"
          << "    <a href=\"/invitados/\">Entrar a la sala</a>\n  </nav>\n";
        return o.str();
    };
    // El reproductor: pestañas YouTube / Twitch / Facebook y los botones de cada una.
    const auto reproductor = [&](const std::string& vacio) {
        std::ostringstream o;
        o << "        <div class=\"reproductor-caja\">\n"
          << "          <div data-vivo=\"reproductor\" data-plataforma=\"youtube\" data-vacio=\"" << escapar(vacio) << "\"></div>\n"
          << "          <nav class=\"plataformas\" data-vivo=\"vivos\" aria-label=\"Verlo en cada plataforma\"></nav>\n"
          << "        </div>\n";
        return o.str();
    };
    const auto pie = [&]() {
        std::ostringstream o;
        o << "  <footer class=\"pie\">\n    <div class=\"contenedor\">\n      <div class=\"pie-grilla\">\n"
          << "        <div>\n          <p class=\"h3\" style=\"margin:0 0 10px\">" << escapar(titulo) << "</p>\n"
          << "          <p>" << escapar(cuando.empty() ? std::string("En vivo por internet.") : cuando + (husoTxt.empty() ? "" : " (hora de " + husoTxt + ")"))
          << "</p>\n          <p>Esta página se actualiza sola mientras el evento pasa.</p>\n        </div>\n"
          << "        <div>\n          <h4>El evento</h4>\n          <ul>\n"
          << "            <li><a href=\"portada.html#vivo\">En vivo</a></li>\n"
          << "            <li><a href=\"portada.html#programa\">Programa</a></li>\n"
          << "            <li><a href=\"portada.html#preguntas\">Preguntas</a></li>\n"
          << "            <li><a href=\"cierre.html\">Cierre</a></li>\n          </ul>\n        </div>\n"
          << "        <div>\n          <h4>Secciones</h4>\n          <ul>\n";
        for (std::size_t i = 0; i < nSec && i < 6; ++i)
            o << "            <li><a href=\"" << paginaDeBloque(e.bloques[i], i) << "\">" << dos(i + 1) << " · "
              << escapar(nombreDe(e.bloques[i], i)) << "</a></li>\n";
        o << "          </ul>\n        </div>\n"
          << "        <div>\n          <h4>Participar</h4>\n          <ul>\n"
          << "            <li><a href=\"/invitados/\">Entrar a la sala</a></li>\n"
          << "            <li><a href=\"portada.html#como\">Cómo seguirlo</a></li>\n          </ul>\n        </div>\n"
          << "      </div>\n      <div class=\"pie-final\">\n"
          << "        <span>" << escapar(titulo) << (f.valida() ? " · " + std::to_string(f.anio) : std::string()) << "</span>\n"
          << "        <span>En vivo. Sin vueltas.</span>\n      </div>\n    </div>\n  </footer>\n";
        return o.str();
    };
    const auto programa = [&](const std::string& numero, const std::string& h2) {
        std::ostringstream o;
        o << "    <section class=\"seccion\" id=\"programa\" data-vivo=\"programa\">\n      <div class=\"contenedor\">\n"
          << "        <div class=\"cabeza-seccion\">\n          <p class=\"ceja\">" << numero << " / Programa</p>\n"
          << "          <h2 class=\"h2\">" << h2 << "</h2>\n"
          << "          <p class=\"bajada\">" << escapar(resumen) << " Tocá una sección para ver de qué se trata.</p>\n"
          << "        </div>\n" << agendaHtml(e) << "      </div>\n    </section>\n";
        return o.str();
    };

    // --- Portada -------------------------------------------------------------------------
    {
        std::ostringstream o;
        o << cabeza(titulo, bajada) << nav("") << "  <main id=\"contenido\">\n"
          // 1. Hero
          << "    <section class=\"hero\">\n      <div class=\"hero-marca\" aria-hidden=\"true\">" << escapar(marca) << "</div>\n"
          << "      <div class=\"contenedor\">\n"
          << "        <p class=\"ceja\">" << (cuando.empty() ? std::string("En vivo") : escapar(cuando))
          << (husoTxt.empty() ? std::string() : " · " + escapar(husoTxt)) << " · <span data-vivo=\"estado\">Próximamente</span></p>\n"
          << "        <h1 class=\"h1\">" << escapar(titulo) << "</h1>\n"
          << "        <p class=\"bajada\">" << escapar(bajada) << "</p>\n"
          << "        <div class=\"hero-acciones\">\n"
          << "          <a class=\"boton primario\" href=\"#vivo\">Ver en vivo</a>\n"
          << "          <a class=\"boton secundario\" href=\"#programa\">Ver el programa</a>\n        </div>\n"
          << "        <p class=\"micro\">En vivo por internet · Sin registrarse"
          << (husoTxt.empty() ? std::string() : " · Hora de " + escapar(husoTxt)) << "</p>\n"
          << "      </div>\n    </section>\n";
        // 2. Cuenta regresiva
        o << "    <div class=\"contenedor\">\n      <div class=\"franja\">\n";
        if (f.valida())
            o << "        <div style=\"display:flex;gap:16px;align-items:center\">\n"
              << "          <div class=\"fecha-bloque\"><div class=\"mes\">" << kMeses[f.mes - 1] << "</div><div class=\"dia\">"
              << f.dia << "</div></div>\n"
              << "          <div><p class=\"ceja\" style=\"margin:0 0 4px\">Empieza</p><b>" << kDiasLargos[f.diaSemana] << " "
              << f.dia << " de " << kMesesLargos[f.mes - 1] << (e.inicio.empty() ? std::string() : ", " + e.inicio + " h") << "</b></div>\n"
              << "        </div>\n";
        else
            o << "        <div><p class=\"ceja\" style=\"margin:0 0 4px\">Estado</p><b data-vivo=\"estado\">Próximamente</b></div>\n";
        o << "        <div class=\"cuenta\" data-vivo=\"cuenta-bloques\" aria-label=\"Cuenta regresiva\"></div>\n"
          << "      </div>\n    </div>\n";
        // 3. Marquesina con las secciones (la lista va dos veces: la animación corre la mitad).
        if (nSec) {
            o << "    <div class=\"marquesina\" aria-hidden=\"true\" style=\"margin-top:64px\"><div class=\"marquesina-pista\">";
            for (int vuelta = 0; vuelta < 2; ++vuelta)
                for (std::size_t r = 0; r < std::max<std::size_t>(1, 6 / nSec + 1); ++r)
                    for (std::size_t i = 0; i < nSec; ++i) o << "<span>" << escapar(nombreDe(e.bloques[i], i)) << "</span>";
            o << "</div></div>\n";
        }
        // 4. En vivo
        o << "    <section class=\"seccion\" id=\"vivo\">\n      <div class=\"contenedor\">\n"
          << "        <div class=\"cabeza-seccion\">\n          <p class=\"ceja\">01 / En vivo</p>\n"
          << "          <h2 class=\"h2\">Seguilo acá. Sin vueltas.</h2>\n"
          << "          <p class=\"bajada\">Elegí la plataforma: YouTube, Twitch o Facebook. Arranca sin sonido; tocá el video para escucharlo.</p>\n"
          << "        </div>\n"
          << reproductor("El vivo aparece acá cuando empiece la transmisión.")
          << "      </div>\n    </section>\n";
        // 5. Programa
        o << programa("02", "El programa, de punta a punta.");
        // 6. Cómo seguirlo (A / B / C)
        o << "    <section class=\"seccion\" id=\"como\">\n      <div class=\"contenedor\">\n"
          << "        <div class=\"cabeza-seccion\">\n          <p class=\"ceja\">03 / Cómo seguirlo</p>\n"
          << "          <h2 class=\"h2\">Tres formas de estar.</h2>\n        </div>\n"
          << "        <div class=\"columnas\">\n"
          << "          <article class=\"tarjeta\"><span class=\"letra\">A</span><h3 class=\"h3\">Mirá el vivo</h3>"
             "<p>En esta misma página, o en YouTube, Twitch o Facebook. Sin cuenta y desde el celular.</p>"
             "<a class=\"enlace\" href=\"#vivo\">Ir al vivo →</a></article>\n"
          << "          <article class=\"tarjeta\"><span class=\"letra\">B</span><h3 class=\"h3\">Seguí cada parte</h3>"
             "<p>La página cambia sola a la sección que se está tratando. Si llegás tarde, sabés dónde estamos.</p>"
             "<a class=\"enlace\" href=\"" << primera << "\">Ver la primera sección →</a></article>\n"
          << "          <article class=\"tarjeta\"><span class=\"letra\">C</span><h3 class=\"h3\">Participá</h3>"
             "<p>Si te invitaron a hablar, entrá a la sala con tu enlace y tu PIN. Te ven y te escuchan en vivo.</p>"
             "<a class=\"enlace\" href=\"/invitados/\">Entrar a la sala →</a></article>\n"
          << "        </div>\n      </div>\n    </section>\n";
        // 7. En números
        o << "    <section class=\"seccion\">\n      <div class=\"contenedor\">\n"
          << "        <div class=\"cabeza-seccion\">\n          <p class=\"ceja\">04 / En números</p>\n"
          << "          <h2 class=\"h2\">Lo que hay que saber.</h2>\n        </div>\n"
          << "        <div class=\"numeros\">\n"
          << "          <div><b>" << dos(nSec) << "</b><span>Secciones</span></div>\n"
          << "          <div><b>" << (totalSeg ? std::to_string((totalSeg + 59) / 60) : std::string("—")) << "</b><span>Minutos de programa</span></div>\n"
          << "          <div><b>" << (e.inicio.empty() ? std::string("—") : escapar(e.inicio)) << "</b><span>Hora de inicio</span></div>\n"
          << "          <div><b>0</b><span>Registros necesarios</span></div>\n"
          << "        </div>\n      </div>\n    </section>\n";
        // 8. Preguntas
        const std::vector<std::pair<std::string, std::string>> faq = {
            {"¿Dónde lo veo?", "Acá mismo, en «En vivo», o en YouTube, Twitch o Facebook con los botones de abajo del video. Elegí la que te quede cómoda."},
            {"¿Hay que registrarse?", "No. Entrás y mirás. No pedimos cuenta, mail ni datos."},
            {"¿A qué hora empieza en mi país?",
             e.inicio.empty() ? std::string("La hora la vas a ver acá apenas esté confirmada.")
                              : "Empieza a las " + e.inicio + (husoTxt.empty() ? "" : " (hora de " + husoTxt + ")") +
                                    ". La cuenta regresiva de arriba ya está en tu hora."},
            {"¿Queda grabado?", "Sí. Cuando termine, la grabación queda en el mismo lugar del vivo y en las plataformas."},
            {"¿Cómo participo?", "Si te invitaron a hablar, entrá a la sala con tu enlace y tu PIN. Si solo querés mirar, no hace falta nada."},
            {"¿Lo puedo ver desde el celular?", "Sí. La página está hecha para el celular y el video se adapta a la pantalla."}};
        o << "    <section class=\"seccion\" id=\"preguntas\">\n      <div class=\"contenedor\">\n"
          << "        <div class=\"cabeza-seccion\">\n          <p class=\"ceja\">05 / Preguntas</p>\n"
          << "          <h2 class=\"h2\">Lo que siempre se pregunta.</h2>\n        </div>\n"
          << "        <div class=\"faq\" data-acordeon>\n";
        for (std::size_t i = 0; i < faq.size(); ++i)
            o << "          <h3><button type=\"button\" aria-expanded=\"false\" aria-controls=\"r" << i << "\">" << escapar(faq[i].first)
              << "</button></h3>\n          <div class=\"respuesta\" id=\"r" << i << "\" hidden><p>" << escapar(faq[i].second) << "</p></div>\n";
        o << "        </div>\n      </div>\n    </section>\n";
        // 9. Cierre de la página
        o << "    <section class=\"seccion cta\">\n      <div class=\"contenedor\">\n"
          << "        <p class=\"ceja\">" << (cuando.empty() ? std::string("En vivo") : escapar(cuando)) << "</p>\n"
          << "        <h2 class=\"h2\">Nos vemos ahí.</h2>\n"
          << "        <div class=\"hero-acciones\">\n          <a class=\"boton primario\" href=\"#vivo\">Ver en vivo</a>\n"
          << "          <a class=\"boton secundario\" href=\"#programa\">Ver el programa</a>\n        </div>\n"
          << "      </div>\n    </section>\n  </main>\n" << pie() << kAyuda;
        poner("portada.html", o.str());
    }

    // --- Una por sección -----------------------------------------------------------------------
    for (std::size_t i = 0; i < nSec; ++i) {
        const Bloque& b = e.bloques[i];
        const std::string nombre = nombreDe(b, i);
        const std::string sigue = i + 1 < nSec ? nombreDe(e.bloques[i + 1], i + 1) : std::string("El cierre");
        std::ostringstream o;
        o << cabeza(nombre + " — " + titulo, nombre + ". Parte de " + titulo + ".") << nav("portada.html")
          << "  <main id=\"contenido\">\n"
          << "    <section class=\"hero\">\n      <div class=\"hero-marca\" aria-hidden=\"true\">" << dos(i + 1) << "</div>\n"
          << "      <div class=\"contenedor\">\n"
          << "        <p class=\"ceja\">Sección " << dos(i + 1) << " / " << dos(nSec)
          << (b.duracionSeg() ? " · " + duracionTexto(b.duracionSeg()) : std::string())
          << " · <span data-vivo=\"estado\">Próximamente</span></p>\n"
          << "        <h1 class=\"h1\">" << escapar(nombre) << "</h1>\n"
          << "        <!-- Una línea concreta de qué pasa en esta parte: quién habla, qué se decide. -->\n"
          << "        <p class=\"bajada\">Parte de " << escapar(titulo) << ".</p>\n"
          << "        <div class=\"hero-acciones\">\n          <a class=\"boton primario\" href=\"#vivo\">Ver en vivo</a>\n"
          << "          <a class=\"boton secundario\" href=\"portada.html#programa\">Todo el programa</a>\n        </div>\n"
          << "      </div>\n    </section>\n"
          // Ahora / sigue / lleva
          << "    <div class=\"contenedor\">\n      <div class=\"ahora\">\n"
          << "        <div class=\"tarjeta\"><p class=\"ceja\" style=\"margin:0\">Ahora</p><b data-vivo=\"seccion\">" << escapar(nombre) << "</b>\n"
          << "          <div class=\"progreso\"><div data-vivo=\"progreso\"></div></div><p class=\"tenue mono\" style=\"font-size:12px;margin:8px 0 0\" data-vivo=\"progreso-texto\"></p></div>\n"
          << "        <div class=\"tarjeta\"><p class=\"ceja\" style=\"margin:0\">Sigue</p><b data-vivo=\"siguiente\">" << escapar(sigue) << "</b></div>\n"
          << "        <div class=\"tarjeta\"><p class=\"ceja\" style=\"margin:0\">Lleva</p><b class=\"mono\" data-vivo=\"transcurrido\">—</b></div>\n"
          << "      </div>\n    </div>\n"
          // En vivo
          << "    <section class=\"seccion\" id=\"vivo\" style=\"margin-top:64px\">\n      <div class=\"contenedor\">\n"
          << "        <div class=\"cabeza-seccion\">\n          <p class=\"ceja\">01 / En vivo</p>\n"
          << "          <h2 class=\"h2\">Está pasando ahora.</h2>\n        </div>\n"
          << reproductor("El vivo aparece acá cuando empiece la transmisión.")
          << "      </div>\n    </section>\n"
          << programa("02", "Dónde estamos.");
        // Anterior / siguiente
        o << "    <div class=\"contenedor\" style=\"padding-bottom:96px\">\n      <nav class=\"pasos\" aria-label=\"Secciones\">\n"
          << "        <a class=\"paso\" href=\"" << (i == 0 ? std::string("portada.html") : paginaDeBloque(e.bloques[i - 1], i - 1))
          << "\"><p class=\"ceja\">← Anterior</p><b>" << escapar(i == 0 ? std::string("Portada") : nombreDe(e.bloques[i - 1], i - 1)) << "</b></a>\n"
          << "        <a class=\"paso siguiente\" href=\"" << (i + 1 < nSec ? paginaDeBloque(e.bloques[i + 1], i + 1) : std::string("cierre.html"))
          << "\"><p class=\"ceja\">Siguiente →</p><b>" << escapar(sigue) << "</b></a>\n"
          << "      </nav>\n    </div>\n  </main>\n" << pie() << kAyuda;
        poner(paginaDeBloque(b, i), o.str());
    }

    // --- Cierre ---------------------------------------------------------------------------------
    {
        std::ostringstream o;
        o << cabeza(titulo + " — Gracias", "Terminó " + titulo + ". La grabación y el programa completo.") << nav("portada.html")
          << "  <main id=\"contenido\">\n"
          << "    <section class=\"hero\">\n      <div class=\"hero-marca\" aria-hidden=\"true\">Gracias</div>\n"
          << "      <div class=\"contenedor\">\n"
          << "        <p class=\"ceja\">" << (cuando.empty() ? std::string("Terminó") : escapar(cuando)) << " · <span data-vivo=\"estado\">Terminó</span></p>\n"
          << "        <h1 class=\"h1\">Gracias por estar.</h1>\n"
          << "        <p class=\"bajada\">" << escapar(titulo) << " terminó. Acá queda la grabación y todo lo que pasó.</p>\n"
          << "        <div class=\"hero-acciones\">\n          <a class=\"boton primario\" href=\"#vivo\">Volver a verlo</a>\n"
          << "          <a class=\"boton secundario\" href=\"#programa\">Lo que pasó</a>\n        </div>\n"
          << "      </div>\n    </section>\n"
          << "    <div class=\"contenedor\">\n      <div class=\"numeros\">\n"
          << "        <div><b>" << dos(nSec) << "</b><span>Secciones</span></div>\n"
          << "        <div><b>" << (totalSeg ? std::to_string((totalSeg + 59) / 60) : std::string("—")) << "</b><span>Minutos</span></div>\n"
          << "        <div><b>" << (f.valida() ? std::to_string(f.dia) + " " + kMeses[f.mes - 1] : std::string("—")) << "</b><span>Fecha</span></div>\n"
          << "        <div><b>1</b><span>Lugar: este</span></div>\n"
          << "      </div>\n    </div>\n"
          << "    <section class=\"seccion\" id=\"vivo\" style=\"margin-top:64px\">\n      <div class=\"contenedor\">\n"
          << "        <div class=\"cabeza-seccion\">\n          <p class=\"ceja\">01 / La grabación</p>\n"
          << "          <h2 class=\"h2\">Volvé a verlo.</h2>\n        </div>\n"
          << reproductor("La grabación va a estar acá.")
          << "      </div>\n    </section>\n"
          << programa("02", "Lo que pasó.")
          << "  </main>\n" << pie() << kAyuda;
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
