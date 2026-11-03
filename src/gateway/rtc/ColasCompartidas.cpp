// ============================================================================
// src/gateway/rtc/ColasCompartidas.cpp — ver ColasCompartidas.hpp
//
// Candados: el de la cola (Cola::mu_) y el del grupo (Grupo::mu_). Nunca se toma el de una
// cola con el del grupo tomado; al revés sí (programar una cola desde su Post). Las tareas
// corren y se destruyen sin ningún candado tomado.
// ============================================================================
#include "ColasCompartidas.hpp"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <fstream>
#include <map>
#include <mutex>
#include <queue>
#include <set>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "ajustes/Ajustes.hpp"
#include "diag/Frontera.hpp"
#include "diag/Registro.hpp"
#include "platform/Hilo.hpp"
#include "platform/Nucleos.hpp"

#if defined(__APPLE__)
#include <sys/sysctl.h>
#elif defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#if BA_GATEWAY_HAVE_WEBRTC
#include "api/task_queue/task_queue_base.h"
#include "api/task_queue/task_queue_factory.h"
#endif

namespace ba::gateway::colas {

// ---------------------------------------------------------------------------- topología

namespace {

#if defined(__linux__)
/// «0-5,12-17» → {0..5, 12..17}.
std::set<int> listaCpus(const std::string& s) {
    std::set<int> r;
    std::stringstream ss(s);
    std::string parte;
    while (std::getline(ss, parte, ',')) {
        const auto guion = parte.find('-');
        try {
            if (guion == std::string::npos) {
                if (!parte.empty()) r.insert(std::stoi(parte));
            } else {
                const int a = std::stoi(parte.substr(0, guion)), b = std::stoi(parte.substr(guion + 1));
                for (int i = a; i <= b && i - a < 4096; ++i) r.insert(i);
            }
        } catch (...) {
        }
    }
    return r;
}
std::string leerLinea(const std::string& ruta) {
    std::ifstream f(ruta);
    std::string l;
    std::getline(f, l);
    return l;
}
#endif

} // namespace

int nucleosDeUnBloque() {
#if defined(__APPLE__)
    // Apple M: los de rendimiento comparten la L2 de a grupos (4 en la M1 Pro).
    int n = 0;
    size_t t = sizeof(n);
    if (sysctlbyname("hw.perflevel0.cpusperl2", &n, &t, nullptr, 0) == 0 && n > 0) return n;
    t = sizeof(n);
    if (sysctlbyname("hw.perflevel0.physicalcpu", &n, &t, nullptr, 0) == 0 && n > 0) return n;
    return 0;
#elif defined(_WIN32)
    DWORD largo = 0;
    GetLogicalProcessorInformationEx(RelationAll, nullptr, &largo);
    if (largo == 0) return 0;
    std::vector<unsigned char> buf(largo);
    auto* base = reinterpret_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>(buf.data());
    if (!GetLogicalProcessorInformationEx(RelationAll, base, &largo)) return 0;
    // La L3 del procesador 0 (grupo 0, bit 0) y los núcleos (no hilos SMT) que caen en ella.
    KAFFINITY l3 = 0;
    for (DWORD o = 0; o < largo;) {
        auto* i = reinterpret_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>(buf.data() + o);
        if (i->Relationship == RelationCache && i->Cache.Level == 3 && i->Cache.GroupMask.Group == 0 &&
            (i->Cache.GroupMask.Mask & 1) != 0)
            l3 = i->Cache.GroupMask.Mask;
        o += i->Size;
    }
    int nucleos = 0;
    for (DWORD o = 0; o < largo;) {
        auto* i = reinterpret_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>(buf.data() + o);
        if (i->Relationship == RelationProcessorCore && i->Processor.GroupCount > 0 && i->Processor.GroupMask[0].Group == 0 &&
            (l3 == 0 || (i->Processor.GroupMask[0].Mask & l3) != 0))
            ++nucleos;
        o += i->Size;
    }
    return nucleos;
#elif defined(__linux__)
    const std::string base = "/sys/devices/system/cpu/";
    std::set<int> bloque;
    for (int i = 3; i >= 2 && bloque.empty(); --i) // la L3; si no hay, la L2
        for (int idx = 0; idx < 8; ++idx) {
            const std::string d = base + "cpu0/cache/index" + std::to_string(idx) + "/";
            if (leerLinea(d + "level") == std::to_string(i)) {
                bloque = listaCpus(leerLinea(d + "shared_cpu_list"));
                break;
            }
        }
    if (bloque.empty()) return 0;
    // Un núcleo físico por grupo de hermanos SMT: el primero de su lista.
    int nucleos = 0;
    for (int c : bloque) {
        const std::set<int> hermanos = listaCpus(leerLinea(base + "cpu" + std::to_string(c) + "/topology/thread_siblings_list"));
        if (hermanos.empty() || *hermanos.begin() == c) ++nucleos;
    }
    return nucleos;
#else
    return 0;
#endif
}

Opciones resolver(const Opciones& o) {
    Opciones r = o;
    if (r.hilosPesadas <= 0) {
        int n = nucleosDeUnBloque();
        if (n <= 0) n = static_cast<int>(std::max(2u, std::thread::hardware_concurrency() / 2));
        r.hilosPesadas = std::clamp(n, 2, 6);
    }
    r.hilosPesadas = std::clamp(r.hilosPesadas, 1, 64);
    if (r.hilosLivianas <= 0) r.hilosLivianas = r.hilosPesadas == 1 ? 1 : 2;
    r.hilosLivianas = std::clamp(r.hilosLivianas, 1, 64);
    return r;
}

bool esPesada(std::string_view nombre) {
    // Las de decodificar video: una por stream de video que llega (VideoReceiveStream2);
    // cada tarea decodifica un cuadro (ms por software o con el MFT de Windows).
    return nombre == "DecodingQueue";
}

#if BA_GATEWAY_HAVE_WEBRTC

namespace {

using Reloj = std::chrono::steady_clock;

class Grupo;

// ---------------------------------------------------------------------------- Cola

class Cola final : public webrtc::TaskQueueBase, public std::enable_shared_from_this<Cola> {
   public:
    Cola(std::shared_ptr<Grupo> g, ba::diag::Frontera& f) : grupo_(std::move(g)), f_(f) {}
    ~Cola() override = default;

