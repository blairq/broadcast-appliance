// ============================================================================
// src/gateway/rtc/LoteTrabajo.hpp — la frontera #4 (red → trabajo de libwebrtc) en lote
//
// PLAN-GATEWAY-FRAGMENTOS.md, F2 «la frontera #4». En libwebrtc cada paquete RTP que llega al
// hilo de red pasa al de trabajo con un PostTask, y cada PostTask llama a WakeUp() del socket
// server del hilo de trabajo: si dormía, una despertada (y un cambio de contexto) por paquete.
// Con 40 invitados en la .177, ~13,5 mil cambios de contexto/s en `ba-rtc-work`.
//
// Lo que se hace acá, sin tocar libwebrtc: el hilo que MANDA (el de red, un `Remitente`) no
// despierta al de trabajo (un `Destino`) en el acto, sino que lo anota y lo despierta UNA vez
// al terminar lo que está haciendo (la tarea en curso, o la vuelta de E/S de su socket
// server), y como mucho cada `loteNs` mientras siga ocupado; antes de dormirse, siempre. Los
// PostTask de esa tanda ya están en la cola del de trabajo (libwebrtc encola antes de llamar a
// WakeUp): el que despierta los atiende todos juntos, en el orden en que llegaron.
//
//  - Si el de trabajo está despierto (atendiendo otra cosa), anotar no le cambia nada: mira su
//    cola antes de dormirse.
//  - Desde cualquier otro hilo (señalización, colas, el reloj del audio, el cierre) el aviso
//    es en el acto, como siempre. También si es «urgente» (un BlockingCall: el que llama queda
//    esperando y no puede esperar al lote; ver HilosLote.hpp).
//  - El propio hilo de trabajo avisándose a sí mismo: nada (está corriendo; mira su cola y
//    sus diferidas antes de dormirse).
//  - Las tareas diferidas: el de trabajo duerme con el plazo de la próxima (lo calcula
//    rtc::Thread, en ms, igual que antes); anotar no lo acorta ni lo alarga.
//
// La latencia agregada es la «espera» de la frontera (desde la primera anotada de la tanda
// hasta la despertada): se ve en `gateway_estado` → fronteras → `rtc.red-trabajo*`.
//
// Esto no depende de libwebrtc (lo prueba test_lote_trabajo); el enganche con rtc::Thread y
// los socket servers está en HilosLote.{hpp,cpp}.
// ============================================================================
#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

namespace ba::diag {
class Frontera;
}

namespace ba::gateway::lote {

/// `gateway.redYTrabajo`.
enum class Modo {
    Separados, ///< como venía libwebrtc: cada PostTask de red → trabajo despierta en el acto
    Lote,      ///< el de red despierta al de trabajo una vez por tanda (de fábrica)
    Juntos,    ///< red y trabajo son el MISMO hilo en cada grupo: la frontera no existe
};
const char* nombre(Modo m);
/// "lote" | "juntos" | "separados" (cualquier otra cosa: Lote).
Modo modoDeTexto(std::string_view s);
Modo modoDeLosAjustes();
/// `gateway.loteTrabajoUs` en ns (0..5000 µs).
std::int64_t loteNsDeLosAjustes();

class Remitente;

/// El hilo que recibe (el de trabajo): su espera y quién lo despierta. Lo comparten (por
/// shared_ptr) el socket server del hilo y los remitentes que lo anotaron.
class Destino : public std::enable_shared_from_this<Destino> {
   public:
    /// `frontera` puede ser nullptr (sin contar).
    explicit Destino(ba::diag::Frontera* frontera = nullptr);
    Destino(const Destino&) = delete;
    Destino& operator=(const Destino&) = delete;

    /// El hilo dueño (el que llama a esperar): marcarlo una vez, en ese hilo, al empezar.
    void soyElDueno();
    bool esElDueno() const;

    /// Solo el dueño: duerme hasta que lo despierten o pasen `maxNs` (< 0: sin plazo). Si ya
    /// lo habían despertado (aviso pendiente), vuelve en el acto.
    void esperar(std::int64_t maxNs);

    /// Lo que hace WakeUp() del socket server, desde cualquier hilo (ver arriba).
    void avisar();

    /// Despertar en el acto. true si estaba durmiendo (una despertada de verdad).
    bool despertar();

