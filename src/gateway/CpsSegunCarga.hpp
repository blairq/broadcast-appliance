// ============================================================================
// src/gateway/CpsSegunCarga.hpp — los cuadros por segundo de la capa baja, según la carga
//
// EL PROBLEMA (medido el 2026-11-03 en la PC de pruebas, RX 5700 XT, escalera por TURN):
// el motor de video de la placa («Video Codec 0»: decodifica y codifica H.264) va al 48 %
// con 40 invitados, al 82 % con 80 y al 96-99 % desde 120. Saturado, no avisa: las celdas
// de la grilla caen solas de 15 cps a 13/11 (120), 10/8 (160) y 7,5 (200), porque la
// recepción descarta lo que el decodificador no llega a tomar. La red y el navegador del
// invitado ya gastaron esos cuadros (se codificaron, viajaron) para nada.
//
// LA CAUSA DE FONDO: se le pide al navegador más de lo que la placa puede decodificar. Lo
// que se hace acá es pedirle MENOS antes de saturar: un controlador mira la carga del motor
// de video y, si pasa un umbral (sostenido), baja el `maxFramerate` que se le pide a la capa
// BAJA de los invitados (mensaje {type:"cps-baja", cps} a la página; comun/capas.js), primero
// a la mitad menos importante (las celdas más chicas y las tarjetas de la interfaz), después
// a todos, de escalón en escalón: 15 → 10 → 7,5. Cuando la carga baja (otro umbral, más
// abajo, sostenido más tiempo), sube de a un escalón. La capa ALTA (como mucho 4 en una
// grilla, el que está al aire) no se toca nunca.
//
// LA CARGA: la mayor de lo que se sepa (carga()):
//   - el motor de video de la placa, en % (Windows: contadores «GPU Engine», los motores
//     de códec de la placa principal sumando todos los procesos —la app también codifica
//     ahí—; CargaVideo.hpp);
//   - los cuadros que se pierden entre que llegan armados y el decodificador (portable: si
//     se pierde más que `perdidosTope` %, la carga cuenta como 100: ya está saturado);
//   - el tiempo de decodificar por cuadro contra un presupuesto (portable, de fábrica
//     apagado: en la placa es demora, no ocupación, y engaña).
// Sin ninguna medida, la carga es desconocida y cuenta como baja (no se degrada a ciegas).
//
// Sin hilos ni red: lo usa SignalingServer en su hilo de la carga y se prueba solo
// (test_cps_segun_carga).
// ============================================================================
#pragma once

#include <cstdint>
#include <deque>
#include <map>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace ba::gateway {

/// Lo medido en el último segundo (-1 = no se sabe).
struct MedidaCarga {
    double motorPct = -1;      ///< el motor de códec más cargado de la placa
    double decodificarMs = -1; ///< promedio por cuadro decodificado
    double perdidosPct = -1;   ///< de los cuadros armados, los que no llegaron a decodificarse
};

/// Los ajustes del controlador (gateway.cps.*; se releen en cada vuelta).
struct AjustesCps {
    bool activo = true;
    double subir = 85;              ///< con la carga en esto o más, sostenida, se baja un escalón
    double bajar = 70;              ///< con la carga en esto o menos, sostenida, se sube un escalón
    std::int64_t sostenerSubirMs = 2000;
    std::int64_t sostenerBajarMs = 10000;
    std::int64_t esperaMs = 3000;   ///< entre dos cambios (que los navegadores lleguen a reaccionar)
    double presupuestoMs = 0;       ///< decodificarMs → % (0 = no se usa)
    double perdidosTope = 5;        ///< % de perdidos que cuentan como saturado (0 = no se usa)
    std::vector<double> niveles{15, 10, 7.5}; ///< cps de cada nivel (el primero, sin bajar)
    int partes = 2;                 ///< cada nivel se baja de a partes (la menos importante primero)
};

/// Alguien cuya capa baja se puede frenar. `alto`: el alto en píxeles del lugar más grande
/// que ocupa en una grilla (0 = ninguno: solo la tarjeta de la interfaz). Los más chicos
/// son los menos importantes.
struct CandidatoCps {
    std::string id;
    int alto = 0;
};

class ControlCps {
   public:
    void ajustes(const AjustesCps& a);
    const AjustesCps& ajustes() const { return a_; }

    /// La carga de una medida, en % (-1 = no se sabe). Ver la cabecera.
    static double carga(const MedidaCarga& m, double presupuestoMs, double perdidosTope);

    /// Una vuelta con la carga `carga` (-1 = no se sabe: cuenta como baja) en `ahoraMs`.
    /// true si cambió el escalón.
    bool medir(double carga, const MedidaCarga& m, std::int64_t ahoraMs);

    /// 0 = nadie frenado; escalonMax() = todos en el último nivel.
    int escalon() const { return escalon_; }
    int escalonMax() const;

    /// Los cps que le toca a cada candidato con el escalón actual (orden: los de menor alto
    /// primero; a igual alto, por id, así no saltan de mitad entre vueltas).
    std::map<std::string, double> repartir(std::vector<CandidatoCps> candidatos) const;
    /// Lo mismo para un escalón dado (las pruebas).
    static std::map<std::string, double> repartir(std::vector<CandidatoCps> candidatos, int escalon,
                                                  const std::vector<double>& niveles, int partes);

    /// {activo, escalon, escalon_max, carga, medida:{motor_pct, decodificar_ms, perdidos_pct},
    ///  umbrales:{subir, bajar}, niveles, decisiones:[{ms, de, a, carga, motivo}]}.
    nlohmann::json estado() const;

   private:
    void cambiar(int a, double carga, const char* motivo, std::int64_t ahoraMs);

    AjustesCps a_;
    int escalon_ = 0;
    double carga_ = -1;
    MedidaCarga medida_;
    std::int64_t altaDesde_ = 0; ///< desde cuándo está por encima de `subir` (0 = no)
    std::int64_t bajaDesde_ = 0; ///< desde cuándo está por debajo de `bajar` (0 = no)
    std::int64_t ultimoCambio_ = 0;
    struct Decision {
        std::int64_t ms = 0;
        int de = 0, a = 0;
        double carga = -1;
        std::string motivo;
    };
    std::deque<Decision> decisiones_; ///< las últimas (para gateway_estado)
};

} // namespace ba::gateway
