// ============================================================================
// src/gateway/Sesion.cpp — ver Sesion.hpp
// ============================================================================

#include "Sesion.hpp"
#include "Canal.hpp"
#include "Borde.hpp"
#include "security/Comparar.hpp"
#include "security/SecretsStore.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>

#include "HttpIo.hpp"
#include "Log.hpp"
#include "ajustes/Ajustes.hpp"
#include "i18n/I18n.hpp"

// Lo que se le manda a la página del integrante (error, motivo) no se traduce
// acá: se marca para el catálogo de la web y la página lo traduce en el idioma
// de su navegador (el valor que viaja es el original).
#ifndef QT_TRANSLATE_NOOP
#define QT_TRANSLATE_NOOP(scope, x) x
#endif

namespace ba::gateway {

using ba::i18n::llenar;
using ba::i18n::traducir;

using json = nlohmann::json;

namespace {

std::int64_t ahoraMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch())
        .count();
}

std::string minus(std::string s) {
    for (auto& c : s) c = static_cast<char>(::tolower(static_cast<unsigned char>(c)));
    return s;
}

inline int kMaxConexiones() { return ba::ajustes::entero("sesion.maxConexiones", 4096); }    // el límite técnico (docs/funciones/SESION-REMOTA.md §7)
inline int kMensajesPorSegundo() { return ba::ajustes::entero("sesion.mensajesPorSegundo", 20); }
inline int kLatidoMs() { return ba::ajustes::sAMs("sesion.latidoS", 30); }
inline int kAuthMs() { return ba::ajustes::sAMs("sesion.authS", 15); }
inline int kIntentosPorIp() { return ba::ajustes::entero("sesion.intentosPorIp", 10); }      // cada 10 minutos
inline int kFallasParaBloquear() { return ba::ajustes::entero("sesion.fallasParaBloquear", 5); }
inline std::int64_t kBloqueoMs() { return ba::ajustes::entero64("sesion.bloqueoMin", 15) * 60 * 1000; }
inline std::size_t kMaxFoto() { return static_cast<std::size_t>(ba::ajustes::entero("sesion.fotoKB", 96)) * 1024; }
// Cada cuánto, como mucho, se avisa que hay foto nueva de alguien (el celular manda una cada 1-3 s).
inline std::int64_t kAvisoFotoMs() { return ba::ajustes::entero64("sesion.avisoFotoMs", 2500); }
// Lo que la página puede mandar; lo demás se ignora. Las de la palabra y el voto
// se agregan en sus fases (R2, R4): la app decide si las acepta.
const std::set<std::string> kTipos{"hola", "palabra", "voto"};
// Lo que manda la consola de la presidencia (la app lo valida y lo hace).
const std::set<std::string> kTiposConsola{"hola",      "habilitar",  "deshabilitar", "expulsar", "dar_palabra",
                                          "terminar",  "quitar_fila", "sumar_tiempo", "siguiente", "votacion",
                                          "ptt",       "cast",        "foto_cuerpo",  "tiempo",
                                          "silenciar_todos", "hablar_presidencia", "debate"};

} // namespace

SesionBorde::SesionBorde(std::string webroot, Log& log, Evento alApp)
    : webroot_(std::move(webroot)), log_(log), alApp_(std::move(alApp)) {}

bool SesionBorde::origenValido(const std::string& headers) {
    // Puede venir por el túnel (Cloudflare): ahí también Origin tiene que ser el
    // Host que pidió. Sin Origin no es un navegador de otra página.
    return borde::mismoOrigen(headers);
}

bool SesionBorde::servir(long long fd, const std::string& rel, const std::string& webroot) {
    // CSP estricta; la cámara y el micrófono solo para esta página.
    return borde::servir(fd, webroot, rel,
                         "Cache-Control: no-cache\r\n"
                         "Content-Security-Policy: default-src 'self'; connect-src 'self' ws: wss:; "
                         "img-src 'self' data: blob:; media-src 'self' blob: mediastream:; "
                         "frame-ancestors 'none'; base-uri 'none'; form-action 'none'\r\n"
                         "Permissions-Policy: camera=(self), microphone=(self)\r\n"
                         "X-Content-Type-Options: nosniff\r\nReferrer-Policy: no-referrer\r\n");
}

bool SesionBorde::intentoPermitido(const std::string& ip) {
    std::lock_guard<std::mutex> l(mu_);
    auto& v = intentos_[ip];
    const std::int64_t ahora = ahoraMs();
    v.erase(std::remove_if(v.begin(), v.end(), [&](std::int64_t t) { return ahora - t > 10 * 60 * 1000; }), v.end());
    if (static_cast<int>(v.size()) >= kIntentosPorIp()) return false;
    v.push_back(ahora);
    return true;
}

int SesionBorde::conectados() const {
    std::lock_guard<std::mutex> g(mu_);
    int n = 0;
    for (const auto& [id, c] : conns_)
        if (c && id.rfind("consola:", 0) != 0) ++n;
    return n;
}

void SesionBorde::ponerCredenciales(const json& participantes) {
    std::lock_guard<std::mutex> l(mu_);
    std::map<std::string, Credencial> nuevas;
    for (const auto& p : participantes) {
        if (!p.is_object()) continue;
        const std::string id = p.value("id", std::string());
        if (id.empty() || p.value("hash_token", std::string()).size() != 64 ||
            p.value("hash_pin", std::string()).size() != 64)
            continue;
        Credencial c;
        c.nombre = p.value("nombre", std::string());
        c.banca = p.value("banca", std::string());
        c.sal = p.value("sal", std::string());
        c.hashToken = p.value("hash_token", std::string());
        c.hashPin = p.value("hash_pin", std::string());
        c.usuario = minus(p.value("usuario", std::string()));
        c.hashClave = p.value("hash_clave", std::string());
        c.prueba = p.value("prueba", false);
        for (const auto& a : p.value("aparatos", json::array()))
            if (a.is_object() && a.value("hash", std::string()).size() == 64 && !a.value("id", std::string()).empty())
                c.aparatos.push_back({a.value("id", std::string()), a.value("sal", std::string()), a.value("hash", std::string())});
        // Las fallas y el bloqueo sobreviven a que la app reenvíe la lista.
        auto it = credenciales_.find(id);
        if (it != credenciales_.end() && it->second.hashToken == c.hashToken) {
            c.fallas = it->second.fallas;
            c.bloqueadoHastaMs = it->second.bloqueadoHastaMs;
        }
        nuevas[id] = std::move(c);
    }
    credenciales_ = std::move(nuevas);
    credencialesListas_ = true;
}

