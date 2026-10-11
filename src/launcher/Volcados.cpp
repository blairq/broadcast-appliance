// ============================================================================
// src/launcher/Volcados.cpp — ver Volcados.hpp
// ============================================================================
#include "Volcados.hpp"

#include "Log.hpp"
#include "diag/CaidaCompartida.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <fstream>
#include <thread>
#include <vector>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <dbghelp.h>
#include <psapi.h>
#endif

namespace ba::launcher {

namespace {

[[maybe_unused]] std::string horaLocal(const char* formato) {
    const std::time_t ahora = std::time(nullptr);
    std::tm tm{};
#if defined(_WIN32)
    localtime_s(&tm, &ahora);
#else
    localtime_r(&ahora, &tm);
#endif
    char b[40];
    std::strftime(b, sizeof b, formato, &tm);
    return b;
}

[[maybe_unused]] std::string hex(std::uint64_t v) {
    char b[24];
    std::snprintf(b, sizeof b, "0x%llX", static_cast<unsigned long long>(v));
    return b;
}

/// Las últimas líneas del registro del hijo (lo que estaba haciendo).
[[maybe_unused]] std::vector<std::string> ultimasLineas(const std::filesystem::path& log, std::size_t cuantas) {
    std::vector<std::string> out;
    std::ifstream f(log, std::ios::binary);
    if (!f) return out;
    f.seekg(0, std::ios::end);
    const std::streamoff tam = f.tellg();
    const std::streamoff desde = std::max<std::streamoff>(0, tam - 96 * 1024);
    f.seekg(desde);
    std::string linea;
    if (desde > 0) std::getline(f, linea); // la primera, cortada
    while (std::getline(f, linea)) {
        if (!linea.empty() && linea.back() == '\r') linea.pop_back();
        out.push_back(linea);
        if (out.size() > cuantas) out.erase(out.begin());
    }
    return out;
}

/// Se quedan los últimos `n` volcados de `nombre` (con su .json).
[[maybe_unused]] void podar(const std::filesystem::path& dir, const std::string& nombre, std::size_t n) {
    std::error_code ec;
    std::vector<std::filesystem::path> dmps;
    for (const auto& e : std::filesystem::directory_iterator(dir, ec))
        if (e.path().extension() == ".dmp" && e.path().filename().string().rfind(nombre + "-", 0) == 0) dmps.push_back(e.path());
    std::sort(dmps.begin(), dmps.end()); // por nombre = por hora
    for (std::size_t i = 0; i + n < dmps.size(); ++i) {
        std::filesystem::remove(dmps[i], ec);
        std::filesystem::path j = dmps[i];
        std::filesystem::remove(j.replace_extension(".json"), ec);
    }
}

} // namespace

#if defined(_WIN32)

struct Volcados::Canal {
    std::string nombre;
    std::filesystem::path log;
    std::wstring base;
    HANDLE mapa = nullptr, pide = nullptr, hecho = nullptr;
    caida::Pedido* pedido = nullptr;
    std::atomic<std::uint32_t> pid{0};
    std::mutex mu;
    std::optional<Volcado> caida;
};

namespace {

/// El módulo que contiene `dir` en el proceso `proc`, y el desplazamiento.
std::pair<std::string, std::uint64_t> moduloDe(HANDLE proc, std::uint64_t dir) {
    HMODULE mods[1024];
    DWORD bytes = 0;
    if (!K32EnumProcessModulesEx(proc, mods, sizeof mods, &bytes, LIST_MODULES_ALL)) return {"?", dir};
    for (DWORD i = 0; i < bytes / sizeof(HMODULE); ++i) {
        MODULEINFO mi{};
        if (!K32GetModuleInformation(proc, mods[i], &mi, sizeof mi)) continue;
        const auto base = reinterpret_cast<std::uint64_t>(mi.lpBaseOfDll);
        if (dir >= base && dir < base + mi.SizeOfImage) {
            wchar_t n[MAX_PATH] = {};
            K32GetModuleBaseNameW(proc, mods[i], n, MAX_PATH);
            char u[MAX_PATH * 3] = {};
            WideCharToMultiByte(CP_UTF8, 0, n, -1, u, sizeof u, nullptr, nullptr);
            return {u, dir - base};
        }
    }
    return {"?", dir};
}

using MiniDumpFn = BOOL(WINAPI*)(HANDLE, DWORD, HANDLE, MINIDUMP_TYPE, PMINIDUMP_EXCEPTION_INFORMATION,
                                 PMINIDUMP_USER_STREAM_INFORMATION, PMINIDUMP_CALLBACK_INFORMATION);

MiniDumpFn miniDump() {
    static MiniDumpFn fn = []() -> MiniDumpFn {
        HMODULE d = LoadLibraryW(L"dbghelp.dll");
        return d != nullptr ? reinterpret_cast<MiniDumpFn>(reinterpret_cast<void*>(GetProcAddress(d, "MiniDumpWriteDump")))
                            : nullptr;
    }();
    return fn;
}

/// Escribe el .dmp y el .json. `pedido` (de una caída) o nada (cuelgue).
Volcado escribir(const std::filesystem::path& dir, const std::string& nombre, std::uint32_t pid,
                 const std::filesystem::path& log, const caida::Pedido* pedido) {
    Volcado v;
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    const std::string base = nombre + "-" + horaLocal("%Y%m%d-%H%M%S") + (pedido != nullptr ? "-caida" : "-colgado");
    v.dmp = dir / (base + ".dmp");
    v.json = dir / (base + ".json");
    nlohmann::json j{{"componente", nombre == "A" ? "app" : nombre == "B" ? "gateway" : nombre},
                     {"pid", pid},
                     {"tipo", pedido != nullptr ? "caída" : "colgado"},
                     {"hora", horaLocal("%Y-%m-%dT%H:%M:%S")},
                     {"volcado", v.dmp.filename().string()}};
    HANDLE proc = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ | PROCESS_DUP_HANDLE, FALSE, pid);
    if (proc == nullptr) {
        j["error"] = "no se pudo abrir el proceso (" + std::to_string(GetLastError()) + ")";
    } else {
        if (pedido != nullptr) {
            const auto [mod, desp] = moduloDe(proc, pedido->direccion);
            char tipo[sizeof pedido->tipo + 1] = {};
            std::memcpy(tipo, pedido->tipo, sizeof pedido->tipo);
            j["codigo"] = hex(pedido->codigo);
            j["descripcion"] = std::string(caida::nombreCodigo(pedido->codigo)) + " (" + tipo + ")";
            j["direccion"] = hex(pedido->direccion);
            j["modulo"] = mod;
            j["desplazamiento"] = hex(desp);
            j["hilo"] = pedido->hilo;
            v.causa = std::string(caida::nombreCodigo(pedido->codigo)) + " en " + mod;
        } else {
            v.causa = "se colgó (dejó de latir)";
        }
        const auto t0 = std::chrono::steady_clock::now();
        HANDLE f = CreateFileW(v.dmp.wstring().c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        bool ok = false;
        if (f != INVALID_HANDLE_VALUE && miniDump() != nullptr) {
            MINIDUMP_EXCEPTION_INFORMATION mei{};
            if (pedido != nullptr) {
                mei.ThreadId = pedido->hilo;
                mei.ExceptionPointers = reinterpret_cast<PEXCEPTION_POINTERS>(pedido->punteros);
                mei.ClientPointers = TRUE; // el puntero es del hijo
            }
            ok = miniDump()(proc, pid, f, static_cast<MINIDUMP_TYPE>(caida::kTipoVolcado), pedido != nullptr ? &mei : nullptr,
                            nullptr, nullptr) != FALSE;
            if (!ok) j["error"] = "MiniDumpWriteDump falló (" + std::to_string(GetLastError()) + ")";
        }
        if (f != INVALID_HANDLE_VALUE) CloseHandle(f);
        CloseHandle(proc);
        j["volcado_ms"] = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
        if (!ok) std::filesystem::remove(v.dmp, ec);
    }
    j["causa"] = v.causa;
    j["ultimas_lineas"] = ultimasLineas(log, 80);
    {
        std::ofstream o(v.json, std::ios::trunc);
        o << j.dump(1);
    }
    podar(dir, nombre, 10);
    BA_LOG_WARN("dump", 0, nombre + ": " + v.causa + " → " + v.dmp.filename().string());
    return v;
}

} // namespace

Volcados::Volcados(std::filesystem::path logsDir) : dir_(std::move(logsDir) / "dumps") {}

Volcados::~Volcados() = default; // los hilos y el canal viven lo que el launcher

std::map<std::string, std::string> Volcados::canalPara(const std::string& nombre, const std::filesystem::path& logDelHijo) {
    std::lock_guard<std::mutex> l(mu_);
    auto& c = canales_[nombre];
    if (!c) {
        c = std::make_shared<Canal>();
        c->nombre = nombre;
        c->base = L"Local\\BA.Caida." + std::to_wstring(GetCurrentProcessId()) + L"." +
                  std::wstring(nombre.begin(), nombre.end());
        c->mapa = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, sizeof(caida::Pedido), c->base.c_str());
        if (c->mapa != nullptr)
            c->pedido = static_cast<caida::Pedido*>(MapViewOfFile(c->mapa, FILE_MAP_WRITE, 0, 0, sizeof(caida::Pedido)));
        c->pide = CreateEventW(nullptr, FALSE, FALSE, caida::nombrePide(c->base).c_str());
        c->hecho = CreateEventW(nullptr, TRUE, FALSE, caida::nombreHecho(c->base).c_str());
        if (c->pedido == nullptr || c->pide == nullptr || c->hecho == nullptr) {
            BA_LOG_WARN("dump", static_cast<int>(GetLastError()), "no se pudo crear el canal de caídas de " + nombre);
            canales_.erase(nombre);
            return {};
        }
        // El que atiende: un hilo por canal, para siempre (los hijos se relanzan).
        std::thread([c, dir = dir_]() {
            for (;;) {
                if (WaitForSingleObject(c->pide, INFINITE) != WAIT_OBJECT_0) return;
                if (c->pedido->magic != caida::kMagic) continue;
                const caida::Pedido p = *c->pedido;
                std::filesystem::path log;
                {
                    std::lock_guard<std::mutex> lc(c->mu);
                    log = c->log;
                }
                Volcado v = escribir(dir, c->nombre, c->pid.load(), log, &p);
                {
                    std::lock_guard<std::mutex> lc(c->mu);
                    c->caida = std::move(v);
                }
                c->pedido->magic = 0;
                SetEvent(c->hecho);
            }
        }).detach();
    }
    {
        std::lock_guard<std::mutex> lc(c->mu);
        c->log = logDelHijo;
        c->caida.reset();
    }
    c->pedido->magic = 0;
    ResetEvent(c->hecho);
    ResetEvent(c->pide);
    const std::string base(c->base.begin(), c->base.end()); // ASCII
    return {{caida::kVariable, base}};
}

