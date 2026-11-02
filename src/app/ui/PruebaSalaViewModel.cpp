// src/app/ui/PruebaSalaViewModel.cpp — la prueba de sala (ver el .hpp)

#include "ui/PruebaSalaViewModel.hpp"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcessEnvironment>
#include <QUrl>

#include <system_error>

#include "diag/Recursos.hpp"
#include "i18n/I18n.hpp"
#include "persist/ColaEscritura.hpp"
#include "platform/Paths.hpp"
#include "sala/CupoSala.hpp"
#include "sala/MedidaMaquina.hpp"
#include "ui/ConferenceClient.hpp"
#include "ui/EventoViewModel.hpp"
#include "ui/GuionViewModel.hpp"
#include "ui/RecursosViewModel.hpp"

namespace ba::app {

namespace pr = sala::prueba;
using json = nlohmann::json;
namespace fs = std::filesystem;

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
/// La CPU (% de la máquina) de uno de nuestros procesos («app», «gateway»), -1 si no se sabe.
double cpuDe(const diag::Recursos::Muestra& m, const char* nombre) {
    for (const auto& p : m.procesos)
        if (p.nombre == nombre) return p.cpu;
    return -1;
}
/// El ítem de la lista de chequeo del evento donde queda el resultado.
constexpr const char* kItemChequeo = "prueba_sala";
} // namespace

PruebaSalaViewModel::PruebaSalaViewModel(Partes p, QObject* parent) : QObject(parent), p_(std::move(p)) {
    tic_.setInterval(1000);
    connect(&tic_, &QTimer::timeout, this, &PruebaSalaViewModel::tic);
    // Quién entró, quién tiene video y quién se cortó: la foto de la sala (sin pedirla).
    connect(&p_.sala, &ConferenceClient::snapshotChanged, this, [this]() {
        if (!corriendo()) return;
        const json snap = deQJson(p_.sala.snapshot());
        if (snap.is_object()) informe_.participantes(ahoraMs(), snap.value("participants", json::array()));
        const bool d = p_.sala.snapshot().value("directa").toBool();
        if (d != directa_) {
            directa_ = d;
            emit entradaDirectaChanged();
        }
    });
    connect(&p_.sala, &ConferenceClient::estadoEvento, this, &PruebaSalaViewModel::alPruebaSala);
    // ¿Quedó una prueba a medias (el programa se cerró en el medio)? Se termina de limpiar
    // en cuanto la sala conteste.
    std::string s;
    if (ba::persist::cola().leer(archivoEnCurso(), s)) {
        colgada_ = json::parse(s, nullptr, false);
        colgadaPendiente_ = colgada_.is_object();
    }
    connect(&p_.sala, &ConferenceClient::onlineChanged, this, [this](bool on) {
        if (on && colgadaPendiente_) limpiarColgada();
    });
    if (colgadaPendiente_ && p_.sala.online()) QTimer::singleShot(0, this, [this]() { limpiarColgada(); });
}

PruebaSalaViewModel::~PruebaSalaViewModel() {
    // Al cerrar el programa: el probador se corta YA (es un hijo nuestro). La sala revoca
    // la credencial sola en kVidaCredencialS (nadie la renueva) y echa a los de prueba;
    // lo que falte (el cupo) se termina al volver a arrancar (archivoEnCurso).
    if (hinchada_ && hinchada_->state() != QProcess::NotRunning) {
        hinchada_->kill();
        hinchada_->waitForFinished(2000);
    }
    if (corriendo()) p_.sala.post("/api/v1/prueba-sala/cerrar", QJsonObject{{"motivo", "cierre"}});
}

int64_t PruebaSalaViewModel::ahoraMs() { return QDateTime::currentMSecsSinceEpoch(); }

fs::path PruebaSalaViewModel::archivoEnCurso() const { return p_.dataRoot / "Runtime" / "prueba-sala-en-curso.json"; }