bool SesionBorde::credencialDePrueba(const std::string& id, const std::string& usuario) const {
    std::lock_guard<std::mutex> l(mu_);
    if (!id.empty()) {
        const auto it = credenciales_.find(id);
        return it != credenciales_.end() && it->second.prueba;
    }
    const std::string u = minus(usuario);
    if (u.empty()) return false;
    for (const auto& [cid, c] : credenciales_)
        if (c.usuario == u) return c.prueba;
    return false;
}

std::string SesionBorde::lugarDePrueba(const std::string& id, const json& pedido, const std::string& real) const {
    const std::string pide = pedido.is_object() ? pedido.value("lugar_prueba", std::string()) : std::string();
    if ((pide != "sala" && pide != "afuera") || !credencialDePrueba(id)) return real;
    return pide;
}

bool SesionBorde::credencialesListas() const {
    std::lock_guard<std::mutex> l(mu_);
    return credencialesListas_;
}

bool SesionBorde::aparatoVale(const std::string& id, const std::string& aparato) const {
    if (aparato.empty()) return true;
    const auto it = credenciales_.find(id);
    if (it != credenciales_.end())
        for (const Aparato& a : it->second.aparatos)
            if (a.id == aparato) return true;
    for (const Pendiente& p : pendientes_)
        if (p.de == id && p.aparato.id == aparato) return true;
    return false;
}

std::string SesionBorde::autenticarAparato(const std::string& id, const std::string& token, const std::string& aparato) {
    if (aparato.size() != 64) return {};
    std::lock_guard<std::mutex> l(mu_);
    const auto it = credenciales_.find(id);
    if (it == credenciales_.end()) return {};
    // La URL tiene que ser la correcta: el token del link, si entró con link.
    if (!token.empty() && !ba::security::igualesEnTiempoConstante(borde::hashConSal(it->second.sal + token), it->second.hashToken))
        return {};
    const auto vale = [&](const Aparato& a) {
        return ba::security::igualesEnTiempoConstante(borde::hashConSal(a.sal + ":aparato:" + aparato), a.hash);
    };
    for (const Aparato& a : it->second.aparatos)
        if (vale(a)) return a.id;
    for (const Pendiente& p : pendientes_)
        if (p.de == id && vale(p.aparato)) return p.aparato.id;
    return {};
}

std::string SesionBorde::registrarAparato(const std::string& id, const std::string& nombre, std::string& aparatoId) {
    std::string token, sal;
    if (!ba::security::randomSecretHex(32, token) || !ba::security::randomSecretHex(8, sal) ||
        !ba::security::randomSecretHex(6, aparatoId))
        return {};
    Pendiente p;
    p.de = id;
    p.aparato = {aparatoId, sal, borde::hashConSal(sal + ":aparato:" + token)};
    p.nombre = nombre.substr(0, 80);
    p.creadoMs = ahoraMs();
    {
        std::lock_guard<std::mutex> l(mu_);
        if (pendientes_.size() >= 256) pendientes_.erase(pendientes_.begin()); // tope: A no viene a buscarlos
        pendientes_.push_back(std::move(p));
    }
    alApp_({{"tipo", "aparatos"}}); // que A los vaya a buscar (y si no está, al volver)
    return token;
}

json SesionBorde::aparatosPendientes() const {
    std::lock_guard<std::mutex> l(mu_);
    json out = json::array();
    for (const Pendiente& p : pendientes_)
        out.push_back({{"de", p.de},
                       {"id", p.aparato.id},
                       {"sal", p.aparato.sal},
                       {"hash", p.aparato.hash},
                       {"nombre", p.nombre},
                       {"creado_ms", p.creadoMs}});
    return out;
}

void SesionBorde::aparatosRecibidos(const json& ids) {
    if (!ids.is_array()) return;
    std::lock_guard<std::mutex> l(mu_);
    std::erase_if(pendientes_, [&](const Pendiente& p) {
        return std::find(ids.begin(), ids.end(), json(p.aparato.id)) != ids.end();
    });
}

bool SesionBorde::bloqueado(const std::string& id) const {
    std::lock_guard<std::mutex> l(mu_);
    auto it = credenciales_.find(id);
    return it != credenciales_.end() && it->second.bloqueadoHastaMs > ahoraMs();
}

void SesionBorde::anotarPinMal(const std::string& id) {
    bool seBloqueo = false;
    {
        std::lock_guard<std::mutex> l(mu_);
        auto it = credenciales_.find(id);
        if (it == credenciales_.end()) return;
        if (++it->second.fallas >= kFallasParaBloquear()) {
            it->second.fallas = 0;
            it->second.bloqueadoHastaMs = ahoraMs() + kBloqueoMs();
            seBloqueo = true;
        }
    }
    if (seBloqueo) {
        log_.warn(llenar(traducir("gateway", "sesión: %1 bloqueado 15 min por PIN errado"), {id}), -1);
        alApp_({{"tipo", "bloqueo"}, {"id", id}, {"minutos", kBloqueoMs() / 60000}});
    }
}

std::string SesionBorde::autenticar(const std::string& id, const std::string& token, const std::string& pin,
                                    bool& pinMal) {
    pinMal = false;
    if (token.size() != 64 || pin.empty() || pin.size() > 12) return {};
    std::lock_guard<std::mutex> l(mu_);
    auto it = credenciales_.find(id);
    if (it == credenciales_.end()) return {};
    const Credencial& c = it->second;
    // El token primero: sin él no se cuenta como PIN errado (no se puede
    // bloquear a alguien solo sabiendo su id).
    if (!ba::security::igualesEnTiempoConstante(borde::hashConSal(c.sal + token), c.hashToken)) return {};
    if (!ba::security::igualesEnTiempoConstante(borde::hashConSal(c.sal + ":" + pin + ":" + token), c.hashPin)) {
        pinMal = true;
        return {};
    }
    return id;
}

std::string SesionBorde::autenticarUsuario(const std::string& usuario, const std::string& clave, std::string& id,
                                           bool& claveMal) {
    claveMal = false;
    id.clear();
    if (usuario.empty() || usuario.size() > 40 || clave.empty() || clave.size() > 64) return {};
    const std::string u = minus(usuario);
    std::lock_guard<std::mutex> l(mu_);
    for (const auto& [cid, c] : credenciales_)
        if (!c.usuario.empty() && c.usuario == u && c.hashClave.size() == 64) {
            id = cid;
            if (ba::security::igualesEnTiempoConstante(borde::hashConSal(c.sal + ":clave:" + clave), c.hashClave)) return cid;
            claveMal = true;
            return {};
        }
    return {};
}

