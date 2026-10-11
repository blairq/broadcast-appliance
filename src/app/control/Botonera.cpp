// SPDX-License-Identifier: GPL-2.0-or-later
#include "control/Botonera.hpp"
#include <QJsonArray>
#include <QFile>

#include "control/BusEventos.hpp"
#include "diag/Registro.hpp"

#include <QCryptographicHash>
#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QRandomGenerator>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <locale>
#include <sstream>

namespace ba::app::control {

using json = nlohmann::json;
namespace fs = std::filesystem;

namespace {

constexpr int kMaxDispositivos = 32;
constexpr int kAccionesPorSegundo = 20;
constexpr std::int64_t kCodigoMs = 2 * 60 * 1000;
constexpr std::int64_t kConfirmarMs = 5000;
constexpr int kMantenerMs = 700;
constexpr std::size_t kAuditoria = 500;

std::int64_t ahoraMs() { return QDateTime::currentMSecsSinceEpoch(); }

std::string hexAleatorio(int bytes) {
    static const char* h = "0123456789abcdef";
    std::string s;
    for (int i = 0; i < bytes; ++i) {
        const quint32 b = QRandomGenerator::system()->generate() & 0xFF;
        s += h[b >> 4];
        s += h[b & 15];
    }
    return s;
}

std::string sha256(const std::string& s) {
    return QCryptographicHash::hash(QByteArray::fromStdString(s), QCryptographicHash::Sha256).toHex().toStdString();
}

QVariant aQv(const json& j) {
    return QJsonDocument::fromJson(QByteArray::fromStdString(json{{"v", j}}.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace))).object().value("v").toVariant();
}

json deQv(const QVariant& v) {
    const QByteArray b = QJsonDocument(QJsonObject{{"v", QJsonValue::fromVariant(v)}}).toJson(QJsonDocument::Compact);
    const json j = json::parse(b.toStdString(), nullptr, false);
    return j.is_object() ? j["v"] : json();
}

std::string comoTexto(const json& v);

/// El valor en `ruta` ("programa", "valor.x", "lista.0"); vacía = todo. Además:
/// "lista[campo=valor]" es el primer elemento de la lista con ese campo (p. ej.
/// "pedidos[banca=5].turno") y "lista.#" es cuántos elementos tiene.
json resolver(const json& v, const std::string& ruta) {
    if (ruta.empty()) return v;
    const json* cur = &v;
    std::size_t a = 0;
    while (a <= ruta.size()) {
        // Un filtro puede tener puntos adentro del valor: el segmento termina en el
        // punto que sigue al «]».
        std::size_t b = ruta.find('.', a);
        const std::size_t corchete = ruta.find('[', a);
        if (corchete != std::string::npos && (b == std::string::npos || corchete < b)) {
            const std::size_t cierra = ruta.find(']', corchete);
            if (cierra == std::string::npos) return nullptr;
            b = ruta.find('.', cierra);
        }
        std::string k = ruta.substr(a, b == std::string::npos ? std::string::npos : b - a);
        if (k == "#") {
            if (!cur->is_array() && !cur->is_object()) return nullptr;
            return static_cast<int>(cur->size());
        }
        if (const auto ab = k.find('['); ab != std::string::npos && k.back() == ']') {
            const std::string lista = k.substr(0, ab), filtro = k.substr(ab + 1, k.size() - ab - 2);
            const auto igual = filtro.find('=');
            if (igual == std::string::npos) return nullptr;
            const std::string campo = filtro.substr(0, igual), valor = filtro.substr(igual + 1);
            const json* l = lista.empty() ? cur : (cur->is_object() && cur->contains(lista) ? &(*cur)[lista] : nullptr);
            if (l == nullptr || !l->is_array()) return nullptr;
            const json* hallado = nullptr;
            for (const auto& x : *l)
                if (x.is_object() && x.contains(campo) && comoTexto(x[campo]) == valor) {
                    hallado = &x;
                    break;
                }
            if (hallado == nullptr) return nullptr;
            cur = hallado;
        } else if (cur->is_object() && cur->contains(k)) {
            cur = &(*cur)[k];
        } else if (cur->is_array() && !k.empty() && std::all_of(k.begin(), k.end(), ::isdigit) &&
                   std::stoul(k) < cur->size()) {
            cur = &(*cur)[std::stoul(k)];
        } else {
            return nullptr;
        }
        if (b == std::string::npos) break;
        a = b + 1;
    }
    return *cur;
}

double numero(const json& v, double siNo) {
    if (v.is_number()) return v.get<double>();
    if (v.is_boolean()) return v.get<bool>() ? 1 : 0;
    if (v.is_string()) {
        try {
            return std::stod(v.get<std::string>());
        } catch (...) {
        }
    }
    return siNo;
}

/// Un número con punto decimal, sin importar el idioma del sistema (snprintf
/// usa el locale: en es_AR saldría «-23,4» y una herramienta no lo entiende).
std::string numeroTexto(double v, int decimales) {
    std::ostringstream o;
    o.imbue(std::locale::classic());
    if (decimales >= 0) o << std::fixed << std::setprecision(decimales);
    o << v;
    return o.str();
}

std::string comoTexto(const json& v) {
    if (v.is_string()) return v.get<std::string>();
    if (v.is_null()) return "";
    if (v.is_number_float()) return numeroTexto(v.get<double>(), 1);
    return v.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
}

bool cumple(const std::string& op, const json& v, const json& ref) {
    if (op.empty()) return true;
    if (op == "existe") return !v.is_null();
    if (op == "==") return v == ref || (v.is_number() && ref.is_number() && numero(v, 0) == numero(ref, 0));
    if (op == "!=") return !(v == ref);
    if (op == ">") return numero(v, NAN) > numero(ref, NAN);
    if (op == "<") return numero(v, NAN) < numero(ref, NAN);
    if (op == ">=") return numero(v, NAN) >= numero(ref, NAN);
    if (op == "<=") return numero(v, NAN) <= numero(ref, NAN);
    if (op == "en") { // v está en la lista ref
        if (ref.is_array())
            for (const auto& x : ref)
                if (x == v) return true;
        return false;
    }
    if (op == "contiene") { // la lista (o el texto) v contiene ref
        if (v.is_array())
            for (const auto& x : v)
                if (x == ref) return true;
        if (v.is_string() && ref.is_string()) return v.get<std::string>().find(ref.get<std::string>()) != std::string::npos;
        return false;
    }
    return false;
}

/// Reemplaza {{valor}}, {{delta}} y {{no_activo}} en los argumentos.
json plantilla(const json& args, double valor, double delta, bool activo) {
    if (args.is_object()) {
        json o = json::object();
        for (const auto& [k, v] : args.items()) o[k] = plantilla(v, valor, delta, activo);
        return o;
    }
    if (args.is_array()) {
        json a = json::array();
        for (const auto& v : args) a.push_back(plantilla(v, valor, delta, activo));
        return a;
    }
    if (!args.is_string()) return args;
    const std::string s = args.get<std::string>();
    if (s == "{{valor}}") return valor;
    if (s == "{{delta}}") return delta;
    if (s == "{{no_activo}}") return !activo;
    if (s == "{{activo}}") return activo;
    std::string r = s;
    const auto rep = [&](const std::string& k, const std::string& v) {
        for (auto p = r.find(k); p != std::string::npos; p = r.find(k, p + v.size())) r.replace(p, k.size(), v);
    };
    rep("{{valor}}", numeroTexto(valor, -1));
    rep("{{delta}}", numeroTexto(delta, -1));
    return r;
}

std::string claveCelda(int x, int y) { return std::to_string(x) + "," + std::to_string(y); }

const std::set<std::string> kTipos{"boton", "fader", "perilla", "medidor", "texto", "reloj"};
const std::set<std::string> kEfectos{"fondo",  "color",  "texto",         "titilar",  "valor",  "icono",
                                     "activo", "nivel",  "imagen",        "borde",    "borde_titilar",
                                     "insignia", "atenuar"};
const std::set<std::string> kOps{"", "==", "!=", ">", "<", ">=", "<=", "en", "contiene", "existe"};

} // namespace

Botonera::Botonera(const fs::path& datos, BusEventos& bus, Ganchos g, QObject* parent)
    : QObject(parent),
      archivo_(datos / "Config" / "botonera.json"),
      archivoDisp_(datos / "Config" / "botonera-dispositivos.json"),
      log_(datos / "Logs" / "botonera.log"),
      bus_(bus),
      g_(std::move(g)) {
    cargar();
    // El estado vivo: cualquier tema puede cambiar lo que muestra una celda.
    // Se recalcula a lo sumo cada 50 ms y solo se manda lo que cambió.
    sub_ = bus_.suscribir("#", [this](const std::string&, const json&) { sucio_ = true; });
    connect(&refresco_, &QTimer::timeout, this, &Botonera::refrescar);
    refresco_.start(50);
    // Faders y perillas: el último valor gana, a lo sumo 30 por segundo por control.
    connect(&coalescer_, &QTimer::timeout, this, [this]() {
        enCoalescer_ = true;
        auto fp = std::move(faderPend_);
        faderPend_.clear();
        for (auto& [k, av] : fp) {
            const std::string dev = k.substr(0, k.find('|'));
            const json* d = buscarDispositivo(dev);
            const json* p = d != nullptr ? buscarPerfil(d->value("perfil", std::string())) : nullptr;
            if (p != nullptr) correr(dev, *p, av.first, k.substr(k.find('|') + 1), av.second, 0, false);
        }
        auto pp = std::move(perillaPend_);
        perillaPend_.clear();
        for (auto& [k, ad] : pp) {
            const std::string dev = k.substr(0, k.find('|'));
            const json* d = buscarDispositivo(dev);
            const json* p = d != nullptr ? buscarPerfil(d->value("perfil", std::string())) : nullptr;
            if (p != nullptr) correr(dev, *p, ad.first, k.substr(k.find('|') + 1), 0, ad.second, false);
        }
        enCoalescer_ = false;
    });
    coalescer_.start(33);
    // Los aparatos que hace mucho que no se usan, una vez por hora.
    auto* vencer = new QTimer(this);
    connect(vencer, &QTimer::timeout, this, &Botonera::vencerDispositivos);
    vencer->start(3600 * 1000);
    vencerDispositivos();
}

