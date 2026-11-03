// SPDX-License-Identifier: GPL-2.0-or-later
//
// CuadrosWeb: el video de las vistas del tablero web (/control/app/). Cada VideoItem del
// navegador pide su clave (la misma que en la app: «program», «armada», una fuente, un
// invitado…) con el ancho al que se dibuja y cuántos cuadros por segundo quiere; acá se
// toma el último cuadro del servicio de cuadros (el MISMO que usan las vistas de la app,
// ProveedorCuadros), se achica, se comprime en JPEG en otro hilo y sale por el control web
// como {type:"replica", op:"cuadro", clave, s, t, w, h, jpg}.
//
// Por qué cuadros JPEG y no WebRTC (docs/local/tablero-web-latencia.md tiene las medidas):
//   - es la misma escena de QML: el video queda DENTRO de la vista (las tarjetas de vidrio
//     lo difuminan, los menús van encima), igual que en la app; un <video> encima del
//     lienzo tapa los menús y no se puede difuminar;
//   - no hay colchón (jitter buffer) ni reordenamiento: lo que llega se muestra; en la red
//     local queda por debajo de WebRTC;
//   - sirve para CUALQUIER clave (fuentes, invitados, la armada) sin pedirle al motor un
//     canal de escena por cada una;
//   - cada cuadro comprimido se comparte entre los aparatos que piden lo mismo.
// A cambio cuesta más ancho de banda que H.264 (un monitor de 640 px a 15 cps, ~2-4 Mbit/s).
//
// `t` es la hora (ms desde 1970, reloj de esta máquina) en que el cuadro llegó a la app: el
// tablero web mide con eso cuánto tarda en verse (en la misma máquina, exacto).
// ============================================================================
#pragma once

#include <QImage>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QTimer>

#include <nlohmann/json.hpp>

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace ba::app {
class ProveedorCuadros;
}

namespace ba::app::control {

class CuadrosWeb : public QObject {
    Q_OBJECT
   public:
    using json = nlohmann::json;
    explicit CuadrosWeb(ProveedorCuadros* fuente, QObject* parent = nullptr);
    ~CuadrosWeb() override;
    /// Mandarle `msg` a esa conexión (por el gateway).
    std::function<void(const std::string& conexion, const json& msg)> enviar;
    /// Lo que pide esa conexión ([{clave, ancho, fps}]; [] = nada).
    void pedir(const std::string& conexion, const json& claves);
    /// Calidad del JPEG (1-100).
    static constexpr int kCalidad = 72;

    /// Lo que costó (para medir): cuadros comprimidos, bytes y microsegundos de compresión.
    struct Cuentas {
        std::uint64_t cuadros = 0, bytes = 0, usComprimir = 0, enviados = 0;
    };
    const Cuentas& cuentas() const { return cuentas_; }

    /// Achica y comprime (lo que hace el hilo; público para las pruebas).
    static QByteArray comprimir(const QImage& img, int ancho, int calidad, int* w, int* h);

   private:
    struct Pedido {
        std::string clave;
        int ancho = 640, fps = 15;
        std::uint64_t enviado = 0; ///< el serial del último cuadro que se le mandó
        std::int64_t ultimoMs = 0;
    };
    struct Fuente {
        std::uint64_t serial = 0;
        QImage img;
        std::int64_t t = 0; ///< ms desde 1970 en que llegó
    };
    struct Trabajo; ///< una compresión en curso (clave, ancho, serial)
    void vuelta();
    void listo(const std::string& clave, int ancho, std::uint64_t serial, std::int64_t t, const QByteArray& jpg, int w, int h,
               std::int64_t us);

    QPointer<ProveedorCuadros> fuente_;
    std::map<std::string, std::vector<Pedido>> pedidos_;
    std::map<std::string, Fuente> fuentes_;
    std::set<std::tuple<std::string, int, std::uint64_t>> enCurso_;
    QTimer reloj_;
    Cuentas cuentas_;
    std::shared_ptr<bool> vivo_ = std::make_shared<bool>(true);
};

} // namespace ba::app::control