QString PruebaSalaViewModel::rutaHinchada() const {
#ifdef _WIN32
    return QCoreApplication::applicationDirPath() + "/hinchada/hinchada.exe";
#else
    return QCoreApplication::applicationDirPath() + "/hinchada/hinchada";
#endif
}

bool PruebaSalaViewModel::hinchadaDisponible() const { return QFileInfo(rutaHinchada()).isExecutable(); }

void PruebaSalaViewModel::setEstado(const QString& e) {
    if (estado_ == e) return;
    estado_ = e;
    emit estadoChanged();
}

QString PruebaSalaViewModel::comandoExterno() const {
    if (credencial_.isEmpty() || url_.isEmpty()) return {};
    return QStringLiteral("HINCHADA_PIN=%1 hinchada --servidor gateway --url %2 --inseguro --participantes %3 "
                          "--modo grabado --hablan %4 --durante %5m")
        .arg(credencial_, url_)
        .arg(pedidos_ > 0 ? pedidos_ : 10)
        .arg(hablan_ > 0 ? QString::number(hablan_) : QStringLiteral("ritmo"))
        .arg(minutos_);
}

void PruebaSalaViewModel::revisarMaquina(int n, bool conHinchada) {
    const pr::Medida m = pr::medirMaquina();
    maquina_ = QVariantMap{{"memLibreMb", m.memLibreMb},
                           {"carga", m.carga},
                           {"hilosApp", m.hilosApp},
                           {"puede", qs(pr::puedeArrancar(m, umbrales_, n, conHinchada && hinchadaDisponible()))}};
    emit maquinaChanged();
}

// --- Arrancar -------------------------------------------------------------------------------

