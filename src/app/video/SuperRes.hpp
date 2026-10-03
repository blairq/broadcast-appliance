// src/app/video/SuperRes.hpp — superresolución de las entradas de red (Studio.superres)
//
// Cuatro motores, todos como elementos de GStreamer en la entrada (ba_gst_fuente):
//   - apple: la de VideoToolbox (macOS 26), ~3 ms. (obs-module/ba_gst_superres_mac.mm)
//   - fsr:   FSR 1 de AMD en OpenGL, cualquier GPU, sin red. (ba_gst_fsr.cpp)
//   - onnx:  una red en ONNX (ba_gst_onnxsr.cpp): la «red chica» que se entrena
//            acá mirando la cámara, SPAN o Real-ESRGAN (convertidos acá).
//
// El LABORATORIO (assets/superres/laboratorio.py) corre con el Python del
// programa; sus bibliotecas (PyTorch, spandrel, onnx: ~800 MB) se bajan UNA vez
// a Datos/Herramientas/superres-libs. Los modelos van a Datos/Modelos/superres
// (cada .onnx con su .json: qué es, de dónde, cuánto mejora). Un trabajo a la
// vez; su avance va a `trabajo`.
#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

#include <nlohmann/json.hpp>

#include <filesystem>
#include <functional>
#include <memory>

namespace ba::app::plugins {
class Hijo;
}

namespace ba::app::video {

class VideoGrafos;

class SuperRes : public QObject {
    Q_OBJECT
    /// [{archivo, nombre, arquitectura, escala, canales, psnr_red, psnr_bicubico, licencia, fecha, tamano_mb}]
    Q_PROPERTY(QVariantList modelos READ modelosQml NOTIFY cambio)
    /// Qué motores hay en este sistema (apple, fsr, onnx).
    Q_PROPERTY(QStringList motores READ motores NOTIFY cambio)
    /// Si el laboratorio ya tiene sus bibliotecas.
    Q_PROPERTY(bool laboratorioListo READ laboratorioListo NOTIFY cambio)
    /// El trabajo en curso o el último: {que, entrada, corriendo, pct, texto, error, psnr_red, psnr_bicubico, modelo}
    Q_PROPERTY(QVariantMap trabajo READ trabajo NOTIFY trabajoCambio)

   public:
    using json = nlohmann::json;

    SuperRes(std::filesystem::path dataRoot, VideoGrafos* video, QObject* parent = nullptr);
    ~SuperRes() override;

    json modelos() const;
    QStringList motores() const;
    bool laboratorioListo() const;
    QVariantMap trabajo() const { return trabajo_; }
    std::filesystem::path carpetaModelos() const;

    /// Baja las bibliotecas del laboratorio. "" o el error (si ya hay un trabajo).
    std::string prepararLaboratorio();
    /// Convierte un modelo de terceros: "span" o "esrgan". Prepara el laboratorio si hace falta.
    std::string convertir(const std::string& cual);
    /// Entrena la red chica mirando la entrada `entrada` (graba `segundos` y entrena
    /// `minutos`); si `aplicar`, al terminar la pone en esa entrada. Sigue desde la
    /// red anterior de esa entrada si la hay.
    std::string entrenar(const std::string& entrada, double minutos, int segundos, bool aplicar);
    void cancelar();
    /// Pone un motor (y modelo, por nombre de archivo o ruta) en una entrada.
    std::string aplicar(const std::string& entrada, const std::string& motor, const std::string& modelo);

    // --- Interfaz ---------------------------------------------------------------------
    QVariantList modelosQml() const;
    Q_INVOKABLE QString prepararUi() { return q(prepararLaboratorio()); }
    Q_INVOKABLE QString convertirUi(const QString& cual) { return q(convertir(cual.toStdString())); }
    Q_INVOKABLE QString entrenarUi(const QString& entrada, double minutos, bool aplicar) {
        return q(entrenar(entrada.toStdString(), minutos, 60, aplicar));
    }
    Q_INVOKABLE void cancelarUi() { cancelar(); }
    Q_INVOKABLE QString aplicarUi(const QString& entrada, const QString& motor, const QString& modelo) {
        return q(aplicar(entrada.toStdString(), motor.toStdString(), modelo.toStdString()));
    }
    Q_INVOKABLE QString borrarModelo(const QString& archivo);

   signals:
    void cambio();
    void trabajoCambio();

   private:
    static QString q(const std::string& s) { return QString::fromStdString(s); }
    std::filesystem::path python() const;
    std::filesystem::path libs() const;
    /// Corre `args` con el Python del programa; cada línea JSON va a `trabajo`; `fin` al terminar.
    std::string correr(const QString& que, const QString& entrada, const QStringList& args, std::function<void(bool ok)> fin);
    void linea(const QByteArray& l);

    std::filesystem::path root_;
    VideoGrafos* video_;
    QVariantMap trabajo_;
    std::unique_ptr<plugins::Hijo> hijo_;
    QByteArray pendiente_;
    std::function<void(bool)> fin_;
    std::function<void()> despues_; ///< lo que sigue cuando el laboratorio quedó listo
};

} // namespace ba::app::video
