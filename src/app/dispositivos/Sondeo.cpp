// src/app/dispositivos/Sondeo.cpp — ver Sondeo.hpp

#include "dispositivos/Sondeo.hpp"

namespace ba::app::dispositivos {

using json = nlohmann::json;

namespace {
constexpr int kTopeStderr = 64 * 1024; ///< lo último que escribió el driver alcanza
}

Sondeo::Sondeo(Opciones o, QObject* parent) : QObject(parent), o_(std::move(o)) {
    reloj_.setSingleShot(true);
    connect(&reloj_, &QTimer::timeout, this, &Sondeo::vencio);
}

Sondeo::~Sondeo() { cerrarProceso(); }

void Sondeo::cerrarProceso() {
    reloj_.stop();
    if (proceso_ == nullptr) return;
    QProcess* p = proceso_;
    proceso_ = nullptr;
    disconnect(p, nullptr, this, nullptr);
    if (p->state() == QProcess::NotRunning) {
        p->deleteLater();
        return;
    }
    // Sin esperarlo acá (el hilo de la interfaz no se queda esperando a un driver
    // colgado): se borra solo cuando termina.
    p->setParent(nullptr);
    connect(p, &QProcess::finished, p, &QObject::deleteLater);
    p->kill();
}

void Sondeo::lanzar(const QStringList& args) {
    pendiente_.clear();
    errores_.clear();
    fin_ = false;
    huboAvance_ = false;
    actual_.clear();
    proceso_ = new QProcess(this);
    proceso_->setProgram(o_.programa);
    proceso_->setArguments(o_.argsBase + args);
    proceso_->setProcessEnvironment(o_.entorno);
    // UN solo caño (stdout y stderr juntos): lo que el driver escribe por stderr llega en el
    // orden en que se escribió, antes del resultado que lo explica. Con dos caños no hay orden
    // entre ellos y, con la máquina cargada, el resultado llegaba antes que «is JACK server
    // running?» y el motivo salía genérico.
    proceso_->setProcessChannelMode(QProcess::MergedChannels);
    connect(proceso_, &QProcess::readyReadStandardOutput, this, &Sondeo::leerLineas);
    connect(proceso_, &QProcess::finished, this, &Sondeo::alTerminarProceso);
    connect(proceso_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError e) {
        if (e != QProcess::FailedToStart || proceso_ == nullptr) return;
        const QString err = tr("no se pudo lanzar la sonda de drivers: %1").arg(proceso_->errorString());
        cerrarProceso();
        if (listando_) {
            listando_ = false;
            if (auto f = std::move(alListar_)) f(false, {}, err);
        } else {
            terminar(err);
        }
    });
    reloj_.start(o_.arranqueMs);
    proceso_->start();
}

void Sondeo::listar(AlListar listo) {
    if (ocupado()) return listo(false, {}, tr("la sonda ya está trabajando"));
    listando_ = true;
    alListar_ = std::move(listo);
    lista_.clear();
    lanzar({"listar"});
}

void Sondeo::probar(const QStringList& ids, AlProbar uno, AlTerminar fin) {
    if (ocupado()) return fin(tr("la sonda ya está trabajando"));
    if (ids.isEmpty()) return fin({});
    faltan_ = ids;
    total_ = static_cast<int>(ids.size());
    hechos_ = 0;
    alProbar_ = std::move(uno);
    alTerminar_ = std::move(fin);
    emit progreso(0, total_, {});
    lanzar(QStringList{"probar"} + faltan_);
}

