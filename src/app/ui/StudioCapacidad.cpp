// src/app/ui/StudioCapacidad.cpp — arma la prueba de capacidad con lo de la app
//
// ui/CapacidadViewModel recibe todo lo de afuera por funciones (así se prueba sola, con
// test_capacidad_vm); acá se le pasa lo de verdad: la sala, el cupo, los recursos de la
// máquina, la grilla y el audio (por la ventanilla del motor), el enlace, pinggy, la red y
// hinchada. Ver docs/funciones/CAPACIDAD.md.

#include <QCoreApplication>
#include <QDateTime>
#include <QPointer>
#include <QSysInfo>

#include "diag/Recursos.hpp"
#include "arranque/RuntimeMotor.hpp"
#include "emision/ApiMotor.hpp"
#include "emision/VentanillaMotor.hpp"
#include "escenas/ControlGrillas.hpp"
#include "i18n/I18nTexto.hpp"
#include "net/AccesoRemoto.hpp"
#include "net/ClienteCapacidad.hpp"
#include "platform/Paths.hpp"
#include "sala/CupoSala.hpp"
#include "sala/GeneradorCarga.hpp"
#include "sala/MedidaMaquina.hpp"
#include "ui/CapacidadViewModel.hpp"
#include "ui/ConferenceClient.hpp"
#include "ui/ConferenceViewModel.hpp"
#include "ui/EnlaceViewModel.hpp"
#include "ui/EstadoAireViewModel.hpp"
#include "ui/PruebaSalaViewModel.hpp"
#include "ui/RecursosViewModel.hpp"
#include "ui/SalaViewModel.hpp"
#include "ui/StudioViewModel.hpp"

#include <QQmlEngine>

