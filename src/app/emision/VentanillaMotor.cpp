// src/app/emision/VentanillaMotor.cpp — ver el .hpp

#include "emision/VentanillaMotor.hpp"

#include "diag/Metricas.hpp"

#include <QCoreApplication>

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
    if (partes != 0) emit cambio(partes);
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
            {"edad_ms", edadMs()}};
}

} // namespace ba::app
