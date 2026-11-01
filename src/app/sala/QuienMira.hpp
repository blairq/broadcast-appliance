// ============================================================================
// src/app/sala/QuienMira.hpp — quién mira el programa de cada salida (modelo)
//
// El retorno a los invitados: la vista que baja el programa entero en cada cuadro
// (BroadcastEngine::retornoEnUso) descansa cuando nadie lo mira hace 10 s. Quién mira lo
// dice el GATEWAY (cuántas sesiones reciben el programa: invitados, televisores, la consola
// de la presidencia…); los que están adentro de la sala, la sesión remota y las pantallas
// que muestran el programa quedan por si la sala todavía no lo informó. Cada salida que no
// es la por omisión, solo mientras el gateway dice que alguien la mira.
//
// Es el dueño de esa decisión: escucha los avisos de sus fuentes (escuchar), revisa al
// cambiar algo y le pide al motor SOLO cuando cambia. La gracia la vence un reloj de una
// vez (no se sondea).
// ============================================================================
#pragma once

#include <QObject>
#include <QTimer>

#include <chrono>
#include <functional>
#include <map>
#include <optional>
#include <string>

namespace ba::app::sala {

class QuienMira : public QObject {
    Q_OBJECT
   public:
    struct Fuentes {
        /// Alguien mira el programa de la por omisión.
        std::function<bool()> porOmision;
        /// Las demás salidas que hay, con si alguien las mira ({id: mira}).
        std::function<std::map<std::string, bool>()> salidas;
    };
    /// `pedir(salida, enUso)`: al motor ("" = la por omisión), solo cuando cambia.
    using Pedir = std::function<void(const std::string& salida, bool enUso)>;

    QuienMira(Fuentes fuentes, Pedir pedir, std::chrono::milliseconds gracia = std::chrono::seconds(10),
              QObject* parent = nullptr);

    /// Revisa cuando `emisor` emite `senal` (una de las fuentes cambió).
    template <class Emisor, class Senal>
    void escuchar(const Emisor* emisor, Senal senal) {
        connect(emisor, senal, this, &QuienMira::revisar);
    }

    /// Lo que se le pidió al motor para `salida` (nada si todavía no se le pidió).
    std::optional<bool> enUso(const std::string& salida) const;

   public slots:
    /// Relee las fuentes y le pide al motor lo que cambió.
    void revisar();

   private:
    struct Estado {
        std::optional<std::chrono::steady_clock::time_point> visto; ///< la última vez que alguien miraba
        std::optional<bool> pedido;                                 ///< lo último que se le pidió al motor
    };
    Fuentes fuentes_;
    Pedir pedir_;
    std::chrono::milliseconds gracia_;
    std::map<std::string, Estado> salidas_; ///< "" = la por omisión
    QTimer vence_; ///< la próxima gracia que vence (de una vez)
    void revisarUna(const std::string& id, bool mira, std::chrono::steady_clock::time_point ahora,
                    std::optional<std::chrono::milliseconds>& proxima);
};

} // namespace ba::app::sala