    /// El dueño es libwebrtc (vía TaskQueueDeleter → Delete); la referencia propia se suelta ahí.
    void adoptar(std::shared_ptr<Cola> yo) { duenio_ = std::move(yo); }

    void Delete() override;

    /// Mueve a la cola las diferidas vencidas. true si hay que programarla.
    bool vencer(Reloj::time_point ahora);
    /// Un turno en el hilo del grupo: corre tareas seguidas. true si quedan (sigue programada).
    bool turno();

   protected:
    void PostTaskImpl(absl::AnyInvocable<void() &&> task, const PostTaskTraits& traits,
                      const webrtc::Location& location) override;
    void PostDelayedTaskImpl(absl::AnyInvocable<void() &&> task, webrtc::TimeDelta delay,
                             const PostDelayedTaskTraits& traits, const webrtc::Location& location) override;

   private:
    struct Tarea {
        absl::AnyInvocable<void() &&> f;
        std::uint64_t publicadaNs = 0;
    };
    enum class Estado { Libre, Programada, Corriendo };

    std::shared_ptr<Grupo> grupo_;
    ba::diag::Frontera& f_;
    std::shared_ptr<Cola> duenio_;

    std::mutex mu_;
    std::condition_variable libre_; ///< para Delete: espera que termine la tarea en curso
    std::deque<Tarea> tareas_;
    std::multimap<Reloj::time_point, absl::AnyInvocable<void() &&>> diferidas_;
    Estado estado_ = Estado::Libre;
    std::thread::id hilo_; ///< el que corre una tarea ahora (Corriendo)
    bool borrada_ = false;
};

// ---------------------------------------------------------------------------- Vigía

/// Donde duerme el hilo que vigila el próximo vencimiento: hasta un plazo o hasta que lo
/// avisen. Se usa con el candado del grupo tomado (lo suelta mientras duerme).
class Vigia {
   public:
#if defined(_WIN32)
    Vigia() {
        evento_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        // Windows 10 1803+: sin redondear al tic del sistema (15,6 ms); ver platform/Ritmo.hpp.
        temporizador_ = CreateWaitableTimerExW(nullptr, nullptr, 0x00000002 /* CREATE_WAITABLE_TIMER_HIGH_RESOLUTION */,
                                               TIMER_ALL_ACCESS);
    }
    ~Vigia() {
        if (temporizador_ != nullptr) CloseHandle(temporizador_);
        if (evento_ != nullptr) CloseHandle(evento_);
    }
    void dormirHasta(std::unique_lock<std::mutex>& l, Reloj::time_point plazo) {
        const auto falta = plazo - Reloj::now();
        if (falta <= Reloj::duration::zero()) return;
        if (evento_ == nullptr) { // sin eventos: como en POSIX (redondea al tic)
            cv_.wait_until(l, plazo);
            return;
        }
        l.unlock();
        LARGE_INTEGER vence;
        vence.QuadPart = -std::max<LONGLONG>(1, std::chrono::duration_cast<std::chrono::nanoseconds>(falta).count() / 100);
        if (temporizador_ != nullptr && SetWaitableTimer(temporizador_, &vence, 0, nullptr, nullptr, FALSE)) {
            HANDLE h[2] = {evento_, temporizador_};
            WaitForMultipleObjects(2, h, FALSE, INFINITE);
            CancelWaitableTimer(temporizador_);
        } else {
            const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(falta).count() + 1;
            WaitForSingleObject(evento_, static_cast<DWORD>(std::min<long long>(ms, 0x7fffffff)));
        }
        l.lock();
    }
    void avisar() {
        if (evento_ != nullptr)
            SetEvent(evento_);
        else
            cv_.notify_one();
    }

