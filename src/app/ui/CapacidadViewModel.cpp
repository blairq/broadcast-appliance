// src/app/ui/CapacidadViewModel.cpp — ¿cuántos invitados aguanta? (ver el .hpp)

#include "ui/CapacidadViewModel.hpp"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QPointer>
#include <QUrl>

#include <algorithm>
#include <cmath>
#include <system_error>

#include "i18n/I18nTexto.hpp"
#include "persist/ColaEscritura.hpp"
#include "sala/GeneradorCarga.hpp"

namespace ba::app {

namespace cap = sala::capacidad;
using json = nlohmann::json;
namespace fs = std::filesystem;
using ba::i18n::llenar;
using ba::i18n::traducir;

namespace {
QVariantMap aMapa(const json& j) {
    return QJsonDocument::fromJson(QByteArray::fromStdString(j.dump(-1, ' ', false, json::error_handler_t::replace)))
        .object()
        .toVariantMap();
}
QVariantList aLista(const json& j) {
    return QJsonDocument::fromJson(QByteArray::fromStdString(j.dump(-1, ' ', false, json::error_handler_t::replace)))
        .array()
        .toVariantList();
}
json deQJson(const QJsonObject& o) {
    return json::parse(QJsonDocument(o).toJson(QJsonDocument::Compact).toStdString(), nullptr, false);
}
QString qs(const std::string& s) { return QString::fromStdString(s); }
/// Los de prueba y los reales de la foto de la sala (lo mismo que cap::genteDe, sin
/// convertir la foto entera: se llama con cada foto).
cap::GenteSala genteDeFoto(const QJsonObject& foto) {
    cap::GenteSala g;
    for (const QJsonValue& v : foto.value("participants").toArray()) {
        const QJsonObject p = v.toObject();
        const bool conectado = p.value("connected").toBool();
        if (p.value("prueba").toBool()) {
            ++g.prueba;
            if (conectado && p.value("mediaUp").toBool()) ++g.pruebaConVideo;
        } else if (conectado) {
            ++g.reales;
        }
    }
    return g;
}
/// Una línea del historial a partir del informe guardado.
json resumenDe(const json& inf, const std::string& archivo) {
    json r{{"archivo", archivo},
           {"fecha", inf.value("fecha", int64_t{0})},
           {"tipo", inf.value("tipo", std::string())},
           {"maquina", inf.value("maquina", std::string())},
           {"frase", inf.value("frase", std::string())}};
    if (const json res = inf.value("resultado", json::object()); res.is_object() && !res.empty()) {
        r["conMargen"] = res.value("conMargen", 0);
        r["maximo"] = res.value("maximo", 0);
        r["alMenos"] = res.value("alMenos", false);
        r["limita"] = res.value("limitaTexto", std::string());
    }
    if (inf.contains("invitados")) r["invitados"] = inf.value("invitados", -1);
    return r;
}
} // namespace

CapacidadViewModel::CapacidadViewModel(Partes p, QObject* parent) : QObject(parent), p_(std::move(p)) {
    if (!p_.reloj) p_.reloj = []() { return QDateTime::currentMSecsSinceEpoch(); };
    tic_.setInterval(1000);
    connect(&tic_, &QTimer::timeout, this, &CapacidadViewModel::tic);
    if (p_.generador) {
        connect(p_.generador.get(), &sala::GeneradorCarga::preparacionTerminada, this, [this](bool ok, const QString& error) {
            if (fase_ != QLatin1String("preparando")) return;
            if (estado_ != QLatin1String("local") && estado_ != QLatin1String("remoto")) return;
            if (!ok) {
                terminarRampa(error.isEmpty() ? traducir("capacidad", "no se pudo preparar el video de prueba de hinchada") : error.toStdString(),
                              true, false);
                return;
            }
            fase_ = QStringLiteral("abriendo");
            faseDesde_ = ahora();
            if (estado_ == QLatin1String("local")) seguirLocal();
            else seguirRemoto();
        });
        connect(p_.generador.get(), &sala::GeneradorCarga::seCayo, this, [this](int codigo) {
            if (estado_ != QLatin1String("local") && estado_ != QLatin1String("remoto")) return;
            terminarRampa(llenar(traducir("capacidad", "el probador (hinchada) se cortó solo (código %1): ver consola.txt"),
                                 {std::to_string(codigo)}),
                          true, false);
        });
    }
    // ¿Quedó una prueba a medias (el programa se cerró en el medio)?
    std::string s;
    if (ba::persist::cola().leer(archivoEnCurso(), s)) {
        colgada_ = json::parse(s, nullptr, false);
        colgadaPendiente_ = colgada_.is_object();
    }
    if (colgadaPendiente_ && p_.online && p_.online()) QTimer::singleShot(0, this, [this]() { limpiarColgada(); });
    recargarHistorial();
    alCambiarPinggy();
    revisar();
}

CapacidadViewModel::~CapacidadViewModel() {
    // Al cerrar: hinchada se corta sola (es hijo nuestro: el generador lo mata). La
    // credencial vence sola en kVidaCredencialS y el gateway echa a los de prueba; el cupo y
    // lo demás se terminan de limpiar al volver a arrancar (archivoEnCurso).
    if (cancelarRed_) *cancelarRed_ = true;
    if (p_.generador) p_.generador->parar();
}

int64_t CapacidadViewModel::ahora() const { return p_.reloj(); }

bool CapacidadViewModel::hinchadaDisponible() const { return p_.generador && p_.generador->disponible(); }

fs::path CapacidadViewModel::dirHistorial() const { return p_.dataRoot / "Capacidad"; }
fs::path CapacidadViewModel::archivoEnCurso() const { return p_.dataRoot / "Runtime" / "capacidad-en-curso.json"; }

void CapacidadViewModel::setEstado(const QString& e) {
    if (estado_ == e) return;
    estado_ = e;
    emit estadoChanged();
}

// --- Lo que impide arrancar ------------------------------------------------------------------

void CapacidadViewModel::revisar() {
    QString r;
    if (!p_.online || !p_.online()) {
        r = qs(traducir("capacidad", "la conferencia no está andando"));
    } else if (p_.alAire && p_.alAire()) {
        r = qs(traducir("capacidad", "hay algo al aire (transmisión, grabación o señal limpia): la prueba carga la máquina"));
    } else {
        const cap::GenteSala g = genteDeFoto(p_.foto());
        if (g.reales > 0)
            r = qs(llenar(traducir("capacidad", "hay %1 invitados de verdad conectados: probá sin gente en la sala"),
                          {std::to_string(g.reales)}));
        else if (p_.otraPrueba)
            r = p_.otraPrueba();
    }
    if (r == riesgo_) return;
    riesgo_ = r;
    emit riesgoChanged();
}

QString CapacidadViewModel::puedeArrancar() const {
    if (ocupado()) return qs(traducir("capacidad", "ya hay una prueba de capacidad en curso"));
    return riesgo_;
}

// --- Este equipo y el remoto: la rampa -------------------------------------------------------

QString CapacidadViewModel::probarEsteEquipo(int tope) {
    revisar();
    if (const QString no = puedeArrancar(); !no.isEmpty()) return no;
    if (!hinchadaDisponible())
        return qs(traducir("capacidad", "el probador (hinchada) no viene en este programa: probá desde otro equipo"));
    if (tope < 1 || tope > topeMax())
        return qs(llenar(traducir("capacidad", "el tope va de 1 a %1 invitados"), {std::to_string(topeMax())}));
    // La guardia de la prueba de sala, con el generador en esta máquina.
    if (p_.medidaGuardia) {
        sala::prueba::Umbrales u;
        if (const std::string no = sala::prueba::puedeArrancar(p_.medidaGuardia(), u, tope, true); !no.empty()) return qs(no);
    }
    plan_ = cap::escalones(tope);
    maquinaMedida_ = p_.maquina;
    huella_.clear();
    setEstado(QStringLiteral("local"));
    empezarRampa();
    emit notify(tr("Prueba de capacidad: entran invitados de prueba por escalones hasta %n (cada escalón, 2 minutos como mucho).", "", tope),
                "ok");
    if (!p_.generador->preparado()) prepararGenerador();
    else seguirLocal();
    return {};
}

void CapacidadViewModel::seguirLocal() {
    const int tope = plan_.empty() ? 0 : plan_.back();
    QPointer<CapacidadViewModel> yo(this);
    abrirCredencial([yo, tope](bool ok, const QString& error) {
        if (!yo || yo->estado_ != QLatin1String("local")) return;
        if (!ok) {
            yo->terminarRampa(error.toStdString(), true, false);
            return;
        }
        // Por dónde entran: el probador local, por la máquina misma (HTTPS de la caja).
        yo->p_.get("/api/v1/network", [yo, tope](bool ok2, const QJsonObject& red) {
            if (!yo || yo->estado_ != QLatin1String("local")) return;
            const QJsonObject https = red.value("https").toObject();
            if (!ok2 || !https.value("enabled").toBool()) {
                yo->terminarRampa(traducir("capacidad", "la conferencia no tiene HTTPS: el probador no puede entrar"), true, false);
                return;
            }
            yo->url_ = QStringLiteral("https://127.0.0.1:%1").arg(https.value("port").toInt());
            // El cupo: tienen que entrar todos.
            const int cupo = yo->p_.foto().value("maxGuests").toInt();
            if (cupo > 0 && cupo < tope) {
                yo->cupoAntes_ = cupo;
                yo->escribirEnCurso();
                yo->p_.cambiarCupo(tope);
            }
            yo->empezarEscalon();
        });
    });
}

QString CapacidadViewModel::probarRemoto(const QString& codigo, int tope) {
    if (ocupado()) return qs(traducir("capacidad", "ya hay una prueba de capacidad en curso"));
    if (p_.otraPrueba) {
        if (const QString o = p_.otraPrueba(); !o.isEmpty()) return o;
    }
    if (!hinchadaDisponible())
        return qs(traducir("capacidad", "el probador (hinchada) no viene en este programa: no se puede generar desde acá"));
    cap::Codigo c;
    if (!cap::leerCodigo(codigo.toStdString(), c))
        return qs(traducir("capacidad", "ese no es un código de capacidad: copialo entero del otro equipo (empieza con BACAP1.)"));
    if (tope < 1 || tope > topeMax())
        return qs(llenar(traducir("capacidad", "el tope va de 1 a %1 invitados"), {std::to_string(topeMax())}));
    // Acá corre el generador: la memoria de ESTA máquina.
    if (p_.medidaGuardia) {
        sala::prueba::Umbrales u;
        if (const std::string no = sala::prueba::puedeArrancar(p_.medidaGuardia(), u, tope, true); !no.empty()) return qs(no);
    }
    url_ = qs(c.url);
    credencial_ = qs(c.credencial);
    huella_ = qs(c.huella);
    maquinaMedida_ = c.maquina.empty() ? url_ : qs(c.maquina);
    plan_ = cap::escalones(tope);
    setEstado(QStringLiteral("remoto"));
    empezarRampa();
    emit notify(tr("Prueba de capacidad del otro equipo (%1): los invitados de prueba salen de esta máquina.").arg(maquinaMedida_), "ok");
    if (!p_.generador->preparado()) prepararGenerador();
    else seguirRemoto();
    return {};
}

void CapacidadViewModel::seguirRemoto() {
    const int tope = plan_.empty() ? 0 : plan_.back();
    // Primero: ¿el otro contesta y cuánto cupo abrió?
    QPointer<CapacidadViewModel> yo(this);
    p_.pedirRemota(this, url_, credencial_, huella_, [yo, tope](const json& r) {
        if (!yo || yo->estado_ != QLatin1String("remoto")) return;
        if (!r.value("ok", false)) {
            yo->terminarRampa(r.value("error", traducir("capacidad", "el otro equipo no contestó")), true, false);
            return;
        }
        yo->remota_ = r;
        yo->remotaOkMs_ = yo->ahora();
        const json med = r.contains("medida") && r["medida"].is_object() ? r["medida"] : json::object();
        const int cupo = med.value("cupo", 0);
        if (cupo > 0 && cupo < tope) {
            yo->plan_ = cap::escalones(cupo);
            emit yo->notify(tr("El otro equipo abrió lugar para %n: la prueba llega hasta ahí.", "", cupo), "warn");
        }
        yo->empezarEscalon();
    });
}

void CapacidadViewModel::prepararGenerador() {
    // La primera vez, hinchada graba su video de prueba (unos minutos): sin eso los del
    // primer escalón no entrarían a tiempo y el resultado mentiría.
    fase_ = QStringLiteral("preparando");
    faseDesde_ = ahora();
    publicarProgreso();
    if (const QString e = p_.generador->preparar(); !e.isEmpty()) terminarRampa(e.toStdString(), true, false);
}

void CapacidadViewModel::empezarRampa() {
    inicioMs_ = ahora();
    indice_ = 0;
    nAnterior_ = 0;
    pasos_.clear();
    pasosUi_.clear();
    emit pasosChanged();
    delEscalon_.clear();
    regimen_.clear();
    cps_ = cpsEscalon_ = audioMs_ = -1;
    ultimoAudioMs_ = ultimaGrillaMs_ = 0;
    remota_ = json();
    remotaOkMs_ = 0;
    fase_ = QStringLiteral("abriendo");
    faseDesde_ = escalonDesde_ = inicioMs_;
    ultimoTic_ = 0;
    salida_ = QString::fromStdString((dirHistorial() / ("hinchada-" + QDateTime::fromMSecsSinceEpoch(inicioMs_)
                                                                          .toString("yyyyMMdd-HHmmss")
                                                                          .toStdString()))
                                         .string());
    publicarProgreso();
    tic_.start();
}

void CapacidadViewModel::abrirCredencial(std::function<void(bool, const QString&)> listo) {
    QPointer<CapacidadViewModel> yo(this);
    p_.post("/api/v1/prueba-sala", QJsonObject{{"duracionS", kVidaCredencialS}}, [yo, listo](bool ok, const QJsonObject& r) {
        if (!yo) return;
        if (!ok || !r.value("ok").toBool() || r.value("credencial").toString().isEmpty()) {
            listo(false, qs(traducir("capacidad", "la conferencia no abrió la credencial de prueba")));
            return;
        }
        yo->credencial_ = r.value("credencial").toString();
        yo->renovadaMs_ = yo->ahora();
        yo->escribirEnCurso();
        listo(true, {});
    });
}

void CapacidadViewModel::renovarCredencial() {
    renovadaMs_ = ahora();
    QPointer<CapacidadViewModel> yo(this);
    p_.post("/api/v1/prueba-sala", QJsonObject{{"duracionS", kVidaCredencialS}}, [yo](bool ok, const QJsonObject& r) {
        if (!yo || !ok) return;
        const QString c = r.value("credencial").toString();
        if (!c.isEmpty() && c != yo->credencial_) {
            // La sala se reinició y la prueba es otra: los de antes ya no están.
            yo->credencial_ = c;
            if (yo->estado_ == QLatin1String("recibiendo")) yo->dejarDeRecibir();
            else if (yo->estado_ == QLatin1String("local"))
                yo->terminarRampa(traducir("capacidad", "la conferencia se reinició en el medio de la prueba"), true, false);
        }
    });
}

void CapacidadViewModel::cerrarCredencial(const std::string& motivo) {
    p_.post("/api/v1/prueba-sala/cerrar", QJsonObject{{"motivo", qs(motivo)}}, {});
    credencial_.clear();
}

void CapacidadViewModel::devolverCupo() {
    if (cupoAntes_ > 0 && p_.cambiarCupo) p_.cambiarCupo(cupoAntes_);
    cupoAntes_ = 0;
}

void CapacidadViewModel::escribirEnCurso() {
    ba::persist::cola().escribir(archivoEnCurso(),
                                 json{{"inicioMs", inicioMs_},
                                      {"cupoAntes", cupoAntes_},
                                      {"estado", estado_.toStdString()},
                                      {"pinggy", pinggyLoPrendi_}}
                                     .dump());
}

void CapacidadViewModel::empezarEscalon() {
    if (indice_ >= plan_.size()) {
        terminarRampa(traducir("capacidad", "terminó la rampa"), false, false);
        return;
    }
    const int n = plan_[indice_];
    const int nuevos = n - nAnterior_;
    sala::GeneradorCarga::Lote l;
    l.url = url_;
    l.credencial = credencial_;
    l.cantidad = nuevos;
    l.entradaCadaMs = cap::entradaCadaMs(nuevos, p_.tiempos);
    l.prefijo = QStringLiteral("c%1-").arg(n);
    l.salida = salida_ + QStringLiteral("/escalon-%1").arg(n);
    l.inseguro = true; // la caja tiene su certificado propio
    if (const QString e = p_.generador->lanzar(l); !e.isEmpty()) {
        terminarRampa(e.toStdString(), true, false);
        return;
    }
    nAnterior_ = n;
    fase_ = QStringLiteral("entrando");
    escalonDesde_ = faseDesde_ = ahora();
    delEscalon_.clear();
    regimen_.clear();
    cpsEscalon_ = -1;
    publicarProgreso();
}

cap::Muestra CapacidadViewModel::muestraLocal(double uiMs) {
    const std::vector<std::uint32_t> pids = p_.generador && estado_ == QLatin1String("local") ? p_.generador->pids()
                                                                                              : std::vector<std::uint32_t>{};
    cap::Muestra m = p_.medirMaquina ? p_.medirMaquina(pids) : cap::Muestra{};
    m.t = ahora();
    m.cpuEquipo = m.cpuMaquina;
    m.cps = cps_;
    m.audioMs = audioMs_;
    m.uiMs = uiMs;
    const cap::GenteSala g = genteDeFoto(p_.foto());
    m.entraron = g.prueba;
    m.conVideo = g.pruebaConVideo;
    return m;
}

cap::Muestra CapacidadViewModel::muestraRemota() {
    // La del otro equipo (la última que llegó) y, de esta, la CPU (la que genera).
    const bool hay = remota_.is_object() && remota_.contains("medida") && remota_["medida"].is_object();
    cap::Muestra m = hay ? cap::muestraDe(remota_["medida"]) : cap::Muestra{};
    const cap::Muestra local = p_.medirMaquina ? p_.medirMaquina({}) : cap::Muestra{};
    m.t = ahora();
    m.cpuGenerador = -1;
    m.cpuEquipo = local.cpuMaquina;
    // Si lo del otro es viejo, no se juzga con eso.
    if (!hay || remota_.value("edadMs", 0) > 5000 || ahora() - remotaOkMs_ > 5000) {
        cap::Muestra vacia;
        vacia.t = m.t;
        vacia.cpuEquipo = m.cpuEquipo;
        return vacia;
    }
    return m;
}

void CapacidadViewModel::medirGrillaYAudio(bool grilla) {
    QPointer<CapacidadViewModel> yo(this);
    const int64_t t = ahora();
    if (!midiendoAudio_ && p_.medirAudio && t - ultimoAudioMs_ >= 5000) {
        midiendoAudio_ = true;
        ultimoAudioMs_ = t;
        p_.medirAudio([yo](double ms) {
            if (!yo) return;
            yo->midiendoAudio_ = false;
            yo->audioMs_ = ms;
        });
    }
    if (grilla && !midiendoGrilla_ && p_.medirGrilla) {
        midiendoGrilla_ = true;
        ultimaGrillaMs_ = t;
        const int seg = std::clamp(p_.tiempos.regimenS - 4, 2, 20);
        p_.medirGrilla(seg, [yo](double cps) {
            if (!yo) return;
            yo->midiendoGrilla_ = false;
            yo->cps_ = cps;
            if (yo->fase_ == QLatin1String("midiendo")) yo->cpsEscalon_ = cps;
        });
    }
}

void CapacidadViewModel::tic() {
    const int64_t t = ahora();
    const double uiMs = ultimoTic_ > 0 ? std::max<double>(0, static_cast<double>(t - ultimoTic_ - tic_.interval())) : 0;
    ultimoTic_ = t;
    if (estado_ == QLatin1String("local") || estado_ == QLatin1String("remoto")) ticRampa(uiMs);
    else if (estado_ == QLatin1String("recibiendo")) ticRecibir(uiMs);
    else if (estado_ == QLatin1String("internet")) ticInternet();
    else tic_.stop();
}

void CapacidadViewModel::ticRampa(double uiMs) {
    if (fase_ == QLatin1String("preparando")) {
        if (ahora() - faseDesde_ > kPrepararMaxS * 1000LL)
            terminarRampa(traducir("capacidad", "hinchada tardó demasiado en preparar su video de prueba"), true, false);
        else
            publicarProgreso();
        return;
    }
    if (fase_ == QLatin1String("abriendo")) {
        if (ahora() - faseDesde_ > 30000) terminarRampa(traducir("capacidad", "no se pudo abrir la prueba a tiempo"), true, false);
        return;
    }
    const bool local = estado_ == QLatin1String("local");
    const int64_t t = ahora();
    if (local) {
        if (t - renovadaMs_ >= kRenovarS * 1000LL) renovarCredencial();
        medirGrillaYAudio(false);
    } else {
        // El otro: se le pide la medida cada segundo (una a la vez).
        if (!pidiendoRemota_) {
            pidiendoRemota_ = true;
            QPointer<CapacidadViewModel> yo(this);
            p_.pedirRemota(this, url_, credencial_, huella_, [yo](const json& r) {
                if (!yo) return;
                yo->pidiendoRemota_ = false;
                if (r.value("ok", false)) {
                    yo->remota_ = r;
                    yo->remotaOkMs_ = yo->ahora();
                }
            });
        }
        if (remotaOkMs_ > 0 && t - remotaOkMs_ > kSinRemotoS * 1000LL) {
            terminarRampa(traducir("capacidad", "el otro equipo dejó de contestar (¿terminó de recibir o se cortó la red?)"), true, false);
            return;
        }
    }
    const cap::Muestra m = local ? muestraLocal(uiMs) : muestraRemota();
    delEscalon_.push_back(m);
    vivo_ = aMapa(cap::aJson(m));
    emit vivoChanged();
    // La guardia, cada segundo, antes que nada.
    if (const std::string clave = cap::claveGuardia(delEscalon_, p_.criterios); !clave.empty()) {
        cerrarEscalon(clave, cap::guardia(delEscalon_, p_.criterios));
        return;
    }
    const int n = plan_[indice_];
    const int64_t enEscalon = t - escalonDesde_;
    if (fase_ == QLatin1String("entrando")) {
        const bool entraron = m.conVideo >= static_cast<int>(std::ceil(p_.criterios.entraronMin * n));
        if (entraron || enEscalon >= p_.tiempos.entradaS() * 1000LL) {
            fase_ = QStringLiteral("asentando");
            faseDesde_ = t;
        }
    } else if (fase_ == QLatin1String("asentando")) {
        if (t - faseDesde_ >= p_.tiempos.asentarS * 1000LL) {
            fase_ = QStringLiteral("midiendo");
            faseDesde_ = t;
            regimen_.clear();
            if (local) medirGrillaYAudio(true);
        }
    } else if (fase_ == QLatin1String("midiendo")) {
        regimen_.push_back(m);
        if (t - faseDesde_ >= p_.tiempos.regimenS * 1000LL) {
            cerrarEscalon();
            return;
        }
    }
    // Nunca más de lo que dura un escalón.
    if (enEscalon >= p_.tiempos.maxEscalonS * 1000LL) {
        cerrarEscalon();
        return;
    }
    publicarProgreso();
}

void CapacidadViewModel::cerrarEscalon(const std::string& claveGuardia, const std::string& motivoGuardia) {
    const int n = plan_[indice_];
    std::vector<cap::Muestra> r = regimen_.empty() ? delEscalon_ : regimen_;
    // La grilla medida en este escalón vale para todo su régimen.
    if (cpsEscalon_ >= 0)
        for (cap::Muestra& m : r) m.cps = cpsEscalon_;
    const cap::Paso paso = cap::juzgar(n, r, estado_ == QLatin1String("local"), p_.criterios, claveGuardia, motivoGuardia);
    pasos_.push_back(paso);
    pasosUi_.push_back(aMapa(cap::aJson(paso)));
    emit pasosChanged();
    if (paso.guardia) {
        terminarRampa(llenar(traducir("capacidad", "la cortó la guardia: %1"), {motivoGuardia}), false, false);
        return;
    }
    if (paso.degradado) {
        terminarRampa(traducir("capacidad", "se degradó"), false, false);
        return;
    }
    ++indice_;
    if (indice_ >= plan_.size()) {
        terminarRampa(traducir("capacidad", "llegó al tope sin degradarse"), false, false);
        return;
    }
    empezarEscalon();
}

void CapacidadViewModel::publicarProgreso() {
    QVariantList esc;
    for (int n : plan_) esc << n;
    const int n = indice_ < plan_.size() ? plan_[indice_] : 0;
    const int64_t t = ahora();
    QString texto;
    if (fase_ == QLatin1String("preparando")) texto = tr("Preparando el video de prueba (solo la primera vez, unos minutos)…");
    else if (fase_ == QLatin1String("abriendo")) texto = tr("Abriendo la prueba…");
    else if (fase_ == QLatin1String("entrando")) texto = tr("Entrando: %n invitados de prueba", "", n);
    else if (fase_ == QLatin1String("asentando")) texto = tr("Se acomodan los %n…", "", n);
    else if (fase_ == QLatin1String("midiendo")) texto = tr("Midiendo con %n invitados", "", n);
    const double porEscalon = plan_.empty() ? 0 : 1.0 / static_cast<double>(plan_.size());
    const double dentro = std::clamp(static_cast<double>(t - escalonDesde_) / (p_.tiempos.maxEscalonS * 1000.0), 0.0, 1.0);
    progreso_ = QVariantMap{{"fase", fase_},
                            {"texto", texto},
                            {"escalon", n},
                            {"escalones", esc},
                            {"indice", static_cast<int>(indice_)},
                            {"avance", std::clamp(porEscalon * (static_cast<double>(indice_) + dentro), 0.0, 1.0)},
                            {"segundosEscalon", static_cast<int>((t - escalonDesde_) / 1000)},
                            {"maquina", maquinaMedida_}};
    emit progresoChanged();
}

void CapacidadViewModel::terminarRampa(const std::string& motivo, bool error, bool cancelada) {
    if (estado_ != QLatin1String("local") && estado_ != QLatin1String("remoto")) return;
    const bool local = estado_ == QLatin1String("local");
    const QString tipo = estado_;
    setEstado(QStringLiteral("terminando"));
    tic_.stop();
    if (p_.generador) p_.generador->parar();
    if (local) {
        cerrarCredencial(motivo);
        devolverCupo();
        ba::persist::cola().borrar(archivoEnCurso());
    }
    const int tope = plan_.empty() ? 0 : plan_.back();
    const cap::Resultado r = cap::resumir(pasos_, tope, cancelada, p_.criterios);
    json pasos = json::array();
    for (const cap::Paso& p : pasos_) pasos.push_back(cap::aJson(p));
    json inf{{"tipo", tipo.toStdString()},
             {"maquina", maquinaMedida_.toStdString()},
             {"generador", p_.maquina.toStdString()},
             {"fecha", inicioMs_},
             {"fin", ahora()},
             {"tope", tope},
             {"escalones", plan_},
             {"pasos", pasos},
             {"resultado", cap::aJson(r)},
             {"motivo", motivo},
             {"criterios",
              {{"cpuMaxPct", p_.criterios.cpuMaxPct},
               {"memAvisoMb", p_.criterios.memAvisoMb},
               {"cpsMin", p_.criterios.cpsMin},
               {"audioMaxMs", p_.criterios.audioMaxMs},
               {"entraronMin", p_.criterios.entraronMin}}}};
    if (error) inf["error"] = motivo;
    std::string frase = cap::frase(r);
    if (error && pasos_.empty()) frase = llenar(traducir("capacidad", "La prueba no pudo hacerse: %1"), {motivo});
    else if (error) frase += " " + llenar(traducir("capacidad", "(Se cortó: %1)"), {motivo});
    inf["frase"] = frase;
    if (local && !salida_.isEmpty()) inf["hinchada"] = salida_.toStdString();
    resultadoJson_ = inf;
    resultado_ = aMapa(inf);
    emit resultadoChanged();
    if (!pasos_.empty()) guardar(inf, tipo.toStdString());
    credencial_.clear();
    url_.clear();
    huella_.clear();
    fase_.clear();
    progreso_.clear();
    emit progresoChanged();
    setEstado(QStringLiteral("libre"));
    revisar();
    emit notify(qs(frase), error ? "error" : (r.degradoEn > 0 ? "warn" : "ok"));
}

// --- Recibir: que otro equipo me pruebe --------------------------------------------------------

QString CapacidadViewModel::empezarARecibir(int cupo, int minutos) {
    revisar();
    if (const QString no = puedeArrancar(); !no.isEmpty()) return no;
    if (cupo < 1 || cupo > topeMax())
        return qs(llenar(traducir("capacidad", "el cupo va de 1 a %1 invitados"), {std::to_string(topeMax())}));
    minutos = std::clamp(minutos, 1, kRecibirMaxMin);
    if (p_.medidaGuardia) {
        sala::prueba::Umbrales u;
        if (const std::string no = sala::prueba::puedeArrancar(p_.medidaGuardia(), u, cupo, false); !no.empty()) return qs(no);
    }
    setEstado(QStringLiteral("recibiendo"));
    inicioMs_ = ahora();
    recibirHastaMs_ = inicioMs_ + minutos * 60000LL;
    cupoRecibir_ = cupo;
    recibidas_.clear();
    cps_ = audioMs_ = -1;
    ultimoTic_ = 0;
    ultimoAudioMs_ = ultimaGrillaMs_ = 0;
    QPointer<CapacidadViewModel> yo(this);
    abrirCredencial([yo, cupo](bool ok, const QString& error) {
        if (!yo || yo->estado_ != QLatin1String("recibiendo")) return;
        if (!ok) {
            emit yo->notify(error, "error");
            yo->dejarDeRecibir();
            return;
        }
        const int antes = yo->p_.foto().value("maxGuests").toInt();
        if (antes > 0 && antes < cupo) {
            yo->cupoAntes_ = antes;
            yo->escribirEnCurso();
            yo->p_.cambiarCupo(cupo);
        }
        // Las direcciones y el certificado para el código.
        yo->p_.get("/api/v1/network", [yo, cupo](bool ok2, const QJsonObject& red) {
            if (!yo || yo->estado_ != QLatin1String("recibiendo")) return;
            QString lan, huella;
            if (ok2) {
                const QJsonObject https = red.value("https").toObject();
                const QUrl mejor(red.value("bestUrl").toString());
                if (https.value("enabled").toBool() && mejor.isValid() && !mejor.host().isEmpty()) {
                    lan = QStringLiteral("https://%1:%2").arg(mejor.host()).arg(https.value("port").toInt());
                    huella = https.value("fingerprint").toString();
                } else if (mejor.isValid() && !mejor.host().isEmpty()) {
                    lan = mejor.scheme() + "://" + mejor.host() + (mejor.port() > 0 ? ":" + QString::number(mejor.port()) : QString());
                }
            }
            const QVariantMap pg = yo->p_.pinggy ? yo->p_.pinggy() : QVariantMap{};
            const QString publica = pg.value("estado").toString() == QLatin1String("listo") ? pg.value("url").toString() : QString();
            cap::Codigo c;
            c.credencial = yo->credencial_.toStdString();
            c.maquina = yo->p_.maquina.toStdString();
            QVariantMap rec{{"activo", true}, {"cupo", cupo}, {"venceMs", static_cast<double>(yo->recibirHastaMs_)},
                            {"participantes", 0}, {"conVideo", 0}};
            if (!lan.isEmpty()) {
                c.url = lan.toStdString();
                c.huella = huella.toStdString();
                rec["url"] = lan;
                rec["codigo"] = qs(cap::armarCodigo(c));
            }
            if (!publica.isEmpty()) {
                c.url = publica.toStdString();
                c.huella.clear(); // pinggy tiene certificado de verdad
                rec["urlInternet"] = publica;
                rec["codigoInternet"] = qs(cap::armarCodigo(c));
            }
            if (lan.isEmpty() && publica.isEmpty()) {
                emit yo->notify(tr("La conferencia no tiene una dirección para que entre el otro equipo."), "error");
                yo->dejarDeRecibir();
                return;
            }
            yo->recibir_ = rec;
            emit yo->recibirChanged();
        });
    });
    tic_.start();
    emit notify(tr("Este equipo se deja probar: copiá el código en el otro equipo (pestaña Capacidad → Un sistema remoto)."), "ok");
    return {};
}

void CapacidadViewModel::ticRecibir(double uiMs) {
    const int64_t t = ahora();
    if (t >= recibirHastaMs_) {
        emit notify(tr("Terminó el tiempo para recibir la prueba."), "ok");
        dejarDeRecibir();
        return;
    }
    if (credencial_.isEmpty()) return; // todavía abriendo
    if (t - renovadaMs_ >= kRenovarS * 1000LL) renovarCredencial();
    // La grilla cada 30 s (unos segundos), el audio cada 5.
    medirGrillaYAudio(t - ultimaGrillaMs_ >= 30000);
    cap::Muestra m = muestraLocal(uiMs);
    m.cpuGenerador = -1; // el generador está en el otro equipo
    recibidas_.push_back(m);
    if (recibidas_.size() > 60) recibidas_.erase(recibidas_.begin());
    vivo_ = aMapa(cap::aJson(m));
    emit vivoChanged();
    json medida = cap::aJson(m);
    medida["cupo"] = cupoRecibir_;
    medida["maquina"] = p_.maquina.toStdString();
    p_.post("/api/v1/prueba-sala", QJsonObject{{"medida", QJsonDocument::fromJson(QByteArray::fromStdString(medida.dump())).object()}}, {});
    if (recibir_.value("participantes").toInt() != m.entraron || recibir_.value("conVideo").toInt() != m.conVideo) {
        recibir_["participantes"] = m.entraron;
        recibir_["conVideo"] = m.conVideo;
        emit recibirChanged();
    }
    // La guardia: si esta máquina se ahoga, se corta (echa a los de prueba).
    if (const std::string g = cap::guardia(recibidas_, p_.criterios); !g.empty()) {
        emit notify(qs(llenar(traducir("capacidad", "Se cortó la prueba que venía del otro equipo: %1"), {g})), "error");
        dejarDeRecibir();
    }
}

void CapacidadViewModel::dejarDeRecibir() {
    if (estado_ != QLatin1String("recibiendo")) return;
    tic_.stop();
    cerrarCredencial(traducir("capacidad", "terminó de recibir"));
    devolverCupo();
    ba::persist::cola().borrar(archivoEnCurso());
    recibir_.clear();
    emit recibirChanged();
    setEstado(QStringLiteral("libre"));
    revisar();
}

// --- La conexión a internet ----------------------------------------------------------------

QString CapacidadViewModel::medirInternet(bool prenderPinggy) {
    if (ocupado()) return qs(traducir("capacidad", "ya hay una prueba de capacidad en curso"));
    if (!p_.online || !p_.online()) return qs(traducir("capacidad", "la conferencia no está andando"));
    if (p_.otraPrueba) {
        if (const QString o = p_.otraPrueba(); !o.isEmpty()) return o;
    }
    const QVariantMap pg = p_.pinggy ? p_.pinggy() : QVariantMap{};
    const bool pinggyListo = pg.value("via").toString() == QLatin1String("pinggy") && pg.value("estado").toString() == QLatin1String("listo");
    if (prenderPinggy && !pinggyListo) {
        // Prenderlo reinicia la conferencia: solo sin nadie y sin nada al aire.
        revisar();
        if (!riesgo_.isEmpty()) return riesgo_;
        if (pg.value("activo").toBool() && pg.value("via").toString() != QLatin1String("pinggy"))
            return qs(traducir("capacidad", "la conferencia sale por otro túnel (Cloudflare): no se prende pinggy encima"));
    }
    setEstado(QStringLiteral("internet"));
    inicioMs_ = ahora();
    pinggyLoPrendi_ = false;
    internetParcial_ = json{{"tipo", "internet"}, {"maquina", p_.maquina.toStdString()}, {"fecha", inicioMs_}};
    cancelarRed_ = std::make_shared<std::atomic<bool>>(false);
    // Primero el túnel WireGuard, si está.
    if (hayTunel() && p_.medirEnlace) {
        if (const QString e = p_.medirEnlace(); e.isEmpty()) {
            faseInternet_ = QStringLiteral("wireguard");
            faseInternetDesde_ = ahora();
        } else {
            internetParcial_["wireguard"] = {{"nota", e.toStdString()}};
            faseInternet_.clear();
        }
    }
    if (faseInternet_.isEmpty()) {
        pinggyLoPrendi_ = prenderPinggy;
        internetPinggy();
    } else {
        pinggyLoPrendi_ = prenderPinggy; // se usa al pasar a pinggy (si sigue apagado)
    }
    // (Si ya terminó —sin nada que medir o con todo a mano—, no queda nada en curso.)
    if (estado_ == QLatin1String("internet")) {
        if (progreso_.isEmpty()) {
            progreso_ = QVariantMap{{"fase", faseInternet_}, {"texto", tr("Midiendo la conexión…")}};
            emit progresoChanged();
        }
        tic_.start();
    }
    return {};
}

void CapacidadViewModel::alTerminarEnlace() {
    if (estado_ != QLatin1String("internet") || faseInternet_ != QLatin1String("wireguard")) return;
    const json u = p_.ultimoEnlace ? p_.ultimoEnlace() : json();
    if (u.is_object() && u.value("fecha", 0.0) * 1000 >= static_cast<double>(inicioMs_) - 1000) {
        const sala::enlace::Medicion m = sala::enlace::deResultado(u);
        internetParcial_["wireguard"] = {{"medicion", u}};
        const int cupo = std::max(1, p_.foto().value("maxGuests").toInt());
        if (p_.juzgarEnlace) internetParcial_["wireguard"]["cuenta"] = p_.juzgarEnlace(m, cupo);
    } else {
        internetParcial_["wireguard"] = {{"nota", traducir("capacidad", "la medición por el túnel no terminó bien (ver Sala → Enlace)")}};
    }
    internetPinggy();
}

void CapacidadViewModel::internetPinggy() {
    // `pinggyLoPrendi_` acá dice si el operador PIDIÓ prenderlo; abajo pasa a decir si se prendió.
    const bool pedir = pinggyLoPrendi_;
    pinggyLoPrendi_ = false;
    const QVariantMap pg = p_.pinggy ? p_.pinggy() : QVariantMap{};
    const bool listo = pg.value("via").toString() == QLatin1String("pinggy") && pg.value("estado").toString() == QLatin1String("listo");
    if (listo) {
        faseInternet_ = QStringLiteral("pinggy");
        medirPorPinggy(pg.value("url").toString());
        return;
    }
    if (!pedir) {
        internetParcial_["pinggy"] = {{"nota", traducir("capacidad", "pinggy está apagado: para medir el camino de los invitados, «Prender pinggy y medir»")}};
        terminarInternet({});
        return;
    }
    const QString e = p_.prenderPinggy ? p_.prenderPinggy() : qs(traducir("capacidad", "no disponible"));
    if (!e.isEmpty()) {
        internetParcial_["pinggy"] = {{"nota", e.toStdString()}};
        terminarInternet({});
        return;
    }
    pinggyLoPrendi_ = true;
    escribirEnCurso(); // si el programa se cierra en el medio, al volver se apaga
    faseInternet_ = QStringLiteral("esperando_pinggy");
    faseInternetDesde_ = ahora();
    progreso_ = QVariantMap{{"fase", faseInternet_}, {"texto", tr("Prendiendo pinggy (la conferencia se reinicia)…")}};
    emit progresoChanged();
}

void CapacidadViewModel::alCambiarPinggy() {
    const QVariantMap pg = p_.pinggy ? p_.pinggy() : QVariantMap{};
    if (pg != pinggy_) {
        pinggy_ = pg;
        emit pinggyChanged();
    }
    if (estado_ == QLatin1String("internet") && faseInternet_ == QLatin1String("esperando_pinggy")) ticInternet();
}

void CapacidadViewModel::ticInternet() {
    if (faseInternet_ == QLatin1String("wireguard")) {
        // Por si el aviso de que terminó no llega: con el enlace quieto, se sigue.
        if (p_.enlaceMidiendo && !p_.enlaceMidiendo() && ahora() - faseInternetDesde_ > 3000) alTerminarEnlace();
        else if (ahora() - faseInternetDesde_ > 120000) {
            if (p_.cancelarEnlace) p_.cancelarEnlace();
            internetParcial_["wireguard"] = {{"nota", traducir("capacidad", "la medición por el túnel tardó demasiado")}};
            internetPinggy();
        }
        return;
    }
    if (faseInternet_ == QLatin1String("esperando_pinggy")) {
        const QVariantMap pg = p_.pinggy ? p_.pinggy() : QVariantMap{};
        if (pg.value("via").toString() == QLatin1String("pinggy") && pg.value("estado").toString() == QLatin1String("listo") &&
            p_.online && p_.online()) {
            faseInternet_ = QStringLiteral("pinggy");
            medirPorPinggy(pg.value("url").toString());
        } else if (pg.value("estado").toString() == QLatin1String("error") || ahora() - faseInternetDesde_ > kEsperaPinggyS * 1000LL) {
            internetParcial_["pinggy"] = {{"nota", pg.value("estado").toString() == QLatin1String("error")
                                                       ? pg.value("texto").toString().toStdString()
                                                       : traducir("capacidad", "pinggy no se conectó a tiempo")}};
            terminarInternet({});
        }
    }
}

void CapacidadViewModel::medirPorPinggy(const QString& url) {
    progreso_ = QVariantMap{{"fase", QStringLiteral("pinggy")}, {"texto", tr("Midiendo por pinggy (ida y vuelta)…")}};
    emit progresoChanged();
    QPointer<CapacidadViewModel> yo(this);
    p_.post("/api/v1/capacidad/red", QJsonObject{{"duracionS", 60}}, [yo, url](bool ok, const QJsonObject& r) {
        if (!yo || yo->estado_ != QLatin1String("internet")) return;
        const QString secreto = r.value("secreto").toString();
        if (!ok || secreto.isEmpty()) {
            yo->internetParcial_["pinggy"] = {{"nota", traducir("capacidad", "esta versión de la conferencia no sabe medir por el túnel")}};
            yo->terminarInternet({});
            return;
        }
        yo->p_.medirIdaYVuelta(yo, url, secreto, yo->cancelarRed_, [yo, url](const json& m) {
            if (!yo || yo->estado_ != QLatin1String("internet")) return;
            if (yo->p_.del) yo->p_.del("/api/v1/capacidad/red");
            json pg{{"url", url.toStdString()}, {"medicion", m}};
            if (m.value("ok", false)) {
                const sala::enlace::Medicion med = cap::idaYVuelta(m.value("kbps", 0.0), m.value("rttMs", -1.0),
                                                                    m.value("rttCargaMs", -1.0), m.value("perdida", 0.0));
                const int cupo = std::max(1, yo->p_.foto().value("maxGuests").toInt());
                if (yo->p_.juzgarEnlace) pg["cuenta"] = yo->p_.juzgarEnlace(med, cupo);
            } else {
                pg["nota"] = m.value("error", traducir("capacidad", "no se pudo medir por pinggy"));
            }
            yo->internetParcial_["pinggy"] = pg;
            yo->terminarInternet({});
        });
    });
}

void CapacidadViewModel::terminarInternet(const std::string& nota) {
    if (estado_ != QLatin1String("internet")) return;
    tic_.stop();
    if (pinggyLoPrendi_) {
        if (p_.apagarPinggy) p_.apagarPinggy();
        ba::persist::cola().borrar(archivoEnCurso());
    }
    pinggyLoPrendi_ = false;
    faseInternet_.clear();
    json inf = internetParcial_;
    inf["fin"] = ahora();
    if (!nota.empty()) inf["nota"] = nota;
    // Con esta conexión entran N: lo peor de los caminos medidos (los invitados usan pinggy;
    // el túnel, si está, es otro camino posible).
    int invitados = -1;
    std::vector<std::string> partes;
    for (const char* camino : {"pinggy", "wireguard"}) {
        const json c = inf.value(camino, json::object()).value("cuenta", json());
        if (!c.is_object()) continue;
        const int n = c.value("entranConMargen", -1);
        if (n >= 0) invitados = invitados < 0 ? n : std::min(invitados, n);
    }
    inf["invitados"] = invitados;
    std::string frase;
    if (!nota.empty()) frase = nota;
    else if (invitados >= 0)
        frase = llenar(traducir("capacidad", "Con esta conexión entran %1 invitados con margen (como mucho 4 en alta, el resto en baja)."),
                       {std::to_string(invitados)});
    else
        frase = traducir("capacidad", "No se pudo medir la conexión: mirá las notas de cada camino.");
    if (const json pg = inf.value("pinggy", json::object()).value("medicion", json()); pg.is_object() && pg.value("ok", false))
        frase += " " + llenar(traducir("capacidad", "Por pinggy: %1 Mbps ida y vuelta, %2 ms de demora."),
                              {std::to_string(pg.value("kbps", 0) / 1000), std::to_string(pg.value("rttMs", 0))});
    inf["frase"] = frase;
    internetJson_ = inf;
    internet_ = aMapa(inf);
    emit internetChanged();
    if (invitados >= 0 || inf.contains("pinggy")) guardar(inf, "internet");
    progreso_.clear();
    emit progresoChanged();
    setEstado(QStringLiteral("libre"));
    revisar();
    emit notify(qs(frase), invitados >= 0 ? "ok" : "warn");
}

// --- Cancelar, limpiar, el historial ----------------------------------------------------

void CapacidadViewModel::cancelar() {
    if (estado_ == QLatin1String("local") || estado_ == QLatin1String("remoto")) {
        terminarRampa(traducir("capacidad", "cancelada a mano"), false, true);
    } else if (estado_ == QLatin1String("recibiendo")) {
        dejarDeRecibir();
    } else if (estado_ == QLatin1String("internet")) {
        if (cancelarRed_) *cancelarRed_ = true;
        if (faseInternet_ == QLatin1String("wireguard") && p_.cancelarEnlace) p_.cancelarEnlace();
        if (p_.del) p_.del("/api/v1/capacidad/red");
        terminarInternet(traducir("capacidad", "cancelada a mano"));
    }
}

void CapacidadViewModel::alEventoSala(const QString& nombre, const QJsonObject& ev) {
    if (nombre != QLatin1String("prueba-sala")) return;
    // La cerró el gateway (venció: nadie la renovó a tiempo) o alguien más.
    if (ev.contains("motivo") && !ev.value("activa").toBool()) {
        if (estado_ == QLatin1String("local") && !credencial_.isEmpty())
            terminarRampa(llenar(traducir("capacidad", "la cerró la conferencia (%1)"), {ev.value("motivo").toString().toStdString()}), true,
                          false);
        else if (estado_ == QLatin1String("recibiendo") && !credencial_.isEmpty()) {
            // (Cuando la cierra este mismo modelo, la credencial ya se olvidó antes.)
            credencial_.clear();
            emit notify(tr("La conferencia cerró la prueba que venía del otro equipo (%1).").arg(ev.value("motivo").toString()), "warn");
            dejarDeRecibir();
        }
    }
}

void CapacidadViewModel::alCambiarOnline(bool on) {
    if (on && colgadaPendiente_) limpiarColgada();
    revisar();
}

void CapacidadViewModel::limpiarColgada() {
    colgadaPendiente_ = false;
    const int cupoAntes = colgada_.value("cupoAntes", 0);
    // pinggy prendido por la medición de la conexión: se apaga.
    if (colgada_.value("pinggy", false) && p_.apagarPinggy) p_.apagarPinggy();
    QPointer<CapacidadViewModel> yo(this);
    p_.post("/api/v1/prueba-sala/cerrar", QJsonObject{{"motivo", "capacidad: quedó de antes"}}, [yo, cupoAntes](bool, const QJsonObject&) {
        if (!yo) return;
        if (cupoAntes > 0 && yo->p_.cambiarCupo) yo->p_.cambiarCupo(cupoAntes);
        ba::persist::cola().borrar(yo->archivoEnCurso());
        emit yo->notify(tr("Había una prueba de capacidad sin terminar (el programa se cerró en el medio): ya se limpió."), "warn");
    });
}

void CapacidadViewModel::guardar(const json& informe, const std::string& tipo) {
    const std::string nombre = cap::nombreArchivo(informe.value("fecha", ahora()), informe.value("maquina", p_.maquina.toStdString()), tipo);
    ba::persist::cola().escribir(dirHistorial() / nombre, informe.dump(1, ' ', false, json::error_handler_t::replace));
    historial_.prepend(aMapa(resumenDe(informe, nombre)));
    while (historial_.size() > 50) historial_.removeLast();
    emit historialChanged();
}

void CapacidadViewModel::recargarHistorial() {
    std::vector<std::pair<std::string, json>> leidos;
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(dirHistorial(), ec)) {
        if (!e.is_regular_file(ec) || e.path().extension() != ".json") continue;
        std::string s;
        if (!ba::persist::cola().leer(e.path(), s)) continue;
        json j = json::parse(s, nullptr, false);
        if (!j.is_object()) continue;
        leidos.emplace_back(e.path().filename().string(), std::move(j));
    }
    std::sort(leidos.begin(), leidos.end(),
              [](const auto& a, const auto& b) { return a.second.value("fecha", int64_t{0}) > b.second.value("fecha", int64_t{0}); });
    QVariantList h;
    bool hayCapacidad = !resultadoJson_.is_null(), hayInternet = !internetJson_.is_null();
    for (const auto& [archivo, inf] : leidos) {
        if (h.size() < 50) h << aMapa(resumenDe(inf, archivo));
        const std::string tipo = inf.value("tipo", std::string());
        // Lo último de cada cosa, para mostrar al abrir (sin correr nada).
        if (!hayCapacidad && (tipo == "local" || tipo == "remoto")) {
            resultadoJson_ = inf;
            resultado_ = aMapa(inf);
            pasosUi_ = aLista(inf.value("pasos", json::array()));
            hayCapacidad = true;
            emit resultadoChanged();
            emit pasosChanged();
        }
        if (!hayInternet && tipo == "internet") {
            internetJson_ = inf;
            internet_ = aMapa(inf);
            hayInternet = true;
            emit internetChanged();
        }
    }
    if (h == historial_) return;
    historial_ = h;
    emit historialChanged();
}

