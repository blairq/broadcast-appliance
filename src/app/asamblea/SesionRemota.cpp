// SPDX-License-Identifier: GPL-2.0-or-later
#include "asamblea/SesionRemota.hpp"
#include "arranque/ArchivoConfig.hpp"
#include "i18n/I18n.hpp"

#include "asamblea/Cuerpo.hpp"
#include "asamblea/Votos.hpp"
#include "security/CredentialStore.hpp"

#include "qrcodegen.hpp"

#include <QCryptographicHash>
#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRandomGenerator>

#include <algorithm>
#include <cstdio>
#include <fstream>

namespace ba::app::asamblea {

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
    return QJsonDocument::fromJson(QByteArray::fromStdString(json{{"v", j}}.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace))).object().value("v").toVariant();
}

json conTipo(const char* tipo, const json& campos) {
    json o = campos.is_object() ? campos : json::object();
    o["type"] = tipo;
    return o;
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
    const json j = config::leerObjeto(archivo_);
    if (j.empty()) return;
    if (j.value("credenciales", json()).is_object()) credenciales_ = j["credenciales"];
    if (j.value("consolas", json()).is_object()) consolas_ = j["consolas"];
    graciaS_ = std::clamp(j.value("gracia_s", 120), 0, 3600);
    votoSegundos_ = std::clamp(j.value("voto_s", 60), 0, 3600);
    if (j.value("vistas", json()).is_array()) vistas_ = j["vistas"];
    pinEnQr_ = j.value("pin_en_qr", false);
    accesoFijo_ = j.value("acceso_fijo", false);
}

void SesionRemota::guardar() const {
    config::guardar(archivo_, {{"credenciales", credenciales_}, {"consolas", consolas_}, {"gracia_s", graciaS_},
                               {"voto_s", votoSegundos_}, {"vistas", vistas_}, {"pin_en_qr", pinEnQr_},
                               {"acceso_fijo", accesoFijo_}});
}

json SesionRemota::integrante(const std::string& id) const {
    for (const auto& i : cuerpo_.integrantes())
        if (i.value("id", std::string()) == id) return i;
    return json();
}

std::string SesionRemota::nombreDe(const std::string& id, const std::string& siNo) const {
    const json i = integrante(id);
    return i.is_object() ? i.value("nombre", siNo) : siNo;
}

std::string SesionRemota::bancaDe(const std::string& id) const {
    const json i = integrante(id);
    return i.is_object() ? i.value("banca", std::string()) : std::string();
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
    std::ofstream(archivoRegistro_, std::ios::app) << e.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace) << "\n";
}

// --- Credenciales ---------------------------------------------------------------------

std::string SesionRemota::nombreSecreto(const std::string& id) const {
    // Por configuración (cada una tiene su Cuerpo y sus ids) y con un nombre válido
    // para el almacén ([a-z0-9._-]) sea cual sea el id.
    return "asamblea-" + sha256(archivo_.string() + "|" + id).substr(0, 32);
}

bool SesionRemota::accesoGuardado(const std::string& id) const {
    return credenciales_.contains(id) && ba::security::CredentialStore::bundle().exists(nombreSecreto(id));
}

json SesionRemota::armarAcceso(const json& i, const std::string& token, const std::string& pin) const {
    json out{{"id", i.value("id", std::string())}, {"pin", pin}, {"nombre", i.value("nombre", std::string())},
             {"banca", i.value("banca", std::string())}, {"fijo", accesoFijo_}, {"pin_en_qr", pinEnQr_}};
    const std::string frag = "/sesion/#i=" + out["id"].get<std::string>() + "&t=" + token;
    ponerLinks(out, frag);
    // Con el PIN adentro, el link solo alcanza para entrar: es la credencial entera.
    if (pinEnQr_) out["link_con_pin"] = out["link"].get<std::string>() + "&p=" + pin;
    const std::string enQr = pinEnQr_ ? out["link_con_pin"].get<std::string>() : out["link"].get<std::string>();
    try {
        const auto qr = qrcodegen::QrCode::encodeText(enQr.c_str(), qrcodegen::QrCode::Ecc::MEDIUM);
        json filas = json::array();
        for (int y = 0; y < qr.getSize(); ++y) {
            std::string f;
            for (int x = 0; x < qr.getSize(); ++x) f.push_back(qr.getModule(x, y) ? '1' : '0');
            filas.push_back(f);
        }
        out["qr"] = filas;
    } catch (...) {
    }
    if (origen().empty())
        out["aviso"] = motivoOrigen_.empty()
                           ? ba::i18n::traducir("asamblea", "El servidor todavía no dio su dirección: completá el comienzo del link.")
                                             : motivoOrigen_;
    return out;
}