namespace ba::app {

namespace cap = sala::capacidad;
using json = nlohmann::json;

void StudioViewModel::crearCapacidad() {
    ConferenceClient& cli = conference_->client();
    CapacidadViewModel::Partes p;
    p.online = [&cli]() { return cli.online(); };
    p.foto = [&cli]() { return cli.snapshot(); };
    p.post = [&cli](const QString& ruta, const QJsonObject& cuerpo, CapacidadViewModel::Respuesta r) {
        cli.post(ruta, cuerpo, std::move(r));
    };
    p.get = [&cli](const QString& ruta, CapacidadViewModel::Respuesta r) { cli.get(ruta, std::move(r)); };
    p.del = [&cli](const QString& ruta) { cli.del(ruta); };
    p.cambiarCupo = [this](int n) { cupoSala().cambiar(n); };
    // La máquina: lo que ya mide el muestreador de recursos (una vez por segundo, en su
    // hilo) y la memoria disponible de la guardia. Los procesos de hinchada se suman al
    // muestreador para saber cuánta CPU usa el generador.
    p.medirMaquina = [this](const std::vector<std::uint32_t>& pids) {
        recursos_->medirTambien("hinchada", pids);
        const diag::Recursos::Muestra r = recursos_->muestreador().ultima();
        const sala::prueba::Medida mm = sala::prueba::medirMaquina();
        cap::Muestra m;
        m.cpuMaquina = r.cpu;
        m.memLibreMb = mm.memLibreMb;
        m.memTotalMb = r.ramTotalMb;
        double gen = 0;
        bool hayGen = false;
        for (const auto& pr : r.procesos) {
            if (pr.nombre == "app") m.cpuApp = pr.cpu;
            else if (pr.nombre == "gateway") m.cpuGateway = pr.cpu;
            else if (pr.nombre == "hinchada" && pr.cpu >= 0) {
                gen += pr.cpu;
                hayGen = true;
            }
        }
        m.cpuGenerador = hayGen ? gen : -1;
        return m;
    };
    p.medidaGuardia = []() { return sala::prueba::medirMaquina(); };
    // La grilla: los cuadros por segundo del mosaico (lo mismo que grilla_personas_medir).
    p.medirGrilla = [this](int segundos, std::function<void(double)> listo) {
        escenas::ControlGrillas* g = grillasPersonas_.get();
        if (g == nullptr) return listo(-1);
        g->recalcular();
        const std::string e = g->medir(segundos, json::object(), [listo](const json& r) { listo(cap::cpsDeMedicion(r)); });
        if (!e.empty()) listo(-1);
    };
    // La edad del audio de los invitados: la latencia del estudio es del dueño del motor.
    p.medirAudio = [this](std::function<void(double)> listo) {
        ventanilla_.pedir(
            this, "capacidad_audio", [&rt = rt_]() { return rt.engine().latenciaEstudio(); },
            [listo](const json& l) { listo(cap::edadAudioInvitados(l)); });
    };
    p.alAire = [this]() { return alAire(); };
    p.otraPrueba = [this]() -> QString {
        if (pruebaSala_ && pruebaSala_->corriendo())
            return QString::fromStdString(ba::i18n::traducir("capacidad", "hay una prueba de sala corriendo"));
        if (enlace_ && enlace_->midiendo())
            return QString::fromStdString(ba::i18n::traducir("capacidad", "se está midiendo el enlace"));
        return {};
    };
    // El enlace por WireGuard.
    p.hayTunel = [this]() { return enlace_ && enlace_->hayTunel(); };
    p.medirEnlace = [this]() { return enlace_->medir(false); };
    p.cancelarEnlace = [this]() { enlace_->cancelar(); };
    p.enlaceMidiendo = [this]() { return enlace_->midiendo(); };
    p.ultimoEnlace = [this]() { return enlace_->ultimaBuena(); };
    p.juzgarEnlace = [this](const sala::enlace::Medicion& m, int n) { return enlace_->juzgarCon(m, n); };
    // pinggy (el camino de los invitados).
    AccesoRemoto* acc = accesoRemoto();
    p.pinggy = [acc]() { return acc ? acc->conexion() : QVariantMap{}; };
    p.prenderPinggy = [this, acc]() -> QString {
        if (acc == nullptr) return QString::fromStdString(ba::i18n::traducir("capacidad", "no disponible"));
        QString id;
        for (const QVariant& v : acc->formasDeConectar()) {
            const QString f = v.toMap().value("id").toString();
            if (f.startsWith(QLatin1String("pinggy"))) {
                id = f;
                break;
            }
        }
        if (id.isEmpty())
            return QString::fromStdString(ba::i18n::traducir("capacidad", "no hay cómo prender pinggy (Configuración → Pinggy)"));
        acc->usar(capacidad_.get(), true, QStringLiteral("capacidad"));
        if (!acc->conectar(id)) {
            acc->usar(capacidad_.get(), false);
            return QString::fromStdString(ba::i18n::traducir("capacidad", "pinggy no arrancó"));
        }
        return {};
    };
    p.apagarPinggy = [this, acc]() {
        if (acc == nullptr) return;
        acc->desconectar();
        acc->usar(capacidad_.get(), false);
    };
    // La red.
    p.pedirRemota = [](QObject* quien, const QString& url, const QString& cred, const QString& huella,
                       std::function<void(const json&)> listo) { net::pedirMedidaRemota(quien, url, cred, huella, std::move(listo)); };
    p.medirIdaYVuelta = [](QObject* quien, const QString& url, const QString& secreto, std::shared_ptr<std::atomic<bool>> cancelar,
                           std::function<void(const json&)> listo) {
        net::medirIdaYVuelta(quien, url, secreto, net::IdaYVuelta{}, std::move(cancelar), std::move(listo));
    };
    // hinchada, la que viaja en el programa (como la prueba de sala), y su video grabado
    // común a todas las configuraciones.
#ifdef _WIN32
    const QString exe = QCoreApplication::applicationDirPath() + "/hinchada/hinchada.exe";
#else
    const QString exe = QCoreApplication::applicationDirPath() + "/hinchada/hinchada";
#endif
    const QString grabado = QString::fromStdString((ba::platform::dataRoot() / "Runtime" / "hinchada" / "grabado.hgr").string());
    p.generador = std::make_unique<sala::GeneradorHinchada>(exe, grabado);
    p.dataRoot = rt_.dataRoot();
    p.maquina = QSysInfo::machineHostName();
    capacidad_ = std::make_unique<CapacidadViewModel>(std::move(p));
    connect(capacidad_.get(), &CapacidadViewModel::notify, this, &StudioViewModel::notify);
    // Lo que la hace revisar (sin sondeo): la foto de la sala, lo que está al aire, la sala
    // que va y viene, el enlace que termina y pinggy que cambia.
    CapacidadViewModel* c = capacidad_.get();
    // De la sala, solo quién está (no la red de cada uno, que cambia en cada foto).
    connect(&cli, &ConferenceClient::salaCambio, c, [c](unsigned partes) {
        if ((partes & sala::Gente) != 0) c->revisar();
    });
    connect(&cli, &ConferenceClient::onlineChanged, c, &CapacidadViewModel::alCambiarOnline);
    connect(&cli, &ConferenceClient::estadoEvento, c, &CapacidadViewModel::alEventoSala);
    connect(aire_.get(), &EstadoAireViewModel::aireChanged, c, &CapacidadViewModel::revisar);
    connect(enlace_.get(), &EnlaceViewModel::termino, c, &CapacidadViewModel::alTerminarEnlace);
    connect(enlace_.get(), &EnlaceViewModel::medicionChanged, c, &CapacidadViewModel::revisar);
    if (acc != nullptr) connect(acc, &AccesoRemoto::estadoChanged, c, &CapacidadViewModel::alCambiarPinggy);
    // La prueba de sala no arranca con la de capacidad cargando la máquina.
    pruebaSala_->setOtraPrueba([c]() -> QString {
        if (c->estado() == QLatin1String("local") || c->estado() == QLatin1String("remoto") ||
            c->estado() == QLatin1String("recibiendo"))
            return QString::fromStdString(ba::i18n::traducir("capacidad", "hay una prueba de capacidad corriendo"));
        return {};
    });
}

QObject* StudioViewModel::capacidadQml() const {
    QQmlEngine::setObjectOwnership(capacidad_.get(), QQmlEngine::CppOwnership);
    return capacidad_.get();
}

} // namespace ba::app
