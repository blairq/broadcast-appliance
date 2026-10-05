// SPDX-License-Identifier: GPL-2.0-or-later
#include "legislatura/SesionRemota.hpp"

#include "legislatura/Cuerpo.hpp"

#include "qrcodegen.hpp"

#include <QCryptographicHash>
#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRandomGenerator>

#include <algorithm>
#include <cstdio>
#include <fstream>

namespace ba::app::legislatura {

using json = nlohmann::json;

namespace {

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
    return QJsonDocument::fromJson(QByteArray::fromStdString(json{{"v", j}}.dump())).object().value("v").toVariant();
}

std::string hora(std::int64_t ms) {
    return QDateTime::fromMSecsSinceEpoch(ms).toString("HH:mm:ss").toStdString();
}

} // namespace

SesionRemota::SesionRemota(const Cuerpo& cuerpo, std::filesystem::path datos, QObject* parent)
    : QObject(parent), cuerpo_(cuerpo), archivo_(datos / "Config" / "sesion-remota.json"),
      archivoRegistro_(datos / "Logs" / "sesion-remota.jsonl") {
    cargar();
}

void SesionRemota::cargar() {
    std::ifstream f(archivo_);
    const json j = json::parse(f, nullptr, false);
    if (!j.is_object()) return;
    if (j.value("credenciales", json()).is_object()) credenciales_ = j["credenciales"];
    graciaS_ = std::clamp(j.value("gracia_s", 120), 0, 3600);
}

void SesionRemota::guardar() const {
    std::error_code ec;
    std::filesystem::create_directories(archivo_.parent_path(), ec);
    const auto tmp = archivo_.string() + ".tmp";
    {
        std::ofstream f(tmp);
        f << json{{"credenciales", credenciales_}, {"gracia_s", graciaS_}}.dump(1);
    }
    std::filesystem::rename(tmp, archivo_, ec);
}

json SesionRemota::integrante(const std::string& id) const {
    for (const auto& i : cuerpo_.integrantes())
        if (i.value("id", std::string()) == id) return i;
    return json();
}

void SesionRemota::anotar(const std::string& que, const std::string& id, const json& extra) {
    json e{{"ms", ahoraMs()}, {"que", que}, {"id", id}};
    const json i = integrante(id);
    if (i.is_object()) {
        e["nombre"] = i.value("nombre", std::string());
        e["banca"] = i.value("banca", std::string());
    }
    for (auto it = extra.begin(); extra.is_object() && it != extra.end(); ++it) e[it.key()] = it.value();
    registro_.push_back(e);
    std::error_code ec;
    std::filesystem::create_directories(archivoRegistro_.parent_path(), ec);
    std::ofstream(archivoRegistro_, std::ios::app) << e.dump() << "\n";
}

// --- Credenciales ---------------------------------------------------------------------

json SesionRemota::generar(const std::string& id) {
    const json i = integrante(id);
    if (!i.is_object()) return {{"error", "no hay ningún integrante con ese id en el Cuerpo"}};
    const std::string token = hexAleatorio(32);
    // PIN de 6 dígitos parejo (bounded no tiene sesgo de módulo).
    const quint32 n = QRandomGenerator::system()->bounded(1000000u);
    char pin[8];
    std::snprintf(pin, sizeof pin, "%06u", n);
    const std::string sal = hexAleatorio(16);
    // El usuario y los datos se conservan; la contraseña no (iba con la sal vieja).
    const json antes = credenciales_.value(id, json::object());
    credenciales_[id] = {{"sal", sal},
                         {"hash_token", sha256(sal + token)},
                         {"hash_pin", sha256(sal + ":" + std::string(pin) + ":" + token)},
                         {"creado_ms", ahoraMs()}};
    if (antes.contains("usuario")) credenciales_[id]["usuario"] = antes["usuario"];
    if (antes.contains("datos")) credenciales_[id]["datos"] = antes["datos"];
    guardar();
    anotar("credencial", id);
    emit credencialesCambio();
    emit cambio();
    json out{{"id", id}, {"pin", pin}, {"nombre", i.value("nombre", std::string())},
             {"banca", i.value("banca", std::string())}};
    const std::string frag = "/sesion/#i=" + id + "&t=" + token;
    out["link"] = origen_.empty() ? frag : origen_ + frag;
    // El QR del link (sin el PIN: el PIN se dice aparte).
    try {
        const auto qr = qrcodegen::QrCode::encodeText(out["link"].get<std::string>().c_str(), qrcodegen::QrCode::Ecc::MEDIUM);
        json filas = json::array();
        for (int y = 0; y < qr.getSize(); ++y) {
            std::string f;
            for (int x = 0; x < qr.getSize(); ++x) f.push_back(qr.getModule(x, y) ? '1' : '0');
            filas.push_back(f);
        }
        out["qr"] = filas;
    } catch (...) {
    }
    if (origen_.empty()) out["aviso"] = "Falta el origen público (el gateway no contestó): completá el comienzo del link.";
    return out;
}