json SesionRemota::generar(const std::string& id, bool forzar) {
    const json i = integrante(id);
    if (!i.is_object()) return {{"error", ba::i18n::traducir("asamblea", "no hay ningún integrante con ese id en el Cuerpo")}};
    const auto almacen = ba::security::CredentialStore::bundle();
    // Acceso fijo: el que ya tiene, igual (mismo link, mismo PIN), salvo que se pida renovar.
    if (accesoFijo_ && !forzar && credenciales_.contains(id)) {
        std::string guardado;
        if (almacen.read(nombreSecreto(id), guardado)) {
            const json g = json::parse(guardado, nullptr, false);
            if (g.is_object() && g.value("token", std::string()).size() == 64) {
                json out = armarAcceso(i, g["token"].get<std::string>(), g.value("pin", std::string()));
                out["repetido"] = true;
                return out;
            }
        }
    }
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
    // Fijo: se guarda cifrado para poder mostrarlo de nuevo. Si no, no queda en ningún lado.
    if (accesoFijo_) almacen.write(nombreSecreto(id), json{{"token", token}, {"pin", pin}}.dump());
    else almacen.remove(nombreSecreto(id));
    guardar();
    anotar(forzar ? "credencial_renovada" : "credencial", id);
    emit credencialesCambio();
    emit cambio();
    return armarAcceso(i, token, pin);
}

void SesionRemota::setPinEnQr(bool si) {
    if (si == pinEnQr_) return;
    pinEnQr_ = si;
    guardar();
    emit cambio();
}

void SesionRemota::setAccesoFijo(bool si) {
    if (si == accesoFijo_) return;
    accesoFijo_ = si;
    // Apagarlo borra los guardados: desde ahí, se muestran una sola vez como antes
    // (los links que ya tienen siguen andando).
    if (!si) {
        const auto almacen = ba::security::CredentialStore::bundle();
        for (auto it = credenciales_.begin(); it != credenciales_.end(); ++it) almacen.remove(nombreSecreto(it.key()));
    }
    guardar();
    anotar(si ? "acceso_fijo" : "acceso_no_fijo", "");
    emit cambio();
}

bool SesionRemota::revocar(const std::string& id) {
    if (!credenciales_.contains(id)) return false;
    credenciales_.erase(id);
    ba::security::CredentialStore::bundle().remove(nombreSecreto(id));
    guardar();
    anotar("revocada", id);
    emit credencialesCambio();
    emit cambio();
    return true;
}

bool SesionRemota::tieneCredencial(const std::string& id) const { return credenciales_.contains(id); }

json SesionRemota::generarClave(const std::string& id, const std::string& usuarioPedido) {
    const json i = integrante(id);
    if (!i.is_object()) return {{"error", ba::i18n::traducir("asamblea", "no hay ningún integrante con ese id en el Cuerpo")}};
    if (!credenciales_.contains(id))
        return {{"error", ba::i18n::traducir("asamblea", "primero generá su link y PIN (la contraseña va con esa credencial)")}};
    json& c = credenciales_[id];
    std::string usuario = usuarioPedido.empty() ? c.value("usuario", std::string()) : usuarioPedido;
    if (usuario.empty()) usuario = "banca" + i.value("banca", id);
    for (auto& ch : usuario) ch = static_cast<char>(::tolower(static_cast<unsigned char>(ch)));
    if (usuario.size() < 3 || usuario.size() > 40 ||
        usuario.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789._-") != std::string::npos)
        return {{"error", ba::i18n::traducir("asamblea", "el usuario va de 3 a 40 letras, números, punto, guion o guion bajo (sin "
                                                         "espacios ni tildes)")}};
    for (auto it = credenciales_.begin(); it != credenciales_.end(); ++it)
        if (it.key() != id && it.value().value("usuario", std::string()) == usuario)
            return {{"error", ba::i18n::llenar(ba::i18n::traducir("asamblea", "el usuario «%1» ya lo tiene otro integrante"), {usuario})}};
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
            {"entrar", origen() + "/sesion/"}};
}