Botonera::~Botonera() { bus_.desuscribir(sub_); }

// ============================================================================
// Persistencia
// ============================================================================

void Botonera::cargar() {
    {
        std::ifstream f(archivo_);
        const json j = json::parse(f, nullptr, false);
        datos_ = j.is_object() ? j : json::object();
        if (!datos_.contains("perfiles") || !datos_["perfiles"].is_array()) datos_["perfiles"] = json::array();
    }
    {
        std::ifstream f(archivoDisp_);
        const json j = json::parse(f, nullptr, false);
        disp_ = j.is_object() && j.contains("dispositivos") && j["dispositivos"].is_array() ? j["dispositivos"]
                                                                                           : json::array();
        // Los locales (plugins, satélites) se vuelven a registrar al arrancar.
        disp_.erase(std::remove_if(disp_.begin(), disp_.end(),
                                   [](const json& d) { return d.value("origen", std::string("tablet")) != "tablet"; }),
                    disp_.end());
    }
    // De fábrica: un perfil Director a partir de las escenas que haya.
    if (datos_["perfiles"].empty()) {
        std::string id;
        crearPerfil(json::object(), "director", &id);
    }
}

void Botonera::guardar() const {
    std::error_code ec;
    fs::create_directories(archivo_.parent_path(), ec);
    const fs::path tmp = archivo_.string() + ".tmp";
    {
        std::ofstream o(tmp);
        o << datos_.dump(1, ' ', false, nlohmann::json::error_handler_t::replace);
    }
    fs::rename(tmp, archivo_, ec);
}

void Botonera::guardarDispositivos() const {
    std::error_code ec;
    fs::create_directories(archivoDisp_.parent_path(), ec);
    json solo = json::array();
    for (const auto& d : disp_)
        if (d.value("origen", std::string("tablet")) == "tablet") solo.push_back(d);
    const fs::path tmp = archivoDisp_.string() + ".tmp";
    {
        std::ofstream o(tmp);
        o << json{{"dispositivos", solo}}.dump(1, ' ', false, nlohmann::json::error_handler_t::replace);
    }
    fs::permissions(tmp, fs::perms::owner_read | fs::perms::owner_write, fs::perm_options::replace, ec);
    fs::rename(tmp, archivoDisp_, ec);
}

void Botonera::auditar(const std::string& dispositivo, const std::string& que, const std::string& detalle) {
    const std::int64_t ms = ahoraMs();
    const json e{{"ms", ms}, {"dispositivo", dispositivo}, {"que", que}, {"detalle", detalle}};
    auditoria_.push_back(e);
    // También al registro general: de ahí lo toma la bitácora del evento en curso.
    ba::diag::registro().linea(ba::diag::Nivel::Info, "botonera", 0,
                               dispositivo + " " + que + (detalle.empty() ? "" : " " + detalle));
    while (auditoria_.size() > kAuditoria) auditoria_.pop_front();
    std::error_code ec;
    fs::create_directories(log_.parent_path(), ec);
    std::ofstream o(log_, std::ios::app);
    o << QDateTime::fromMSecsSinceEpoch(ms).toString(Qt::ISODateWithMs).toStdString() << "  " << dispositivo << "  "
      << que << "  " << detalle << "\n";
}

json Botonera::auditoria(std::int64_t desdeMs, const std::string& dispositivo) const {
    json out = json::array();
    for (const auto& e : auditoria_)
        if (e.value("ms", 0LL) >= desdeMs && (dispositivo.empty() || e.value("dispositivo", std::string()) == dispositivo))
            out.push_back(e);
    return out;
}

// ============================================================================
// Búsquedas
// ============================================================================

json* Botonera::buscarPerfil(const std::string& id) {
    for (auto& p : datos_["perfiles"])
        if (p.value("id", std::string()) == id) return &p;
    return nullptr;
}
const json* Botonera::buscarPerfil(const std::string& id) const {
    for (const auto& p : datos_["perfiles"])
        if (p.value("id", std::string()) == id) return &p;
    return nullptr;
}
json* Botonera::buscarPagina(const std::string& perfil, const std::string& pagina) {
    json* p = buscarPerfil(perfil);
    if (p == nullptr) return nullptr;
    for (auto& g : (*p)["paginas"])
        if (g.value("id", std::string()) == pagina) return &g;
    return nullptr;
}
const json* Botonera::buscarPagina(const std::string& perfil, const std::string& pagina) const {
    const json* p = buscarPerfil(perfil);
    if (p == nullptr || !p->contains("paginas")) return nullptr;
    for (const auto& g : (*p)["paginas"])
        if (g.value("id", std::string()) == pagina) return &g;
    return nullptr;
}
json* Botonera::buscarDispositivo(const std::string& id) {
    for (auto& d : disp_)
        if (d.value("id", std::string()) == id) return &d;
    return nullptr;
}
const json* Botonera::buscarDispositivo(const std::string& id) const {
    for (const auto& d : disp_)
        if (d.value("id", std::string()) == id) return &d;
    return nullptr;
}
const json* Botonera::celdaEn(const json& pagina, int x, int y) const {
    // Por referencia: `value()` devolvería una copia y el puntero quedaría colgando.
    if (!pagina.contains("celdas") || !pagina["celdas"].is_array()) return nullptr;
    for (const auto& c : pagina["celdas"]) {
        const int cx = c.value("x", 0), cy = c.value("y", 0);
        if (x >= cx && x < cx + std::max(1, c.value("ancho", 1)) && y >= cy && y < cy + std::max(1, c.value("alto", 1)))
            return &c;
    }
    return nullptr;
}

std::string Botonera::origenDe(const std::string& dispositivo) const {
    const json* d = buscarDispositivo(dispositivo);
    return d != nullptr ? d->value("origen", std::string("tablet")) : std::string();
}

std::string Botonera::idNuevo(const char* prefijo) const { return std::string(prefijo) + "-" + hexAleatorio(4); }

// ============================================================================
// Modelo
// ============================================================================

json Botonera::perfiles() const {
    json out = json::array();
    for (const auto& p : datos_["perfiles"]) {
        json pg = json::array();
        for (const auto& g : p.value("paginas", json::array()))
            pg.push_back({{"id", g.value("id", std::string())},
                          {"nombre", g.value("nombre", std::string())},
                          {"columnas", g.value("columnas", 4)},
                          {"filas", g.value("filas", 3)},
                          {"celdas", g.value("celdas", json::array()).size()}});
        out.push_back({{"id", p.value("id", std::string())},
                       {"nombre", p.value("nombre", std::string())},
                       {"herramientas_permitidas", p.value("herramientas_permitidas", json::array())},
                       {"puede_editar", p.value("puede_editar", false)},
                       {"paginas", pg}});
    }
    return out;
}

json Botonera::perfil(const std::string& id) const {
    const json* p = buscarPerfil(id);
    return p != nullptr ? *p : json();
}

std::string Botonera::crearPerfil(const json& p, const std::string& plantilla, std::string* idOut) {
    json nuevo = plantilla.empty() ? json::object() : armarPlantilla(plantilla);
    if (nuevo.is_null()) return "no hay una plantilla «" + plantilla + "» (botonera_plantillas)";
    if (p.contains("nombre")) nuevo["nombre"] = p["nombre"];
    if (!nuevo.contains("nombre")) nuevo["nombre"] = "Perfil";
    if (p.contains("herramientas_permitidas")) nuevo["herramientas_permitidas"] = p["herramientas_permitidas"];
    if (!nuevo.contains("herramientas_permitidas")) nuevo["herramientas_permitidas"] = json::array();
    nuevo["puede_editar"] = p.value("puede_editar", false);
    if (!nuevo.contains("paginas") || nuevo["paginas"].empty())
        nuevo["paginas"] = json::array({{{"id", "p1"}, {"nombre", "Página 1"}, {"columnas", 4}, {"filas", 3},
                                         {"celdas", json::array()}}});
    nuevo["id"] = idNuevo("perfil");
    datos_["perfiles"].push_back(nuevo);
    guardar();
    if (idOut != nullptr) *idOut = nuevo["id"];
    sucio_ = true;
    emit cambio();
    return {};
}

std::string Botonera::editarPerfil(const std::string& id, const json& c, bool humano) {
    json* p = buscarPerfil(id);
    if (p == nullptr) return "no hay un perfil «" + id + "»";
    if (c.contains("nombre") && c["nombre"].is_string()) (*p)["nombre"] = c["nombre"];
    if (c.contains("herramientas_permitidas")) {
        if (!c["herramientas_permitidas"].is_array()) return "herramientas_permitidas es una lista";
        std::set<std::string> antes;
        for (const auto& h : p->value("herramientas_permitidas", json::array()))
            if (h.is_string()) antes.insert(h.get<std::string>());
        for (const auto& h : c["herramientas_permitidas"]) {
            if (!h.is_string()) return "herramientas_permitidas: solo nombres";
            const std::string n = h.get<std::string>();
            // Ampliar permisos es decisión de una persona, no del asistente.
            if (!humano && !antes.count(n) && !antes.count("*"))
                return "solo una persona puede sumar herramientas a un perfil (hacelo en la página Botonera): «" + n + "»";
        }
        (*p)["herramientas_permitidas"] = c["herramientas_permitidas"];
    }
    if (c.contains("puede_editar")) {
        const bool v = c.value("puede_editar", false);
        if (v && !humano && !p->value("puede_editar", false)) return "solo una persona puede habilitar la edición desde el aparato";
        (*p)["puede_editar"] = v;
    }
    guardar();
    auditar(humano ? "escritorio" : "asistente", "editar_perfil", id + " " + c.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace));
    sucio_ = true;
    emit cambio();
    return {};
}

bool Botonera::borrarPerfil(const std::string& id) {
    auto& ps = datos_["perfiles"];
    const auto n = ps.size();
    ps.erase(std::remove_if(ps.begin(), ps.end(), [&](const json& p) { return p.value("id", std::string()) == id; }),
             ps.end());
    if (ps.size() == n) return false;
    guardar();
    auditar("escritorio", "borrar_perfil", id);
    sucio_ = true;
    emit cambio();
    return true;
}

