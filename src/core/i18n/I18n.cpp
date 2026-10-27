// ============================================================================
// src/core/i18n/I18n.cpp — ver I18n.hpp
// ============================================================================
#include "i18n/I18n.hpp"

#include "platform/IdiomaSistema.hpp"
#include "platform/Paths.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <map>
#include <memory>
#include <mutex>
#include <system_error>

namespace ba::i18n {
namespace fs = std::filesystem;
using json = nlohmann::json;

namespace {

/// Los mensajes de un idioma: contexto → (texto[\x04desambiguación] → traducción).
/// Una traducción con plurales es un arreglo con sus formas.
struct Catalogo {
    Perfil perfil;
    json mensajes = json::object();
    Cobertura cobertura;
};

struct Estado {
    std::mutex mu;
    fs::path perfiles, catalogos; ///< vacías = las de por defecto
    bool perfilesLeidos = false;
    std::vector<Perfil> todos;
    std::map<std::string, std::shared_ptr<const Catalogo>> cargados;
    Perfil actual;
    std::vector<std::shared_ptr<const Catalogo>> cadenaActual; ///< el idioma y sus respaldos
};

Estado& estado() {
    static Estado e;
    return e;
}

std::string minusculas(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string lengua(const std::string& codigo) { return minusculas(codigo.substr(0, codigo.find('-'))); }

json leerJson(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    if (!in) return json();
    return json::parse(in, nullptr, false, true);
}

fs::path laQueHay(const fs::path& delBundle, const char* deLaCompilacion) {
    std::error_code ec;
    if (fs::is_directory(delBundle, ec) || deLaCompilacion == nullptr) return delBundle;
    return fs::path(deLaCompilacion);
}

fs::path dirPerfiles(const Estado& e) {
    if (!e.perfiles.empty()) return e.perfiles;
#ifdef BA_I18N_DEV_PERFILES
    return laQueHay(ba::platform::bundleRoot() / "data" / "idiomas", BA_I18N_DEV_PERFILES);
#else
    return laQueHay(ba::platform::bundleRoot() / "data" / "idiomas", nullptr);
#endif
}

fs::path dirCatalogos(const Estado& e) {
    if (!e.catalogos.empty()) return e.catalogos;
#ifdef BA_I18N_DEV_CATALOGOS
    return laQueHay(ba::platform::bundleRoot() / "data" / "i18n", BA_I18N_DEV_CATALOGOS);
#else
    return laQueHay(ba::platform::bundleRoot() / "data" / "i18n", nullptr);
#endif
}

Perfil perfilDeJson(const json& j, const std::string& codigo) {
    Perfil p;
    p.codigo = codigo;
    p.nombre = j.value("nombre", codigo);
    p.locale = j.value("locale", codigo);
    p.derechaAIzquierda = j.value("direccion", std::string("ltr")) == "rtl";
    p.plural = j.value("plural", std::string("uno-otro"));
    if (const auto r = j.find("respaldo"); r != j.end() && r->is_array())
        for (const auto& c : *r)
            if (c.is_string()) p.respaldo.push_back(c.get<std::string>());
    p.fuente = j.value("fuente", false);
    p.pseudo = j.value("pseudo", false);
    p.tipografia = j.value("tipografia", std::string());
    p.datos = j;
    return p;
}

/// Los perfiles (una vez; con el candado tomado).
const std::vector<Perfil>& perfilesSinCandado(Estado& e) {
    if (e.perfilesLeidos) return e.todos;
    e.perfilesLeidos = true;
    e.todos.clear();
    std::error_code ec;
    for (const auto& f : fs::directory_iterator(dirPerfiles(e), ec)) {
        if (f.path().extension() != ".json") continue;
        const json j = leerJson(f.path());
        const std::string codigo = f.path().stem().string();
        // El código es el nombre del archivo; uno que diga otro está mal armado.
        if (!j.is_object() || j.value("codigo", codigo) != codigo) continue;
        e.todos.push_back(perfilDeJson(j, codigo));
    }
    std::sort(e.todos.begin(), e.todos.end(), [](const Perfil& a, const Perfil& b) {
        if (a.fuente != b.fuente) return a.fuente;
        if (a.pseudo != b.pseudo) return b.pseudo;
        return a.codigo < b.codigo;
    });
    return e.todos;
}

const Perfil* buscarSinCandado(Estado& e, const std::string& codigo) {
    for (const auto& p : perfilesSinCandado(e))
        if (minusculas(p.codigo) == minusculas(codigo)) return &p;
    return nullptr;
}

std::shared_ptr<const Catalogo> catalogoSinCandado(Estado& e, const std::string& codigo) {
    if (const auto c = e.cargados.find(codigo); c != e.cargados.end()) return c->second;
    auto cat = std::make_shared<Catalogo>();
    if (const Perfil* p = buscarSinCandado(e, codigo)) cat->perfil = *p;
    else cat->perfil.codigo = codigo;
    const json j = leerJson(dirCatalogos(e) / (codigo + ".json"));
    if (j.is_object()) {
        if (const auto m = j.find("mensajes"); m != j.end() && m->is_object()) cat->mensajes = *m;
        cat->cobertura.total = j.value("total", 0);
        cat->cobertura.traducidos = j.value("traducidos", 0);
    }
    e.cargados[codigo] = cat;
    return cat;
}

/// El idioma y sus respaldos, sin repetir y cortando en el original (lo que sigue
/// sería el texto tal cual de todos modos).
std::vector<std::shared_ptr<const Catalogo>> cadenaSinCandado(Estado& e, const std::string& codigo) {
    std::vector<std::shared_ptr<const Catalogo>> out;
    std::vector<std::string> pendientes{codigo};
    for (std::size_t i = 0; i < pendientes.size() && out.size() < 8; ++i) {
        const std::string& c = pendientes[i];
        if (std::any_of(out.begin(), out.end(), [&](const auto& x) { return x->perfil.codigo == c; })) continue;
        auto cat = catalogoSinCandado(e, c);
        out.push_back(cat);
        if (cat->perfil.fuente) break;
        for (const auto& r : cat->perfil.respaldo) pendientes.push_back(r);
    }
    return out;
}

void reemplazarN(std::string& s, int n) {
    if (n < 0) return;
    const std::string v = std::to_string(n);
    for (const char* marca : {"%Ln", "%n"}) {
        const std::string m(marca);
        for (std::size_t p = s.find(m); p != std::string::npos; p = s.find(m, p + v.size())) s.replace(p, m.size(), v);
    }
}

std::string traducirCon(const std::vector<std::shared_ptr<const Catalogo>>& cadena, const char* contexto,
                        const char* texto, const char* desambiguacion, int n) {
    if (texto == nullptr) return {};
    std::string out = texto;
    bool hecho = false;
    const std::string ctx = contexto != nullptr ? contexto : "";
    std::string clave = out;
    if (desambiguacion != nullptr && *desambiguacion != '\0') clave += std::string("\x04") + desambiguacion;
    for (const auto& cat : cadena) {
        if (cat->perfil.pseudo) {
            out = pseudo(out);
            hecho = true;
            break;
        }
        const auto c = cat->mensajes.find(ctx);
        if (c == cat->mensajes.end() || !c->is_object()) continue;
        const auto t = c->find(clave);
        if (t == c->end()) continue;
        if (t->is_string()) {
            out = t->get<std::string>();
            hecho = true;
        } else if (t->is_array() && !t->empty()) {
            const int forma = std::clamp(formaPlural(cat->perfil.plural, n < 0 ? 1 : n), 0,
                                         static_cast<int>(t->size()) - 1);
            if ((*t)[forma].is_string()) {
                out = (*t)[forma].get<std::string>();
                hecho = true;
            }
        }
        if (hecho) break;
    }
    reemplazarN(out, n);
    return out;
}

/// Un carácter UTF-8 desde `i` (la cantidad de bytes que ocupa).
std::size_t largoUtf8(unsigned char c) {
    if (c < 0x80) return 1;
    if ((c >> 5) == 0x6) return 2;
    if ((c >> 4) == 0xE) return 3;
    if ((c >> 3) == 0x1E) return 4;
    return 1;
}

const char* letraPseudo(char c) {
    switch (c) {
    case 'a': return "ä";
    case 'e': return "ë";
    case 'i': return "ï";
    case 'o': return "ö";
    case 'u': return "ü";
    case 'y': return "ÿ";
    case 'c': return "ç";
    case 'n': return "ñ";
    case 'A': return "Ä";
    case 'E': return "Ë";
    case 'I': return "Ï";
    case 'O': return "Ö";
    case 'U': return "Ü";
    case 'Y': return "Ÿ";
    case 'C': return "Ç";
    case 'N': return "Ñ";
    default: return nullptr;
    }
}

} // namespace

void carpetas(const fs::path& perfiles, const fs::path& catalogos) {
    Estado& e = estado();
    std::lock_guard<std::mutex> l(e.mu);
    e.perfiles = perfiles;
    e.catalogos = catalogos;
    e.perfilesLeidos = false;
    e.cargados.clear();
    e.cadenaActual.clear();
    e.actual = Perfil{};
}

fs::path carpetaPerfiles() {
    Estado& e = estado();
    std::lock_guard<std::mutex> l(e.mu);
    return dirPerfiles(e);
}

fs::path carpetaCatalogos() {
    Estado& e = estado();
    std::lock_guard<std::mutex> l(e.mu);
    return dirCatalogos(e);
}

std::vector<Perfil> idiomas() {
    Estado& e = estado();
    std::lock_guard<std::mutex> l(e.mu);
    return perfilesSinCandado(e);
}

Perfil perfil(const std::string& codigo) {
    Estado& e = estado();
    std::lock_guard<std::mutex> l(e.mu);
    const Perfil* p = buscarSinCandado(e, codigo);
    return p != nullptr ? *p : Perfil{};
}

Cobertura cobertura(const std::string& codigo) {
    Estado& e = estado();
    std::lock_guard<std::mutex> l(e.mu);
    const Perfil* p = buscarSinCandado(e, codigo);
    if (p == nullptr) return {};
    if (p->fuente || p->pseudo) return {1, 1};
    return catalogoSinCandado(e, p->codigo)->cobertura;
}

std::string elegirDelSistema(const std::vector<std::string>& preferidos) {
    const std::vector<Perfil> hay = idiomas();
    const auto existe = [&](const std::string& c) -> const Perfil* {
        for (const auto& p : hay)
            if (!p.pseudo && minusculas(p.codigo) == minusculas(c)) return &p;
        return nullptr;
    };
    for (const auto& pref : preferidos) {
        if (const Perfil* p = existe(pref)) return p->codigo;
        if (const Perfil* p = existe(lengua(pref))) return p->codigo;
        // «pt-PT» pedido y solo hay «pt-BR»: mejor eso que otra lengua.
        for (const auto& p : hay)
            if (!p.pseudo && lengua(p.codigo) == lengua(pref)) return p.codigo;
    }
    if (const Perfil* p = existe("en")) return p->codigo;
    for (const auto& p : hay)
        if (p.fuente) return p.codigo;
    return "es";
}

std::string resolver(const std::string& pedido) {
    if (!pedido.empty()) {
        const Perfil p = perfil(pedido);
        if (!p.codigo.empty()) return p.codigo;
        const Perfil l = perfil(lengua(pedido));
        if (!l.codigo.empty()) return l.codigo;
    }
    return elegirDelSistema(ba::platform::idiomasDelSistema());
}

std::string idiomaDeLaMaquina(const fs::path& datos) {
    if (const char* v = std::getenv("BA_IDIOMA"); v != nullptr && *v != '\0') return resolver(v);
    const json j = leerJson(ba::platform::launcherJsonFor(datos));
    return resolver(j.is_object() ? j.value("idioma", std::string()) : std::string());
}

void usar(const std::string& codigo) {
    Estado& e = estado();
    std::lock_guard<std::mutex> l(e.mu);
    e.cadenaActual = cadenaSinCandado(e, codigo);
    e.actual = e.cadenaActual.empty() ? Perfil{} : e.cadenaActual.front()->perfil;
}

const Perfil& actual() {
    Estado& e = estado();
    std::lock_guard<std::mutex> l(e.mu);
    if (e.actual.codigo.empty()) {
        for (const auto& p : perfilesSinCandado(e))
            if (p.fuente) e.actual = p;
        if (e.actual.codigo.empty()) e.actual.codigo = "es";
    }
    return e.actual;
}

std::string traducir(const char* contexto, const char* texto, const char* desambiguacion, int n) {
    std::vector<std::shared_ptr<const Catalogo>> cadena;
    {
        Estado& e = estado();
        std::lock_guard<std::mutex> l(e.mu);
        cadena = e.cadenaActual;
    }
    return traducirCon(cadena, contexto, texto, desambiguacion, n);
}

std::string En::traducir(const char* contexto, const char* texto, const char* desambiguacion, int n) const {
    std::vector<std::shared_ptr<const Catalogo>> cadena;
    {
        Estado& e = estado();
        std::lock_guard<std::mutex> l(e.mu);
        cadena = cadenaSinCandado(e, codigo_);
    }
    return traducirCon(cadena, contexto, texto, desambiguacion, n);
}

std::string llenar(const std::string& plantilla, std::initializer_list<std::string> valores) {
    std::string out;
    out.reserve(plantilla.size());
    for (std::size_t i = 0; i < plantilla.size(); ++i) {
        if (plantilla[i] == '%' && i + 1 < plantilla.size() && plantilla[i + 1] >= '1' && plantilla[i + 1] <= '9') {
            const std::size_t k = static_cast<std::size_t>(plantilla[i + 1] - '1');
            if (k < valores.size()) {
                out += *(valores.begin() + k);
                ++i;
                continue;
            }
        }
        out += plantilla[i];
    }
    return out;
}

int formaPlural(const std::string& regla, long long n) {
    const long long a = n < 0 ? -n : n;
    const long long d = a % 10, c = a % 100;
    if (regla == "sin-plural") return 0;
    if (regla == "frances") return a <= 1 ? 0 : 1;
    if (regla == "eslavo") return (d == 1 && c != 11) ? 0 : (d >= 2 && d <= 4 && (c < 12 || c > 14)) ? 1 : 2;
    if (regla == "polaco") return a == 1 ? 0 : (d >= 2 && d <= 4 && (c < 12 || c > 14)) ? 1 : 2;
    if (regla == "checo") return a == 1 ? 0 : (a >= 2 && a <= 4) ? 1 : 2;
    if (regla == "arabe")
        return a == 0 ? 0 : a == 1 ? 1 : a == 2 ? 2 : (c >= 3 && c <= 10) ? 3 : (c >= 11) ? 4 : 5;
    return a == 1 ? 0 : 1; // uno-otro
}

int formasDePlural(const std::string& regla) {
    if (regla == "sin-plural") return 1;
    if (regla == "eslavo" || regla == "polaco" || regla == "checo") return 3;
    if (regla == "arabe") return 6;
    return 2;
}

std::string pseudo(const std::string& texto) {
    if (texto.empty()) return texto;
    std::string out = "⟦";
    int letras = 0;
    for (std::size_t i = 0; i < texto.size();) {
        const char c = texto[i];
        // Lo que no se toca: %1, %n, %L1, <etiquetas> y &entidades;.
        if (c == '%' && i + 1 < texto.size()) {
            std::size_t f = i + 1;
            if (texto[f] == 'L' && f + 1 < texto.size()) ++f;
            if (texto[f] == 'n' || (texto[f] >= '1' && texto[f] <= '9')) {
                out.append(texto, i, f + 1 - i);
                i = f + 1;
                continue;
            }
        }
        if (c == '<' || c == '&') {
            const std::size_t f = texto.find(c == '<' ? '>' : ';', i);
            if (f != std::string::npos && (c == '<' || f - i < 10)) {
                out.append(texto, i, f + 1 - i);
                i = f + 1;
                continue;
            }
        }
        const std::size_t largo = largoUtf8(static_cast<unsigned char>(c));
        if (largo == 1 && std::isalpha(static_cast<unsigned char>(c))) ++letras;
        else if (largo > 1) ++letras;
        if (const char* p = largo == 1 ? letraPseudo(c) : nullptr) out += p;
        else out.append(texto, i, largo);
        i += largo;
    }
    for (int k = 0, extra = std::max(1, (letras * 2 + 4) / 5); k < extra; ++k) out += "·";
    out += "⟧";
    return out;
}

} // namespace ba::i18n