QString CapacidadViewModel::exportar(const QString& destino) {
    QString ruta = destino.trimmed();
    if (ruta.startsWith(QLatin1String("file:"))) ruta = QUrl(ruta).toLocalFile();
    if (ruta.isEmpty()) return qs(traducir("capacidad", "elegí dónde guardarlo"));
    QByteArray datos;
    if (ruta.endsWith(QLatin1String(".csv"), Qt::CaseInsensitive)) {
        const auto campo = [](QString s) {
            s.replace('"', QStringLiteral("\"\""));
            return '"' + s + '"';
        };
        datos += "fecha,tipo,maquina,con_margen,maximo,limita,invitados_por_conexion,resultado\n";
        for (const QVariant& v : historial_) {
            const QVariantMap m = v.toMap();
            const QString fecha = QDateTime::fromMSecsSinceEpoch(static_cast<qint64>(m.value("fecha").toDouble())).toString(Qt::ISODate);
            datos += (QStringList{fecha, m.value("tipo").toString(), campo(m.value("maquina").toString()),
                                  m.value("conMargen").toString(), m.value("maximo").toString(), campo(m.value("limita").toString()),
                                  m.value("invitados").toString(), campo(m.value("frase").toString())}
                          .join(',') +
                      '\n')
                         .toUtf8();
        }
    } else {
        const json todo{{"maquina", p_.maquina.toStdString()},
                        {"capacidad", resultadoJson_},
                        {"internet", internetJson_},
                        {"exportado", ahora()}};
        datos = QByteArray::fromStdString(todo.dump(1, ' ', false, json::error_handler_t::replace));
    }
    QFile f(ruta);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return tr("no se pudo escribir %1").arg(ruta);
    if (f.write(datos) != datos.size()) return tr("no se pudo escribir %1").arg(ruta);
    f.close();
    emit notify(tr("Exportado: %1").arg(ruta), "ok");
    return {};
}

