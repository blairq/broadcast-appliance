// src/app/ui/PruebaSalaViewModel.cpp — la prueba de sala (ver el .hpp)

#include "ui/PruebaSalaViewModel.hpp"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QHostAddress>
#include <QProcessEnvironment>
#include <QRandomGenerator>
#include <QSysInfo>
#include <QTcpServer>
#include <QThreadPool>
#include <QUrl>

#include <system_error>
#include <vector>

#include "diag/Recursos.hpp"
#include "i18n/I18nTexto.hpp"
#include "persist/ColaEscritura.hpp"
#include "platform/Paths.hpp"
#include "sala/ArchivosPrueba.hpp"
#include "sala/CupoSala.hpp"
#include "sala/HistorialPruebas.hpp"
#include "sala/MedidaMaquina.hpp"
#include "ui/ConferenceClient.hpp"
#include "ui/EnlaceViewModel.hpp"
#include "ui/EventoViewModel.hpp"
#include "ui/GuionViewModel.hpp"
#include "ui/RecursosViewModel.hpp"

#ifndef BA_APP_VERSION
#define BA_APP_VERSION ""
#endif

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
/// El puerto de coordinación de hinchada (el suyo de fábrica); si está ocupado, otro libre.
constexpr quint16 kPuertoCoordinador = 7890;
quint16 puertoLibre() {
    QTcpServer s;
    if (s.listen(QHostAddress::Any, kPuertoCoordinador)) return kPuertoCoordinador;
    if (s.listen(QHostAddress::Any, 0)) return s.serverPort();
    return kPuertoCoordinador;
}
/// La clave de coordinación de esta prueba: el coordinador rechaza a quien no la trae.
QString claveAlAzar() {
    static const char* letras = "abcdefghijkmnpqrstuvwxyzABCDEFGHJKLMNPQRSTUVWXYZ23456789";
    QString c;
    for (int i = 0; i < 20; ++i) c += QLatin1Char(letras[QRandomGenerator::system()->bounded(56)]);
    return c;
}
/// hinchada en macOS reparte de a 150 por proceso: el % de la muestra es por proceso.
int porProcesoDe(int n) { return std::min(n, sala::prueba::kPorProcesoMax); }
} // namespace

PruebaSalaViewModel::PruebaSalaViewModel(Partes p, QObject* parent) : QObject(parent), p_(std::move(p)) {
    tic_.setInterval(1000);
    connect(&tic_, &QTimer::timeout, this, &PruebaSalaViewModel::tic);
    // Generando para otro equipo: la CPU de hinchada acá, una vez por segundo.
    genTic_.setInterval(1000);
    connect(&genTic_, &QTimer::timeout, this, [this]() {
        cpuGen_ = cpuProbador();
        publicarGenerando();
    });
    // Quién entró, quién tiene video y quién se cortó: la foto de la sala (sin pedirla).
    // Solo cuando cambió la gente o la entrada directa (no la red de cada uno, en cada foto);
    // la foto en JSON ya la armó el lector de la sala (no se convierte acá).
    connect(&p_.sala, &ConferenceClient::salaCambio, this, [this](unsigned partes) {
        if (!corriendo()) return;
        if ((partes & sala::Gente) != 0)
            if (const std::shared_ptr<const json> snap = p_.sala.fotoJson(); snap && snap->is_object())
                informe_.participantes(ahoraMs(), snap->value("participants", json::array()));
        const bool d = p_.sala.resumen().directa;
        if (d != directa_) {
            directa_ = d;
            emit entradaDirectaChanged();
        }
    });
    connect(&p_.sala, &ConferenceClient::estadoEvento, this, &PruebaSalaViewModel::alPruebaSala);
    // ¿Quedó una prueba a medias (el programa se cerró en el medio)? Se lee en el fondo y se
    // termina de limpiar en cuanto la sala conteste.
    connect(&p_.sala, &ConferenceClient::onlineChanged, this, [this](bool on) {
        if (on && colgadaPendiente_) limpiarColgada();
    });
    QPointer<PruebaSalaViewModel> yo(this);
    const fs::path enCurso = archivoEnCurso();
    QThreadPool::globalInstance()->start([yo, enCurso]() {
        json c = sala::archivos::leerJsonEncolado(enCurso);
        QMetaObject::invokeMethod(
            QCoreApplication::instance(),
            [yo, c = std::move(c)]() {
                // Si ya arrancó otra, el archivo es el de ella: no se toca.
                if (!yo || !c.is_object() || yo->estado_ != QLatin1String("apagada")) return;
                yo->colgada_ = c;
                yo->colgadaPendiente_ = true;
                if (yo->p_.sala.online()) yo->limpiarColgada();
            },
            Qt::QueuedConnection);
    });
    // El enlace: con otra medición (o la grilla cambió), la cuenta para los N de la prueba.
    connect(&p_.enlace, &EnlaceViewModel::necesidadChanged, this, [this]() { cuentaEnlace(nRevisado_); });
    cuentaEnlace(nRevisado_);
}