QString PruebaSalaViewModel::arrancar(int n, int minutos, bool conHinchada, int hablan) {
    using ba::i18n::traducir;
    if (estado_ != QLatin1String("apagada")) return qs(traducir("sala", "ya hay una prueba de sala en curso"));
    if (!p_.sala.online()) return qs(traducir("sala", "la conferencia no está andando"));
    if (!p_.evento.eventoAbierto().hay())
        return qs(traducir("sala", "abrí el evento que querés probar: el informe va a su lista de chequeo"));
    if (minutos < 1 || minutos > 120) return qs(traducir("sala", "la prueba dura de 1 a 120 minutos"));
    if (conHinchada && !hinchadaDisponible())
        return qs(traducir("sala", "el probador (hinchada) no viene en este programa: usá un generador externo"));
    // La guardia, antes que nada.
    const pr::Medida m = pr::medirMaquina();
    const std::string no = pr::puedeArrancar(m, umbrales_, n, conHinchada);
    revisarMaquina(n, conHinchada);
    if (!no.empty()) return qs(no);

    generador_ = conHinchada ? QStringLiteral("hinchada") : QStringLiteral("externo");
    pedidos_ = n;
    minutos_ = minutos;
    hablan_ = hablan;
    inicioMs_ = ahoraMs();
    // Con hinchada: lo que dura más lo que tarda en entrar y, la primera vez, en grabar
    // el video de prueba (unos minutos). Afuera: lo que se pidió.
    topeMs_ = inicioMs_ + static_cast<int64_t>(minutos) * 60000 + (conHinchada ? 10 * 60000 : 0);
    informe_.empezar(inicioMs_, n, generador_.toStdString(), p_.evento.eventoAbierto().nombre().toStdString());
    cupoAntes_ = 0;
    setEstado(QStringLiteral("corriendo"));
    // Por si el programa se cierra en el medio: lo que hay que deshacer al volver.
    ba::persist::cola().escribir(archivoEnCurso(), json{{"inicioMs", inicioMs_}, {"cupoAntes", 0}}.dump());

    QPointer<PruebaSalaViewModel> yo(this);
    p_.sala.post("/api/v1/prueba-sala", QJsonObject{{"duracionS", kVidaCredencialS}},
                 [yo, n, minutos, conHinchada, hablan](bool ok, const QJsonObject& r) {
        if (!yo || !yo->corriendo()) return;
        if (!ok || !r.value("ok").toBool()) {
            yo->terminar(ba::i18n::traducir("sala", "la conferencia no abrió la prueba"), false);
            return;
        }
        yo->credencial_ = r.value("credencial").toString();
        yo->renovadaMs_ = ahoraMs();
        // El cupo: tiene que entrar la gente de prueba (y los que ya estaban).
        const int cupo = yo->p_.sala.snapshot().value("maxGuests").toInt();
        if (cupo > 0 && cupo < n) {
            yo->cupoAntes_ = cupo;
            ba::persist::cola().escribir(yo->archivoEnCurso(),
                                         json{{"inicioMs", yo->inicioMs_}, {"cupoAntes", cupo}}.dump());
            yo->p_.cupo.cambiar(n);
        }
        // Por dónde entran: el probador local, por la máquina misma; uno externo, por la red.
        yo->p_.sala.get("/api/v1/network", [yo, n, minutos, conHinchada, hablan](bool ok2, const QJsonObject& red) {
            if (!yo || !yo->corriendo()) return;
            QString local, lan;
            if (ok2) {
                const QJsonObject https = red.value("https").toObject();
                if (https.value("enabled").toBool())
                    local = QStringLiteral("https://127.0.0.1:%1").arg(https.value("port").toInt());
                const QUrl mejor(red.value("bestUrl").toString());
                if (mejor.isValid() && !mejor.host().isEmpty())
                    lan = mejor.scheme() + "://" + mejor.host() + (mejor.port() > 0 ? ":" + QString::number(mejor.port()) : QString());
            }
            yo->url_ = conHinchada ? local : (lan.isEmpty() ? local : lan);
            emit yo->accesoChanged();
            if (!conHinchada) {
                emit yo->notify(tr("Prueba de sala abierta: conectá el generador externo con la credencial de prueba."), "ok");
                return;
            }
            if (local.isEmpty()) {
                yo->terminar(ba::i18n::traducir("sala", "la conferencia no tiene HTTPS: el probador no puede entrar"), false);
                return;
            }
            yo->lanzarHinchada(n, minutos, hablan);
        });
    });
    tic_.start();
    emit accesoChanged();
    return {};
}

void PruebaSalaViewModel::lanzarHinchada(int n, int minutos, int hablan) {
    const fs::path dirEvento = p_.evento.eventoAbierto().dir();
    const QString fecha = QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss");
    salidaHinchada_ = QString::fromStdString((dirEvento / "Pruebas").string()) + "/hinchada-" + fecha;
    QDir().mkpath(salidaHinchada_);
    // El video grabado de hinchada (~100 MB) es uno para todas las configuraciones.
    const QString grabado = QString::fromStdString((ba::platform::dataRoot() / "Runtime" / "hinchada").string());
    QDir().mkpath(grabado);
    auto* proc = new QProcess(this);
    hinchada_ = proc;
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert("HINCHADA_PIN", credencial_);
    proc->setProcessEnvironment(env);
    // Su consola a un archivo: sin tuberías que se llenen y lo traben.
    proc->setStandardOutputFile(salidaHinchada_ + "/consola.txt");
    proc->setStandardErrorFile(salidaHinchada_ + "/consola.txt", QIODevice::Append);
    const QStringList args{"--servidor", "gateway", "--url", url_, "--inseguro",
                           "--participantes", QString::number(n),
                           // Nadie codifica: cada uno manda en loop el patrón con su número.
                           "--modo", "grabado", "--grabado", grabado + "/grabado.hgr",
                           "--hablan", hablan > 0 ? QString::number(hablan) : QStringLiteral("ritmo"),
                           "--durante", QString::number(minutos) + "m",
                           "--entrada-cada", "300ms",
                           "--salida", salidaHinchada_,
                           "--vigilar", "ba-gateway,ba-app"};
    QPointer<PruebaSalaViewModel> yo(this);
    connect(proc, &QProcess::finished, this, [yo, proc](int codigo, QProcess::ExitStatus) {
        proc->deleteLater();
        if (!yo || !yo->corriendo()) return;
        yo->terminar(codigo == 0 ? ba::i18n::traducir("sala", "el probador terminó")
                                 : ba::i18n::llenar(ba::i18n::traducir("sala", "el probador terminó con error (código %1): ver consola.txt"),
                                                    {std::to_string(codigo)}),
                     false);
    });
    connect(proc, &QProcess::errorOccurred, this, [yo](QProcess::ProcessError e) {
        if (!yo || !yo->corriendo() || e != QProcess::FailedToStart) return;
        yo->terminar(ba::i18n::traducir("sala", "no arrancó el probador (hinchada)"), false);
    });
    proc->start(rutaHinchada(), args);
    emit notify(tr("Prueba de sala: entran %n participantes de prueba (la primera vez, el probador graba su video unos minutos).",
                   "", n),
                "ok");
}