   private:
    HANDLE evento_ = nullptr;
    HANDLE temporizador_ = nullptr;
    std::condition_variable cv_;
#else
    void dormirHasta(std::unique_lock<std::mutex>& l, Reloj::time_point plazo) { cv_.wait_until(l, plazo); }
    void avisar() { cv_.notify_one(); }

   private:
    std::condition_variable cv_;
#endif
};

// ---------------------------------------------------------------------------- Grupo

class Grupo final : public std::enable_shared_from_this<Grupo> {
   public:
    Grupo(std::string nombre, int hilos) : nombre_(std::move(nombre)), hilos_(hilos),
          f_(ba::diag::frontera("colas." + nombre_, 0, "colas de libwebrtc → hilos «" + nombre_ + "» (turnos)")) {}

    void arrancar() {
        auto yo = shared_from_this();
        for (int i = 0; i < hilos_; ++i)
            threads_.emplace_back([yo, i]() {
                ba::platform::nombrarHilo("ba-colas-" + yo->nombre_ + "/" + std::to_string(i + 1));
                ba::platform::nucleos::alGrupoGateway(); // F4: el bloque de caché del gateway
                yo->bucle();
            });
    }

    /// Termina lo que haya listo y espera a los hilos (o los suelta si lo llama uno de ellos).
    void parar() {
        {
            std::lock_guard<std::mutex> l(mu_);
            parar_ = true;
            cvTrabajo_.notify_all();
            vigia_.avisar();
        }
        for (auto& t : threads_) {
            if (t.get_id() == std::this_thread::get_id())
                t.detach();
            else if (t.joinable())
                t.join();
        }
        threads_.clear();
    }

