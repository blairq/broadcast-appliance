// ============================================================================
// src/app/evento/TemaEvento.cpp — ver TemaEvento.hpp
// ============================================================================
#include "evento/TemaEvento.hpp"

#include "i18n/I18n.hpp" // QT_TRANSLATE_NOOP: los errores se traducen al mostrarlos
#include "tema/TemaWeb.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iterator>
#include <set>
#include <sstream>

namespace ba::evento {

using json = nlohmann::json;
namespace fs = std::filesystem;

namespace {

bool escribirAtomico(const fs::path& p, const std::string& texto) {
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

std::string leerTodo(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return {};
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

std::string texto(const json& o, const char* k) {
    if (!o.is_object()) return {};
    const auto it = o.find(k);
    return it != o.end() && it->is_string() ? it->get<std::string>() : std::string();
}

std::string minusculas(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

} // namespace

// --- tema.json ------------------------------------------------------------------------

TemaEvento TemaEvento::deJson(const json& j) {
    TemaEvento t;
    if (!j.is_object()) return t;
    t.resto = j;
    t.existe = true;
    t.modo = texto(j, "modo");
    const json c = j.value("colores", json::object());
    t.colores = {texto(c, "acento"), texto(c, "acentoTexto"), texto(c, "fondo"),
                 texto(c, "superficie"), texto(c, "texto"), texto(c, "textoSuave")};
    const json tip = j.value("tipografia", json::object());
    t.tipografia.texto = texto(tip, "texto");
    t.tipografia.titulos = texto(tip, "titulos");
    if (tip.is_object() && tip.contains("archivos") && tip["archivos"].is_array())
        for (const json& a : tip["archivos"]) {
            Fuente f{texto(a, "familia"), texto(a, "archivo"), texto(a, "peso"), texto(a, "estilo")};
            if (f.peso.empty()) f.peso = "400";
            if (f.estilo.empty()) f.estilo = "normal";
            if (!f.familia.empty() && !f.archivo.empty()) t.tipografia.archivos.push_back(f);
        }
    const json forma = j.value("forma", json::object());
    if (forma.is_object() && forma.contains("radio") && forma["radio"].is_number_integer()) t.radio = forma["radio"].get<int>();
    const json m = j.value("marca", json::object());
    t.marca = {texto(m, "nombre"), texto(m, "logo"), texto(m, "logoClaro"), texto(m, "favicon")};
    const json al = j.value("alcance", json::object());
    if (const std::string p = texto(al, "publico"); !p.empty()) t.alcance.publico = p;
    if (const std::string c2 = texto(al, "control"); !c2.empty()) t.alcance.control = c2;
    return t;
}

json TemaEvento::aJson() const {
    json j = resto.is_object() ? resto : json::object();
    // Cada objeto de adentro también conserva lo suyo: se escribe encima de lo que había.
    const auto sub = [&j](const char* k) -> json& {
        if (!j.contains(k) || !j[k].is_object()) j[k] = json::object();
        return j[k];
    };
    j["modo"] = modo;
    json& c = sub("colores");
    c["acento"] = colores.acento;
    c["acentoTexto"] = colores.acentoTexto;
    c["fondo"] = colores.fondo;
    c["superficie"] = colores.superficie;
    c["texto"] = colores.texto;
    c["textoSuave"] = colores.textoSuave;
    json& tip = sub("tipografia");
    tip["texto"] = tipografia.texto;
    tip["titulos"] = tipografia.titulos;
    json arch = json::array();
    for (const Fuente& f : tipografia.archivos)
        arch.push_back({{"familia", f.familia}, {"archivo", f.archivo}, {"peso", f.peso}, {"estilo", f.estilo}});
    tip["archivos"] = arch;
    json& forma = sub("forma");
    if (radio >= 0) forma["radio"] = radio;
    else forma.erase("radio");
    json& m = sub("marca");
    m["nombre"] = marca.nombre;
    m["logo"] = marca.logo;
    m["logoClaro"] = marca.logoClaro;
    m["favicon"] = marca.favicon;
    json& al = sub("alcance");
    al["publico"] = alcance.publico;
    al["control"] = alcance.control;
    return j;
}

TemaEvento TemaEvento::leer(const fs::path& carpetaEvento) {
    {
        std::ifstream f(carpetaEvento / "tema.json");
        const json j = f ? json::parse(f, nullptr, false) : json();
        if (j.is_object()) return deJson(j);
    }
    // Sin tema.json pero con sitio: hay tema, el de fábrica con los colores del sitio (sus
    // colores eran la cara del evento). No se copian acá: los pone paraPublicar mientras el
    // tema no fije los suyos.
    TemaEvento t;
    std::error_code ec;
    if (fs::is_regular_file(carpetaEvento / "Web" / "sitio.json", ec)) t.existe = t.delSitio = true;
    return t;
}

bool TemaEvento::guardar(const fs::path& carpetaEvento) const {
    return escribirAtomico(carpetaEvento / "tema.json", aJson().dump(2, ' ', false, json::error_handler_t::replace) + "\n");
}

json TemaEvento::paraPublicar(const fs::path& carpetaEvento) const {
    if (!existe) return json::object();
    json j = aJson();
    std::ifstream f(carpetaEvento / "Web" / "sitio.json");
    const json s = f ? json::parse(f, nullptr, false) : json();
    if (s.is_object()) {
        if (colores.acento.empty()) j["colores"]["acento"] = texto(s, "color");
        if (colores.fondo.empty()) j["colores"]["fondo"] = texto(s, "fondo");
        if (marca.nombre.empty()) j["marca"]["nombre"] = texto(s, "titulo");
    }
    return j;
}

std::vector<std::string> TemaEvento::archivos() const {
    std::vector<std::string> out;
    for (const std::string* a : {&marca.logo, &marca.logoClaro, &marca.favicon})
        if (!a->empty()) out.push_back(*a);
    for (const Fuente& f : tipografia.archivos) out.push_back(f.archivo);
    return out;
}

// --- Los archivos de Tema/ ------------------------------------------------------------

RecursoCopiado copiarRecurso(const fs::path& carpetaEvento, const fs::path& origen, const std::string& tipo) {
    RecursoCopiado r;
    const auto fallo = [](const char* que) {
        RecursoCopiado x;
        x.error = que;
        return x;
    };
    std::error_code ec;
    if (!fs::is_regular_file(origen, ec)) return fallo(QT_TRANSLATE_NOOP("evento", "No existe el archivo."));
    std::string ext = minusculas(origen.extension().string());
    if (!ext.empty()) ext.erase(0, 1);
    const bool logo = tipo == "logo";
    if (!logo && tipo != "fuente") return fallo(QT_TRANSLATE_NOOP("evento", "Tipo de archivo desconocido."));
    const std::set<std::string> vale = logo ? std::set<std::string>{"svg", "png", "webp"}
                                            : std::set<std::string>{"woff2", "woff", "ttf", "otf"};
    if (!vale.count(ext))
        return fallo(logo ? QT_TRANSLATE_NOOP("evento", "El logo tiene que ser SVG, PNG o WebP.") : QT_TRANSLATE_NOOP("evento", "La fuente tiene que ser WOFF2, WOFF, TTF u OTF."));
    const std::uintmax_t peso = fs::file_size(origen, ec);
    if (ec || peso == 0) return fallo(QT_TRANSLATE_NOOP("evento", "El archivo está vacío."));
    if (peso > (logo ? kTopeLogo : kTopeFuente))
        return fallo(logo ? QT_TRANSLATE_NOOP("evento", "El logo pesa más de 2 MB.") : QT_TRANSLATE_NOOP("evento", "La fuente pesa más de 4 MB."));
    const std::string datos = leerTodo(origen);
    if (datos.size() != peso) return fallo(QT_TRANSLATE_NOOP("evento", "No se pudo leer el archivo."));
    // Que adentro sea lo que dice la extensión (la firma), no un .exe renombrado.
    const auto empieza = [&datos](const std::string& firma) { return datos.compare(0, firma.size(), firma) == 0; };
    bool firma = false;
    if (ext == "png") firma = empieza("\x89PNG\r\n\x1a\n");
    else if (ext == "webp") firma = datos.size() > 12 && empieza("RIFF") && datos.compare(8, 4, "WEBP") == 0;
    else if (ext == "woff2") firma = empieza("wOF2");
    else if (ext == "woff") firma = empieza("wOFF");
    else if (ext == "otf") firma = empieza("OTTO");
    else if (ext == "ttf") firma = empieza(std::string("\x00\x01\x00\x00", 4)) || empieza("true");
    else if (ext == "svg") {
        // Un SVG es código: con <script> o un <foreignObject> no entra. (El gateway igual
        // lo sirve con «CSP: sandbox», por si alguien lo abre suelto.)
        const std::string m = minusculas(datos);
        // Ni atributos de evento (onload=, onclick=…): también son código.
        const auto conEventos = [&m]() {
            for (std::size_t i = m.find("on"); i != std::string::npos; i = m.find("on", i + 2)) {
                if (i == 0 || !(m[i - 1] == ' ' || m[i - 1] == '\t' || m[i - 1] == '\n' || m[i - 1] == '\r' || m[i - 1] == '/' ||
                                m[i - 1] == '"' || m[i - 1] == '\''))
                    continue;
                std::size_t j = i + 2;
                while (j < m.size() && m[j] >= 'a' && m[j] <= 'z') ++j;
                if (j == i + 2) continue;
                while (j < m.size() && (m[j] == ' ' || m[j] == '\t' || m[j] == '\n' || m[j] == '\r')) ++j;
                if (j < m.size() && m[j] == '=') return true;
            }
            return false;
        };
        firma = m.find("<svg") != std::string::npos && m.find("<script") == std::string::npos &&
                m.find("<foreignobject") == std::string::npos && m.find("javascript:") == std::string::npos && !conEventos();
        if (!firma) return fallo(QT_TRANSLATE_NOOP("evento", "El SVG no es una imagen limpia (tiene scripts o no es un SVG)."));
    }
    if (!firma) return fallo(QT_TRANSLATE_NOOP("evento", "El archivo no es lo que dice su extensión."));
    const std::string nombre = std::string(logo ? "logo" : "fuente") + "-" + ba::tema::huella(datos) + "." + ext;
    const fs::path dir = carpetaTema(carpetaEvento);
    if (!escribirAtomico(dir / nombre, datos)) return fallo(QT_TRANSLATE_NOOP("evento", "No se pudo copiar a la carpeta Tema/ del evento."));
    r.archivo = nombre;
    return r;
}

int limpiarRecursos(const fs::path& carpetaEvento, const TemaEvento& t) {
    const std::vector<std::string> usados = t.archivos();
    std::error_code ec;
    int n = 0;
    for (const auto& e : fs::directory_iterator(carpetaTema(carpetaEvento), ec)) {
        const std::string nombre = e.path().filename().string();
        // Solo lo que pudo haber copiado copiarRecurso: lo que puso una persona a mano se queda.
        if (!e.is_regular_file(ec) || (nombre.rfind("logo-", 0) != 0 && nombre.rfind("fuente-", 0) != 0)) continue;
        if (std::find(usados.begin(), usados.end(), nombre) != usados.end()) continue;
        if (fs::remove(e.path(), ec)) ++n;
    }
    return n;
}

// --- Los sitios generados antes del tema ---------------------------------------------

std::vector<std::string> migrarSitio(const fs::path& carpetaWeb) {
    std::vector<std::string> cambiados;
    std::error_code ec;
    if (!fs::is_directory(carpetaWeb, ec)) return cambiados;
    // Las páginas: el tema común entre el estilo del sitio y el del evento.
    const std::string linkEstilo = "<link rel=\"stylesheet\" href=\"estilo.css\" />";
    const std::string linkTema = "<link rel=\"stylesheet\" href=\"/comun/tema.css\" />";
    for (const auto& e : fs::directory_iterator(carpetaWeb, ec)) {
        if (!e.is_regular_file(ec) || minusculas(e.path().extension().string()) != ".html") continue;
        std::string h = leerTodo(e.path());
        const std::string antes = h;
        if (h.find("/comun/tema.css") == std::string::npos) {
            if (const std::size_t p = h.find(linkEstilo); p != std::string::npos)
                h.insert(p, linkTema + "\n  ");
        }
        if (const std::size_t p = h.find("<html"); p != std::string::npos) {
            const std::size_t fin = h.find('>', p);
            if (fin != std::string::npos && h.substr(p, fin - p).find("data-ev") == std::string::npos)
                h.insert(p + 5, " data-ev=\"publico\"");
        }
        // El logo del evento al lado del nombre (lo pone tema.js; sin logo no se ve).
        if (h.find("data-tema-logo") == std::string::npos) {
            if (const std::size_t p = h.find("<a class=\"marca\""); p != std::string::npos)
                h.insert(p, "<img class=\"marca-logo\" data-tema-logo alt=\"\" hidden />\n      ");
        }
        if (h.find("/comun/tema.js") == std::string::npos) {
            if (const std::size_t p = h.find("</head>"); p != std::string::npos)
                h.insert(p, "  <script src=\"/comun/tema.js\" defer></script>\n");
        }
        if (h != antes && escribirAtomico(e.path(), h)) cambiados.push_back(e.path().filename().string());
    }
    // estilo.css: la plantilla vieja escribía el acento y el fondo de sitio.json. Si siguen
    // siendo esos, se sacan (los pone el tema); si alguien los cambió, se respetan y el bloque
    // pasa a un selector que le gana al tema (html[data-ev], que va después).
    const fs::path css = carpetaWeb / "estilo.css";
    std::string s = leerTodo(css);
    if (!s.empty()) {
        std::ifstream f(carpetaWeb / "sitio.json");
        const json sitio = f ? json::parse(f, nullptr, false) : json();
        const std::string antes = s;
        const auto sacar = [&s](const std::string& var, const std::string& valor) {
            if (valor.empty()) return;
            const std::string linea = "  " + var + ": " + valor + ";\n";
            if (const std::size_t p = s.find(linea); p != std::string::npos) s.erase(p, linea.size());
        };
        sacar("--accent", texto(sitio, "color"));
        sacar("--bg", texto(sitio, "fondo"));
        if (const std::size_t p = s.find(":root {"); p != std::string::npos && s.find("html[data-ev]") == std::string::npos)
            s.replace(p, 7, ":root, html[data-ev] {");
        if (s != antes && escribirAtomico(css, s)) cambiados.push_back("estilo.css");
    }
    return cambiados;
}

} // namespace ba::evento
