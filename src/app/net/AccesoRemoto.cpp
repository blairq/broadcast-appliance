// ============================================================================
// src/app/net/AccesoRemoto.cpp — ver AccesoRemoto.hpp
// ============================================================================

#include "net/AccesoRemoto.hpp"

#include "arranque/AppRuntime.hpp"
#include "net/AsyncHttp.hpp"
#include "plugins/Hijo.hpp"
#include "security/CredentialStore.hpp"
#include "ui/ConferenceClient.hpp"

#include <QCoreApplication>
#include <QDnsLookup>
#include <QProcessEnvironment>
#include <QHostAddress>
#include <QHostInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QRegularExpression>

#include <algorithm>

namespace ba::app {

using json = nlohmann::json;

namespace {

constexpr const char* kToken = "pinggy-token";
constexpr const char* kApiKey = "pinggy-api-key";
constexpr const char* kTokenCloudflare = "cloudflare-token"; // lo lee el gateway (main.cpp)
constexpr const char* kSesionesActivas = "https://dashboard.pinggy.io/backend/api/v1/session/active";
// El mismo backend que usa el panel web de pinggy: con la API key (Bearer) contesta
// también el token del túnel, el subdominio y cortar sesiones (probado 2026-10-12).
constexpr const char* kApiPinggy = "https://dashboard.pinggy.io/backend/api/v1";

/// Un secreto pegado del panel: sin espacios alrededor; adentro, nada de blancos.
QString validarSecreto(const QString& crudo, QString& limpio) {
    limpio = crudo.trimmed();
    if (limpio.isEmpty()) return "Está vacío.";
    if (limpio.contains(QRegularExpression("\\s"))) return "Tiene espacios o saltos de línea: copialo de nuevo, entero.";
    if (limpio.size() < 6) return "Es demasiado corto: copialo entero del panel de pinggy.";
    if (limpio.size() > 1024) return "Es demasiado largo: ¿se pegó otra cosa?";
    return {};
}

QString normalHost(QString h) {
    h = h.trimmed().toLower();
    h.remove(QRegularExpression("^[a-z]+://"));
    h = h.section('/', 0, 0);
    while (h.endsWith('.')) h.chop(1);
    return h;
}

/// Nada que se llame token, clave o secreto sale a la vista (la respuesta del panel
/// de pinggy podría traer el token del túnel).
void taparSecretos(json& j) {
    if (j.is_object()) {
        for (auto it = j.begin(); it != j.end(); ++it) {
            std::string k = it.key();
            std::transform(k.begin(), k.end(), k.begin(), [](unsigned char c) { return std::tolower(c); });
            if (k.find("token") != std::string::npos || k.find("key") != std::string::npos ||
                k.find("secret") != std::string::npos || k.find("pass") != std::string::npos)
                it.value() = "(oculto)";
            else
                taparSecretos(it.value());
        }
    } else if (j.is_array()) {
        for (auto& e : j) taparSecretos(e);
    }
}

} // namespace

AccesoRemoto::AccesoRemoto(AppRuntime& rt, ConferenceClient& cli, QObject* parent)
    : QObject(parent), rt_(rt), cli_(cli) {
    poll_.setInterval(5000);
    connect(&poll_, &QTimer::timeout, this, &AccesoRemoto::refrescar);
    estado_ = {{"disponible", false}, {"sala", false}, {"texto", "Sin consultar todavía."}};
    // Se prendió o apagó un túnel (desde acá, Configuración o el MCP): la conexión cambió.
    observador_ = rt_.observar([this](AppRuntime::Cambio c) {
        if (c != AppRuntime::Cambio::Ajustes) return;
        QMetaObject::invokeMethod(this, [this]() {
            emit estadoChanged();
            emit credencialesChanged();
            refrescar();
        }, Qt::QueuedConnection);
    });
}

AccesoRemoto::~AccesoRemoto() { rt_.dejarDeObservar(observador_); }

QVariantMap AccesoRemoto::conexion() const {
    const auto& g = rt_.gateway();
    if (g.pinggyActivo) {
        const QVariantMap p = estado_.value("pinggy").toMap();
        const QString url = p.value("urlPublica").toString();
        const QString est = p.value("estado").toString();
        const QString error = p.value("error").toString();
        const QString estado = !error.isEmpty() ? "error" : (!url.isEmpty() && est == "listo") ? "listo" : "conectando";
        return {{"activo", true}, {"via", "pinggy"}, {"estado", estado}, {"url", url},
                {"texto", estado == "listo" ? "Por internet (pinggy): " + url
                          : estado == "error" ? "Pinggy no conecta: " + error
                                              : QString("Conectando por pinggy…")}};
    }
    if (g.tunnel) {
        const QString host = QString::fromStdString(g.tunnelHostname);
        const QString url = host.isEmpty() ? QString() : "https://" + host;
        return {{"activo", true}, {"via", "cloudflare"}, {"estado", url.isEmpty() ? "conectando" : "listo"}, {"url", url},
                {"texto", url.isEmpty() ? QString("Por internet (Cloudflare)") : "Por internet (Cloudflare): " + url}};
    }
    return {{"activo", false}, {"via", ""}, {"estado", "apagado"}, {"url", ""},
            {"texto", "Solo en la red local: nadie de afuera puede entrar"}};
}

QVariantList AccesoRemoto::formasDeConectar() const {
    const auto& g = rt_.gateway();
    QVariantList out;
    if (tokenGuardado()) {
        const QString host = QString::fromStdString(g.pinggyHost);
        out << QVariantMap{{"id", "pinggy"}, {"nombre", "Pinggy"},
                           {"detalle", g.pinggyDominioTipo == "azar" || host.isEmpty() ? QString("con tu cuenta (dirección al azar)")
                                                                                       : "con tu cuenta: " + host}};
    } else if (apiKeyGuardada()) {
        out << QVariantMap{{"id", "pinggy-cuenta"}, {"nombre", "Pinggy"},
                           {"detalle", "con tu API key: arma el túnel y la dirección fija de la cuenta"}};
    }
    if (tokenCloudflareGuardado()) {
        const QString host = QString::fromStdString(g.tunnelHostname);
        out << QVariantMap{{"id", "cloudflare"}, {"nombre", "Cloudflare"},
                           {"detalle", host.isEmpty() ? QString("con tu token") : "con tu token: " + host}};
    }
    // Sin cuenta de pinggy: igual se puede, con una dirección al azar que dura 60 minutos.
    if (!tokenGuardado() && !apiKeyGuardada())
        out << QVariantMap{{"id", "pinggy-gratis"}, {"nombre", "Pinggy gratis"},
                           {"detalle", "sin cuenta: dirección al azar, se corta a los 60 minutos"}};
    return out;
}

QString AccesoRemoto::conectar(const QString& id) {
    if (!aplicarPinggy_) return "no disponible";
    if (enVivo_ && enVivo_()) return "El evento está corriendo: el túnel no se toca hasta que termine (reinicia la sala).";
    if (id == "pinggy-cuenta") {
        if (!apiKeyGuardada()) return "No hay API key de pinggy guardada (Configuración → Pinggy).";
        if (rt_.gateway().tunnel)
            if (const QString e = aplicarPinggy_({{"gateway.tunnel", false}}); !e.isEmpty()) return e;
        configurarConApiKey(); // asíncrono: el resultado en `cuenta`
        return {};
    }
    QVariantMap cambios;
    if (id == "pinggy") {
        if (!tokenGuardado()) return "No hay token de pinggy guardado (Configuración → Pinggy).";
        cambios = {{"pinggy.activo", true}, {"gateway.tunnel", false}};
    } else if (id == "pinggy-gratis") {
        cambios = {{"pinggy.activo", true}, {"pinggy.dominioTipo", "azar"}, {"gateway.tunnel", false}};
    } else if (id == "cloudflare") {
        if (!tokenCloudflareGuardado()) return "No hay token de Cloudflare guardado (Configuración → Cloudflare).";
        cambios = {{"gateway.tunnel", true}, {"pinggy.activo", false}};
    } else {
        return "No conozco esa forma de conectar: «" + id + "».";
    }
    return aplicarPinggy_(cambios);
}

QString AccesoRemoto::desconectar() {
    if (!aplicarPinggy_) return "no disponible";
    if (enVivo_ && enVivo_()) return "El evento está corriendo: el túnel no se toca hasta que termine (reinicia la sala).";
    const auto& g = rt_.gateway();
    if (!g.pinggyActivo && !g.tunnel) return {};
    return aplicarPinggy_({{"pinggy.activo", false}, {"gateway.tunnel", false}});
}

bool AccesoRemoto::tokenGuardado() const { return ba::security::CredentialStore::bundle().exists(kToken); }
bool AccesoRemoto::apiKeyGuardada() const { return ba::security::CredentialStore::bundle().exists(kApiKey); }
bool AccesoRemoto::clavesIguales() const {
    std::string t, k;
    const auto almacen = ba::security::CredentialStore::bundle();
    return almacen.read(kToken, t) && almacen.read(kApiKey, k) && !t.empty() && t == k;
}

void AccesoRemoto::setSiguiendo(bool on) {
    if (on == siguiendo_) return;
    siguiendo_ = on;
    if (on) {
        refrescar();
        poll_.start();
    } else {
        poll_.stop();
    }
    emit siguiendoChanged();
}

void AccesoRemoto::pedirEstado(std::function<void(bool, const QVariantMap&)> done) {
    if (!cli_.online()) return done(false, {});
    // getRaw: un 404 (gateway viejo) no es un error para avisarle al operador.
    QPointer<AccesoRemoto> self(this);
    cli_.getRaw(ruta_, [self, done](bool ok, const QByteArray& body) {
        if (!self) return;
        if (ok) return done(true, QJsonDocument::fromJson(body).object().toVariantMap());
        // Por si la ruta quedó con la versión en el camino, como las demás de la API.
        const QString otra = self->ruta_ == "/api/remoto" ? "/api/v1/remoto" : "/api/remoto";
        self->cli_.getRaw(otra, [self, done, otra](bool ok2, const QByteArray& body2) {
            if (!self) return;
            if (ok2) self->ruta_ = otra;
            done(ok2, ok2 ? QJsonDocument::fromJson(body2).object().toVariantMap() : QVariantMap{});
        });
    });
}

void AccesoRemoto::refrescar() {
    QPointer<AccesoRemoto> self(this);
    pedirEstado([self](bool ok, const QVariantMap& r) {
        if (self) self->aplicarEstado(ok, r);
    });
}

void AccesoRemoto::aplicarEstado(bool ok, const QVariantMap& r) {
    QVariantMap m;
    m["sala"] = cli_.online();
    m["disponible"] = ok;
    if (!cli_.online())
        m["texto"] = "La sala no está andando: el estado del túnel se ve cuando arranca.";
    else if (!ok)
        m["texto"] = "La sala no informa el estado del túnel (el gateway es de una versión anterior).";
    const QVariantMap p = r.value("pinggy").toMap();
    m["pinggy"] = p;

    // La dirección de cada página, a partir de la pública.
    QString base = p.value("urlPublica").toString();
    if (base.isEmpty())
        for (const QVariant& u : p.value("urls").toList())
            if (u.toString().startsWith("https://")) {
                base = u.toString();
                break;
            }
    while (base.endsWith('/')) base.chop(1);
    QVariantList enl;
    if (!base.isEmpty()) {
        const QString sala = QString::fromStdString(rt_.gateway().room);
        enl << QVariantMap{{"nombre", "Invitados"}, {"url", base + "/invitados/#room=" + sala}}
            << QVariantMap{{"nombre", "Sesión remota (integrantes)"}, {"url", base + "/sesion/"}}
            << QVariantMap{{"nombre", "Público"}, {"url", base + "/publico/"}}
            << QVariantMap{{"nombre", "Botonera (tablets)"}, {"url", base + "/botonera/"}};
    }
    const bool cambio = m != estado_ || enl != enlaces_;
    estado_ = m;
    enlaces_ = enl;
    if (cambio) emit estadoChanged();
}

QString AccesoRemoto::guardarToken(const QString& token) {
    QString limpio;
    if (const QString e = validarSecreto(token, limpio); !e.isEmpty()) return e;
    if (!ba::security::CredentialStore::bundle().write(kToken, limpio.toStdString()))
        return "No se pudo guardar en Credenciales/ (¿el disco está lleno o es de solo lectura?).";
    emit credencialesChanged();
    return {};
}

QVariantMap AccesoRemoto::guardarTokenTunel(const QString& token, bool hayCambiosSinAplicar) {
    if (const QString e = guardarToken(token); !e.isEmpty())
        return {{"error", e}, {"reiniciarAhora", false}};
    // Lo que rige es lo guardado (rt_.gateway()), no lo que está a medio editar.
    const bool reiniciar = cli_.online() && rt_.gateway().pinggyActivo && !hayCambiosSinAplicar;
    return {{"error", QString()}, {"reiniciarAhora", reiniciar}};
}

QString AccesoRemoto::borrarToken() {
    if (!ba::security::CredentialStore::bundle().remove(kToken) && tokenGuardado())
        return "No se pudo borrar de Credenciales/.";
    emit credencialesChanged();
    return {};
}

bool AccesoRemoto::tokenCloudflareGuardado() const {
    return ba::security::CredentialStore::bundle().exists(kTokenCloudflare);
}

QString AccesoRemoto::guardarTokenCloudflare(const QString& token) {
    QString limpio;
    if (const QString e = validarSecreto(token, limpio); !e.isEmpty()) return e;
    if (!ba::security::CredentialStore::bundle().write(kTokenCloudflare, limpio.toStdString()))
        return "No se pudo guardar en Credenciales/ (¿el disco está lleno o es de solo lectura?).";
    emit credencialesChanged();
    return {};
}

QString AccesoRemoto::borrarTokenCloudflare() {
    if (!ba::security::CredentialStore::bundle().remove(kTokenCloudflare) && tokenCloudflareGuardado())
        return "No se pudo borrar de Credenciales/.";
    emit credencialesChanged();
    return {};
}

QString AccesoRemoto::guardarApiKey(const QString& clave) {
    QString limpio;
    if (const QString e = validarSecreto(clave, limpio); !e.isEmpty()) return e;
    if (!ba::security::CredentialStore::bundle().write(kApiKey, limpio.toStdString()))
        return "No se pudo guardar en Credenciales/.";
    emit credencialesChanged();
    return {};
}

QString AccesoRemoto::borrarApiKey() {
    if (!ba::security::CredentialStore::bundle().remove(kApiKey) && apiKeyGuardada())
        return "No se pudo borrar de Credenciales/.";
    panel_ = {};
    emit panelChanged();
    emit credencialesChanged();
    return {};
}

QString AccesoRemoto::instruccionesDns(const QString& hostCrudo, const QString& destinoCrudo) const {
    const QString host = normalHost(hostCrudo);
    const QString destino = normalHost(destinoCrudo);
    const QString h = host.isEmpty() ? "sala.miorganismo.gob.ar" : host;
    const QString d = destino.isEmpty() ? "<el destino que muestra el panel de pinggy>" : destino;
    const QString primero = h.section('.', 0, 0);
    const QString zona = h.section('.', 1);
    QStringList t;
    t << "Para que " + h + " lleve a la sala por pinggy:"
      << ""
      << "1) En el DNS de " + (zona.isEmpty() ? h : zona) + " (lo hace quien administra el dominio), crear este registro:"
      << "     Tipo:     CNAME"
      << "     Nombre:   " + h + "   (si el panel pide solo la parte de adelante: " + primero + ")"
      << "     Destino:  " + d + "."
      << "     TTL:      300 (5 minutos)"
      << "   Si el DNS está en Cloudflare: con la nube GRIS («solo DNS», sin proxy). Con la nube naranja no anda."
      << ""
      << "2) En el panel de pinggy (dashboard.pinggy.io, cuenta Pro):"
      << "   a. Domains → agregar el dominio " + h + "."
      << "   b. Asignarlo al token de túnel que usa este equipo."
      << "   c. Con el túnel APAGADO (en este equipo: Configuración → Pinggy → «Túnel de pinggy» apagado),"
      << "      validar el dominio y emitir el certificado."
      << "   d. Recién después prender el túnel."
      << "   Guía de pinggy: https://pinggy.io/docs/custom_domain/"
      << ""
      << "3) Opcional, para más adelante (todavía no se usa): un nombre por servicio"
      << "   (sesion." + h + ", botonera." + h + "…) con un comodín:"
      << "     CNAME   *." + h + "   →   " + d + "."
      << ""
      << "4) El dominio raíz (" + (zona.isEmpty() ? h : zona) + ", sin «" + primero + ".» adelante) no se puede apuntar con CNAME."
      << "   Para eso pinggy usa «relays» (registros TXT más A/AAAA de la región): https://pinggy.io/docs/relays/"
      << "   Ojo: por un relay no pasa UDP (el video de los invitados iría solo por TCP).";
    return t.join('\n');
}

void AccesoRemoto::verificarDns(const QString& hostCrudo, const QString& destinoCrudo) {
    const QString host = normalHost(hostCrudo);
    const QString destino = normalHost(destinoCrudo);
    if (host.isEmpty() || !host.contains('.')) {
        dns_ = {{"estado", "error"}, {"texto", "Falta el dominio (p. ej. sala.miorganismo.gob.ar)."}};
        emit dnsChanged();
        return;
    }
    dns_ = {{"estado", "verificando"}, {"texto", "Consultando el DNS de " + host + "…"}};
    emit dnsChanged();
    // QDnsLookup es asincrónico (no cuelga la interfaz) y no usa programas del sistema.
    auto* q = new QDnsLookup(QDnsLookup::CNAME, host, this);
    QPointer<AccesoRemoto> self(this);
    connect(q, &QDnsLookup::finished, this, [self, q, host, destino]() {
        q->deleteLater();
        if (!self) return;
        QString cname;
        for (const QDnsDomainNameRecord& r : q->canonicalNameRecords()) {
            cname = normalHost(r.value());
            if (!cname.isEmpty()) break;
        }
        const bool fallo = q->error() != QDnsLookup::NoError && q->error() != QDnsLookup::NotFoundError;
        const QString errDns = q->errorString();
        // Y a qué direcciones llega, para decir algo útil aunque no haya CNAME.
        QHostInfo::lookupHost(host, self.data(), [self, host, destino, cname, fallo, errDns](const QHostInfo& hi) {
            if (!self) return;
            QStringList ips;
            for (const QHostAddress& a : hi.addresses()) ips << a.toString();
            const QString llega = ips.isEmpty() ? QString("no resuelve a ninguna dirección")
                                                : "resuelve a " + ips.mid(0, 4).join(", ");
            QString estado, texto;
            if (!cname.isEmpty() && !destino.isEmpty() && cname == destino) {
                estado = "ok";
                texto = "Bien: " + host + " apunta a " + cname + " (" + llega + "). Si recién se cargó en pinggy, "
                        "falta validar el dominio y emitir el certificado con el túnel apagado.";
            } else if (!cname.isEmpty()) {
                estado = "mal";
                texto = destino.isEmpty()
                            ? host + " apunta a " + cname + ". Cargá el «Destino del CNAME» para comparar."
                            : host + " apunta a " + cname + ", pero tendría que apuntar a " + destino +
                                  ". Hay que corregir el registro CNAME.";
            } else if (fallo) {
                estado = "error";
                texto = "No se pudo consultar el DNS (" + errDns + "). ¿Hay internet en este equipo?";
            } else {
                estado = "mal";
                texto = host + " no tiene un registro CNAME (" + llega + "). Falta crear el registro, o todavía no "
                        "se propagó (con TTL 300, unos minutos).";
                if (!ips.isEmpty())
                    texto += " Si el DNS está en Cloudflare con la nube naranja, pasala a gris: así se ve como "
                             "direcciones de Cloudflare y no como CNAME.";
            }
            self->dns_ = {{"estado", estado}, {"texto", texto}};
            emit self->dnsChanged();
        });
    });
    q->lookup();
}

void AccesoRemoto::verEnPinggy() {
    std::string clave;
    if (!ba::security::CredentialStore::bundle().read(kApiKey, clave) || clave.empty()) {
        panel_ = {{"estado", "error"}, {"texto", "Falta la API key de pinggy (guardala acá arriba)."}};
        emit panelChanged();
        return;
    }
    panel_ = {{"estado", "consultando"}, {"texto", "Consultando el panel de pinggy…"}};
    emit panelChanged();
    net::HttpRequest req;
    req.url = kSesionesActivas;
    req.headers = {{"Authorization", "Bearer " + clave}, {"Accept", "application/json"}};
    req.timeoutMs = 15000;
    QPointer<AccesoRemoto> self(this);
    net::asyncRequest(this, std::move(req), [self](const net::HttpResponse& res) {
        if (!self) return;
        QString estado = "ok", texto;
        if (res.status == 0) {
            estado = "error";
            texto = "No se pudo llegar a pinggy: " + QString::fromStdString(res.error);
        } else if (res.status == 401 || res.status == 403) {
            estado = "error";
            texto = "Pinggy no aceptó la API key (¿es la del plan Pro? ¿la copiaste entera?).";
        } else if (res.status < 200 || res.status >= 300) {
            estado = "error";
            texto = QString("Pinggy contestó %1.").arg(res.status);
        } else {
            json j = json::parse(res.body, nullptr, false);
            if (j.is_discarded()) {
                texto = "Pinggy contestó algo que no es JSON.";
            } else {
                taparSecretos(j);
                const bool vacio = (j.is_array() && j.empty()) || (j.is_object() && j.empty());
                texto = vacio ? QString("Pinggy no tiene ningún túnel activo con esta cuenta.")
                              : QString::fromStdString(j.dump(2, ' ', false, json::error_handler_t::replace)).left(4000);
            }
        }
        self->panel_ = {{"estado", estado}, {"texto", texto}};
        emit self->panelChanged();
    });
}

QVariantMap AccesoRemoto::configuracion() const {
    const GatewaySettings& g = rt_.gateway();
    return {{"activo", g.pinggyActivo},
            {"region", QString::fromStdString(g.pinggyRegion)},
            {"dominioTipo", QString::fromStdString(g.pinggyDominioTipo)},
            {"host", QString::fromStdString(g.pinggyHost)},
            {"cnameDestino", QString::fromStdString(g.pinggyCnameDestino)},
            {"turn", g.pinggyTurn},
            {"turnUdp", g.pinggyTurnUdp},
            {"listaBlanca", QString::fromStdString(g.pinggyListaBlanca)}};
}

void AccesoRemoto::consultar(std::function<void(json)> done) {
    QPointer<AccesoRemoto> self(this);
    pedirEstado([self, done](bool ok, const QVariantMap& r) {
        if (!self) return done(json{{"error", "no disponible"}});
        self->aplicarEstado(ok, r);
        const auto aJson = [](const QVariant& v) {
            return json::parse(QJsonDocument::fromVariant(v).toJson(QJsonDocument::Compact).toStdString(), nullptr,
                               false);
        };
        json out = aJson(self->estado_);
        if (out.is_discarded() || !out.is_object()) out = json::object();
        json enl = aJson(self->enlaces_);
        out["enlaces"] = enl.is_discarded() ? json::array() : enl;
        json cfg = aJson(self->configuracion());
        out["configuracion"] = cfg.is_discarded() ? json::object() : cfg;
        // Solo si está o falta: el token nunca pasa por el asistente.
        out["token_tunel"] = self->tokenGuardado() ? "guardado" : "falta";
        out["api_key"] = self->apiKeyGuardada() ? "guardada" : "falta";
        if (self->rt_.gateway().pinggyDominioTipo == "propio")
            out["instrucciones_dns"] = self->instruccionesDns(QString::fromStdString(self->rt_.gateway().pinggyHost),
                                                              QString::fromStdString(self->rt_.gateway().pinggyCnameDestino))
                                           .toStdString();
        done(out);
    });
}

} // namespace ba::app