std::string Botonera::crearPagina(const std::string& perfil, const std::string& nombre, int columnas, int filas,
                                  std::string* idOut) {
    json* p = buscarPerfil(perfil);
    if (p == nullptr) return "no hay un perfil «" + perfil + "»";
    if (columnas < 1 || columnas > 16 || filas < 1 || filas > 16) return "columnas y filas van de 1 a 16";
    const std::string id = idNuevo("pag");
    (*p)["paginas"].push_back(
        {{"id", id}, {"nombre", nombre.empty() ? "Página" : nombre}, {"columnas", columnas}, {"filas", filas},
         {"celdas", json::array()}});
    guardar();
    if (idOut != nullptr) *idOut = id;
    sucio_ = true;
    emit cambio();
    return {};
}

std::string Botonera::editarPagina(const std::string& perfil, const std::string& pagina, const json& c) {
    json* g = buscarPagina(perfil, pagina);
    if (g == nullptr) return "no hay esa página";
    if (c.contains("nombre") && c["nombre"].is_string()) (*g)["nombre"] = c["nombre"];
    for (const char* k : {"columnas", "filas"})
        if (c.contains(k)) {
            const int v = c.value(k, 0);
            if (v < 1 || v > 16) return std::string(k) + " va de 1 a 16";
            (*g)[k] = v;
        }
    guardar();
    sucio_ = true;
    emit cambio();
    return {};
}

bool Botonera::borrarPagina(const std::string& perfil, const std::string& pagina) {
    json* p = buscarPerfil(perfil);
    if (p == nullptr) return false;
    auto& gs = (*p)["paginas"];
    if (gs.size() <= 1) return false; // un perfil sin páginas no muestra nada
    const auto n = gs.size();
    gs.erase(std::remove_if(gs.begin(), gs.end(), [&](const json& g) { return g.value("id", std::string()) == pagina; }),
             gs.end());
    if (gs.size() == n) return false;
    guardar();
    sucio_ = true;
    emit cambio();
    return true;
}

std::string Botonera::validarAccion(const json& a, int prof) const {
    if (a.is_null()) return {};
    if (!a.is_object()) return "una acción es un objeto {tipo, …}";
    if (prof > 3) return "secuencias demasiado anidadas";
    const std::string t = a.value("tipo", std::string("herramienta"));
    if (t == "herramienta") {
        const std::string h = a.value("herramienta", std::string());
        if (h.empty()) return "falta `herramienta`";
        if (g_.herramientas) {
            const auto hs = g_.herramientas();
            if (!hs.empty() && !hs.count(h)) return "no existe la herramienta «" + h + "»";
        }
        if (a.contains("args") && !a["args"].is_object()) return "`args` es un objeto";
        return {};
    }
    if (t == "pagina") return a.value("ir_a", std::string()).empty() ? "falta `ir_a` (id de página)" : std::string();
    if (t == "receta") return a.value("id", std::string()).empty() ? "falta el `id` de la receta" : std::string();
    if (t == "secuencia") {
        if (!a.contains("pasos") || !a["pasos"].is_array() || a["pasos"].empty()) return "una secuencia lleva `pasos`";
        for (const auto& p : a["pasos"])
            if (const std::string e = validarAccion(p, prof + 1); !e.empty()) return "paso: " + e;
        return {};
    }
    return "tipo de acción desconocido «" + t + "» (herramienta, pagina, receta, secuencia)";
}

std::string Botonera::validarCelda(const json& c) const {
    if (!c.is_object()) return "la celda es un objeto";
    if (!c.contains("x") || !c.contains("y") || !c["x"].is_number_integer() || !c["y"].is_number_integer())
        return "faltan x e y (enteros)";
    const std::string tipo = c.value("tipo", std::string("boton"));
    if (!kTipos.count(tipo)) return "tipo «" + tipo + "»: boton, fader, perilla, medidor, texto o reloj";
    for (const char* k : {"accion", "accion_soltar", "accion_mantener"})
        if (c.contains(k))
            if (const std::string e = validarAccion(c[k], 0); !e.empty()) return std::string(k) + ": " + e;
    if (c.contains("estado")) {
        if (!c["estado"].is_array()) return "`estado` es una lista de ligaduras";
        for (const auto& l : c["estado"]) {
            if (!l.is_object() || l.value("tema", std::string()).empty()) return "cada ligadura lleva `tema`";
            if (!kOps.count(l.value("op", std::string()))) return "op «" + l.value("op", std::string()) + "» desconocido";
            if (!kEfectos.count(l.value("efecto", std::string())))
                return "efecto: fondo, color, texto, titilar, valor, icono, activo, nivel, imagen, borde, borde_titilar, "
                       "insignia o atenuar";
        }
    }
    return {};
}

std::string Botonera::setCelda(const std::string& perfil, const std::string& pagina, const json& celda) {
    json* g = buscarPagina(perfil, pagina);
    if (g == nullptr) return "no hay esa página (perfil «" + perfil + "», página «" + pagina + "»)";
    if (const std::string e = validarCelda(celda); !e.empty()) return e;
    json c = celda;
    if (!c.contains("tipo")) c["tipo"] = "boton";
    const int x = c["x"], y = c["y"], w = std::max(1, c.value("ancho", 1)), h = std::max(1, c.value("alto", 1));
    if (x < 0 || y < 0 || x + w > g->value("columnas", 4) || y + h > g->value("filas", 3))
        return "la celda se sale de la grilla (" + std::to_string(g->value("columnas", 4)) + "×" +
               std::to_string(g->value("filas", 3)) + ")";
    // Reemplaza lo que pise.
    auto& cs = (*g)["celdas"];
    cs.erase(std::remove_if(cs.begin(), cs.end(),
                            [&](const json& o) {
                                const int ox = o.value("x", 0), oy = o.value("y", 0);
                                const int ow = std::max(1, o.value("ancho", 1)), oh = std::max(1, o.value("alto", 1));
                                return ox < x + w && x < ox + ow && oy < y + h && y < oy + oh;
                            }),
             cs.end());
    cs.push_back(c);
    guardar();
    auditar("escritorio", "editar_celda", perfil + "/" + pagina + " " + claveCelda(x, y));
    sucio_ = true;
    emit cambio();
    return {};
}

bool Botonera::borrarCelda(const std::string& perfil, const std::string& pagina, int x, int y) {
    json* g = buscarPagina(perfil, pagina);
    if (g == nullptr) return false;
    auto& cs = (*g)["celdas"];
    const auto n = cs.size();
    cs.erase(std::remove_if(cs.begin(), cs.end(),
                            [&](const json& o) { return o.value("x", -1) == x && o.value("y", -1) == y; }),
             cs.end());
    if (cs.size() == n) return false;
    guardar();
    sucio_ = true;
    emit cambio();
    return true;
}

json Botonera::celdas(const std::string& perfil, const std::string& pagina) const {
    const json* g = buscarPagina(perfil, pagina);
    if (g == nullptr) return nullptr;
    json out = json::array();
    for (const auto& c : g->value("celdas", json::array())) {
        json r = c;
        r["render"] = renderCelda(c, perfil + "/" + pagina + "/" + claveCelda(c.value("x", 0), c.value("y", 0)));
        out.push_back(r);
    }
    return out;
}

std::string Botonera::importar(const json& p, std::string* idOut) {
    if (!p.is_object() || !p.contains("paginas") || !p["paginas"].is_array()) return "un perfil lleva `paginas`";
    for (const auto& g : p["paginas"])
        for (const auto& c : g.value("celdas", json::array()))
            if (const std::string e = validarCelda(c); !e.empty()) return "celda inválida: " + e;
    json nuevo = p;
    nuevo["id"] = idNuevo("perfil");
    // Lo importado no trae permisos: los da una persona después.
    nuevo["herramientas_permitidas"] = json::array();
    nuevo["puede_editar"] = false;
    datos_["perfiles"].push_back(nuevo);
    guardar();
    if (idOut != nullptr) *idOut = nuevo["id"];
    auditar("escritorio", "importar", nuevo.value("nombre", std::string()));
    emit cambio();
    return {};
}

json Botonera::exportar(const std::string& perfil) const {
    const json* p = buscarPerfil(perfil);
    return p != nullptr ? *p : json();
}

// ============================================================================
// Plantillas de fábrica
// ============================================================================

json Botonera::plantillas() const {
    return json::array({{{"id", "director"}, {"nombre", "Director"},
                         {"descripcion", "Escenas con tally (un toque arma, TAKE corta), FTB, grabación y transmisión"}},
                        {{"id", "audio"}, {"nombre", "Audio"},
                         {"descripcion", "Un fader por fuente con su medidor y mudo; loudness del programa"}},
                        {{"id", "asamblea"}, {"nombre", "Asamblea"},
                         {"descripcion", "Las bancas del cuerpo (un toque: al aire armado), siguiente, terminar, TAKE"}},
                        {{"id", "recinto"}, {"nombre", "Recinto"},
                         {"descripcion", "Las bancas donde están en la sala (un toque: al aire armado), siguiente, terminar, TAKE"}},
                        {{"id", "microfonos"}, {"nombre", "Micrófonos de bancas"},
                         {"descripcion", "La central de conferencia: la cara de cada banca donde está en la sala; un toque le "
                                         "da o le corta el audio, mantener la pone al aire; siguiente, cerrar todos"}},
                        {{"id", "prompter"}, {"nombre", "Prompter"},
                         {"descripcion", "Correr y parar, velocidad, avisos al orador"}}});
}