// --- Para el MCP y el chequeo ------------------------------------------------------------

json CapacidadViewModel::estadoJson(bool conCodigo) const {
    json j{{"estado", estado_.toStdString()},
           {"riesgo", riesgo_.toStdString()},
           {"maquina", p_.maquina.toStdString()},
           {"hinchada_disponible", hinchadaDisponible()},
           {"progreso", deQJson(QJsonObject::fromVariantMap(progreso_))},
           {"vivo", deQJson(QJsonObject::fromVariantMap(vivo_))},
           {"pasos", json::array()},
           {"pinggy", deQJson(QJsonObject::fromVariantMap(pinggy_))},
           {"tunel_wireguard", hayTunel()}};
    for (const cap::Paso& p : pasos_) j["pasos"].push_back(cap::aJson(p));
    if (!recibir_.isEmpty()) {
        QVariantMap r = recibir_;
        if (!conCodigo) {
            r.remove("codigo");
            r.remove("codigoInternet");
        }
        j["recibir"] = deQJson(QJsonObject::fromVariantMap(r));
    }
    if (resultadoJson_.is_object()) {
        json r = resultadoJson_;
        r.erase("pasos");
        j["ultimo_resultado"] = r;
    }
    if (internetJson_.is_object()) j["ultima_conexion"] = internetJson_;
    return j;
}