bool SesionBorde::atender(long long fd, const std::string& method, const std::string& path, const std::string& query,
                          const std::string& headers, std::uint32_t clientIp, bool externo) {
    const auto q = http::parseQuery(query);
    const bool wsConsola = path == "/ws" && q.count("presidencia") > 0;
    const bool ws = (path == "/ws" && q.count("sesion") > 0) || wsConsola;
    const bool pagina = path == "/sesion" || path.rfind("/sesion/", 0) == 0;
    const bool paginaConsola = path == "/presidencia" || path.rfind("/presidencia/", 0) == 0;
    if (!ws && !pagina && !paginaConsola) return false;
    // La IP y «por un túnel» ya vienen resueltas (IpCliente.hpp): acá no se lee
    // ninguna cabecera de IP. En la sala (red local) o afuera (túnel o IP pública): la página lo
    // usa para no reproducir la sala en un celular que está adentro (acoplaría).
    const std::string ip = borde::ipTexto(clientIp);
    const std::string lugar = !externo && borde::ipLocal(clientIp) ? "sala" : "afuera";
    if (!origenValido(headers)) {
        log_.warn(llenar(traducir("gateway", "sesión: origen ajeno desde %1"), {ip}), -1);
        http::sendResponse(fd, 403, "text/plain", traducir("gateway", "origen inválido") + "\n");
        return true;
    }
    if (pagina || paginaConsola) {
        if (method != "GET") return http::sendResponse(fd, 405, "text/plain", "\n"), true;
        const std::string base = pagina ? "/sesion" : "/presidencia";
        if (path == base) return http::sendResponse(fd, 301, "text/plain", "", "Location: " + base + "/\r\n"), true;
        std::string rel = path.substr(base.size() + 1);
        if (rel.empty()) rel = "index.html";
        // La consola de la presidencia vive al lado: Web/presidencia.
        return servir(fd, rel, paginaConsola ? (std::filesystem::path(webroot_).parent_path() / "presidencia").string()
                                             : webroot_);
    }
    {
        std::lock_guard<std::mutex> l(mu_);
        if (static_cast<int>(conns_.size()) >= kMaxConexiones())
            return http::sendResponse(fd, 503, "text/plain", traducir("gateway", "sesión llena") + "\n"), true;
    }
    if (!canal::aceptar(fd, headers)) return true;
    wsLoop(fd, ip, lugar, wsConsola);
    return true;
}

