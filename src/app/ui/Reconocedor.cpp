// ============================================================================
// src/app/ui/Reconocedor.cpp — ver Reconocedor.hpp
// ============================================================================
#include "ui/Reconocedor.hpp"

// Sin los modelos vendoreados (`make deps-rostros`) este archivo no existe:
// no hay a quién sacarle el trabajo del hilo de la interfaz.
#if BA_HAVE_ROSTROS

#include <utility>

#include <QPointer>
#include <QThread>

namespace ba::app {

struct Reconocedor::Impl {
    QThread hilo;
    /// Vive en `hilo`. No hace nada por sí mismo: es a quién se le mandan las
    /// tareas para que corran allá (invokeMethod necesita un destino).
    QObject* alla = nullptr;

    /// SOLO SE TOCA DESDE `hilo`. Esa es toda la sincronización que hay.
    ba::rostros::Motor motor;
    bool listo = false;
    QString error;

    /// Cuántos pedidos hay dando vueltas. Solo se toca desde la interfaz.
    int enVuelo = 0;
};

Reconocedor::Reconocedor(QObject* parent) : QObject(parent), d_(std::make_unique<Impl>()) {
    d_->alla = new QObject;
    d_->alla->moveToThread(&d_->hilo);
    d_->hilo.start();
}

Reconocedor::~Reconocedor() {
    // Primero se para el hilo y se lo espera: así no queda ninguna tarea a
    // medio correr tocando el motor mientras se destruye. Las respuestas que
    // ya estén en la cola de la interfaz las cubre el QPointer de `pedir`.
    d_->hilo.quit();
    d_->hilo.wait();
    delete d_->alla;
}

bool Reconocedor::abrir(const std::string& carpetaModelos) {
    // Bloquea a propósito: es una sola vez, al construir la página, y el resto
    // del sistema necesita saber ya si el reconocimiento está disponible o no.
    QMetaObject::invokeMethod(
        d_->alla,
        [this, carpetaModelos]() {
            d_->listo = d_->motor.abrir(carpetaModelos);
            if (!d_->listo) d_->error = QString::fromStdString(d_->motor.error());
        },
        Qt::BlockingQueuedConnection);
    return d_->listo;
}

bool Reconocedor::listo() const { return d_->listo; }
QString Reconocedor::error() const { return d_->error; }

void Reconocedor::pedir(std::vector<std::uint8_t> bgra, int ancho, int alto, bool descartable,
                        Respuesta luego) {
    if (descartable && d_->enVuelo > 0) return; // ver el encabezado: no se encola
    ++d_->enVuelo;
    QPointer<Reconocedor> yo = this;
    QMetaObject::invokeMethod(
        d_->alla,
        [this, yo, bgra = std::move(bgra), ancho, alto, luego = std::move(luego)]() mutable {
            std::vector<Visto> vistos;
            QString error;
            if (!d_->listo) {
                error = d_->error;
            } else {
                for (const auto& c : d_->motor.detectar(bgra.data(), ancho, alto)) {
                    Visto v;
                    v.cara = c;
                    v.vector = d_->motor.describir(bgra.data(), ancho, alto, c);
                    vistos.push_back(std::move(v));
                }
                if (!d_->motor.error().empty()) error = QString::fromStdString(d_->motor.error());
            }
            Reconocedor* destino = yo.data();
            if (destino == nullptr) return; // se fue mientras tanto
            QMetaObject::invokeMethod(
                destino,
                [yo, luego = std::move(luego), vistos = std::move(vistos), error]() mutable {
                    if (yo.isNull()) return;
                    --yo->d_->enVuelo;
                    luego(std::move(vistos), error);
                },
                Qt::QueuedConnection);
        },
        Qt::QueuedConnection);
}

} // namespace ba::app

#endif // BA_HAVE_ROSTROS
