// SPDX-License-Identifier: GPL-2.0-or-later
// src/app/control/ControlWeb.cpp — ver el .hpp
#include "control/ControlWeb.hpp"

#include "ajustes/Ajustes.hpp"
#include "diag/Registro.hpp"
#include "mcp/McpCore.hpp"
#include "mcp/McpTraza.hpp"

#include <QCryptographicHash>
#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QRandomGenerator>
#include <QRegularExpression>

#include <algorithm>
#include <fstream>

namespace ba::app::control {

using json = nlohmann::json;
namespace fs = std::filesystem;

namespace {

constexpr int kMaxAparatos = 32;
/// La categoría de las herramientas que administran esto: ningún aparato la usa
/// (ampliar un perfil o aprobar otro aparato lo hace una persona en la app).
constexpr const char* kCategoriaPropia = "control_web";
/// Cuántas herramientas por mensaje en el auth (el catálogo entero no entra en uno).
constexpr std::size_t kTanda = 120;
inline std::int64_t kCodigoMs() { return ba::ajustes::entero64("controlWeb.codigoMin", 5) * 60 * 1000; }
inline int kLlamadasPorSegundo() { return ba::ajustes::entero("controlWeb.llamadasPorSegundo", 30); }
inline std::int64_t kSondeoTrabadoMs() { return ba::ajustes::entero64("controlWeb.sondeoTrabadoMs", 5000); }

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

bool igualesTiempoConstante(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    unsigned char d = 0;
    for (std::size_t i = 0; i < a.size(); ++i) d |= static_cast<unsigned char>(a[i] ^ b[i]);
    return d == 0;
}

std::string volcar(const json& j) { return j.dump(-1, ' ', false, json::error_handler_t::replace); }

QVariant aQv(const json& j) {
    return QJsonDocument::fromJson(QByteArray::fromStdString(volcar(json{{"v", j}}))).object().value("v").toVariant();
}

std::string textoDe(const json& j, const char* k) {
    return j.is_object() && j.contains(k) && j[k].is_string() ? j[k].get<std::string>() : std::string();
}

/// El perfil para leerlo: «todo» o «escenas, audio».
std::string perfilTexto(const json& p) {
    if (!p.is_array() || p.empty()) return "nada";
    if (p.size() == 1 && p[0] == "*") return "todo";
    std::string s;
    for (const auto& c : p) s += (s.empty() ? "" : ", ") + c.get<std::string>();
    return s;
}

/// Del texto de la interfaz («todo», «*», «escenas, audio») al perfil.
json perfilDeTexto(const QString& t) {
    const QString x = t.trimmed().toLower();
    if (x.isEmpty() || x == "todo" || x == "*") return json::array({"*"});
    json out = json::array();
    for (const QString& c : t.split(QRegularExpression("[,;\\s]+"), Qt::SkipEmptyParts)) out.push_back(c.toStdString());
    return out;
}

/// Lo que devolvió una herramienta, para el aparato: structuredContent, o el
/// texto como JSON si lo es, o el texto. Si trae imágenes (el QR de room_link),
/// van en `imagenes: [{mime, data}]` (y el texto suelto, en `texto`).
json datosDe(const json& structured, const json& content) {
    std::string t;
    json imagenes = json::array();
    for (const auto& c : content.is_array() ? content : json::array()) {
        const std::string tipo = c.value("type", std::string());
        if (tipo == "text") t += (t.empty() ? "" : "\n") + c.value("text", std::string());
        else if (tipo == "image")
            imagenes.push_back({{"mime", c.value("mimeType", std::string())}, {"data", c.value("data", std::string())}});
    }
    json d = structured;
    if (d.is_null()) {
        d = json::parse(t, nullptr, false);
        if (d.is_discarded()) d = json(t);
    }
    if (imagenes.empty()) return d;
    if (!d.is_object()) d = json{{"texto", d}};
    d["imagenes"] = std::move(imagenes);
    return d;
}

} // namespace

ControlWeb::ControlWeb(const fs::path& datos, QObject* parent)
    : QObject(parent), archivo_(datos / "Config" / "control-web.json") {
    cargar();
}

ControlWeb::~ControlWeb() {
    // Que el Mezclador (u otro) no quede midiendo para nadie.
    if (g_.mirando)
        for (const auto& [tema, s] : sondeos_) g_.mirando(tema, false);
}

void ControlWeb::setCore(mcp::McpCore* core) { core_ = core; }

const std::vector<ControlWeb::Tema>& ControlWeb::temas() {
    static const std::vector<Tema> t = {
        {"escenas", {"scene_list"}, 500},
        {"audio", {"mezclador_estado"}, 66},
        {"ptz", {"ptz_camaras"}, 500},
        {"salidas", {"stream_salud", "record_disco"}, 1000},
        {"latencias", {"tiempos_mapa"}, 1000},
        {"estado", {"status"}, 2000},
        {"sala", {"room_status"}, 1000},
        {"oradores", {"remotos_fila_ver"}, 1000},
        {"graficos", {"dsk_capas_ver", "grafico_en_escena"}, 1000},
    };
    return t;
}

// ============================================================================
// Persistencia
// ============================================================================

void ControlWeb::cargar() {
    std::ifstream f(archivo_);
    const json j = json::parse(f, nullptr, false);
    datos_ = j.is_object() ? j : json::object();
    if (!datos_.contains("aparatos") || !datos_["aparatos"].is_array()) datos_["aparatos"] = json::array();
    if (!datos_.contains("tunel") || !datos_["tunel"].is_object()) datos_["tunel"] = {{"habilitado", false}};
    // Un perfil roto no se adivina: queda vacío (no puede nada) hasta que una persona lo arregle.
    for (auto& a : datos_["aparatos"]) {
        const json p = normalizarPerfil(a.value("perfil", json()));
        a["perfil"] = p.is_null() ? json::array() : p;
    }
}

void ControlWeb::guardar() const {
    std::error_code ec;
    fs::create_directories(archivo_.parent_path(), ec);
    const fs::path tmp = archivo_.string() + ".tmp";
    {
        std::ofstream o(tmp);
        o << datos_.dump(1, ' ', false, json::error_handler_t::replace);
    }
    fs::permissions(tmp, fs::perms::owner_read | fs::perms::owner_write, fs::perm_options::replace, ec);
    fs::rename(tmp, archivo_, ec);
}

json* ControlWeb::buscarAparato(const std::string& id) {
    for (auto& a : datos_["aparatos"])
        if (a.value("id", std::string()) == id) return &a;
    return nullptr;
}

const json* ControlWeb::buscarAparato(const std::string& id) const {
    for (const auto& a : datos_["aparatos"])
        if (a.value("id", std::string()) == id) return &a;
    return nullptr;
}

void ControlWeb::auditar(const std::string& aparato, const std::string& texto) const {
    const json* a = buscarAparato(aparato);
    const std::string quien = aparato + (a != nullptr ? " «" + a->value("nombre", std::string()) + "»" : std::string());
    ba::diag::registro().linea(ba::diag::Nivel::Info, "control-web", 0, quien + ": " + texto);
}

// ============================================================================
// Perfiles
// ============================================================================

json ControlWeb::normalizarPerfil(const json& perfil, std::string* error) {
    std::vector<std::string> v;
    if (perfil.is_string()) {
        v.push_back(perfil.get<std::string>());
    } else if (perfil.is_array()) {
        for (const auto& c : perfil) {
            if (!c.is_string() || c.get<std::string>().empty() || c.get<std::string>().size() > 64) {
                if (error) *error = "el perfil es una lista de categorías (texto) o \"*\"";
                return nullptr;
            }
            v.push_back(c.get<std::string>());
        }
    } else {
        if (error) *error = "el perfil es una lista de categorías o \"*\" (todas)";
        return nullptr;
    }
    for (auto& c : v)
        if (c == "todo" || c == "todas") c = "*";
    if (std::find(v.begin(), v.end(), "*") != v.end()) return json::array({"*"});
    std::sort(v.begin(), v.end());
    v.erase(std::unique(v.begin(), v.end()), v.end());
    v.erase(std::remove(v.begin(), v.end(), std::string(kCategoriaPropia)), v.end());
    return v;
}

bool ControlWeb::amplia(const json& antes, const json& despues) {
    const auto tiene = [](const json& p, const std::string& c) {
        return p.is_array() && std::any_of(p.begin(), p.end(), [&](const json& x) { return x == c; });
    };
    if (tiene(antes, "*")) return false;
    if (!despues.is_array()) return false;
    for (const auto& c : despues)
        if (!tiene(antes, c.get<std::string>())) return true;
    return false;
}

bool ControlWeb::permite(const json& perfil, const std::string& categoria) {
    if (categoria == kCategoriaPropia || !perfil.is_array()) return false;
    for (const auto& c : perfil)
        if (c == "*" || c == categoria) return true;
    return false;
}

// ============================================================================
// Aparatos y emparejamiento
// ============================================================================

json ControlWeb::aparatos() const {
    json out = json::array();
    for (const auto& a : datos_["aparatos"]) {
        const std::string id = a.value("id", std::string());
        const bool conectado = std::any_of(conns_.begin(), conns_.end(), [&](const auto& c) { return c.second.aparato == id; });
        out.push_back({{"id", id},
                       {"nombre", a.value("nombre", std::string())},
                       {"perfil", a.value("perfil", json::array())},
                       {"ip", a.value("ip", std::string())},
                       {"creado_ms", a.value("creado_ms", std::int64_t{0})},
                       {"ultimo_uso_ms", a.value("ultimo_uso_ms", std::int64_t{0})},
                       {"conectado", conectado}});
    }
    return out;
}

json ControlWeb::solicitudes() const {
    json out = json::array();
    for (const auto& [id, s] : solicitudes_)
        out.push_back({{"id", id}, {"nombre", s.nombre}, {"ip", s.ip}, {"perfil", s.perfil}, {"ms", s.ms}});
    return out;
}

json ControlWeb::nuevoCodigo(const json& perfil, const std::string& nombre) {
    const std::int64_t ahora = ahoraMs();
    for (auto it = codigos_.begin(); it != codigos_.end();) it = it->second.venceMs < ahora ? codigos_.erase(it) : std::next(it);
    std::string e;
    const json p = normalizarPerfil(perfil.is_null() ? json("*") : perfil, &e);
    if (p.is_null()) return {{"error", e}};
    const std::string codigo = hexAleatorio(16); // 128 bits
    codigos_[codigo] = {p, nombre.substr(0, 60), ahora + kCodigoMs()};
    ba::diag::registro().linea(ba::diag::Nivel::Info, "control-web", 0, "código de emparejamiento (perfil " + perfilTexto(p) + ")");
    return {{"codigo", codigo}, {"vence_ms", ahora + kCodigoMs()}, {"perfil", p}};
}

std::string ControlWeb::solicitud(const std::string& sol, const std::string& codigo, const std::string& nombre,
                                  const std::string& ip) {
    auto it = codigos_.find(codigo);
    if (it == codigos_.end() || it->second.venceMs < ahoraMs()) {
        ba::diag::registro().linea(ba::diag::Nivel::Aviso, "control-web", 0, "emparejar rechazado desde " + ip + ": código inválido o vencido");
        return "código inválido o vencido";
    }
    const Codigo c = it->second;
    codigos_.erase(it); // de un solo uso
    if (static_cast<int>(datos_["aparatos"].size()) >= kMaxAparatos)
        return "ya hay " + std::to_string(kMaxAparatos) + " aparatos";
    const std::string n = !nombre.empty() ? nombre.substr(0, 60) : (!c.nombre.empty() ? c.nombre : "Navegador");
    solicitudes_[sol] = {n, ip, c.perfil, ahoraMs()};
    ba::diag::registro().linea(ba::diag::Nivel::Info, "control-web", 0,
                               "«" + n + "» (" + ip + ") pide emparejarse (perfil " + perfilTexto(c.perfil) + ")");
    emit solicitudNueva(QString::fromStdString(sol), QString::fromStdString(n), QString::fromStdString(ip));
    emit solicitudesCambio();
    return {};
}

json ControlWeb::resolverSolicitud(const std::string& sol, bool aprobar, const json& perfil, bool humano) {
    auto it = solicitudes_.find(sol);
    if (it == solicitudes_.end()) return {{"error", "no hay esa solicitud"}};
    if (aprobar && !humano)
        return {{"error", "aprobar un aparato lo hace una persona (en la app, Servicios → Control web)"}};
    json p = it->second.perfil;
    if (!perfil.is_null()) {
        std::string e;
        p = normalizarPerfil(perfil, &e);
        if (p.is_null()) return {{"error", e}};
    }
    const Solicitud s = it->second;
    solicitudes_.erase(it);
    emit solicitudesCambio();
    if (!aprobar) {
        ba::diag::registro().linea(ba::diag::Nivel::Info, "control-web", 0, "emparejamiento negado: «" + s.nombre + "» (" + s.ip + ")");
        emit resuelta({{"solicitud", sol}, {"estado", "rechazada"}});
        return {{"aprobada", false}};
    }
    std::string id;
    do id = "cw-" + hexAleatorio(4);
    while (buscarAparato(id) != nullptr);
    const std::string token = hexAleatorio(32); // 256 bits
    const std::string sal = hexAleatorio(16);
    datos_["aparatos"].push_back({{"id", id},
                                  {"nombre", s.nombre},
                                  {"hash", sha256(sal + token)},
                                  {"sal", sal},
                                  {"perfil", p},
                                  {"ip", s.ip},
                                  {"creado_ms", ahoraMs()},
                                  {"ultimo_uso_ms", ahoraMs()}});
    guardar();
    auditar(id, "emparejado desde " + s.ip + " con el perfil " + perfilTexto(p));
    // Primero las credenciales (para que el token ya valga) y después el token al aparato.
    emit credencialesCambio();
    emit resuelta({{"solicitud", sol}, {"estado", "aprobada"}, {"aparato", id}, {"token", token}});
    emit aparatosCambio();
    return {{"aprobada", true}, {"aparato", id}, {"token", token}, {"perfil", p}};
}

std::string ControlWeb::setPerfil(const std::string& aparato, const json& categorias, bool humano) {
    json* a = buscarAparato(aparato);
    if (a == nullptr) return "no hay un aparato «" + aparato + "»";
    std::string e;
    const json p = normalizarPerfil(categorias, &e);
    if (p.is_null()) return e;
    const json antes = a->value("perfil", json::array());
    if (!humano && amplia(antes, p)) return "ampliar el perfil de un aparato lo hace una persona: solo se puede achicar";
    (*a)["perfil"] = p;
    guardar();
    auditar(aparato, "perfil " + perfilTexto(antes) + " → " + perfilTexto(p) + (humano ? "" : " (achicado por el asistente)"));
    // Lo que mira y las herramientas que ve se rehacen ya.
    for (auto& [cid, c] : conns_) {
        if (c.aparato != aparato) continue;
        enviarAuth(cid);
        json t = json::array();
        for (const auto& x : c.temas) t.push_back(x);
        mirar(cid, {{"temas", t}});
    }
    emit aparatosCambio();
    return {};
}

bool ControlWeb::quitar(const std::string& aparato) {
    auto& v = datos_["aparatos"];
    const auto antes = v.size();
    v.erase(std::remove_if(v.begin(), v.end(), [&](const json& a) { return a.value("id", std::string()) == aparato; }), v.end());
    if (v.size() == antes) return false;
    guardar();
    ba::diag::registro().linea(ba::diag::Nivel::Info, "control-web", 0, aparato + ": quitado (su token deja de valer)");
    std::vector<std::string> cortarlas;
    for (const auto& [cid, c] : conns_)
        if (c.aparato == aparato) cortarlas.push_back(cid);
    for (const auto& cid : cortarlas) {
        conns_.erase(cid);
        emit cortar(QString::fromStdString(cid), "Este aparato fue quitado.");
    }
    reacomodarSondeos();
    emit credencialesCambio();
    emit aparatosCambio();
    if (!cortarlas.empty()) emit conexionesCambio();
    return true;
}

bool ControlWeb::tunel() const {
    const json& t = datos_["tunel"];
    return t.value("habilitado", false) && t.value("hash_pin", std::string()).size() == 64;
}

std::string ControlWeb::setTunel(bool habilitar, const std::string& pin, bool humano) {
    if (!habilitar) {
        datos_["tunel"] = {{"habilitado", false}};
        guardar();
        ba::diag::registro().linea(ba::diag::Nivel::Info, "control-web", 0, "acceso desde afuera: apagado");
        emit credencialesCambio();
        emit aparatosCambio();
        return {};
    }
    if (!humano) return "habilitar el acceso desde afuera lo hace una persona";
    if (pin.size() < 4 || pin.size() > 12 || !std::all_of(pin.begin(), pin.end(), [](char c) { return c >= '0' && c <= '9'; }))
        return "el PIN es de 4 a 12 cifras";
    const std::string sal = hexAleatorio(16);
    datos_["tunel"] = {{"habilitado", true}, {"sal", sal}, {"hash_pin", sha256(sal + pin)}};
    guardar();
    ba::diag::registro().linea(ba::diag::Nivel::Aviso, "control-web", 0, "acceso desde afuera (túnel, con PIN): HABILITADO");
    emit credencialesCambio();
    emit aparatosCambio();
    return {};
}

json ControlWeb::credencialesParaGateway() const {
    json ap = json::array();
    for (const auto& a : datos_["aparatos"])
        ap.push_back({{"id", a.value("id", std::string())}, {"hash", a.value("hash", std::string())}, {"sal", a.value("sal", std::string())}});
    const json& t = datos_["tunel"];
    return {{"aparatos", ap},
            {"tunel", {{"habilitado", tunel()}, {"sal", t.value("sal", std::string())}, {"hash_pin", t.value("hash_pin", std::string())}}}};
}

std::string ControlWeb::autenticar(const std::string& token) const {
    if (token.size() != 64) return {};
    std::string encontrado;
    for (const auto& a : datos_["aparatos"])
        if (igualesTiempoConstante(sha256(a.value("sal", std::string()) + token), a.value("hash", std::string())))
            encontrado = a.value("id", std::string());
    return encontrado;
}

// ============================================================================
// Conexiones
// ============================================================================

void ControlWeb::mandar(const std::string& conexion, const json& msg) {
    emit enviar(QStringList{QString::fromStdString(conexion)}, msg);
}

void ControlWeb::conexion(const std::string& cid, const std::string& aparato, bool conectado, const std::string& ip,
                          const std::string& lugar) {
    if (!conectado) {
        if (conns_.erase(cid) == 0) return;
        reacomodarSondeos();
        emit conexionesCambio();
        emit aparatosCambio();
        return;
    }
    json* a = buscarAparato(aparato);
    if (a == nullptr) {
        mandar(cid, {{"type", "auth"}, {"ok", false}, {"error", "Este aparato no está emparejado (o fue quitado)."}});
        emit cortar(QString::fromStdString(cid), "");
        return;
    }
    Conn& c = conns_[cid];
    c.aparato = aparato;
    c.ip = ip;
    c.lugar = lugar.empty() ? "sala" : lugar;
    (*a)["ultimo_uso_ms"] = ahoraMs();
    if (!ip.empty()) (*a)["ip"] = ip;
    guardar();
    auditar(aparato, "conectado desde " + (ip.empty() ? std::string("?") : ip) + (c.lugar == "afuera" ? " (afuera)" : ""));
    enviarAuth(cid);
    emit conexionesCambio();
    emit aparatosCambio();
}

void ControlWeb::enviarAuth(const std::string& cid) {
    const auto it = conns_.find(cid);
    if (it == conns_.end()) return;
    const json* a = buscarAparato(it->second.aparato);
    if (a == nullptr) return;
    const json perfil = a->value("perfil", json::array());
    json herramientas = json::array();
    json categorias = json::array();
    if (core_ != nullptr) {
        for (const mcp::Tool& t : core_->tools()) {
            if (!permite(perfil, t.category)) continue;
            herramientas.push_back({{"name", t.name},
                                    {"title", t.title},
                                    {"category", t.category},
                                    {"description", t.description},
                                    {"inputSchema", t.inputSchema},
                                    {"readOnly", t.readOnly},
                                    {"destructive", t.destructive},
                                    {"aire", core_->aire().clase(t.name) != mcp::ClaseAire::Normal}});
        }
        for (const auto& [id, titulo] : core_->categoryTitles())
            if (permite(perfil, id)) categorias.push_back({{"id", id}, {"titulo", titulo}});
    }
    // En tandas: el catálogo entero (con los esquemas) no entra en un solo mensaje.
    std::size_t i = 0;
    bool primero = true;
    do {
        json tanda = json::array();
        for (std::size_t n = 0; n < kTanda && i < herramientas.size(); ++n, ++i) tanda.push_back(std::move(herramientas[i]));
        const bool mas = i < herramientas.size();
        if (primero) {
            mandar(cid, {{"type", "auth"},
                         {"ok", true},
                         {"aparato", it->second.aparato},
                         {"nombre", a->value("nombre", std::string())},
                         {"perfil", perfil},
                         {"lugar", it->second.lugar},
                         {"preferencias", a->value("preferencias", json::object())},
                         {"categorias", categorias},
                         {"temas", [] {
                              json t = json::array();
                              for (const Tema& x : temas()) t.push_back(x.id);
                              return t;
                          }()},
                         {"herramientas", tanda},
                         {"mas", mas}});
            primero = false;
        } else {
            mandar(cid, {{"type", "herramientas"}, {"herramientas", tanda}, {"mas", mas}});
        }
    } while (i < herramientas.size());
}

void ControlWeb::entrada(const std::string& cid, const std::string& aparato, const json& msg) {
    if (!conns_.count(cid)) {
        // Una conexión de antes de que la app arrancara de nuevo (el gateway la sigue teniendo).
        if (aparato.empty() || buscarAparato(aparato) == nullptr) {
            emit cortar(QString::fromStdString(cid), "");
            return;
        }
        conexion(cid, aparato, true, "", "");
    }
    const std::string t = textoDe(msg, "type");
    if (t == "llamar") llamar(cid, msg);
    else if (t == "mirar") mirar(cid, msg);
    else if (t == "hola") enviarAuth(cid);
    else if (t == "preferencias") preferencias(cid, msg);
}

void ControlWeb::preferencias(const std::string& cid, const json& msg) {
    const json d = msg.contains("datos") ? msg["datos"] : json();
    json* a = buscarAparato(conns_[cid].aparato);
    if (a == nullptr || !d.is_object() || d.dump().size() > 8192) return;
    (*a)["preferencias"] = d;
    guardar();
}

void ControlWeb::llamar(const std::string& cid, const json& msg) {
    Conn& c = conns_[cid];
    const json id = msg.contains("id") ? msg["id"] : json();
    const auto responder = [this, cid, id](bool ok, const json& datosOError) {
        json r{{"type", "resultado"}, {"id", id}, {"ok", ok}};
        r[ok ? "datos" : "error"] = datosOError;
        mandar(cid, r);
    };
    const json* a = buscarAparato(c.aparato);
    if (a == nullptr || core_ == nullptr) return responder(false, "La app no está lista para recibir órdenes.");
    // El límite por aparato (un joystick que se vuelve loco no tapa a la app).
    const std::int64_t ahora = ahoraMs();
    while (!c.llamadas.empty() && ahora - c.llamadas.front() > 1000) c.llamadas.pop_front();
    if (static_cast<int>(c.llamadas.size()) >= kLlamadasPorSegundo())
        return responder(false, "Demasiadas órdenes por segundo: esperá un momento.");
    c.llamadas.push_back(ahora);

    const std::string pedida = textoDe(msg, "herramienta");
    const std::string h = core_->nombreActual(pedida);
    const json args = msg.contains("args") && msg["args"].is_object() ? msg["args"] : json::object();
    const mcp::Tool* tool = core_->findTool(h);
    if (tool == nullptr) {
        auditar(c.aparato, "pidió «" + pedida + "», que no existe");
        return responder(false, "No existe la herramienta «" + pedida + "».");
    }
    if (!permite(a->value("perfil", json::array()), tool->category)) {
        auditar(c.aparato, "RECHAZADA " + h + " (categoría «" + tool->category + "» fuera de su perfil)");
        return responder(false, "«" + h + "» (categoría «" + tool->category +
                                    "») no está en el perfil de este aparato. Lo amplía una persona desde la app.");
    }
    const bool confirmado = msg.value("confirmado", false);
    if (!confirmado && core_->hayAire() && core_->aire().esDeAire(h, args)) {
        auditar(c.aparato, "a confirmar (de aire, con aire) " + h + " " + mcp::Traza::resumir(args, 220));
        mandar(cid, {{"type", "confirmar"},
                     {"id", id},
                     {"herramienta", h},
                     {"texto", "«" + (tool->title.empty() ? h : tool->title) +
                                   "» cambia lo que sale y hay aire (transmisión o grabación). ¿Lo hacés igual?"}});
        return;
    }
    auditar(c.aparato, std::string(confirmado ? "llamó (confirmada) " : "llamó ") + h + " " + mcp::Traza::resumir(args, 220));
    // Una persona apretando, como la botonera: no pasa por la aprobación del
    // asistente ni por la guarda de aire (la confirmó en su pantalla).
    const mcp::CallContext ctx{"control-web:" + c.aparato, "", "control-web:" + c.aparato, true, {}};
    QPointer<ControlWeb> yo(this);
    core_->callTool({{"name", h}, {"arguments", args}},
                    [yo, cid, id, h](json r) {
                        if (!yo || !yo->conns_.count(cid)) return; // se fue mientras tanto
                        const bool ok = !r.value("isError", false);
                        const json d = datosDe(r.contains("structuredContent") ? r["structuredContent"] : json(),
                                               r.value("content", json::array()));
                        json m{{"type", "resultado"}, {"id", id}, {"ok", ok}};
                        m[ok ? "datos" : "error"] = d;
                        yo->mandar(cid, m);
                    },
                    ctx);
}

std::string ControlWeb::motivoTema(const json& perfil, const Tema& t) const {
    if (core_ == nullptr) return "la app no está lista";
    int hay = 0;
    for (const auto& h : t.herramientas) {
        const mcp::Tool* tool = core_->findTool(h);
        if (tool == nullptr) continue;
        ++hay;
        if (!permite(perfil, tool->category))
            return "necesita la categoría «" + tool->category + "», que no está en el perfil";
    }
    return hay == 0 ? "no está disponible en esta app" : std::string();
}

void ControlWeb::mirar(const std::string& cid, const json& msg) {
    auto it = conns_.find(cid);
    if (it == conns_.end()) return;
    Conn& c = it->second;
    const json* a = buscarAparato(c.aparato);
    const json perfil = a != nullptr ? a->value("perfil", json::array()) : json::array();
    std::set<std::string> nuevos;
    json rechazados = json::array();
    for (const auto& x : msg.value("temas", json::array())) {
        if (!x.is_string()) continue;
        const std::string id = x.get<std::string>();
        const auto t = std::find_if(temas().begin(), temas().end(), [&](const Tema& y) { return y.id == id; });
        const std::string motivo = t == temas().end() ? "no hay un tema «" + id + "»" : motivoTema(perfil, *t);
        if (motivo.empty()) nuevos.insert(id);
        else rechazados.push_back({{"tema", id}, {"error", motivo}});
    }
    std::set<std::string> agregados;
    std::set_difference(nuevos.begin(), nuevos.end(), c.temas.begin(), c.temas.end(),
                        std::inserter(agregados, agregados.begin()));
    c.temas = nuevos;
    json lista = json::array();
    for (const auto& x : nuevos) lista.push_back(x);
    mandar(cid, {{"type", "mirando"}, {"temas", lista}, {"rechazados", rechazados}});
    // Lo que ya se sabe de lo recién pedido, ya (sin esperar a que cambie). Lo
    // que nadie miraba arranca acá abajo y su primera consulta le llega a todos.
    for (const auto& t : agregados) {
        const auto s = sondeos_.find(t);
        if (s != sondeos_.end() && !s->second.ultimo.empty())
            mandar(cid, {{"type", "estado"}, {"tema", t}, {"datos", s->second.datos}});
    }
    reacomodarSondeos();
}

void ControlWeb::reacomodarSondeos() {
    std::set<std::string> mirados;
    for (const auto& [cid, c] : conns_) mirados.insert(c.temas.begin(), c.temas.end());
    for (auto it = sondeos_.begin(); it != sondeos_.end();) {
        if (mirados.count(it->first)) {
            ++it;
            continue;
        }
        const std::string tema = it->first;
        it = sondeos_.erase(it); // nadie lo mira: no se consulta más
        if (g_.mirando) g_.mirando(tema, false);
    }
    for (const auto& tema : mirados) {
        if (sondeos_.count(tema)) continue;
        const auto t = std::find_if(temas().begin(), temas().end(), [&](const Tema& y) { return y.id == tema; });
        if (t == temas().end()) continue;
        Sondeo& s = sondeos_[tema];
        s.timer = std::make_unique<QTimer>();
        s.timer->setInterval(t->periodoMs);
        connect(s.timer.get(), &QTimer::timeout, this, [this, tema]() { sondear(tema); });
        s.timer->start();
        if (g_.mirando) g_.mirando(tema, true);
        sondear(tema);
    }
}

void ControlWeb::sondear(const std::string& tema) {
    auto it = sondeos_.find(tema);
    if (it == sondeos_.end() || core_ == nullptr) return;
    Sondeo& s = it->second;
    const std::int64_t ahora = ahoraMs();
    if (s.enCurso && ahora - s.desdeMs < kSondeoTrabadoMs()) return; // la consulta anterior todavía no volvió
    const auto t = std::find_if(temas().begin(), temas().end(), [&](const Tema& y) { return y.id == tema; });
    if (t == temas().end()) return;
    std::vector<std::string> herramientas;
    for (const auto& h : t->herramientas)
        if (core_->findTool(h) != nullptr) herramientas.push_back(h);
    if (herramientas.empty()) return;
    s.enCurso = true;
    s.desdeMs = ahora;
    // Son lecturas fijas y sin argumentos: van directo a la herramienta, sin pasar
    // por el despachador (que dejaría una línea en el registro cada 66 ms).
    auto acumulado = std::make_shared<json>(json::object());
    // Cada paso se sostiene solo mientras una herramienta no contestó (la
    // respuesta guarda el paso); el lambda se ve a sí mismo por un puntero débil
    // para no quedar dando vueltas en un ciclo.
    auto paso = std::make_shared<std::function<void(std::size_t)>>();
    std::weak_ptr<std::function<void(std::size_t)>> debil = paso;
    QPointer<ControlWeb> yo(this);
    const std::int64_t desde = ahora;
    *paso = [yo, tema, herramientas, acumulado, debil, desde](std::size_t i) {
        if (!yo) return;
        auto si = yo->sondeos_.find(tema);
        if (si == yo->sondeos_.end() || si->second.desdeMs != desde) return; // nadie lo mira, o vuelta vieja
        if (i >= herramientas.size()) {
            Sondeo& s = si->second;
            s.enCurso = false;
            const json datos = herramientas.size() == 1 ? (*acumulado)[herramientas[0]] : *acumulado;
            std::string v = volcar(datos);
            if (v == s.ultimo) return; // no cambió: no se manda nada
            s.ultimo = std::move(v);
            s.datos = datos;
            QStringList a;
            for (const auto& [cid, c] : yo->conns_)
                if (c.temas.count(tema)) a << QString::fromStdString(cid);
            if (!a.isEmpty()) emit yo->enviar(a, json{{"type", "estado"}, {"tema", tema}, {"datos", datos}});
            return;
        }
        auto fuerte = debil.lock();
        if (!fuerte) return;
        const mcp::Tool* tool = yo->core_ != nullptr ? yo->core_->findTool(herramientas[i]) : nullptr;
        if (tool == nullptr || !tool->run) {
            (*fuerte)(i + 1);
            return;
        }
        const std::string h = herramientas[i];
        auto llamado = std::make_shared<bool>(false);
        try {
            tool->run(json::object(), [fuerte, acumulado, h, i, llamado](mcp::ToolResult r) {
                if (*llamado) return; // una sola respuesta
                *llamado = true;
                (*acumulado)[h] = r.isError ? json{{"error", datosDe(nullptr, r.content)}} : datosDe(r.structured, r.content);
                (*fuerte)(i + 1);
            });
        } catch (const std::exception& e) {
            if (!*llamado) {
                *llamado = true;
                (*acumulado)[h] = {{"error", std::string("la herramienta falló: ") + e.what()}};
                (*fuerte)(i + 1);
            }
        }
    };
    (*paso)(0);
}

// ============================================================================
// Interfaz
// ============================================================================

QVariantList ControlWeb::aparatosUi() const {
    QVariantList out;
    for (const auto& a : aparatos()) {
        QVariantMap m = aQv(a).toMap();
        m["perfil"] = QString::fromStdString(perfilTexto(a.value("perfil", json::array())));
        m["categorias"] = aQv(a.value("perfil", json::array()));
        out << m;
    }
    return out;
}

QVariantList ControlWeb::solicitudesUi() const {
    QVariantList out;
    for (const auto& s : solicitudes()) {
        QVariantMap m = aQv(s).toMap();
        m["perfil"] = QString::fromStdString(perfilTexto(s.value("perfil", json::array())));
        out << m;
    }
    return out;
}

QString ControlWeb::aprobarUi(const QString& sol, bool aprobar, const QString& categorias) {
    const json r = resolverSolicitud(sol.toStdString(), aprobar, categorias.trimmed().isEmpty() ? json() : perfilDeTexto(categorias), true);
    return QString::fromStdString(textoDe(r, "error"));
}

QString ControlWeb::perfilUi(const QString& aparato, const QString& categorias) {
    return QString::fromStdString(setPerfil(aparato.toStdString(), perfilDeTexto(categorias), true));
}

bool ControlWeb::quitarUi(const QString& aparato) { return quitar(aparato.toStdString()); }

} // namespace ba::app::control
