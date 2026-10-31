// src/app/ui/AppViewModel.cpp — ver el .hpp

#include "ui/AppViewModel.hpp"

#include "Piezas.hpp" // motor::derivaDePlacas (los chequeos del equipo)
#include "agent/AgentViewModel.hpp"
#include "ajustes/Ajustes.hpp"
#include "arranque/AppRuntime.hpp"
#include "arranque/AppSettings.hpp"
#include "arranque/Configuraciones.hpp"
#include "arranque/ModoVivoConfig.hpp"
#include "control/BusEventos.hpp"
#include "diag/ChequeosEquipo.hpp"
#include "diag/Registro.hpp"
#include "emision/VentanillaMotor.hpp"
#include "i18n/I18n.hpp"
#include "persist/ColaEscritura.hpp"
#include "platform/GstEntorno.hpp"
#include "platform/Paths.hpp"
#include "ui/SystemViewModel.hpp"

#include <QCoreApplication>
#include <QDateTime>
#include <QQmlEngine>
#include <QStringList>
#include <QTimer>

#include <fstream>
#include <system_error>

namespace ba::app {

namespace {

/// Sin padre y devuelto a QML: que no se lo quede su recolector. Es del área.
QObject* deCpp(QObject* o) {
    if (o != nullptr) QQmlEngine::setObjectOwnership(o, QQmlEngine::CppOwnership);
    return o;
}

/// «a», «a y b», «a, b y c».
QString enumerar(QStringList l) {
    if (l.isEmpty()) return {};
    const QString ultimo = l.takeLast();
    return l.isEmpty() ? ultimo
                       : QCoreApplication::translate("estudio", "%1 y %2")
                             .arg(l.join(QCoreApplication::translate("estudio", ", ")), ultimo);
}

} // namespace

AppViewModel::AppViewModel(Partes p, QObject* parent) : QObject(parent), p_(std::move(p)) {
    AppRuntime& rt = p_.rt;
    modoVivoCfg_ = std::make_unique<config::ModoVivoConfig>(rt.dataRoot());
    system_ = std::make_unique<SystemViewModel>();
    // Los chequeos del equipo en la tarjeta de preparación (F4-07): con lo que ya miden
    // las salidas (las sesiones de codificador), el motor (las placas) y el reloj.
    // Lo del motor (las sesiones, el reloj), de su foto (parte Equipo).
    system_->alEquipo([&v = p_.ventanilla]() {
        const FotoMotor::DelEquipo& eq = v.foto().equipo;
        diag::DatosEquipo d;
        d.codificaciones = eq.salidas.value("codificaciones", nlohmann::json::array());
        d.limiteSesiones = ba::ajustes::entero("equipo.limiteSesiones", 0);
        d.placas = ba::motor::derivaDePlacas();
        d.relojTipo = eq.relojTipo;
        d.relojError = eq.relojError;
        const std::filesystem::path ayudante = ba::platform::gstRaiz() / "libexec" / "gstreamer-1.0" /
#if defined(_WIN32)
                                               "gst-ptp-helper.exe";
#else
                                               "gst-ptp-helper";
#endif
        std::error_code ec;
        d.ptpAyudante = std::filesystem::exists(ayudante, ec);
        d.ptpPermisos = diag::permisosDeAyudantePtp(ayudante);
        QVariantList out;
        for (const auto& it : diag::chequeosDeEquipo(d))
            out.push_back(QVariantMap{{"id", QString::fromStdString(it.id)},
                                      {"estado", QString::fromStdString(it.estado)},
                                      {"titulo", QString::fromStdString(it.titulo)},
                                      {"detalle", QString::fromStdString(it.detalle)},
                                      {"como", QString::fromStdString(it.como)}});
        return out;
    });
    // Cambiar de configuración = anotarla y reiniciar por el launcher (como una actualización).
    configuraciones_ = std::make_unique<Configuraciones>([this]() {
        const QString evento = configuraciones_->activaUi();
        const QString aviso = eventoNuevo_ ? tr("Evento «%1» creado: reiniciando para abrirlo").arg(evento)
                                           : tr("Abriendo el evento «%1»: la app se reinicia").arg(evento);
        eventoNuevo_ = false;
        return reiniciarPrograma(aviso);
    });
    // Terminó un evento nuevo hecho aparte (una copia puede tardar): un solo aviso.
    connect(configuraciones_.get(), &Configuraciones::nuevoEventoListo, this,
            [this](const QString& nombre, const QString& error, bool abriendo) {
                eventoNuevo_ = abriendo; // el reinicio sale enseguida (Configuraciones::nuevoEventoAparte)
                if (!error.isEmpty()) emit notify(tr("No se pudo crear «%1»: %2").arg(nombre, error), "error");
                else if (!abriendo) emit notify(tr("Evento «%1» listo: abrilo con «Abrir otro…»").arg(nombre), "info");
            });
    // Lo que lleva más de un mes en la papelera de eventos se va (con grabaciones, son GB).
    if (const int n = configuraciones_->vaciarViejos(Configuraciones::kDiasPapelera); n > 0)
        qInfo("%s", ba::i18n::llenar(ba::i18n::traducir("eventos", "[eventos] papelera: %1 con más de %2 días, borrados"),
                                     {std::to_string(n), std::to_string(Configuraciones::kDiasPapelera)})
                        .c_str());
    // Antes de cerrarse, el aviso con una cuenta regresiva (Main.qml lo muestra encima de todo):
    // que se cierre de golpe parecía una caída. El launcher ya está esperando que cierre.
    connect(system_.get(), &SystemViewModel::quitForUpdate, this, &AppViewModel::contarHastaCerrar);
    leerCaida();
    // La foto para los otros hilos, cada vez que cambia lo que muestra.
    connect(this, &AppViewModel::estadoCambio, this, &AppViewModel::publicarFoto);
    connect(this, &AppViewModel::caidaChanged, this, &AppViewModel::publicarFoto);
    publicarFoto();
    if (!configRecuperada().empty())
        QTimer::singleShot(3000, this, [this]() {
            emit notify(tr("La configuración «%1» estaba dañada: se cargó la versión anterior (la dañada quedó "
                           "como .corrupto).")
                            .arg(QString::fromStdString(configRecuperada())),
                        "warn");
        });
}

AppViewModel::~AppViewModel() = default;

QObject* AppViewModel::sistemaQml() const { return deCpp(system_.get()); }
QObject* AppViewModel::configuracionesQml() const { return deCpp(configuraciones_.get()); }

void AppViewModel::contarHastaCerrar(const QString& aviso) {
    emit notify(aviso, "info");
    reinicio_ = {{"aviso", aviso}, {"segundos", 3}};
    emit reinicioChanged();
    auto* cuenta = new QTimer(this);
    cuenta->setInterval(1000);
    connect(cuenta, &QTimer::timeout, this, [this, cuenta]() {
        const int quedan = reinicio_.value("segundos").toInt() - 1;
        reinicio_["segundos"] = std::max(0, quedan);
        emit reinicioChanged();
        if (quedan > 0) return;
        cuenta->stop();
        emit quitRequested();
    });
    cuenta->start();
}

void AppViewModel::leerCaida() {
    // ¿Volvemos de una caída? El supervisor dejó escrita la causa.
    const auto archivo = ba::platform::dataRoot() / "Runtime" / "ultima-caida.json";
    std::ifstream f(archivo);
    nlohmann::json j = nlohmann::json::parse(f, nullptr, false);
    const qint64 ahora = QDateTime::currentMSecsSinceEpoch();
    if (!j.is_object() || j.value("vista", false) ||
        ahora - j.value("hora_ms", 0LL) >= ba::ajustes::entero64("launcher.cartelCaidaMin", 10) * 60 * 1000)
        return;
    caidaMs_ = j.value("hora_ms", 0LL);
    caida_ = QVariantMap{{"causa", QString::fromStdString(j.value("causa", std::string()))},
                         {"componente", QString::fromStdString(j.value("componente", std::string()))},
                         {"hace_s", static_cast<int>((ahora - caidaMs_) / 1000)}};
    // ¿Había algo al aire (en alguna salida)? Si no, no hay nada que esperar.
    bool habia = false;
    for (const auto& [id, a] : p_.rt.aireAnterior().salidas) habia = habia || a.transmitiendo || a.grabando || !a.destinos.empty();
    caida_["habia_aire"] = habia;
    j["vista"] = true; // el cartel sale una sola vez
    f.close();
    ba::persist::cola().escribir(archivo, j.dump(1, ' ', false, nlohmann::json::error_handler_t::replace));
}

void AppViewModel::cerrarCartelCaida() {
    caida_.clear();
    emit caidaChanged();
}

nlohmann::json AppViewModel::caidas(int n) {
    std::ifstream f(ba::platform::dataRoot() / "Runtime" / "caidas.jsonl");
    std::vector<nlohmann::json> todas;
    std::string l;
    while (std::getline(f, l)) {
        const nlohmann::json j = nlohmann::json::parse(l, nullptr, false);
        if (j.is_object()) todas.push_back(j);
    }
    nlohmann::json out = nlohmann::json::array();
    for (int i = static_cast<int>(todas.size()) - 1; i >= 0 && static_cast<int>(out.size()) < n; --i)
        out.push_back(todas[static_cast<std::size_t>(i)]);
    return out;
}

void AppViewModel::vigilar(control::BusEventos& bus) {
    // Volviendo de una caída: cuánto tardó el aire en volver.
    if (!caida_.isEmpty() && !caida_.contains("aire_vuelta_s") && (p_.transmitiendo() || p_.grabando()) && caidaMs_ > 0) {
        caida_["aire_vuelta_s"] = (QDateTime::currentMSecsSinceEpoch() - caidaMs_) / 1000.0;
        emit caidaChanged();
        // Volvió el aire: el aviso dura 30 s más y se cierra solo (en el modelo, no en la
        // vista: el MCP deja de informarla aunque la ventana no la muestre).
        const qint64 marca = caidaMs_;
        QTimer::singleShot(30000, this, [this, marca]() {
            if (caidaMs_ == marca && caida_.contains("aire_vuelta_s")) cerrarCartelCaida();
        });
    }
    // Al terminar el vivo, lo desbloqueado vuelve a quedar protegido para la próxima.
    if (const bool vivo = modoVivo(); vivo != estabaEnVivo_) {
        estabaEnVivo_ = vivo;
        if (!vivo) desbloqueadas_.clear();
        publicarFoto();
        bus.publicar("aire/modo_vivo", {{"activo", vivo}});
    }
}

bool AppViewModel::modoVivo() const {
    // En vivo = sale algo de verdad (una transmisión; la grabación, si el ajuste
    // la cuenta). Correr el guion del evento sin transmitir no es estar al aire.
    // De la foto del motor: haySalida cuenta la grabación, hayTransmision no.
    if (!modoVivoCfg_->activo()) return false;
    const FotoMotor::EstadoAire& e = p_.ventanilla.foto().estado;
    return modoVivoCfg_->incluirGrabando() ? e.haySalida : e.hayTransmision;
}

bool AppViewModel::paginaBloqueada(const QString& id) const {
    return modoVivo() && modoVivoCfg_->paginas().contains(id) && !desbloqueadas_.contains(id);
}

void AppViewModel::desbloquearPagina(const QString& id, const QString& motivo) {
    desbloqueadas_.insert(id);
    ba::diag::registro().info(ba::i18n::llenar(ba::i18n::traducir("modo-vivo", "modo en vivo: se desbloqueó «%1» — motivo: %2"),
                                               {id.toStdString(), motivo.toStdString()}));
    emit estadoCambio();
}

void AppViewModel::publicarFoto() {
    nlohmann::json pg = nlohmann::json::array();
    for (const QString& x : modoVivoCfg_->paginas()) pg.push_back(x.toStdString());
    nlohmann::json d = nlohmann::json::array();
    for (const QString& x : desbloqueadas_) d.push_back(x.toStdString());
    nlohmann::json caida = nlohmann::json::object();
    if (!caida_.isEmpty()) {
        caida = {{"causa", caida_.value("causa").toString().toStdString()}, {"habia_aire", caida_.value("habia_aire").toBool()}};
        if (caida_.contains("aire_vuelta_s")) caida["aire_vuelta_s"] = caida_.value("aire_vuelta_s").toDouble();
    }
    foto_.poner({{"activo", modoVivoCfg_->activo()}, {"incluir_grabando", modoVivoCfg_->incluirGrabando()},
                 {"paginas_protegidas", pg}, {"desbloqueadas", d}, {"caida", caida}});
}

nlohmann::json AppViewModel::caidaRecienteFoto() const { return foto_.leer().value("caida", nlohmann::json::object()); }

nlohmann::json AppViewModel::modoVivoEstado() const {
    const nlohmann::json f = foto_.leer();
    const bool activo = f.value("activo", false);
    // Como modoVivo(), con lo de la foto: si sale algo lo dice el motor desde cualquier hilo.
    const bool enVivo = activo && p_.rt.haySalida(f.value("incluir_grabando", false));
    return {{"en_vivo", enVivo}, {"activo", activo}, {"incluir_grabando", f.value("incluir_grabando", false)},
            {"paginas_protegidas", f.value("paginas_protegidas", nlohmann::json::array())},
            {"desbloqueadas", f.value("desbloqueadas", nlohmann::json::array())},
            {"que_protege", ba::i18n::traducir("asistente", "las páginas de la lista piden escribir CONFIRMAR con un motivo; los ajustes que reinician el gateway o el programa no se aplican")}};
}

std::string AppViewModel::setModoVivoAjustes(const nlohmann::json& a) {
    if (modoVivo()) return tr("no se cambia en vivo").toStdString();
    const std::string e = modoVivoCfg_->cambiar(a);
    emit estadoCambio();
    return e;
}

QVariantMap AppViewModel::impactoDeCerrar() const {
    const bool tx = p_.transmitiendo(), rec = p_.grabando();
    const int n = p_.personas();
    return {{"transmitiendo", tx}, {"grabando", rec}, {"personas", n}, {"hay", tx || rec || n > 0}};
}

QString AppViewModel::avisoDeCerrar(const QString& accion) const {
    const bool tx = p_.transmitiendo(), rec = p_.grabando();
    const int n = p_.personas();
    if (!tx && !rec && n == 0) return {};
    QStringList corta;
    if (accion.isEmpty()) {
        // Cerrar la ventana: «Estás AL AIRE y hay 2 personas conectadas. Si cerrás, …».
        QString que = tx && rec ? tr("Estás AL AIRE y grabando")
                      : tx      ? tr("Estás AL AIRE")
                      : rec     ? tr("Estás grabando")
                                : QString();
        if (n > 0)
            que = que.isEmpty() ? tr("Hay %n persona conectada", "", n)
                  : tx && rec   ? tr("%1, y hay %n persona conectada", "", n).arg(que)
                                : tr("%1 y hay %n persona conectada", "", n).arg(que);
        if (tx) corta << tr("se corta la emisión");
        if (rec) corta << tr("se detiene la grabación");
        if (n > 0) corta << (n == 1 ? tr("esa persona queda afuera") : tr("esas personas quedan afuera"));
        return tr("%1. Si cerrás, %2.").arg(que, enumerar(corta));
    }
    // Actualizar, volver atrás…: «Estás AL AIRE: actualizar cierra la aplicación y …».
    // `accion` es una clave ("actualizar", "volver"); por compatibilidad también el texto
    // que mandaba la página («volver a la versión anterior»). Otra cosa va tal cual.
    const QString que = tx ? tr("Estás AL AIRE") : rec ? tr("Estás GRABANDO") : tr("Hay gente conectada");
    if (tx) corta << tr("corta la emisión");
    if (rec) corta << tr("corta la grabación");
    if (n > 0) corta << (corta.isEmpty() ? tr("la desconecta") : tr("desconecta a quienes están conectados"));
    if (accion == "actualizar") return tr("%1: actualizar cierra la aplicación y %2.").arg(que, enumerar(corta));
    if (accion == "volver" || accion == "volver a la versión anterior")
        return tr("%1: volver a la versión anterior cierra la aplicación y %2.").arg(que, enumerar(corta));
    return tr("%1: %2 cierra la aplicación y %3.").arg(que, accion, enumerar(corta));
}

QString AppViewModel::reiniciarPrograma(const QString& aviso) {
    // Los modos de esta corrida (--test / --debug) siguen después del reinicio.
    QStringList args{"--reiniciar"};
    for (const QString& a : QCoreApplication::arguments())
        if (a == "--test" || a == "--debug") args << a;
    // El emisor aparte sigue después del reinicio: si este launcher lo lanzó (A recibe
    // BA_EMISOR), el nuevo también (aunque haya venido de --emisor-aparte y no del ajuste).
    if (!qEnvironmentVariableIsEmpty("BA_EMISOR")) args << "--emisor-aparte";
    // La conversación del asistente sigue después del reinicio (la retoma al abrir y le
    // avisa en qué evento quedó, para que siga con lo que estaba haciendo).
    if (agent::AgentViewModel* agente = p_.asistente ? p_.asistente() : nullptr)
        if (const QString s = agente->sesionParaReanudar(); !s.isEmpty())
            args << "--app-arg=--reanudar-asistente=" + s + "|" + agente->provider() + "|" + agente->model()
                 << "--app-arg=--asistente-evento=" + configuraciones_->activaUi();
    return system_->launch(args, aviso);
}

} // namespace ba::app