void PruebaSalaViewModel::cuentaEnlace(int n) {
    nRevisado_ = n;
    json j = p_.enlace.necesidadPara(n);
    j["medido"] = p_.enlace.medido().subidaKbps >= 0;
    const QVariantMap m = aMapa(j);
    if (m == enlace_) return;
    enlace_ = m;
    emit enlaceChanged();
}

PruebaSalaViewModel::~PruebaSalaViewModel() {
    // Al cerrar el programa: el probador se corta YA (es un hijo nuestro), sin esperarlo
    // (sala/ArchivosPrueba: soltarProceso). La sala revoca la credencial sola en
    // kVidaCredencialS (nadie la renueva) y echa a los de prueba; lo que falte (el cupo) se
    // termina al volver a arrancar (archivoEnCurso).
    sala::archivos::soltarProceso(hinchada_, this);
    for (const QPointer<QProcess>& t : trabajadores_) sala::archivos::soltarProceso(t, this);
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
    const int n = pedidos_ > 0 ? pedidos_ : 10;
    return QStringLiteral("HINCHADA_PIN=%1 hinchada --servidor gateway --url %2 --inseguro --participantes %3 "
                          "--modo grabado --hablan %4 --durante %5m --decodificar %6")
        .arg(credencial_, url_)
        .arg(n)
        .arg(hablan_ > 0 ? QString::number(hablan_) : QStringLiteral("ritmo"))
        .arg(minutos_)
        .arg(qs(pr::argDecodificar(decodificar_, porProcesoDe(n))));
}

void PruebaSalaViewModel::revisarMaquina(int n, bool conHinchada, int otrasMaquinas, bool estaGenera) {
    const pr::Medida m = pr::medirMaquina();
    // Lo que genera esta máquina: todos, su parte del reparto o nada (un generador externo).
    const bool local = conHinchada && hinchadaDisponible();
    const int nLocal = !local ? 0 : otrasMaquinas > 0 ? pr::repartir(n, otrasMaquinas, estaGenera).local : n;
    std::string puede = otrasMaquinas > 0 ? pr::puedeRepartir(n, otrasMaquinas, estaGenera) : std::string();
    if (puede.empty()) puede = pr::puedeArrancarRepartido(m, umbrales_, n, nLocal);
    maquina_ = QVariantMap{{"memLibreMb", m.memLibreMb}, {"carga", m.carga}, {"hilosApp", m.hilosApp}, {"puede", qs(puede)}};
    emit maquinaChanged();
    cuentaEnlace(n);
}

// --- Arrancar -------------------------------------------------------------------------------