bool SesionRemota::revocar(const std::string& id) {
    if (!credenciales_.contains(id)) return false;
    credenciales_.erase(id);
    guardar();
    anotar("revocada", id);
    emit credencialesCambio();
    emit cambio();
    return true;
}

bool SesionRemota::tieneCredencial(const std::string& id) const { return credenciales_.contains(id); }

json SesionRemota::generarClave(const std::string& id, const std::string& usuarioPedido) {
    const json i = integrante(id);
    if (!i.is_object()) return {{"error", "no hay ningún integrante con ese id en el Cuerpo"}};
    if (!credenciales_.contains(id)) return {{"error", "primero generá su link y PIN (la contraseña va con esa credencial)"}};
    json& c = credenciales_[id];
    std::string usuario = usuarioPedido.empty() ? c.value("usuario", std::string()) : usuarioPedido;
    if (usuario.empty()) usuario = "banca" + i.value("banca", id);
    for (auto& ch : usuario) ch = static_cast<char>(::tolower(static_cast<unsigned char>(ch)));
    if (usuario.size() < 3 || usuario.size() > 40 ||
        usuario.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789._-") != std::string::npos)
        return {{"error", "el usuario va de 3 a 40 letras, números, punto, guion o guion bajo (sin espacios ni tildes)"}};
    for (auto it = credenciales_.begin(); it != credenciales_.end(); ++it)
        if (it.key() != id && it.value().value("usuario", std::string()) == usuario)
            return {{"error", "el usuario «" + usuario + "» ya lo tiene otro integrante"}};
    // 12 caracteres sin los que se confunden al dictarlos (0/o, 1/l/i).
    static const char* abc = "abcdefghjkmnpqrstuvwxyz23456789";
    std::string clave;
    for (int k = 0; k < 12; ++k) clave += abc[QRandomGenerator::system()->bounded(31u)];
    c["usuario"] = usuario;
    c["hash_clave"] = sha256(c.value("sal", std::string()) + ":clave:" + clave);
    guardar();
    anotar("clave", id, {{"usuario", usuario}});
    emit credencialesCambio();
    emit cambio();
    return {{"usuario", usuario}, {"clave", clave}, {"id", id}, {"nombre", i.value("nombre", std::string())},
            {"entrar", (origen_.empty() ? std::string() : origen_) + "/sesion/"}};
}

std::string SesionRemota::setDatos(const std::string& id, const json& datos) {
    if (!integrante(id).is_object()) return "no hay ningún integrante con ese id en el Cuerpo";
    if (!credenciales_.contains(id)) return "primero generá su link y PIN";
    json& c = credenciales_[id];
    for (const char* k : {"email", "telefono", "nota"})
        if (datos.contains(k) && datos[k].is_string()) {
            std::string v = datos[k].get<std::string>();
            if (v.size() > 200) v.resize(200);
            c["datos"][k] = v;
        }
    guardar();
    emit cambio();
    return {};
}

json SesionRemota::paraGateway() const {
    json out = json::array();
    for (auto it = credenciales_.begin(); it != credenciales_.end(); ++it) {
        const json i = integrante(it.key());
        if (!i.is_object()) continue; // si lo borraron del Cuerpo, su link deja de andar
        out.push_back({{"id", it.key()},
                       {"nombre", i.value("nombre", std::string())},
                       {"banca", i.value("banca", std::string())},
                       {"sal", it.value().value("sal", std::string())},
                       {"hash_token", it.value().value("hash_token", std::string())},
                       {"hash_pin", it.value().value("hash_pin", std::string())},
                       {"usuario", it.value().value("usuario", std::string())},
                       {"hash_clave", it.value().value("hash_clave", std::string())}});
    }
    return out;
}

void SesionRemota::setOrigen(const std::string& o) {
    if (o == origen_) return;
    origen_ = o;
    emit cambio();
}

// --- Estado ---------------------------------------------------------------------------

void SesionRemota::mandarEstado(const std::string& id) {
    const Estado& e = estado_[id];
    emit enviar(QString::fromStdString(id),
                json{{"type", "estado"}, {"estado", e.habilitado ? "habilitado" : "conectado"}});
}

void SesionRemota::evento(const json& ev) {
    const std::string tipo = ev.value("tipo", std::string());
    const std::string id = ev.value("id", std::string());
    if (id.empty()) return;
    Estado& e = estado_[id];
    if (tipo == "conexion") {
        const bool on = ev.value("conectado", false);
        if (on) {
            const bool dentroDeGracia = e.habilitado && e.cayoMs > 0 && ahoraMs() - e.cayoMs <= graciaS_ * 1000LL;
            if (!dentroDeGracia) e.habilitado = false;
            e.conectado = true;
            e.desdeMs = ahoraMs();
            e.cayoMs = 0;
            e.ip = ev.value("ip", std::string());
            e.lugar = ev.value("lugar", std::string());
            anotar("entro", id, {{"ip", e.ip}, {"lugar", e.lugar}, {"sigue_habilitado", e.habilitado}});
            mandarEstado(id);
            if (!e.habilitado) {
                const json i = integrante(id);
                emit aviso(QString::fromStdString(i.value("nombre", id) + " entró a la sesión y espera que lo habiliten"),
                           "info");
            }
        } else {
            e.conectado = false;
            e.cayoMs = ahoraMs();
            anotar("salio", id);
        }
    } else if (tipo == "intento") {
        anotar("intento_fallido", id, {{"ip", ev.value("ip", std::string())}, {"motivo", ev.value("motivo", std::string())}});
    } else if (tipo == "bloqueo") {
        e.bloqueadoHasta = ahoraMs() + ev.value("minutos", 15) * 60000LL;
        anotar("bloqueado", id, {{"minutos", ev.value("minutos", 15)}});
        const json i = integrante(id);
        emit aviso(QString::fromStdString("Acceso de " + i.value("nombre", id) +
                                          " bloqueado 15 minutos por PIN errado varias veces"),
                   "warning");
    } else if (tipo == "entrada") {
        // Palabra y voto: fases R2 y R4.
        return;
    }
    emit cambio();
}

std::string SesionRemota::habilitar(const std::string& id, bool si, const std::string& quien) {
    auto it = estado_.find(id);
    if (it == estado_.end() || !it->second.conectado) return "no está conectado a la sesión";
    if (it->second.habilitado == si) return {};
    it->second.habilitado = si;
    it->second.habilitadoPor = si ? quien : std::string();
    anotar(si ? "habilitado" : "deshabilitado", id, {{"por", quien}});
    mandarEstado(id);
    emit cambio();
    return {};
}

std::string SesionRemota::expulsar(const std::string& id, const std::string& quien, const std::string& motivo) {
    auto it = estado_.find(id);
    if (it == estado_.end() || !it->second.conectado) return "no está conectado a la sesión";
    it->second.habilitado = false;
    it->second.cayoMs = 0; // sin gracia: si vuelve, hay que habilitarlo de nuevo
    anotar("expulsado", id, {{"por", quien}, {"motivo", motivo}});
    emit cortar(QString::fromStdString(id), QString::fromStdString(motivo.empty() ? "La presidencia te desconectó." : motivo));
    emit cambio();
    return {};
}

json SesionRemota::participantes() const {
    json out = json::array();
    const std::int64_t ahora = ahoraMs();
    for (const auto& i : cuerpo_.integrantes()) {
        const std::string id = i.value("id", std::string());
        auto it = estado_.find(id);
        const bool cred = credenciales_.contains(id);
        if (!cred && it == estado_.end()) continue;
        json p{{"id", id},
               {"nombre", i.value("nombre", std::string())},
               {"banca", i.value("banca", std::string())},
               {"credencial", cred},
               {"conectado", false},
               {"estado", "sin_conectar"}};
        if (cred) {
            const json& c = credenciales_.at(id);
            p["usuario"] = c.value("usuario", std::string());
            p["con_clave"] = !c.value("hash_clave", std::string()).empty();
            p["datos"] = c.value("datos", json::object());
            p["creado_ms"] = c.value("creado_ms", std::int64_t(0));
        }
        if (it != estado_.end()) {
            const Estado& e = it->second;
            p["conectado"] = e.conectado;
            p["estado"] = !e.conectado ? "sin_conectar" : e.habilitado ? "habilitado" : "esperando";
            p["lugar"] = e.lugar;
            p["ip"] = e.ip;
            p["desde_ms"] = e.desdeMs;
            p["habilitado_por"] = e.habilitadoPor;
            p["bloqueado"] = e.bloqueadoHasta > ahora;
        }
        out.push_back(p);
    }
    return out;
}

int SesionRemota::presentes() const { return static_cast<int>(presentesIds().size()); }

int SesionRemota::conectados() const {
    int n = 0;
    for (const auto& [id, e] : estado_) n += e.conectado ? 1 : 0;
    return n;
}

std::vector<std::string> SesionRemota::presentesIds() const {
    std::vector<std::string> out;
    for (const auto& [id, e] : estado_)
        if (e.conectado && e.habilitado) out.push_back(id);
    return out;
}

std::vector<std::string> SesionRemota::presentesBancas() const {
    std::vector<std::string> out;
    for (const auto& id : presentesIds()) {
        const json i = integrante(id);
        if (i.is_object() && !i.value("banca", std::string()).empty()) out.push_back(i.value("banca", std::string()));
    }
    return out;
}

// --- Registro ---------------------------------------------------------------------------

std::string SesionRemota::anexoActa() const {
    if (registro_.empty()) return {};
    std::string s = "Sesión por la banca virtual (link personal y PIN)\n";
    static const std::map<std::string, std::string> que{
        {"entro", "entró"},           {"salio", "salió"},         {"habilitado", "habilitado"},
        {"deshabilitado", "deshabilitado"}, {"expulsado", "desconectado por la presidencia"},
        {"intento_fallido", "intento fallido"}, {"bloqueado", "bloqueado por PIN errado"},
        {"credencial", "link generado"}, {"revocada", "link revocado"}, {"clave", "contraseña generada"}};
    for (const auto& e : registro_) {
        const std::string q = e.value("que", std::string());
        auto it = que.find(q);
        s += "  " + hora(e.value("ms", std::int64_t(0))) + "  " + e.value("nombre", e.value("id", std::string()));
        if (!e.value("banca", std::string()).empty()) s += " (banca " + e.value("banca", std::string()) + ")";
        s += ": " + (it != que.end() ? it->second : q);
        if (!e.value("por", std::string()).empty()) s += " por " + e.value("por", std::string());
        if (q == "entro") s += std::string(" desde ") + (e.value("lugar", std::string()) == "sala" ? "la sala" : "afuera");
        s += "\n";
    }
    return s;
}

void SesionRemota::limpiarRegistro() {
    registro_ = json::array();
    emit cambio();
}

// --- Para la interfaz --------------------------------------------------------------------

QVariantList SesionRemota::participantesUi() const { return aQv(participantes()).toList(); }

QVariantMap SesionRemota::generarUi(const QString& id) { return aQv(generar(id.toStdString())).toMap(); }

bool SesionRemota::revocarUi(const QString& id) { return revocar(id.toStdString()); }

QVariantMap SesionRemota::generarClaveUi(const QString& id, const QString& usuario) {
    return aQv(generarClave(id.toStdString(), usuario.toStdString())).toMap();
}

QString SesionRemota::setDatosUi(const QString& id, const QVariantMap& datos) {
    const json d = json::parse(QJsonDocument(QJsonObject::fromVariantMap(datos)).toJson(QJsonDocument::Compact).toStdString(),
                               nullptr, false);
    return QString::fromStdString(setDatos(id.toStdString(), d.is_object() ? d : json::object()));
}

QString SesionRemota::habilitarUi(const QString& id, bool si) {
    return QString::fromStdString(habilitar(id.toStdString(), si, "operador"));
}

QString SesionRemota::expulsarUi(const QString& id, const QString& motivo) {
    return QString::fromStdString(expulsar(id.toStdString(), "operador", motivo.toStdString()));
}

void SesionRemota::pedirFoto(const QString& id) { emit fotoPedida(id); }

QString SesionRemota::fotoUrl(const QString& id) const {
    auto it = fotos_.find(id);
    return it == fotos_.end() ? QString() : "data:image/jpeg;base64," + QString::fromLatin1(it->second.toBase64());
}

void SesionRemota::ponerFoto(const QString& id, const QByteArray& jpeg) {
    if (jpeg.size() < 4 || static_cast<unsigned char>(jpeg[0]) != 0xFF) return;
    fotos_[id] = jpeg;
    emit fotoLista(id);
}

} // namespace ba::app::legislatura
