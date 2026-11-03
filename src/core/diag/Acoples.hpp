// ============================================================================
// src/core/diag/Acoples.hpp — el detector de acoples, la parte DINÁMICA («modo sabotaje»)
//
// docs/arquitectura/detector-de-acoples.md. UN ACOPLE es un camino de la interfaz que no pasa
// por el modelo: la vista o su C++ le pregunta algo al motor, a la configuración, al gateway,
// al disco o a la red EN EL HILO DE LA INTERFAZ, o llama desde un binding a una Q_INVOKABLE
// que devuelve datos. Con la sala llena, cada uno es un lugar donde la interfaz se puede
// clavar; casi nunca tarda, así que no se ve.
//
// EL SABOTAJE: con la app en --test y `--detectar-acoples[=ms]` (o la herramienta MCP
// `diag_acoples`), cada paso por uno de esos caminos en el hilo de la interfaz ESPERA `ms`
// de más (300 de fábrica) y queda anotado con su lugar, el archivo:línea, lo que corría el
// bucle (VigiaBucle) y la pila de la primera vez. Así cualquier acople queda a la vista: la
// interfaz se traba exactamente ahí (el vigía de trabas, diag/VigiaTraba, saca las pilas si
// junta más de `diag.trabaInterfazMs`) y el informe dice cuántas veces y cuánto costó.
// Para no dejar la app inservible, lo que se duerme tiene un tope: kTopeVentanaMs en cada
// ventana de kVentanaMs (lo que pasa de ahí se anota igual, sin dormir).
//
// EL PUNTO DE ENGANCHE: BA_ACOPLE("clase", "lugar") al principio de un método (una guardia:
// también mide lo que de verdad duró la llamada) o BA_ACOPLE_EN(clase, lugar, donde) donde
// ya se tiene un std::source_location. Apagado cuesta una lectura atómica. Hoy están en:
// TurnoMotor (cada entrada directa de la interfaz al motor, la configuración, el gateway),
// el candado del motor, persist:: (el disco), HttpsClient (la red) y las Q_INVOKABLE que
// un binding llama. tests/test_acoples.py cuenta los ganchos por archivo: sacar uno falla.
//
// Clases: "motor" (entró directo al motor/configuración/gateway, con el turno), "candado"
// (el candado del motor), "disco", "red", "proceso", "bloqueante" (espera a otro hilo) e
// "invocable" (una Q_INVOKABLE que devuelve algo, llamada desde la QML).
//
// Sin Qt (es de ba_core): la app marca el hilo de la interfaz (marcarHiloInterfaz) y lo
// enciende (activar). Desde cualquier hilo: informe(), reiniciar(), activar().
// ============================================================================
#pragma once

#include <nlohmann/json.hpp>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <source_location>
#include <string>
#include <string_view>

namespace ba::diag::acoples {

/// Lo que se duerme de más por cada acople, si nadie dice otra cosa (ms).
constexpr int kDemoraDeFabricaMs = 300;
/// El tope de lo que se duerme: como mucho kTopeVentanaMs en cada kVentanaMs.
constexpr int kVentanaMs = 5000;
constexpr int kTopeVentanaMs = 3000;

namespace detalle {
/// La demora en ms (0 = apagado). Inline: el camino apagado es una lectura atómica.
inline std::atomic<int> demoraMs{0};
} // namespace detalle

/// ¿Está encendido el sabotaje? (Desde cualquier hilo.)
inline bool activo() { return detalle::demoraMs.load(std::memory_order_relaxed) > 0; }
/// Lo enciende con `ms` de demora por acople (0 lo apaga). Lo anotado queda (ver reiniciar).
void activar(int ms);
int demoraMs();

/// Marca el hilo que llama como el de la interfaz (GuiMode, una vez al arrancar).
void marcarHiloInterfaz();
bool enHiloInterfaz();

/// Un paso por un acople: si está encendido y es el hilo de la interfaz, lo anota (la primera
/// vez de cada lugar, con la pila y una línea en el registro) y duerme la demora (con el
/// tope). Devuelve true si lo anotó. `lugar`: «Clase::metodo» o lo que lo nombre.
bool tocar(const char* clase, std::string_view lugar, const std::source_location& donde = std::source_location::current());
/// Lo que de verdad duró una llamada anotada (lo suma la Guardia al salir).
void sumarReal(const char* clase, std::string_view lugar, const std::source_location& donde, std::int64_t us);

/// La guardia de BA_ACOPLE: toca al entrar y mide lo que dura hasta que sale.
class Guardia {
   public:
    Guardia(const char* clase, const char* lugar, std::source_location donde = std::source_location::current())
        : clase_(clase), lugar_(lugar), donde_(donde) {
        // Empieza a medir al volver de tocar: lo que se durmió a propósito no cuenta.
        if (activo() && tocar(clase_, lugar_, donde_)) desde_ = std::chrono::steady_clock::now();
    }
    ~Guardia() {
        if (desde_ != std::chrono::steady_clock::time_point{})
            sumarReal(clase_, lugar_, donde_,
                      std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - desde_)
                          .count());
    }
    Guardia(const Guardia&) = delete;
    Guardia& operator=(const Guardia&) = delete;

   private:
    const char* clase_;
    const char* lugar_;
    std::source_location donde_;
    std::chrono::steady_clock::time_point desde_{};
};

/// El informe: {activo, demora_ms, dormido_ms, sin_dormir (los que pasaron el tope), total
/// (pasos), lugares: [{clase, lugar, donde (archivo:línea), veces, demora_ms, real_ms,
/// real_max_ms, total_ms, corria, pila: [...]}] ordenados por total_ms (mayor primero)}.
nlohmann::json informe();
/// Borra lo anotado (no cambia si está encendido).
void reiniciar();

} // namespace ba::diag::acoples

#define BA_ACOPLE_CAT2(a, b) a##b
#define BA_ACOPLE_CAT(a, b) BA_ACOPLE_CAT2(a, b)
/// Un camino de la interfaz que no pasa por el modelo: con el sabotaje encendido, espera de
/// más y queda en el informe (ver arriba). `clase` y `lugar`: literales.
#define BA_ACOPLE(clase, lugar) \
    const ::ba::diag::acoples::Guardia BA_ACOPLE_CAT(ba_acople_, __LINE__)((clase), (lugar))
/// Lo mismo donde ya se tiene el lugar como texto y un std::source_location (sin medir).
#define BA_ACOPLE_EN(clase, lugar, donde)                                                     \
    do {                                                                                      \
        if (::ba::diag::acoples::activo()) ::ba::diag::acoples::tocar((clase), (lugar), (donde)); \
    } while (false)