    /// La cola tiene tareas y estaba libre: a la lista de listas.
    void programar(std::shared_ptr<Cola> c) {
        std::lock_guard<std::mutex> l(mu_);
        listas_.push_back(std::move(c));
        avisarLocked();
    }

    /// Una diferida de `c` vence en `vence`.
    void alarma(const std::shared_ptr<Cola>& c, Reloj::time_point vence) {
        std::lock_guard<std::mutex> l(mu_);
        const bool primera = alarmas_.empty() || vence < alarmas_.top().vence;
        alarmas_.push(Alarma{vence, ++secuencia_, c});
        if (!primera) return;
        if (vigiaDuerme_) {
            if (vence < vigiaHasta_) vigia_.avisar();
        } else if (!hayVigia_ && dormidos_ > senales_) {
            // Nadie vigila y hay hilos durmiendo: que uno se haga cargo.
            ++senales_;
            cvTrabajo_.notify_one();
        }
    }

   private:
    struct Alarma {
        Reloj::time_point vence;
        std::uint64_t secuencia;
        std::weak_ptr<Cola> cola;
        bool operator<(const Alarma& o) const { // la más próxima arriba
            return vence != o.vence ? vence > o.vence : secuencia > o.secuencia;
        }
    };

    /// Despierta a un hilo si hay más colas listas que hilos ya avisados (con mu_ tomado).
    void avisarLocked() {
        const int pendientes = static_cast<int>(listas_.size());
        if (pendientes == 0) return;
        if (dormidos_ > senales_ && senales_ < pendientes) {
            ++senales_;
            cvTrabajo_.notify_one();
            f_.despertada();
        } else if (vigiaDuerme_ && !vigiaAvisado_ && dormidos_ == senales_) {
            // Los demás están ocupados o ya avisados: que el vigía ayude.
            vigiaAvisado_ = true;
            vigia_.avisar();
            f_.despertada();
        }
    }

    void bucle() {
        std::unique_lock<std::mutex> l(mu_);
        std::vector<std::shared_ptr<Cola>> vencidas;
        while (true) {
            // 1) Las diferidas vencidas pasan a su cola (sin el candado del grupo).
            if (!alarmas_.empty()) {
                const auto ahora = Reloj::now();
                while (!alarmas_.empty() && alarmas_.top().vence <= ahora) {
                    if (auto c = alarmas_.top().cola.lock()) vencidas.push_back(std::move(c));
                    alarmas_.pop();
                }
                if (!vencidas.empty()) {
                    l.unlock();
                    for (auto& c : vencidas)
                        if (c->vencer(ahora)) programar(c);
                    vencidas.clear();
                    l.lock();
                    continue;
                }
            }
            // 2) Una cola lista: un turno.
            if (!listas_.empty()) {
                std::shared_ptr<Cola> c = std::move(listas_.front());
                listas_.pop_front();
                avisarLocked(); // si quedan más, que otro las tome
                if (!hayVigia_ && !alarmas_.empty() && dormidos_ > senales_) {
                    // Este hilo (quizás el vigía, que se fue a trabajar) no vigila mientras
                    // corre el turno: que uno de los que duermen se haga cargo.
                    ++senales_;
                    cvTrabajo_.notify_one();
                }
                l.unlock();
                const std::uint64_t t0 = ba::diag::relojNs();
                const bool quedan = c->turno();
                f_.pase(0, ba::diag::relojNs() - t0);
                if (quedan) {
                    l.lock();
                    listas_.push_back(std::move(c)); // al fondo: las otras también tienen su turno
                    continue;
                }
                c.reset(); // puede destruir la cola: sin el candado
                l.lock();
                continue;
            }
            if (parar_) return;
            // 3) Dormir: vigilando la próxima alarma si nadie la vigila, o hasta que haya trabajo.
            if (!hayVigia_ && !alarmas_.empty()) {
                hayVigia_ = vigiaDuerme_ = true;
                vigiaAvisado_ = false;
                vigiaHasta_ = alarmas_.top().vence;
                vigia_.dormirHasta(l, vigiaHasta_);
                hayVigia_ = vigiaDuerme_ = vigiaAvisado_ = false;
                continue;
            }
            ++dormidos_;
            cvTrabajo_.wait(l);
            --dormidos_;
            if (senales_ > 0) --senales_;
        }
    }

