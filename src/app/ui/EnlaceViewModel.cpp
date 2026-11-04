// src/app/ui/EnlaceViewModel.cpp — la velocidad del enlace (ver el .hpp)

#include "ui/EnlaceViewModel.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>

#include <algorithm>

#include "i18n/I18nTexto.hpp"
#include "ui/ConferenceClient.hpp"
#include "ui/EventoViewModel.hpp"
#include "ui/GuionViewModel.hpp"

namespace ba::app {

namespace en = sala::enlace;
using json = nlohmann::json;

namespace {
QVariantMap aMapa(const json& j) {
    return QJsonDocument::fromJson(QByteArray::fromStdString(j.dump(-1, ' ', false, json::error_handler_t::replace)))
        .object()
        .toVariantMap();
}
json deQJson(const QJsonObject& o) {
    return json::parse(QJsonDocument(o).toJson(QJsonDocument::Compact).toStdString(), nullptr, false);
}
QString qs(const std::string& s) { return QString::fromStdString(s); }
constexpr const char* kItemChequeo = "enlace";
} // namespace

EnlaceViewModel::EnlaceViewModel(Partes p, QObject* parent) : QObject(parent), p_(std::move(p)) {
    connect(&p_.sala, &ConferenceClient::estadoEvento, this, [this](const QString& nombre, const QJsonObject& ev) {
        if (nombre == QLatin1String("enlace")) aplicar(deQJson(ev));
    });
    // Al conectar con la sala (y si se reinicia): el estado entero, una vez.
    connect(&p_.sala, &ConferenceClient::onlineChanged, this, [this](bool on) {
        if (!on) {
            aplicar(json{{"estado", "sin_sala"}});
            return;
        }
        QPointer<EnlaceViewModel> yo(this);
        p_.sala.get("/api/v1/enlace", [yo](bool ok, const QJsonObject& r) {
            if (yo && ok) yo->aplicar(deQJson(r));
        });
    });
    // El cupo (la cuenta de fábrica) y quién está conectado (el riesgo): la foto de la sala.
    // Solo cuando cambia el cupo o la gente (no la red de cada invitado, en cada foto).
    connect(&p_.sala, &ConferenceClient::salaCambio, this, [this](unsigned partes) {
        if ((partes & (sala::Sala | sala::Gente)) != 0) revisar();
    });
    if (p_.sala.online()) {
        QPointer<EnlaceViewModel> yo(this);
        p_.sala.get("/api/v1/enlace", [yo](bool ok, const QJsonObject& r) {
            if (yo && ok) yo->aplicar(deQJson(r));
        });
    }
    revisar();
}

en::Perfil EnlaceViewModel::perfil(int invitados) const {
    en::Perfil pf;
    pf.invitados = invitados;
    pf.altaHasta = p_.altaHasta ? p_.altaHasta() : 10;
    pf.altaMax = p_.altaMax ? p_.altaMax() : 4;
    return pf;
}

en::Medicion EnlaceViewModel::medido() const { return en::deResultado(buenaJson_); }

json EnlaceViewModel::necesidadPara(int invitados) const {
    const en::Perfil pf = perfil(invitados);
    return en::aJson(pf, en::necesita(pf), en::juzgar(pf, medido()));
}

json EnlaceViewModel::juzgarCon(const en::Medicion& m, int invitados) const {
    const en::Perfil pf = perfil(invitados);
    return en::aJson(pf, en::necesita(pf), en::juzgar(pf, m));
}

void EnlaceViewModel::setInvitados(int n) {
    n = std::clamp(n, 1, en::kMaxInvitados);
    invitadosAMano_ = true;
    if (n == invitados_) return;
    invitados_ = n;
    recalcular();
}

void EnlaceViewModel::recalcular() {
    const QVariantMap nueva = aMapa(necesidadPara(invitados_));
    if (nueva == necesidad_) return;
    necesidad_ = nueva;
    emit necesidadChanged();
}

void EnlaceViewModel::revisar() {
    const QJsonObject snap = p_.sala.snapshot();
    if (!invitadosAMano_) {
        const int cupo = snap.value("maxGuests").toInt();
        if (cupo > 0) invitados_ = std::min(cupo, en::kMaxInvitados);
        else if (invitados_ <= 0) invitados_ = 10;
    }
    int conectados = 0;
    for (const QJsonValue& v : snap.value("participants").toArray()) {
        const QJsonObject o = v.toObject();
        if (o.value("connected").toBool() && !o.value("prueba").toBool()) ++conectados;
    }
    QString r;
    if (conectados > 0)
        r = tr("Hay %n invitados conectados: la medición carga el enlace y se les puede cortar la imagen unos segundos.", "",
               conectados);
    else if (p_.alAire && p_.alAire())
        r = tr("Hay algo al aire: la medición carga el enlace y puede cortar la salida unos segundos.");
    if (r != riesgo_) {
        riesgo_ = r;
        emit riesgoChanged();
    }
    recalcular();
}

QString EnlaceViewModel::medir(bool confirmado, int topeKbps, int tasaKbps) {
    using ba::i18n::traducir;
    if (!p_.sala.online()) return qs(traducir("sala", "la conferencia no está andando"));
    if (!hayTunel_) return qs(traducir("sala", "no hay túnel WireGuard configurado: la medición va por el túnel hasta el servidor"));
    if (midiendo()) return qs(traducir("sala", "ya hay una medición del enlace en curso"));
    if (p_.pruebaCorriendo && p_.pruebaCorriendo())
        return qs(traducir("sala", "hay una prueba de sala corriendo: medí el enlace antes o después"));
    revisar();
    if (!riesgo_.isEmpty() && !confirmado) return riesgo_;
    QJsonObject pedido;
    int tope = topeKbps > 0 ? topeKbps : 100000;
    if (!riesgo_.isEmpty()) {
        // En vivo: suave (poco tope, escalones cortos) para no cortar a nadie.
        tope = std::min(tope, kTopeEnVivoKbps);
        tasaKbps = tasaKbps > 0 ? std::min(tasaKbps, kTopeEnVivoKbps) : 0;
        pedido.insert("escalonMs", 1000);
    }
    pedido.insert("topeSubidaKbps", tope);
    pedido.insert("topeBajadaKbps", tope);
    if (tasaKbps > 0) pedido.insert("tasaKbps", tasaKbps);
    QPointer<EnlaceViewModel> yo(this);
    p_.sala.post("/api/v1/enlace/medir", pedido, [yo](bool ok, const QJsonObject& r) {
        if (!yo) return;
        if (!ok || !r.value("ok").toBool())
            emit yo->notify(tr("No arrancó la medición del enlace: %1").arg(r.value("error").toString()), "error");
    });
    return {};
}

void EnlaceViewModel::cancelar() {
    if (midiendo()) p_.sala.post("/api/v1/enlace/cancelar");
}

void EnlaceViewModel::pedirInstalador() {
    QPointer<EnlaceViewModel> yo(this);
    p_.sala.get("/api/v1/enlace/instalador", [yo](bool ok, const QJsonObject& r) {
        if (!yo) return;
        if (!ok || !r.value("ok").toBool()) {
            const QString e = r.value("error").toString();
            emit yo->instaladorListo({}, e.isEmpty() ? tr("La conferencia no contestó.") : e);
            return;
        }
        emit yo->instaladorListo(r.value("script").toString(), {});
    });
}

void EnlaceViewModel::aplicar(const json& e) {
    if (!e.is_object()) return;
    const QString estado = qs(e.value("estado", std::string("sin_sala")));
    const bool estabaMidiendo = midiendo();
    estado_ = estado;
    hayTunel_ = e.value("hayTunel", false);
    eco_ = qs(e.value("eco", std::string()));
    fase_ = qs(e.value("fase", std::string()));
    avance_ = e.value("avance", 0.0);
    const json ult = e.value("ultimo", json());
    const bool otraUltima = ult.is_object() && ult.value("fecha", 0.0) != fechaUltima_;
    if (ult.is_object()) {
        ultimaJson_ = ult;
        // Una cancelada o fallida no borra lo que se sabe: la cuenta sigue con la última buena.
        if (ult.value("ok", false)) buenaJson_ = ult;
        ultima_ = aMapa(ult);
    }
    // Mientras mide, lo parcial; si no, la última buena (el error de otra, en `ultima`).
    json m = midiendo() ? e : (buenaJson_.is_object() ? buenaJson_ : json::object());
    m.erase("ultimo");
    medicion_ = aMapa(m);
    emit medicionChanged();
    if (otraUltima) {
        fechaUltima_ = ult.value("fecha", 0.0);
        recalcular();
        if (estabaMidiendo) alTerminar();
    }
}

void EnlaceViewModel::alTerminar() {
    const json& u = ultimaJson_;
    if (u.value("cancelado", false)) {
        emit notify(tr("Medición del enlace cancelada."), "warn");
        emit termino();
        return;
    }
    if (!u.value("ok", false)) {
        emit notify(tr("La medición del enlace no se pudo hacer: %1").arg(qs(u.value("error", std::string()))), "error");
        emit termino();
        return;
    }
    const QString veredicto = necesidad_.value("veredicto").toString();
    const double sube = u.value("subida", json::object()).value("capacidadKbps", 0.0) / 1000.0;
    const double baja = u.value("bajada", json::object()).value("capacidadKbps", 0.0) / 1000.0;
    const int conMargen = necesidad_.value("entranConMargen").toInt();
    const QString nota = tr("Subida %1 Mbps, bajada %2 Mbps, RTT %3 ms: para %n invitados %4 (entran hasta %5 con margen).", "",
                            invitados_)
                             .arg(sube, 0, 'f', 1)
                             .arg(baja, 0, 'f', 1)
                             .arg(u.value("reposo", json::object()).value("rttMs", -1.0), 0, 'f', 0)
                             .arg(veredicto == QLatin1String("entra")   ? tr("entra")
                                  : veredicto == QLatin1String("justo") ? tr("está justo")
                                                                        : tr("no entra"))
                             .arg(conMargen);
    // A la lista de chequeo del evento abierto (si hay).
    if (p_.evento.eventoAbierto().hay())
        p_.evento.guionVm().marcarChequeo(QString::fromLatin1(kItemChequeo), veredicto == QLatin1String("entra"), nota,
                                          tr("Enlace medido por el túnel (subida, bajada, demora)"));
    emit notify(nota, veredicto == QLatin1String("entra") ? "ok" : veredicto == QLatin1String("justo") ? "warn" : "error");
    emit termino();
}

json EnlaceViewModel::estadoJson() const {
    json j{{"estado", estado_.toStdString()},
           {"hay_tunel", hayTunel_},
           {"eco", eco_.toStdString()},
           {"riesgo", riesgo_.toStdString()},
           {"necesidad", necesidadPara(invitados_)}};
    if (midiendo()) {
        j["fase"] = fase_.toStdString();
        j["avance"] = avance_;
        j["parcial"] = deQJson(QJsonObject::fromVariantMap(medicion_));
    }
    if (ultimaJson_.is_object()) j["ultima"] = ultimaJson_;
    if (buenaJson_.is_object() && buenaJson_ != ultimaJson_) j["ultima_buena"] = buenaJson_;
    return j;
}

} // namespace ba::app
