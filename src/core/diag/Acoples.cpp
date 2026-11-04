// src/core/diag/Acoples.cpp — ver el .hpp

#include "diag/Acoples.hpp"

// Solo con el detector (desarrollo y pruebas): sin BA_DIAG_ACOPLES este archivo queda vacío.
#ifdef BA_DIAG_ACOPLES

#include "diag/Metricas.hpp"
#include "diag/Registro.hpp"
#include "diag/VigiaBucle.hpp"
#include "i18n/I18nTexto.hpp"

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <cxxabi.h>
#include <dlfcn.h>
#include <execinfo.h>
#endif

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

namespace ba::diag::acoples {

namespace detalle {
std::atomic<int> demoraMs{0};
} // namespace detalle

namespace {

std::atomic<std::thread::id> g_hiloInterfaz{};
std::atomic<bool> g_hayInterfaz{false};
/// Los pasos encendidos que no se anotaron: de otro hilo, o adentro de una anotación. Para ver
/// en el informe que el detector de verdad mira (y desde qué hilo).
std::atomic<std::uint64_t> g_deOtroHilo{0}, g_reentradas{0};
/// Adentro de tocar (lo que anota no vuelve a entrar: una métrica, el registro).
thread_local bool t_adentro = false;

struct Entrada {
    std::string clase, lugar, donde, corria;
    std::vector<std::string> pila;
    std::uint64_t veces = 0;
    std::int64_t demoraUs = 0; ///< lo dormido a propósito
    std::int64_t realUs = 0;   ///< lo que de verdad duró (las que pasan por la Guardia)
    std::int64_t realMaxUs = 0;
};

std::mutex g_mu; // lo de abajo
std::map<std::string, Entrada> g_lugares;
std::uint64_t g_total = 0;
std::uint64_t g_sinDormir = 0;
std::int64_t g_dormidoUs = 0;
// La ventana del tope (solo la toca el hilo de la interfaz, pero informe() la lee).
std::int64_t g_ventanaDesdeUs = 0;
std::int64_t g_ventanaDormidoUs = 0;

std::int64_t ahoraUs() {
    return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

/// «/ruta/larga/src/app/ui/SalaViewModel.cpp» → «src/app/ui/SalaViewModel.cpp».
std::string archivoCorto(const char* f) {
    if (f == nullptr) return "?";
    std::string s = f;
    std::replace(s.begin(), s.end(), '\\', '/');
    if (const auto p = s.rfind("/src/"); p != std::string::npos) s.erase(0, p + 1);
    return s;
}

std::string clave(const char* clase, std::string_view lugar, const std::source_location& donde) {
    std::string k = clase != nullptr ? clase : "?";
    k += '|';
    k.append(lugar.data(), lugar.size());
    k += '|';
    k += donde.file_name();
    k += ':';
    k += std::to_string(donde.line());
    return k;
}

#if !defined(_WIN32)
std::string desenredar(const char* s) {
    int st = 0;
    if (char* d = abi::__cxa_demangle(s, nullptr, nullptr, &st); d != nullptr && st == 0) {
        std::string out = d;
        std::free(d);
        return out;
    }
    return s;
}
#endif

/// La pila del hilo que llama: «módulo función+0xNN» (o «módulo+0xNN» sin símbolo), sin
/// los marcos de acá. Para `atos`/`addr2line` alcanza con módulo+desplazamiento.
std::vector<std::string> pilaActual() {
    std::vector<std::string> out;
    constexpr int kMarcos = 40;
    void* marcos[kMarcos];
#if defined(_WIN32)
    const int n = static_cast<int>(RtlCaptureStackBackTrace(0, kMarcos, marcos, nullptr));
#else
    const int n = backtrace(marcos, kMarcos);
#endif
    for (int i = 2; i < n; ++i) { // sin pilaActual ni tocar
        char linea[512];
#if defined(_WIN32)
        HMODULE mod = nullptr;
        char nombre[MAX_PATH] = "?";
        if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                               static_cast<LPCSTR>(marcos[i]), &mod) &&
            mod != nullptr)
            GetModuleFileNameA(mod, nombre, MAX_PATH);
        const char* b = std::strrchr(nombre, '\\');
        std::snprintf(linea, sizeof linea, "%s+0x%llx", b != nullptr ? b + 1 : nombre,
                      static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(marcos[i]) -
                                                      reinterpret_cast<std::uintptr_t>(mod)));
#else
        Dl_info di{};
        if (dladdr(marcos[i], &di) == 0 || di.dli_fname == nullptr) {
            std::snprintf(linea, sizeof linea, "? %p", marcos[i]);
        } else {
            const char* b = std::strrchr(di.dli_fname, '/');
            const char* modulo = b != nullptr ? b + 1 : di.dli_fname;
            if (di.dli_sname != nullptr) {
                std::string f = desenredar(di.dli_sname);
                if (f.size() > 300) f.resize(300);
                std::snprintf(linea, sizeof linea, "%s %s+0x%lx", modulo, f.c_str(),
                              static_cast<unsigned long>(reinterpret_cast<std::uintptr_t>(marcos[i]) -
                                                         reinterpret_cast<std::uintptr_t>(di.dli_saddr)));
            } else {
                std::snprintf(linea, sizeof linea, "%s+0x%lx", modulo,
                              static_cast<unsigned long>(reinterpret_cast<std::uintptr_t>(marcos[i]) -
                                                         reinterpret_cast<std::uintptr_t>(di.dli_fbase)));
            }
        }
#endif
        out.emplace_back(linea);
    }
    return out;
}

} // namespace

