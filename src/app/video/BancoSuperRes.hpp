// ============================================================================
// src/app/video/BancoSuperRes.hpp — el banco de pruebas de superresolución
//
// «¿Qué superresolución puede usar ESTA máquina?» Se arma la lista con lo que
// hay (FSR, Apple, los shaders y cada red instalada, una vez por acelerador:
// Neural Engine y GPU en la Mac; en Windows y Linux, los proveedores de ONNX
// Runtime que haya y Vulkan) y se mide cada uno a 540p, 720p, 1080p y al
// tamaño de cada cámara (motor/BancoSr: lo que tarda de verdad por cuadro).
//
// El veredicto sale de los cuadros del lienzo (a 30 fps, 33 ms por cuadro):
//     en_vivo   entra en el 85 % del cuadro (y cuántas a la vez en ese acelerador)
//     reducido  corre, pero a menos cuadros (hasta 2,5 cuadros por cuadro)
//     diferido  solo para lo que no es en vivo (grabaciones, repeticiones)
//     no_corre  no se pudo (falta algo, error, ni un cuadro)
//
// Queda en Config/superres-banco.json (con la máquina y la fecha). El
// programador de superresolución lo usa para saber cuánto va a costar una red
// antes de prenderla (antes suponía 35 ms); la página y el MCP lo muestran.
//
// Mide con la app andando: lo que esté corriendo (cámaras, composición) le
// quita placa. Por eso no arranca con una transmisión o grabación al aire salvo
// que se fuerce, y avisa.
// ============================================================================
#pragma once

#include <QObject>
#include <QVariantMap>

#include <atomic>
#include <filesystem>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

namespace ba::app::video {

class SuperRes;

class BancoSuperRes : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantMap estado READ estadoUi NOTIFY cambio)
    Q_PROPERTY(QVariantMap resultado READ resultadoUi NOTIFY cambio)
   public:
    using json = nlohmann::json;

    struct Entorno {
        std::function<bool()> alAire;                                  ///< transmitiendo o grabando
        std::function<std::pair<int, int>()> fps;                      ///< los del lienzo
        std::function<std::vector<std::pair<int, int>>()> tamanosCamaras; ///< de las entradas
    };

    BancoSuperRes(std::filesystem::path dataRoot, SuperRes& sr, Entorno en, QObject* parent = nullptr);
    ~BancoSuperRes() override;

    /// Lo que se mediría: [{id, nombre, metodo, acelerador, elemento, propiedades, solo_chico}].
    json candidatos() const;

    /// Arranca (en otro hilo). opciones: {resoluciones: ["1280x720",…] (vacío = las de
    /// fábrica y las de las cámaras), metodos: [ids de plugin] (vacío = todos),
    /// segundos (por prueba, 4), forzar (con algo al aire)}. "" o el error.
    std::string correr(const json& opciones);
    Q_INVOKABLE QString correrUi(const QVariantMap& opciones);
    Q_INVOKABLE void cancelar();

    /// {corriendo, hechas, total, actual, eta_s, parciales: […]}.
    json estado() const;
    /// Lo último medido (el archivo), o {} si nunca se midió.
    json resultado() const;
    QVariantMap estadoUi() const;
    QVariantMap resultadoUi() const;

    /// Para el programador: lo que tarda `plugin` con entradas de ancho×alto según
    /// lo medido (el mejor acelerador; el tamaño medido más parecido, escalado por
    /// los píxeles). 0 si no hay medida.
    double estimarMs(const std::string& plugin, int ancho, int alto) const;

    /// El veredicto de un tiempo, con los cuadros del lienzo.
    static std::string veredicto(double ms, double cuadroMs);

   signals:
    void cambio();

   private:
    void trabajar(json candidatos, std::vector<std::pair<int, int>> tamanos, double segundos, int fpsN, int fpsD);

    std::filesystem::path archivo_;
    SuperRes& sr_;
    Entorno en_;
    mutable std::mutex mu_;
    json estado_ = json::object();
    json resultado_ = json::object();
    std::thread hilo_;
    std::atomic<bool> cancelar_{false};
};

} // namespace ba::app::video
