// src/gateway/CargaVideo.cpp — cuánto se exige al decodificar video (ver el .hpp)

#include "CargaVideo.hpp"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cmath>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <vector>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <pdh.h>
#endif

namespace ba::gateway::cargavideo {

namespace {
std::atomic<std::uint64_t> g_recibidos{0};
std::atomic<std::uint64_t> g_decodificados{0};
std::atomic<std::uint64_t> g_nsDecodificar{0};

std::int64_t ahoraMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
        .count();
}
} // namespace

void alRecibir() { g_recibidos.fetch_add(1, std::memory_order_relaxed); }

namespace {
struct Abiertos {
    std::mutex m;
    std::set<const Actividad*> todos;
};
Abiertos& abiertos() {
    static Abiertos a;
    return a;
}
} // namespace

Actividad::Actividad() {
    std::lock_guard<std::mutex> l(abiertos().m);
    abiertos().todos.insert(this);
}

Actividad::~Actividad() {
    std::lock_guard<std::mutex> l(abiertos().m);
    abiertos().todos.erase(this);
}

void Actividad::decodifico(std::int64_t ns) {
    ultimoMs_.store(ahoraMs(), std::memory_order_relaxed);
    g_decodificados.fetch_add(1, std::memory_order_relaxed);
    if (ns > 0) g_nsDecodificar.fetch_add(static_cast<std::uint64_t>(ns), std::memory_order_relaxed);
}

Decodificadores decodificadores(std::int64_t ventanaMs) {
    Decodificadores d;
    const std::int64_t t = ahoraMs();
    std::lock_guard<std::mutex> l(abiertos().m);
    for (const Actividad* a : abiertos().todos) {
        const std::int64_t u = a->ultimoMs();
        if (u != 0 && t - u <= ventanaMs)
            ++d.trabajando;
        else
            ++d.enPausa;
    }
    return d;
}

#if defined(_WIN32)
/// El motor de códec más cargado (contadores «GPU Engine», todos los procesos).
struct Medidor::Motor {
    using Abrir = PDH_STATUS(WINAPI*)(LPCWSTR, DWORD_PTR, PDH_HQUERY*);
    using Agregar = PDH_STATUS(WINAPI*)(PDH_HQUERY, LPCWSTR, DWORD_PTR, PDH_HCOUNTER*);
    using Juntar = PDH_STATUS(WINAPI*)(PDH_HQUERY);
    using Leer = PDH_STATUS(WINAPI*)(PDH_HCOUNTER, DWORD, LPDWORD, LPDWORD, PPDH_FMT_COUNTERVALUE_ITEM_W);
    using Cerrar = PDH_STATUS(WINAPI*)(PDH_HQUERY);