void SesionBorde::wsLoop(long long fd, const std::string& ip, const std::string& lugar, bool consola) {
    auto c = std::make_shared<Conn>();
    c->ip = ip;
    c->lugar = lugar;
    // 1. Autenticación: el primer mensaje (docs/arquitectura/06-canal-web.md), a los 15 s como mucho.
    json pedido;
    {
        const json m = canal::primerMensaje(fd, kAuthMs());
        if (m.is_null()) return;
        const auto rechazar = [&](const std::string& error, const std::string& codigo = {}) { canal::rechazar(fd, error, codigo); };
        pedido = m;
        // La consola de la presidencia tiene su propia credencial (id "consola:<x>"); un
        // integrante nunca entra por la consola ni la consola por la página del integrante.
        const bool conUsuario = !consola && m.contains("u");
        std::string id = consola ? "consola:" + m.value("c", std::string()) : m.value("i", std::string());
        if (!consola && id.rfind("consola:", 0) == 0) return rechazar(QT_TRANSLATE_NOOP("web/comun", "Mensaje inválido."));
        // Una sala recién arrancada sin las credenciales de A todavía: «esperá», no «mal»
        // (la página reintenta sola y no se cuenta como intento).
        if (!credencialesListas())
            return rechazar(QT_TRANSLATE_NOOP("web/comun", "Conectando con la sala…"), "esperar");
        // Un aparato registrado (ver Sesion.hpp): con el link correcto, entra sin el PIN.
        if (const std::string aparato = m.value("aparato", std::string()); !aparato.empty()) {
            std::string deQuien = conUsuario ? std::string() : id;
            if (conUsuario) {
                const std::string u = minus(m.value("u", std::string()));
                std::lock_guard<std::mutex> l(mu_);
                for (const auto& [cid, cr] : credenciales_)
                    if (!cr.usuario.empty() && cr.usuario == u) deQuien = cid;
            }
            const std::string cual =
                deQuien.empty() || bloqueado(deQuien)
                    ? std::string()
                    : autenticarAparato(deQuien, conUsuario ? std::string() : m.value("t", std::string()), aparato);
            if (cual.empty()) {
                intentoPermitido(ip); // cuenta como intento: no se puede probar sin fin
                alApp_({{"tipo", "intento"}, {"id", deQuien}, {"ip", ip}, {"motivo", "aparato"}});
                return rechazar(QT_TRANSLATE_NOOP("web/comun", "Este aparato ya no está registrado: entrá con el PIN."),
                                "aparato");
            }
            c->id = deQuien;
            c->aparato = cual;
        } else {
            // Los integrantes de prueba salen todos de la máquina del probador: no cuentan
            // para el tope de intentos por IP (el bloqueo por PIN errado sí les vale).
            const bool dePrueba = conUsuario ? credencialDePrueba({}, m.value("u", std::string())) : credencialDePrueba(id);
            if (!dePrueba && !intentoPermitido(ip)) {
                log_.warn(llenar(traducir("gateway", "sesión: demasiados intentos desde %1"), {ip}), -1);
                return rechazar(QT_TRANSLATE_NOOP("web/comun", "Demasiados intentos desde esta conexión. Esperá 10 minutos."));
            }
            bool pinMal = false;
            std::string quien;
            if (conUsuario) {
                quien = autenticarUsuario(m.value("u", std::string()), m.value("clave", std::string()), id, pinMal);
                if (!id.empty() && bloqueado(id))
                    return rechazar(
                        QT_TRANSLATE_NOOP("web/comun", "Por contraseña errada varias veces, tu acceso está bloqueado 15 minutos."));
                if (quien.empty() && !pinMal) {
                    alApp_({{"tipo", "intento"}, {"id", ""}, {"ip", ip}, {"motivo", "usuario"}});
                    return rechazar(QT_TRANSLATE_NOOP("web/comun", "Usuario o contraseña incorrectos."));
                }
            } else {
                if (bloqueado(id))
                    return rechazar(QT_TRANSLATE_NOOP("web/comun", "Por PIN errado varias veces, tu acceso está bloqueado 15 minutos."));
                quien = autenticar(id, m.value("t", std::string()), m.value("pin", std::string()), pinMal);
            }
            if (quien.empty()) {
                if (pinMal) {
                    anotarPinMal(id);
                    alApp_({{"tipo", "intento"}, {"id", id}, {"ip", ip}, {"motivo", "pin"}});
                    return rechazar(conUsuario ? QT_TRANSLATE_NOOP("web/comun", "Usuario o contraseña incorrectos.")
                                               : QT_TRANSLATE_NOOP("web/comun", "El PIN no es correcto."));
                }
                alApp_({{"tipo", "intento"}, {"id", id}, {"ip", ip}, {"motivo", "link"}});
                return rechazar(
                    QT_TRANSLATE_NOOP("web/comun", "Este link no es válido (o fue revocado). Pedile uno nuevo a la secretaría."));
            }
            c->id = quien;
        }
    }
    c->fd = fd;
    // Un integrante de prueba puede decir dónde simula estar (ver Sesion.hpp).
    if (!consola) c->lugar = lugarDePrueba(c->id, pedido, c->lugar);
    canal::anotar(c);
    // Entró con el PIN y la página pide registrar el aparato: el token, una sola vez.
    std::string tokenAparato;
    if (c->aparato.empty() && pedido.value("registrar", false))
        tokenAparato = registrarAparato(c->id, pedido.value("nombre", std::string()), c->aparato);
    std::shared_ptr<Conn> vieja;
    json datos;
    {
        std::lock_guard<std::mutex> l(mu_);
        auto it = conns_.find(c->id);
        if (it != conns_.end()) vieja = it->second;
        conns_[c->id] = c;
        auto cr = credenciales_.find(c->id);
        if (cr != credenciales_.end()) {
            cr->second.fallas = 0;
            datos = {{"nombre", cr->second.nombre}, {"banca", cr->second.banca}};
        }
    }
    if (vieja) { // un integrante, una conexión: la nueva corta la vieja
        vieja->enviar({{"type", "cortado"},
                       {"motivo", QT_TRANSLATE_NOOP("web/comun", "Entraste desde otro lado: esta ventana se cerró.")}});
        vieja->cortar();
    }
    int canalMedios = -1;
    {
        std::lock_guard<std::mutex> l(mu_);
        if (!consola)
            if (auto it = mediosDe_.find(c->id); it != mediosDe_.end()) canalMedios = it->second;
    }
    const bool tieneMedios = canalMedios >= 0;
    // Si tenía la palabra, su WebRTC hacia la sala era de la conexión vieja (las
    // respuestas iban a ella): se cierra para que la oferta nueva arranque limpia.
    if (tieneMedios && vieja && medios_.cerrar) medios_.cerrar(c->id, canalMedios);
    json bien{{"type", "auth"}, {"ok", true}, {"id", c->id}, {"nombre", datos.value("nombre", std::string())},
              {"banca", datos.value("banca", std::string())}, {"lugar", c->lugar}};
    if (!tokenAparato.empty()) bien["aparato"] = tokenAparato;
    c->enviar(bien);
    log_.info(llenar(traducir("gateway", "sesión: %1 conectado desde %2"), {c->id, c->ip}));
    alApp_({{"tipo", "conexion"}, {"id", c->id}, {"conectado", true}, {"ip", c->ip}, {"lugar", c->lugar},
            {"aparato", c->aparato}});
    // Volvió el que tiene la palabra (se le cortó la red, recargó la página): se le
    // vuelve a pedir el micrófono y la cámara.
    if (tieneMedios) c->enviar({{"type", "medios"}, {"activo", true}, {"previo", canalMedios == kCanalSiguiente}});
    if (!consola) {
        json lista;
        {
            std::lock_guard<std::mutex> l(mu_);
            lista = listaVistas();
        }
        c->enviar({{"type", "vistas"}, {"lista", lista}});
        // Su vista de la conexión vieja (si la había) era de esa página: la nueva ofrece otra.
        if (vieja && medios_.vistaCerrar) medios_.vistaCerrar(c->id);
        // Lo estaba atendiendo el asistente hablando (recargó la página, se le cortó
        // la red): el WebRTC de la página vieja no sigue, y a la nueva se le vuelve a pedir.
        bool atendido = false;
        {
            std::lock_guard<std::mutex> l(mu_);
            atendido = atiende_ == c->id;
        }
        if (atendido) {
            if (vieja && medios_.atiendeCerrar) medios_.atiendeCerrar(c->id);
            c->enviar({{"type", "atiende"}, {"on", true}});
        }
    }
    // 2. El lazo (la consola junta candidatos ICE de varias conexiones a la vez: más margen).
    const canal::Fin fin = canal::leer(*c, {kLatidoMs(), kMaxFoto() * 2, consola ? kMensajesPorSegundo() * 4 : kMensajesPorSegundo()},
                                       [&](const json& m) {
        const std::string t = m.value("type", std::string());
        // R3: el que tiene la palabra manda su micrófono y su cámara a la sala.
        if (!consola && (t == "medios-oferta" || t == "medios-ice")) {
            int canal = -1;
            {
                std::lock_guard<std::mutex> l(mu_);
                if (auto it = mediosDe_.find(c->id); it != mediosDe_.end()) canal = it->second;
            }
            if (canal < 0 || !medios_.oferta) return true;
            if (t == "medios-oferta") {
                std::weak_ptr<Conn> w = c;
                const bool ok = medios_.oferta(c->id, canal, m.value("sdp", std::string()), [this, w](const json& r) {
                    if (auto cc = w.lock()) cc->enviar(r);
                }, m.contains("capas") ? m["capas"] : json());
                if (!ok)
                    c->enviar({{"type", "medios"},
                               {"activo", false},
                               {"error", QT_TRANSLATE_NOOP("web/comun", "no hay lugar para el audio en la sala")}});
            } else if (medios_.ice) {
                const json& cand = m.contains("candidate") ? m["candidate"] : json();
                medios_.ice(c->id, cand.is_string() ? cand.get<std::string>() : cand.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace));
            }
            return true;
        }
        // Lo que mira en su celular: un WebRTC que solo recibe, y el canal que elige.
        if (!consola && (t == "vista-oferta" || t == "vista-ice" || t == "vista")) {
            if (t == "vista-ice") {
                const json& cand = m.contains("candidate") ? m["candidate"] : json();
                if (medios_.vistaIce) medios_.vistaIce(c->id, cand.is_string() ? cand.get<std::string>() : cand.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace));
                return true;
            }
            Vista vista;
            bool habla;
            {
                std::lock_guard<std::mutex> l(mu_);
                std::string v = m.value("vista", std::string("yo"));
                if (v.size() > 48) v = "yo"; // «salida:» y el id de una salida entran
                vistaDe_[c->id] = v;
                vista = vistaElegida(v);
                habla = hablando_[c->id];
            }
            if (t == "vista") {
                if (medios_.vistaRuta) medios_.vistaRuta(c->id, vista, habla);
            } else if (medios_.vistaOferta) {
                std::weak_ptr<Conn> w = c;
                const bool ok = medios_.vistaOferta(c->id, m.value("sdp", std::string()), vista, habla, [this, w](const json& r) {
                    if (auto cc = w.lock()) cc->enviar(r);
                });
                if (!ok)
                    c->enviar({{"type", "vista-error"},
                               {"error", QT_TRANSLATE_NOOP("web/comun", "La sala no tiene lugar para mandarte la imagen ahora.")}});
            }
            return true;
        }
        // La atención hablada mientras espera: su micrófono, solo audio, solo si la app
        // la prendió para él (lo demás se ignora: nadie se abre un canal solo).
        if (!consola && (t == "atiende-oferta" || t == "atiende-ice" || t == "atiende-error")) {
            bool atendido = false;
            {
                std::lock_guard<std::mutex> l(mu_);
                atendido = atiende_ == c->id;
            }
            if (!atendido) return true;
            const auto noSePudo = [&](const std::string& error) {
                c->enviar({{"type", "atiende-fallo"}, {"error", error}});
                alApp_({{"tipo", "atiende_error"}, {"id", c->id}, {"error", error}});
            };
            if (t == "atiende-error") {
                // Su página no pudo (no dio permiso para el micrófono, no tiene): la app
                // sigue sin la voz con él.
                std::string e = m.value("error", std::string());
                if (e.size() > 200) e.resize(200);
                alApp_({{"tipo", "atiende_error"}, {"id", c->id}, {"error", e}});
            } else if (t == "atiende-ice") {
                const json& cand = m.contains("candidate") ? m["candidate"] : json();
                if (medios_.atiendeIce)
                    medios_.atiendeIce(c->id, cand.is_string() ? cand.get<std::string>()
                                                               : cand.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace));
            } else {
                const std::string sdp = m.value("sdp", std::string());
                if (!subeSoloAudio(sdp)) {
                    log_.warn(llenar(traducir("gateway", "sesión: la atención de %1 quiso mandar algo más que audio; se ignora"),
                                     {c->id}),
                              -1);
                    noSePudo(QT_TRANSLATE_NOOP("web/comun", "La página ofreció algo más que el micrófono."));
                    return true;
                }
                std::weak_ptr<Conn> w = c;
                if (!medios_.atiendeOferta ||
                    !medios_.atiendeOferta(c->id, sdp, [this, w](const json& r) {
                        if (auto cc = w.lock()) cc->enviar(r);
                    }))
                    noSePudo(QT_TRANSLATE_NOOP("web/comun", "La sala no tiene lugar para escucharte ahora."));
            }
            return true;
        }
        if (t == "rtc") {
            // Señalización WebRTC directa entre la consola de la presidencia y un
            // integrante (ver la cara del que habla, hablarle «apretar para hablar»).
            // El gateway solo la pasa: un integrante solo le puede escribir a una
            // consola, y una consola solo a un integrante.
            const std::string a = m.value("a", std::string());
            const bool aConsola = a.rfind("consola:", 0) == 0;
            if (!a.empty() && aConsola != consola) {
                json r = m;
                r.erase("a");
                r["de"] = c->id;
                std::shared_ptr<Conn> dest;
                {
                    std::lock_guard<std::mutex> l(mu_);
                    auto it = conns_.find(a);
                    if (it != conns_.end()) dest = it->second;
                }
                if (dest) dest->enviar(r);
            }
            return true;
        }
        if (consola) {
            if (t == "foto") {
                // La cara de un integrante conectado (la última foto de su cámara).
                const std::string quien = m.value("id", std::string());
                std::string jpeg;
                {
                    std::lock_guard<std::mutex> l(mu_);
                    auto it = fotos_.find(quien);
                    if (it != fotos_.end()) jpeg = it->second;
                }
                if (!jpeg.empty())
                    c->enviar({{"type", "foto"}, {"id", quien},
                               {"jpeg", http::base64(reinterpret_cast<const uint8_t*>(jpeg.data()), jpeg.size())}});
                return true;
            }
            // Lo demás lo decide la app (habilitar, dar la palabra, votar…).
            if (kTiposConsola.count(t)) alApp_({{"tipo", "consola"}, {"id", c->id}, {"msg", m}});
            return true;
        }
        if (t == "foto") {
            // La foto de su cámara: queda acá; la app la pide cuando la muestra.
            std::string jpeg = http::deBase64(m.value("jpeg", std::string()));
            if (jpeg.size() >= 4 && jpeg.size() <= kMaxFoto() && static_cast<unsigned char>(jpeg[0]) == 0xFF &&
                static_cast<unsigned char>(jpeg[1]) == 0xD8) {
                if (alFoto_) alFoto_(c->id, jpeg);
                std::vector<std::shared_ptr<Conn>> consolas;
                bool avisar = false;
                {
                    std::lock_guard<std::mutex> l(mu_);
                    fotos_[c->id] = std::move(jpeg);
                    std::int64_t& antes = avisoFotoMs_[c->id];
                    if (ahoraMs() - antes >= kAvisoFotoMs()) {
                        antes = ahoraMs();
                        avisar = true;
                        for (const auto& [cid, cc] : conns_)
                            if (cid.rfind("consola:", 0) == 0) consolas.push_back(cc);
                    }
                }
                if (avisar) {
                    for (const auto& cc : consolas) cc->enviar({{"type", "foto_nueva"}, {"id", c->id}});
                    if (alFotoNueva_) alFotoNueva_(c->id);
                }
            }
            return true;
        }
        if (!kTipos.count(t)) return true;
        // Se reenvía solo lo que la app entiende, con los campos que espera.
        json limpio{{"type", t}};
        for (const char* k : {"accion", "opcion", "votacion"})
            if (m.contains(k) && (m[k].is_number() || m[k].is_string() || m[k].is_boolean())) limpio[k] = m[k];
        alApp_({{"tipo", "entrada"}, {"id", c->id}, {"msg", limpio}});
        return true;
    });
    if (fin == canal::Fin::Exceso) log_.warn(llenar(traducir("gateway", "sesión: %1 manda demasiado; se corta"), {c->id}), -1);
    bool eraLaActual = false;
    {
        std::lock_guard<std::mutex> l(mu_);
        auto it = conns_.find(c->id);
        if (it != conns_.end() && it->second == c) {
            conns_.erase(it);
            eraLaActual = true;
        }
    }
    c->soltar();
    // Si lo reemplazó otra conexión suya, no se avisa que se fue.
    if (eraLaActual && medios_.cerrar) {
        int canal = -1;
        {
            std::lock_guard<std::mutex> l(mu_);
            if (auto it = mediosDe_.find(c->id); it != mediosDe_.end()) canal = it->second;
        }
        if (canal >= 0) medios_.cerrar(c->id, canal); // su WebRTC hacia la sala no sigue sin él
    }
    if (eraLaActual && !consola && medios_.vistaCerrar) medios_.vistaCerrar(c->id);
    if (eraLaActual && !consola) {
        // Se fue: la atención hablada termina con él (la app se entera por la conexión).
        bool atendido = false;
        {
            std::lock_guard<std::mutex> l(mu_);
            atendido = atiende_ == c->id;
            if (atendido) atiende_.clear();
        }
        if (atendido && medios_.atiendeCerrar) medios_.atiendeCerrar(c->id);
    }
    if (eraLaActual) {
        alApp_({{"tipo", "conexion"}, {"id", c->id}, {"conectado", false}, {"ip", c->ip}});
        log_.info(llenar(traducir("gateway", "sesión: %1 desconectado"), {c->id}));
    }
}