// --- Probar una dirección -------------------------------------------------------------------

namespace ba::app {

void AccesoRemoto::ponerPrueba(const QString& estado, const QString& texto) {
    prueba_ = {{"estado", estado}, {"texto", texto}};
    emit pruebaChanged();
}

void AccesoRemoto::probarDireccion(const QString& tipo, const QString& hostCrudo) {
    QString host = hostCrudo.trimmed().toLower();
    host.remove(QRegularExpression("^https?://"));
    host = host.section('/', 0, 0);
    while (host.endsWith('.')) host.chop(1);
    if (tipo == "azar")
        return ponerPrueba("ok", "Al azar no hay nada que probar: la da pinggy cada vez que arranca el túnel.");
    if (host.isEmpty()) return ponerPrueba("error", "Escribí primero el subdominio o el dominio.");
    if (!tokenGuardado())
        return ponerPrueba("error", "Falta el token de túnel: sin él pinggy no puede dar una dirección fija.");
    // El túnel de este equipo usa el mismo token: pinggy deja uno por token.
    const QVariantMap p = estado_.value("pinggy").toMap();
    const QString est = p.value("estado").toString();
    if (est == "listo" || est == "conectando" || est == "reconectando") {
        if (est == "listo" && p.value("urlPublica").toString().contains(host))
            return ponerPrueba("ok", "Ya anda: el túnel de este equipo está saliendo por " + host + ".");
        return ponerPrueba("error", "El túnel de este equipo está prendido y pinggy deja un solo túnel por token: "
                                    "apagalo (y aplicá) para probar otra dirección.");
    }
    ponerPrueba("probando", "Probando " + host + " en pinggy…");
    // Con la API key: ¿ya la usa un túnel activo de la cuenta (otro equipo)?
    std::string clave;
    if (!ba::security::CredentialStore::bundle().read(kApiKey, clave) || clave.empty()) return probarConTunel(host);
    net::HttpRequest req;
    req.url = kSesionesActivas;
    req.headers = {{"Authorization", "Bearer " + clave}, {"Accept", "application/json"}};
    req.timeoutMs = 15000;
    QPointer<AccesoRemoto> self(this);
    net::asyncRequest(this, std::move(req), [self, host](const net::HttpResponse& res) {
        if (!self) return;
        if (res.status >= 200 && res.status < 300 && QString::fromStdString(res.body).toLower().contains(host))
            return self->ponerPrueba("mal", "Esa dirección la está usando ahora un túnel de tu cuenta (¿otro equipo "
                                            "con el mismo token?). Apagalo allá o usá otra.");
        self->probarConTunel(host);
    });
}

void AccesoRemoto::probarConTunel(const QString& host) {
    std::string token;
    ba::security::CredentialStore::bundle().read(kToken, token);
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert("BA_PINGGY_TOKEN", QString::fromStdString(token).trimmed());
#ifdef _WIN32
    const QString exe = QCoreApplication::applicationDirPath() + "/ba-pinggy.exe";
#else
    const QString exe = QCoreApplication::applicationDirPath() + "/ba-pinggy";
#endif
    const auto& g = rt_.gateway();
    // Hacia el puerto del túnel (nadie lo atiende si el túnel está apagado: no importa,
    // solo se mira qué dirección da pinggy).
    const QStringList args{"--forward", QString("http=127.0.0.1:%1").arg(g.pinggyPuertoTunel > 0 ? g.pinggyPuertoTunel : 8786),
                           "--host", host, "--region", QString::fromStdString(g.pinggyRegion.empty() ? "auto" : g.pinggyRegion),
                           "--haproxy", "no", "--probar"};
    QPointer<AccesoRemoto> self(this);
    plugins::Hijo::correr(this, exe, args, env, 30000, [self, host](int codigo, QByteArray out, QByteArray) {
        if (!self) return;
        QStringList urls;
        QString error;
        for (const QByteArray& l : out.split('\n')) {
            const json j = json::parse(l.toStdString(), nullptr, false);
            if (!j.is_object()) continue;
            const std::string ev = j.value("ev", std::string());
            if (ev == "listo" || ev == "reconectado")
                for (const auto& u : j.value("urls", json::array()))
                    if (u.is_string()) urls << QString::fromStdString(u.get<std::string>());
            if (ev == "error") error = QString::fromStdString(j.value("error", std::string()));
        }
        const QString err = error.toLower();
        if (!urls.isEmpty()) {
            for (const QString& u : urls)
                if (u.toLower().contains(host))
                    return self->ponerPrueba("ok", "Pinggy dio " + host + ": está asignada a tu token y libre. "
                                                   "Prendé el túnel y aplicá.");
            return self->ponerPrueba("mal", "Pinggy NO dio " + host + " (dio otra al azar: " + urls.first() +
                                                "): no está asignada a tu token. En dashboard.pinggy.io → Domains, "
                                                "cargala y asignala a este token.");
        }
        if (err.contains("already") || err.contains("in use") || err.contains("bound") || err.contains("en uso"))
            return self->ponerPrueba("mal", "Esa dirección está en uso por otro túnel (¿otro equipo con el mismo "
                                            "token?): " + error);
        if (err.contains("token"))
            return self->ponerPrueba("error", "Pinggy no aceptó el token guardado: " + error);
        if (codigo == -1 && error.isEmpty())
            return self->ponerPrueba("error", "No arrancó la prueba (¿falta ba-pinggy en el programa?) o pinggy no "
                                              "contestó en 30 s.");
        self->ponerPrueba("error", error.isEmpty() ? QString("Pinggy no contestó como se esperaba.") : error);
    });
}

// --- Todo con la API key ----------------------------------------------------------------

namespace {
/// El primer mensaje de error que manda pinggy ({"errors": ["…"]} o {"error": "…"}).
QString errorDePinggy(const net::HttpResponse& res) {
    const json j = json::parse(res.body, nullptr, false);
    if (j.is_object()) {
        if (j.contains("errors") && j["errors"].is_array() && !j["errors"].empty() && j["errors"][0].is_string())
            return QString::fromStdString(j["errors"][0].get<std::string>());
        if (j.contains("error") && j["error"].is_string()) return QString::fromStdString(j["error"].get<std::string>());
    }
    if (res.status == 401 || res.status == 403) return "pinggy no aceptó la API key";
    if (res.status == 0) return "no se pudo llegar a pinggy: " + QString::fromStdString(res.error);
    return QString("pinggy contestó %1").arg(res.status);
}

net::HttpRequest pedidoPinggy(const std::string& clave, const std::string& ruta, const json* cuerpo = nullptr) {
    net::HttpRequest req;
    req.url = std::string(kApiPinggy) + ruta;
    req.headers = {{"Authorization", "Bearer " + clave}, {"Accept", "application/json"}};
    if (cuerpo) {
        req.method = "POST";
        req.headers.emplace_back("Content-Type", "application/json");
        req.body = cuerpo->dump();
    }
    req.timeoutMs = 20000;
    return req;
}
} // namespace

void AccesoRemoto::ponerCuenta(const QString& estado, const QString& texto, const QVariantMap& extra) {
    QVariantMap m = extra;
    m["estado"] = estado;
    m["texto"] = texto;
    cuenta_ = m;
    emit cuentaChanged();
}

void AccesoRemoto::configurarConApiKey(const QString& pedido) {
    std::string clave;
    if (!ba::security::CredentialStore::bundle().read(kApiKey, clave) || clave.empty())
        return ponerCuenta("error", "Falta la API key: pegala en «API key» y guardala.");
    // Solo el nombre: «Sala-Concejo.a.pinggy.link» → «sala-concejo».
    QString sub = pedido.trimmed().toLower();
    sub.remove(QRegularExpression("^https?://"));
    sub = sub.section('.', 0, 0);
    if (!sub.isEmpty() && !QRegularExpression("^[a-z0-9]([a-z0-9-]{1,61}[a-z0-9])?$").match(sub).hasMatch())
        return ponerCuenta("error", "El subdominio va con letras, números y guiones (sin espacios ni acentos), p. ej. sala-concejo.");
    ponerCuenta("trabajando", "Pidiendo los datos a tu cuenta de pinggy…");
    if (sub.isEmpty()) return seguirConToken(clave, {});
    QPointer<AccesoRemoto> self(this);
    net::asyncRequest(this, pedidoPinggy(clave, "/subdomain/list"), [self, clave, sub](const net::HttpResponse& res) {
        if (!self) return;
        const json l = json::parse(res.body, nullptr, false);
        if (res.status < 200 || res.status >= 300 || !l.is_array() || l.empty() || !l[0].is_object())
            return self->ponerCuenta("error", "No pude leer los subdominios de la cuenta: " + errorDePinggy(res) + ".");
        const json& s0 = l[0];
        const std::string actual = s0.value("subdomain", std::string());
        if (QString::fromStdString(actual).section('.', 0, 0) == sub) return self->seguirConToken(clave, sub); // ya es ese
        const json cuerpo{{"subdomain_id", s0.value("subdomain_id", 0)}, {"subdomain_substring", sub.toStdString()}};
        self->ponerCuenta("trabajando", "Pidiendo «" + sub + "» en pinggy…");
        net::asyncRequest(self, pedidoPinggy(clave, "/subdomain/update", &cuerpo), [self, clave, sub](const net::HttpResponse& r) {
            if (!self) return;
            if (r.status < 200 || r.status >= 300)
                return self->ponerCuenta("error", "Pinggy no dio «" + sub + "»: " + errorDePinggy(r) +
                                                      ". Probá con otro nombre (o dejalo vacío para usar el que ya tenés).");
            self->seguirConToken(clave, sub);
        });
    });
}

void AccesoRemoto::seguirConToken(const std::string& clave, const QString& sub) {
    QPointer<AccesoRemoto> self(this);
    net::asyncRequest(this, pedidoPinggy(clave, "/token/active"), [self, sub](const net::HttpResponse& res) {
        if (!self) return;
        const json l = json::parse(res.body, nullptr, false);
        if (res.status < 200 || res.status >= 300 || !l.is_array())
            return self->ponerCuenta("error", "No pude pedir el token: " + errorDePinggy(res) + ".");
        const json* tok = nullptr;
        for (const auto& t : l)
            if (t.is_object() && t.value("enabled", true) && !t.value("token", std::string()).empty()) { tok = &t; break; }
        if (!tok) return self->ponerCuenta("error", "Tu cuenta de pinggy no tiene ningún token activo (¿el plan Pro está vencido?).");
        // El token: directo a la bóveda; no pasa por la interfaz ni por el registro.
        if (!ba::security::CredentialStore::bundle().write(kToken, tok->value("token", std::string())))
            return self->ponerCuenta("error", "No se pudo guardar el token en Credenciales/.");
        emit self->credencialesChanged();
        // Su dirección: la preferencia del token (subdominio o dominio propio) y el puerto TCP fijo.
        std::string host, propio;
        int puerto = 0;
        if (tok->contains("preferences") && (*tok)["preferences"].is_array() && !(*tok)["preferences"].empty()) {
            const json& p = (*tok)["preferences"][0];
            host = p.value("subdomain", std::string());
            if (p.contains("customdomain") && p["customdomain"].is_string()) propio = p["customdomain"].get<std::string>();
            if (p.contains("tcp_persistent_port") && p["tcp_persistent_port"].is_number_integer()) puerto = p["tcp_persistent_port"].get<int>();
        }
        if (!sub.isEmpty() && QString::fromStdString(host).section('.', 0, 0) != sub && propio.empty())
            host = (sub + ".a.pinggy.link").toStdString(); // recién cambiado: la lista puede tardar
        QVariantMap cambios{{"pinggy.activo", true}};
        if (!propio.empty()) { cambios["pinggy.dominioTipo"] = "propio"; cambios["pinggy.host"] = QString::fromStdString(propio); }
        else if (!host.empty()) { cambios["pinggy.dominioTipo"] = "pinggy"; cambios["pinggy.host"] = QString::fromStdString(host); }
        if (puerto > 0) cambios["pinggy.turnTcpPuerto"] = puerto;
        const QString dir = QString::fromStdString(propio.empty() ? host : propio);
        const QVariantMap extra{{"host", dir}, {"puerto_tcp", puerto}};
        if (!self->aplicarPinggy_)
            return self->ponerCuenta("ok", "Token guardado. Dirección: " + dir + ". Prendé el túnel y aplicá.", extra);
        const QString e = self->aplicarPinggy_(cambios);
        if (!e.isEmpty()) return self->ponerCuenta("error", "Token guardado, pero no se pudo prender el túnel: " + e, extra);
        self->ponerCuenta("ok", "Listo: el túnel sale con tu plan Pro por https://" + dir +
                                    (puerto > 0 ? QString(" (video de los de afuera por el puerto %1)").arg(puerto) : QString()) +
                                    ". La sala se reinicia sola con eso.", extra);
    });
}

void AccesoRemoto::cortarTunelesDeLaCuenta() {
    std::string clave;
    if (!ba::security::CredentialStore::bundle().read(kApiKey, clave) || clave.empty())
        return ponerCuenta("error", "Falta la API key.");
    ponerCuenta("trabajando", "Cortando los túneles activos de la cuenta…");
    QPointer<AccesoRemoto> self(this);
    net::asyncRequest(this, pedidoPinggy(clave, "/session/active"), [self, clave](const net::HttpResponse& res) {
        if (!self) return;
        const json l = json::parse(res.body, nullptr, false);
        if (res.status < 200 || res.status >= 300 || !l.is_array())
            return self->ponerCuenta("error", "No pude ver los túneles activos: " + errorDePinggy(res) + ".");
        if (l.empty()) return self->ponerCuenta("ok", "La cuenta no tenía ningún túnel activo.");
        int n = 0;
        for (const auto& s : l) {
            if (!s.is_object() || !s.contains("id")) continue;
            const json cuerpo{{"session-id", s["id"]}};
            ++n;
            net::asyncRequest(self, pedidoPinggy(clave, "/session/kill", &cuerpo), [](const net::HttpResponse&) {});
        }
        self->ponerCuenta("ok", QString("Pedí cortar %1 túnel%2 de la cuenta (el de este equipo vuelve a conectarse solo si está prendido).")
                                    .arg(n).arg(n == 1 ? "" : "es"));
    });
}

} // namespace ba::app