    Motor() {
        dll = LoadLibraryW(L"pdh.dll");
        if (dll == nullptr) return;
        const auto f = [this](const char* n) { return reinterpret_cast<void*>(GetProcAddress(dll, n)); };
        abrir = reinterpret_cast<Abrir>(f("PdhOpenQueryW"));
        agregar = reinterpret_cast<Agregar>(f("PdhAddEnglishCounterW"));
        juntar = reinterpret_cast<Juntar>(f("PdhCollectQueryData"));
        leer = reinterpret_cast<Leer>(f("PdhGetFormattedCounterArrayW"));
        cerrar = reinterpret_cast<Cerrar>(f("PdhCloseQuery"));
        if (!abrir || !agregar || !juntar || !leer || !cerrar) return;
        if (abrir(nullptr, 0, &q) != ERROR_SUCCESS) return;
        if (agregar(q, L"\\GPU Engine(*)\\Utilization Percentage", 0, &c) != ERROR_SUCCESS) return;
        juntar(q); // la primera vuelta no da porcentaje
        ok = true;
    }
    ~Motor() {
        if (q != nullptr && cerrar != nullptr) cerrar(q);
        if (dll != nullptr) FreeLibrary(dll);
    }
    static std::string minusculas(const wchar_t* w) {
        std::string s;
        if (w == nullptr) return s;
        for (; *w != 0; ++w) s.push_back(*w < 128 ? static_cast<char>(std::tolower(static_cast<int>(*w))) : '?');
        return s;
    }
    /// El % del motor de códec más cargado (-1 si no se pudo).
    double medir() {
        if (!ok || juntar(q) != ERROR_SUCCESS) return -1;
        DWORD bytes = 0, n = 0;
        leer(c, PDH_FMT_DOUBLE, &bytes, &n, nullptr);
        if (bytes == 0) return -1;
        std::vector<unsigned char> buf(bytes);
        auto* items = reinterpret_cast<PDH_FMT_COUNTERVALUE_ITEM_W*>(buf.data());
        if (leer(c, PDH_FMT_DOUBLE, &bytes, &n, items) != ERROR_SUCCESS) return -1;
        // «pid_1234_luid_0x…_0x…_phys_0_eng_3_engtype_videodecode»: cada motor de cada placa,
        // sumado entre procesos (desde «luid_»).
        std::map<std::string, double> porMotor;
        bool hayCodec = false;
        for (DWORD i = 0; i < n; ++i) {
            if (items[i].FmtValue.CStatus != 0 && items[i].FmtValue.CStatus != 1) continue;
            const std::string nom = minusculas(items[i].szName);
            const auto t = nom.find("engtype_");
            const auto l = nom.find("luid_");
            if (t == std::string::npos || l == std::string::npos) continue;
            const std::string tipo = nom.substr(t + 8);
            if (tipo.find("codec") == std::string::npos && tipo.find("decode") == std::string::npos &&
                tipo.find("encode") == std::string::npos)
                continue;
            hayCodec = true;
            porMotor[nom.substr(l)] += items[i].FmtValue.doubleValue;
        }
        if (!hayCodec) return -1;
        double m = 0;
        for (const auto& [k, v] : porMotor) m = std::max(m, v);
        return std::min(100.0, m);
    }

    HMODULE dll = nullptr;
    Abrir abrir = nullptr;
    Agregar agregar = nullptr;
    Juntar juntar = nullptr;
    Leer leer = nullptr;
    Cerrar cerrar = nullptr;
    PDH_HQUERY q = nullptr;
    PDH_HCOUNTER c = nullptr;
    bool ok = false;
};
#else
/// Fuera de Windows el sistema no dice la carga del motor de video (macOS: VideoToolbox no
/// tiene contador público; Linux: decodifica por software). Quedan los perdidos.
struct Medidor::Motor {
    double medir() { return -1; }
};
#endif

Medidor::Medidor() : motor_(std::make_unique<Motor>()) {
    recibidos_ = g_recibidos.load(std::memory_order_relaxed);
    decodificados_ = g_decodificados.load(std::memory_order_relaxed);
    ns_ = g_nsDecodificar.load(std::memory_order_relaxed);
    ultimoMs_ = ahoraMs();
}

Medidor::~Medidor() = default;

MedidaCarga Medidor::medir() {
    MedidaCarga m;
    m.motorPct = motor_ ? motor_->medir() : -1;
    const std::uint64_t r = g_recibidos.load(std::memory_order_relaxed);
    const std::uint64_t d = g_decodificados.load(std::memory_order_relaxed);
    const std::uint64_t ns = g_nsDecodificar.load(std::memory_order_relaxed);
    const std::int64_t t = ahoraMs();
    const double seg = std::max<std::int64_t>(1, t - ultimoMs_) / 1000.0;
    const double dr = static_cast<double>(r - recibidos_);
    const double dd = static_cast<double>(d - decodificados_);
    recibidosCps_ = dr / seg;
    decodificadosCps_ = dd / seg;
    if (dd > 0) m.decodificarMs = static_cast<double>(ns - ns_) / dd / 1e6;
    // Con pocos cuadros, un par de perdidos por la red no dicen nada.
    if (dr >= 30) m.perdidosPct = std::max(0.0, dr - dd) / dr * 100.0;
    recibidos_ = r;
    decodificados_ = d;
    ns_ = ns;
    ultimoMs_ = t;
    return m;
}

nlohmann::json Medidor::estado() const {
#if defined(_WIN32)
    const bool motor = motor_ && motor_->ok;
#else
    const bool motor = false;
#endif
    return {{"motor", motor},
            {"recibidos_cps", std::round(recibidosCps_ * 10) / 10},
            {"decodificados_cps", std::round(decodificadosCps_ * 10) / 10}};
}

} // namespace ba::gateway::cargavideo
