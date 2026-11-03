// src/app/ui/PruebaAsambleaViewModel.cpp — la prueba de asamblea (ver el .hpp)

#include "ui/PruebaAsambleaViewModel.hpp"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcessEnvironment>
#include <QUrl>

#include <algorithm>
#include <memory>
#include <system_error>

#include "asamblea/Cuerpo.hpp"
#include "asamblea/CuerpoViewModel.hpp"
#include "asamblea/SesionRemota.hpp"
#include "i18n/I18n.hpp"
#include "persist/ColaEscritura.hpp"
#include "persist/Escritor.hpp"
#include "platform/Paths.hpp"
#include "ui/ConferenceClient.hpp"
#include "ui/EventoAbierto.hpp"
#include "ui/EventoViewModel.hpp"
#include "ui/GuionViewModel.hpp"

namespace ba::app {

namespace pa = asamblea::prueba;
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
json deMapa(const QVariantMap& m) {
    return json::parse(QJsonDocument(QJsonObject::fromVariantMap(m)).toJson(QJsonDocument::Compact).toStdString(), nullptr,
                       false);
}
QString qs(const std::string& s) { return QString::fromStdString(s); }
/// El ítem de la lista de chequeo del evento donde queda el resultado.
constexpr const char* kItemChequeo = "prueba_asamblea";
/// Lo que espera el probador para irse solo antes de cortarlo de prepo.
constexpr int kEsperaProbadorMs = 15000;
} // namespace

PruebaAsambleaViewModel::PruebaAsambleaViewModel(Partes p, QObject* parent) : QObject(parent), p_(std::move(p)) {
    tic_.setInterval(1000);
    connect(&tic_, &QTimer::timeout, this, &PruebaAsambleaViewModel::tic);
    // El padrón cambió (la prueba, la planilla, el MCP): la tarjeta lo vuelve a contar.
    connect(&p_.cuerpo, &asamblea::CuerpoViewModel::cambio, this, &PruebaAsambleaViewModel::padronChanged);
    // ¿Quedó una prueba a medias (el programa se cerró en el medio)? Se limpia al arrancar.
    QTimer::singleShot(0, this, [this]() { limpiarColgada(); });
}

PruebaAsambleaViewModel::~PruebaAsambleaViewModel() {
    // Al cerrar el programa: el probador se corta ya (es un hijo nuestro). Los integrantes de
    // prueba quedan en el Cuerpo y se limpian al volver a arrancar (archivoEnCurso).
    if (hinchada_ && hinchada_->state() != QProcess::NotRunning) {
        hinchada_->kill();
        hinchada_->waitForFinished(2000);
    }
}

std::int64_t PruebaAsambleaViewModel::ahoraMs() { return QDateTime::currentMSecsSinceEpoch(); }

asamblea::Cuerpo& PruebaAsambleaViewModel::cuerpo() const { return p_.cuerpo.cuerpo(); }

fs::path PruebaAsambleaViewModel::archivoEnCurso() const { return p_.dataRoot / "Runtime" / "prueba-asamblea-en-curso.json"; }

fs::path PruebaAsambleaViewModel::carpetaPruebas() const {
    const fs::path dir = p_.evento.eventoAbierto().dir();
    return (dir.empty() ? p_.dataRoot : dir) / "Pruebas";
}

QString PruebaAsambleaViewModel::rutaHinchada() const {
#ifdef _WIN32
    return QCoreApplication::applicationDirPath() + "/hinchada/hinchada.exe";
#else
    return QCoreApplication::applicationDirPath() + "/hinchada/hinchada";
#endif
}

bool PruebaAsambleaViewModel::hinchadaDisponible() const { return QFileInfo(rutaHinchada()).isExecutable(); }

int PruebaAsambleaViewModel::integrantesReales() const { return pa::integrantesReales(cuerpo().integrantes()); }

int PruebaAsambleaViewModel::integrantesPrueba() const {
    return static_cast<int>(pa::aBorrar(cuerpo().integrantes()).size());
}