json CapacidadViewModel::ultimoDeEstaMaquina() const {
    for (const QVariant& v : historial_) {
        const QVariantMap m = v.toMap();
        if (m.value("tipo").toString() != QLatin1String("local") || m.value("maquina").toString() != p_.maquina) continue;
        std::string s;
        if (ba::persist::cola().leer(dirHistorial() / m.value("archivo").toString().toStdString(), s)) {
            json j = json::parse(s, nullptr, false);
            if (j.is_object()) return j;
        }
        // Recién guardado (la cola todavía no escribió): el de memoria.
        if (resultadoJson_.is_object() && resultadoJson_.value("tipo", std::string()) == "local") return resultadoJson_;
    }
    return nullptr;
}

json CapacidadViewModel::ultimoInternet() const { return internetJson_.is_object() ? internetJson_ : json(); }

QVariantList CapacidadViewModel::chequeoPrevio(int cupoEvento) const {
    QVariantList out;
    if (cupoEvento <= 0) return out;
    QVariantMap ultimo;
    for (const QVariant& v : historial_) {
        const QVariantMap m = v.toMap();
        if (m.value("tipo").toString() == QLatin1String("local") && m.value("maquina").toString() == p_.maquina) {
            ultimo = m;
            break;
        }
    }
    if (ultimo.isEmpty()) {
        out << QVariantMap{{"grave", false},
                           {"texto", tr("Capacidad de esta máquina sin medir: Configuración → Sistema → Capacidad dice cuántos invitados aguanta.")}};
    } else {
        const int n = ultimo.value("conMargen").toInt();
        const QString cuando = QDateTime::fromMSecsSinceEpoch(static_cast<qint64>(ultimo.value("fecha").toDouble())).toString("dd/MM/yyyy");
        if (n < cupoEvento)
            out << QVariantMap{{"grave", false},
                               {"texto", tr("Capacidad: el %1 esta máquina aguantó %2 invitados con margen y el evento tiene lugar para %3.")
                                             .arg(cuando)
                                             .arg(n)
                                             .arg(cupoEvento)}};
    }
    for (const QVariant& v : historial_) {
        const QVariantMap m = v.toMap();
        if (m.value("tipo").toString() != QLatin1String("internet")) continue;
        const int n = m.value("invitados").toInt();
        if (n >= 0 && n < cupoEvento)
            out << QVariantMap{{"grave", false},
                               {"texto", tr("Conexión: según la última medición entran %1 invitados con margen y el evento tiene lugar para %2.")
                                             .arg(n)
                                             .arg(cupoEvento)}};
        break;
    }
    return out;
}

} // namespace ba::app