void Sondeo::leerLineas() {
    if (proceso_ == nullptr) return;
    pendiente_ += proceso_->readAllStandardOutput();
    for (qsizetype n = pendiente_.indexOf('\n'); n >= 0; n = pendiente_.indexOf('\n')) {
        const QByteArray linea = pendiente_.left(n).trimmed();
        pendiente_.remove(0, n + 1);
        if (linea.isEmpty()) continue;
        const json j = json::parse(linea.constData(), linea.constData() + linea.size(), nullptr, false);
        if (!j.is_object()) {
            // Lo que no es nuestro: lo que dice el driver (por stderr o stdout). Es lo que
            // explica una falla («Open error: is JACK server running?»).
            errores_ += linea + '\n';
            if (errores_.size() > kTopeStderr) errores_ = errores_.right(kTopeStderr);
            continue;
        }
        if (j.contains("fin")) {
            fin_ = true;
            reloj_.stop();
            continue;
        }
        if (listando_) {
            if (j.contains("driver") && j["driver"].is_object()) {
                const json& d = j["driver"];
                lista_.push_back({d.value("id", std::string()), d.value("nombre", std::string()),
                                  d.value("tipo", std::string()), d.value("firma", std::string()),
                                  d.value("rango", 0)});
            }
            continue;
        }
        if (j.contains("empieza")) {
            actual_ = QString::fromStdString(j.value("empieza", std::string()));
            errores_.clear();
            reloj_.start(o_.limiteMs);
            emit progreso(hechos_, total_, actual_);
            continue;
        }
        if (!j.contains("resultado")) continue;
        ResultadoSonda r;
        r.id = j.value("resultado", std::string());
        r.ausente = j.value("ausente", false);
        r.ok = !r.ausente && j.value("ok", false);
        r.detalle = j.value("detalle", std::string());
        if (j.contains("dispositivos") && j["dispositivos"].is_array()) r.dispositivos = j["dispositivos"];
        if (!r.ok && !r.ausente)
            r.motivo = hw::motivoLegible(hw::Falla::Error, r.id, j.value("motivo", std::string()),
                                         errores_.toStdString(), o_.limiteMs);
        faltan_.removeOne(QString::fromStdString(r.id));
        ++hechos_;
        huboAvance_ = true;
        actual_.clear();
        // Entre un driver y el siguiente también corre el tiempo (el hijo podría colgarse
        // descargando el anterior).
        reloj_.start(o_.limiteMs);
        if (alProbar_) alProbar_(r);
        emit progreso(hechos_, total_, {});
    }
}

void Sondeo::alTerminarProceso(int codigo, QProcess::ExitStatus estado) {
    leerLineas();
    const QString actual = actual_;
    const std::string salida = errores_.toStdString();
    cerrarProceso();
    if (listando_) {
        listando_ = false;
        auto f = std::move(alListar_);
        if (f) {
            if (fin_)
                f(true, lista_, {});
            else
                f(false, lista_, tr("la sonda terminó sin terminar de listar (código %1)").arg(codigo));
        }
        return;
    }
    if (fin_ && faltan_.isEmpty()) return terminar({});
    if (!actual.isEmpty()) {
        // Se cayó en medio de un driver: ese fue.
        ResultadoSonda r;
        r.id = actual.toStdString();
        r.falla = hw::Falla::Caida;
        r.motivo = hw::motivoLegible(hw::Falla::Caida, r.id, {}, salida, o_.limiteMs);
        if (estado == QProcess::NormalExit && codigo != 0 && salida.empty())
            r.motivo += " " + tr("(código %1)").arg(codigo).toStdString();
        faltan_.removeOne(actual);
        ++hechos_;
        if (alProbar_) alProbar_(r);
        emit progreso(hechos_, total_, {});
        return seguir();
    }
    if (huboAvance_) return seguir();
    terminar(tr("la sonda de drivers terminó antes de empezar (código %1)").arg(codigo));
}

void Sondeo::vencio() {
    if (proceso_ == nullptr) return;
    const QString actual = actual_;
    const std::string salida = errores_.toStdString();
    cerrarProceso();
    if (listando_) {
        listando_ = false;
        if (auto f = std::move(alListar_)) f(false, lista_, tr("la sonda no terminó de listar a tiempo"));
        return;
    }
    if (!actual.isEmpty()) {
        ResultadoSonda r;
        r.id = actual.toStdString();
        r.falla = hw::Falla::Tiempo;
        r.motivo = hw::motivoLegible(hw::Falla::Tiempo, r.id, {}, salida, o_.limiteMs);
        faltan_.removeOne(actual);
        ++hechos_;
        if (alProbar_) alProbar_(r);
        emit progreso(hechos_, total_, {});
        return seguir();
    }
    if (huboAvance_) return seguir();
    terminar(tr("la sonda de drivers no arrancó en %1 s").arg(o_.arranqueMs / 1000));
}

void Sondeo::seguir() {
    if (faltan_.isEmpty()) return terminar({});
    lanzar(QStringList{"probar"} + faltan_);
}

void Sondeo::terminar(const QString& error) {
    reloj_.stop();
    faltan_.clear();
    actual_.clear();
    alProbar_ = nullptr;
    if (auto f = std::move(alTerminar_)) f(error);
}

} // namespace ba::app::dispositivos
