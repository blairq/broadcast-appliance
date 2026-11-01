// ============================================================================
// src/app/ui/EventosListaViewModel.cpp — ver EventosListaViewModel.hpp
// ============================================================================
#include "ui/EventosListaViewModel.hpp"

#include <QDate>
#include <QDateTime>
#include <QPointer>
#include <QTimer>

#include <algorithm>

#include "arranque/ArchivoConfig.hpp"
#include <QCoreApplication>

namespace ba::app {

namespace {

QVariantMap datosAMap(const evento::DatosDelEvento& d) {
    QStringList etiquetas;
    for (const auto& e : d.etiquetas) etiquetas << QString::fromStdString(e);
    return QVariantMap{{"estado", QString::fromStdString(d.estado)},
                       {"iniciadoMs", static_cast<qint64>(d.iniciadoMs)},
                       {"terminadoMs", static_cast<qint64>(d.terminadoMs)},
                       {"lugar", QString::fromStdString(d.lugar)},
                       {"etiquetas", etiquetas},
                       {"notas", QString::fromStdString(d.notas)},
                       {"automatico", d.automatico},
                       {"dia", QString::fromStdString(d.dia)}};
}

std::string grupoDe(const std::string& carpeta) {
    const auto barra = carpeta.rfind('/');
    return barra == std::string::npos ? std::string() : carpeta.substr(0, barra);
}

std::int64_t ahoraMs() { return QDateTime::currentMSecsSinceEpoch(); }

} // namespace

EventosListaViewModel::EventosListaViewModel(std::filesystem::path raiz, std::filesystem::path ajustes, QObject* parent)
    : QObject(parent), store_(std::move(raiz)), ajustesArchivo_(std::move(ajustes)) {
    fondo_.setMaxThreadCount(1);
    ajustes_ = config::leerObjeto(ajustesArchivo_);
    automatico_ = ajustes_.value("evento_automatico", true);
    // La primera lectura, ya: quien pregunta enseguida (el MCP, el arranque) tiene que
    // ver lo que hay. Las que siguen, en el fondo.
    Leido l{store_.listar(), store_.carpetas(), store_.enCurso()};
    ++lecturas_;
    llego(std::move(l));
    // Lo que cambia en Eventos/ por otro lado (una carpeta copiada a mano): se relee.
    // Los avisos llegan de a varios (un archivo temporal y su renombre): se juntan.
    auto* junta = new QTimer(this);
    junta->setSingleShot(true);
    junta->setInterval(300);
    connect(junta, &QTimer::timeout, this, &EventosListaViewModel::releer);
    connect(&vigia_, &QFileSystemWatcher::directoryChanged, junta, qOverload<>(&QTimer::start));
}

EventosListaViewModel::~EventosListaViewModel() { fondo_.waitForDone(); }

void EventosListaViewModel::esperarLecturas() { fondo_.waitForDone(); }

void EventosListaViewModel::releer() {
    // Una a la vez: lo que se pide mientras se lee sale cuando termina.
    if (leyendo_) {
        otraVez_ = true;
        return;
    }
    leyendo_ = true;
    QPointer<EventosListaViewModel> yo(this);
    fondo_.start([yo, store = store_, cuenta = &lecturas_]() {
        Leido l{store.listar(), store.carpetas(), store.enCurso()};
        ++*cuenta;
        QMetaObject::invokeMethod(
            yo,
            [yo, l = std::move(l)]() mutable {
                if (!yo) return;
                yo->leyendo_ = false;
                yo->llego(std::move(l));
                if (yo->otraVez_) {
                    yo->otraVez_ = false;
                    yo->releer();
                }
            },
            Qt::QueuedConnection);
    });
}

void EventosListaViewModel::llego(Leido l) {
    const QVariantMap antes = enCurso();
    fichas_ = std::move(l.fichas);
    carpetas_ = std::move(l.carpetas);
    enCurso_ = std::move(l.enCurso);
    leida_ = true;
    armarUi();
    vigilar();
    emit listaChanged();
    if (enCurso() != antes) emit enCursoChanged();
}

void EventosListaViewModel::armarUi() {
    eventosUi_.clear();
    for (const evento::Ficha& f : fichas_)
        eventosUi_.append(QVariantMap{{"carpeta", QString::fromStdString(f.carpeta)},
                                      {"grupo", QString::fromStdString(f.grupo)},
                                      {"enCurso", f.carpeta == enCurso_},
                                      {"historico", datosAMap(f.datos)},
                                      {"nombre", QString::fromStdString(f.nombre)},
                                      {"fecha", QString::fromStdString(f.fecha)},
                                      {"inicio", QString::fromStdString(f.inicio)},
                                      {"huso", QString::fromStdString(f.huso)},
                                      {"duracionSeg", f.duracionSeg},
                                      {"bloques", static_cast<int>(f.bloques)},
                                      {"cues", static_cast<int>(f.cues)}});
    carpetasUi_.clear();
    for (const auto& c : carpetas_) carpetasUi_ << QString::fromStdString(c);
}

void EventosListaViewModel::vigilar() {
    // Eventos/ y las carpetas que agrupan (pocas): ahí aparece un evento nuevo.
    QStringList quiero{QString::fromStdString(store_.raiz().string())};
    for (const auto& c : carpetas_) quiero << QString::fromStdString(store_.carpetaDe(c).string());
    const QStringList hay = vigia_.directories();
    QStringList sacar, poner;
    for (const QString& d : hay)
        if (!quiero.contains(d)) sacar << d;
    for (const QString& d : quiero)
        if (!hay.contains(d)) poner << d;
    if (!sacar.isEmpty()) vigia_.removePaths(sacar);
    if (!poner.isEmpty()) vigia_.addPaths(poner);
}

QVariantMap EventosListaViewModel::enCurso() const {
    if (enCurso_.empty()) return {};
    const auto it = std::find_if(fichas_.begin(), fichas_.end(), [this](const evento::Ficha& f) { return f.carpeta == enCurso_; });
    const evento::DatosDelEvento d = it == fichas_.end() ? evento::DatosDelEvento{} : it->datos;
    const std::string nombre = it == fichas_.end() || it->nombre.empty() ? enCurso_ : it->nombre;
    return QVariantMap{{"carpeta", QString::fromStdString(enCurso_)},
                       {"nombre", QString::fromStdString(nombre)},
                       {"grupo", QString::fromStdString(grupoDe(enCurso_))},
                       {"desdeMs", static_cast<qint64>(d.iniciadoMs)},
                       {"automatico", d.automatico}};
}

evento::DatosDelEvento EventosListaViewModel::datos(const std::string& carpeta) const {
    for (const evento::Ficha& f : fichas_)
        if (f.carpeta == carpeta) return f.datos;
    return {};
}

void EventosListaViewModel::setEventoAutomatico(bool si) {
    if (si == automatico_) return;
    automatico_ = si;
    ajustes_["evento_automatico"] = si; // lo demás que haya, se conserva
    config::guardar(ajustesArchivo_, ajustes_);
    emit automaticoChanged();
    asegurarEnCurso();
}

void EventosListaViewModel::guardarAjustesHistorico(nlohmann::json j) {
    if (!j.is_object()) return;
    ajustes_ = std::move(j);
    config::guardar(ajustesArchivo_, ajustes_);
    const bool a = ajustes_.value("evento_automatico", true);
    if (a != automatico_) {
        automatico_ = a;
        emit automaticoChanged();
        asegurarEnCurso();
    }
}

void EventosListaViewModel::asegurarEnCurso() {
    if (!automatico_) return;
    const std::string dia = QDate::currentDate().toString("yyyy-MM-dd").toStdString();
    // Lo de siempre no va al disco: hay uno en curso y es el elegido (sigue aunque pase
    // la medianoche) o la jornada de hoy.
    if (!enCurso_.empty()) {
        const evento::DatosDelEvento d = datos(enCurso_);
        if (!d.automatico || d.dia == dia) return;
    }
    std::string err;
    const std::string c = store_.asegurarDelDia(dia, ahoraMs(), &err);
    if (c.empty()) {
        emit notify(QCoreApplication::translate("EventoViewModel", "No se pudo crear el evento del día: %1").arg(QString::fromStdString(err)), "error");
        return;
    }
    if (c != enCurso_) {
        enCurso_ = c;
        emit enCursoChanged();
    }
    releer();
}

bool EventosListaViewModel::iniciar(const std::string& carpeta, std::string* err) {
    if (enCurso_ == carpeta) return true;
    if (!store_.iniciar(carpeta, ahoraMs(), err)) return false;
    enCurso_ = carpeta;
    armarUi();
    emit enCursoChanged();
    emit listaChanged();
    releer(); // la ficha nueva (desde cuándo)
    return true;
}

bool EventosListaViewModel::terminar(std::string* err) {
    if (!store_.terminar(ahoraMs(), err)) return false;
    enCurso_.clear();
    // Con el automático prendido, el del día vuelve a quedar en curso (nada pasa sin evento).
    asegurarEnCurso();
    armarUi();
    emit enCursoChanged();
    emit listaChanged();
    releer();
    return true;
}

std::string EventosListaViewModel::mover(const std::string& carpeta, const std::string& aCarpeta, std::string* err) {
    const std::string nueva = store_.mover(carpeta, aCarpeta, err);
    if (nueva.empty()) return {};
    for (evento::Ficha& f : fichas_)
        if (f.carpeta == carpeta) {
            f.carpeta = nueva;
            f.grupo = grupoDe(nueva);
        }
    if (enCurso_ == carpeta) enCurso_ = nueva;
    armarUi();
    emit enCursoChanged();
    emit listaChanged();
    releer();
    return nueva;
}

std::string EventosListaViewModel::crearCarpeta(const std::string& ruta, std::string* err) {
    const std::string c = store_.crearCarpeta(ruta, err);
    if (c.empty()) return {};
    if (std::find(carpetas_.begin(), carpetas_.end(), c) == carpetas_.end()) {
        carpetas_.push_back(c);
        std::sort(carpetas_.begin(), carpetas_.end());
    }
    armarUi();
    emit listaChanged();
    releer();
    return c;
}

std::string EventosListaViewModel::crear(const std::string& nombre, const std::string& enCarpeta, std::string* err) {
    const std::string c = store_.crear(nombre, err, enCarpeta);
    if (c.empty()) return {};
    // Ya en la lista (lo demás, con la próxima lectura).
    evento::Ficha f;
    f.carpeta = c;
    f.grupo = grupoDe(c);
    f.nombre = nombre.empty() ? c : nombre;
    fichas_.push_back(std::move(f));
    std::sort(fichas_.begin(), fichas_.end(), [](const evento::Ficha& a, const evento::Ficha& b) { return a.nombre < b.nombre; });
    armarUi();
    emit listaChanged();
    releer();
    return c;
}

void EventosListaViewModel::borrar(const std::string& carpeta, std::function<void(const std::string& error)> listo) {
    QPointer<EventosListaViewModel> yo(this);
    fondo_.start([yo, store = store_, carpeta, listo = std::move(listo)]() {
        std::string err;
        const bool ok = store.borrar(carpeta, &err);
        QMetaObject::invokeMethod(
            yo,
            [yo, carpeta, ok, err, listo]() {
                if (!yo) return;
                if (ok) {
                    auto& f = yo->fichas_;
                    f.erase(std::remove_if(f.begin(), f.end(), [&](const evento::Ficha& x) { return x.carpeta == carpeta; }), f.end());
                    const bool eraEnCurso = yo->enCurso_ == carpeta;
                    if (eraEnCurso) yo->enCurso_.clear();
                    yo->armarUi();
                    emit yo->listaChanged();
                    if (eraEnCurso) emit yo->enCursoChanged();
                    yo->releer();
                }
                if (listo) listo(ok ? std::string() : err);
            },
            Qt::QueuedConnection);
    });
}

bool EventosListaViewModel::guardarDatos(const std::string& carpeta, const evento::DatosDelEvento& d, std::string* err) {
    if (!store_.guardarDatos(carpeta, d, err)) return false;
    for (evento::Ficha& f : fichas_)
        if (f.carpeta == carpeta) f.datos = d;
    armarUi();
    emit listaChanged();
    return true;
}

void EventosListaViewModel::alGuardar(const std::string& carpeta, const evento::Evento& e) {
    for (evento::Ficha& f : fichas_) {
        if (f.carpeta != carpeta) continue;
        if (!e.nombre.empty()) f.nombre = e.nombre;
        f.fecha = e.fecha;
        f.inicio = e.inicio;
        f.huso = e.huso;
        f.duracionSeg = e.duracionSeg();
        f.bloques = e.bloques.size();
        f.cues = e.cantidadDeCues();
        armarUi();
        emit listaChanged();
        if (carpeta == enCurso_) emit enCursoChanged();
        return;
    }
    releer(); // no estaba (recién creado por otro lado)
}

} // namespace ba::app