std::string SesionRemota::setDatos(const std::string& id, const json& datos) {
    if (!integrante(id).is_object()) return ba::i18n::traducir("asamblea", "no hay ningún integrante con ese id en el Cuerpo");
    if (!credenciales_.contains(id)) return ba::i18n::traducir("asamblea", "primero generá su link y PIN");
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
    // Las consolas de la presidencia: mismo esquema, id "consola:<id>".
    for (auto it = consolas_.begin(); it != consolas_.end(); ++it)
        out.push_back({{"id", "consola:" + it.key()},
                       {"nombre", it.value().value("nombre", std::string())},
                       {"sal", it.value().value("sal", std::string())},
                       {"hash_token", it.value().value("hash_token", std::string())},
                       {"hash_pin", it.value().value("hash_pin", std::string())}});
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

void SesionRemota::setOrigenes(const std::string& local, const std::string& remoto, const std::string& motivo) {
    if (local == origenLocal_ && remoto == origenRemoto_ && motivo == motivoOrigen_) return;
    origenLocal_ = local;
    origenRemoto_ = remoto;
    motivoOrigen_ = motivo;
    emit cambio();
}

void SesionRemota::ponerLinks(json& out, const std::string& frag) const {
    // El link de la red local primero (los de la sala); el de afuera, aparte y rotulado.
    out["link"] = origen().empty() ? frag : origen() + frag;
    if (!origenLocal_.empty() && !origenRemoto_.empty()) out["link_remoto"] = origenRemoto_ + frag;
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
    // La consola de la presidencia: no es un integrante.
    if (id.rfind("consola:", 0) == 0) {
        if (tipo == "conexion") {
            const bool on = ev.value("conectado", false);
            if (on) consolasConectadas_.insert(id);
            else consolasConectadas_.erase(id);
            anotar(on ? "consola_entro" : "consola_salio", "", {{"consola", id}, {"ip", ev.value("ip", std::string())}});
            emit consolaConexion(QString::fromStdString(id), on);
            emit cambio();
        } else if (tipo == "consola") {
            emit accionConsola(QString::fromStdString(id), ev.value("msg", json::object()));
        } else if (tipo == "intento" || tipo == "bloqueo") {
            anotar(tipo == "bloqueo" ? "consola_bloqueada" : "consola_intento", "", {{"consola", id}});
        }
        return;
    }
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
                emit aviso(tr("%1 entró a la sesión y espera que lo habiliten").arg(QString::fromStdString(nombreDe(id, id))),
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
        emit aviso(tr("Acceso de %1 bloqueado 15 minutos por PIN errado varias veces").arg(QString::fromStdString(nombreDe(id, id))),
                   "warning");
    } else if (tipo == "entrada") {
        const json m = ev.value("msg", json::object());
        const std::string t = m.value("type", std::string());
        if (t == "hola") {
            // Recién entró (o volvió): lo que tiene que saber.
            mandarEstado(id);
            if (!ultimaInfo_.empty()) emit enviar(QString::fromStdString(id), conTipo("info", ultimaInfo_));
            auto tu = turnos_.find(id);
            if (tu != turnos_.end()) emit enviar(QString::fromStdString(id), json{{"type", "fila"}, {"turno", tu->second}});
            if (vot_.estado == "abierta" || vot_.estado == "pausada") emit enviar(QString::fromStdString(id), mensajeVotacion(id));
            emit volvio(QString::fromStdString(id));
            // Tenía la palabra y se le cortó (o recargó): la sigue teniendo, con el
            // mismo reloj; el gateway le vuelve a pedir el micrófono y la cámara.
            for (const Palabra& pa : palabras_)
                if (pa.id == id) {
                    // Lo que le queda (el celular cuenta desde que le llega, con su reloj);
                    // sin límite (0) sigue sin límite.
                    const long long pasados = (ahoraMs() - pa.desdeMs) / 1000;
                    const int queda = pa.segundos > 0 ? static_cast<int>(std::max<long long>(1, pa.segundos - pasados)) : 0;
                    emit enviar(QString::fromStdString(id), json{{"type", "palabra"}, {"activa", true}, {"segundos", queda},
                                                                 {"desde_ms", pa.desdeMs}});
                }
            return;
        }
        if (t == "palabra" && e.habilitado && e.conectado) {
            const bool pedir = m.value("accion", std::string()) != "cancelar";
            anotar(pedir ? "pidio_palabra" : "cancelo_pedido", id);
            emit pedidoPalabra(QString::fromStdString(id), pedir);
            if (pedir) {
                emit aviso(tr("%1 (remoto) pidió la palabra").arg(QString::fromStdString(nombreDe(id, id))), "info");
            }
        }
        if (t == "voto" && e.habilitado && e.conectado) {
            const std::string op = m.value("opcion", std::string());
            bool valida = false;
            for (const auto& o : vot_.opciones) valida = valida || o.value("id", std::string()) == op;
            const bool vencida = vot_.hastaMs > 0 && ahoraMs() > vot_.hastaMs + 1500;
            if (vot_.estado != "abierta" || !valida || vencida || m.value("votacion", 0) != vot_.numero) {
                emit enviar(QString::fromStdString(id), json{{"type", "voto"}, {"ok", false},
                                                             // Lo lee el integrante en el celular: en el idioma del evento.
                                                             {"error", vot_.estado != "abierta"
                                                                           ? ba::i18n::traducirEvento("asamblea", "La votación no está abierta.")
                                                                       : vencida ? ba::i18n::traducirEvento("asamblea", "Se terminó el tiempo para votar.")
                                                                                 : ba::i18n::traducirEvento("asamblea", "Opción inválida.")}});
                return;
            }
            vot_.votos[id] = {op, ahoraMs()};
            anotar("voto", id, {{"votacion", vot_.numero}, {"opcion", op}});
            emit enviar(QString::fromStdString(id), json{{"type", "voto"}, {"ok", true}, {"opcion", op}});
            emit cambio();
        }
        return;
    }
    emit cambio();
}

std::string SesionRemota::habilitar(const std::string& id, bool si, const std::string& quien) {
    auto it = estado_.find(id);
    if (it == estado_.end() || !it->second.conectado) return ba::i18n::traducir("asamblea", "no está conectado a la sesión");
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
    if (it == estado_.end() || !it->second.conectado) return ba::i18n::traducir("asamblea", "no está conectado a la sesión");
    it->second.habilitado = false;
    it->second.cayoMs = 0; // sin gracia: si vuelve, hay que habilitarlo de nuevo
    anotar("expulsado", id, {{"por", quien}, {"motivo", motivo}});
    emit cortar(QString::fromStdString(id), QString::fromStdString(motivo.empty() ? ba::i18n::traducirEvento("asamblea", "La presidencia te desconectó.") : motivo));
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
            p["fijo"] = accesoFijo_ && accesoGuardado(id);
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
            p["palabra"] = tienePalabra(id);
            auto tu = turnos_.find(id);
            p["turno"] = tu != turnos_.end() ? tu->second : 0;
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

bool SesionRemota::presente(const std::string& integrante) const {
    const auto it = estado_.find(integrante);
    return it != estado_.end() && it->second.conectado && it->second.habilitado;
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

// --- La palabra ----------------------------------------------------------------------------

bool SesionRemota::tienePalabra(const std::string& id) const {
    return std::any_of(palabras_.begin(), palabras_.end(), [&](const Palabra& p) { return p.id == id; });
}

std::vector<std::string> SesionRemota::conPalabraTodos() const {
    std::vector<std::string> v;
    for (const Palabra& p : palabras_) v.push_back(p.id);
    return v;
}

void SesionRemota::emitirMedios() {
    QStringList l;
    for (const Palabra& p : palabras_) l << QString::fromStdString(p.id);
    emit mediosA(l);
}

void SesionRemota::darPalabra(const std::string& id, bool palabra, int segundos) {
    long long desde = ahoraMs();
    if (palabra) {
        auto it = std::find_if(palabras_.begin(), palabras_.end(), [&](const Palabra& p) { return p.id == id; });
        if (it != palabras_.end()) {
            it->segundos = segundos;
            it->desdeMs = desde;
        } else {
            // Hasta dos a la vez (los dos de un debate): un tercero saca al más viejo.
            if (palabras_.size() >= 2) {
                const std::string viejo = palabras_.front().id;
                palabras_.erase(palabras_.begin());
                anotar("fin_palabra", viejo);
                emit enviar(QString::fromStdString(viejo), json{{"type", "palabra"}, {"activa", false}, {"segundos", 0},
                                                                {"desde_ms", desde}});
            }
            palabras_.push_back({id, segundos, desde});
        }
        anotar("palabra", id, {{"segundos", segundos}});
    } else {
        const auto antes = palabras_.size();
        palabras_.erase(std::remove_if(palabras_.begin(), palabras_.end(), [&](const Palabra& p) { return p.id == id; }),
                        palabras_.end());
        if (palabras_.size() != antes) anotar("fin_palabra", id);
    }
    emit enviar(QString::fromStdString(id), json{{"type", "palabra"}, {"activa", palabra}, {"segundos", segundos},
                                                 {"desde_ms", desde}});
    // R3: su micrófono y su cámara entran a la app mientras tenga la palabra.
    emitirMedios();
    emit cambio();
}

void SesionRemota::fila(const std::vector<std::string>& cola, const std::string& alAire) {
    // A cada remoto conectado, su turno (0 = no está en la fila); solo si cambió.
    for (const auto& [id, e] : estado_) {
        if (!e.conectado) continue;
        int turno = 0;
        for (std::size_t k = 0; k < cola.size(); ++k)
            if (cola[k] == id) turno = static_cast<int>(k) + 1;
        auto it = turnos_.find(id);
        if (it != turnos_.end() && it->second == turno) continue;
        turnos_[id] = turno;
        emit enviar(QString::fromStdString(id), json{{"type", "fila"}, {"turno", turno}});
    }
    // A todos, quién habla.
    std::string hablando;
    if (!alAire.empty()) {
        const json i = integrante(alAire);
        // Lo ven los integrantes: en el idioma del evento.
        if (i.is_object())
            hablando = i.value("banca", std::string()).empty()
                           ? i.value("nombre", std::string())
                           : ba::i18n::llenar(ba::i18n::traducirEvento("asamblea", "%1 · Banca %2"),
                                              {i.value("nombre", std::string()), i.value("banca", std::string())});
    }
    info({{"hablando", hablando}});
}

std::string SesionRemota::setVistas(const json& vistas) {
    if (!vistas.is_array()) return ba::i18n::traducir("asamblea", "las vistas van en una lista");
    if (vistas.size() > 2) return ba::i18n::traducir("asamblea", "hasta dos canales (además de su propia cámara)");
    json limpio = json::array();
    const char* ids[] = {"a", "b"};
    for (size_t i = 0; i < vistas.size(); ++i) {
        const json& v = vistas[i];
        const std::string fuente = v.is_object() ? v.value("fuente", std::string()) : std::string();
        if (fuente.empty())
            return ba::i18n::traducir("asamblea", "a cada canal le falta la fuente: «programa» o el nombre de una escena");
        // Sin nombre se guarda vacío: vistas() le pone uno al darlas, en el idioma del evento.
        std::string nombre = v.value("nombre", std::string());
        if (nombre.size() > 40) nombre.resize(40);
        limpio.push_back({{"id", ids[i]}, {"nombre", nombre}, {"fuente", fuente}});
    }
    vistas_ = limpio;
    guardar();
    anotar("vistas", "", {{"vistas", vistas_}});
    emit cambio();
    return {};
}

QVariantList SesionRemota::vistasUi() const {
    json out = vistas_.empty() ? json::array({{{"id", "a"}, {"nombre", ""}, {"fuente", "programa"}}}) : vistas_;
    const json conNombre = vistas();
    for (size_t i = 0; i < out.size() && i < conNombre.size(); ++i) {
        out[i]["nombreAuto"] = conNombre[i].value("nombre", std::string());
        // El nombre por omisión de antes (se guardaba) cuenta como automático.
        if (out[i].value("fuente", std::string()) == "programa" && out[i].value("nombre", std::string()) == "Salida principal")
            out[i]["nombre"] = "";
    }
    return aQv(out).toList();
}

QString SesionRemota::setVistasUi(const QVariantList& vistas) {
    const json j = json::parse(QJsonDocument::fromVariant(vistas).toJson(QJsonDocument::Compact).toStdString(), nullptr, false);
    return QString::fromStdString(setVistas(j));
}

json SesionRemota::vistas() const {
    // Los nombres que ven los integrantes, en el idioma del evento: se rearman cada vez
    // (si cambia el idioma, la presidencia ve otra firma y se las vuelve a mandar).
    if (vistas_.empty())
        return json::array({{{"id", "a"}, {"nombre", ba::i18n::traducirEvento("asamblea", "Salida principal")}, {"fuente", "programa"}}});
    json out = vistas_;
    for (auto& v : out) {
        const std::string fuente = v.value("fuente", std::string());
        // «Salida principal» guardado = el nombre por omisión de antes (se guardaba).
        const std::string nombre = v.value("nombre", std::string());
        if (!nombre.empty() && !(fuente == "programa" && nombre == "Salida principal")) continue;
        v["nombre"] = fuente == "programa" ? ba::i18n::traducirEvento("asamblea", "Salida principal") : fuente;
    }
    return out;
}

void SesionRemota::ptt(const std::string& id, bool activo, const std::string& consola) {
    emit enviar(QString::fromStdString(id), json{{"type", "ptt"}, {"activo", activo}, {"consola", consola}});
    if (activo) anotar("ptt", id, {{"consola", consola}});
}

void SesionRemota::avisoTiempo(const std::string& id, const std::string& aviso) {
    emit enviar(QString::fromStdString(id), json{{"type", "tiempo"}, {"aviso", aviso}});
}

void SesionRemota::info(const json& campos) {
    bool cambio = false;
    for (auto it = campos.begin(); it != campos.end(); ++it)
        if (!ultimaInfo_.contains(it.key()) || ultimaInfo_[it.key()] != it.value()) {
            ultimaInfo_[it.key()] = it.value();
            cambio = true;
        }
    if (cambio) emit enviar("*", conTipo("info", campos));
}

// --- La consola de la presidencia ------------------------------------------------------------

json SesionRemota::generarConsola(const std::string& nombre) {
    // Sin origen el link sería «/presidencia/#…» (sin https ni dirección): un QR que no
    // abre nada. Mejor decirlo que entregar una credencial rota (y se ve una sola vez).
    if (origen().empty())
        return {{"error", motivoOrigen_.empty()
                              ? ba::i18n::traducir("asamblea", "El servidor todavía no dio su dirección: esperá unos segundos y "
                                                               "probá de nuevo.")
                              : ba::i18n::llenar(ba::i18n::traducir("asamblea", "Todavía no hay una dirección para el link: %1"),
                                                 {motivoOrigen_})}};
    const std::string id = hexAleatorio(4);
    const std::string token = hexAleatorio(32);
    const quint32 n = QRandomGenerator::system()->bounded(1000000u);
    char pin[8];
    std::snprintf(pin, sizeof pin, "%06u", n);
    const std::string sal = hexAleatorio(16);
    consolas_[id] = {{"nombre", nombre.empty() ? std::string("Presidencia") : nombre},
                     {"sal", sal},
                     {"hash_token", sha256(sal + token)},
                     {"hash_pin", sha256(sal + ":" + std::string(pin) + ":" + token)},
                     {"creado_ms", ahoraMs()}};
    guardar();
    anotar("consola_creada", "", {{"consola", id}, {"nombre", nombre}});
    emit credencialesCambio();
    emit cambio();
    const std::string frag = "/presidencia/#c=" + id + "&t=" + token;
    json out{{"id", id}, {"pin", pin}, {"nombre", consolas_[id]["nombre"]}};
    ponerLinks(out, frag);
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
    return out;
}

bool SesionRemota::revocarConsola(const std::string& id) {
    if (!consolas_.contains(id)) return false;
    consolas_.erase(id);
    guardar();
    emit credencialesCambio();
    emit cambio();
    return true;
}

json SesionRemota::consolas() const {
    json out = json::array();
    for (auto it = consolas_.begin(); it != consolas_.end(); ++it)
        out.push_back({{"id", it.key()}, {"nombre", it.value().value("nombre", std::string())},
                       {"conectada", consolasConectadas_.count("consola:" + it.key()) > 0},
                       {"creado_ms", it.value().value("creado_ms", std::int64_t(0))}});
    return out;
}

QVariantList SesionRemota::consolasUi() const { return aQv(consolas()).toList(); }

QVariantMap SesionRemota::generarConsolaUi(const QString& nombre) {
    return aQv(generarConsola(nombre.toStdString())).toMap();
}

// --- La votación ------------------------------------------------------------------------------

json SesionRemota::mensajeVotacion(const std::string& id) const {
    json m{{"type", "votacion"}, {"estado", vot_.estado}, {"numero", vot_.numero}, {"tema", vot_.tema},
           {"opciones", vot_.opciones}, {"hasta_ms", vot_.hastaMs}, {"ahora_ms", ahoraMs()}};
    auto it = vot_.votos.find(id);
    m["mi_voto"] = it != vot_.votos.end() ? it->second.first : std::string();
    return m;
}

void SesionRemota::mandarVotacion() {
    for (const auto& [id, e] : estado_)
        if (e.conectado) emit enviar(QString::fromStdString(id), mensajeVotacion(id));
}

std::string SesionRemota::abrirVotacion(int numero, const std::string& tema, const json& opciones, int segundos,
                                        const std::string& origen) {
    if (!opciones.is_array() || opciones.size() < 2) return ba::i18n::traducir("asamblea", "hacen falta al menos dos opciones");
    if (vot_.estado == "abierta" && vot_.numero == numero && vot_.origen == origen) return {}; // ya estaba
    vot_ = Votacion{};
    vot_.estado = "abierta";
    vot_.numero = numero;
    vot_.tema = tema;
    vot_.origen = origen;
    vot_.opciones = opciones;
    vot_.abiertaMs = ahoraMs();
    vot_.hastaMs = segundos > 0 ? vot_.abiertaMs + segundos * 1000LL : 0;
    anotar("votacion_abierta", "", {{"votacion", numero}, {"tema", tema}, {"origen", origen}});
    mandarVotacion();
    emit cambio();
    return {};
}

void SesionRemota::pausarVotacion(bool pausa) {
    if (vot_.estado != "abierta" && vot_.estado != "pausada") return;
    vot_.estado = pausa ? "pausada" : "abierta";
    mandarVotacion();
    emit cambio();
}

json SesionRemota::cerrarVotacion() {
    if (vot_.estado == "abierta" || vot_.estado == "pausada") {
        vot_.estado = "cerrada";
        anotar("votacion_cerrada", "", {{"votacion", vot_.numero}, {"votos_remotos", vot_.votos.size()}});
        mandarVotacion();
        emit cambio();
    }
    return votacion().value("votos", json::array());
}

json SesionRemota::votacion() const {
    json votos = json::array();
    json totales = json::object();
    for (const auto& o : vot_.opciones) totales[o.value("id", std::string())] = 0;
    for (const auto& [id, v] : vot_.votos) {
        votos.push_back({{"id", id}, {"nombre", nombreDe(id, id)}, {"banca", bancaDe(id)},
                         {"opcion", v.first}, {"ms", v.second}});
        totales[v.first] = totales.value(v.first, 0) + 1;
    }
    return {{"estado", vot_.estado}, {"numero", vot_.numero}, {"tema", vot_.tema}, {"opciones", vot_.opciones},
            {"hasta_ms", vot_.hastaMs}, {"origen", vot_.origen}, {"votos", votos}, {"totales", totales}};
}

json SesionRemota::resultadoCombinado(const json& nominalCentral) const {
    // Por banca: lo de la central primero, después lo remoto (que pisa si es del mismo integrante).
    std::map<std::string, json> porBanca;
    for (const auto& n : nominalCentral) {
        const std::string b = n.value("banca", std::string());
        if (b.empty()) continue;
        json v = n;
        v["origen"] = "central";
        porBanca[b] = v;
    }
    json observaciones = json::array();
    for (const auto& [id, v] : vot_.votos) {
        const std::string b = bancaDe(id);
        json nuevo{{"banca", b}, {"nombre", nombreDe(id, id)}, {"voto", v.first}, {"origen", "remoto"}};
        auto it = porBanca.find(b);
        if (it != porBanca.end() && it->second.value("voto", std::string()) != v.first)
            observaciones.push_back(ba::i18n::llenar(
                ba::i18n::traducirEvento("asamblea", "Banca %1: votó «%2» en la central y «%3» por el celular; vale el del "
                                                     "celular (el último)"),
                {b, etiquetaDeVoto(it->second.value("voto", std::string())), etiquetaDeVoto(v.first)}));
        porBanca[b.empty() ? "id:" + id : b] = nuevo;
    }
    json nominal = json::array();
    json totales = json::object();
    for (const auto& o : vot_.opciones) totales[o.value("id", std::string())] = 0;
    for (auto& [b, v] : porBanca) {
        nominal.push_back(v);
        const std::string op = v.value("voto", std::string());
        totales[op] = totales.value(op, 0) + 1;
    }
    return {{"nominal", nominal}, {"totales", totales}, {"observaciones", observaciones}};
}

void SesionRemota::mostrarResultado(const json& totales) {
    emit enviar("*", json{{"type", "resultado"}, {"numero", vot_.numero}, {"tema", vot_.tema}, {"totales", totales},
                          {"opciones", vot_.opciones}});
}

void SesionRemota::setProximos(const json& lista) { info({{"proximos", lista}}); }

void SesionRemota::setVotoSegundos(int s) {
    s = std::clamp(s, 0, 3600);
    if (s == votoSegundos_) return;
    votoSegundos_ = s;
    guardar();
    emit cambio();
}

json SesionRemota::opcionesDeMenu(const std::string& menu) {
    // Las opciones que ven los integrantes en el celular: la clave (id) se guarda y se
    // compara; el texto va en el idioma del evento y se arma al abrir cada votación.
    static const std::map<std::string, const char*> texto{{"afirmativo", QT_TRANSLATE_NOOP("asamblea", "Afirmativo")},
                                                          {"negativo", QT_TRANSLATE_NOOP("asamblea", "Negativo")},
                                                          {"abstencion", QT_TRANSLATE_NOOP("asamblea", "Abstención")},
                                                          {"no_participa", QT_TRANSLATE_NOOP("asamblea", "No participa")}};
    std::vector<std::string> ids{"afirmativo", "negativo"};
    if (menu == "si_no_abstencion" || menu.empty()) ids.push_back("abstencion");
    if (menu == "si_no_abstencion_nppv") ids = {"afirmativo", "negativo", "abstencion", "no_participa"};
    json out = json::array();
    for (const auto& id : ids) out.push_back({{"id", id}, {"texto", ba::i18n::traducirEvento("asamblea", texto.at(id))}});
    return out;
}

QVariantMap SesionRemota::votacionUi() const { return aQv(votacion()).toMap(); }

// --- Registro ---------------------------------------------------------------------------

std::string SesionRemota::anexoActa() const {
    // El acta es del evento: va en su idioma (se arma cada vez que se pide). Lo que
    // pasó se guarda como clave («entro», «voto»…) y al acta va su etiqueta.
    using ba::i18n::llenar;
    using ba::i18n::traducirEvento;
    if (registro_.empty()) return {};
    std::string s = traducirEvento("asamblea", "Sesión por la banca virtual (link personal y PIN)") + "\n";
    static const std::map<std::string, const char*> que{
        {"entro", QT_TRANSLATE_NOOP("asamblea", "entró")},
        {"salio", QT_TRANSLATE_NOOP("asamblea", "salió")},
        {"habilitado", QT_TRANSLATE_NOOP("asamblea", "habilitado")},
        {"deshabilitado", QT_TRANSLATE_NOOP("asamblea", "deshabilitado")},
        {"expulsado", QT_TRANSLATE_NOOP("asamblea", "desconectado por la presidencia")},
        {"intento_fallido", QT_TRANSLATE_NOOP("asamblea", "intento fallido")},
        {"bloqueado", QT_TRANSLATE_NOOP("asamblea", "bloqueado por PIN errado")},
        {"credencial", QT_TRANSLATE_NOOP("asamblea", "link generado")},
        {"revocada", QT_TRANSLATE_NOOP("asamblea", "link revocado")},
        {"clave", QT_TRANSLATE_NOOP("asamblea", "contraseña generada")},
        {"pidio_palabra", QT_TRANSLATE_NOOP("asamblea", "pidió la palabra")},
        {"cancelo_pedido", QT_TRANSLATE_NOOP("asamblea", "canceló su pedido")},
        {"palabra", QT_TRANSLATE_NOOP("asamblea", "tuvo la palabra")},
        {"fin_palabra", QT_TRANSLATE_NOOP("asamblea", "terminó de hablar")},
        {"voto", QT_TRANSLATE_NOOP("asamblea", "votó")},
        {"votacion_abierta", QT_TRANSLATE_NOOP("asamblea", "se abrió la votación")},
        {"votacion_cerrada", QT_TRANSLATE_NOOP("asamblea", "se cerró la votación")}};
    for (const auto& e : registro_) {
        const std::string q = e.value("que", std::string());
        if (q.rfind("votacion_", 0) == 0) {
            const std::string num = std::to_string(e.value("votacion", 0));
            const std::string tema = e.value("tema", std::string());
            const int remotos = e.value("votos_remotos", 0);
            std::string linea;
            if (q == "votacion_abierta")
                linea = tema.empty() ? llenar(traducirEvento("asamblea", "Votación %1: se abrió en los celulares"), {num})
                                     : llenar(traducirEvento("asamblea", "Votación %1 «%2»: se abrió en los celulares"), {num, tema});
            else
                linea = tema.empty()
                            ? llenar(traducirEvento("asamblea", "Votación %1: se cerró (%n voto remoto)", nullptr, remotos), {num})
                            : llenar(traducirEvento("asamblea", "Votación %1 «%2»: se cerró (%n voto remoto)", nullptr, remotos),
                                     {num, tema});
            s += "  " + hora(e.value("ms", std::int64_t(0))) + "  " + linea + "\n";
            continue;
        }
        auto it = que.find(q);
        s += "  " + hora(e.value("ms", std::int64_t(0))) + "  " + e.value("nombre", e.value("id", std::string()));
        if (!e.value("banca", std::string()).empty())
            s += llenar(traducirEvento("asamblea", " (banca %1)"), {e.value("banca", std::string())});
        s += ": " + (it != que.end() ? traducirEvento("asamblea", it->second) : q);
        if (!e.value("por", std::string()).empty()) s += llenar(traducirEvento("asamblea", " por %1"), {e.value("por", std::string())});
        if (q == "entro")
            s += e.value("lugar", std::string()) == "sala" ? traducirEvento("asamblea", " desde la sala")
                                                           : traducirEvento("asamblea", " desde afuera");
        if (q == "voto")
            s += llenar(traducirEvento("asamblea", " «%1» (votación %2)"),
                        {etiquetaDeVoto(e.value("opcion", std::string())), std::to_string(e.value("votacion", 0))});
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

QVariantMap SesionRemota::generarUi(const QString& id, bool forzar) { return aQv(generar(id.toStdString(), forzar)).toMap(); }

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

} // namespace ba::app::asamblea