json Botonera::armarPlantilla(const std::string& id) const {
    const auto herr = [](const std::string& h, json args = json::object()) {
        return json{{"tipo", "herramienta"}, {"herramienta", h}, {"args", std::move(args)}};
    };
    const auto boton = [](int x, int y, const std::string& texto, json accion, json estado = json::array(),
                          const std::string& fondo = "#2b2f3a") {
        return json{{"x", x}, {"y", y}, {"tipo", "boton"},
                    {"estilo", {{"texto", texto}, {"fondo", fondo}, {"color", "#ffffff"}}},
                    {"accion", std::move(accion)}, {"estado", std::move(estado)}};
    };
    const auto lig = [](const std::string& tema, const std::string& ruta, const std::string& op, json valor,
                        const std::string& efecto, json dato = nullptr) {
        json l{{"tema", tema}, {"ruta", ruta}, {"op", op}, {"efecto", efecto}};
        if (!valor.is_null()) l["valor"] = std::move(valor);
        if (!dato.is_null()) l["dato"] = std::move(dato);
        return l;
    };
    json celdas = json::array();
    std::set<std::string> usadas;
    const auto usar = [&](const json& c) {
        for (const char* k : {"accion", "accion_soltar", "accion_mantener"})
            if (c.contains(k) && c[k].is_object() && c[k].contains("herramienta")) usadas.insert(c[k]["herramienta"]);
        celdas.push_back(c);
    };
    int columnas = 6, filas = 4;
    std::string nombre;
    if (id == "director") {
        nombre = "Director";
        const std::vector<std::string> esc = g_.escenas ? g_.escenas() : std::vector<std::string>{};
        int i = 0;
        for (const auto& e : esc) {
            if (i >= 12) break;
            usar(boton(i % 6, i / 6, e, herr("scene_preview", {{"name", e}}),
                       {lig("aire/tally", "preview", "==", e, "fondo", "#1f8a3c"),
                        lig("aire/tally", "programa", "==", e, "fondo", "#c62828")}));
            ++i;
        }
        usar(boton(0, 3, "TAKE", herr("scene_take"), json::array(), "#8e1b1b"));
        usar(boton(1, 3, "FTB", herr("aire_ftb", {{"activar", true}}), json::array(), "#000000"));
        usar(boton(2, 3, "Volver de FTB", herr("aire_ftb", {{"activar", false}})));
        usar(boton(4, 3, "REC", herr("record_start"),
                   {lig("salida/grabacion", "activo", "==", true, "fondo", "#c62828"),
                    lig("salida/grabacion", "activo", "==", true, "titilar", true),
                    lig("salida/grabacion", "tiempo", "", nullptr, "texto", "REC\n{{v}}")}));
        usar(boton(5, 3, "AIRE", herr("stream_start"),
                   {lig("salida/stream", "activo", "==", true, "fondo", "#c62828"),
                    lig("salida/stream", "tiempo", "", nullptr, "texto", "AIRE\n{{v}}")}));
    } else if (id == "audio") {
        nombre = "Audio";
        std::vector<std::string> fu = g_.fuentesAudio ? g_.fuentesAudio() : std::vector<std::string>{};
        if (fu.size() > 8) fu.resize(8);
        columnas = std::max<int>(2, static_cast<int>(fu.size()) + 1);
        int x = 0;
        for (const auto& f : fu) {
            const std::string tema = "audio/nivel/" + f;
            usar(json{{"x", x}, {"y", 0}, {"alto", 3}, {"tipo", "fader"},
                      {"estilo", {{"texto", f}, {"fondo", "#20242d"}, {"color", "#ffffff"}}},
                      {"valor", {{"min", -60}, {"max", 6}, {"paso", 0.5}}},
                      {"accion", herr("audio_set", {{"name", f}, {"volumeDb", "{{valor}}"}})},
                      {"estado", {lig(tema, "volumen_db", "", nullptr, "valor"), lig(tema, "pico_db", "", nullptr, "nivel")}}});
            usar(boton(x, 3, "MUTE", herr("audio_set", {{"name", f}, {"muted", "{{no_activo}}"}}),
                       {lig(tema, "mudo", "==", true, "activo", true), lig(tema, "mudo", "==", true, "fondo", "#c62828")}));
            ++x;
        }
        usar(json{{"x", x}, {"y", 0}, {"tipo", "texto"},
                  {"estilo", {{"texto", "LUFS"}, {"fondo", "#111"}, {"color", "#9ad"}}},
                  {"estado", {lig("audio/loudness", "i", "", nullptr, "texto", "I {{v}}"),
                              lig("audio/loudness", "fuera_de_objetivo", "==", true, "color", "#ff5252")}}});
        usar(json{{"x", x}, {"y", 1}, {"tipo", "texto"},
                  {"estilo", {{"texto", "TP"}, {"fondo", "#111"}, {"color", "#9ad"}}},
                  {"estado", {lig("audio/loudness", "tp", "", nullptr, "texto", "TP {{v}}")}}});
    } else if (id == "asamblea") {
        nombre = "Asamblea";
        const auto bs = g_.bancas ? g_.bancas() : std::vector<std::pair<std::string, std::string>>{};
        columnas = 6;
        filas = std::max(3, static_cast<int>((bs.size() + 5) / 6) + 1);
        filas = std::min(filas, 8);
        int i = 0;
        for (const auto& [banca, nom] : bs) {
            if (i >= (filas - 1) * 6) break;
            usar(boton(i % 6, i / 6, banca + "\n" + nom, herr("banca_al_aire", {{"banca", banca}}),
                       {lig("oradores/al_aire", "banca", "==", banca, "fondo", "#c62828")}));
            ++i;
        }
        const int f = filas - 1;
        usar(boton(0, f, "Siguiente", herr("banca_al_aire"), json::array(), "#1f5f8a"));
        usar(boton(1, f, "Terminar", herr("banca_terminar")));
        usar(json{{"x", 2}, {"y", f}, {"ancho", 2}, {"tipo", "texto"},
                  {"estilo", {{"texto", "—"}, {"fondo", "#111"}, {"color", "#fff"}}},
                  {"estado", {lig("oradores/al_aire", "nombre", "existe", nullptr, "texto", "{{v}}")}}});
        usar(boton(5, f, "TAKE", herr("scene_take"), json::array(), "#8e1b1b"));
    } else if (id == "recinto") {
        // Como Asamblea, pero cada banca en su lugar de la grilla del recinto
        // (hasta 16 columnas y 15 filas; la última fila es la de los comandos).
        nombre = "Recinto";
        const json r = g_.recinto ? g_.recinto() : json::object();
        columnas = std::clamp(r.value("columnas", 0), 4, 16);
        filas = std::clamp(r.value("filas", 0) + 1, 2, 16);
        if (r.value("columnas", 0) < 1) return nullptr;
        for (const auto& b : r.value("bancas", json::array())) {
            const int x = b.value("columna", 0), y = b.value("fila", 0);
            if (x >= columnas || y >= filas - 1) continue;
            const std::string banca = b.value("banca", std::string());
            const std::string nom = b.contains("integrante") ? b["integrante"].value("nombre", std::string()) : std::string();
            usar(boton(x, y, banca + (nom.empty() ? "" : "\n" + nom), herr("banca_al_aire", {{"banca", banca}}),
                       {lig("oradores/al_aire", "banca", "==", banca, "fondo", "#c62828")}));
        }
        const int f = filas - 1;
        usar(boton(0, f, "Siguiente", herr("banca_al_aire"), json::array(), "#1f5f8a"));
        usar(boton(1, f, "Terminar", herr("banca_terminar")));
        usar(json{{"x", 2}, {"y", f}, {"ancho", columnas - 3}, {"tipo", "texto"},
                  {"estilo", {{"texto", "—"}, {"fondo", "#111"}, {"color", "#fff"}}},
                  {"estado", {lig("oradores/al_aire", "nombre", "existe", nullptr, "texto", "{{v}}")}}});
        usar(boton(columnas - 1, f, "TAKE", herr("scene_take"), json::array(), "#8e1b1b"));
    } else if (id == "microfonos") {
        // Guía bosch-dcn §11. Cada banca en su lugar del recinto (o en orden si no
        // hay grilla), con su foto; el estado sale de plugin/bosch-dcn/dcn/estado.
        nombre = "Micrófonos de bancas";
        const std::string tema = "plugin/bosch-dcn/dcn/estado";
        const json r = g_.recinto ? g_.recinto() : json::object();
        const std::set<std::string> presis = g_.presidentes ? g_.presidentes() : std::set<std::string>{};
        std::vector<std::tuple<std::string, std::string, int, int>> lugares;  // banca, nombre, x, y
        if (r.value("columnas", 0) > 0 && !r.value("bancas", json::array()).empty()) {
            columnas = std::clamp(r.value("columnas", 4), 4, 16);
            filas = std::clamp(r.value("filas", 1) + 1, 2, 16);
            for (const auto& b : r["bancas"]) {
                const std::string nom = b.contains("integrante") ? b["integrante"].value("nombre", std::string()) : "";
                lugares.emplace_back(b.value("banca", std::string()), nom, b.value("columna", 0), b.value("fila", 0));
            }
        } else {
            const auto bs = g_.bancas ? g_.bancas() : std::vector<std::pair<std::string, std::string>>{};
            columnas = 6;
            filas = std::clamp(static_cast<int>((bs.size() + 5) / 6) + 1, 2, 8);
            int i = 0;
            for (const auto& [banca, nom] : bs) {
                lugares.emplace_back(banca, nom, i % 6, i / 6);
                ++i;
            }
        }
        for (const auto& [banca, nom, x, y] : lugares) {
            if (x >= columnas || y >= filas - 1) continue;
            const auto esp = nom.rfind(' ');
            const std::string apellido = esp == std::string::npos ? nom : nom.substr(esp + 1);
            json accion = herr("dcn_microfono", {{"banca", banca}, {"abierto", "{{no_activo}}"}, {"confirmar", true}});
            if (presis.count(banca)) accion["confirmar"] = true;  // el del presidente se confirma en el aparato
            json c{{"x", x}, {"y", y}, {"tipo", "boton"},
                   {"estilo", {{"texto", banca + (apellido.empty() ? "" : "\n" + apellido)}, {"fondo", "#20242d"},
                               {"color", "#ffffff"}, {"imagen", "cuerpo:" + banca}, {"borde", presis.count(banca) ? "#d4af37" : ""}}},
                   {"accion", accion},
                   {"accion_mantener", herr("banca_al_aire", {{"banca", banca}})},
                   {"estado",
                    {lig(tema, "abiertos", "contiene", banca, "activo", true),
                     lig(tema, "abiertos", "contiene", banca, "fondo", "#c62828"),
                     lig(tema, "siguiente", "==", banca, "borde", "#ff1744"),
                     lig(tema, "siguiente", "==", banca, "borde_titilar", true),
                     lig(tema, "pedidos[banca=" + banca + "].turno", "existe", nullptr, "insignia", "{{v}}"),
                     lig(tema, "unidades[banca=" + banca + "].conectada", "!=", true, "atenuar", true),
                     lig(tema, "enlace", "!=", "ok", "atenuar", true)}}};
            usar(c);
        }
        const int f = filas - 1;
        usar(boton(0, f, "Siguiente ▶", herr("dcn_siguiente"), json::array(), "#1f5f8a"));
        usar(boton(1, f, "Cerrar todos", herr("dcn_cerrar_todos"), json::array(), "#8e1b1b"));
        // Con 4 columnas no entran los textos: Siguiente, Cerrar todos, Limpiar y TAKE.
        if (columnas >= 5)
            usar(json{{"x", 2}, {"y", f}, {"tipo", "texto"},
                      {"estilo", {{"texto", "sin central"}, {"fondo", "#111"}, {"color", "#fff"}}},
                      {"estado", {lig(tema, "abiertos.#", "existe", nullptr, "texto", "Abiertos {{v}}"),
                                  lig(tema, "enlace", "!=", "ok", "texto", "sin central")}}});
        if (columnas >= 6)
            usar(json{{"x", 3}, {"y", f}, {"ancho", columnas - 5}, {"tipo", "texto"},
                      {"estilo", {{"texto", "—"}, {"fondo", "#111"}, {"color", "#fff"}}},
                      {"estado", {lig(tema, "hablando.0.nombre", "existe", nullptr, "texto", "{{v}}")}}});
        usar(boton(columnas - 2, f, "Limpiar pedidos", herr("dcn_pedidos_limpiar")));
        usar(boton(columnas - 1, f, "TAKE", herr("scene_take"), json::array(), "#8e1b1b"));
    } else if (id == "prompter") {
        nombre = "Prompter";
        columnas = 4;
        filas = 3;
        usar(boton(0, 0, "▶ Correr", herr("prompter_control", {{"corriendo", true}}), json::array(), "#1f8a3c"));
        usar(boton(1, 0, "■ Parar", herr("prompter_control", {{"corriendo", false}}), json::array(), "#8e1b1b"));
        usar(json{{"x", 2}, {"y", 0}, {"alto", 3}, {"tipo", "fader"},
                  {"estilo", {{"texto", "Velocidad"}, {"fondo", "#20242d"}, {"color", "#fff"}}},
                  {"valor", {{"min", 60}, {"max", 250}, {"paso", 5}, {"inicial", 130}}},
                  {"accion", herr("prompter_control", {{"ppm", "{{valor}}"}})}});
        usar(boton(0, 1, "Redondeá", herr("prompter_control", {{"aviso", "Redondeá"}})));
        usar(boton(1, 1, "Un minuto", herr("prompter_control", {{"aviso", "Un minuto"}})));
        usar(boton(0, 2, "Sacar aviso", herr("prompter_control", {{"aviso", ""}})));
        usar(boton(3, 0, "PÁNICO", herr("prompter_control", {{"panico", true}}), json::array(), "#b71c1c"));
    } else {
        return nullptr;
    }
    json permitidas = json::array();
    for (const auto& h : usadas) permitidas.push_back(h);
    return {{"nombre", nombre},
            {"herramientas_permitidas", permitidas},
            {"paginas", json::array({{{"id", "p1"}, {"nombre", nombre}, {"columnas", columnas}, {"filas", filas},
                                      {"celdas", celdas}}})}};
}