// --- Mientras dura ----------------------------------------------------------------------

void PruebaSalaViewModel::tic() {
    if (!corriendo()) {
        tic_.stop();
        return;
    }
    const int64_t ahora = ahoraMs();
    const pr::Medida m = pr::medirMaquina();
    pr::Muestra mu;
    mu.t = ahora;
    // El ancho de banda: la suma de lo que dice la conexión de cada uno (GetStats: todo lo
    // que sube y le baja, audio y retorno incluidos; se actualiza cada ~6 s). Sin eso, el
    // contador de video del gateway (no cuenta el retorno compartido).
    const QJsonObject snap = p_.sala.snapshot();
    double sube = 0, baja = 0;
    bool conRed = false;
    for (const QJsonValue& v : snap.value("participants").toArray()) {
        const QJsonObject red = v.toObject().value("red").toObject();
        if (!v.toObject().value("connected").toBool() || !red.contains("kbps_in")) continue;
        conRed = true;
        sube += red.value("kbps_in").toDouble();
        baja += red.value("kbps_out").toDouble();
    }
    const QJsonObject uso = snap.value("uso").toObject();
    if (conRed) {
        mu.kbpsSubida = sube;
        mu.kbpsBajada = baja;
    } else {
        if (uso.contains("kbpsEntrada")) mu.kbpsSubida = uso.value("kbpsEntrada").toDouble();
        if (uso.contains("kbpsSalida")) mu.kbpsBajada = uso.value("kbpsSalida").toDouble();
    }
    const diag::Recursos::Muestra r = p_.recursos.muestreador().ultima();
    mu.cpuApp = cpuDe(r, "app");
    mu.cpuGateway = cpuDe(r, "gateway");
    mu.memLibreMb = m.memLibreMb;
    mu.carga = m.carga;
    mu.hilosApp = m.hilosApp;
    informe_.muestra(mu);
    maquina_ = QVariantMap{{"memLibreMb", m.memLibreMb}, {"carga", m.carga}, {"hilosApp", m.hilosApp}, {"puede", QString()}};
    emit maquinaChanged();
    vivo_ = aMapa(informe_.vivo(ahora));
    emit vivoChanged();
    // La guardia: si la máquina se aprieta, se corta YA.
    if (const std::string corte = pr::debeCortar(m, umbrales_); !corte.empty()) {
        terminar(ba::i18n::llenar(ba::i18n::traducir("sala", "la cortó la guardia: %1"), {corte}), true);
        return;
    }
    if (ahora >= topeMs_) {
        terminar(ba::i18n::traducir("sala", "terminó el tiempo"), false);
        return;
    }
    if (ahora - renovadaMs_ >= kRenovarS * 1000LL) renovar();
}

