// src/app/emision/TurnoMotor.cpp — ver el .hpp

#include "emision/TurnoMotor.hpp"

#include "diag/Acoples.hpp"
#include "diag/Metricas.hpp"
#include "diag/Registro.hpp"
#include "i18n/I18nTexto.hpp"

#include <QtGlobal>
#if !defined(_WIN32)
#include <dlfcn.h>
#include <execinfo.h>
#endif

#include <chrono>
#include <cstdio>
#include <cstring>
#include <set>
#include <utility>
#include <vector>

namespace ba::app {

namespace {
thread_local const char* t_participa = nullptr; ///< el nombre del hilo, si participa
thread_local bool t_porVuelta = false;           ///< participa por vuelta (la interfaz)
thread_local const TurnoMotor* t_vuelta = nullptr; ///< el turno que tiene por la vuelta
thread_local std::int64_t t_vueltaDesdeUs = 0;      ///< desde cuándo lo tiene por la vuelta
thread_local const char* t_vueltaLugar = nullptr;   ///< dónde lo tomó (function_name)
thread_local const char* t_vueltaArchivo = nullptr;
thread_local unsigned t_vueltaLinea = 0;
/// Las directas ya contadas en este evento (archivo, línea): una vez por lugar y evento.
thread_local std::vector<std::pair<const char*, unsigned>> t_contadas;

std::int64_t ahoraUs() {
    return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

/// Lo que se esperó para entrar (solo cuando hubo que esperar: entrar libre no cuesta nada).
void anotarEspera(std::int64_t us, std::atomic<std::uint64_t>& esperas, std::atomic<std::int64_t>& maxUs) {
    ++esperas;
    std::int64_t m = maxUs.load();
    while (us > m && !maxUs.compare_exchange_weak(m, us)) {
    }
    ba::diag::Metricas::proceso()
        .histograma("ba_motor_turno_espera_ms",
                    QT_TRANSLATE_NOOP("metricas", "Lo que se esperó el turno del motor (la interfaz o el plano de control adentro), ms"),
                    {{"hilo", t_participa != nullptr ? t_participa : "otro"}})
        .observar(static_cast<double>(us) / 1000.0);
}

/// Una sola línea por lugar (para que el registro diga qué falta sin llenarse).
bool primeraVez(std::mutex& mu, std::set<std::string>& vistos, const std::string& lugar) {
    const std::lock_guard<std::mutex> l(mu);
    return vistos.insert(lugar).second;
}
} // namespace

std::string TurnoMotor::lugar(const std::source_location& l) {
    // «bool ba::app::BroadcastEngine::addScene(const std::string &)» → «BroadcastEngine::addScene».
    std::string f = l.function_name();
    if (const auto p = f.find('('); p != std::string::npos) f.resize(p);
    if (const auto e = f.rfind(' '); e != std::string::npos) f.erase(0, e + 1);
    for (const char* ns : {"ba::app::", "ba::"})
        if (f.rfind(ns, 0) == 0) f.erase(0, std::char_traits<char>::length(ns));
    return f.empty() ? std::string(l.file_name()) : f;
}

namespace {
std::atomic<bool> g_estricto{false};
/// Lo que espera el turno un hilo que no participa (ver la cabecera): poco, nunca trabado.
constexpr int kSinParticiparMs = 5;
} // namespace

void TurnoMotor::estricto(bool si) { g_estricto = si; }

TurnoMotor::Guarda::Guarda(const TurnoMotor& t, std::source_location donde) {
    if (t_participa == nullptr) {
        if (t.tomarHasta(kSinParticiparMs)) t_ = &t;
        t.anotarSinParticipar(donde);
        return;
    }
    if (t_porVuelta) {
        // LA INTERFAZ, POR LLAMADA: un método del motor con su Guarda devuelve valores, así
        // que el turno es suyo solo mientras dura el método (antes lo tenía hasta el final del
        // evento: un evento de la interfaz de 10 s que llamaba una vez al motor frenaba 10 s
        // el reloj del motor y la agenda). Se cuenta como directa (una vez por lugar y evento).
        t.contarDirecta(donde);
        t_ = &t;
        t.tomar(donde);
        return;
    }
    t_ = &t;
    t.tomar(donde);
}

void TurnoMotor::participar(const char* nombre) {
    soltarVuelta();
    t_porVuelta = false;
    t_participa = nombre != nullptr ? nombre : "?";
}

void TurnoMotor::participarPorVuelta(const char* nombre) {
    t_participa = nombre != nullptr ? nombre : "?";
    t_porVuelta = true;
}

bool TurnoMotor::participa() { return t_participa != nullptr; }

void TurnoMotor::soltarVuelta() {
    t_contadas.clear();
    if (t_vuelta == nullptr) return;
    const TurnoMotor* t = t_vuelta;
    t_vuelta = nullptr;
    t->anotarTenencia(ahoraUs() - t_vueltaDesdeUs);
    t->soltar();
}

void TurnoMotor::anotarTenencia(std::int64_t us) const {
    // Lo que la interfaz tuvo el turno por la vuelta: mientras tanto el reloj del motor
    // midió sin él (lo demás lo salteó) y la agenda reintentó. Lo largo, al registro una
    // vez por lugar: es lo que hay que pasar a la foto o a la ventanilla.
    // El lugar como lo cuenta `lugar` («Clase::metodo»).
    std::string donde = t_vueltaLugar != nullptr ? std::string(t_vueltaLugar) : std::string("?");
    if (const auto p = donde.find('('); p != std::string::npos) donde.resize(p);
    if (const auto e = donde.rfind(' '); e != std::string::npos) donde.erase(0, e + 1);
    for (const char* ns : {"ba::app::", "ba::"})
        if (donde.rfind(ns, 0) == 0) donde.erase(0, std::char_traits<char>::length(ns));
    {
        const std::lock_guard<std::mutex> g(directasMu_);
        if (us > tenenciaMaxUs_) {
            tenenciaMaxUs_ = us;
            tenenciaMaxLugar_ = donde;
        }
    }
    if (us < kTenenciaLargaUs) return;
    ++tenenciasLargas_;
    ba::diag::Metricas::proceso()
        .histograma("ba_motor_turno_interfaz_ms",
                    QT_TRANSLATE_NOOP("metricas", "Lo que la interfaz tuvo el turno del motor en un evento (solo lo largo), ms, por dónde lo tomó"),
                    {{"lugar", donde}})
        .observar(static_cast<double>(us) / 1000.0);
    static std::mutex mu;
    static std::set<std::string> vistos;
    if (us >= kTenenciaAvisoUs && primeraVez(mu, vistos, donde))
        ba::diag::registro().linea(
            ba::diag::Nivel::Aviso, "motor", 0,
            ba::i18n::llenar(ba::i18n::traducir("motor", "la interfaz tuvo el turno del motor %1 ms en un evento (lo tomó en %2, %3:%4): "
                                                         "mientras tanto la agenda esperó; pasarlo a la foto o a la ventanilla"),
                             {std::to_string(us / 1000), donde, std::string(t_vueltaArchivo != nullptr ? t_vueltaArchivo : "?"),
                              std::to_string(t_vueltaLinea)}));
}

void TurnoMotor::entrarDirecto(std::source_location donde) const {
    if (t_participa != nullptr && t_porVuelta) entrarPorVuelta(donde);
}

bool TurnoMotor::contarDirecta(std::source_location donde) const {
    if (t_participa == nullptr || !t_porVuelta) return false;
    // Una vez por lugar y evento (un bucle de la interfaz que llama 120 veces al motor no
    // llena las métricas).
    const std::pair<const char*, unsigned> clave{donde.file_name(), static_cast<unsigned>(donde.line())};
    for (const auto& c : t_contadas)
        if (c.second == clave.second && (c.first == clave.first || std::strcmp(c.first, clave.first) == 0)) return false;
    t_contadas.push_back(clave);
    anotarDirecta(donde);
    // El detector de acoples (diag/Acoples.hpp, solo pruebas): la interfaz entró directo, así
    // que espera de más ACÁ (antes de tomar el turno, para no frenar también al dueño) y queda
    // en el informe con el lugar y quién lo llamó. Una vez por lugar y evento, como lo de arriba.
    BA_ACOPLE_EN("motor", lugar(donde), donde);
    return true;
}

void TurnoMotor::entrarPorVuelta(const std::source_location& donde) const {
    if (t_vuelta == this) return; // ya lo tiene esta vuelta
    // Se cuenta (y el detector de acoples espera) antes de tomarlo.
    contarDirecta(donde);
    tomar(donde);
    t_vuelta = this;
    t_vueltaDesdeUs = ahoraUs();
    t_vueltaLugar = donde.function_name();
    t_vueltaArchivo = donde.file_name();
    t_vueltaLinea = static_cast<unsigned>(donde.line());
}

namespace {
/// Quién llamó (módulo+desplazamiento, para `atos`/`addr2line`): cuando el lugar es un
/// método del motor con su Guarda, el lugar no dice quién de la interfaz lo llamó.
std::string quienLlamo() {
#if defined(_WIN32)
    return {};
#else
    void* marcos[10];
    const int n = backtrace(marcos, 10);
    std::string out;
    for (int i = 3; i < n; ++i) { // sin este, anotarDirecta y entrarPorVuelta
        Dl_info di{};
        if (dladdr(marcos[i], &di) == 0 || di.dli_fname == nullptr) continue;
        const char* b = std::strrchr(di.dli_fname, '/');
        char hex[32];
        std::snprintf(hex, sizeof hex, "+0x%lx",
                      static_cast<unsigned long>(reinterpret_cast<std::uintptr_t>(marcos[i]) -
                                                 reinterpret_cast<std::uintptr_t>(di.dli_fbase)));
        out += (out.empty() ? "" : " < ") + std::string(b != nullptr ? b + 1 : di.dli_fname) + hex;
    }
    return out;
#endif
}
} // namespace

void TurnoMotor::anotarDirecta(const std::source_location& donde) const {
    const std::string l = lugar(donde);
    ++directasTotal_;
    bool nuevo = false;
    {
        const std::lock_guard<std::mutex> g(directasMu_);
        nuevo = ++directas_[l] == 1;
    }
    ba::diag::Metricas::proceso()
        .contador("ba_motor_directas_total",
                  QT_TRANSLATE_NOOP("metricas", "Veces que la interfaz entró directo al motor (sin la ventanilla), por lugar"),
                  {{"lugar", l}})
        .sumar();
    // La primera vez de cada lugar, al registro: lo que falta migrar a la ventanilla (F3-03).
    if (nuevo)
        ba::diag::registro().linea(
            ba::diag::Nivel::Info, "motor", 0,
            ba::i18n::llenar(ba::i18n::traducir("motor", "la interfaz entró directo al motor: %1 (%2:%3)"),
                             {l, std::string(donde.file_name()), std::to_string(donde.line())}) +
                " [" + quienLlamo() + "]");
}

void TurnoMotor::anotarSinParticipar(const std::source_location& donde) const {
    const std::string l = lugar(donde);
    bool nuevo = false;
    {
        const std::lock_guard<std::mutex> g(directasMu_);
        nuevo = ++sinParticipar_[l] == 1;
    }
    ba::diag::Metricas::proceso()
        .contador("ba_motor_sin_participar_total",
                  QT_TRANSLATE_NOOP("metricas", "Veces que un hilo que no participa del turno entró al motor, por lugar"),
                  {{"lugar", l}})
        .sumar();
    if (nuevo)
        ba::diag::registro().linea(
            g_estricto ? ba::diag::Nivel::Error : ba::diag::Nivel::Aviso, "motor", 0,
            ba::i18n::llenar(ba::i18n::traducir("motor", "un hilo que no participa del turno entró al motor: %1 (%2:%3)"),
                             {l, std::string(donde.file_name()), std::to_string(donde.line())}) +
                " [" + quienLlamo() + "]");
}

void TurnoMotor::anotarEntrada() const {
    if (profundidad_++ == 0) {
        dueno_.store(std::this_thread::get_id());
        quien_.store(t_participa != nullptr ? t_participa : "otro");
    }
}

void TurnoMotor::tomar(std::source_location donde) const {
    if (!mu_.try_lock()) {
        const std::int64_t desde = ahoraUs();
        mu_.lock();
        const std::int64_t us = ahoraUs() - desde;
        anotarEspera(us, esperas_, esperaMaxUs_);
        // El dueño no espera a la interfaz (toma con tope): si igual esperó, que se sepa dónde.
        static std::mutex mu;
        static std::set<std::string> vistos;
        if (us > 50000 && t_participa != nullptr && !t_porVuelta && primeraVez(mu, vistos, lugar(donde)))
            ba::diag::registro().linea(ba::diag::Nivel::Aviso, "motor", 0,
                                       ba::i18n::llenar(ba::i18n::traducir("motor", "%1 esperó el turno del motor %2 ms en %3"),
                                                        {std::string(t_participa), std::to_string(us / 1000), lugar(donde)}));
    }
    anotarEntrada();
}

bool TurnoMotor::tomarHasta(int ms) const {
    if (!mu_.try_lock()) {
        const std::int64_t desde = ahoraUs();
        if (!mu_.try_lock_for(std::chrono::milliseconds(ms > 0 ? ms : 0))) return false;
        anotarEspera(ahoraUs() - desde, esperas_, esperaMaxUs_);
    }
    anotarEntrada();
    return true;
}

void TurnoMotor::soltar() const {
    if (--profundidad_ == 0) {
        quien_.store(nullptr);
        dueno_.store(std::thread::id());
    }
    mu_.unlock();
}

nlohmann::json TurnoMotor::directas() const {
    nlohmann::json l = nlohmann::json::object();
    {
        const std::lock_guard<std::mutex> g(directasMu_);
        for (const auto& [k, n] : directas_) l[k] = n;
    }
    return {{"total", directasTotal_.load()}, {"lugares", std::move(l)}};
}

nlohmann::json TurnoMotor::estado() const {
    const char* q = quien_.load();
    nlohmann::json sin = nlohmann::json::object();
    {
        const std::lock_guard<std::mutex> g(directasMu_);
        for (const auto& [k, n] : sinParticipar_) sin[k] = n;
    }
    double tenenciaMaxMs = 0;
    std::string tenenciaLugar;
    {
        const std::lock_guard<std::mutex> g(directasMu_);
        tenenciaMaxMs = static_cast<double>(tenenciaMaxUs_) / 1000.0;
        tenenciaLugar = tenenciaMaxLugar_;
    }
    return {{"quien", q != nullptr ? q : ""},
            {"sin_participar", std::move(sin)},
            {"esperas", esperas_.load()},
            {"espera_max_ms", static_cast<double>(esperaMaxUs_.load()) / 1000.0},
            {"directas", directas()},
            // Lo que la interfaz tuvo el turno por un evento (config()/gateway()/launcher()):
            // lo más largo, dónde lo tomó y cuántas veces pasó de kTenenciaLargaUs.
            {"interfaz", {{"max_ms", tenenciaMaxMs}, {"max_lugar", tenenciaLugar}, {"largas", tenenciasLargas_.load()}}}};
}

} // namespace ba::app
