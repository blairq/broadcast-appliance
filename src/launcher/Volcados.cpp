// ============================================================================
// src/launcher/Volcados.cpp — ver Volcados.hpp
// ============================================================================
#include "Volcados.hpp"

#include "Log.hpp"
#include "diag/CaidaCompartida.hpp"
#include "i18n/I18n.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <fstream>
#include <sstream>
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
#else
#include <csignal>
#include <cxxabi.h>
#include <sys/types.h>
#endif

namespace ba::launcher {

namespace caida = ba::diag::caida;

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
[[maybe_unused]] void podar(const std::filesystem::path& dir, const std::string& nombre, std::size_t n,
                            const char* extension = ".dmp") {
    std::error_code ec;
    std::vector<std::filesystem::path> dmps;
    for (const auto& e : std::filesystem::directory_iterator(dir, ec))
        if (e.path().extension() == extension && e.path().filename().string().rfind(nombre + "-", 0) == 0) dmps.push_back(e.path());
    std::sort(dmps.begin(), dmps.end()); // por nombre = por hora
    for (std::size_t i = 0; i + n < dmps.size(); ++i) {
        std::filesystem::remove(dmps[i], ec);
        std::filesystem::path j = dmps[i];
        std::filesystem::remove(j.replace_extension(".json"), ec);
    }
}

} // namespace

std::string causaDeCodigo(std::optional<int> code) {
    using ba::i18n::llenar;
    using ba::i18n::traducir;
    if (!code) return traducir("caidas", "terminó sin código");
    switch (*code) {
        case 139: return traducir("caidas", "error de memoria (SIGSEGV)");
        case 134: return traducir("caidas", "se abortó (SIGABRT)");
        case 138: return traducir("caidas", "error de bus (SIGBUS)");
        case 137: return traducir("caidas", "lo mató el sistema (SIGKILL: ¿falta de memoria?)");
        case 136: return traducir("caidas", "error aritmético (SIGFPE)");
        case 132: return traducir("caidas", "instrucción inválida (SIGILL)");
        case 131: return traducir("caidas", "se colgó (dejó de latir)"); // SIGQUIT: lo pidió el launcher
        default: break;
    }
    if (static_cast<unsigned>(*code) == 0xC0000005u) return traducir("caidas", "violación de acceso (0xC0000005)");
    if (static_cast<unsigned>(*code) == 0xC00000FDu) return traducir("caidas", "desbordamiento de pila (0xC00000FD)");
    return llenar(traducir("caidas", "terminó con el código %1"), {std::to_string(*code)});
}

