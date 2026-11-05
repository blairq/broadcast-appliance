// ============================================================================
// src/app/emision/NivelesMotor.hpp — el canal de niveles: los vúmetros sin tocar el motor (F3-03)
//
// docs/arquitectura/05-motor-del-plano.md. Lo que se mueve a cada rato (los picos de cada
// entrada, de los buses y del programa de cada salida) no va en la foto del motor
// (FotoMotor): cambia en cada vuelta y la compararía siempre distinta. Va por este canal,
// que la interfaz lee a su ritmo (los vúmetros a 10 Hz, el Mezclador a 30 Hz) sin el turno
// ni candados del motor.
//
//   - LOS NIVELES de cada entrada y bus los publica el hilo del motor (Motor::niveles, cada
//     ~40 ms; Motor::alPublicarNiveles): acá queda la última foto. Si el motor se rearma,
//     el nuevo publica en el mismo canal; apagado, queda vacío.
//   - DESPERTAR AL SONAR: la interfaz no relee el canal con un reloj mientras todo está en
//     silencio. Quien lo lee (ui/RelojNiveles) arma `despertarAlSonar` y se duerme; lo
//     despierta, UNA vez, el hilo que publique algo que suene (el del motor o el dueño).
//   - EL PICO DEL PROGRAMA de cada salida lo suma el dueño en cada vuelta de su reloj
//     (AppRuntime::ticMotor, BroadcastEngine::programPeaksDb): el mayor desde que la
//     interfaz lo tomó (tomarPrograma, el vúmetro de arriba con su caída) y el de la
//     última vuelta (programa: el MCP, que no se lo roba a la vista).
//   - EL LOUDNESS de cada salida (M/S/I, true peak, LRA) también va acá, no en la foto del
//     motor: cambia siempre, y en la foto la hacía distinta en cada vuelta. Lo pone el dueño
//     a un ritmo acotado (medir, kLoudnessCadaVueltas) y la vista lo lee a su ritmo, solo
//     si alguien lo muestra (audio/Mezclador).
//   - SIN EL TURNO DEL MOTOR: el dueño mide (el pico del programa, el loudness) en cada
//     vuelta de su reloj aunque la interfaz tenga el turno (PLAN-MOTOR-SIN-ESPERAS: el
//     reloj del motor no espera a nadie). Para eso guarda los MEDIDORES de cada salida
//     (ponerMedidores, con el turno, cuando cambian las salidas: lo que mide cada una es
//     atómico o tiene su candado, MedidasSalida) y `medir` los lee sin él.
//
// Es de BroadcastEngine (vive lo que la app); la interfaz lo pide una vez
// (AppRuntime::niveles). DESDE CUALQUIER HILO.
// ============================================================================
#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

// Un nivel de la foto (motor/NivelAudio.hpp: id, pico y RMS por canal): solo lo nombra. Quien lee
// los niveles uno por uno (el Mezclador, los vúmetros) incluye NivelAudio.hpp; los demás que
// usan el canal (la mayoría: el pico del programa, el loudness) no dependen del motor.
namespace ba::motor {
struct NivelAudio;
} // namespace ba::motor

namespace ba::app {

class NivelesMotor {
   public:
    using Niveles = std::vector<motor::NivelAudio>;

    /// DESDE EL MOTOR: la foto nueva de los niveles (nullptr: el motor se fue).
    void ponerNiveles(std::shared_ptr<const Niveles> n);
    /// La última (nullptr si no hay motor). Se puede guardar: no cambia.
    std::shared_ptr<const Niveles> niveles() const;
    /// El pico (dBFS, el mayor de los dos canales) de lo que entrega la entrada `nombre`,
    /// después de su rack y antes de su volumen; -100 si no suena o no es de audio.
    float picoEntrada(const std::string& nombre) const;
    /// Lo mismo de un bus (`programa`, `pista2`, `matriz:<id>`…).
    float picoBus(const std::string& id) const;