bool SesionBorde::admin(long long fd, const std::string& method, const std::string& path, const std::string& query,
                        const std::string& body) {
    if (path.rfind("/api/v1/sesion/", 0) != 0) return false;
    const std::string que = path.substr(std::string("/api/v1/sesion/").size());
    if (method == "GET" && que == "estado") {
        json cs = json::array();
        std::lock_guard<std::mutex> l(mu_);
        for (const auto& [id, c] : conns_)
            cs.push_back({{"id", id}, {"ip", c->ip}, {"lugar", c->lugar}, {"foto", fotos_.count(id) > 0}});
        http::sendResponse(fd, 200, "application/json", json{{"conexiones", cs}, {"credenciales", credenciales_.size()}, {"atiende", atiende_}}.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace));
        return true;
    }
    if (method == "GET" && que == "foto") {
        const auto q = http::parseQuery(query);
        const std::string id = q.count("id") ? http::urlDecode(q.at("id")) : std::string();
        std::string jpeg;
        {
            std::lock_guard<std::mutex> l(mu_);
            auto it = fotos_.find(id);
            if (it != fotos_.end()) jpeg = it->second;
        }
        if (jpeg.empty()) return http::sendResponse(fd, 404, "text/plain", traducir("gateway", "sin foto") + "\n"), true;
        http::sendResponse(fd, 200, "image/jpeg", jpeg, "Cache-Control: no-store\r\n");
        return true;
    }
    if (method == "GET" && que == "aparatos") {
        http::sendResponse(fd, 200, "application/json",
                           json{{"pendientes", aparatosPendientes()}}.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace));
        return true;
    }
    const json r = json::parse(body.empty() ? "{}" : body, nullptr, false);
    if (method != "POST" || !r.is_object()) {
        http::sendResponse(fd, 400, "application/json", json{{"error", "bad_request"}}.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace));
        return true;
    }
    if (que == "credenciales") {
        ponerCredenciales(r.value("participantes", json::array()));
        // Lo revocado se corta ya.
        std::vector<std::shared_ptr<Conn>> cortar;
        {
            std::lock_guard<std::mutex> l(mu_);
            for (const auto& [id, c] : conns_)
                if (!credenciales_.count(id) || !aparatoVale(id, c->aparato)) cortar.push_back(c);
        }
        for (const auto& c : cortar) {
            c->enviar({{"type", "cortado"}, {"motivo", QT_TRANSLATE_NOOP("web/comun", "Tu acceso fue revocado.")}});
            c->cortar();
        }
        http::sendResponse(fd, 200, "application/json", json{{"ok", true}}.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace));
        return true;
    }
    if (que == "aparatos") {
        aparatosRecibidos(r.value("recibidos", json::array()));
        http::sendResponse(fd, 200, "application/json", json{{"ok", true}}.dump());
        return true;
    }
    if (que == "vistas") {
        {
            std::lock_guard<std::mutex> l(mu_);
            vistas_ = json::array();
            for (const auto& v : r.value("vistas", json::array()))
                if (v.is_object() && !v.value("id", std::string()).empty())
                    vistas_.push_back({{"id", v.value("id", std::string())}, {"nombre", v.value("nombre", std::string())},
                                       {"canal", v.value("canal", -1)}, {"salida", v.value("salida", std::string())}});
        }
        difundirVistas();
        json lista;
        {
            std::lock_guard<std::mutex> l(mu_);
            lista = listaVistas();
        }
        http::sendResponse(fd, 200, "application/json", json{{"ok", true}, {"vistas", lista}}.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace));
        return true;
    }
    if (que == "hablar") {
        const std::string id = r.value("id", std::string());
        const bool on = r.value("activo", false);
        Vista vista;
        bool conVista = false;
        {
            std::lock_guard<std::mutex> l(mu_);
            hablando_[id] = on;
            if (!on) hablando_.erase(id);
            auto it = vistaDe_.find(id);
            conVista = it != vistaDe_.end();
            if (conVista) vista = vistaElegida(it->second);
        }
        if (conVista && medios_.vistaRuta) medios_.vistaRuta(id, vista, on);
        http::sendResponse(fd, 200, "application/json", json{{"ok", true}, {"con_vista", conVista}}.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace));
        return true;
    }
    if (que == "atiende") {
        const std::string error = ponerAtiende(r.value("id", std::string()), r.value("on", true));
        if (!error.empty())
            http::sendResponse(fd, 409, "application/json", json{{"error", error}}.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace));
        else
            http::sendResponse(fd, 200, "application/json", json{{"ok", true}}.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace));
        return true;
    }
    if (que == "medios") {
        // Con número: una lista vieja que llega después de una nueva no la pisa.
        if (r.contains("seq") && r["seq"].is_number_integer()) {
            const long long seq = r["seq"].get<long long>();
            std::lock_guard<std::mutex> l(mu_);
            if (seq <= ultimoSeqMedios_) {
                http::sendResponse(fd, 200, "application/json", json{{"ok", true}, {"ignorado", "vieja"}}.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace));
                return true;
            }
            ultimoSeqMedios_ = seq;
        }
        std::vector<std::string> ids;
        if (r.contains("ids") && r["ids"].is_array()) {
            for (const auto& x : r["ids"])
                if (x.is_string() && !x.get<std::string>().empty()) ids.push_back(x.get<std::string>());
        } else if (!r.value("id", std::string()).empty()) {
            ids.push_back(r.value("id", std::string()));
        }
        if (static_cast<int>(ids.size()) > kCanalesRemotos) ids.resize(kCanalesRemotos);
        const std::string siguiente = r.value("siguiente", std::string());
        ponerMedios(ids, siguiente);
        http::sendResponse(fd, 200, "application/json", json{{"ok", true}, {"ids", ids}, {"siguiente", siguiente}}.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace));
        return true;
    }
    if (que == "enviar" || que == "cortar") {
        const std::string id = r.value("id", std::string());
        std::vector<std::shared_ptr<Conn>> a;
        {
            std::lock_guard<std::mutex> l(mu_);
            // "*" = todos los integrantes; "consolas" = todas las consolas de la presidencia.
            for (const auto& [cid, c] : conns_) {
                const bool esConsola = cid.rfind("consola:", 0) == 0;
                if ((id == "*" && !esConsola) || (id == "consolas" && esConsola) || cid == id) a.push_back(c);
            }
            if (que == "cortar") fotos_.erase(id);
        }
        for (const auto& c : a) {
            if (que == "enviar") {
                c->enviar(r.value("msg", json::object()));
            } else {
                c->enviar({{"type", "cortado"},
                           {"motivo", r.value("motivo", std::string(QT_TRANSLATE_NOOP("web/comun", "La presidencia te desconectó.")))}});
                c->cortar();
            }
        }
        http::sendResponse(fd, 200, "application/json", json{{"ok", true}, {"entregado", a.size()}}.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace));
        return true;
    }
    http::sendResponse(fd, 404, "application/json", json{{"error", "not_found"}}.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace));
    return true;
}