QString PruebaAsambleaViewModel::impedimento() const {
    if (!p_.sesion) return qs(traducir("asamblea", "la sesión remota no está disponible"));
    if (activa()) return {};
    return qs(pa::puedeGenerar(cuerpo().integrantes(), 1));
}

QString PruebaAsambleaViewModel::comandoExterno() const {
    if (!corriendo() || url_.isEmpty()) return {};
    return qs(pa::comandoExterno(comp_, url_.toStdString(), minutos_));
}

QVariantMap PruebaAsambleaViewModel::comportamientoPorOmision() const { return aMapa(pa::aJson(pa::Comportamiento{})); }

void PruebaAsambleaViewModel::setEstado(const QString& e) {
    if (estado_ == e) return;
    estado_ = e;
    emit estadoChanged();
    emit padronChanged(); // el impedimento depende de si hay una en curso
}

// --- Preparar ----------------------------------------------------------------------------

QString PruebaAsambleaViewModel::preparar(int n) {
    if (estado_ != QLatin1String("apagada")) return qs(traducir("asamblea", "ya hay una prueba de asamblea en curso"));
    if (!p_.sesion) return qs(traducir("asamblea", "la sesión remota no está disponible"));
    const json antes = cuerpo().integrantes();
    // La regla que protege el padrón: solo en un evento sin integrantes reales.
    if (const std::string no = pa::puedeGenerar(antes, n); !no.empty()) return qs(no);
    recintoAntes_ = cuerpo().recintoGuardado();
    hayRecintoAntes_ = true;
    const json nuevos = pa::integrantesNuevos(antes, n);
    ids_.clear();
    std::string error;
    cuerpo().enLote([&]() {
        for (const json& c : nuevos) {
            std::string e;
            const std::string id = cuerpo().guardar(c, &e);
            if (!id.empty()) ids_.push_back(id);
            else if (error.empty()) error = e;
        }
        // Las bancas de prueba en el recinto, para habilitarlos viéndoles la cara.
        cuerpo().setRecinto(pa::recintoPara(static_cast<int>(ids_.size())));
    });
    if (ids_.empty()) {
        cuerpo().restaurarRecinto(recintoAntes_);
        return qs(error.empty() ? traducir("asamblea", "no se pudieron crear los integrantes de prueba") : error);
    }
    // Por si el programa se cierra en el medio: lo que hay que deshacer al volver.
    ba::persist::cola().escribir(archivoEnCurso(),
                                 json{{"inicioMs", ahoraMs()}, {"recinto", recintoAntes_}, {"ids", ids_}}.dump());
    // El acceso de cada uno (link + PIN, y usuario + contraseña), todo junto: el gateway se
    // entera una vez.
    json accesos = json::array();
    p_.sesion->enLote([&]() {
        for (const std::string& id : ids_) {
            json a = p_.sesion->generar(id, true);
            if (a.contains("error")) continue;
            const json k = p_.sesion->generarClave(id, pa::usuarioDe(a.value("banca", std::string())));
            if (!k.contains("error")) {
                a["usuario"] = k.value("usuario", std::string());
                a["clave"] = k.value("clave", std::string());
            }
            accesos.push_back(a);
        }
    });
    archivo_.clear();
    // Lo que hinchada va a leer (se escribe al arrancar, en la carpeta de esta prueba).
    accesosJson_ = pa::archivoHinchada(accesos);
    pedidos_ = static_cast<int>(ids_.size());
    maxConectados_ = 0;
    esperaDesde_.clear();
    inicioMs_ = ahoraMs();
    vivoJson_ = calcularVivo();
    vivo_ = aMapa(vivoJson_);
    emit vivoChanged();
    setEstado(QStringLiteral("preparada"));
    emit notify(tr("Prueba de asamblea: %n integrantes de prueba con su acceso, en el Recinto.", "", pedidos_), "ok");
    return {};
}

// --- Arrancar ----------------------------------------------------------------------------

QString PruebaAsambleaViewModel::arrancar(int n, int minutos, const QVariantMap& comportamiento, bool conHinchada) {
    return qs(arrancarJson(n, minutos, deMapa(comportamiento), conHinchada));
}

