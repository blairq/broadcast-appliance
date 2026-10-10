// ============================================================================
// src/app/ui/FuenteQrPintor.hpp — pinta las fuentes «Código QR» (ver emision/FuenteQr)
//
// Cada segundo mira las fuentes ba_qr; si lo que tienen que mostrar cambió (otro
// QR, la clave del Público que se renovó, otro tamaño), rasteriza el MISMO SVG
// que exporta «Códigos QR» y se lo manda al motor.
// ============================================================================
#pragma once

#include <QObject>
#include <QTimer>

#include <cstdint>
#include <map>

namespace ba::app {

class QrViewModel;

class FuenteQrPintor : public QObject {
    Q_OBJECT
   public:
    explicit FuenteQrPintor(QrViewModel& qr, QObject* parent = nullptr);
    ~FuenteQrPintor() override;

   private:
    void tic();
    QrViewModel& qr_;
    QTimer timer_;
    std::map<std::uint64_t, std::size_t> firmas_; ///< lo último que se pintó, por fuente
};

} // namespace ba::app