// ============================================================================
// Render
// ============================================================================

json Botonera::renderCelda(const json& c, const std::string& claveValor) const {
    const json est = c.value("estilo", json::object());
    const std::string tipo = c.value("tipo", std::string("boton"));
    json r{{"x", c.value("x", 0)},
           {"y", c.value("y", 0)},
           {"ancho", std::max(1, c.value("ancho", 1))},
           {"alto", std::max(1, c.value("alto", 1))},
           {"tipo", tipo},
           {"texto", est.value("texto", std::string())},
           {"icono", est.value("icono", std::string())},
           {"color", est.value("color", std::string("#ffffff"))},
           {"fondo", est.value("fondo", std::string("#2b2f3a"))},
           {"tamano_texto", est.value("tamano_texto", 0)},
           {"titilar", false},
           {"activo", false},
           {"borde", est.value("borde", std::string())},
           {"borde_titilar", false},
           {"insignia", est.value("insignia", std::string())},
           {"atenuar", false}};
    std::string imagen = est.value("imagen", std::string());
    if (tipo == "fader" || tipo == "perilla" || tipo == "medidor") {
        const json v = c.value("valor", json::object());
        const double mn = numero(v.value("min", json(0)), 0), mx = numero(v.value("max", json(100)), 100);
        r["min"] = mn;
        r["max"] = mx;
        r["paso"] = numero(v.value("paso", json(1)), 1);
        r["modo"] = v.value("modo", std::string("absoluto"));
        auto it = valores_.find(claveValor);
        r["valor"] = it != valores_.end() ? it->second : numero(v.value("inicial", json(mn)), mn);
    }
    for (const auto& l : c.value("estado", json::array())) {
        const json u = bus_.ultimo(l.value("tema", std::string()));
        const json val = u.is_object() ? resolver(u["valor"], l.value("ruta", std::string())) : json();
        if (!u.is_object()) continue;
        if (!cumple(l.value("op", std::string()), val, l.value("valor", json()))) continue;
        const std::string ef = l.value("efecto", std::string());
        const json dato = l.value("dato", json());
        if (ef == "texto" || ef == "insignia" || ef == "imagen") {
            std::string t = dato.is_string() ? dato.get<std::string>() : "{{v}}";
            const std::string v = comoTexto(val);
            for (auto p = t.find("{{v}}"); p != std::string::npos; p = t.find("{{v}}", p + v.size())) t.replace(p, 5, v);
            if (ef == "imagen")
                imagen = t;
            else
                r[ef] = t;
        } else if (ef == "borde_titilar" || ef == "atenuar") {
            r[ef] = dato.is_null() ? true : dato.is_boolean() && dato.get<bool>();
        } else if (ef == "valor" || ef == "nivel") {
            r[ef == "valor" ? "valor" : "nivel"] = numero(dato.is_null() ? val : dato, 0);
        } else if (ef == "titilar" || ef == "activo") {
            r[ef] = dato.is_null() ? true : dato.is_boolean() && dato.get<bool>();
        } else if (!dato.is_null()) {
            r[ef] = dato;
        }
    }
    // La imagen es una referencia («cuerpo:5», «ptz:Cámara 1/Atril»): la app la
    // convierte en una miniatura (data: URL) que viaja con la celda.
    r["imagen"] = !imagen.empty() && g_.imagen ? g_.imagen(imagen) : std::string();
    // Una cara propia (foto o video corto): la referencia liviana; los cuadros
    // los trae cada superficie a su medida (ver CarasBotonera).
    if (!imagen.empty() && g_.cara)
        if (json c = g_.cara(imagen); c.is_object()) r["cara"] = std::move(c);
    return r;
}

json Botonera::renderPagina(const json& perfil, const json& pagina, const std::string& dispositivo) const {
    json cs = json::array();
    const std::string base = perfil.value("id", std::string()) + "/" + pagina.value("id", std::string()) + "/";
    for (const auto& c : pagina.value("celdas", json::array()))
        cs.push_back(renderCelda(c, base + claveCelda(c.value("x", 0), c.value("y", 0))));
    json pgs = json::array();
    for (const auto& g : perfil.value("paginas", json::array()))
        pgs.push_back({{"id", g.value("id", std::string())}, {"nombre", g.value("nombre", std::string())}});
    json r{{"type", "estado"},
           {"perfil", {{"id", perfil.value("id", std::string())}, {"nombre", perfil.value("nombre", std::string())}}},
           {"pagina",
            {{"id", pagina.value("id", std::string())},
             {"nombre", pagina.value("nombre", std::string())},
             {"columnas", pagina.value("columnas", 4)},
             {"filas", pagina.value("filas", 3)}}},
           {"paginas", pgs},
           {"celdas", cs},
           {"puede_editar", perfil.value("puede_editar", false)}};
    auto pe = pendientes_.find(dispositivo);
    if (pe != pendientes_.end() && pe->second.venceMs > ahoraMs()) r["confirmar"] = pe->second.celda;
    return r;
}

json Botonera::render(const std::string& dispositivo) const {
    const json* d = buscarDispositivo(dispositivo);
    if (d == nullptr) return {{"type", "estado"}, {"error", "aparato desconocido"}};
    const json* p = buscarPerfil(d->value("perfil", std::string()));
    if (p == nullptr || p->value("paginas", json::array()).empty())
        return {{"type", "estado"}, {"error", "el aparato no tiene un perfil asignado (página Botonera)"}};
    auto it = paginaDe_.find(dispositivo);
    const std::string pag = it != paginaDe_.end() ? it->second : d->value("pagina", std::string());
    const json* g = buscarPagina(p->value("id", std::string()), pag);
    if (g == nullptr) g = &(*p)["paginas"][0];
    return renderPagina(*p, *g, dispositivo);
}

void Botonera::refrescar() {
    if (!sucio_) return;
    sucio_ = false;
    for (const auto& id : conectados_) {
        json r = render(id);
        auto it = ultimoRender_.find(id);
        if (it != ultimoRender_.end() && it->second == r) continue;
        ultimoRender_[id] = r;
        emit mensaje(QString::fromStdString(id), r);
    }
}

// ============================================================================
// Aparatos y emparejamiento
// ============================================================================

json Botonera::dispositivos() const {
    json out = json::array();
    for (const auto& d : disp_) {
        if (d.value("origen", std::string()) == "asistente") continue;
        json o = d;
        o.erase("hash");
        o.erase("sal");
        o["conectado"] = conectados_.count(d.value("id", std::string())) > 0;
        auto it = paginaDe_.find(d.value("id", std::string()));
        if (it != paginaDe_.end()) o["pagina_actual"] = it->second;
        out.push_back(o);
    }
    return out;
}