std::string PruebaAsambleaViewModel::arrancarJson(int n, int minutos, const json& comportamiento, bool conHinchada) {
    if (estado_ == QLatin1String("corriendo") || estado_ == QLatin1String("terminando"))
        return traducir("asamblea", "ya hay una prueba de asamblea en curso");
    pa::Comportamiento c;
    if (const std::string e = pa::comportamientoDeJson(comportamiento, c); !e.empty()) return e;
    if (minutos < 1 || minutos > 120) return traducir("asamblea", "la prueba dura de 1 a 120 minutos");
    if (conHinchada && !hinchadaDisponible())
        return traducir("asamblea", "el probador (hinchada) no viene en este programa: usá un generador externo");
    if (!p_.sala.online()) return traducir("asamblea", "la conferencia no está andando (el gateway atiende la sesión remota)");
    if (estado_ == QLatin1String("apagada"))
        if (const QString e = preparar(n); !e.isEmpty()) return e.toStdString();
    comp_ = c;
    arranco_ = true;
    minutos_ = minutos;
    generador_ = conHinchada ? QStringLiteral("hinchada") : QStringLiteral("externo");
    inicioMs_ = ahoraMs();
    // La entrada escalonada más lo que dura; con hinchada, lo que tarda en grabar su video la
    // primera vez (unos minutos). El probador termina solo antes; esto es por si no.
    topeMs_ = inicioMs_ + static_cast<std::int64_t>(minutos) * 60000 + static_cast<std::int64_t>(pedidos_) * c.entradaCadaMs +
              (conHinchada ? 10 * 60000 : 60000);
    // Las credenciales para el probador: en la carpeta de esta prueba, solo para el dueño.
    const QString fecha = QDateTime::fromMSecsSinceEpoch(inicioMs_).toString("yyyyMMdd-HHmmss");
    const fs::path carpeta = carpetaPruebas() / ("prueba-asamblea-" + fecha.toStdString());
    std::error_code ec;
    fs::create_directories(carpeta, ec);
    salida_ = QString::fromStdString(carpeta.string());
    std::string err;
    const fs::path archivo = carpeta / "integrantes.json";
    if (!ba::persist::escribir(archivo, accesosJson_.dump(1), ba::persist::Durabilidad::Normal, ba::persist::Permisos::Privados,
                               &err)) {
        terminar(llenar(traducir("asamblea", "no se pudo escribir el archivo de credenciales: %1"), {err}));
        return traducir("asamblea", "no se pudo escribir el archivo de credenciales");
    }
    archivo_ = QString::fromStdString(archivo.string());
    ba::persist::cola().escribir(
        archivoEnCurso(),
        json{{"inicioMs", inicioMs_}, {"recinto", recintoAntes_}, {"ids", ids_}, {"archivo", archivo.string()}}.dump());
    setEstado(QStringLiteral("corriendo"));
    tic_.start();
    // Por dónde entran: el probador local, por la máquina misma (el HTTPS del gateway); uno
    // externo, por la red (la mejor dirección que da el gateway).
    QPointer<PruebaAsambleaViewModel> yo(this);
    p_.sala.get("/api/v1/network", [yo, conHinchada](bool ok, const QJsonObject& red) {
        if (!yo || !yo->corriendo()) return;
        QString local, lan;
        if (ok) {
            const QJsonObject https = red.value("https").toObject();
            if (https.value("enabled").toBool()) local = QStringLiteral("https://127.0.0.1:%1").arg(https.value("port").toInt());
            const QUrl mejor(red.value("bestUrl").toString());
            if (mejor.isValid() && !mejor.host().isEmpty())
                lan = mejor.scheme() + "://" + mejor.host() + (mejor.port() > 0 ? ":" + QString::number(mejor.port()) : QString());
        }
        yo->url_ = conHinchada ? local : (lan.isEmpty() ? local : lan);
        emit yo->accesoChanged();
        if (!conHinchada) {
            emit yo->notify(tr("Prueba de asamblea abierta: corré el generador externo con el archivo de credenciales."), "ok");
            return;
        }
        if (local.isEmpty()) {
            yo->terminar(traducir("asamblea", "la conferencia no tiene HTTPS: el probador no puede entrar"));
            return;
        }
        yo->lanzarHinchada();
    });
    emit accesoChanged();
    return {};
}