    /// DESDE EL DUEÑO: cuál es la salida por omisión (su id también se pide como "").
    void setPorOmision(const std::string& id);
    /// DESDE EL DUEÑO: el pico del programa de `salida` en esta vuelta (dBFS, L y R).
    void sumarPrograma(const std::string& salida, float l, float r);
    /// El mayor desde la última vez que se lo tomó (y lo vuelve a -100). "" = la por omisión.
    std::pair<float, float> tomarPrograma(const std::string& salida);
    /// El de la última vuelta del dueño, sin tomarlo.
    std::pair<float, float> programa(const std::string& salida) const;
    /// Una salida que ya no está (deja de sumarse).
    void olvidarSalida(const std::string& salida);

    // --- Lo que mide el dueño sin el turno ------------------------------------------------
    /// Lo que se mide de una salida, desde cualquier hilo y sin el turno del motor (lo arma
    /// BroadcastEngine::medidores con las medidas compartidas de cada salida).
    struct Medidor {
        std::string salida;
        std::function<std::pair<float, float>()> pico; ///< el pico desde la última vez (dBFS) y lo pone en cero
        std::function<nlohmann::json()> loudness;      ///< LoudnessPrograma::json
    };
    /// DESDE EL DUEÑO, con el turno: los medidores de las salidas que hay ahora.
    void ponerMedidores(std::vector<Medidor> m);
    /// Cada cuántas vueltas del reloj del motor se publica el loudness (~4 por segundo).
    static constexpr int kLoudnessCadaVueltas = 3;
    /// DESDE EL DUEÑO, SIN el turno: suma el pico del programa de cada salida y, si
    /// `conLoudness`, publica el loudness de todas. Devuelve cuántas midió.
    int medir(bool conLoudness);
    /// El loudness de cada salida, {salida: LoudnessPrograma::json} (nunca nulo). Se puede
    /// guardar: no cambia.
    std::shared_ptr<const nlohmann::json> loudness() const;
    /// El de `salida` ("" = la por omisión); un objeto vacío si no hay.
    nlohmann::json loudnessDe(const std::string& salida) const;
    /// Cuántas veces se publicó el loudness (para el test y el diagnóstico).
    std::uint64_t medidas() const { return medidas_.load(); }

    /// Por debajo de esto (dBFS) es silencio: nada que mostrar en un vúmetro.
    static constexpr float kSilencioDb = -90.0f;
    /// Si algo suena: una entrada, un bus o el programa de alguna salida (su última vuelta).
    bool haySonido() const;
    /// `fn` se llama UNA vez cuando algo suene (desde el hilo que lo publica, el del motor o
    /// el dueño; o enseguida, desde este, si ya suena). Tiene que encolar y volver. nullptr
    /// = nadie; ponerlo espera al aviso en curso (después no se llama más).
    void despertarAlSonar(std::function<void()> fn);

   private:
    bool haySonidoSinCandado() const; ///< con mu_
    /// Si hay un despertador armado y algo suena, lo llama (sin mu_ tomado).
    void despertarSiSuena();
    std::string clave(const std::string& salida) const; ///< con mu_
    struct Programa {
        float acum[2] = {-100, -100}; ///< el mayor desde que se tomó
        float ultimo[2] = {-100, -100};
    };
    mutable std::mutex mu_;
    std::shared_ptr<const Niveles> niveles_;
    std::string porOmision_;
    std::map<std::string, Programa> programa_; ///< "" = la por omisión
    std::function<void()> despertar_;   ///< con mu_ (ver despertarAlSonar)
    std::atomic<bool> armado_{false};   ///< hay despertador: el que publica mira si suena
    std::mutex avisoMu_;                ///< tomado mientras se llama al despertador
    std::mutex medidoresMu_;            ///< el dueño los pone y los lee; por si cambia de hilo
    std::vector<Medidor> medidores_;    ///< con medidoresMu_
    std::shared_ptr<const nlohmann::json> loudness_ = std::make_shared<const nlohmann::json>(nlohmann::json::object()); ///< con mu_
    std::atomic<std::uint64_t> medidas_{0};
};

} // namespace ba::app
