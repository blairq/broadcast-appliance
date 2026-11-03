// src/app/emision/VentanillaMotor.cpp — ver el .hpp

#include "emision/VentanillaMotor.hpp"

#include "diag/Metricas.hpp"
#include "diag/Registro.hpp"
#include "emision/TurnoMotor.hpp"
#include "i18n/I18n.hpp"

#include <QCoreApplication>
#include <QThread>

#include <algorithm>

namespace ba::app {

VentanillaMotor::VentanillaMotor(Dueno d, QObject* parent)
    : QObject(parent), d_(std::move(d)), nucleo_(std::make_shared<Nucleo>()), vista_(std::make_shared<const FotoMotor>()) {
    nucleo_->alTerminarOrden = d_.alTerminarOrden;
}

VentanillaMotor::~VentanillaMotor() = default;

void VentanillaMotor::responder(std::function<void()> fn) {
    QMetaObject::invokeMethod(QCoreApplication::instance(), std::move(fn), Qt::QueuedConnection);
}

void VentanillaMotor::Nucleo::terminar(const char* que, std::chrono::steady_clock::time_point desde) {
    // Lo que la orden cambió, a la foto ANTES de la respuesta: la interfaz recibe primero la
    // foto (el aviso ya está en su cola o se encola ahora) y después la respuesta.
    if (alTerminarOrden) alTerminarOrden();
    ++hechas;
    const double ms =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - desde).count();
    ba::diag::Metricas::proceso()
        .histograma("ba_ventanilla_orden_ms",
                    QT_TRANSLATE_NOOP("metricas", "Lo que tardó una orden de la interfaz al motor (de pedirla a hecha), ms"),
                    {{"orden", que != nullptr ? que : "?"}})
        .observar(ms);
}

void VentanillaMotor::publicar(std::shared_ptr<const FotoMotor> foto, unsigned partes) {
    if (!foto) return;
    {
        const std::lock_guard<std::mutex> l(mu_);
        pendiente_ = std::move(foto);
        partesPendientes_ |= partes;
        publicadaEn_ = std::chrono::steady_clock::now();
    }
    ++publicadas_;
    // Un solo aviso en camino: con la interfaz trabada se juntan (la última, con todas las
    // partes que cambiaron mientras tanto).
    if (!avisoEnCamino_.exchange(true)) QMetaObject::invokeMethod(this, [this]() { alLlegar(); }, Qt::QueuedConnection);
}

void VentanillaMotor::recibirAhora() { alLlegar(); }

void VentanillaMotor::alLlegar() {
    // Primero se baja la marca: lo que se publique desde acá encola otro aviso.
    avisoEnCamino_ = false;
    unsigned partes = 0;
    {
        const std::lock_guard<std::mutex> l(mu_);
        if (!pendiente_) return;
        vista_ = pendiente_;
        partes = partesPendientes_;
        partesPendientes_ = 0;
    }
    ++recibidas_;
    if (partes == 0) return;
    const auto desde = std::chrono::steady_clock::now();
    // Los oyentes, de a uno, con el turno suelto entre uno y otro (ver escuchar). Por índice:
    // uno puede sumar otro mientras se le avisa (se le avisa también, al final).
    bool hayMuertos = false;
    for (std::size_t i = 0; i < oyentes_.size(); ++i) {
        const QPointer<QObject> quien = oyentes_[i].quien;
        if (!quien) {
            hayMuertos = true;
            continue;
        }
        const std::function<void(unsigned)> fn = oyentes_[i].fn; // por si se borra mientras corre
        // Uno que vive en otro hilo (un modelo adoptado por el dueño): encolado en el suyo,
        // como haría la señal.
        if (quien->thread() != QThread::currentThread()) {
            QMetaObject::invokeMethod(
                quien.data(), [fn, partes]() { fn(partes); }, Qt::QueuedConnection);
            continue;
        }
        const auto t0 = std::chrono::steady_clock::now();
        if (fn) fn(partes);
        TurnoMotor::soltarVuelta();
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
        if (ms >= kOyenteLentoMs) anotarOyenteLento(quien ? quien->metaObject()->className() : "?", partes, ms);
    }
    if (hayMuertos)
        oyentes_.erase(std::remove_if(oyentes_.begin(), oyentes_.end(), [](const Oyente& o) { return !o.quien; }), oyentes_.end());
    // Los conectados a la señal (los que todavía no escuchan con escuchar).
    emit cambio(partes);
    TurnoMotor::soltarVuelta();
    const auto total = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - desde).count();
    if (total > llegadaMaxMs_.load()) llegadaMaxMs_ = total;
    ba::diag::Metricas::proceso()
        .histograma("ba_ventanilla_llegada_ms",
                    QT_TRANSLATE_NOOP("metricas", "Lo que tardó la interfaz con una foto del motor (todos los que la escuchan), ms"))
        .observar(static_cast<double>(total));
}

void VentanillaMotor::escuchar(QObject* quien, std::function<void(unsigned)> fn) {
    if (quien == nullptr || !fn) return;
    oyentes_.push_back({QPointer<QObject>(quien), std::move(fn)});
}

void VentanillaMotor::anotarOyenteLento(const char* clase, unsigned partes, std::int64_t ms) {
    const std::string c = clase != nullptr ? clase : "?";
    ba::diag::Metricas::proceso()
        .histograma("ba_ventanilla_oyente_ms",
                    QT_TRANSLATE_NOOP("metricas", "Lo que tardó un oyente de la foto del motor (solo lo lento), ms, por clase"),
                    {{"quien", c}})
        .observar(static_cast<double>(ms));
    // Una línea por clase la primera vez que pasa de un segundo: qué hace la interfaz cuando
    // llega una foto y tarda (con qué partes).
    if (ms < 1000 || !lentosAvisados_.insert(c).second) return;
    ba::diag::registro().linea(ba::diag::Nivel::Aviso, "ventanilla", 0,
                               ba::i18n::llenar(ba::i18n::traducir("ventanilla", "%1 tardó %2 ms con una foto del motor (partes %3)"),
                                                {c, std::to_string(ms), std::to_string(partes)}));
}

std::int64_t VentanillaMotor::edadMs() const {
    const std::lock_guard<std::mutex> l(mu_);
    if (!pendiente_) return -1;
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - publicadaEn_).count();
}

nlohmann::json VentanillaMotor::estado() const {
    return {{"pedidas", pedidas_.load()},
            {"hechas", nucleo_->hechas.load()},
            {"publicadas", publicadas_.load()},
            {"recibidas", recibidas_.load()},
            {"edad_ms", edadMs()},
            {"llegada_max_ms", llegadaMaxMs_.load()}};
}

} // namespace ba::app