std::string Botonera::asignar(const std::string& dispositivo, const std::string& perfil, const std::string& pagina) {
    json* d = buscarDispositivo(dispositivo);
    if (d == nullptr) return "no hay un aparato «" + dispositivo + "»";
    if (buscarPerfil(perfil) == nullptr) return "no hay un perfil «" + perfil + "»";
    if (!pagina.empty() && buscarPagina(perfil, pagina) == nullptr) return "ese perfil no tiene la página «" + pagina + "»";
    (*d)["perfil"] = perfil;
    (*d)["pagina"] = pagina;
    paginaDe_.erase(dispositivo);
    guardarDispositivos();
    auditar(dispositivo, "asignar", perfil + (pagina.empty() ? "" : "/" + pagina));
    ultimoRender_.erase(dispositivo);
    sucio_ = true;
    emit dispositivosCambio();
    return {};
}

int Botonera::revocar(const std::string& dispositivo) {
    int n = 0;
    for (auto it = disp_.begin(); it != disp_.end();) {
        const std::string id = it->value("id", std::string());
        if ((dispositivo == "todos" && it->value("origen", std::string("tablet")) == "tablet") || id == dispositivo) {
            auditar(id, "revocar", it->value("nombre", std::string()));
            conectados_.erase(id);
            paginaDe_.erase(id);
            it = disp_.erase(it);
            ++n;
        } else {
            ++it;
        }
    }
    if (n > 0) {
        guardarDispositivos();
        emit credencialesCambio();
        emit dispositivosCambio();
    }
    return n;
}

void Botonera::registrarLocal(const std::string& id, const std::string& nombre, const std::string& origen,
                              const json& capacidades) {
    json* d = buscarDispositivo(id);
    if (d == nullptr) {
        if (static_cast<int>(disp_.size()) >= kMaxDispositivos) return;
        std::string perfil;
        if (!datos_["perfiles"].empty()) perfil = datos_["perfiles"][0].value("id", std::string());
        disp_.push_back({{"id", id}, {"nombre", nombre}, {"origen", origen}, {"perfil", perfil}, {"pagina", ""},
                         {"capacidades", capacidades}, {"creado_ms", ahoraMs()}});
        auditar(id, "alta", origen + " " + nombre);
    } else {
        (*d)["nombre"] = nombre;
        (*d)["capacidades"] = capacidades;
    }
    marcarConexion(id, true, "");
}

void Botonera::quitarLocal(const std::string& id) {
    marcarConexion(id, false, "");
    disp_.erase(std::remove_if(disp_.begin(), disp_.end(),
                               [&](const json& d) {
                                   return d.value("id", std::string()) == id &&
                                          d.value("origen", std::string("tablet")) != "tablet";
                               }),
                disp_.end());
    emit dispositivosCambio();
}

json Botonera::credencialesParaGateway() const {
    json out = json::array();
    const bool api = apiRemotaActiva();
    for (const auto& d : disp_)
        if (d.value("origen", std::string("tablet")) == "tablet")
            out.push_back({{"id", d.value("id", std::string())}, {"hash", d.value("hash", std::string())},
                           {"sal", d.value("sal", std::string())},
                           {"http", api && d.value("via", std::string()) == "http"}});
    return out;
}

bool Botonera::apiRemotaActiva() const {
    return datos_.contains("api_remota") && datos_["api_remota"].value("activo", false);
}

void Botonera::setApiRemotaActiva(bool on) {
    if (on == apiRemotaActiva()) return;
    datos_["api_remota"]["activo"] = on;
    guardar();
    auditar("escritorio", on ? "api_remota_prendida" : "api_remota_apagada", "");
    emit credencialesCambio(); // el gateway acepta o deja de aceptar los tokens HTTP
    emit dispositivosCambio();
}

json Botonera::crearAccesoHttp(const std::string& perfil, const std::string& nombre) {
    std::string p = perfil;
    if (p.empty() && !datos_["perfiles"].empty()) p = datos_["perfiles"][0].value("id", std::string());
    if (buscarPerfil(p) == nullptr) return {{"error", "no hay un perfil «" + perfil + "»"}};
    if (static_cast<int>(disp_.size()) >= kMaxDispositivos) return {{"error", "ya hay " + std::to_string(kMaxDispositivos) + " aparatos"}};
    const std::string id = idNuevo("http");
    const std::string token = hexAleatorio(32); // 256 bits
    const std::string sal = hexAleatorio(16);
    const std::string n = nombre.empty() ? std::string("Companion (HTTP)") : nombre.substr(0, 60);
    disp_.push_back({{"id", id},
                     {"nombre", n},
                     {"origen", "tablet"},
                     {"via", "http"},
                     {"perfil", p},
                     {"pagina", ""},
                     {"creado_ms", ahoraMs()},
                     {"ultimo_uso_ms", ahoraMs()},
                     {"vence_dias", 0}, // un equipo fijo: no vence por falta de uso
                     {"hash", sha256(sal + token)},
                     {"sal", sal}});
    guardarDispositivos();
    auditar(id, "acceso_http", n + " → " + p);
    emit credencialesCambio();
    emit dispositivosCambio();
    const json* pf = buscarPerfil(p);
    std::string pag = "1";
    if (pf != nullptr && pf->contains("paginas") && !(*pf)["paginas"].empty()) pag = (*pf)["paginas"][0].value("id", pag);
    return {{"dispositivo", id},
            {"token", token},
            {"perfil", p},
            {"ejemplo", "/api/botonera/" + p + "/pagina/" + pag + "/celda/0/0/tocar"}};
}

json Botonera::accesosHttp() const {
    json out = json::array();
    for (const auto& d : disp_)
        if (d.value("via", std::string()) == "http")
            out.push_back({{"id", d.value("id", std::string())},
                           {"nombre", d.value("nombre", std::string())},
                           {"perfil", d.value("perfil", std::string())},
                           {"ultimo_uso_ms", d.value("ultimo_uso_ms", 0LL)}});
    return out;
}

std::string Botonera::entradaRemota(const std::string& dev, const std::string& perfil, const std::string& pagina, int x,
                                    int y, const std::string& accion, const std::string& ip) {
    const json* d = buscarDispositivo(dev);
    std::string motivo;
    if (d == nullptr || d->value("via", std::string()) != "http") motivo = "no es un acceso HTTP";
    else if (!apiRemotaActiva()) motivo = "la API remota está apagada";
    else if (d->value("perfil", std::string()) != perfil) motivo = "ese acceso es del perfil «" + d->value("perfil", std::string()) + "»";
    else if (buscarPagina(perfil, pagina) == nullptr) motivo = "no hay una página «" + pagina + "» en «" + perfil + "»";
    if (!motivo.empty()) {
        auditar(dev, "api_remota_rechazada", motivo + " (" + ip + ")");
        return motivo;
    }
    // La celda de ESA página, sin cambiar la que ve el aparato en otra parte.
    const std::string antes = paginaDe_.count(dev) ? paginaDe_[dev] : std::string();
    paginaDe_[dev] = pagina;
    if (accion == "presionar" || accion == "tocar") entrada(dev, {{"type", "presionar"}, {"x", x}, {"y", y}});
    if (accion == "soltar" || accion == "tocar") entrada(dev, {{"type", "soltar"}, {"x", x}, {"y", y}});
    if (antes.empty()) paginaDe_.erase(dev);
    else paginaDe_[dev] = antes;
    return {};
}

void Botonera::marcarConexion(const std::string& dispositivo, bool conectado, const std::string& ip) {
    json* d = buscarDispositivo(dispositivo);
    if (d == nullptr) return;
    if (conectado) {
        conectados_.insert(dispositivo);
        (*d)["ultimo_uso_ms"] = ahoraMs();
        if (!ip.empty()) (*d)["ip"] = ip;
        ultimoRender_.erase(dispositivo); // que reciba todo de nuevo
        sucio_ = true;
        auditar(dispositivo, "conectado", ip);
    } else {
        conectados_.erase(dispositivo);
        pendientes_.erase(dispositivo);
        auditar(dispositivo, "desconectado", "");
    }
    if (d->value("origen", std::string("tablet")) == "tablet") guardarDispositivos();
    emit dispositivosCambio();
}

void Botonera::vencerDispositivos() {
    const std::int64_t ahora = ahoraMs();
    std::vector<std::string> viejos;
    for (const auto& d : disp_) {
        if (d.value("origen", std::string("tablet")) != "tablet") continue;
        const int dias = d.value("vence_dias", 30);
        if (dias <= 0) continue; // 0 = no vence
        const std::int64_t ult = std::max(d.value("ultimo_uso_ms", 0LL), d.value("creado_ms", 0LL));
        if (ahora - ult > static_cast<std::int64_t>(dias) * 86400000LL) viejos.push_back(d.value("id", std::string()));
    }
    for (const auto& v : viejos) {
        auditar(v, "vencido", "sin uso");
        revocar(v);
    }
}

json Botonera::nuevoCodigo(const std::string& perfil, const std::string& nombre, int venceDias) {
    // Se limpian los vencidos.
    const std::int64_t ahora = ahoraMs();
    for (auto it = codigos_.begin(); it != codigos_.end();) it = it->second.venceMs < ahora ? codigos_.erase(it) : std::next(it);
    std::string p = perfil;
    if (p.empty() && !datos_["perfiles"].empty()) p = datos_["perfiles"][0].value("id", std::string());
    if (buscarPerfil(p) == nullptr) return {{"error", "no hay un perfil «" + perfil + "»"}};
    const std::string codigo = hexAleatorio(16); // 128 bits
    codigos_[codigo] = {p, nombre, venceDias, ahora + kCodigoMs};
    auditar("escritorio", "codigo", "perfil " + p);
    return {{"codigo", codigo}, {"vence_ms", ahora + kCodigoMs}, {"perfil", p}};
}

std::string Botonera::solicitud(const std::string& sol, const std::string& codigo, const std::string& nombre,
                                const std::string& ip) {
    auto it = codigos_.find(codigo);
    if (it == codigos_.end() || it->second.venceMs < ahoraMs()) {
        auditar(ip, "emparejar_rechazado", "código inválido o vencido");
        return "código inválido o vencido";
    }
    const Codigo c = it->second;
    codigos_.erase(it); // de un solo uso
    if (static_cast<int>(disp_.size()) >= kMaxDispositivos) return "ya hay " + std::to_string(kMaxDispositivos) + " aparatos";
    const std::string n = !nombre.empty() ? nombre.substr(0, 60) : (!c.nombre.empty() ? c.nombre : "Tablet");
    solicitudes_[sol] = {codigo, n, ip, c.perfil, c.venceDias, ahoraMs()};
    auditar(ip, "emparejar_pide", n + " → " + c.perfil);
    const json* p = buscarPerfil(c.perfil);
    emit solicitudNueva(QString::fromStdString(sol), QString::fromStdString(n),
                        QString::fromStdString(p != nullptr ? p->value("nombre", std::string()) : c.perfil),
                        QString::fromStdString(ip));
    emit solicitudesCambio();
    return {};
}