void Volcados::lanzado(const std::string& nombre, std::uint32_t pid) {
    std::lock_guard<std::mutex> l(mu_);
    if (auto it = canales_.find(nombre); it != canales_.end()) it->second->pid = pid;
}

std::optional<Volcado> Volcados::tomarCaida(const std::string& nombre) {
    std::lock_guard<std::mutex> l(mu_);
    auto it = canales_.find(nombre);
    if (it == canales_.end()) return std::nullopt;
    std::lock_guard<std::mutex> lc(it->second->mu);
    std::optional<Volcado> v = std::move(it->second->caida);
    it->second->caida.reset();
    return v;
}

bool Volcados::hayCaida(const std::string& nombre) const {
    std::lock_guard<std::mutex> l(mu_);
    auto it = canales_.find(nombre);
    if (it == canales_.end()) return false;
    std::lock_guard<std::mutex> lc(it->second->mu);
    return it->second->caida.has_value();
}

Volcado Volcados::porCuelgue(const std::string& nombre, std::uint32_t pid, const std::filesystem::path& logDelHijo) {
    return escribir(dir_, nombre, pid, logDelHijo, nullptr);
}

#else // !_WIN32

struct Volcados::Canal {};

Volcados::Volcados(std::filesystem::path logsDir) : dir_(std::move(logsDir) / "dumps") {}
Volcados::~Volcados() = default;
std::map<std::string, std::string> Volcados::canalPara(const std::string&, const std::filesystem::path&) { return {}; }
void Volcados::lanzado(const std::string&, std::uint32_t) {}
std::optional<Volcado> Volcados::tomarCaida(const std::string&) { return std::nullopt; }
bool Volcados::hayCaida(const std::string&) const { return false; }
Volcado Volcados::porCuelgue(const std::string&, std::uint32_t, const std::filesystem::path&) { return {}; }

#endif

} // namespace ba::launcher
