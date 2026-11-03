// src/app/sala/ResumenSala.cpp — ver ResumenSala.hpp

#include "sala/ResumenSala.hpp"

#include "sala/OradoresSala.hpp"

#include <QHash>
#include <QJsonDocument>
#include <QSet>
#include <QUrl>

#include <algorithm>
#include <cmath>

namespace ba::app::sala {



const QStringList& clavesEspera() {
    static const QStringList k{"uid", "name", "connected", "joinedMs", "lobbySlot", "talk", "info", "contact", "prueba", "entrando"};
    return k;
}
const QStringList& clavesAdentro() {
    static const QStringList k{"uid",     "name",       "slot",      "connected", "mediaUp", "mic",
                               "cam",     "screen",     "hand",      "admittedMs", "presenter", "speaker",
                               "info",    "contact",    "talk",      "prueba",     "semaforo",  "sinRetorno"};
    return k;
}
const QStringList& clavesArchivos() {
    static const QStringList k{"uid", "name", "kind", "mime", "bytes", "participantName", "path", "atMs", "url"};
    return k;
}
const QStringList& clavesOradores() {
    static const QStringList k{"uid",          "name",         "info",        "contact",       "speaker",
                               "position",     "allottedSec",  "onAirSinceMs", "usedMs",       "connected",
                               "slot",         "avatar",       "backstageSlot", "presenter",   "restanteMs",
                               "estadoTiempo", "textoTiempo",  "textoAsignado", "textoUsado"};
    return k;
}
const QStringList& clavesLugares() {
    static const QStringList k{"slot", "nombre", "pid", "info", "sinRetorno", "semaforo", "red"};
    return k;
}

namespace {
/// Una fila con solo `claves` (y el id) de `p`: lo demás (la red, el uso) no viaja a la vista.
QJsonObject soloClaves(const QJsonObject& p, const QStringList& claves) {
    QJsonObject f;
    f.insert("id", p.value("id"));
    for (const QString& k : claves)
        if (const auto it = p.constFind(k); it != p.constEnd()) f.insert(k, it.value());
    return f;
}
QString tiempo(double ms) { return QString::fromStdString(OradoresSala::textoTiempo(ms)); }
} // namespace

std::shared_ptr<const ResumenSala> ResumenSala::construir(const QJsonObject& s, const ResumenSala* ant, const Opciones& o) {
    auto r = std::make_shared<ResumenSala>();
    r->foto = s;
    const QByteArray bytes = QJsonDocument(s).toJson(QJsonDocument::Compact);
    r->json = std::make_shared<const nlohmann::json>(nlohmann::json::parse(bytes.constData(), bytes.constData() + bytes.size(), nullptr, false));
    r->nLugares = std::max(0, o.nLugares);

    const int maxG = s.value("maxGuests").toInt();
    for (int i = 0; i < maxG; ++i) r->slotNames << QString();
    r->chatTargets << QVariantMap{{"label", "A todos"}, {"value", "all"}};
    r->monitor.assign(9, false);

    // La red de los lugares: la nueva si ya pasó kRedCadaMs; si no, la que ya tenía cada
    // lugar (si sigue la misma persona).
    const bool refrescarRed = ant == nullptr || o.ahoraMs - ant->redMs >= kRedCadaMs;
    r->redMs = refrescarRed ? o.ahoraMs : ant->redMs;
    QHash<int, QJsonObject> lugarAnterior;
    if (!refrescarRed)
        for (const QJsonValue& v : ant->lugares) lugarAnterior.insert(v.toObject().value("slot").toInt(), v.toObject());

    const QJsonArray participantes = s.value("participants").toArray();
    QHash<QString, QJsonObject> porId;
    QList<QJsonObject> adentro;
    QHash<int, QJsonObject> porLugar;
    int conectados = 0, conVideo = 0, enSala = 0;
    for (const QJsonValue& v : participantes) {
        const QJsonObject p = v.toObject();
        const QString id = p.value("id").toString();
        const QString nombre = p.value("name").toString();
        const QString estado = p.value("state").toString();
        const int slot = p.value("slot").toInt();
        const QJsonObject red = p.value("red").toObject();
        porId.insert(id, p);

        QJsonObject g = p; // quién está y cómo, sin lo que cambia siempre
        g.remove("red");
        g.remove("usedMs");
        r->gente.append(g);
        r->participantesBus.push_back({{"id", id.toStdString()},
                                       {"nombre", nombre.toStdString()},
                                       {"estado", estado.toStdString()},
                                       {"mic", p.value("mic").toBool()},
                                       {"cam", p.value("cam").toBool()},
                                       {"mano", p.value("hand").toBool()}});
        r->chatTargets << QVariantMap{{"label", "A " + nombre + (estado == "waiting" ? " (lobby)" : "")}, {"value", id}};
        if (p.value("connected").toBool()) {
            ++conectados;
            if (p.value("mediaUp").toBool() && p.value("cam").toBool()) ++conVideo;
            if (estado == "admitted") ++enSala;
        }
        if (estado == "admitted" && slot >= 1 && slot <= r->slotNames.size()) r->slotNames[slot - 1] = nombre;

        QJsonObject fila = p;
        fila.insert("uid", id); // "id" no puede ser un rol en QML
        // Lo que la grilla de la sala muestra como íconos: el semáforo de su red (solo el
        // color: las cifras cambian en cada foto) y si su retorno está apagado.
        fila.insert("semaforo", red.value("semaforo").toString());
        fila.insert("sinRetorno", p.value("retorno").toString() == "apagado");
        if (estado == "waiting") {
            r->espera.append(soloClaves(fila, clavesEspera()));
            // Monitoreo: solo se escucha (por auriculares) a quien se le está hablando.
            const int k = p.value("lobbySlot").toInt();
            if (k >= 1 && k <= 8 && p.value("talk").toBool()) r->monitor[static_cast<std::size_t>(k)] = true;
        } else if (slot > 0 || p.value("speaker").toString() == "none") {
            // En el programa; o admitido, sin lugar y fuera de la fila de oradores: igual está
            // en la sala y conectado (que nunca quede alguien invisible para el operador).
            adentro << soloClaves(fila, clavesAdentro());
        }
        if (estado == "admitted" && slot >= 1 && slot <= r->nLugares) {
            QJsonObject l{{"id", slot},
                          {"slot", slot},
                          {"nombre", nombre},
                          {"pid", id},
                          {"info", p.value("info").toString()},
                          {"sinRetorno", p.value("retorno").toString() == "apagado"},
                          {"semaforo", red.value("semaforo").toString()},
                          {"red", red}};
            if (!refrescarRed) {
                const QJsonObject a = lugarAnterior.value(slot);
                if (a.value("pid").toString() == id) l.insert("red", a.value("red"));
            }
            porLugar.insert(slot, l);
        }
    }
    r->conectados = static_cast<int>(participantes.size());
    // Adentro, ordenados por lugar (coincide con «Invitado N» de las fuentes).
    std::stable_sort(adentro.begin(), adentro.end(),
                     [](const QJsonObject& a, const QJsonObject& b) { return a.value("slot").toInt() < b.value("slot").toInt(); });
    for (const QJsonObject& f : adentro) r->adentro.append(f);
    for (int k = 1; k <= r->nLugares; ++k)
        r->lugares.append(porLugar.contains(k) ? porLugar.value(k)
                                               : QJsonObject{{"id", k},
                                                             {"slot", k},
                                                             {"nombre", ""},
                                                             {"pid", ""},
                                                             {"info", ""},
                                                             {"sinRetorno", false},
                                                             {"semaforo", ""},
                                                             {"red", QJsonObject{}}});
    r->gente.append(QJsonObject{{"order", s.value("order")}});

    // Los archivos: lo más nuevo arriba.
    const QJsonArray todos = s.value("shares").toArray();
    for (qsizetype i = todos.size() - 1; i >= 0; --i) {
        QJsonObject f = todos.at(i).toObject();
        f.insert("url", QUrl::fromLocalFile(f.value("path").toString()).toString());
        f.insert("uid", f.value("id"));
        r->archivos.append(soloClaves(f, clavesArchivos()));
    }

    // La fila de oradores en orden: la misma que da remotos_fila_ver (OradoresSala::fila).
    if (r->json->is_object())
        for (const nlohmann::json& f : OradoresSala::fila(*r->json)) {
            const QString id = QString::fromStdString(f.value("id", std::string()));
            QJsonObject p = porId.value(id);
            p.insert("uid", id);
            p.insert("position", f.value("position", 0));
            p.insert("avatar", o.avatar ? o.avatar(id) : QString());
            p.insert("textoAsignado", tiempo(p.value("allottedSec").toInt() * 1000.0));
            p.insert("textoUsado", tiempo(p.value("usedMs").toDouble()));
            if (p.value("speaker").toString() == "onair")
                r->onAir << QVariantMap{{"uid", id},
                                        {"name", p.value("name").toString()},
                                        {"allottedSec", p.value("allottedSec").toInt()},
                                        {"onAirSinceMs", p.value("onAirSinceMs").toDouble()}};
            r->oradores.append(soloClaves(p, clavesOradores()));
        }

    r->firmaSala = {s.value("room").toString(),      s.value("pin").toString(),     s.value("guestUrl").toString(),
                    s.value("lobby").toBool(true),   s.value("locked").toBool(),    maxG,
                    s.value("slotsFree").toInt(),    s.value("entrando").toInt(),   s.value("entradaPorSegundo").toInt()};
    r->speakerCfg = s.value("speakers").toObject().toVariantMap();
    r->speakerCfg["defaultTexto"] = tiempo(r->speakerCfg.value("defaultSec").toInt() * 1000.0);
    r->speakerMode = s.value("speakers").toObject().value("enabled").toBool();

    r->uso = s.value("uso").toObject().toVariantMap();
    r->uso["conectados"] = conectados;
    r->uso["conVideo"] = conVideo;
    r->uso["enSala"] = enSala;
    r->uso["lugares"] = maxG;
    if (s.contains("serverNowMs")) {
        const double off = s.value("serverNowMs").toDouble() - static_cast<double>(o.ahoraMs);
        r->clockOffset = ant != nullptr && std::abs(off - ant->clockOffset) < kRelojTolMs ? ant->clockOffset : off;
    } else if (ant != nullptr) {
        r->clockOffset = ant->clockOffset;
    }

    r->chat = s.value("chat").toArray();
    r->ultimoSeq = r->chat.isEmpty() ? 0 : r->chat.last().toObject().value("seq").toInteger();
    r->directa = s.value("directa").toBool();
    return r;
}

std::shared_ptr<PaqueteSala> PaqueteSala::diferencia(const std::shared_ptr<const ResumenSala>& base,
                                                     const std::shared_ptr<const ResumenSala>& r) {
    auto p = std::make_shared<PaqueteSala>();
    p->r = r;
    p->primera = base == nullptr;
    static const ResumenSala vacio;
    const ResumenSala& b = base ? *base : vacio;
    unsigned& x = p->partes;
    const auto plan = [&](PlanFilas& destino, const QJsonArray& antes, const QJsonArray& ahora, const QStringList& claves,
                          unsigned parte) {
        destino = JsonListModel::planear(antes, ahora, claves);
        if (!destino.vacio()) x |= parte;
    };
    plan(p->espera, b.espera, r->espera, clavesEspera(), Espera);
    plan(p->adentro, b.adentro, r->adentro, clavesAdentro(), Adentro);
    plan(p->archivos, b.archivos, r->archivos, clavesArchivos(), Archivos);
    plan(p->oradores, b.oradores, r->oradores, clavesOradores(), Oradores);
    plan(p->lugares, b.lugares, r->lugares, clavesLugares(), Lugares);
    if (base == nullptr) x = Todo;
    if (r->onAir != b.onAir) x |= Oradores;
    if (r->gente != b.gente) x |= Gente;
    if (r->firmaSala != b.firmaSala) x |= Sala;
    if (r->speakerCfg != b.speakerCfg || r->speakerMode != b.speakerMode) x |= OradoresCfg;
    if (r->uso != b.uso || r->clockOffset != b.clockOffset) x |= Uso;
    if (r->monitor != b.monitor) x |= Monitor;
    if (r->conectados != b.conectados || r->slotNames != b.slotNames || r->chatTargets != b.chatTargets) x |= Participantes;
    if (r->directa != b.directa) x |= Directa;

    // El chat: lo que no tenía (por número de secuencia). Si la secuencia volvió atrás, B se
    // reinició y el historial se reemplaza.
    p->chatReinicio = base != nullptr && r->ultimoSeq < b.ultimoSeq;
    const qint64 desde = p->chatReinicio ? 0 : b.ultimoSeq;
    for (const QJsonValue& v : r->chat)
        if (v.toObject().value("seq").toInteger() > desde) p->chatNuevos.append(v);
    if (p->chatReinicio || !p->chatNuevos.isEmpty()) x |= Chat;

    // Los que llegaron al lobby (para avisar y para la atención automática).
    if ((x & Espera) != 0) {
        QSet<QString> estaban;
        for (const QJsonValue& v : b.espera) estaban.insert(v.toObject().value("id").toString());
        for (const QJsonValue& v : r->espera) {
            const QJsonObject e = v.toObject();
            if (!estaban.contains(e.value("id").toString()))
                p->llegaronAlLobby << QJsonObject{{"id", e.value("id")}, {"name", e.value("name")}, {"prueba", e.value("prueba")}};
        }
    }
    return p;
}

// --- El buzón ------------------------------------------------------------------------------

bool BuzonSala::depositar(const QJsonObject& foto, std::int64_t ahoraMs) {
    bool nueva = false;
    {
        std::lock_guard<std::mutex> l(mu_);
        ++cuenta_.depositadas;
        // El primero de cada conexión dice de qué arranque es la B y si pudo reenviar lo
        // perdido (los que siguen son solo la foto): es de un solo disparo, se acumula en
        // lo que espera hasta que la interfaz lo toma.
        if (foto.contains("epoch")) {
            const QString epoch = foto.value("epoch").toString();
            nueva = epoch != epoch_ || !foto.value("reanudado").toBool();
            epoch_ = epoch;
        }
    }
    return poner(foto, ahoraMs, nueva);
}

bool BuzonSala::rehacer(std::int64_t ahoraMs) {
    QJsonObject foto;
    {
        std::lock_guard<std::mutex> l(mu_);
        if (!ultimo_) return false;
        foto = ultimo_->foto;
    }
    return poner(foto, ahoraMs, false);
}

bool BuzonSala::poner(const QJsonObject& foto, std::int64_t ahoraMs, bool nueva) {
    ResumenSala::Opciones o;
    std::shared_ptr<const ResumenSala> anterior;
    {
        std::lock_guard<std::mutex> l(mu_);
        o.nLugares = nLugares_;
        o.avatar = avatar_;
        anterior = ultimo_;
    }
    o.ahoraMs = ahoraMs;
    // Lo caro, sin el candado (la interfaz puede tomar mientras tanto).
    std::shared_ptr<const ResumenSala> r = ResumenSala::construir(foto, anterior.get(), o);
    for (;;) {
        std::shared_ptr<const ResumenSala> base;
        {
            std::lock_guard<std::mutex> l(mu_);
            base = base_;
        }
        std::shared_ptr<PaqueteSala> p = PaqueteSala::diferencia(base, r);
        std::lock_guard<std::mutex> l(mu_);
        // Si la interfaz tomó mientras se calculaba, la base es otra: se recalcula.
        if (base_ != base) continue;
        ultimo_ = r;
        nuevaPendiente_ = nuevaPendiente_ || nueva;
        p->nueva = nuevaPendiente_;
        p->epoch = epoch_;
        p->numero = cuenta_.depositadas;
        const bool avisar = pendiente_ == nullptr;
        if (!avisar) ++cuenta_.descartadas; // la última gana
        pendiente_ = std::move(p);
        return avisar;
    }
}

std::shared_ptr<const PaqueteSala> BuzonSala::tomar() {
    std::lock_guard<std::mutex> l(mu_);
    if (!pendiente_) return nullptr;
    std::shared_ptr<PaqueteSala> p = std::move(pendiente_);
    pendiente_.reset();
    base_ = p->r;
    if (p->nueva) nuevaPendiente_ = false; // ya va en este
    ++cuenta_.tomadas;
    return p;
}

std::shared_ptr<const ResumenSala> BuzonSala::ultimo() const {
    std::lock_guard<std::mutex> l(mu_);
    return ultimo_;
}

void BuzonSala::setLugares(int n) {
    std::lock_guard<std::mutex> l(mu_);
    nLugares_ = n;
}

int BuzonSala::lugares() const {
    std::lock_guard<std::mutex> l(mu_);
    return nLugares_;
}

void BuzonSala::setAvatar(std::function<QString(const QString&)> fn) {
    std::lock_guard<std::mutex> l(mu_);
    avatar_ = std::move(fn);
}

BuzonSala::Cuenta BuzonSala::cuenta() const {
    std::lock_guard<std::mutex> l(mu_);
    return cuenta_;
}

} // namespace ba::app::sala