    const std::string nombre_;
    const int hilos_;
    ba::diag::Frontera& f_;
    std::vector<std::thread> threads_;

    std::mutex mu_;
    std::condition_variable cvTrabajo_;
    Vigia vigia_;
    std::deque<std::shared_ptr<Cola>> listas_;
    std::priority_queue<Alarma> alarmas_;
    std::uint64_t secuencia_ = 0;
    int dormidos_ = 0; ///< hilos esperando trabajo (sin el vigía)
    int senales_ = 0;  ///< de esos, los ya avisados que todavía no se despertaron
    bool hayVigia_ = false, vigiaDuerme_ = false, vigiaAvisado_ = false;
    Reloj::time_point vigiaHasta_{};
    bool parar_ = false;
};

// ---------------------------------------------------------------------------- Cola (cuerpo)

constexpr int kTareasPorTurno = 16;
constexpr std::uint64_t kTurnoMaxNs = 2'000'000;

void Cola::PostTaskImpl(absl::AnyInvocable<void() &&> task, const PostTaskTraits&, const webrtc::Location&) {
    const std::uint64_t ahora = ba::diag::relojNs();
    {
        std::lock_guard<std::mutex> l(mu_);
        if (borrada_) return; // la tarea se destruye al salir, sin candado
        tareas_.push_back(Tarea{std::move(task), ahora});
        if (estado_ != Estado::Libre) return; // ya programada o corriendo: nadie más se despierta
        estado_ = Estado::Programada;
    }
    grupo_->programar(shared_from_this());
}

void Cola::PostDelayedTaskImpl(absl::AnyInvocable<void() &&> task, webrtc::TimeDelta delay, const PostDelayedTaskTraits& traits,
                               const webrtc::Location& location) {
    if (delay <= webrtc::TimeDelta::Zero()) {
        PostTaskImpl(std::move(task), PostTaskTraits{}, location);
        return;
    }
    (void)traits; // la precisión la da el vigía: alta siempre (en Windows, temporizador de alta resolución)
    // Una «infinita» (o de días) se guarda igual, hasta Delete: con un tope que no desborde.
    const std::int64_t us = delay.IsPlusInfinity() ? INT64_C(1'000'000'000'000) : std::min<std::int64_t>(delay.us(), INT64_C(1'000'000'000'000));
    const auto vence = Reloj::now() + std::chrono::microseconds(us);
    {
        std::lock_guard<std::mutex> l(mu_);
        if (borrada_) return; // la tarea se destruye al salir
        diferidas_.emplace(vence, std::move(task));
    }
    grupo_->alarma(shared_from_this(), vence);
}

bool Cola::vencer(Reloj::time_point ahora) {
    const std::uint64_t ns = ba::diag::relojNs();
    std::lock_guard<std::mutex> l(mu_);
    if (borrada_) return false;
    auto fin = diferidas_.upper_bound(ahora);
    if (fin == diferidas_.begin()) return false;
    for (auto it = diferidas_.begin(); it != fin; ++it) tareas_.push_back(Tarea{std::move(it->second), ns});
    diferidas_.erase(diferidas_.begin(), fin);
    if (estado_ != Estado::Libre) return false;
    estado_ = Estado::Programada;
    return true;
}

bool Cola::turno() {
    CurrentTaskQueueSetter actual(this);
    const std::uint64_t t0 = ba::diag::relojNs();
    for (int n = 0;; ++n) {
        Tarea t;
        {
            std::lock_guard<std::mutex> l(mu_);
            if (borrada_ || tareas_.empty()) {
                estado_ = Estado::Libre;
                hilo_ = {};
                libre_.notify_all();
                return false;
            }
            if (n >= kTareasPorTurno || ba::diag::relojNs() - t0 > kTurnoMaxNs) {
                estado_ = Estado::Programada; // le toca a otra; vuelve al fondo de la lista
                hilo_ = {};
                libre_.notify_all();
                return true;
            }
            t = std::move(tareas_.front());
            tareas_.pop_front();
            estado_ = Estado::Corriendo;
            hilo_ = std::this_thread::get_id();
        }
        const std::uint64_t a = ba::diag::relojNs();
        std::move(t.f)();
        t.f = nullptr; // se destruye todavía «en» la cola (como las de libwebrtc)
        const std::uint64_t b = ba::diag::relojNs();
        f_.pase(0, b - a + 1, a > t.publicadaNs ? a - t.publicadaNs : 1);
    }
}

void Cola::Delete() {
    std::deque<Tarea> tirar;
    std::multimap<Reloj::time_point, absl::AnyInvocable<void() &&>> tirarDiferidas;
    {
        std::unique_lock<std::mutex> l(mu_);
        borrada_ = true;
        tirar.swap(tareas_);
        tirarDiferidas.swap(diferidas_);
        // Desde otro hilo: que termine la tarea en curso. Desde una tarea de esta misma cola,
        // no se puede esperar (es esta): al volver, el turno ve `borrada_` y no corre más.
        if (hilo_ != std::this_thread::get_id()) libre_.wait(l, [this]() { return estado_ != Estado::Corriendo; });
    }
    tirar.clear();
    tirarDiferidas.clear();
    std::shared_ptr<Cola> yo = std::move(duenio_); // puede ser la última referencia: nada después
}

// ---------------------------------------------------------------------------- Fábrica

class Fabrica final : public webrtc::TaskQueueFactory {
   public:
    explicit Fabrica(const Opciones& o) {
        const Opciones r = resolver(o);
        pesadas_ = std::make_shared<Grupo>("dec", r.hilosPesadas);
        livianas_ = std::make_shared<Grupo>("rtc", r.hilosLivianas);
        pesadas_->arrancar();
        livianas_->arrancar();
        ba::diag::registro().linea(ba::diag::Nivel::Info, "gateway", 0,
                                   "colas de libwebrtc compartidas: " + std::to_string(r.hilosPesadas) +
                                       " hilos para decodificar video y " + std::to_string(r.hilosLivianas) +
                                       " para el resto (gateway.hilosColas)");
    }
    ~Fabrica() override {
        pesadas_->parar();
        livianas_->parar();
    }