void activar(int ms) {
    const int antes = detalle::demoraMs.exchange(std::max(0, ms));
    if (antes == ms) return;
    registro().linea(Nivel::Aviso, "interfaz", 0,
                     ms > 0 ? ba::i18n::llenar(ba::i18n::traducir("diag", "detector de acoples ENCENDIDO: cada camino de la interfaz que no pasa "
                                                                           "por el modelo espera %1 ms de más y queda anotado (solo pruebas)"),
                                               {std::to_string(ms)})
                            : ba::i18n::traducir("diag", "detector de acoples apagado"));
}

int demoraMs() { return detalle::demoraMs.load(); }

void marcarHiloInterfaz() {
    g_hiloInterfaz.store(std::this_thread::get_id());
    g_hayInterfaz.store(true);
}

bool enHiloInterfaz() { return g_hiloInterfaz.load() == std::this_thread::get_id(); }

bool tocar(const char* clase, std::string_view lugar, const std::source_location& donde) {
    const int demora = detalle::demoraMs.load(std::memory_order_relaxed);
    if (demora <= 0) return false;
    if (t_adentro) {
        g_reentradas.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    if (!enHiloInterfaz()) {
        g_deOtroHilo.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    t_adentro = true;
    const std::string k = clave(clase, lugar, donde);
    bool primera = false;
    std::int64_t dormir = static_cast<std::int64_t>(demora) * 1000;
    {
        const std::lock_guard<std::mutex> g(g_mu);
        Entrada& e = g_lugares[k];
        primera = e.veces++ == 0;
        ++g_total;
        if (primera) {
            e.clase = clase != nullptr ? clase : "?";
            e.lugar = std::string(lugar);
            e.donde = archivoCorto(donde.file_name()) + ":" + std::to_string(donde.line());
        }
        // El tope: como mucho kTopeVentanaMs dormidos en cada kVentanaMs.
        const std::int64_t ahora = ahoraUs();
        if (ahora - g_ventanaDesdeUs > static_cast<std::int64_t>(kVentanaMs) * 1000) {
            g_ventanaDesdeUs = ahora;
            g_ventanaDormidoUs = 0;
        }
        if (g_ventanaDormidoUs + dormir > static_cast<std::int64_t>(kTopeVentanaMs) * 1000) {
            dormir = 0;
            ++g_sinDormir;
        }
        g_ventanaDormidoUs += dormir;
        g_dormidoUs += dormir;
        e.demoraUs += dormir;
    }
    if (primera) {
        // Fuera del candado: la pila y lo que corría cuestan (una vez por lugar).
        std::vector<std::string> pila = pilaActual();
        std::string corria = VigiaBucle::interfaz().enCurso();
        std::string donde_;
        {
            const std::lock_guard<std::mutex> g(g_mu);
            Entrada& e = g_lugares[k];
            e.pila = std::move(pila);
            e.corria = corria;
            donde_ = e.donde;
        }
        registro().linea(Nivel::Aviso, "interfaz", 0,
                         ba::i18n::llenar(ba::i18n::traducir("diag", "acople de la interfaz (%1): %2 en %3, mientras corría «%4»"),
                                          {std::string(clase != nullptr ? clase : "?"), std::string(lugar), donde_, corria}));
    }
    Metricas::proceso()
        .contador("ba_acoples_total",
                  QT_TRANSLATE_NOOP("metricas", "Pasos de la interfaz por un camino que no pasa por el modelo (detector de acoples, solo pruebas), por clase"),
                  {{"clase", clase != nullptr ? clase : "?"}})
        .sumar();
    t_adentro = false;
    if (dormir > 0) std::this_thread::sleep_for(std::chrono::microseconds(dormir));
    return true;
}

void sumarReal(const char* clase, std::string_view lugar, const std::source_location& donde, std::int64_t us) {
    // La guardia empieza a medir al volver de tocar: lo dormido no entra.
    const std::string k = clave(clase, lugar, donde);
    const std::lock_guard<std::mutex> g(g_mu);
    const auto it = g_lugares.find(k);
    if (it == g_lugares.end()) return;
    Entrada& e = it->second;
    e.realUs += us;
    e.realMaxUs = std::max(e.realMaxUs, us);
}

nlohmann::json informe() {
    std::vector<const Entrada*> orden;
    nlohmann::json lugares = nlohmann::json::array();
    const std::lock_guard<std::mutex> g(g_mu);
    for (const auto& [k, e] : g_lugares) orden.push_back(&e);
    std::sort(orden.begin(), orden.end(), [](const Entrada* a, const Entrada* b) {
        const std::int64_t ta = a->demoraUs + a->realUs, tb = b->demoraUs + b->realUs;
        return ta != tb ? ta > tb : a->veces > b->veces;
    });
    for (const Entrada* e : orden)
        lugares.push_back({{"clase", e->clase},
                           {"lugar", e->lugar},
                           {"donde", e->donde},
                           {"veces", e->veces},
                           {"demora_ms", static_cast<double>(e->demoraUs) / 1000.0},
                           {"real_ms", static_cast<double>(e->realUs) / 1000.0},
                           {"real_max_ms", static_cast<double>(e->realMaxUs) / 1000.0},
                           {"total_ms", static_cast<double>(e->demoraUs + e->realUs) / 1000.0},
                           {"corria", e->corria},
                           {"pila", e->pila}});
    return {{"activo", detalle::demoraMs.load() > 0},
            {"demora_ms", detalle::demoraMs.load()},
            {"tope_ms", kTopeVentanaMs},
            {"ventana_ms", kVentanaMs},
            {"total", g_total},
            {"dormido_ms", static_cast<double>(g_dormidoUs) / 1000.0},
            {"sin_dormir", g_sinDormir},
            {"hilo_interfaz_marcado", g_hayInterfaz.load()},
            {"de_otro_hilo", g_deOtroHilo.load()},
            {"reentradas", g_reentradas.load()},
            {"lugares", std::move(lugares)}};
}

void reiniciar() {
    const std::lock_guard<std::mutex> g(g_mu);
    g_lugares.clear();
    g_total = 0;
    g_sinDormir = 0;
    g_dormidoUs = 0;
    g_ventanaDesdeUs = 0;
    g_ventanaDormidoUs = 0;
    g_deOtroHilo = 0;
    g_reentradas = 0;
}

} // namespace ba::diag::acoples

#endif // BA_DIAG_ACOPLES