void PruebaAsambleaViewModel::lanzarHinchada() {
    // El video grabado de hinchada (~200 MB) es uno para todas las pruebas.
    const QString grabado = QString::fromStdString((ba::platform::dataRoot() / "Runtime" / "hinchada").string());
    QDir().mkpath(grabado);
    auto* proc = new QProcess(this);
    hinchada_ = proc;
    // Su consola a un archivo: sin tuberías que se llenen y lo traben.
    proc->setStandardOutputFile(salida_ + "/consola.txt");
    proc->setStandardErrorFile(salida_ + "/consola.txt", QIODevice::Append);
    QStringList args;
    for (const std::string& a : pa::argumentosHinchada(comp_, url_.toStdString(), archivo_.toStdString(), minutos_,
                                                       (salida_ + "/hinchada").toStdString(),
                                                       (grabado + "/grabado.hgr").toStdString()))
        args << QString::fromStdString(a);
    QPointer<PruebaAsambleaViewModel> yo(this);
    connect(proc, &QProcess::finished, this, [yo, proc](int codigo, QProcess::ExitStatus) {
        proc->deleteLater();
        if (!yo || !yo->corriendo()) return;
        yo->terminar(codigo == 0 ? traducir("asamblea", "el probador terminó")
                                 : llenar(traducir("asamblea", "el probador terminó con error (código %1): ver consola.txt"),
                                          {std::to_string(codigo)}));
    });
    connect(proc, &QProcess::errorOccurred, this, [yo](QProcess::ProcessError e) {
        if (!yo || !yo->corriendo() || e != QProcess::FailedToStart) return;
        yo->terminar(traducir("asamblea", "no arrancó el probador (hinchada)"));
    });
    proc->start(rutaHinchada(), args);
    emit notify(tr("Prueba de asamblea: entran %n integrantes de prueba (la primera vez el probador graba su video unos minutos).",
                   "", pedidos_),
                "ok");
}

// --- Mientras dura ------------------------------------------------------------------------

json PruebaAsambleaViewModel::calcularVivo() const {
    if (!p_.sesion) return json::object();
    json v = pa::vivo(p_.sesion->participantes(), p_.sesion->votacion(), ids_, pedidos_);
    v["segundos"] = inicioMs_ > 0 ? (ahoraMs() - inicioMs_) / 1000 : 0;
    v["maxConectados"] = std::max(maxConectados_, v.value("conectados", 0));
    return v;
}

void PruebaAsambleaViewModel::tic() {
    if (!corriendo()) {
        tic_.stop();
        return;
    }
    const std::int64_t ahora = ahoraMs();
    // «Habilitar solos»: los que esperan hace N s (como haría el operador viéndoles la cara).
    if (comp_.habilitarSolosS > 0 && p_.sesion) {
        for (const json& pt : p_.sesion->participantes()) {
            const std::string id = pt.value("id", std::string());
            if (std::find(ids_.begin(), ids_.end(), id) == ids_.end()) continue;
            if (pt.value("estado", std::string()) != "esperando") {
                esperaDesde_.erase(id);
                continue;
            }
            auto [it, nuevo] = esperaDesde_.emplace(id, ahora);
            if (!nuevo && ahora - it->second >= comp_.habilitarSolosS * 1000LL) {
                p_.sesion->habilitar(id, true, "prueba");
                esperaDesde_.erase(it);
            }
        }
    }
    const json v = calcularVivo();
    maxConectados_ = v.value("maxConectados", maxConectados_);
    if (v != vivoJson_) {
        vivoJson_ = v;
        vivo_ = aMapa(v);
        emit vivoChanged();
    }
    if (ahora >= topeMs_) terminar(traducir("asamblea", "terminó el tiempo"));
}