    std::unique_ptr<webrtc::TaskQueueBase, webrtc::TaskQueueDeleter> CreateTaskQueue(absl::string_view nombre,
                                                                                      Priority) const override {
        const std::string_view n(nombre.data(), nombre.size());
        const bool pesada = esPesada(n);
        ba::diag::Frontera& f = ba::diag::frontera("cola." + std::string(n), pesada ? 5 : 0,
                                                   "cola de libwebrtc «" + std::string(n) + "» (tareas)");
        auto c = std::make_shared<Cola>(pesada ? pesadas_ : livianas_, f);
        c->adoptar(c);
        return std::unique_ptr<webrtc::TaskQueueBase, webrtc::TaskQueueDeleter>(c.get());
    }

   private:
    std::shared_ptr<Grupo> pesadas_;
    std::shared_ptr<Grupo> livianas_;
};

} // namespace

std::unique_ptr<webrtc::TaskQueueFactory> crear(const Opciones& o) { return std::make_unique<Fabrica>(o); }

std::unique_ptr<webrtc::TaskQueueFactory> crearDeLosAjustes() {
    Opciones o;
    o.hilosPesadas = std::clamp(ba::ajustes::entero("gateway.hilosColas", 0), 0, 64);
    return crear(o);
}

#endif // sin libwebrtc no hay colas que compartir: crear() no existe

} // namespace ba::gateway::colas
