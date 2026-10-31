// src/app/ui/AsambleaViewModel.cpp — ver el .hpp

#include "ui/AsambleaViewModel.hpp"

#include "arranque/AppRuntime.hpp"
#include "asamblea/Bancas.hpp"
#include "asamblea/ConferenciaDcn.hpp"
#include "asamblea/Cuerpo.hpp"
#include "asamblea/CuerpoViewModel.hpp"
#include "asamblea/Debate.hpp"
#include "asamblea/InformesResultado.hpp"
#include "asamblea/OrdenDelDia.hpp"
#include "asamblea/Presidencia.hpp"
#include "asamblea/Quorum.hpp"
#include "asamblea/SeguirMicrofono.hpp"
#include "asamblea/SesionRemota.hpp"
#include "asamblea/Votaciones.hpp"
#include "control/BusEventos.hpp"
#include "escenas/Conmutador.hpp"
#include "evento/APdf.hpp"
#include "i18n/I18n.hpp"
#include "ptz/ControlCamaras.hpp"
#include "sala/Direcciones.hpp"
#include "ui/ConferenceClient.hpp"
#include "ui/ConferenceViewModel.hpp"
#include "ui/EventoViewModel.hpp"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QFile>
#include <QGuiApplication>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QPointer>
#include <QQmlEngine>

#include <algorithm>
#include <map>
#include <set>