// --- Terminar y limpiar -------------------------------------------------------------------

void PruebaAsambleaViewModel::parar() {
    if (corriendo() || estado_ == QLatin1String("preparada")) terminar(traducir("asamblea", "a mano"));
}

void PruebaAsambleaViewModel::terminar(const std::string& motivo) {
    if (!corriendo() && estado_ != QLatin1String("preparada")) return;
    setEstado(QStringLiteral("terminando"));
    tic_.stop();
    vivoJson_ = calcularVivo();
    // El probador primero: que se vaya solo (cierra sus conexiones y escribe su informe) y,
    // si no, de prepo. Recién después se limpia.
    if (hinchada_ && hinchada_->state() != QProcess::NotRunning) {
        QPointer<QProcess> h = hinchada_;
        QPointer<PruebaAsambleaViewModel> yo(this);
        auto listo = std::make_shared<bool>(false);
        const auto seguir = [yo, motivo, listo]() {
            if (*listo || !yo) return;
            *listo = true;
            yo->cerrar(motivo);
        };
        connect(h, &QProcess::finished, this, seguir);
        h->terminate();
        QTimer::singleShot(kEsperaProbadorMs, this, [h, seguir]() {
            if (h && h->state() != QProcess::NotRunning) h->kill();
            seguir();
        });
        return;
    }
    cerrar(motivo);
}

int PruebaAsambleaViewModel::limpiarIntegrantes(const json& recinto, bool restaurar) {
    const std::vector<std::string> ids = pa::aBorrar(cuerpo().integrantes());
    if (p_.sesion) {
        // Fuera de la sesión (si siguen conectados) y sin acceso: el gateway corta al que quede.
        p_.sesion->enLote([&]() {
            for (const std::string& id : ids) {
                p_.sesion->expulsar(id, "prueba", ba::i18n::traducirEvento("asamblea", "Terminó la prueba de asamblea."));
                p_.sesion->revocar(id);
            }
        });
    }
    cuerpo().enLote([&]() {
        for (const std::string& id : ids) cuerpo().borrar(id);
    });
    if (restaurar) cuerpo().restaurarRecinto(recinto);
    return static_cast<int>(ids.size());
}

void PruebaAsambleaViewModel::cerrar(const std::string& motivo) {
    const std::int64_t fin = ahoraMs();
    // Lo que dejó el probador (si hubo): lo que le pasó a cada uno.
    json h = json::object();
    if (!salida_.isEmpty()) {
        std::string s;
        if (ba::persist::leerArchivo(fs::path(salida_.toStdString()) / "hinchada" / "informe.json", s)) {
            const json todo = json::parse(s, nullptr, false);
            if (todo.is_object())
                h = {{"carpeta", (fs::path(salida_.toStdString()) / "hinchada").string()},
                     {"asamblea", todo.value("asamblea", json::object())},
                     {"integrantes", todo.value("integrantes", json::array())},
                     {"resumen", todo.value("resumen", json::object())},
                     {"notas", todo.value("notas", json::array())}};
        }
    }
    const bool habiaPrueba = arranco_;
    const int borrados = limpiarIntegrantes(recintoAntes_, hayRecintoAntes_);
    // Las credenciales no quedan en ningún lado.
    if (!archivo_.isEmpty()) ba::persist::borrar(fs::path(archivo_.toStdString()));
    ba::persist::cola().borrar(archivoEnCurso());

    json fin_{{"tipo", "prueba_asamblea"},
              {"inicioMs", inicioMs_},
              {"finMs", fin},
              {"motivo", motivo},
              {"generador", generador_.toStdString()},
              {"pedidos", pedidos_},
              {"minutos", minutos_},
              {"comportamiento", pa::aJson(comp_)},
              {"app", vivoJson_},
              {"borrados", borrados}};
    if (!h.empty()) fin_["hinchada"] = h;
    const json a = h.value("asamblea", json::object());
    const int entraron = a.value("entraron", vivoJson_.value("maxConectados", 0));
    const int bloqueados = a.value("bloqueados", vivoJson_.value("bloqueados", 0));
    const bool bien = pedidos_ > 0 && entraron + bloqueados >= pedidos_ * 95 / 100;
    fin_["bien"] = bien;
    const std::string nota = pa::nota(vivoJson_, h);
    fin_["nota"] = nota;
    if (habiaPrueba && estado_ == QLatin1String("terminando") && inicioMs_ > 0) {
        const std::string nombre =
            "prueba-asamblea-" + QDateTime::fromMSecsSinceEpoch(inicioMs_).toString("yyyyMMdd-HHmmss").toStdString() + ".json";
        ba::persist::cola().escribir(carpetaPruebas() / nombre, fin_.dump(1, ' ', false, json::error_handler_t::replace));
        fin_["archivo"] = (fs::path("Pruebas") / nombre).generic_string();
        if (p_.evento.eventoAbierto().hay())
            p_.evento.guionVm().marcarChequeo(QString::fromLatin1(kItemChequeo), bien, qs(nota),
                                              tr("Prueba de asamblea (integrantes simulados por la sesión remota)"));
    }
    ultimoJson_ = fin_;
    ultimo_ = aMapa(fin_);
    ids_.clear();
    arranco_ = false;
    esperaDesde_.clear();
    archivo_.clear();
    url_.clear();
    salida_.clear();
    hayRecintoAntes_ = false;
    recintoAntes_ = json();
    accesosJson_ = json();
    emit accesoChanged();
    emit informeChanged();
    setEstado(QStringLiteral("apagada"));
    emit notify(qs(nota), bien ? "ok" : "warn");
}