    /// Contadores (para gateway_estado).
    struct Cuentas {
        std::uint64_t avisos = 0;       ///< WakeUp() de cualquiera (menos el dueño)
        std::uint64_t deRed = 0;        ///< de ellos, desde un remitente (la frontera #4)
        std::uint64_t anotados = 0;     ///< de ellos, anotados para el lote
        std::uint64_t urgentes = 0;     ///< desde un remitente pero urgentes (BlockingCall)
        std::uint64_t despertadas = 0;  ///< veces que de verdad se despertó a un dormido
        std::uint64_t dormidoNs = 0;    ///< tiempo total durmiendo
        std::uint64_t despiertoNs = 0;  ///< tiempo total despierto (lo que se tomó trabajando)
    };
    Cuentas cuentas() const;
    nlohmann::json estado() const;

    /// Para las pruebas: está durmiendo ahora.
    bool durmiendo() const;

   private:
    friend class Remitente;
    void contarAnotados(std::uint64_t n, std::uint64_t esperaNs, bool desperto);

    ba::diag::Frontera* const frontera_;
    mutable std::mutex m_;
    std::condition_variable cv_;
    bool pendiente_ = false; ///< bajo m_
    bool durmiendo_ = false; ///< bajo m_
    std::atomic<std::uint64_t> avisos_{0}, deRed_{0}, anotados_{0}, urgentes_{0}, despertadas_{0}, dormidoNs_{0},
        despiertoNs_{0};
    std::uint64_t ultimoDespertarNs_ = 0; ///< solo el dueño
};

/// El hilo que manda (el de red). Uno por hilo; se instala en su hilo (`instalar`) y desde
/// ahí cada `Destino::avisar` que haga ese hilo pasa por él.
class Remitente {
   public:
    /// `loteNs`: cuánto puede esperar la primera anotada mientras este hilo siga ocupado
    /// (0: se suelta al terminar cada tarea o vuelta). `enLote` = false: no anota nada,
    /// despierta en el acto (solo cuenta, para comparar con `separados`).
    explicit Remitente(bool enLote, std::int64_t loteNs);
    ~Remitente();
    Remitente(const Remitente&) = delete;
    Remitente& operator=(const Remitente&) = delete;

    /// En el hilo que manda: desde acá, sus avisos pasan por este remitente. `alPrimero` (o
    /// nullptr) se llama cuando la tanda pasa de vacía a tener algo (el hilo de red lo usa para
    /// salir de su espera de E/S al terminar la vuelta).
    void instalar(void (*alPrimero)(void*) = nullptr, void* ctx = nullptr);
    /// En el mismo hilo, al terminar: suelta lo anotado y deja de estar instalado.
    void desinstalar();
    /// El del hilo actual (nullptr si no hay).
    static Remitente* delHilo();

    bool enLote() const { return enLote_; }
    std::int64_t loteNs() const { return loteNs_; }

    /// Anota a `d` para despertarlo al soltar.
    void anotar(Destino& d);
    /// Despierta a todos los anotados (antes de dormirse, antes de bloquearse esperando a
    /// otro, al cerrar).
    void soltar();
    /// Despierta a todos si la primera anotada ya esperó `loteNs` (al terminar cada tarea o
    /// vuelta de E/S).
    void soltarSiVencio();
    /// Hay algo anotado.
    bool hayAnotados() const { return !anotados_.empty(); }

    /// Urgente: mientras viva, los avisos de ESTE hilo no se anotan (BlockingCall).
    class Urgente {
       public:
        Urgente();
        ~Urgente();
        Urgente(const Urgente&) = delete;
        Urgente& operator=(const Urgente&) = delete;

       private:
        bool antes_;
    };
    static bool urgente();

   private:
    struct Anotado {
        std::shared_ptr<Destino> d;
        std::uint64_t n = 0;
        std::uint64_t desdeNs = 0; ///< cuándo se anotó el primero de este destino
    };
    const bool enLote_;
    const std::int64_t loteNs_;
    std::vector<Anotado> anotados_; ///< solo el hilo dueño; casi siempre 1 a 6
    std::uint64_t primeraNs_ = 0;
    void (*alPrimero_)(void*) = nullptr;
    void* ctx_ = nullptr;
};

} // namespace ba::gateway::lote