namespace ba::app {

namespace {

/// Un hijo objeto, o {} si falta o no es un objeto (value() sobre null tira una excepción).
nlohmann::json hijoObjeto(const nlohmann::json& j, const char* k) {
    return j.is_object() && j.contains(k) && j[k].is_object() ? j[k] : nlohmann::json::object();
}

/// Sin padre y devuelto a QML: que no se lo quede su recolector. Es del área.
QObject* deCpp(QObject* o) {
    if (o != nullptr) QQmlEngine::setObjectOwnership(o, QQmlEngine::CppOwnership);
    return o;
}

} // namespace

AsambleaViewModel::AsambleaViewModel(Partes p, QObject* parent) : QObject(parent), p_(std::move(p)) {
    cuerpo_ = std::make_unique<asamblea::CuerpoViewModel>(p_.rt.dataRoot());
    armarBancas();
    armarSesion();
    // La central de conferencia (plugin bosch-dcn): su estado sale del bus.
    dcn_ = std::make_unique<asamblea::ConferenciaDcn>(p_.plugins, p_.bus, p_.rt.dataRoot());
    // Los presentes del quórum salen del padrón (o de la asistencia de la central): cada
    // vez que cambian, el quórum avisa, así se recalcula mientras se marca quién está.
    if (p_.evento) connect(p_.evento, &EventoViewModel::padronChanged, quorum_.get(), &asamblea::Quorum::cambio);
    connect(dcn_.get(), &asamblea::ConferenciaDcn::cambio, quorum_.get(), &asamblea::Quorum::cambio);
    armarInformes();
    // Las votaciones (central, celulares, Quórum) son del modelo.
    votaciones_ = std::make_unique<asamblea::Votaciones>(*sesionRemota_, *dcn_, *quorum_, p_.bus, this);
    // El Cuerpo cambió (nombres, fotos, unidades de la central): al bus, así se
    // redibuja la botonera y el plugin de la central relee su mapa.
    connect(cuerpo_.get(), &asamblea::CuerpoViewModel::cambio, this, [this]() {
        p_.bus.publicar("cuerpo/cambio", {{"ms", QDateTime::currentMSecsSinceEpoch()}}, true);
    });
    // La banca virtual: el gateway autentica y avisa; decide asamblea::Presidencia (con
    // las consolas de la presidencia y la prioridad del presidente). Acá solo se muestra.
    connect(sesionRemota_.get(), &asamblea::SesionRemota::aviso, this,
            [this](const QString& t, const QString& nivel) { emit notify(t, nivel); });
    connect(sesionRemota_.get(), &asamblea::SesionRemota::cambio, this, [this]() { emit quorum_->cambio(); });
    asamblea::Presidencia::Partes pp{.sesion = *sesionRemota_,
                                     .bancas = *bancas_,
                                     .debate = debate_.get(),
                                     .dcn = *dcn_,
                                     .votaciones = votaciones_.get(),
                                     .cuerpo = *cuerpo_,
                                     .quorum = quorum_.get(),
                                     .ordenDia = ordenDia_.get(),
                                     .sala = p_.sala.client(),
                                     .bus = p_.bus,
                                     .motor = p_.rt.engine(),
                                     .microfonoOperador = [this]() { return !p_.sala.operatorAudio().isEmpty(); }};
    presidencia_ = std::make_unique<asamblea::Presidencia>(std::move(pp), this);
    connect(presidencia_.get(), &asamblea::Presidencia::aviso, this,
            [this](const QString& t, const QString& nivel) { emit notify(t, nivel); });
    connect(presidencia_.get(), &asamblea::Presidencia::presidenciaHablaCambio, this,
            &AsambleaViewModel::presidenciaHablaChanged);
    // La fuente «Banca con la palabra»: lo que muestra cada ranura sigue a la fila y al debate.
    ranurasTarde_.setSingleShot(true);
    ranurasTarde_.setInterval(1500);
    connect(&ranurasTarde_, &QTimer::timeout, this, [this]() { actualizarRanurasBanca(true); });
    const auto ranuras = [this]() {
        actualizarRanurasBanca();
        ranurasTarde_.start();
    };
    connect(bancas_.get(), &asamblea::Bancas::cambio, this, ranuras);
    connect(debate_.get(), &asamblea::Debate::cambio, this, ranuras);
    connect(cuerpo_.get(), &asamblea::CuerpoViewModel::cambio, this, ranuras);
    connect(&p_.camaras, &ptz::ControlCamaras::cambio, this, ranuras);
    QTimer::singleShot(0, this, ranuras);
}

AsambleaViewModel::~AsambleaViewModel() = default;

void AsambleaViewModel::armarBancas() {
    // Las bancas presenciales de la fila de oradores (asamblea/Bancas).
    asamblea::Bancas::Ganchos g;
    // La regla del interlock es la de ControlCamaras (respeta su ajuste), no solo «está al aire».
    g.camaraAlAire = [this](const std::string& c) {
        return !p_.camaras.chequearMovimiento(QString::fromStdString(c)).isEmpty();
    };
    g.irAPosicion = [this](const std::string& c, const std::string& pos) {
        const QVariantMap r = p_.camaras.irAPosicion(QString::fromStdString(c), QString::fromStdString(pos), -1);
        return r.value("ok").toBool() ? std::string() : r.value("error").toString().toStdString();
    };
    g.escenaDe = [this](const std::string& camara) {
        // La primera escena que muestra esa cámara (que no sea la del aire si hay otra).
        std::string otra;
        for (const std::string& sc : p_.rt.engine().sceneNames()) {
            const auto ss = p_.rt.engine().sceneSources(sc);
            if (std::find(ss.begin(), ss.end(), camara) == ss.end()) continue;
            if (sc != p_.rt.engine().currentScene()) return sc;
            otra = sc;
        }
        return otra;
    };
    g.armar = [this](const std::string& e) { p_.conmutador.armar(e); };
    g.cortar = [this](const std::string& e) { p_.conmutador.poner(e); };
    g.microfono = [this](const std::string& f, bool abierto) { p_.rt.engine().setSourceMuted(f, !abierto); };
    g.microfonoBanca = [this](const std::string& banca, bool abierto, bool terminar) {
        // Por la central solo si está conectada y la banca tiene su unidad; si
        // no, sigue valiendo la fuente de audio de la banca (la de arriba).
        if (!dcn_ || !dcn_->conectada() || !dcn_->tieneUnidad(banca)) return false;
        QPointer<AsambleaViewModel> yo(this);
        dcn_->control("microfono", {{"banca", banca}, {"abierto", abierto}, {"quitar", terminar}, {"quien", "fila de oradores"}},
                      [yo, banca](bool ok, const nlohmann::json&, const std::string& e) {
                          if (!ok && yo)
                              emit yo->notify(tr("Central DCN, banca %1: %2").arg(QString::fromStdString(banca), QString::fromStdString(e)),
                                              "error");
                      });
        return true;
    };
    // La fila única: los que hablan desde el navegador (sesión remota) también.
    g.esRemoto = [this](const std::string& id) {
        if (!sesionRemota_) return false;
        // El que tiene la palabra remota sigue siendo remoto aunque se le haya
        // cortado la red: si no, terminar no le soltaba los medios.
        if (sesionRemota_->tienePalabra(id)) return true;
        const auto v = sesionRemota_->presentesIds();
        return std::find(v.begin(), v.end(), id) != v.end();
    };
    g.palabraRemota = [this](const std::string& id, bool palabra, int segundos) {
        if (sesionRemota_) sesionRemota_->darPalabra(id, palabra, segundos);
    };
    g.filaCambio = [this](const std::vector<std::string>& cola, const std::string& alAire) {
        if (sesionRemota_) sesionRemota_->fila(cola, alAire);
    };
    // La fuente «Banca con la palabra»: si la toma es digital y hay escenas con la
    // fuente, ella la recorta de la toma entera (no se mueve nada).
    g.laEncuadraLaFuente = [this](const std::string& c, const std::string& pos) {
        if (!p_.rt.engine().hayFuenteBanca()) return false;
        const QVariantMap d = p_.camaras.datosPosicion(QString::fromStdString(c), QString::fromStdString(pos));
        return d.value("existe").toBool() && d.value("digital").toBool() && !d.value("motorizada").toBool();
    };
    g.escenaDeRanura = [this](int n) { return p_.rt.engine().escenaDeRanura(n); };
    g.posicionesConNombre = [this](const std::string& nombre) {
        std::vector<std::pair<std::string, std::string>> out;
        for (const QVariant& v : p_.camaras.buscarPosicion(QString::fromStdString(nombre))) {
            const QVariantMap m = v.toMap();
            out.emplace_back(m.value("entrada").toString().toStdString(), m.value("nombre").toString().toStdString());
        }
        return out;
    };
    bancas_ = std::make_unique<asamblea::Bancas>(cuerpo_->cuerpo(), g);
    // El debate entre dos: reusa lo de la fila (micrófonos, cámaras, escenas, remotos).
    asamblea::Debate::Ganchos gd;
    gd.esRemoto = g.esRemoto;
    gd.palabraRemota = g.palabraRemota;
    gd.canalRemoto = [this](const std::string& id) {
        if (!sesionRemota_) return 0;
        const auto v = sesionRemota_->conPalabraTodos();
        const auto it = std::find(v.begin(), v.end(), id);
        return it == v.end() ? 0 : static_cast<int>(it - v.begin()) + 1;
    };
    gd.microfono = g.microfono;
    gd.microfonoBanca = g.microfonoBanca;
    // Cada lado lista: lo que la fuente de banca recorta no se mueve (dos bancas de la
    // misma cámara digital se ven a la vez); una motorizada, sí.
    gd.apuntar = [this](const std::string& id) { return bancas_->prepararToma(QString::fromStdString(id)).toStdString(); };
    gd.armar = g.armar;
    gd.cortar = g.cortar;
    gd.terminarOrador = [this]() { bancas_->terminar(); };
    gd.central = [this](bool debate) {
        if (!dcn_ || !dcn_->conectada()) return;
        QPointer<AsambleaViewModel> yo(this);
        if (debate) {
            // Lo que tenía, para devolvérselo; después, operador con dos abiertos.
            dcn_->control("ajustes", nlohmann::json::object(), [yo](bool ok, const nlohmann::json& r, const std::string& e) {
                if (!yo) return;
                if (!ok) {
                    emit yo->notify(tr("Central (debate): %1").arg(QString::fromStdString(e)), "error");
                    return;
                }
                yo->centralAntesDelDebate_ = {{"modo", r.value("modo", std::string("operador"))},
                                              {"max_abiertos", r.value("max_abiertos", 4)}};
                yo->dcn_->control("ajustes", {{"modo", "operador"}, {"max_abiertos", 2}},
                                  [yo](bool ok2, const nlohmann::json&, const std::string& e2) {
                                      if (yo && !ok2)
                                          emit yo->notify(tr("Central (debate): %1").arg(QString::fromStdString(e2)), "error");
                                  });
            });
        } else if (centralAntesDelDebate_.is_object() && !centralAntesDelDebate_.empty()) {
            dcn_->control("ajustes", centralAntesDelDebate_, [yo](bool ok, const nlohmann::json&, const std::string& e) {
                if (yo && !ok) emit yo->notify(tr("Central (fin del debate): %1").arg(QString::fromStdString(e)), "error");
            });
            centralAntesDelDebate_ = nlohmann::json();
        }
    };
    gd.avisarRemoto = [this](const std::string& id, const nlohmann::json& m) {
        if (sesionRemota_) sesionRemota_->mandar(id, m);
    };
    gd.avisoRemoto = [this](const std::string& id, const std::string& a) {
        if (sesionRemota_) sesionRemota_->avisoTiempo(id, a);
    };
    gd.avisoCentral = [this](const std::string& banca, const std::string& a) {
        if (dcn_ && dcn_->conectada() && dcn_->tieneUnidad(banca))
            dcn_->control("tiempo", {{"banca", banca}, {"aviso", a}}, [](bool, const nlohmann::json&, const std::string&) {});
    };
    debate_ = std::make_unique<asamblea::Debate>(cuerpo_->cuerpo(), p_.rt.dataRoot(), gd);
    connect(debate_.get(), &asamblea::Debate::aviso, this, [this](const QString& t, const QString& n) { emit notify(t, n); });
    // La foto de cada integrante, capturada con la cámara de su banca.
    asamblea::CuerpoViewModel::Ganchos gc;
    gc.apuntar = [this](const QString& banca) { return bancas_->apuntar(banca); };
    gc.capturar = [this](const QString& camara, std::function<void(QImage)> listo) {
        // Sin esperar el cuadro acá (capturarAparte): vuelve a este hilo cuando llega.
        p_.rt.engine().capturarAparte(
            camara.toStdString(), 960,
            [listo = std::move(listo)](const std::string& e, std::vector<std::uint8_t> rgba, int w, int h) mutable {
                QImage img;
                if (e.empty() && w > 0 && h > 0) img = QImage(rgba.data(), w, h, w * 4, QImage::Format_RGBA8888).copy();
                QMetaObject::invokeMethod(qApp, [listo = std::move(listo), img]() { listo(img); }, Qt::QueuedConnection);
            });
    };
    cuerpo_->setGanchos(std::move(gc));
    seguirMic_ = std::make_unique<asamblea::SeguirMicrofono>(p_.rt.dataRoot(), p_.bus, cuerpo_->cuerpo(), *bancas_);
    ordenDia_ = std::make_unique<asamblea::OrdenDelDia>(p_.rt.dataRoot(), p_.bus);
}

void AsambleaViewModel::armarSesion() {
    // La banca virtual (sesión remota, en la sala o afuera): docs/funciones/SESION-REMOTA.md.
    sesionRemota_ = std::make_unique<asamblea::SesionRemota>(cuerpo_->cuerpo(), p_.rt.dataRoot());
    // Los orígenes de los links de integrantes y presidencia: los de sala/Direcciones (la
    // red local para los de la sala; el túnel, aparte).
    const auto origenes = [this]() {
        sesionRemota_->setOrigenes(p_.direcciones.origen("sesion"), p_.direcciones.origen("sesion", true),
                                   p_.direcciones.motivo("sesion"));
    };
    connect(&p_.direcciones, &sala::Direcciones::cambio, sesionRemota_.get(), origenes);
    origenes();
    // Los presentes con voto salen del padrón del evento abierto (o de la central),
    // más los habilitados por la banca virtual, sin contar a nadie dos veces.
    quorum_ = std::make_unique<asamblea::Quorum>(p_.rt.dataRoot(), [this]() {
        const std::vector<std::string> virtuales = sesionRemota_ ? sesionRemota_->presentesBancas() : std::vector<std::string>{};
        // Si se eligió, los presentes los dice la asistencia de la central de conferencia.
        if (dcn_ && dcn_->presentesDeLaCentral())
            if (const int pr = dcn_->presentesCentral(); pr >= 0) {
                // Los que la central ya cuenta presentes en su banca no se suman.
                const nlohmann::json porBanca = hijoObjeto(hijoObjeto(dcn_->estado(), "asistencia"), "por_banca");
                int extra = 0;
                for (const auto& b : virtuales)
                    if (!porBanca.value(b, false)) ++extra;
                return pr + extra;
            }
        const int virt = sesionRemota_ ? sesionRemota_->presentes() : 0;
        if (p_.evento == nullptr) return virt;
        const QVariantMap r = p_.evento->property("resumenPadron").toMap();
        return (r.value("conVoto").toInt() > 0 ? r.value("votoPresente").toInt() : r.value("presentes").toInt()) + virt;
    }, [this]() {
        // Mientras no se fije a mano, los miembros son los del padrón con voto.
        return p_.evento ? p_.evento->property("resumenPadron").toMap().value("conVoto").toInt() : 0;
    });
    if (p_.evento == nullptr) return;
    p_.evento->setAnexoActa([this]() {
        QString t = QString::fromStdString(quorum_->anexoActa() + ordenDia_->anexoActa(remotosPalabra()));
        if (sesionRemota_) {
            const std::string sr = sesionRemota_->anexoActa();
            // El acta es del evento: sus títulos van en el idioma del evento (se
            // arma cada vez que se genera el acta).
            if (!sr.empty())
                t += QString::fromStdString(ba::i18n::traducirEvento("AsambleaViewModel", "## Banca virtual") +
                                            "\n\n```\n" + sr + "```\n\n");
        }
        // La transcripción de la sesión, con la huella de cada archivo (se puede verificar después).
        const QStringList archivos = p_.archivosDeSesion ? p_.archivosDeSesion() : QStringList{};
        if (!archivos.isEmpty()) {
            t += QString::fromStdString(ba::i18n::traducirEvento("AsambleaViewModel", "## Transcripción") + "\n\n| " +
                                        ba::i18n::traducirEvento("AsambleaViewModel", "Archivo") +
                                        " | SHA-256 |\n|---|---|\n");
            for (const QString& a : archivos) {
                QFile f(a);
                QString h = "—";
                if (f.open(QIODevice::ReadOnly)) {
                    QCryptographicHash c(QCryptographicHash::Sha256);
                    c.addData(&f);
                    h = QString::fromLatin1(c.result().toHex());
                }
                t += "| " + a + " | " + h + " |\n";
            }
            t += "\n_" +
                 QString::fromStdString(ba::i18n::traducirEvento(
                     "AsambleaViewModel",
                     "La transcripción es automática: puede tener errores; la versión taquigráfica es la oficial.")) +
                 "_\n\n";
        }
        return t;
    });
}

void AsambleaViewModel::armarInformes() {
    // El resultado de la sesión en PDF, con plantillas (asamblea/InformesResultado): lo que
    // se rellena sale del cuerpo, el quórum, el orden del día, las votaciones oficiales,
    // el padrón y el uso de la palabra. Los textos en el idioma del evento.
    informes_ = std::make_unique<asamblea::InformesResultado>(
        p_.rt.dataRoot(),
        [this]() {
            using nlohmann::json;
            const auto T = [](const char* t) { return ba::i18n::traducirEvento("asamblea", t); };
            json d = json::object();
            d["cuerpo"] = cuerpo_ ? cuerpo_->cuerpo().nombre() : std::string();
            if (p_.evento) {
                const QVariantMap g = p_.evento->guion();
                d["evento"] = g.value("nombre").toString().toStdString();
                d["fecha"] = g.value("fecha").toString().toStdString();
                d["hora"] = g.value("inicio").toString().toStdString();
                const QVariantMap r = p_.evento->resumenPadron();
                d["padron"] = {{"presentes", r.value("presentes").toInt()}, {"total", r.value("total").toInt()},
                               {"con_voto", r.value("conVoto").toInt()}, {"voto_presentes", r.value("votoPresente").toInt()}};
                json presentes = json::array();
                for (const QVariant& v : p_.evento->padron()) {
                    const QVariantMap a = v.toMap();
                    const qint64 llego = a.value("llegoMs").toLongLong();
                    if (llego <= 0 && a.value("estado").toString() != "llego") continue;
                    presentes.push_back({{"nombre", a.value("nombre").toString().toStdString()},
                                         {"papel", a.value("papel").toString().toStdString()},
                                         {"por_donde", a.value("porDonde").toString().toStdString()},
                                         {"hora", llego > 0 ? QDateTime::fromMSecsSinceEpoch(llego).toString("HH:mm").toStdString() : std::string()}});
                }
                d["presentes"] = presentes;
            }
            if (quorum_) {
                const json q = quorum_->estado();
                d["quorum"] = {{"presentes", q.value("presentes", 0)}, {"necesarios", q.value("necesarios", 0)},
                               {"miembros", q.value("miembros", 0)}, {"hay", q.value("hay_quorum", false)},
                               {"estado", q.value("hay_quorum", false) ? T(QT_TRANSLATE_NOOP("asamblea", "Hay quórum")) : T(QT_TRANSLATE_NOOP("asamblea", "Sin quórum"))}};
                json vs = json::array();
                for (const auto& o : quorum_->oficiales())
                    vs.push_back({{"asunto", o.value("asunto", std::string())}, {"afirmativos", o.value("afirmativos", 0)},
                                  {"negativos", o.value("negativos", 0)}, {"abstenciones", o.value("abstenciones", 0)},
                                  {"presentes", o.value("presentes", 0)}, {"mayoria", asamblea::Quorum::nombreMayoria(o.value("mayoria", std::string()))},
                                  {"aprobado", o.value("aprobado", false)},
                                  {"resultado", o.value("aprobado", false) ? T(QT_TRANSLATE_NOOP("asamblea", "APROBADO")) : T(QT_TRANSLATE_NOOP("asamblea", "RECHAZADO"))}});
                d["votaciones"] = vs;
            }
            if (ordenDia_) {
                const std::map<std::string, std::string> estados{{"pendiente", T(QT_TRANSLATE_NOOP("asamblea", "pendiente"))}, {"en_tratamiento", T(QT_TRANSLATE_NOOP("asamblea", "en tratamiento"))},
                                                                 {"votado", T(QT_TRANSLATE_NOOP("asamblea", "votado"))}, {"comision", T(QT_TRANSLATE_NOOP("asamblea", "a comisión"))},
                                                                 {"retirado", T(QT_TRANSLATE_NOOP("asamblea", "retirado"))}};
                json ps = json::array();
                for (const auto& pt : ordenDia_->puntos()) {
                    const std::string e = pt.value("estado", std::string());
                    ps.push_back({{"n", pt.value("n", 0)}, {"expediente", pt.value("expediente", std::string())},
                                  {"titulo", pt.value("titulo", std::string())},
                                  {"estado", estados.count(e) ? estados.at(e) : e}, {"resultado", pt.value("resultado", std::string())}});
                }
                d["puntos"] = ps;
                json pal = json::array();
                for (const auto& t : ordenDia_->tiemposPalabra(remotosPalabra())) {
                    const long long seg = t.value("segundos", 0LL);
                    pal.push_back({{"nombre", t.value("nombre", std::string())}, {"segundos", seg},
                                   {"tiempo", QString("%1:%2").arg(seg / 60).arg(seg % 60, 2, 10, QChar('0')).toStdString()}});
                }
                d["palabra"] = pal;
            }
            return d;
        },
        [this]() { return p_.evento ? p_.evento->carpetaEvento() : std::filesystem::path(); },
        [](const QString& md, const QString& titulo, const QString& destino) { return APdf::texto(md, "markdown", titulo, destino); });
}

QObject* AsambleaViewModel::cuerpoQml() const { return deCpp(cuerpo_.get()); }
QObject* AsambleaViewModel::bancasQml() const { return deCpp(bancas_.get()); }
QObject* AsambleaViewModel::debateQml() const { return deCpp(debate_.get()); }
QObject* AsambleaViewModel::dcnQml() const { return deCpp(dcn_.get()); }
QObject* AsambleaViewModel::sesionRemotaQml() const { return deCpp(sesionRemota_.get()); }
QObject* AsambleaViewModel::votacionesQml() const { return deCpp(votaciones_.get()); }
QObject* AsambleaViewModel::seguirMicrofonoQml() const { return deCpp(seguirMic_.get()); }
QObject* AsambleaViewModel::ordenDelDiaQml() const { return deCpp(ordenDia_.get()); }
QObject* AsambleaViewModel::informesQml() const { return deCpp(informes_.get()); }
QObject* AsambleaViewModel::quorumQml() const { return deCpp(quorum_.get()); }

bool AsambleaViewModel::presidenciaHabla() const { return presidencia_ && presidencia_->presidenciaHabla(); }

QString AsambleaViewModel::hablarARemoto(const QString& integrante, bool activo) {
    if (!presidencia_) return tr("La sesión remota no está andando.");
    return QString::fromStdString(presidencia_->hablarARemoto(integrante.toStdString(), activo));
}

void AsambleaViewModel::silenciarTodos() {
    if (!presidencia_) return;
    QPointer<AsambleaViewModel> yo(this);
    presidencia_->silenciarTodos("operador", [yo](const std::string& e) {
        if (yo && !e.empty()) emit yo->notify(QString::fromStdString(e), "error");
    });
}

void AsambleaViewModel::hablaPresidencia(bool activo) {
    if (!presidencia_) return;
    QPointer<AsambleaViewModel> yo(this);
    presidencia_->hablaPresidencia(activo, "operador", [yo](const std::string& e) {
        if (yo && !e.empty()) emit yo->notify(QString::fromStdString(e), "error");
    });
}

nlohmann::json AsambleaViewModel::remotosPalabra() const {
    nlohmann::json out = nlohmann::json::array();
    const QJsonObject snap = p_.sala.client().snapshot();
    for (const QJsonValue& v : snap.value("participants").toArray()) {
        const QJsonObject pa = v.toObject();
        const qint64 ms = static_cast<qint64>(pa.value("usedMs").toDouble());
        if (ms > 0) out.push_back({{"nombre", pa.value("name").toString().toStdString()}, {"segundos", ms / 1000}});
    }
    return out;
}

void AsambleaViewModel::tic() {
    // El tiempo del que tiene la palabra (presencial o remoto), una vez por segundo:
    // a un minuto y al terminar, aviso al operador, en el celular del remoto y en la
    // unidad de su banca si está la central (Bosch DCN), que además lleva el reloj.
    if (++dcnTic_ % 10 != 0 || !bancas_) return;
    const nlohmann::json j = bancas_->estadoJson();
    const nlohmann::json al = j.value("al_aire", nlohmann::json());
    if (!al.is_object()) {
        dcnOrador_.clear();
        return;
    }
    const std::string id = al.value("id", std::string()), banca = al.value("banca", std::string());
    const std::string nombre = al.value("nombre", std::string());
    const int asignado = j.value("asignado_s", 0), usado = static_cast<int>(j.value("usado_s", 0LL));
    if (asignado <= 0) return;
    const bool remoto = sesionRemota_ && sesionRemota_->tienePalabra(id);
    const bool central = !remoto && dcn_ && dcn_->conectada() && !banca.empty() && dcn_->tieneUnidad(banca);
    const std::string clave = id + "@" + std::to_string(j.value("desde_ms", 0LL));
    const auto ignorar = [](bool, const nlohmann::json&, const std::string&) {};
    if (clave != dcnOrador_) {
        dcnOrador_ = clave;
        dcnAvisos_ = 0;
        if (central) dcn_->control("tiempo", {{"limite_min", (asignado + 59) / 60}, {"reloj", true}}, ignorar);
    }
    const int restante = asignado - usado;
    const QString quien = banca.empty() ? QString::fromStdString(nombre)
                                        : tr("%1 (banca %2)").arg(QString::fromStdString(nombre), QString::fromStdString(banca));
    if (restante <= 60 && restante > 0 && !(dcnAvisos_ & 1)) {
        dcnAvisos_ |= 1;
        emit notify(tr("%1: queda 1 minuto").arg(quien), "warn");
        if (remoto) sesionRemota_->avisoTiempo(id, "ultimo_minuto");
        if (central) dcn_->control("tiempo", {{"banca", banca}, {"aviso", "ultimo_minuto"}}, ignorar);
        p_.bus.publicar("oradores/tiempo", {{"id", id}, {"banca", banca}, {"aviso", "ultimo_minuto"}});
    }
    if (restante <= 0 && !(dcnAvisos_ & 2)) {
        dcnAvisos_ |= 3;
        emit notify(tr("%1: se terminó su tiempo").arg(quien), "error");
        if (remoto) sesionRemota_->avisoTiempo(id, "terminado");
        if (central) dcn_->control("tiempo", {{"banca", banca}, {"aviso", "terminado"}}, ignorar);
        p_.bus.publicar("oradores/tiempo", {{"id", id}, {"banca", banca}, {"aviso", "terminado"}});
    }
}

void AsambleaViewModel::actualizarRanurasBanca(bool forzar) {
    if (!bancas_ || !cuerpo_) return;
    // Quién va en cada ranura: en un debate, A y B; si no, quien tiene la palabra. Al
    // que habla desde el navegador no se le pone cámara (tiene la suya: el remoto).
    std::vector<std::string> ids;
    if (debate_ && debate_->activo()) {
        // Los de la sala, en orden (ranura 1 y 2): con uno remoto, el de la sala va en
        // la 1 (la escena «sala y remoto» es banca 1 + el remoto).
        if (!debate_->aEsRemoto()) ids.push_back(debate_->idA());
        if (!debate_->bEsRemoto()) ids.push_back(debate_->idB());
    } else {
        const std::string id = bancas_->alAireId();
        bool remoto = false;
        if (sesionRemota_ && !id.empty()) {
            const std::vector<std::string> conPalabra = sesionRemota_->conPalabraTodos();
            remoto = std::find(conPalabra.begin(), conPalabra.end(), id) != conPalabra.end();
        }
        ids.push_back(remoto ? std::string() : id);
    }
    if (ids.size() > static_cast<std::size_t>(BroadcastEngine::kRanurasBanca)) ids.resize(BroadcastEngine::kRanurasBanca);
    std::vector<BroadcastEngine::RanuraBanca> ranuras;
    for (const std::string& id : ids) {
        BroadcastEngine::RanuraBanca r;
        const nlohmann::json i = id.empty() ? nlohmann::json::object() : cuerpo_->cuerpo().buscar(id);
        const nlohmann::json t = i.value("preset_ptz", nlohmann::json::object()); // la toma de su banca
        const QString camara = QString::fromStdString(t.value("camara", std::string()));
        if (!camara.isEmpty()) {
            r.entrada = camara.toStdString();
            const QVariantMap d = p_.camaras.datosPosicion(camara, QString::fromStdString(t.value("posicion", std::string())));
            // Digital (sin motor): la ventana de la posición, recortada de la toma entera.
            // Motorizada: la cámara entera (la cabeza ya fue a la posición al dar la palabra).
            if (d.value("existe").toBool() && d.value("digital").toBool() && !d.value("motorizada").toBool()) {
                const double z = std::max(1.0, d.value("zoom").toDouble());
                const double x = d.value("x").toDouble(), y = d.value("y").toDouble(), m = 0.5 / z;
                r.entera = d.value("recorteDigital").toBool();
                r.recorte[0] = static_cast<float>(std::max(0.0, x - m));
                r.recorte[1] = static_cast<float>(std::max(0.0, y - m));
                r.recorte[2] = static_cast<float>(std::max(0.0, 1.0 - (x + m)));
                r.recorte[3] = static_cast<float>(std::max(0.0, 1.0 - (y + m)));
            }
        }
        ranuras.push_back(r);
    }
    p_.rt.engine().setRanurasBanca(std::move(ranuras), forzar);
}

nlohmann::json AsambleaViewModel::paraLaWeb() const {
    nlohmann::json sala = nlohmann::json::object();
    const auto publicoDe = [](const nlohmann::json& i) {
        nlohmann::json pu{{"nombre", i.value("nombre", std::string())}};
        for (const char* k : {"bloque", "cargo"})
            if (i.contains(k) && i[k].is_string() && !i[k].get<std::string>().empty()) pu[k] = i[k];
        return pu;
    };
    {
        const nlohmann::json b = bancas_->estadoJson();
        nlohmann::json fila = nlohmann::json::array();
        for (const auto& i : b.value("cola", nlohmann::json::array())) fila.push_back(publicoDe(i));
        const nlohmann::json alAire = b.value("al_aire", nlohmann::json());
        if (alAire.is_object() || !fila.empty()) {
            nlohmann::json o{{"fila", fila}};
            if (alAire.is_object()) {
                o["habla"] = publicoDe(alAire);
                o["asignadoSeg"] = b.value("asignado_s", 0);
                // Cuándo se le termina el tiempo (hora absoluta): la página cuenta sola.
                o["hastaMs"] = b.value("desde_ms", 0LL) + b.value("asignado_s", 0LL) * 1000;
            }
            sala["oradores"] = o;
        }
    }
    {
        nlohmann::json puntos = nlohmann::json::array();
        for (const auto& pt : ordenDia_->puntos())
            puntos.push_back({{"n", pt.value("n", 0)}, {"titulo", pt.value("titulo", std::string())},
                              {"expediente", pt.value("expediente", std::string())},
                              {"estado", pt.value("estado", std::string())},
                              {"resultado", pt.value("resultado", std::string())}});
        if (!puntos.empty()) sala["ordenDelDia"] = puntos;
    }
    {
        const nlohmann::json q = quorum_->estado();
        if (q.value("configurado", false))
            sala["quorum"] = {{"configurado", true}, {"presentes", q.value("presentes", 0)},
                              {"necesarios", q.value("necesarios", 0)}, {"hay", q.value("hay_quorum", false)}};
    }
    // La asamblea: el cuerpo, quién está presente (conectado desde el navegador y
    // habilitado, o presente en su banca según la central), quién habla, el quórum,
    // el punto en tratamiento y los resultados oficiales. La página solo lo muestra si
    // el evento es una asamblea.
    std::set<std::string> remotos;
    for (const auto& b : sesionRemota_->presentesBancas()) remotos.insert(b);
    const nlohmann::json porBanca = hijoObjeto(hijoObjeto(dcn_->estado(), "asistencia"), "por_banca");
    const std::string hablaId = hijoObjeto(bancas_->estadoJson(), "al_aire").value("id", std::string());
    nlohmann::json integrantes = nlohmann::json::array();
    nlohmann::json porId = nlohmann::json::object(); // id → lo público del integrante (para el recinto)
    std::map<std::string, std::pair<int, int>> bloques; // bloque → {presentes, total}
    int presentes = 0;
    for (const auto& i : cuerpo_->cuerpo().integrantes()) {
        const std::string banca = i.value("banca", std::string());
        const bool remoto = !banca.empty() && remotos.count(banca);
        const bool enSala = !banca.empty() && porBanca.is_object() && porBanca.value(banca, false);
        nlohmann::json pu{{"nombre", i.value("nombre", std::string())}, {"banca", banca},
                          {"presente", remoto || enSala}, {"lugar", remoto ? "remoto" : enSala ? "sala" : ""},
                          {"habla", !hablaId.empty() && i.value("id", std::string()) == hablaId}};
        for (const char* k : {"bloque", "cargo"})
            if (i.contains(k) && i[k].is_string() && !i[k].get<std::string>().empty()) pu[k] = i[k];
        if (remoto || enSala) ++presentes;
        auto& bl = bloques[i.value("bloque", std::string())];
        ++bl.second;
        if (remoto || enSala) ++bl.first;
        if (i.contains("id") && i["id"].is_string()) porId[i["id"].get<std::string>()] = pu;
        integrantes.push_back(pu);
    }
    nlohmann::json asam{{"cuerpo", cuerpo_->cuerpo().nombre()}, {"integrantes", integrantes},
                        {"presentes", presentes}, {"total", static_cast<int>(integrantes.size())}};
    // Los bloques (para ver de un vistazo cuántos de cada uno están).
    nlohmann::json bjs = nlohmann::json::array();
    for (const auto& [nombre, n] : bloques)
        if (!nombre.empty()) bjs.push_back({{"bloque", nombre}, {"presentes", n.first}, {"total", n.second}});
    asam["bloques"] = bjs;
    // El recinto: dónde está sentado cada uno (fila 0 la de adelante, la más cerca de
    // la presidencia; columna 0 la de la izquierda vista desde la cabina) y cómo se
    // dibuja (grilla, hemiciclo o lista). Solo lo público: sin presets ni micrófonos.
    {
        const nlohmann::json r = cuerpo_->cuerpo().recinto(true);
        // (x, y: ubicada a mano en el hemiciclo; la presidencia va en la mesa.)
        const auto publico = [&porId](const nlohmann::json& l) {
            nlohmann::json x{{"banca", l.value("banca", std::string())}, {"fila", l.value("fila", 0)},
                             {"columna", l.value("columna", 0)}};
            for (const char* k : {"x", "y"})
                if (l.contains(k)) x[k] = l[k];
            const std::string id = hijoObjeto(l, "integrante").value("id", std::string());
            if (!id.empty() && porId.contains(id)) {
                const auto& pu = porId[id];
                for (const char* k : {"nombre", "bloque", "cargo", "presente", "lugar", "habla"})
                    if (pu.contains(k)) x[k] = pu[k];
            }
            return x;
        };
        nlohmann::json lugares = nlohmann::json::array();
        for (const auto& l : r.value("bancas", nlohmann::json::array())) lugares.push_back(publico(l));
        if (!lugares.empty()) {
            asam["recinto"] = {{"distribucion", r.value("distribucion", std::string("grilla"))},
                               {"filas", r.value("filas", 0)}, {"columnas", r.value("columnas", 0)},
                               {"bancas", lugares}};
            if (r.contains("presidencia")) asam["recinto"]["presidencia"] = publico(r["presidencia"]);
        }
    }
    {
        const nlohmann::json q = quorum_->estado();
        if (q.value("configurado", false))
            asam["quorum"] = {{"presentes", q.value("presentes", 0)}, {"necesarios", q.value("necesarios", 0)},
                              {"hay", q.value("hay_quorum", false)}, {"miembros", q.value("miembros", 0)}};
        nlohmann::json vots = nlohmann::json::array();
        for (const auto& o : quorum_->oficiales())
            vots.push_back({{"asunto", o.value("asunto", std::string())},
                            {"afirmativos", o.value("afirmativos", 0)}, {"negativos", o.value("negativos", 0)},
                            {"abstenciones", o.value("abstenciones", 0)}, {"aprobado", o.value("aprobado", false)}});
        asam["votaciones"] = vots;
    }
    {
        const nlohmann::json a = ordenDia_->actual();
        if (a.is_object() && a.value("n", 0) > 0)
            asam["punto"] = {{"n", a.value("n", 0)}, {"titulo", a.value("titulo", std::string())},
                             {"expediente", a.value("expediente", std::string())}};
    }
    sala["asamblea"] = asam;
    return sala;
}

void AsambleaViewModel::completarVinculos(QJsonObject& ctx) const {
    // El punto del orden del día que se está tratando: {punto.texto}, {punto.n}…
    {
        const nlohmann::json pt = ordenDia_->actual();
        if (!pt.empty())
            ctx["punto"] = QJsonObject{{"n", pt.value("n", 0)},
                                       {"titulo", QString::fromStdString(pt.value("titulo", std::string()))},
                                       {"expediente", QString::fromStdString(pt.value("expediente", std::string()))},
                                       // Sale al aire (rótulos): en el idioma del evento. El
                                       // contexto se arma en cada vuelta de los vínculos.
                                       {"texto", QString::fromStdString(ba::i18n::llenar(
                                                     ba::i18n::traducirEvento("AsambleaViewModel", "Punto %1: %2"),
                                                     {std::to_string(pt.value("n", 0)), pt.value("titulo", std::string())}))}};
    }
    // Un orador presencial (banca) al aire manda sobre el de la sala.
    {
        const nlohmann::json b = bancas_->oradorParaZocalo();
        if (!b.empty())
            ctx["orador"] = QJsonDocument::fromJson(QByteArray::fromStdString(b.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace))).object();
    }
    // El quórum de ahora y la última votación oficial (asamblea/Quorum), para
    // las placas de la asamblea: {quorum.texto}, {votacion.resultado}…
    {
        const nlohmann::json q = quorum_->estado();
        if (q.value("configurado", false))
            ctx["quorum"] = QJsonObject{
                {"presentes", q.value("presentes", 0)},
                {"necesarios", q.value("necesarios", 0)},
                {"miembros", q.value("miembros", 0)},
                {"hay", q.value("hay_quorum", false)},
                {"estado", QString::fromStdString(q.value("hay_quorum", false)
                                                      ? ba::i18n::traducirEvento("AsambleaViewModel", "CON QUÓRUM")
                                                      : ba::i18n::traducirEvento("AsambleaViewModel", "SIN QUÓRUM"))},
                {"texto", QString::fromStdString(ba::i18n::llenar(ba::i18n::traducirEvento("AsambleaViewModel", "%1 presentes de %2"),
                                                                  {std::to_string(q.value("presentes", 0)),
                                                                   std::to_string(q.value("miembros", 0))}))}};
        const nlohmann::json of = quorum_->oficiales();
        if (!of.empty()) {
            const nlohmann::json& v = of.back();
            ctx["votacion"] = QJsonObject{
                {"asunto", QString::fromStdString(v.value("asunto", std::string()))},
                {"afirmativos", v.value("afirmativos", 0)},
                {"negativos", v.value("negativos", 0)},
                {"abstenciones", v.value("abstenciones", 0)},
                {"presentes", v.value("presentes", 0)},
                {"necesarios", v.value("necesarios", 0)},
                {"resultado", QString::fromStdString(v.value("aprobado", false)
                                                         ? ba::i18n::traducirEvento("AsambleaViewModel", "APROBADO")
                                                         : ba::i18n::traducirEvento("AsambleaViewModel", "RECHAZADO"))},
                {"color", v.value("aprobado", false) ? "#2e7d32" : "#c62828"}};
        }
    }
    // Campos vivos (T43): el cronómetro del orador cambia cada segundo; en un vínculo
    // «vivo» el gráfico se actualiza sin volver a entrar.
    const nlohmann::json b = bancas_->estadoJson();
    if (!b.value("al_aire", nlohmann::json()).is_null()) {
        const long long usado = b.value("usado_s", 0LL), asignado = b.value("asignado_s", 0LL);
        const auto mmss = [](long long s) { return QString("%1:%2").arg(s / 60).arg(s % 60, 2, 10, QChar('0')); };
        QJsonObject o = ctx.value("orador").toObject();
        o["cronometro"] = mmss(usado);
        o["restante"] = asignado > 0 ? (usado > asignado ? "+" + mmss(usado - asignado) : mmss(asignado - usado)) : QString();
        ctx["orador"] = o;
    }
}

} // namespace ba::app