json Botonera::resolverSolicitud(const std::string& sol, bool aprobar) {
    auto it = solicitudes_.find(sol);
    if (it == solicitudes_.end()) return {{"error", "no hay esa solicitud"}};
    const Solicitud s = it->second;
    solicitudes_.erase(it);
    emit solicitudesCambio();
    if (!aprobar) {
        auditar(s.ip, "emparejar_negado", s.nombre);
        return {{"aprobada", false}};
    }
    const std::string id = idNuevo("tab");
    const std::string token = hexAleatorio(32); // 256 bits
    const std::string sal = hexAleatorio(16);
    disp_.push_back({{"id", id},
                     {"nombre", s.nombre},
                     {"origen", "tablet"},
                     {"perfil", s.perfil},
                     {"pagina", ""},
                     {"ip", s.ip},
                     {"creado_ms", ahoraMs()},
                     {"ultimo_uso_ms", ahoraMs()},
                     {"vence_dias", s.venceDias},
                     {"hash", sha256(sal + token)},
                     {"sal", sal}});
    guardarDispositivos();
    auditar(id, "emparejado", s.nombre + " (" + s.ip + ") → " + s.perfil);
    emit credencialesCambio();
    emit dispositivosCambio();
    return {{"aprobada", true}, {"dispositivo", id}, {"token", token}};
}

// ============================================================================
// Entradas de los aparatos
// ============================================================================

bool Botonera::limiteOk(const std::string& dispositivo) {
    auto& q = acciones_[dispositivo];
    const std::int64_t ahora = ahoraMs();
    while (!q.empty() && ahora - q.front() > 1000) q.pop_front();
    if (static_cast<int>(q.size()) >= kAccionesPorSegundo) return false;
    q.push_back(ahora);
    return true;
}

void Botonera::avisar(const std::string& dispositivo, const std::string& texto, bool error) {
    emit mensaje(QString::fromStdString(dispositivo), json{{"type", "aviso"}, {"texto", texto}, {"error", error}});
}

void Botonera::entrada(const std::string& dev, const json& msg) {
    json* d = buscarDispositivo(dev);
    if (d == nullptr) return;
    (*d)["ultimo_uso_ms"] = ahoraMs();
    const std::string t = msg.value("type", std::string());
    if (t == "hola") {
        conectados_.insert(dev);
        ultimoRender_[dev] = render(dev);
        emit mensaje(QString::fromStdString(dev), ultimoRender_[dev]);
        return;
    }
    const json* p = buscarPerfil(d->value("perfil", std::string()));
    if (p == nullptr) {
        avisar(dev, "Este aparato no tiene perfil: asignalo en la página Botonera.", true);
        return;
    }
    const json r = render(dev);
    const std::string pagId = r.contains("pagina") ? r["pagina"].value("id", std::string()) : std::string();
    if (t == "pagina") {
        const std::string id = msg.value("id", std::string());
        if (buscarPagina(p->value("id", std::string()), id) == nullptr) return;
        paginaDe_[dev] = id;
        ultimoRender_[dev] = render(dev);
        emit mensaje(QString::fromStdString(dev), ultimoRender_[dev]);
        return;
    }
    const json* g = buscarPagina(p->value("id", std::string()), pagId);
    if (g == nullptr) return;
    const int x = msg.value("x", -1), y = msg.value("y", -1);
    const json* c = celdaEn(*g, x, y);
    if (c == nullptr) return;
    const int cx = c->value("x", 0), cy = c->value("y", 0);
    const std::string celda = claveCelda(cx, cy);
    const std::string kv = p->value("id", std::string()) + "/" + pagId + "/" + celda;
    const std::string kd = dev + "|" + celda;
    const std::string tipo = c->value("tipo", std::string("boton"));
    if (t == "editar") {
        if (!p->value("puede_editar", false)) {
            auditar(dev, "editar_rechazado", celda);
            return avisar(dev, "Este perfil no permite editar desde el aparato.", true);
        }
        json nueva = msg.value("celda", json::object());
        nueva["x"] = cx;
        nueva["y"] = cy;
        // Desde el aparato no se cambian permisos: la acción tiene que estar en la lista del perfil.
        if (const std::string e = setCelda(p->value("id", std::string()), pagId, nueva); !e.empty()) avisar(dev, e, true);
        return;
    }
    if (t == "confirmar") {
        auto pe = pendientes_.find(dev);
        if (pe == pendientes_.end() || pe->second.celda != celda || pe->second.venceMs < ahoraMs()) {
            pendientes_.erase(dev);
            sucio_ = true;
            return avisar(dev, "La confirmación venció: volvé a apretar.", true);
        }
        const json accion = pe->second.accion;
        pendientes_.erase(pe);
        ultimoRender_.erase(dev);
        sucio_ = true;
        correr(dev, *p, accion, celda, valores_.count(kv) ? valores_[kv] : 0, 0, true);
        return;
    }
    if (t == "cancelar") {
        pendientes_.erase(dev);
        sucio_ = true;
        return;
    }
    if (t == "presionar" && (tipo == "boton" || tipo == "perilla")) {
        if (c->contains("accion_mantener")) {
            presionadoMs_[kd] = ahoraMs();
            mantenidos_.erase(kd);
            QPointer<Botonera> yo(this);
            const json am = (*c)["accion_mantener"];
            const std::int64_t marca = presionadoMs_[kd];
            QTimer::singleShot(kMantenerMs, this, [yo, dev, kd, marca, am, celda]() {
                if (!yo) return;
                auto it = yo->presionadoMs_.find(kd);
                if (it == yo->presionadoMs_.end() || it->second != marca) return; // ya lo soltaron
                yo->mantenidos_.insert(kd);
                const json* dd = yo->buscarDispositivo(dev);
                const json* pp = dd != nullptr ? yo->buscarPerfil(dd->value("perfil", std::string())) : nullptr;
                if (pp != nullptr) yo->correr(dev, *pp, am, celda, 0, 0, false);
            });
        } else if (c->contains("accion")) {
            correr(dev, *p, (*c)["accion"], celda, valores_.count(kv) ? valores_[kv] : 0, 0, false);
        }
        return;
    }
    if (t == "soltar") {
        if (c->contains("accion_mantener")) {
            const bool fueMantenido = mantenidos_.count(kd) > 0;
            presionadoMs_.erase(kd);
            mantenidos_.erase(kd);
            if (!fueMantenido && c->contains("accion")) correr(dev, *p, (*c)["accion"], celda, 0, 0, false);
        }
        if (c->contains("accion_soltar")) correr(dev, *p, (*c)["accion_soltar"], celda, 0, 0, false);
        return;
    }
    if ((t == "valor" || t == "girar") && (tipo == "fader" || tipo == "perilla")) {
        const json v = c->value("valor", json::object());
        const double mn = numero(v.value("min", json(0)), 0), mx = numero(v.value("max", json(100)), 100);
        const double paso = std::max(1e-6, numero(v.value("paso", json(1)), 1));
        const bool relativo = v.value("modo", std::string("absoluto")) == "relativo";
        if (!c->contains("accion")) return;
        if (t == "girar" && relativo) {
            auto& pend = perillaPend_[kd];
            pend.first = (*c)["accion"];
            pend.second += numero(msg.value("delta", json(0)), 0);
            return;
        }
        double nuevo;
        const json render0 = renderCelda(*c, kv);
        const double actual = numero(render0.value("valor", json(mn)), mn);
        if (t == "girar") nuevo = actual + numero(msg.value("delta", json(0)), 0) * paso;
        else nuevo = numero(msg.value("v", json(actual)), actual);
        nuevo = std::clamp(std::round(nuevo / paso) * paso, mn, mx);
        valores_[kv] = nuevo;
        faderPend_[kd] = {(*c)["accion"], nuevo};
        sucio_ = true;
        return;
    }
}

std::string Botonera::simular(const std::string& perfil, const std::string& pagina, const json& msg) {
    if (buscarPagina(perfil, pagina) == nullptr) return "no hay esa página (perfil «" + perfil + "», página «" + pagina + "»)";
    json* d = buscarDispositivo("asistente");
    if (d == nullptr) {
        disp_.push_back({{"id", "asistente"}, {"nombre", "Asistente (prueba)"}, {"origen", "asistente"}});
        d = &disp_.back();
    }
    (*d)["perfil"] = perfil;
    (*d)["pagina"] = pagina;
    paginaDe_.erase("asistente");
    entrada("asistente", msg);
    return {};
}