void SesionBorde::cerrar() {
    std::vector<std::shared_ptr<Conn>> a;
    {
        std::lock_guard<std::mutex> l(mu_);
        for (const auto& [id, c] : conns_) a.push_back(c);
    }
    for (const auto& c : a) {
        c->cortar();
    }
}

std::string SesionBorde::ponerAtiende(const std::string& id, bool on) {
    std::string anterior;
    {
        std::lock_guard<std::mutex> l(mu_);
        if (on) {
            if (id.empty() || id.rfind("consola:", 0) == 0 || conns_.count(id) == 0)
                return traducir("gateway", "no está conectado a la sesión remota");
            if (atiende_ == id) return {};
            anterior = atiende_; // de a uno: hay una sola voz del que atiende
            atiende_ = id;
        } else {
            if (atiende_ != id) return {}; // ya estaba apagada (se fue, o se la pasó a otro)
            atiende_.clear();
        }
    }
    if (!anterior.empty()) avisarAtiende(anterior, false);
    avisarAtiende(id, on);
    log_.info(on ? llenar(traducir("gateway", "sesión: atención hablada para %1"), {id})
                 : llenar(traducir("gateway", "sesión: atención hablada apagada para %1"), {id}));
    return {};
}

std::string SesionBorde::conAtiende() const {
    std::lock_guard<std::mutex> l(mu_);
    return atiende_;
}

