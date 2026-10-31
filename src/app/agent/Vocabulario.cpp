// SPDX-License-Identifier: GPL-2.0-or-later
#include "agent/Vocabulario.hpp"
#include "arranque/ArchivoConfig.hpp"

#include "asamblea/Cuerpo.hpp"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <sstream>

namespace ba::app::agent {

using json = nlohmann::json;
namespace fs = std::filesystem;

namespace {
Vocabulario* g_vocab = nullptr;

/// Minúsculas sin tildes (UTF-8 de las vocales acentuadas, ü y ñ -> ny).
std::string plano(const std::string& s) {
    std::string out;
    for (size_t i = 0; i < s.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        if (c == 0xC3 && i + 1 < s.size()) {
            const unsigned char d = static_cast<unsigned char>(s[++i]);
            switch (d) {
                case 0xA1: case 0x81: out += 'a'; break;
                case 0xA9: case 0x89: out += 'e'; break;
                case 0xAD: case 0x8D: out += 'i'; break;
                case 0xB3: case 0x93: out += 'o'; break;
                case 0xBA: case 0x9A: case 0xBC: case 0x9C: out += 'u'; break;
                case 0xB1: case 0x91: out += "ny"; break;
                default: break;
            }
            continue;
        }
        if (c < 0x80) out += static_cast<char>(std::tolower(c));
    }
    return out;
}

int distancia(const std::string& a, const std::string& b) {
    std::vector<int> prev(b.size() + 1), cur(b.size() + 1);
    for (size_t j = 0; j <= b.size(); ++j) prev[j] = static_cast<int>(j);
    for (size_t i = 1; i <= a.size(); ++i) {
        cur[0] = static_cast<int>(i);
        for (size_t j = 1; j <= b.size(); ++j)
            cur[j] = std::min({prev[j] + 1, cur[j - 1] + 1, prev[j - 1] + (a[i - 1] == b[j - 1] ? 0 : 1)});
        std::swap(prev, cur);
    }
    return prev[b.size()];
}

std::vector<std::string> palabras(const std::string& t) {
    std::vector<std::string> v;
    std::istringstream is(t);
    std::string w;
    while (is >> w) v.push_back(w);
    return v;
}

/// Separa la puntuación de los bordes de una palabra.
void bordes(const std::string& w, std::string& pre, std::string& nucleo, std::string& post) {
    size_t a = 0, b = w.size();
    while (a < b && std::ispunct(static_cast<unsigned char>(w[a])) && static_cast<unsigned char>(w[a]) < 0x80) ++a;
    while (b > a && std::ispunct(static_cast<unsigned char>(w[b - 1])) && static_cast<unsigned char>(w[b - 1]) < 0x80) --b;
    pre = w.substr(0, a);
    nucleo = w.substr(a, b - a);
    post = w.substr(b);
}
} // namespace

Vocabulario* Vocabulario::global() { return g_vocab; }

std::string Vocabulario::fonetica(const std::string& s) {
    const std::string p = plano(s);
    std::string o;
    for (size_t i = 0; i < p.size(); ++i) {
        const char c = p[i], n = i + 1 < p.size() ? p[i + 1] : '\0';
        if (!std::isalnum(static_cast<unsigned char>(c))) {
            if (c == ' ' && !o.empty() && o.back() != ' ') o += ' ';
            continue;
        }
        switch (c) {
            case 'h': break; // muda
            case 'v': case 'w': o += 'b'; break;
            case 'z': o += 's'; break;
            case 'c':
                if (n == 'h') { o += 'C'; ++i; }
                else if (n == 'e' || n == 'i') o += 's';
                else o += 'k';
                break;
            case 'q': o += 'k'; if (n == 'u') ++i; break;
            case 'g': o += (n == 'e' || n == 'i') ? 'j' : 'g'; break;
            case 'y': o += (n != '\0' && n != ' ') ? 'y' : 'i'; break;
            case 'l': if (n == 'l') { o += 'y'; ++i; } else o += 'l'; break;
            case 'x': o += "ks"; break;
            default:
                // Letras dobles (rr, ss, nn) cuentan como una.
                if (o.empty() || o.back() != c) o += c;
        }
    }
    return o;
}

Vocabulario::Vocabulario(fs::path config) : archivo_(std::move(config)) {
    const json j = config::leerObjeto(archivo_);
    if (j.contains("grupos")) datos_ = j;
    else sembrar();
    reindexar();
    g_vocab = this;
}

Vocabulario::~Vocabulario() {
    if (g_vocab == this) g_vocab = nullptr;
}

void Vocabulario::sembrar() {
    const auto grupo = [](std::initializer_list<const char*> l) {
        json t = json::array();
        for (const char* s : l) t.push_back({{"termino", s}, {"variantes", json::array()}});
        return json{{"activo", true}, {"terminos", t}};
    };
    datos_ = {{"grupos",
               // Las localidades son de cada lugar: vacías de fábrica (cada configuración pone las suyas).
               {{"localidades", json{{"activo", true}, {"terminos", json::array()}}},
                {"asamblea",
                 grupo({"Asamblea", "quórum", "cuarto intermedio", "orden del día", "expediente", "despacho",
                        "moción", "sesión ordinaria", "sesión extraordinaria", "comisión",
                        "presidencia", "vicepresidencia", "bloque", "dictamen", "resolución",
                        "declaración", "votación nominal", "a mano alzada", "unanimidad"})},
                {"cuerpo", json{{"activo", true}, {"terminos", json::array()}}},
                {"operador", json{{"activo", true}, {"terminos", json::array()}}}}}};
    guardar();
}

void Vocabulario::guardar() const {
    config::guardar(archivo_, datos_);
}

void Vocabulario::reindexar() {
    indice_.clear();
    for (const auto& [g, gr] : datos_["grupos"].items()) {
        if (!gr.value("activo", true)) continue;
        for (const auto& t : gr.value("terminos", json::array())) {
            const std::string term = t.value("termino", std::string());
            if (term.size() < 4) continue; // muy cortos: demasiados falsos positivos
            const auto add = [&](const std::string& forma) {
                Entrada e{term, fonetica(forma), static_cast<int>(palabras(forma).size())};
                if (e.palabras >= 1 && e.palabras <= 3) indice_.push_back(e);
            };
            add(term);
            for (const auto& v : t.value("variantes", json::array()))
                if (v.is_string()) add(v.get<std::string>());
        }
    }
}

std::string Vocabulario::corregir(const std::string& texto, json* hechos) {
    std::lock_guard<std::mutex> l(mu_);
    if (indice_.empty()) return texto;
    const std::vector<std::string> w = palabras(texto);
    std::vector<std::string> out;
    for (size_t i = 0; i < w.size();) {
        bool hecho = false;
        // De lo más largo a lo más corto: «Comodoro Rivadavia» antes que «Comodoro».
        for (int n = 3; n >= 1 && !hecho; --n) {
            if (i + static_cast<size_t>(n) > w.size()) continue;
            std::string pre, nucleo, post, junto;
            for (int k = 0; k < n; ++k) {
                std::string p1, nu, p2;
                bordes(w[i + static_cast<size_t>(k)], p1, nu, p2);
                if (k == 0) pre = p1;
                if (k == n - 1) post = p2;
                junto += (k ? " " : "") + nu;
            }
            if (junto.size() < 4) continue;
            const std::string clave = fonetica(junto);
            const Entrada* mejor = nullptr;
            int mejorD = 1000;
            for (const Entrada& e : indice_) {
                if (e.palabras != n) continue;
                const int d = distancia(clave, e.clave);
                // Confianza alta: casi igual en sonido (1 error cada 6 letras, como mucho 2).
                const int tope = std::min(2, static_cast<int>(e.clave.size()) / 6);
                if (d <= tope && d < mejorD) {
                    mejor = &e;
                    mejorD = d;
                }
            }
            if (mejor == nullptr) continue;
            if (plano(junto) != plano(mejor->termino) || junto != mejor->termino) {
                if (junto != mejor->termino) {
                    const json r{{"original", junto},
                                 {"reemplazo", mejor->termino},
                                 {"ms", std::chrono::duration_cast<std::chrono::milliseconds>(
                                            std::chrono::system_clock::now().time_since_epoch())
                                            .count()}};
                    if (hechos != nullptr) hechos->push_back(r);
                    reemplazos_.push_back(r);
                    while (reemplazos_.size() > 50) reemplazos_.pop_front();
                }
            }
            out.push_back(pre + mejor->termino + post);
            i += static_cast<size_t>(n);
            hecho = true;
        }
        if (!hecho) out.push_back(w[i++]);
    }
    std::string res;
    for (size_t k = 0; k < out.size(); ++k) res += (k ? " " : "") + out[k];
    return res;
}

json Vocabulario::todo() const {
    std::lock_guard<std::mutex> l(mu_);
    return datos_;
}

std::string Vocabulario::agregar(const std::string& termino, const std::vector<std::string>& variantes,
                                 const std::string& grupo) {
    if (termino.size() < 2) return "término demasiado corto";
    std::lock_guard<std::mutex> l(mu_);
    const std::string g = grupo.empty() ? "operador" : grupo;
    json& gr = datos_["grupos"][g];
    if (!gr.is_object()) gr = {{"activo", true}, {"terminos", json::array()}};
    json& ts = gr["terminos"];
    json nuevo{{"termino", termino}, {"variantes", variantes}};
    bool cambiado = false;
    for (auto& t : ts)
        if (t.value("termino", std::string()) == termino) {
            t = nuevo;
            cambiado = true;
        }
    if (!cambiado) ts.push_back(nuevo);
    guardar();
    reindexar();
    return {};
}

bool Vocabulario::quitar(const std::string& termino) {
    std::lock_guard<std::mutex> l(mu_);
    bool hecho = false;
    for (auto& [g, gr] : datos_["grupos"].items()) {
        auto& ts = gr["terminos"];
        for (auto it = ts.begin(); it != ts.end();)
            if (it->value("termino", std::string()) == termino) {
                it = ts.erase(it);
                hecho = true;
            } else {
                ++it;
            }
    }
    if (hecho) {
        guardar();
        reindexar();
    }
    return hecho;
}

void Vocabulario::activarGrupo(const std::string& grupo, bool activo) {
    std::lock_guard<std::mutex> l(mu_);
    if (!datos_["grupos"].contains(grupo)) return;
    datos_["grupos"][grupo]["activo"] = activo;
    guardar();
    reindexar();
}

void Vocabulario::ponerGrupo(const std::string& grupo, const std::vector<std::string>& terminos) {
    std::lock_guard<std::mutex> l(mu_);
    json t = json::array();
    for (const auto& s : terminos) t.push_back({{"termino", s}, {"variantes", json::array()}});
    const bool activo = datos_["grupos"].contains(grupo) ? datos_["grupos"][grupo].value("activo", true) : true;
    datos_["grupos"][grupo] = {{"activo", activo}, {"terminos", t}};
    guardar();
    reindexar();
}

int Vocabulario::cargarCuerpo(const asamblea::Cuerpo& cuerpo) {
    std::vector<std::string> nombres;
    for (const auto& i : cuerpo.integrantes()) {
        const std::string n = i.value("nombre", std::string());
        if (n.empty()) continue;
        nombres.push_back(n);
        // El apellido solo también (se dice «el consejero Pérez»); uno corto
        // («Paz», «Gil») se confundiría con palabras comunes: no va solo.
        if (const auto sp = n.rfind(' '); sp != std::string::npos && n.size() - sp > 4) nombres.push_back(n.substr(sp + 1));
    }
    std::sort(nombres.begin(), nombres.end());
    nombres.erase(std::unique(nombres.begin(), nombres.end()), nombres.end());
    ponerGrupo("cuerpo", nombres);
    return static_cast<int>(nombres.size());
}

json Vocabulario::ultimosReemplazos() const {
    std::lock_guard<std::mutex> l(mu_);
    return json(reemplazos_);
}

} // namespace ba::app::agent