void PruebaSalaViewModel::renovar() {
    renovadaMs_ = ahoraMs();
    QPointer<PruebaSalaViewModel> yo(this);
    p_.sala.post("/api/v1/prueba-sala", QJsonObject{{"duracionS", kVidaCredencialS}}, [yo](bool ok, const QJsonObject& r) {
        if (!yo || !yo->corriendo() || !ok) return;
        if (r.value("credencial").toString() != yo->credencial_) {
            // La sala se reinició y la prueba es otra: los de antes ya no están.
            yo->credencial_ = r.value("credencial").toString();
            emit yo->accesoChanged();
        }
    });
}

void PruebaSalaViewModel::alPruebaSala(const QString& nombre, const QJsonObject& ev) {
    if (nombre == QLatin1String("prueba-sala-hd")) {
        informe_.hd(ev.value("id").toString().toStdString(), ev.value("ms").toInt(-1));
        return;
    }
    if (nombre != QLatin1String("prueba-sala")) return;
    const bool d = ev.value("directa").toBool();
    if (d != directa_) {
        directa_ = d;
        emit entradaDirectaChanged();
    }
    // La cerró la sala (venció: nadie la renovó a tiempo).
    if (corriendo() && ev.contains("motivo") && !ev.value("activa").toBool() && ev.value("motivo").toString() != "app")
        terminar(ba::i18n::llenar(ba::i18n::traducir("sala", "la cerró la conferencia (%1)"), {ev.value("motivo").toString().toStdString()}),
                 false);
}

QString PruebaSalaViewModel::setEntradaDirecta(bool on) {
    if (on && !corriendo()) return qs(ba::i18n::traducir("sala", "la entrada directa solo existe durante una prueba de sala"));
    if (!corriendo()) return {};
    QPointer<PruebaSalaViewModel> yo(this);
    p_.sala.post("/api/v1/prueba-sala", QJsonObject{{"directa", on}}, [yo, on](bool ok, const QJsonObject&) {
        if (!yo) return;
        if (!ok) {
            emit yo->notify(tr("La conferencia no cambió la entrada directa."), "error");
            return;
        }
        if (yo->directa_ != on) {
            yo->directa_ = on;
            emit yo->entradaDirectaChanged();
        }
        emit yo->notify(on ? tr("ENTRADA DIRECTA PARA TODOS: cualquiera con el enlace entra sin lobby hasta que termine la prueba.")
                           : tr("Entrada directa apagada: vuelve el lobby."),
                        on ? "warn" : "ok");
    });
    return {};
}

void PruebaSalaViewModel::parar() {
    if (corriendo()) terminar(ba::i18n::traducir("sala", "a mano"), false);
}

// --- Terminar y limpiar -------------------------------------------------------------------