void SesionBorde::avisarAtiende(const std::string& id, bool on) {
    if (!on && medios_.atiendeCerrar) medios_.atiendeCerrar(id);
    std::shared_ptr<Conn> c;
    {
        std::lock_guard<std::mutex> l(mu_);
        if (auto it = conns_.find(id); it != conns_.end()) c = it->second;
    }
    if (c) c->enviar({{"type", "atiende"}, {"on", on}});
}

bool SesionBorde::subeSoloAudio(const std::string& sdp) {
    // Sección por sección (m=): un video tiene que decir recvonly o inactive (sin
    // dirección, el valor por omisión es sendrecv: sube); el audio puede subir.
    enum class Tipo { Nada, Audio, Video, Otro } tipo = Tipo::Nada;
    bool hayAudio = false, videoQuieto = true;
    std::size_t pos = 0;
    while (pos < sdp.size()) {
        const std::size_t fin = sdp.find('\n', pos);
        std::string linea = sdp.substr(pos, fin == std::string::npos ? std::string::npos : fin - pos);
        pos = fin == std::string::npos ? sdp.size() : fin + 1;
        if (!linea.empty() && linea.back() == '\r') linea.pop_back();
        if (linea.rfind("m=", 0) == 0) {
            if (tipo == Tipo::Video && !videoQuieto) return false;
            tipo = linea.rfind("m=audio", 0) == 0   ? Tipo::Audio
                   : linea.rfind("m=video", 0) == 0 ? Tipo::Video
                                                    : Tipo::Otro;
            if (tipo == Tipo::Audio) hayAudio = true;
            videoQuieto = tipo != Tipo::Video;
            continue;
        }
        if (tipo == Tipo::Video && (linea == "a=recvonly" || linea == "a=inactive")) videoQuieto = true;
    }
    if (tipo == Tipo::Video && !videoQuieto) return false;
    return hayAudio;
}

namespace {
/// Las salidas que no son la por omisión se ofrecen como vistas «salida:<id>».
const std::string kVistaSalida = "salida:";
} // namespace

