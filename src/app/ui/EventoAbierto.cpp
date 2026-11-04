// ============================================================================
// src/app/ui/EventoAbierto.cpp — ver EventoAbierto.hpp
// ============================================================================
#include "ui/EventoAbierto.hpp"

#include <QCoreApplication>

#include <QDir>
#include <QDateTime>
#include <QTimeZone>

#include <system_error>

namespace ba::app {

EventoAbierto::EventoAbierto(std::filesystem::path raizEventos, QObject* parent)
    : QObject(parent), store_(std::move(raizEventos)) {}

std::filesystem::path EventoAbierto::dir() const {
    return abierto_.isEmpty() ? std::filesystem::path() : store_.carpetaDe(abierto_.toStdString());
}

std::filesystem::path EventoAbierto::sub(const char* nombre) const {
    return abierto_.isEmpty() ? std::filesystem::path() : dir() / nombre;
}

QString EventoAbierto::dentro(const QString& sub, const QString& rel) const {
    if (abierto_.isEmpty() || rel.isEmpty()) return {};
    const QDir carpeta(QString::fromStdString(dir().string()));
    // "Medios/x.wav" y "x.wav" valen lo mismo: el operador escribe lo que le
    // sale y las dos formas apuntan al mismo lado.
    QString limpio = rel;
    if (limpio.startsWith(sub + "/") || limpio.startsWith(sub + "\\")) limpio = limpio.mid(sub.size() + 1);
    const QDir d(carpeta.filePath(sub));
    const QString abs = QDir::cleanPath(d.filePath(limpio));
    // Fuera de esa subcarpeta no se lee nada: un ".." en un guion que vino de
    // otro lado no tiene por que sacarle archivos al que lo abre.
    if (!abs.startsWith(QDir::cleanPath(d.absolutePath()) + "/")) return {};
    return abs;
}

QString EventoAbierto::cuando() const {
    if (e_.fecha.empty() || e_.inicio.empty()) return {};
    if (!evento::fechaValida(e_.fecha) || !evento::horaValida(e_.inicio)) return {};
    const QDate d = QDate::fromString(QString::fromStdString(e_.fecha), Qt::ISODate);
    const QTime h = QTime::fromString(QString::fromStdString(e_.inicio), QStringLiteral("HH:mm"));
    // Sin huso guardado corre el del aparato, que es lo que el operador ve en
    // la pared. Es un valor por defecto razonable, no una adivinanza: el que
    // arma el evento está en el lugar del evento casi siempre.
    const QTimeZone z = e_.huso.empty() ? QTimeZone::systemTimeZone() : QTimeZone(QByteArray::fromStdString(e_.huso));
    if (!z.isValid()) return {};
    const QDateTime dt(d, h, z);
    if (!dt.isValid()) return {};
    return dt.toString(Qt::ISODate);
}

QString EventoAbierto::poner(const QString& carpeta, evento::Evento e) {
    e_ = std::move(e);
    abierto_ = carpeta;
    // LA LÍNEA MANDA: la del archivo o, en un evento de antes (v1), la migración del
    // guion de cues, con una copia del v1 en Respaldos/ y el archivo pasado a v2.
    bool migrado = false;
    QString aviso;
    if (e_.linea.is_object()) {
        linea_ = evento::linea::Linea::deJson(e_.linea);
    } else {
        linea_ = evento::linea::migrar(e_);
        migrado = !e_.bloques.empty();
    }
    proyeccion_ = evento::linea::proyectar(linea_);
    e_.bloques = proyeccion_.bloques;
    if (migrado) {
        const auto d = dir();
        std::error_code ec;
        std::filesystem::create_directories(d / "Respaldos", ec);
        if (!std::filesystem::exists(d / "Respaldos" / "evento-v1.json", ec))
            std::filesystem::copy_file(d / "evento.json", d / "Respaldos" / "evento-v1.json", ec);
        e_.linea = linea_.aJson();
        std::string err;
        aviso = store_.guardar(carpeta.toStdString(), e_, &err)
                    ? QCoreApplication::translate("EventoViewModel", "El guion pasó a la línea de tiempo: la copia de antes quedó en Respaldos/evento-v1.json")
                    : QCoreApplication::translate("EventoViewModel", "No se pudo guardar el guion pasado a la línea de tiempo: %1")
                          .arg(QString::fromStdString(err));
    }
    sucio_ = false;
    emit abiertoCambio();
    emit lineaCambio(false);
    emit sucioCambio();
    emit guionCambio();
    return aviso;
}

void EventoAbierto::cerrar() {
    e_ = {};
    linea_ = {};
    proyeccion_ = {};
    abierto_.clear();
    sucio_ = false;
    emit abiertoCambio();
    emit lineaCambio(false);
    emit sucioCambio();
    emit guionCambio();
}

std::string EventoAbierto::guardar() {
    if (abierto_.isEmpty()) return QCoreApplication::translate("EventoViewModel", "No hay ningún evento abierto.").toStdString();
    e_.linea = linea_.aJson(); // lo que manda; `bloques` va como su lectura
    std::string err;
    if (!store_.guardar(abierto_.toStdString(), e_, &err)) return err.empty() ? std::string("?") : err;
    ponerSucio(false);
    return {};
}

void EventoAbierto::renombrar(const QString& carpeta) {
    if (carpeta == abierto_) return;
    abierto_ = carpeta;
    emit abiertoCambio();
    emit guionCambio();
}

void EventoAbierto::editado() {
    // Lo editado en el guion de cues pasa a la línea; lo que no se tocó queda exacto.
    linea_ = evento::linea::reconciliar(linea_, e_.bloques);
    proyeccion_ = evento::linea::proyectar(linea_);
    e_.bloques = proyeccion_.bloques;
    emit lineaCambio(false);
    ponerSucio(true);
    emit guionCambio();
}

void EventoAbierto::ponerLinea(evento::linea::Linea l) {
    linea_ = std::move(l);
    proyeccion_ = evento::linea::proyectar(linea_);
    e_.bloques = proyeccion_.bloques;
    ponerSucio(true);
    emit guionCambio();
    emit lineaCambio(true);
}

bool EventoAbierto::seguirEscenas(const escenas::TablaEscenas& t) {
    if (abierto_.isEmpty() || !evento::linea::seguirEscenas(linea_, t)) return false;
    proyeccion_ = evento::linea::proyectar(linea_);
    e_.bloques = proyeccion_.bloques;
    emit lineaCambio(false);
    emit guionCambio();
    // Es lo mismo que había (otro nombre de la misma escena): sin cambios a mano, al disco ya.
    if (!sucio_) guardar();
    return true;
}

void EventoAbierto::marcarSucio() {
    ponerSucio(true);
    emit guionCambio();
}

void EventoAbierto::ponerSucio(bool si) {
    if (sucio_ == si) return;
    sucio_ = si;
    emit sucioCambio();
}

const evento::Cue* EventoAbierto::cue(int bloque, int cue) const {
    if (bloque < 0 || bloque >= static_cast<int>(e_.bloques.size())) return nullptr;
    const auto& cues = e_.bloques[static_cast<std::size_t>(bloque)].cues;
    if (cue < 0 || cue >= static_cast<int>(cues.size())) return nullptr;
    return &cues[static_cast<std::size_t>(cue)];
}

evento::Cue* EventoAbierto::cue(int bloque, int cue) {
    return const_cast<evento::Cue*>(static_cast<const EventoAbierto*>(this)->cue(bloque, cue));
}

} // namespace ba::app

#include "moc_EventoAbierto.cpp"