void PruebaSalaViewModel::terminar(const std::string& motivo, bool porGuardia) {
    if (!corriendo()) return;
    setEstado(QStringLiteral("terminando"));
    tic_.stop();
    const int64_t ahora = ahoraMs();
    // Primero el informe (lo que pase con la limpieza no cuenta), después se corta todo.
    informe_.terminar(ahora, motivo, porGuardia);
    if (hinchada_ && hinchada_->state() != QProcess::NotRunning) {
        hinchada_->terminate();
        QPointer<QProcess> h = hinchada_;
        QTimer::singleShot(3000, this, [h]() {
            if (h && h->state() != QProcess::NotRunning) h->kill();
        });
    }
    QPointer<PruebaSalaViewModel> yo(this);
    const int cupoAntes = cupoAntes_;
    p_.sala.post("/api/v1/prueba-sala/cerrar", QJsonObject{{"motivo", QString::fromStdString(motivo)}},
                 [yo, cupoAntes, porGuardia](bool, const QJsonObject&) {
        if (!yo) return;
        // Los de prueba ya no están: el cupo vuelve al de antes.
        if (cupoAntes > 0) yo->p_.cupo.cambiar(cupoAntes);
        // El informe va al evento: el archivo entero y la línea en su lista de chequeo.
        json fin = yo->informe_.final();
        fin["umbrales"] = {{"cortarMemMb", yo->umbrales_.cortarMemMb},
                           {"cortarHilos", yo->umbrales_.cortarHilos},
                           {"cortarCarga", yo->umbrales_.cortarCarga}};
        if (!yo->salidaHinchada_.isEmpty()) fin["hinchada"] = yo->salidaHinchada_.toStdString();
        const std::string nota = yo->informe_.nota();
        fin["nota"] = nota;
        const fs::path dir = yo->p_.evento.eventoAbierto().dir();
        if (!dir.empty()) {
            const std::string nombre =
                "prueba-sala-" + QDateTime::fromMSecsSinceEpoch(yo->inicioMs_).toString("yyyyMMdd-HHmmss").toStdString() + ".json";
            ba::persist::cola().escribir(dir / "Pruebas" / nombre, fin.dump(1, ' ', false, json::error_handler_t::replace));
            fin["archivo"] = (fs::path("Pruebas") / nombre).generic_string();
            yo->p_.evento.guionVm().marcarChequeo(QString::fromLatin1(kItemChequeo), yo->informe_.bien(), QString::fromStdString(nota),
                                                  tr("Prueba de sala (simulacro con participantes de prueba)"));
        }
        ba::persist::cola().borrar(yo->archivoEnCurso());
        yo->ultimoJson_ = fin;
        yo->ultimo_ = aMapa(fin);
        yo->credencial_.clear();
        yo->url_.clear();
        yo->cupoAntes_ = 0;
        if (yo->directa_) {
            yo->directa_ = false;
            emit yo->entradaDirectaChanged();
        }
        yo->vivo_ = aMapa(yo->informe_.vivo(ahoraMs()));
        emit yo->vivoChanged();
        emit yo->accesoChanged();
        emit yo->informeChanged();
        yo->setEstado(QStringLiteral("apagada"));
        emit yo->notify(QString::fromStdString(nota), porGuardia ? "error" : yo->informe_.bien() ? "ok" : "warn");
    }, 15000);
}

void PruebaSalaViewModel::limpiarColgada() {
    colgadaPendiente_ = false;
    const int cupoAntes = colgada_.value("cupoAntes", 0);
    QPointer<PruebaSalaViewModel> yo(this);
    p_.sala.post("/api/v1/prueba-sala/cerrar", QJsonObject{{"motivo", "quedó de antes"}}, [yo, cupoAntes](bool, const QJsonObject&) {
        if (!yo) return;
        if (cupoAntes > 0) yo->p_.cupo.cambiar(cupoAntes);
        ba::persist::cola().borrar(yo->archivoEnCurso());
        emit yo->notify(tr("Había una prueba de sala sin terminar (el programa se cerró en el medio): ya se limpió."), "warn");
    });
}

json PruebaSalaViewModel::estadoJson(bool conCredencial) const {
    json j{{"estado", estado_.toStdString()},
           {"generador", generador_.toStdString()},
           {"entrada_directa", directa_},
           {"hinchada_disponible", hinchadaDisponible()},
           {"url", url_.toStdString()},
           {"vivo", corriendo() ? informe_.vivo(ahoraMs()) : json::object()},
           {"maquina", deQJson(QJsonObject::fromVariantMap(maquina_))}};
    if (conCredencial && !credencial_.isEmpty()) {
        j["credencial"] = credencial_.toStdString();
        j["comando_externo"] = comandoExterno().toStdString();
    }
    if (!ultimoJson_.empty()) {
        json u = ultimoJson_;
        u.erase("serie"); // el resumen; la serie entera, en prueba_sala_informe
        u.erase("personas");
        j["ultimo_informe"] = u;
    }
    return j;
}

} // namespace ba::app