SesionBorde::Vista SesionBorde::vistaElegida(const std::string& vista) const {
    if (vista == "programa") return {-1, {}};
    if (vista.rfind(kVistaSalida, 0) == 0) {
        const std::string id = vista.substr(kVistaSalida.size());
        for (const auto& s : salidas_)
            if (s.value("id", std::string()) == id) return {-1, s.value("porOmision", false) ? std::string() : id};
        return {}; // una salida que se borró: sin imagen, con el sonido
    }
    for (const auto& v : vistas_)
        if (v.value("id", std::string()) == vista) return {v.value("canal", -1), v.value("salida", std::string())};
    return {}; // "yo" o una que ya no está: sin imagen, con el sonido
}

json SesionBorde::listaVistas() const {
    // Con una sola salida, la lista es la de la app tal cual (sin colores).
    const bool varias = salidas_.size() > 1;
    const auto salida = [this](const std::string& id) {
        for (const auto& s : salidas_)
            if (id.empty() ? s.value("porOmision", false) : s.value("id", std::string()) == id) return s;
        return json();
    };
    std::set<std::string> yaEstan;
    for (const auto& v : vistas_) yaEstan.insert(v.value("salida", std::string()));
    json out = json::array();
    bool agregadas = false;
    for (const auto& v : vistas_) {
        json e{{"id", v.value("id", std::string())}, {"nombre", v.value("nombre", std::string())}};
        const bool esPrograma = v.value("canal", -1) == -1;
        if (!varias || !esPrograma) {
            out.push_back(e);
            continue;
        }
        const json s = salida(v.value("salida", std::string()));
        if (s.is_object() && !s.value("color", std::string()).empty()) e["color"] = s["color"];
        out.push_back(e);
        // Quien puede mirar el programa puede mirar las otras salidas: van al lado, una
        // vez, con su nombre y su color (las que la app ya puso con nombre propio, no).
        if (agregadas || !v.value("salida", std::string()).empty()) continue;
        agregadas = true;
        for (const auto& o : salidas_) {
            const std::string id = o.value("id", std::string());
            if (o.value("porOmision", false) || yaEstan.count(id) != 0) continue;
            json x{{"id", kVistaSalida + id}, {"nombre", o.value("nombre", id)}};
            if (!o.value("color", std::string()).empty()) x["color"] = o["color"];
            out.push_back(x);
        }
    }
    return out;
}

void SesionBorde::difundirVistas() {
    json lista;
    std::vector<std::pair<std::string, std::pair<Vista, bool>>> rutas;
    std::vector<std::shared_ptr<Conn>> a;
    {
        std::lock_guard<std::mutex> l(mu_);
        lista = listaVistas();
        for (const auto& [id, v] : vistaDe_) rutas.push_back({id, {vistaElegida(v), hablando_[id]}});
        for (const auto& [cid, c] : conns_)
            if (cid.rfind("consola:", 0) != 0) a.push_back(c);
    }
    for (const auto& [id, ru] : rutas)
        if (medios_.vistaRuta) medios_.vistaRuta(id, ru.first, ru.second);
    for (const auto& c : a) c->enviar({{"type", "vistas"}, {"lista", lista}});
}

void SesionBorde::ponerSalidas(const json& saneada) {
    {
        std::lock_guard<std::mutex> l(mu_);
        if (salidas_ == saneada) return;
        salidas_ = saneada.is_array() ? saneada : json::array();
    }
    difundirVistas();
}

void SesionBorde::ponerMedios(const std::vector<std::string>& ids, const std::string& siguiente) {
    // Los que siguen conservan su canal; los nuevos toman el que quede libre. El siguiente de
    // la fila (si no es uno de los que hablan) va al canal kCanalSiguiente: ya manda, sin canal.
    std::map<std::string, int> nuevo;
    std::vector<std::pair<std::string, int>> salen, cambian;
    std::vector<std::pair<std::shared_ptr<Conn>, bool>> avisarEntra; // y si es el siguiente
    std::vector<std::shared_ptr<Conn>> avisarSale;
    {
        std::lock_guard<std::mutex> l(mu_);
        std::set<int> libres;
        for (int k = 1; k <= kCanalesRemotos; ++k) libres.insert(k);
        for (const auto& id : ids)
            if (auto it = mediosDe_.find(id); it != mediosDe_.end() && it->second != kCanalSiguiente) {
                nuevo[id] = it->second;
                libres.erase(it->second);
            }
        for (const auto& id : ids)
            if (!nuevo.count(id) && !libres.empty()) {
                nuevo[id] = *libres.begin();
                libres.erase(libres.begin());
            }
        if (!siguiente.empty() && !nuevo.count(siguiente) && conns_.count(siguiente)) nuevo[siguiente] = kCanalSiguiente;
        for (const auto& [id, canal] : mediosDe_)
            if (!nuevo.count(id)) {
                salen.push_back({id, canal});
                if (auto it = conns_.find(id); it != conns_.end()) avisarSale.push_back(it->second);
            }
        for (const auto& [id, canal] : nuevo) {
            const auto antes = mediosDe_.find(id);
            if (antes == mediosDe_.end()) {
                if (auto it = conns_.find(id); it != conns_.end()) avisarEntra.push_back({it->second, canal == kCanalSiguiente});
            } else if (antes->second != canal) {
                // Ya manda (el siguiente que recibe la palabra): cambia de canal sin reconectar.
                cambian.push_back({id, canal});
                if (auto it = conns_.find(id); it != conns_.end()) avisarEntra.push_back({it->second, canal == kCanalSiguiente});
            }
        }
        if (nuevo == mediosDe_) return;
        mediosDe_ = nuevo;
    }
    for (const auto& [id, canal] : salen)
        if (medios_.cerrar) medios_.cerrar(id, canal);
    for (const auto& [id, canal] : cambian)
        if (medios_.ruta) medios_.ruta(id, canal);
    for (const auto& c : avisarSale) c->enviar({{"type", "medios"}, {"activo", false}});
    for (const auto& [c, previo] : avisarEntra) c->enviar({{"type", "medios"}, {"activo", true}, {"previo", previo}});
    std::string quienes;
    for (const auto& [id, canal] : nuevo)
        quienes += (quienes.empty() ? "" : ", ") +
                   (canal == kCanalSiguiente ? llenar(traducir("gateway", "%1 (el siguiente, sin canal)"), {id})
                                             : llenar(traducir("gateway", "%1 (canal %2)"), {id, std::to_string(canal)}));
    log_.info(llenar(traducir("gateway", "sesión: medios a la sala: %1"),
                     {quienes.empty() ? traducir("gateway", "nadie") : quienes}));
}

} // namespace ba::gateway