void Botonera::correr(const std::string& dev, const json& perfil, const json& accion, const std::string& celda,
                      double valor, double delta, bool confirmadoEnAparato) {
    if (!accion.is_object()) return;
    // El asistente no tiene un aparato donde confirmar: lo de aire lo frena la
    // guarda del MCP y lo aprueba una persona en el escritorio.
    const bool confirmado = confirmadoEnAparato || dev == "asistente";
    const std::string tipo = accion.value("tipo", std::string("herramienta"));
    if (tipo == "pagina") {
        const std::string id = accion.value("ir_a", std::string());
        if (buscarPagina(perfil.value("id", std::string()), id) == nullptr) return;
        paginaDe_[dev] = id;
        ultimoRender_[dev] = render(dev);
        emit mensaje(QString::fromStdString(dev), ultimoRender_[dev]);
        return;
    }
    // Lo que se va a ejecutar, como herramientas.
    std::vector<json> pasos;
    std::function<void(const json&)> aplanar = [&](const json& a) {
        const std::string t = a.value("tipo", std::string("herramienta"));
        if (t == "secuencia") {
            for (const auto& p : a.value("pasos", json::array())) aplanar(p);
        } else if (t == "receta") {
            pasos.push_back({{"herramienta", "receta_correr"}, {"args", {{"id", a.value("id", std::string())}}}});
        } else if (t == "herramienta") {
            pasos.push_back({{"herramienta", a.value("herramienta", std::string())},
                             {"args", a.value("args", json::object())}});
        }
    };
    aplanar(accion);
    if (pasos.empty()) return;
    // Permisos del perfil: del lado de la app, nunca del cliente.
    std::set<std::string> permitidas;
    for (const auto& h : perfil.value("herramientas_permitidas", json::array()))
        if (h.is_string()) permitidas.insert(h.get<std::string>());
    // El estado de la celda (para {{no_activo}}).
    bool activo = false;
    if (buscarDispositivo(dev) != nullptr) {
        const json r = render(dev);
        for (const auto& c : r.value("celdas", json::array()))
            if (claveCelda(c.value("x", 0), c.value("y", 0)) == celda) activo = c.value("activo", false);
    }
    bool deAire = false;
    for (auto& p : pasos) {
        const std::string h = p["herramienta"];
        if (!permitidas.count("*") && !permitidas.count(h)) {
            auditar(dev, "rechazada", h + " (no está en el perfil «" + perfil.value("nombre", std::string()) + "»)");
            avisar(dev, "«" + h + "» no está permitida en este perfil.", true);
            return;
        }
        p["args"] = plantilla(p["args"], valor, delta, activo);
        if (g_.deAireAhora && g_.deAireAhora(h, p["args"])) deAire = true;
    }
    // Una acción con «confirmar» (p. ej. el micrófono del presidente) también se confirma en el aparato.
    if ((deAire || accion.value("confirmar", false)) && !confirmado) {
        pendientes_[dev] = {celda, accion, ahoraMs() + kConfirmarMs};
        auditar(dev, "confirmar", celda);
        emit mensaje(QString::fromStdString(dev),
                     json{{"type", "confirmar"}, {"celda", celda}, {"vence_ms", kConfirmarMs},
                          {"texto", "Cambia el aire: confirmá"}});
        ultimoRender_.erase(dev);
        sucio_ = true;
        return;
    }
    const bool esFader = enCoalescer_;
    if (!esFader && !limiteOk(dev)) {
        auditar(dev, "limite", celda);
        return avisar(dev, "Demasiadas acciones por segundo.", true);
    }
    if (!g_.ejecutar) return;
    const std::string perfilId = perfil.value("id", std::string());
    const int espera = accion.value("espera_ms", 0);
    QPointer<Botonera> yo(this);
    // Se ejecutan en orden; una secuencia espera `espera_ms` entre pasos.
    auto siguiente = std::make_shared<std::function<void(std::size_t)>>();
    *siguiente = [yo, pasos, dev, perfilId, espera, siguiente, esFader](std::size_t i) {
        if (!yo || i >= pasos.size()) return;
        const std::string h = pasos[i]["herramienta"];
        const json args = pasos[i]["args"];
        if (!esFader) yo->auditar(dev, "accion", h + " " + args.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace));
        yo->g_.ejecutar(h, args, dev, perfilId, [yo, dev, h, i, espera, siguiente](bool ok, const std::string& texto) {
            if (!yo) return;
            if (!ok) {
                yo->auditar(dev, "error", h + ": " + texto);
                yo->avisar(dev, texto.empty() ? "No se pudo." : texto, true);
                return; // una secuencia se corta en el primer error
            }
            if (espera > 0) QTimer::singleShot(espera, yo, [siguiente, i]() { (*siguiente)(i + 1); });
            else (*siguiente)(i + 1);
        });
    };
    (*siguiente)(0);
}

// ============================================================================
// Interfaz
// ============================================================================

QVariantList Botonera::perfilesUi() const { return aQv(perfiles()).toList(); }
QVariantList Botonera::dispositivosUi() const { return aQv(dispositivos()).toList(); }
QVariantList Botonera::solicitudesUi() const {
    QVariantList out;
    for (const auto& [id, s] : solicitudes_) {
        const json* p = buscarPerfil(s.perfil);
        out.append(QVariantMap{{"id", QString::fromStdString(id)},
                               {"nombre", QString::fromStdString(s.nombre)},
                               {"ip", QString::fromStdString(s.ip)},
                               {"perfil", QString::fromStdString(p != nullptr ? p->value("nombre", std::string()) : s.perfil)}});
    }
    return out;
}

QVariantMap Botonera::paginaUi(const QString& perfil, const QString& pagina) const {
    const json* p = buscarPerfil(perfil.toStdString());
    if (p == nullptr) return {};
    const json* g = buscarPagina(perfil.toStdString(), pagina.toStdString());
    if (g == nullptr) {
        if (p->value("paginas", json::array()).empty()) return {};
        g = &(*p)["paginas"][0];
    }
    return aQv(renderPagina(*p, *g, "escritorio")).toMap();
}

QString Botonera::crearPerfilUi(const QString& nombre, const QString& plantilla) {
    std::string id;
    json p = json::object();
    if (!nombre.isEmpty()) p["nombre"] = nombre.toStdString();
    const std::string e = crearPerfil(p, plantilla.toStdString(), &id);
    return QString::fromStdString(e.empty() ? id : "error: " + e);
}

QString Botonera::editarPerfilUi(const QString& id, const QVariantMap& c) {
    return QString::fromStdString(editarPerfil(id.toStdString(), deQv(c), true));
}
bool Botonera::borrarPerfilUi(const QString& id) { return borrarPerfil(id.toStdString()); }
QString Botonera::crearPaginaUi(const QString& perfil, const QString& nombre, int columnas, int filas) {
    std::string id;
    const std::string e = crearPagina(perfil.toStdString(), nombre.toStdString(), columnas, filas, &id);
    return QString::fromStdString(e.empty() ? id : "error: " + e);
}
QString Botonera::editarPaginaUi(const QString& perfil, const QString& pagina, const QVariantMap& c) {
    return QString::fromStdString(editarPagina(perfil.toStdString(), pagina.toStdString(), deQv(c)));
}
bool Botonera::borrarPaginaUi(const QString& perfil, const QString& pagina) {
    return borrarPagina(perfil.toStdString(), pagina.toStdString());
}
QString Botonera::setCeldaUi(const QString& perfil, const QString& pagina, const QString& celdaJson) {
    const json c = json::parse(celdaJson.toStdString(), nullptr, false);
    if (c.is_discarded()) return "el JSON de la celda no se entiende";
    return QString::fromStdString(setCelda(perfil.toStdString(), pagina.toStdString(), c));
}
QString Botonera::celdaJsonUi(const QString& perfil, const QString& pagina, int x, int y) const {
    const json* g = buscarPagina(perfil.toStdString(), pagina.toStdString());
    if (g == nullptr) return {};
    const json* c = celdaEn(*g, x, y);
    return c != nullptr ? QString::fromStdString(c->dump(2)) : QString();
}
bool Botonera::borrarCeldaUi(const QString& perfil, const QString& pagina, int x, int y) {
    return borrarCelda(perfil.toStdString(), pagina.toStdString(), x, y);
}
QVariantList Botonera::plantillasUi() const { return aQv(plantillas()).toList(); }
QString Botonera::exportarUi(const QString& perfil) const {
    const json p = exportar(perfil.toStdString());
    return p.is_null() ? QString() : QString::fromStdString(p.dump(2, ' ', false, nlohmann::json::error_handler_t::replace));
}
QString Botonera::importarUi(const QString& texto) {
    const json p = json::parse(texto.toStdString(), nullptr, false);
    if (p.is_discarded()) return "error: el JSON no se entiende";
    std::string id;
    const std::string e = importar(p, &id);
    return QString::fromStdString(e.empty() ? id : "error: " + e);
}

void Botonera::probarUi(const QString& perfil, const QString& pagina, int x, int y) {
    const json* p = buscarPerfil(perfil.toStdString());
    const json* g = buscarPagina(perfil.toStdString(), pagina.toStdString());
    if (p == nullptr || g == nullptr) return;
    const json* c = celdaEn(*g, x, y);
    if (c == nullptr || !c->contains("accion")) return;
    // Desde el escritorio ya decidió una persona: no hace falta confirmar acá.
    correr("escritorio", *p, (*c)["accion"], claveCelda(c->value("x", 0), c->value("y", 0)), 0, 0, true);
}

QString Botonera::asignarUi(const QString& dispositivo, const QString& perfil, const QString& pagina) {
    return QString::fromStdString(asignar(dispositivo.toStdString(), perfil.toStdString(), pagina.toStdString()));
}
int Botonera::revocarUi(const QString& dispositivo) { return revocar(dispositivo.toStdString()); }

QVariantList Botonera::auditoriaUi() const {
    QVariantList out;
    for (auto it = auditoria_.rbegin(); it != auditoria_.rend() && out.size() < 200; ++it)
        out.append(QVariantMap{
            {"hora", QDateTime::fromMSecsSinceEpoch(it->value("ms", 0LL)).toString("dd/MM HH:mm:ss")},
            {"dispositivo", QString::fromStdString(it->value("dispositivo", std::string()))},
            {"que", QString::fromStdString(it->value("que", std::string()))},
            {"detalle", QString::fromStdString(it->value("detalle", std::string()))}});
    return out;
}

QVariantList Botonera::catalogoUi() const {
    if (!g_.catalogo) return {};
    json cat = g_.catalogo();
    for (auto& c : cat) {
        // La miniatura de la página de la categoría, si viaja en el programa.
        const QString ruta = ":/botonera/" + QString::fromStdString(c.value("id", std::string())) + ".jpg";
        if (QFile::exists(ruta)) c["captura"] = ("qrc" + ruta).toStdString();
    }
    return QJsonDocument::fromJson(QByteArray::fromStdString(cat.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace))).array().toVariantList();
}

QVariantList Botonera::buscarUi(const QString& texto) const {
    if (!g_.buscar) return {};
    return QJsonDocument::fromJson(QByteArray::fromStdString(g_.buscar(texto.toStdString()).dump(-1, ' ', false, nlohmann::json::error_handler_t::replace))).array().toVariantList();
}

QStringList Botonera::herramientasUi(const QString& filtro) const {
    QStringList out;
    if (!g_.herramientas) return out;
    for (const auto& h : g_.herramientas())
        if (filtro.isEmpty() || QString::fromStdString(h).contains(filtro, Qt::CaseInsensitive))
            out << QString::fromStdString(h);
    return out;
}

} // namespace ba::app::control
