// ============================================================================
// src/app/vivos/PlataformasVivo.cpp — ver PlataformasVivo.hpp
// ============================================================================

#include "vivos/PlataformasVivo.hpp"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonObject>
#include <QUrlQuery>

#include "facebook/FacebookClient.hpp"
#include "i18n/I18nTexto.hpp"
#include "twitch/TwitchClient.hpp"
#include "youtube/YouTubeClient.hpp"

namespace ba::app::vivos {

namespace {

std::string s(const QJsonValue& v) { return v.toString().toStdString(); }

/// Un Done de los clientes -> Listo, transformando los datos.
template <class F>
auto alListo(Listo listo, F transformar) {
    return [listo = std::move(listo), transformar = std::move(transformar)](bool ok, const QJsonObject& d, const QString& e) {
        if (!ok) return listo(Resultado::mal(e.isEmpty() ? std::string("?") : e.toStdString()));
        listo(Resultado::bien(transformar(d)));
    };
}

/// Los vivos que todavía sirven (no terminados ni revocados).
bool sirve(const QString& estado) { return estado != "complete" && estado != "revoked"; }

} // namespace

// --- YouTube -------------------------------------------------------------------------------

YouTubeVivo::YouTubeVivo(YouTubeClient& principal, const QString& dir, QObject* parent)
    : PlataformaVivo(parent), c_(principal, dir, "youtube"), appAntes_(principal.clientId()) {
    connect(&principal, &YouTubeClient::stateChanged, this, [this]() {
        // La página cambió la app de Google: las demás cuentas la releen.
        if (c_.principal().clientId() != appAntes_) {
            appAntes_ = c_.principal().clientId();
            c_.cadaOtro([](YouTubeClient& c) { c.recargar(); });
        }
        emit cambio();
    });
}

YouTubeVivo::~YouTubeVivo() = default;

YouTubeClient& YouTubeVivo::cli(const std::string& cuenta) const {
    auto* yo = const_cast<YouTubeVivo*>(this);
    bool nuevo = false;
    YouTubeClient& c = c_.de(cuenta, &nuevo);
    if (nuevo) connect(&c, &YouTubeClient::stateChanged, yo, &PlataformaVivo::cambio);
    return c;
}

bool YouTubeVivo::configurada() const { return c_.principal().hasClient(); }
bool YouTubeVivo::conectada(const std::string& cuenta) const { return cli(cuenta).loggedIn(); }
bool YouTubeVivo::conectando(const std::string& cuenta) const { return cli(cuenta).loginPending(); }
bool YouTubeVivo::sinPermiso(const std::string& cuenta) const { return cli(cuenta).sinPermiso(); }
std::string YouTubeVivo::canal(const std::string& cuenta) const { return s(cli(cuenta).account().value("title")); }

void YouTubeVivo::conectar(const std::string& cuenta, Listo listo) {
    YouTubeClient& c = cli(cuenta);
    QPointer<YouTubeClient> cp(&c);
    c.login([cp, listo](bool ok, const QJsonObject&, const QString& e) {
        if (!ok) return listo(Resultado::mal(e.toStdString()));
        if (!cp) return listo(Resultado::mal("?"));
        // El canal (nombre, avatar) antes de contestar.
        cp->loadChannel([listo](bool ok2, const QJsonObject& ch, const QString& e2) {
            if (!ok2) return listo(Resultado::mal(e2.toStdString()));
            listo(Resultado::bien({{"canal", s(ch.value("title"))}}));
        });
    });
}

void YouTubeVivo::cancelarConexion(const std::string& cuenta) { cli(cuenta).cancelLogin(); }

void YouTubeVivo::desconectar(const std::string& cuenta) {
    cli(cuenta).logout([](bool, const QJsonObject&, const QString&) {});
}

void YouTubeVivo::olvidar(const std::string& cuenta) {
    if (cuenta.empty() || cuenta == kPrincipal) return;
    c_.olvidar(cuenta);
}

void YouTubeVivo::vivos(const std::string& cuenta, Listo listo) {
    cli(cuenta).listBroadcasts(alListo(std::move(listo), [](const QJsonObject& d) {
        json items = json::array();
        for (const QJsonValue& v : d.value("items").toArray()) {
            const QJsonObject b = v.toObject();
            if (!sirve(b.value("status").toString())) continue;
            items.push_back({{"id", s(b.value("id"))},
                             {"titulo", s(b.value("title"))},
                             {"estado", s(b.value("status"))},
                             {"privacidad", s(b.value("privacy"))},
                             {"inicio", s(b.value("scheduledStart"))},
                             {"url", s(b.value("url"))},
                             {"estudio", s(b.value("studioUrl"))},
                             {"stream", s(b.value("boundStreamId"))}});
        }
        return json{{"items", items}};
    }));
}

void YouTubeVivo::crearVivo(const std::string& cuenta, const json& datos, Listo listo) {
    QJsonObject p;
    if (datos.contains("titulo")) p.insert("title", QString::fromStdString(datos.value("titulo", std::string())));
    if (datos.contains("descripcion")) p.insert("description", QString::fromStdString(datos.value("descripcion", std::string())));
    p.insert("privacy", QString::fromStdString(datos.value("privacidad", std::string("private"))));
    if (const std::string ini = datos.value("inicio", std::string()); !ini.empty()) p.insert("scheduledStart", QString::fromStdString(ini));
    if (const std::string lat = datos.value("latencia", std::string()); !lat.empty()) p.insert("latency", QString::fromStdString(lat));
    cli(cuenta).createBroadcast(p, alListo(std::move(listo), [](const QJsonObject& d) {
        const QJsonObject b = d.value("broadcast").toObject();
        return json{{"vivo",
                     {{"id", s(b.value("id"))},
                      {"titulo", s(b.value("title"))},
                      {"estado", s(b.value("status"))},
                      {"privacidad", s(b.value("privacy"))},
                      {"inicio", s(b.value("scheduledStart"))},
                      {"url", s(d.value("url"))},
                      {"estudio", s(d.value("studioUrl"))}}}};
    }));
}

void YouTubeVivo::ingesta(const std::string& cuenta, const std::string& vivo, Listo listo) {
    cli(cuenta).ingestion(QString::fromStdString(vivo), alListo(std::move(listo), [](const QJsonObject& d) {
        return json{{"url", s(d.value("url"))},
                    {"clave", s(d.value("key"))},
                    {"estado_stream", s(d.value("streamStatus"))},
                    {"salud", s(d.value("health"))}};
    }));
}

void YouTubeVivo::salud(const std::string& cuenta, const std::string& vivo, Listo listo) {
    // Solo lectura (ingestion crearía un stream si no hay): el vivo y su stream enlazado.
    YouTubeClient& c = cli(cuenta);
    QPointer<YouTubeClient> cp(&c);
    QUrlQuery q;
    q.addQueryItem("part", "status,contentDetails");
    q.addQueryItem("id", QString::fromStdString(vivo));
    c.api("GET", "/liveBroadcasts", q, {}, [cp, vivo, listo](bool ok, const QJsonObject& d, const QString& e) {
        if (!ok) return listo(Resultado::mal(e.toStdString()));
        const QJsonObject b = d.value("items").toArray().at(0).toObject();
        if (b.isEmpty()) return listo(Resultado::mal(ba::i18n::traducir("vivos", "Ese vivo ya no existe en YouTube.")));
        const QString vida = b.value("status").toObject().value("lifeCycleStatus").toString();
        const bool autoStart = b.value("contentDetails").toObject().value("enableAutoStart").toBool(true);
        const QString sid = b.value("contentDetails").toObject().value("boundStreamId").toString();
        if (sid.isEmpty() || !cp) return listo(Resultado::bien({{"estado_vivo", vida.toStdString()}}));
        QUrlQuery sq;
        sq.addQueryItem("part", "status");
        sq.addQueryItem("id", sid);
        cp->api("GET", "/liveStreams", sq, {}, [cp, vivo, vida, autoStart, listo](bool ok2, const QJsonObject& sd, const QString& e2) {
            if (!ok2) return listo(Resultado::mal(e2.toStdString()));
            const QJsonObject st = sd.value("items").toArray().at(0).toObject().value("status").toObject();
            const QString estado = st.value("streamStatus").toString();
            const QString salud = st.value("healthStatus").toObject().value("status").toString();
            // Un vivo sin inicio automático: si YouTube ya recibe la señal, pasa a en vivo
            // (lo mismo que hace la página al transmitir desde la app).
            if (cp && !autoStart && estado == "active" && (vida == "ready" || vida == "testing"))
                cp->transition(QString::fromStdString(vivo), "live", [](bool, const QJsonObject&, const QString&) {});
            listo(Resultado::bien(
                {{"estado_vivo", vida.toStdString()}, {"estado_stream", estado.toStdString()}, {"salud", salud.toStdString()}}));
        });
    });
}

// --- Twitch --------------------------------------------------------------------------------

TwitchVivo::TwitchVivo(TwitchClient& principal, const QString& dir, QObject* parent)
    : PlataformaVivo(parent), c_(principal, dir, "twitch"), appAntes_(principal.clientId()) {
    connect(&principal, &TwitchClient::stateChanged, this, [this]() {
        if (c_.principal().clientId() != appAntes_) {
            appAntes_ = c_.principal().clientId();
            c_.cadaOtro([](TwitchClient& c) { c.recargar(); });
        }
        emit cambio();
    });
}

TwitchVivo::~TwitchVivo() = default;

TwitchClient& TwitchVivo::cli(const std::string& cuenta) const {
    auto* yo = const_cast<TwitchVivo*>(this);
    bool nuevo = false;
    TwitchClient& c = c_.de(cuenta, &nuevo);
    if (nuevo) connect(&c, &TwitchClient::stateChanged, yo, &PlataformaVivo::cambio);
    return c;
}

bool TwitchVivo::configurada() const { return c_.principal().hasClient(); }
bool TwitchVivo::conectada(const std::string& cuenta) const { return cli(cuenta).loggedIn(); }
bool TwitchVivo::conectando(const std::string& cuenta) const { return cli(cuenta).loginPending(); }
std::string TwitchVivo::canal(const std::string& cuenta) const { return s(cli(cuenta).user().value("display_name")); }

void TwitchVivo::conectar(const std::string& cuenta, Listo listo) {
    TwitchClient& c = cli(cuenta);
    QPointer<TwitchClient> cp(&c);
    c.login([cp, listo](bool ok, const QJsonObject&, const QString& e) {
        if (!ok) return listo(Resultado::mal(e.toStdString()));
        listo(Resultado::bien({{"canal", cp ? s(cp->user().value("display_name")) : std::string()}}));
    });
}

void TwitchVivo::cancelarConexion(const std::string& cuenta) { cli(cuenta).cancelLogin(); }

void TwitchVivo::desconectar(const std::string& cuenta) {
    cli(cuenta).logout([](bool, const QJsonObject&, const QString&) {});
}

void TwitchVivo::olvidar(const std::string& cuenta) {
    if (cuenta.empty() || cuenta == kPrincipal) return;
    c_.olvidar(cuenta);
}

void TwitchVivo::vivos(const std::string& cuenta, Listo listo) {
    // En Twitch el vivo es el canal: uno solo, con su título.
    TwitchClient& c = cli(cuenta);
    QPointer<TwitchClient> cp(&c);
    c.channel([cp, listo](bool ok, const QJsonObject& ch, const QString& e) {
        if (!ok) return listo(Resultado::mal(e.toStdString()));
        const std::string login = cp ? s(cp->user().value("login")) : std::string();
        listo(Resultado::bien({{"items",
                                json::array({{{"id", ""},
                                              {"titulo", s(ch.value("title"))},
                                              {"estado", "canal"},
                                              {"privacidad", "public"},
                                              {"inicio", ""},
                                              {"url", login.empty() ? std::string() : "https://www.twitch.tv/" + login}}})}}));
    });
}

void TwitchVivo::crearVivo(const std::string& cuenta, const json& datos, Listo listo) {
    // No hay vivos aparte: «crear» le pone el título al canal (y la categoría, si viene).
    QJsonObject cambios;
    if (datos.contains("titulo")) cambios.insert("title", QString::fromStdString(datos.value("titulo", std::string())));
    if (datos.contains("categoria")) cambios.insert("game", QString::fromStdString(datos.value("categoria", std::string())));
    const std::string titulo = datos.value("titulo", std::string());
    cli(cuenta).updateChannel(cambios, alListo(std::move(listo), [titulo](const QJsonObject&) {
        return json{{"vivo", {{"id", ""}, {"titulo", titulo}, {"estado", "canal"}}}};
    }));
}

void TwitchVivo::ingesta(const std::string& cuenta, const std::string&, Listo listo) {
    cli(cuenta).ingest(alListo(std::move(listo), [](const QJsonObject& d) {
        return json{{"url", s(d.value("url"))}, {"clave", s(d.value("key"))}};
    }));
}

void TwitchVivo::salud(const std::string& cuenta, const std::string&, Listo listo) {
    cli(cuenta).streamStatus(alListo(std::move(listo), [](const QJsonObject& d) {
        const bool live = d.value("live").toBool();
        return json{{"estado_vivo", live ? "live" : "offline"}, {"espectadores", d.value("viewers").toInt()}};
    }));
}

// --- Facebook ------------------------------------------------------------------------------

FacebookVivo::FacebookVivo(FacebookClient& principal, const QString& dir, QObject* parent)
    : PlataformaVivo(parent), c_(principal, dir, "facebook"), appAntes_(principal.appId()) {
    connect(&principal, &FacebookClient::stateChanged, this, [this]() {
        if (c_.principal().appId() != appAntes_) {
            appAntes_ = c_.principal().appId();
            c_.cadaOtro([](FacebookClient& c) { c.recargar(); });
        }
        emit cambio();
    });
}

FacebookVivo::~FacebookVivo() = default;

FacebookClient& FacebookVivo::cli(const std::string& cuenta) const {
    auto* yo = const_cast<FacebookVivo*>(this);
    bool nuevo = false;
    FacebookClient& c = c_.de(cuenta, &nuevo);
    if (nuevo) connect(&c, &FacebookClient::stateChanged, yo, &PlataformaVivo::cambio);
    return c;
}

bool FacebookVivo::configurada() const { return c_.principal().hasClient(); }

bool FacebookVivo::conectada(const std::string& cuenta) const {
    const FacebookClient& c = cli(cuenta);
    // El token del usuario vence (unos 60 días): vencido cuenta como no conectada.
    return c.loggedIn() && (c.expiresAtMs() <= 0 || QDateTime::currentMSecsSinceEpoch() < c.expiresAtMs());
}

bool FacebookVivo::conectando(const std::string& cuenta) const { return cli(cuenta).loginPending(); }

std::string FacebookVivo::canal(const std::string& cuenta) const {
    const FacebookClient& c = cli(cuenta);
    const std::string pagina = s(c.page().value("name"));
    return pagina.empty() ? s(c.user().value("name")) : pagina;
}

void FacebookVivo::conectar(const std::string& cuenta, Listo listo) {
    FacebookClient& c = cli(cuenta);
    QPointer<FacebookClient> cp(&c);
    c.login([cp, listo](bool ok, const QJsonObject&, const QString& e) {
        if (!ok) return listo(Resultado::mal(e.toStdString()));
        std::string canal;
        if (cp) {
            canal = s(cp->page().value("name"));
            if (canal.empty()) canal = s(cp->user().value("name"));
        }
        listo(Resultado::bien({{"canal", canal}}));
    });
}

void FacebookVivo::cancelarConexion(const std::string& cuenta) { cli(cuenta).cancelLogin(); }

void FacebookVivo::desconectar(const std::string& cuenta) {
    cli(cuenta).logout([](bool, const QJsonObject&, const QString&) {});
}

void FacebookVivo::olvidar(const std::string& cuenta) {
    if (cuenta.empty() || cuenta == kPrincipal) return;
    c_.olvidar(cuenta);
}

void FacebookVivo::vivos(const std::string& cuenta, Listo listo) {
    cli(cuenta).liveVideos(alListo(std::move(listo), [](const QJsonObject& d) {
        json items = json::array();
        for (const QJsonValue& v : d.value("items").toArray()) {
            const QJsonObject o = v.toObject();
            const QString st = o.value("status").toString();
            // Los que todavía pueden recibir señal (en prueba, programados o en vivo).
            if (st != "UNPUBLISHED" && st != "SCHEDULED_UNPUBLISHED" && st != "LIVE" && st != "LIVE_NOW" && st != "SCHEDULED_LIVE")
                continue;
            items.push_back({{"id", s(o.value("id"))},
                             {"titulo", s(o.value("title"))},
                             {"estado", st.toLower().toStdString()},
                             {"privacidad", ""},
                             {"inicio", s(o.value("created"))},
                             {"url", s(o.value("url"))}});
        }
        return json{{"items", items}};
    }));
}

void FacebookVivo::crearVivo(const std::string& cuenta, const json& datos, Listo listo) {
    QJsonObject p;
    if (datos.contains("titulo")) p.insert("title", QString::fromStdString(datos.value("titulo", std::string())));
    if (datos.contains("descripcion")) p.insert("description", QString::fromStdString(datos.value("descripcion", std::string())));
    // Sin «publico» explícito queda en prueba (no publicado): se publica desde la página.
    p.insert("status", datos.value("privacidad", std::string()) == "public" ? "LIVE_NOW" : "UNPUBLISHED");
    const std::string titulo = datos.value("titulo", std::string());
    cli(cuenta).createLive(p, alListo(std::move(listo), [titulo](const QJsonObject& d) {
        return json{{"vivo", {{"id", s(d.value("id"))}, {"titulo", titulo}, {"estado", "unpublished"}}}};
    }));
}

void FacebookVivo::ingesta(const std::string& cuenta, const std::string& vivo, Listo listo) {
    cli(cuenta).liveStatus(QString::fromStdString(vivo), [listo](bool ok, const QJsonObject& d, const QString& e) {
        if (!ok) return listo(Resultado::mal(e.toStdString()));
        const QJsonObject ing = d.value("ingestion").toObject();
        if (ing.isEmpty())
            return listo(Resultado::mal(ba::i18n::traducir("vivos", "Facebook no da la ingesta de ese vivo (¿ya terminó?).")));
        listo(Resultado::bien({{"url", s(ing.value("url"))}, {"clave", s(ing.value("key"))}, {"estado_stream", s(d.value("status"))}}));
    });
}

void FacebookVivo::salud(const std::string& cuenta, const std::string& vivo, Listo listo) {
    cli(cuenta).liveStatus(QString::fromStdString(vivo), [listo](bool ok, const QJsonObject& d, const QString& e) {
        if (!ok) return listo(Resultado::mal(e.toStdString()));
        listo(Resultado::bien({{"estado_vivo", s(d.value("status")).empty() ? std::string() : d.value("status").toString().toLower().toStdString()},
                               {"espectadores", d.value("live_views").toInt()}}));
    });
}

} // namespace ba::app::vivos