Volcados::Pilas Volcados::leerPilas(const std::string& texto) {
    // El formato lo escribe diag/Caidas.cpp: «senal N …», «== cayo <id> <nombre>»,
    // los marcos «#NN 0x… modulo+0x… simbolo+0x…», «== hilo …», «== imagenes», «FIN».
    Pilas p;
    std::istringstream in(texto);
    std::string l;
    bool enCayo = false;
    while (std::getline(in, l)) {
        if (l.rfind("senal ", 0) == 0) {
            p.senal = std::atoi(l.c_str() + 6);
        } else if (l.rfind("== cayo ", 0) == 0) {
            enCayo = true;
            const std::size_t esp = l.find(' ', 8);
            p.hilo = esp == std::string::npos ? std::string() : l.substr(esp + 1);
        } else if (l.rfind("== ", 0) == 0) {
            enCayo = false;
        } else if (l == "FIN") {
            p.completa = true;
        } else if (enCayo && l.rfind("#", 0) == 0) {
            // «#03 0x1 modulo+0x2 simbolo+0x3»: lo de antes del último «+».
            std::istringstream m(l);
            std::string num, dir, mod, sim;
            m >> num >> dir >> mod >> sim;
            ba::fault::Marco mc;
            mc.modulo = mod.substr(0, mod.rfind('+'));
            sim = sim.substr(0, sim.rfind('+'));
#if !defined(_WIN32)
            int st = 0;
            if (char* d = abi::__cxa_demangle(sim.c_str(), nullptr, nullptr, &st); d != nullptr && st == 0) {
                sim = d;
                std::free(d);
            }
#endif
            mc.simbolo = sim;
            p.marcos.push_back(std::move(mc));
        }
    }
    p.atribucion = ba::fault::atribuir(p.marcos, p.hilo);
    return p;
}

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
        j["error"] = ba::i18n::llenar(ba::i18n::traducir("dump", "no se pudo abrir el proceso (%1)"),
                                      {std::to_string(GetLastError())});
    } else {
        if (pedido != nullptr) {
            const auto [mod, desp] = moduloDe(proc, pedido->direccion);
            char tipo[sizeof pedido->tipo + 1] = {};
            std::memcpy(tipo, pedido->tipo, sizeof pedido->tipo);
            j["codigo"] = hex(pedido->codigo);
            // La descripción y la causa las lee el operador (el cartel al volver): traducidas.
            const std::string queFue = ba::i18n::traducir("caidas", caida::nombreCodigo(pedido->codigo));
            j["descripcion"] = queFue + " (" + tipo + ")";
            j["direccion"] = hex(pedido->direccion);
            j["modulo"] = mod;
            j["desplazamiento"] = hex(desp);
            j["hilo"] = pedido->hilo;
            v.causa = ba::i18n::llenar(ba::i18n::traducir("caidas", "%1 en %2"), {queFue, mod});
            v.atribucion = ba::fault::atribuir({{mod, std::string()}});
            if (v.atribucion.pieza) j["pieza"] = ba::fault::clave(*v.atribucion.pieza);
        } else {
            v.causa = ba::i18n::traducir("caidas", "se colgó (dejó de latir)");
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
            if (!ok)
                j["error"] = ba::i18n::llenar(ba::i18n::traducir("dump", "MiniDumpWriteDump falló (%1)"),
                                              {std::to_string(GetLastError())});
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
        o << j.dump(1, ' ', false, nlohmann::json::error_handler_t::replace);
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
            BA_LOG_WARN("dump", static_cast<int>(GetLastError()),
                        ba::i18n::llenar(ba::i18n::traducir("dump", "no se pudo crear el canal de caídas de %1"), {nombre}));
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

// En POSIX el canal es un archivo (Logs/dumps/<nombre>.pila): lo abre el hijo
// al instalar su manejador y lo llena solo si se cae (diag/Caidas.cpp).
struct Volcados::Canal {};

namespace {

std::filesystem::path pilaDe(const std::filesystem::path& dir, const std::string& nombre) { return dir / (nombre + ".pila"); }

std::string leerTodo(const std::filesystem::path& p) {
    std::ifstream f(p, std::ios::binary);
    std::stringstream s;
    s << f.rdbuf();
    return s.str();
}

bool hayPilas(const std::filesystem::path& p) {
    std::error_code ec;
    const auto tam = std::filesystem::file_size(p, ec);
    return !ec && tam > 0;
}

/// Se lleva el archivo de pilas a su nombre con la hora y le escribe el .json al
/// lado, como en Windows (lo que leen tools/caidas/analizar.py y el cartel).
Volcado guardar(const std::filesystem::path& dir, const std::string& nombre, std::uint32_t pid,
                const std::filesystem::path& log, bool colgado) {
    Volcado v;
    std::error_code ec;
    const std::filesystem::path origen = pilaDe(dir, nombre);
    const std::string texto = leerTodo(origen);
    const Volcados::Pilas p = Volcados::leerPilas(texto);
    const std::string base = nombre + "-" + horaLocal("%Y%m%d-%H%M%S") + (colgado ? "-colgado" : "-caida");
    v.json = dir / (base + ".json");
    nlohmann::json j{{"componente", nombre == "A" ? "app" : nombre == "B" ? "gateway" : nombre},
                     {"pid", pid},
                     {"tipo", colgado ? "colgado" : "caída"},
                     {"hora", horaLocal("%Y-%m-%dT%H:%M:%S")}};
    if (!texto.empty()) {
        v.dmp = dir / (base + ".pila");
        std::filesystem::rename(origen, v.dmp, ec);
        if (ec) v.dmp.clear();
        else j["volcado"] = v.dmp.filename().string();
    }
    if (!p.completa) j["error"] = texto.empty() ? "sin pilas" : "pilas a medias";
    v.atribucion = p.atribucion;
    const std::string queFue = causaDeCodigo(p.senal > 0 ? std::optional<int>(128 + p.senal) : std::nullopt);
    if (colgado) {
        v.causa = ba::i18n::traducir("caidas", "se colgó (dejó de latir)");
    } else {
        j["codigo"] = p.senal;
        j["descripcion"] = queFue;
        v.causa = p.atribucion.modulo.empty()
                      ? queFue
                      : ba::i18n::llenar(ba::i18n::traducir("caidas", "%1 en %2"), {queFue, p.atribucion.modulo});
    }
    j["hilo"] = p.hilo;
    if (!p.atribucion.modulo.empty()) j["modulo"] = p.atribucion.modulo;
    if (p.atribucion.pieza) j["pieza"] = ba::fault::clave(*p.atribucion.pieza);
    j["causa"] = v.causa;
    j["ultimas_lineas"] = ultimasLineas(log, 80);
    {
        std::ofstream o(v.json, std::ios::trunc);
        o << j.dump(1, ' ', false, nlohmann::json::error_handler_t::replace);
    }
    podar(dir, nombre, 10, ".pila");
    BA_LOG_WARN("dump", 0, nombre + ": " + v.causa + (v.dmp.empty() ? std::string() : " → " + v.dmp.filename().string()));
    return v;
}

} // namespace

Volcados::Volcados(std::filesystem::path logsDir) : dir_(std::move(logsDir) / "dumps") {}
Volcados::~Volcados() = default;

std::map<std::string, std::string> Volcados::canalPara(const std::string& nombre, const std::filesystem::path& logDelHijo) {
    std::lock_guard<std::mutex> l(mu_);
    std::error_code ec;
    std::filesystem::create_directories(dir_, ec);
    // Lo que quedó de una corrida que no se tomó (el launcher se cerró antes): se guarda.
    if (hayPilas(pilaDe(dir_, nombre))) guardar(dir_, nombre, 0, logDelHijo, false);
    std::filesystem::remove(pilaDe(dir_, nombre), ec);
    logs_[nombre] = logDelHijo;
    return {{ba::diag::caida::kVariable, pilaDe(dir_, nombre).string()}};
}

void Volcados::lanzado(const std::string& nombre, std::uint32_t pid) {
    std::lock_guard<std::mutex> l(mu_);
    pids_[nombre] = pid;
}

std::optional<Volcado> Volcados::tomarCaida(const std::string& nombre) {
    std::lock_guard<std::mutex> l(mu_);
    if (!hayPilas(pilaDe(dir_, nombre))) return std::nullopt;
    return guardar(dir_, nombre, pids_[nombre], logs_[nombre], false);
}

bool Volcados::hayCaida(const std::string& nombre) const {
    std::lock_guard<std::mutex> l(mu_);
    return hayPilas(pilaDe(dir_, nombre));
}

Volcado Volcados::porCuelgue(const std::string& nombre, std::uint32_t pid, const std::filesystem::path& logDelHijo) {
    // SIGQUIT: el manejador del hijo escribe las pilas de todos sus hilos y se
    // termina. Se lo espera hasta «FIN» (3 s como mucho); después el supervisor lo mata igual.
    if (pid != 0 && ::kill(static_cast<pid_t>(pid), SIGQUIT) == 0) {
        for (int i = 0; i < 60; ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            if (leerTodo(pilaDe(dir_, nombre)).find("\nFIN\n") != std::string::npos) break;
        }
    }
    std::lock_guard<std::mutex> l(mu_);
    return guardar(dir_, nombre, pid, logDelHijo, true);
}

#endif

} // namespace ba::launcher