QString PruebaAsambleaViewModel::limpiar() {
    if (activa()) return qs(traducir("asamblea", "hay una prueba en curso: terminala"));
    const int n = limpiarIntegrantes(json(), false);
    if (n > 0) emit notify(tr("Se borraron %n integrantes de prueba que habían quedado (y su acceso).", "", n), "ok");
    return {};
}

void PruebaAsambleaViewModel::limpiarColgada() {
    std::string s;
    json colgada;
    if (ba::persist::cola().leer(archivoEnCurso(), s)) colgada = json::parse(s, nullptr, false);
    const bool hayArchivo = colgada.is_object();
    if (!hayArchivo && integrantesPrueba() == 0) return;
    // El recinto de antes, si la prueba que quedó era de este evento (si no, el recinto no se toca).
    const int n = limpiarIntegrantes(hayArchivo ? colgada.value("recinto", json()) : json(), hayArchivo);
    if (hayArchivo) {
        if (colgada.contains("archivo") && colgada["archivo"].is_string())
            ba::persist::borrar(fs::path(colgada["archivo"].get<std::string>()));
        ba::persist::cola().borrar(archivoEnCurso());
    }
    if (n > 0)
        emit notify(tr("Había una prueba de asamblea sin terminar (el programa se cerró en el medio): se borraron %n "
                       "integrantes de prueba y su acceso.",
                       "", n),
                    "warn");
}

json PruebaAsambleaViewModel::estadoJson(bool conAcceso) const {
    json j{{"estado", estado_.toStdString()},
           {"generador", generador_.toStdString()},
           {"hinchada_disponible", hinchadaDisponible()},
           {"integrantes_reales", integrantesReales()},
           {"integrantes_prueba", integrantesPrueba()},
           {"impedimento", impedimento().toStdString()},
           {"vivo", activa() ? calcularVivo() : json::object()},
           {"comportamiento", pa::aJson(comp_)}};
    if (conAcceso && corriendo()) {
        j["url"] = url_.toStdString();
        j["archivo_credenciales"] = archivo_.toStdString();
        j["comando_externo"] = comandoExterno().toStdString();
    }
    if (!ultimoJson_.empty()) {
        json u = ultimoJson_;
        if (u.contains("hinchada")) u["hinchada"].erase("integrantes"); // el detalle, en prueba_asamblea_informe
        j["ultimo_informe"] = u;
    }
    return j;
}

} // namespace ba::app