QString PruebaSalaViewModel::arrancar(int n, int minutos, bool conHinchada, int hablan, const QString& decodificar, int muestraPct,
                                      int otrasMaquinas, bool estaGenera) {
    using ba::i18n::traducir;
    if (estado_ != QLatin1String("apagada")) return qs(traducir("sala", "ya hay una prueba de sala en curso"));
    if (generandoActivo()) return qs(traducir("sala", "esta máquina está generando para otro equipo: pará eso primero"));
    if (otraPrueba_) {
        if (const QString o = otraPrueba_(); !o.isEmpty()) return o;
    }
    if (!p_.sala.online()) return qs(traducir("sala", "la conferencia no está andando"));
    if (!p_.evento.eventoAbierto().hay())
        return qs(traducir("sala", "abrí el evento que querés probar: el informe va a su lista de chequeo"));
    if (minutos < 1 || minutos > 120) return qs(traducir("sala", "la prueba dura de 1 a 120 minutos"));
    if (conHinchada && !hinchadaDisponible())
        return qs(traducir("sala", "el probador (hinchada) no viene en este programa: usá un generador externo"));
    pr::Decodificar dec;
    if (const std::string e = pr::leerDecodificar(decodificar.toStdString(), muestraPct, dec); !e.empty()) return qs(e);
    if (otrasMaquinas > 0) {
        // Repartir lo hace el coordinador de hinchada: tiene que venir en el programa.
        if (!conHinchada) return qs(traducir("sala", "para repartir entre varias máquinas, la prueba tiene que usar hinchada"));
        if (const std::string e = pr::puedeRepartir(n, otrasMaquinas, estaGenera); !e.empty()) return qs(e);
    }
    const pr::Reparto rep = pr::repartir(n, conHinchada ? otrasMaquinas : 0, estaGenera);
    // La guardia, antes que nada (con la parte del probador que corre acá).
    const pr::Medida m = pr::medirMaquina();
    const std::string no = pr::puedeArrancarRepartido(m, umbrales_, n, conHinchada ? rep.local : 0);
    revisarMaquina(n, conHinchada, rep.otras, rep.estaGenera);
    if (!no.empty()) return qs(no);

    // El enlace: los de afuera entran por el túnel. Si lo medido no alcanza, se avisa
    // (no frena: con hinchada la carga es de esta máquina y no pasa por el enlace).
    if (enlace_.value("veredicto").toString() == QLatin1String("no_entra"))
        emit notify(tr("Ojo: según la última medición, el enlace no alcanza para %n invitados por el túnel.", "", n), "warn");
    else if (enlace_.value("veredicto").toString() == QLatin1String("sin_medir"))
        emit notify(tr("El enlace no está medido: medilo antes del evento real (tarjeta «Enlace»)."), "warn");
    generador_ = conHinchada ? QStringLiteral("hinchada") : QStringLiteral("externo");
    pedidos_ = n;
    minutos_ = minutos;
    hablan_ = hablan;
    decodificar_ = dec;
    reparto_ = rep;
    clave_ = rep.varias() ? claveAlAzar() : QString();
    lanUrl_.clear();
    puerto_ = 0;
    escuchando_ = false;
    sumados_ = sumadosOtras_ = 0;
    consolaLeida_ = 0;
    notasCoordinador_.clear();
    pidsProbador_.clear();
    pidsMs_ = 0;
    // Con varias máquinas, el % es el de cada proceso de hinchada (todos reciben el mismo).
    const int porProceso = rep.varias() ? std::max(1, std::max(rep.porProcesoOtra, rep.porProcesoLocal)) : porProcesoDe(n);
    decodificacion_ = aMapa(pr::aJson(dec, porProceso));
    inicioMs_ = ahoraMs();
    // Con hinchada: lo que dura más lo que tarda en entrar y, la primera vez, en grabar
    // el video de prueba (unos minutos). Afuera: lo que se pidió.
    topeMs_ = inicioMs_ + static_cast<int64_t>(minutos) * 60000 + (conHinchada ? 10 * 60000 : 0);
    informe_.empezar(inicioMs_, n, generador_.toStdString(), p_.evento.eventoAbierto().nombre().toStdString());
    serie_.clear();
    emit serieChanged();
    json probador{{"decodificar", pr::aJson(dec, porProceso)}};
    if (rep.varias()) probador["maquinas"] = {{"otras", rep.otras}, {"estaGenera", rep.estaGenera}, {"sumadas", 0}, {"porMaquina", rep.porMaquina}};
    informe_.probador(probador);
    publicarReparto();
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
            yo->lanUrl_ = lan;
            emit yo->accesoChanged();
            if (!conHinchada) {
                emit yo->notify(tr("Prueba de sala abierta: conectá el generador externo con la credencial de prueba."), "ok");
                return;
            }
            if (local.isEmpty()) {
                yo->terminar(ba::i18n::traducir("sala", "la conferencia no tiene HTTPS: el probador no puede entrar"), false);
                return;
            }
            // Las otras máquinas entran por la red: el gateway en la LAN (y el coordinador ahí).
            if (yo->reparto_.varias() && (lan.isEmpty() || !lan.startsWith(QLatin1String("https")))) {
                yo->terminar(ba::i18n::traducir("sala", "la conferencia no tiene una dirección HTTPS en la red: las otras máquinas no pueden "
                                                        "entrar (Configuración → Red de la conferencia → Acceso: «Red local»)"),
                             false);
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
    // El video grabado de hinchada (~100 MB) es uno para todas las configuraciones.
    const QString grabado = QString::fromStdString((ba::platform::dataRoot() / "Runtime" / "hinchada").string());
    // Las carpetas, en el fondo; el probador se lanza cuando estén (si la prueba sigue). Si no
    // se pudieron crear, el probador no arranca (no puede abrir su consola) y eso se avisa.
    QPointer<PruebaSalaViewModel> yo(this);
    const int ronda = ++ronda_;
    const std::vector<fs::path> carpetas{fs::path(salidaHinchada_.toStdString()), fs::path(grabado.toStdString())};
    QThreadPool::globalInstance()->start([yo, ronda, carpetas, n, minutos, hablan, grabado]() {
        sala::archivos::crearCarpetas(carpetas);
        QMetaObject::invokeMethod(
            QCoreApplication::instance(),
            [yo, ronda, n, minutos, hablan, grabado]() {
                if (!yo || !yo->corriendo() || ronda != yo->ronda_) return;
                if (yo->reparto_.varias()) yo->arrancarCoordinador(n, minutos, hablan, grabado);
                else yo->arrancarProbador(n, minutos, hablan, grabado);
            },
            Qt::QueuedConnection);
    });
}

QProcess* PruebaSalaViewModel::nuevoProceso(const QString& consola, const QString& dir, const QProcessEnvironment& env) {
    auto* proc = new QProcess(this);
    proc->setProcessEnvironment(env);
    if (!dir.isEmpty()) proc->setWorkingDirectory(dir);
    // Su consola a un archivo: sin tuberías que se llenen y lo traben.
    proc->setStandardOutputFile(consola);
    proc->setStandardErrorFile(consola, QIODevice::Append);
    return proc;
}

void PruebaSalaViewModel::arrancarProbador(int n, int minutos, int hablan, const QString& grabado) {
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert("HINCHADA_PIN", credencial_);
    QProcess* proc = nuevoProceso(salidaHinchada_ + "/consola.txt", {}, env);
    hinchada_ = proc;
    const QStringList args{"--servidor", "gateway", "--url", url_, "--inseguro",
                           "--participantes", QString::number(n),
                           // Nadie codifica: cada uno manda en loop el patrón con su número.
                           "--modo", "grabado", "--grabado", grabado + "/grabado.hgr",
                           "--hablan", hablan > 0 ? QString::number(hablan) : QStringLiteral("ritmo"),
                           "--durante", QString::number(minutos) + "m",
                           "--entrada-cada", "300ms",
                           // Lo que vuelve: nada, una muestra o todo (sala::prueba::Decodificar).
                           "--decodificar", qs(pr::argDecodificar(decodificar_, porProcesoDe(n))),
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

void PruebaSalaViewModel::arrancarCoordinador(int n, int minutos, int hablan, const QString& grabado) {
    // hinchada como COORDINADOR: espera a los trabajadores (los de acá y los de las otras
    // máquinas), les reparte los N y junta un solo informe. Corre en la carpeta del video
    // grabado y lo nombra sin ruta: cada trabajador lo busca en SU carpeta (en otra máquina,
    // la de su hinchada; si no está, lo graba la primera vez).
    puerto_ = puertoLibre();
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert("HINCHADA_PIN", credencial_);
    env.insert("HINCHADA_CLAVE", clave_);
    QProcess* proc = nuevoProceso(salidaHinchada_ + "/consola.txt", grabado, env);
    hinchada_ = proc;
    const int porProceso = std::max(1, std::max(reparto_.porProcesoOtra, reparto_.porProcesoLocal));
    const QStringList args{"coordinar", "--trabajadores", QString::number(reparto_.trabajadores),
                           "--puerto", QString::number(puerto_),
                           "--servidor", "gateway", "--url", lanUrl_, "--inseguro",
                           "--participantes", QString::number(n),
                           "--modo", "grabado", "--grabado", "grabado.hgr",
                           "--hablan", hablan > 0 ? QString::number(hablan) : QStringLiteral("ritmo"),
                           "--durante", QString::number(minutos) + "m",
                           "--entrada-cada", "300ms",
                           "--decodificar", qs(pr::argDecodificar(decodificar_, porProceso)),
                           "--salida", salidaHinchada_,
                           "--vigilar", "ba-gateway,ba-app"};
    QPointer<PruebaSalaViewModel> yo(this);
    connect(proc, &QProcess::finished, this, [yo, proc](int codigo, QProcess::ExitStatus) {
        proc->deleteLater();
        if (!yo || !yo->corriendo()) return;
        if (codigo == 0) {
            yo->terminar(ba::i18n::traducir("sala", "el probador terminó"), false);
        } else if (!yo->escuchando_) {
            yo->terminar(ba::i18n::llenar(ba::i18n::traducir("sala", "el coordinador de hinchada no pudo escuchar en el puerto %1 "
                                                                     "(código %2): ver consola.txt"),
                                          {std::to_string(yo->puerto_), std::to_string(codigo)}),
                         false);
        } else {
            yo->terminar(ba::i18n::llenar(ba::i18n::traducir("sala", "el coordinador terminó con error (código %1; ¿no llegaron las "
                                                                     "otras máquinas?): ver consola.txt"),
                                          {std::to_string(codigo)}),
                         false);
        }
    });
    connect(proc, &QProcess::errorOccurred, this, [yo](QProcess::ProcessError e) {
        if (!yo || !yo->corriendo() || e != QProcess::FailedToStart) return;
        yo->terminar(ba::i18n::traducir("sala", "no arrancó el probador (hinchada)"), false);
    });
    proc->start(rutaHinchada(), args);
    publicarReparto();
    emit notify(tr("Prueba de sala con varias máquinas: esperando %n generadoras. En cada una, pegá el código en su "
                   "Pruebas → «Generar para otro equipo» (o corré la línea).",
                   "", reparto_.otras),
                "ok");
}

void PruebaSalaViewModel::lanzarTrabajadoresLocales(const QString& grabado) {
    if (!reparto_.estaGenera || !trabajadores_.empty()) return;
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert("HINCHADA_CLAVE", clave_);
    QPointer<PruebaSalaViewModel> yo(this);
    for (int i = 1; i <= reparto_.procesosLocales; ++i) {
        QProcess* t = nuevoProceso(salidaHinchada_ + QStringLiteral("/trabajador-%1.txt").arg(i), grabado, env);
        trabajadores_.emplace_back(t);
        connect(t, &QProcess::finished, this, [yo, t](int codigo, QProcess::ExitStatus) {
            t->deleteLater();
            if (!yo || !yo->corriendo() || codigo == 0) return;
            emit yo->notify(tr("Un generador de esta máquina terminó con error (código %1): ver trabajador-*.txt").arg(codigo), "warn");
        });
        t->start(rutaHinchada(), {"trabajar", "--coordinador", QStringLiteral("127.0.0.1:%1").arg(puerto_), "--nombre",
                                  QString::fromLatin1(pr::kPrefijoLocal) + QString::number(i), "--por-proceso",
                                  QString::number(reparto_.porProcesoLocal)});
    }
}

namespace {
/// Lo que se lee de la carpeta del coordinador en el fondo (ver leerCoordinador).
struct LeidoCoordinador {
    std::string notas;      ///< solo las líneas de los trabajadores y de «escuchando»
    int64_t hasta = 0, hastaMuestras = 0;
    json ultima;            ///< la última línea entera de muestras.jsonl
};
LeidoCoordinador leerCarpetaCoordinador(const fs::path& carpeta, int64_t desde, int64_t desdeMuestras) {
    LeidoCoordinador r;
    const sala::archivos::Tramo t = sala::archivos::leerLineasDesde(carpeta / "consola.txt", desde);
    r.hasta = t.hasta;
    std::size_t i = 0;
    while (i < t.texto.size()) {
        std::size_t f = t.texto.find('\n', i);
        if (f == std::string::npos) f = t.texto.size() - 1;
        const std::string linea = t.texto.substr(i, f - i + 1);
        if (linea.find("trabajador") != std::string::npos || linea.find("escuchando") != std::string::npos) r.notas += linea;
        i = f + 1;
    }
    const sala::archivos::Tramo m = sala::archivos::leerLineasDesde(carpeta / "muestras.jsonl", desdeMuestras, 8 << 20);
    r.hastaMuestras = m.hasta;
    const std::size_t fin = m.texto.find_last_not_of('\n');
    if (fin != std::string::npos) {
        const std::size_t nl = m.texto.rfind('\n', fin);
        const std::size_t ini = nl == std::string::npos ? 0 : nl + 1;
        r.ultima = json::parse(m.texto.substr(ini, fin - ini + 1), nullptr, false);
    }
    return r;
}
} // namespace

void PruebaSalaViewModel::leerCoordinador() {
    if (leyendoConsola_ || salidaHinchada_.isEmpty()) return;
    leyendoConsola_ = true;
    QPointer<PruebaSalaViewModel> yo(this);
    const int ronda = ronda_;
    const fs::path carpeta(salidaHinchada_.toStdString());
    const int64_t desde = consolaLeida_, desdeMuestras = muestrasLeidas_;
    QThreadPool::globalInstance()->start([yo, ronda, carpeta, desde, desdeMuestras]() {
        LeidoCoordinador l = leerCarpetaCoordinador(carpeta, desde, desdeMuestras);
        QMetaObject::invokeMethod(
            QCoreApplication::instance(),
            [yo, ronda, l = std::move(l)]() {
                if (!yo) return;
                yo->leyendoConsola_ = false;
                if (ronda != yo->ronda_ || !yo->corriendo()) return;
                yo->consolaLeida_ = l.hasta;
                yo->muestrasLeidas_ = l.hastaMuestras;
                yo->notasCoordinador_ += l.notas;
                bool cambio = false;
                if (!yo->escuchando_ && yo->notasCoordinador_.find("escuchando") != std::string::npos) {
                    yo->escuchando_ = true;
                    cambio = true;
                    yo->lanzarTrabajadoresLocales(QString::fromStdString((ba::platform::dataRoot() / "Runtime" / "hinchada").string()));
                }
                int otras = 0;
                const int sumados = pr::trabajadoresSumados(yo->notasCoordinador_, &otras);
                if (sumados != yo->sumados_ || otras != yo->sumadosOtras_) {
                    yo->sumados_ = sumados;
                    yo->sumadosOtras_ = otras;
                    cambio = true;
                }
                if (l.ultima.is_object()) {
                    const auto gen = pr::generadorasDeMuestra(l.ultima);
                    yo->informe_.generadoras(gen);
                    QVariantList g;
                    for (const auto& [maquina, cpu] : gen) g.push_back(QVariantMap{{"maquina", qs(maquina)}, {"cpu", cpu}});
                    if (g != yo->generadorasUi_) {
                        yo->generadorasUi_ = g;
                        cambio = true;
                    }
                }
                if (cambio) yo->publicarReparto();
            },
            Qt::QueuedConnection);
    });
}

void PruebaSalaViewModel::publicarReparto() {
    QVariantMap m;
    if (reparto_.varias()) {
        // Esta máquina como la ven las otras: el host del gateway en la red.
        const QString host = QUrl(lanUrl_).host();
        const QString dir = host.isEmpty() || puerto_ <= 0 ? QString() : host + ":" + QString::number(puerto_);
        pr::CodigoGenerador c;
        c.coordinador = dir.toStdString();
        c.clave = clave_.toStdString();
        c.procesos = reparto_.procesosPorOtra;
        c.porProceso = reparto_.porProcesoOtra;
        c.maquina = QSysInfo::machineHostName().toStdString();
        const int sumadas = reparto_.procesosPorOtra > 0 ? sumadosOtras_ / reparto_.procesosPorOtra : 0;
        const bool hay = !dir.isEmpty() && corriendo();
        m = QVariantMap{{"activo", corriendo()},
                        {"otras", reparto_.otras},
                        {"estaGenera", reparto_.estaGenera},
                        {"porMaquina", reparto_.porMaquina},
                        {"local", reparto_.local},
                        {"procesosPorOtra", reparto_.procesosPorOtra},
                        {"porProcesoOtra", reparto_.porProcesoOtra},
                        {"esperados", reparto_.trabajadores},
                        {"sumados", sumados_},
                        {"sumadasOtras", sumadas},
                        {"escuchando", escuchando_},
                        {"direccion", dir},
                        {"generadoras", generadorasUi_},
                        {"codigo", hay ? qs(pr::armarCodigoGenerador(c)) : QString()},
                        {"comando", hay ? qs(pr::comandoTrabajador(reparto_, host.toStdString(), puerto_, clave_.toStdString(), false)) : QString()},
                        {"comandoWindows",
                         hay ? qs(pr::comandoTrabajador(reparto_, host.toStdString(), puerto_, clave_.toStdString(), true)) : QString()}};
        informe_.probador({{"maquinas",
                            {{"otras", reparto_.otras}, {"estaGenera", reparto_.estaGenera}, {"sumadas", sumadas}, {"porMaquina", reparto_.porMaquina}}}});
    }
    if (m == repartoUi_) return;
    repartoUi_ = m;
    emit probadorChanged();
}

double PruebaSalaViewModel::cpuProbador() {
    std::vector<std::uint32_t> pids;
    const auto sumar = [&pids](const QPointer<QProcess>& p) {
        if (p && p->state() == QProcess::Running && p->processId() > 0) pids.push_back(static_cast<std::uint32_t>(p->processId()));
    };
    sumar(hinchada_);
    for (const QPointer<QProcess>& t : trabajadores_) sumar(t);
    for (const QPointer<QProcess>& t : generando_) sumar(t);
    if (pids.empty()) {
        if (!pidsProbador_.empty()) {
            pidsProbador_.clear();
            p_.recursos.medirTambien("hinchada", {});
        }
        return -1;
    }
    // Con sus hijos (hinchada reparte en procesos de a 150): cada 5 s, no en cada segundo.
    const int64_t ahora = ahoraMs();
    if (ahora - pidsMs_ >= 5000 || pidsProbador_.size() < pids.size()) {
        pidsProbador_ = pr::conHijos(pids);
        pidsMs_ = ahora;
        p_.recursos.medirTambien("hinchada", pidsProbador_);
    }
    double cpu = 0;
    bool hay = false;
    for (const auto& proc : p_.recursos.muestreador().ultima().procesos)
        if (proc.nombre == "hinchada" && proc.cpu >= 0) {
            cpu += proc.cpu;
            hay = true;
        }
    return hay ? cpu : -1;
}

void PruebaSalaViewModel::cortarProbador() {
    const auto cortar = [this](const QPointer<QProcess>& p) {
        if (!p || p->state() == QProcess::NotRunning) return;
        p->terminate();
        QPointer<QProcess> h = p;
        QTimer::singleShot(3000, this, [h]() {
            if (h && h->state() != QProcess::NotRunning) h->kill();
        });
    };
    cortar(hinchada_);
    for (const QPointer<QProcess>& t : trabajadores_) cortar(t);
    trabajadores_.clear();
}

// --- Generar para otro equipo (esta máquina como generadora de la prueba de OTRA) ---------

QString PruebaSalaViewModel::generarPara(const QString& codigo) {
    using ba::i18n::traducir;
    if (generandoActivo()) return qs(traducir("sala", "esta máquina ya está generando para otro equipo"));
    if (estado_ != QLatin1String("apagada")) return qs(traducir("sala", "hay una prueba de sala corriendo en esta máquina"));
    if (otraPrueba_) {
        if (const QString o = otraPrueba_(); !o.isEmpty()) return o;
    }
    if (!hinchadaDisponible()) return qs(traducir("sala", "el probador (hinchada) no viene en este programa"));
    pr::CodigoGenerador c;
    if (!pr::leerCodigoGenerador(codigo.toStdString(), c))
        return qs(traducir("sala", "ese no es un código de generador: copialo entero de la Prueba de sala del otro equipo "
                                   "(empieza con BAGEN1.)"));
    // La guardia: esta máquina solo genera (la sala es la del otro).
    const int n = std::min(c.procesos * c.porProceso, umbrales_.maxParticipantes);
    if (const std::string no = pr::puedeArrancar(pr::medirMaquina(), umbrales_, n, true); !no.empty()) return qs(no);
    codigoGen_ = c;
    const QString fecha = QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss");
    const QString grabado = QString::fromStdString((ba::platform::dataRoot() / "Runtime" / "hinchada").string());
    const QString carpeta = grabado + "/generador-" + fecha;
    generandoDesde_ = ahoraMs();
    generandoCodigo_ = 0;
    generandoError_.clear();
    generandoEstado_ = QStringLiteral("arrancando");
    publicarGenerando();
    QPointer<PruebaSalaViewModel> yo(this);
    const int ronda = ++rondaGen_;
    QThreadPool::globalInstance()->start([yo, ronda, carpeta, grabado]() {
        sala::archivos::crearCarpetas({fs::path(carpeta.toStdString()), fs::path(grabado.toStdString())});
        QMetaObject::invokeMethod(
            QCoreApplication::instance(),
            [yo, ronda, carpeta, grabado]() {
                if (!yo || ronda != yo->rondaGen_ || yo->generandoEstado_ != QLatin1String("arrancando")) return;
                yo->lanzarGeneradores(carpeta, grabado);
            },
            Qt::QueuedConnection);
    });
    return {};
}

void PruebaSalaViewModel::lanzarGeneradores(const QString& carpeta, const QString& grabado) {
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert("HINCHADA_CLAVE", qs(codigoGen_.clave)); // nunca en la línea de comandos
    const QString host = QSysInfo::machineHostName();
    QPointer<PruebaSalaViewModel> yo(this);
    for (int i = 1; i <= codigoGen_.procesos; ++i) {
        QProcess* t = nuevoProceso(carpeta + QStringLiteral("/trabajador-%1.txt").arg(i), grabado, env);
        generando_.emplace_back(t);
        connect(t, &QProcess::finished, this, [yo, t](int codigo, QProcess::ExitStatus) {
            t->deleteLater();
            if (!yo) return;
            if (codigo != 0) yo->generandoCodigo_ = codigo;
            yo->alTerminarGenerador();
        });
        connect(t, &QProcess::errorOccurred, this, [yo](QProcess::ProcessError e) {
            if (!yo || e != QProcess::FailedToStart) return;
            yo->generandoCodigo_ = -1;
            yo->alTerminarGenerador();
        });
        t->start(rutaHinchada(), {"trabajar", "--coordinador", qs(codigoGen_.coordinador), "--nombre",
                                  host + QStringLiteral("-%1").arg(i), "--por-proceso", QString::number(codigoGen_.porProceso)});
    }
    generandoEstado_ = QStringLiteral("generando");
    genTic_.start();
    publicarGenerando();
    emit notify(tr("Generando para %1: los invitados de prueba salen de esta máquina hasta que termine la prueba del otro.")
                    .arg(qs(codigoGen_.maquina)),
                "ok");
}

void PruebaSalaViewModel::alTerminarGenerador() {
    for (const QPointer<QProcess>& t : generando_)
        if (t && t->state() != QProcess::NotRunning) return; // quedan otros andando
    generando_.clear();
    genTic_.stop();
    if (generandoEstado_ != QLatin1String("generando") && generandoEstado_ != QLatin1String("parando")) return;
    const bool aMano = generandoEstado_ == QLatin1String("parando");
    generandoEstado_ = QStringLiteral("terminado");
    if (!aMano && generandoCodigo_ != 0)
        generandoError_ = generandoCodigo_ == 2 ? tr("no se pudo conectar con el otro equipo (¿la dirección, el firewall o la prueba ya terminó?)")
                                                : tr("hinchada terminó con error (código %1): ver trabajador-*.txt").arg(generandoCodigo_);
    cpuProbador(); // deja de medir los procesos que se fueron
    publicarGenerando();
    emit notify(generandoError_.isEmpty() ? tr("Terminó la generación para %1.").arg(qs(codigoGen_.maquina))
                                          : tr("Generación para %1: %2").arg(qs(codigoGen_.maquina), generandoError_),
                generandoError_.isEmpty() ? "ok" : "warn");
}

void PruebaSalaViewModel::dejarDeGenerar() {
    if (!generandoActivo()) return;
    if (generandoEstado_ == QLatin1String("arrancando")) {
        generandoEstado_ = QStringLiteral("terminado");
        publicarGenerando();
        return;
    }
    generandoEstado_ = QStringLiteral("parando");
    for (const QPointer<QProcess>& t : generando_) {
        if (!t || t->state() == QProcess::NotRunning) continue;
        t->terminate();
        QPointer<QProcess> h = t;
        QTimer::singleShot(3000, this, [h]() {
            if (h && h->state() != QProcess::NotRunning) h->kill();
        });
    }
    publicarGenerando();
}

void PruebaSalaViewModel::publicarGenerando() {
    int andando = 0;
    for (const QPointer<QProcess>& t : generando_)
        if (t && t->state() == QProcess::Running) ++andando;
    const QVariantMap m{{"estado", generandoEstado_},
                        {"activo", generandoActivo()},
                        {"maquina", qs(codigoGen_.maquina)},
                        {"coordinador", qs(codigoGen_.coordinador)},
                        {"procesos", codigoGen_.procesos},
                        {"porProceso", codigoGen_.porProceso},
                        {"andando", andando},
                        {"cpu", cpuGen_},
                        {"segundos", generandoDesde_ > 0 ? static_cast<int>((ahoraMs() - generandoDesde_) / 1000) : 0},
                        {"error", generandoError_}};
    if (m == generandoUi_) return;
    generandoUi_ = m;
    emit generandoChanged();
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
    mu.cpuGenerador = cpuProbador();
    mu.cpuMaquina = r.cpu;
    mu.redEntraMbps = r.redEntradaMbps;
    mu.redSaleMbps = r.redSalidaMbps;
    if (reparto_.varias()) leerCoordinador();
    mu.memLibreMb = m.memLibreMb;
    mu.carga = m.carga;
    mu.hilosApp = m.hilosApp;
    informe_.muestra(mu);
    {
        double gen = -1;
        for (const QVariant& g : generadorasUi_) gen = std::max(gen, g.toMap().value("cpu").toDouble());
        serie_.push_back(QVariantMap{{"t", static_cast<double>(ahora)},
                                     {"cpuMaquina", mu.cpuMaquina},
                                     {"cpuApp", mu.cpuApp},
                                     {"cpuGateway", mu.cpuGateway},
                                     {"cpuGenerador", mu.cpuGenerador},
                                     {"redEntraMbps", mu.redEntraMbps},
                                     {"redSaleMbps", mu.redSaleMbps},
                                     {"cpuGeneradoras", gen}});
        while (serie_.size() > 600) serie_.removeFirst();
        if (serie_.size() % 2 == 0) emit serieChanged();
    }
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
    cortarProbador();
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
        // El enlace (por el túnel) para esta cantidad: lo medido y la cuenta.
        fin["enlace"] = yo->p_.enlace.necesidadPara(yo->pedidos_);
        if (const json u = yo->p_.enlace.ultimaBuena(); u.is_object())
            fin["enlace"]["medicion"] = {{"fecha", u.value("fecha", 0.0)},
                                         {"reposo", u.value("reposo", json::object())},
                                         {"subida", u.value("subida", json::object())},
                                         {"bajada", u.value("bajada", json::object())}};
        const std::string nota = yo->informe_.nota();
        fin["nota"] = nota;
        fin["parametros"] = {{"participantes", yo->pedidos_},
                             {"minutos", yo->minutos_},
                             {"hablan", yo->hablan_},
                             {"generador", yo->generador_.toStdString()},
                             {"decodificar", pr::aJson(yo->decodificar_, porProcesoDe(std::max(1, yo->pedidos_)))},
                             {"otrasMaquinas", yo->reparto_.otras},
                             {"estaGenera", yo->reparto_.estaGenera}};
        fin["maquina"] = QSysInfo::machineHostName().toStdString();
        fin["fichas"] = {{"local", pr::fichaMaquina(yo->p_.recursos.muestreador().ultima())}};
        // También al historial de pruebas de la máquina (sin lo privado).
        {
            const json sobre = sala::historial::sobre(fin, "sala", BA_APP_VERSION, ahoraMs());
            ba::persist::cola().escribir(yo->p_.dataRoot / sala::historial::kCarpeta /
                                             sala::historial::nombreArchivo(yo->inicioMs_, fin.value("maquina", std::string()), "sala"),
                                         sobre.dump(1, ' ', false, json::error_handler_t::replace));
        }
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
        yo->clave_.clear();
        yo->publicarReparto(); // sin la prueba, sin código ni línea para las otras
        yo->cpuProbador();     // deja de medir los procesos del probador
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

json PruebaSalaViewModel::historialPruebas(int limite, bool csv) const {
    const json lista = sala::historial::listar(p_.dataRoot, std::clamp(limite, 1, 500));
    return csv ? json{{"csv", sala::historial::csv(lista)}} : json{{"pruebas", lista}};
}

json PruebaSalaViewModel::verPrueba(const std::string& archivo) const { return sala::historial::leer(p_.dataRoot, archivo); }

json PruebaSalaViewModel::compararPruebas(const std::string& a, const std::string& b) const {
    const json ja = sala::historial::leer(p_.dataRoot, a), jb = sala::historial::leer(p_.dataRoot, b);
    if (!ja.is_object() || !jb.is_object()) return nullptr;
    return sala::historial::comparar(ja, jb);
}

json PruebaSalaViewModel::estadoJson(bool conCredencial) const {
    json j{{"estado", estado_.toStdString()},
           {"generador", generador_.toStdString()},
           {"entrada_directa", directa_},
           {"hinchada_disponible", hinchadaDisponible()},
           {"url", url_.toStdString()},
           {"vivo", corriendo() ? informe_.vivo(ahoraMs()) : json::object()},
           {"maquina", deQJson(QJsonObject::fromVariantMap(maquina_))},
           {"enlace", deQJson(QJsonObject::fromVariantMap(enlace_))},
           {"decodificar", deQJson(QJsonObject::fromVariantMap(decodificacion_))},
           {"generando", deQJson(QJsonObject::fromVariantMap(generandoUi_))}};
    if (!repartoUi_.isEmpty()) {
        QVariantMap r = repartoUi_;
        if (!conCredencial) {
            r.remove("codigo");
            r.remove("comando");
            r.remove("comandoWindows");
        }
        j["reparto"] = deQJson(QJsonObject::fromVariantMap(r));
    }
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

#include "moc_PruebaSalaViewModel.cpp"
