// ============================================================================
// src/core/diag/Perfil.cpp — ver Perfil.hpp
// ============================================================================
#include "diag/Perfil.hpp"

#include "diag/Registro.hpp"
#include "i18n/I18n.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <mutex>
#include <sstream>
#include <thread>
#include <vector>

#if defined(_WIN32)
#include <windows.h>
#include <pdh.h>
#include <psapi.h>
#include <tlhelp32.h>
#elif defined(__APPLE__)
#include <mach/mach.h>
#include <pthread.h>
#elif defined(__linux__)
#include <dirent.h>
#include <pthread.h>
#include <unistd.h>
#endif

namespace ba::diag {
namespace {

using json = nlohmann::json;
using Reloj = std::chrono::steady_clock;

struct Hilo {
    std::uint64_t id = 0;
    std::string nombre;
    double cpuSeg = 0; ///< CPU acumulada (usuario + sistema), en segundos
};

/// Un nombre de hilo en UTF-8 válido. Los sistemas los truncan por BYTES (15 o 63)
/// y pueden cortar una letra a la mitad: nlohmann::json tira una excepción al
/// escribirlo, y eso tiraba la app abajo (SIGABRT, visto en la Mac).
std::string limpio(const std::string& s) {
    std::string out;
    for (size_t i = 0; i < s.size();) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        const size_t n = c < 0x80 ? 1 : (c >> 5) == 0x6 ? 2 : (c >> 4) == 0xE ? 3 : (c >> 3) == 0x1E ? 4 : 0;
        bool ok = n > 0 && i + n <= s.size();
        for (size_t k = 1; ok && k < n; ++k) ok = (static_cast<unsigned char>(s[i + k]) >> 6) == 0x2;
        if (ok) {
            out.append(s, i, n);
            i += n;
        } else {
            out.push_back('?');
            ++i;
        }
    }
    return out;
}

// --- Lo que da cada sistema ----------------------------------------------------

#if defined(_WIN32)
std::string utf8(const wchar_t* w) {
    if (w == nullptr || *w == 0) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    std::string s(static_cast<size_t>(n > 0 ? n - 1 : 0), '\0');
    if (n > 1) WideCharToMultiByte(CP_UTF8, 0, w, -1, s.data(), n, nullptr, nullptr);
    return s;
}
double segundos(const FILETIME& f) {
    return static_cast<double>((static_cast<std::uint64_t>(f.dwHighDateTime) << 32) | f.dwLowDateTime) / 1e7;
}
std::vector<Hilo> hilos() {
    // GetThreadDescription es de Windows 10 1607: se busca, no se enlaza.
    using Desc = HRESULT(WINAPI*)(HANDLE, PWSTR*);
    static const Desc desc =
        reinterpret_cast<Desc>(reinterpret_cast<void*>(GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "GetThreadDescription")));
    std::vector<Hilo> out;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snap == INVALID_HANDLE_VALUE) return out;
    THREADENTRY32 te{};
    te.dwSize = sizeof(te);
    const DWORD pid = GetCurrentProcessId();
    for (BOOL ok = Thread32First(snap, &te); ok; ok = Thread32Next(snap, &te)) {
        if (te.th32OwnerProcessID != pid) continue;
        HANDLE h = OpenThread(THREAD_QUERY_LIMITED_INFORMATION, FALSE, te.th32ThreadID);
        if (h == nullptr) continue;
        FILETIME c, e, k, u;
        Hilo hi;
        hi.id = te.th32ThreadID;
        if (GetThreadTimes(h, &c, &e, &k, &u)) hi.cpuSeg = segundos(k) + segundos(u);
        if (desc != nullptr) {
            PWSTR d = nullptr;
            if (SUCCEEDED(desc(h, &d)) && d != nullptr) {
                hi.nombre = utf8(d);
                LocalFree(d);
            }
        }
        CloseHandle(h);
        out.push_back(std::move(hi));
    }
    CloseHandle(snap);
    return out;
}
/// De quién es un hilo sin nombre: se lo frena un instante, se recorre su pila
/// (con la información de desenrollado de cada módulo, sin dbghelp) y se dan los
/// módulos por los que pasa, del más adentro al más afuera
/// («ntdll+0x… < amdxc64+0x… < kernel32+0x…»). Mientras está frenado no se pide
/// memoria ni se toma el lock del cargador: solo se juntan direcciones.
#if defined(_M_X64) || defined(__x86_64__)
/// Las direcciones de la pila de un hilo (frenado un instante). Mientras está
/// frenado no se pide memoria ni se toma el lock del cargador.
int direcciones(HANDLE h, DWORD64* dirs, int max) {
    int n = 0;
    if (SuspendThread(h) == static_cast<DWORD>(-1)) return 0;
    alignas(16) CONTEXT ctx{};
    ctx.ContextFlags = CONTEXT_FULL;
    if (GetThreadContext(h, &ctx)) {
        while (n < max && ctx.Rip != 0) {
            dirs[n++] = ctx.Rip;
            DWORD64 base = 0;
            PRUNTIME_FUNCTION f = RtlLookupFunctionEntry(ctx.Rip, &base, nullptr);
            const DWORD64 rsp = ctx.Rsp;
            if (f == nullptr) {
                // Sin información (código generado al vuelo, o una hoja): solo
                // se confía en el primer marco; después se para.
                if (n > 1) break;
                ctx.Rip = *reinterpret_cast<DWORD64*>(ctx.Rsp);
                ctx.Rsp += 8;
            } else {
                PVOID datos = nullptr;
                DWORD64 marco = 0;
                RtlVirtualUnwind(UNW_FLAG_NHANDLER, base, ctx.Rip, f, &ctx, &datos, &marco, nullptr);
            }
            if (ctx.Rsp <= rsp) break;
        }
    }
    ResumeThread(h);
    return n;
}
HMODULE moduloDe(DWORD64 dir) {
    HMODULE m = nullptr;
    return GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                              reinterpret_cast<LPCWSTR>(dir), &m)
               ? m
               : nullptr;
}
#endif

/// De quién es un hilo sin nombre: se recorre su pila (con la información de
/// desenrollado de cada módulo, sin dbghelp) y se dan los módulos por los que
/// pasa, del más adentro al más afuera, con hasta 4 direcciones por módulo
/// («ntdll+0x… < Qt6Core+0x…,0x… < kernel32+0x…»). Un hilo que gasta pero casi
/// siempre está esperando se mira varias veces seguidas hasta agarrarlo
/// trabajando: esa pila dice qué hace (la de espera, solo de quién es).
std::string pila(std::uint64_t tid) {
#if defined(_M_X64) || defined(__x86_64__)
    if (tid == GetCurrentThreadId()) return {};
    HANDLE h = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE,
                          static_cast<DWORD>(tid));
    if (h == nullptr) return {};
    static const HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    static const HMODULE win32u = GetModuleHandleW(L"win32u.dll");
    DWORD64 dirs[48];
    int n = 0;
    bool trabajando = false;
    for (int intento = 0; intento < 60 && !trabajando; ++intento) {
        DWORD64 otra[48];
        const int k = direcciones(h, otra, 48);
        if (k == 0) continue;
        const HMODULE m = moduloDe(otra[0]);
        trabajando = m != ntdll && m != win32u;
        if (trabajando || n == 0) {
            std::copy(otra, otra + k, dirs);
            n = k;
        }
        if (!trabajando) Sleep(2);
    }
    CloseHandle(h);
    std::string out;
    HMODULE ultimo = nullptr;
    int mods = 0, enModulo = 0;
    for (int i = 0; i < n && mods < 12; ++i) {
        const HMODULE m = moduloDe(dirs[i]);
        char off[32];
        std::snprintf(off, sizeof off, "0x%llx",
                      static_cast<unsigned long long>(dirs[i] - reinterpret_cast<DWORD64>(m)));
        if (m != nullptr && m == ultimo) {
            if (++enModulo < 4) out += std::string(",") + off;
            continue;
        }
        ultimo = m;
        enModulo = 0;
        ++mods;
        if (!out.empty()) out += " < ";
        if (m == nullptr) {
            out += "?";
            continue;
        }
        wchar_t ruta[MAX_PATH] = {};
        GetModuleFileNameW(m, ruta, MAX_PATH);
        const wchar_t* nom = ruta;
        for (const wchar_t* c = ruta; *c != 0; ++c)
            if (*c == L'\\' || *c == L'/') nom = c + 1;
        out += utf8(nom) + "+" + off;
    }
    return trabajando ? "[trabajando] " + out : out;
#else
    (void)tid;
    return {};
#endif
}
double cpuProceso() {
    FILETIME c, e, k, u;
    return GetProcessTimes(GetCurrentProcess(), &c, &e, &k, &u) ? segundos(k) + segundos(u) : 0;
}
double memoriaMb() {
    PROCESS_MEMORY_COUNTERS pmc{};
    return K32GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc)) ? pmc.WorkingSetSize / 1048576.0 : 0;
}

/// La GPU del proceso por motor (los contadores «GPU Engine», como el Administrador de tareas).
/// pdh.dll se CARGA al medir (no se importa): este archivo se compila siempre y un
/// build normal no tiene por qué depender de ella (check-portable-win la rechazaba).
class Gpu {
    using Abrir = PDH_STATUS(WINAPI*)(LPCWSTR, DWORD_PTR, PDH_HQUERY*);
    using Agregar = PDH_STATUS(WINAPI*)(PDH_HQUERY, LPCWSTR, DWORD_PTR, PDH_HCOUNTER*);
    using Juntar = PDH_STATUS(WINAPI*)(PDH_HQUERY);
    using Leer = PDH_STATUS(WINAPI*)(PDH_HCOUNTER, DWORD, LPDWORD, LPDWORD, PPDH_FMT_COUNTERVALUE_ITEM_W);
    using Cerrar = PDH_STATUS(WINAPI*)(PDH_HQUERY);

   public:
    Gpu() {
        dll_ = LoadLibraryW(L"pdh.dll");
        if (dll_ == nullptr) return;
        const auto f = [this](const char* n) { return reinterpret_cast<void*>(GetProcAddress(dll_, n)); };
        abrir_ = reinterpret_cast<Abrir>(f("PdhOpenQueryW"));
        agregar_ = reinterpret_cast<Agregar>(f("PdhAddEnglishCounterW"));
        juntar_ = reinterpret_cast<Juntar>(f("PdhCollectQueryData"));
        leer_ = reinterpret_cast<Leer>(f("PdhGetFormattedCounterArrayW"));
        cerrar_ = reinterpret_cast<Cerrar>(f("PdhCloseQuery"));
        if (!abrir_ || !agregar_ || !juntar_ || !leer_ || !cerrar_) return;
        if (abrir_(nullptr, 0, &q_) != ERROR_SUCCESS) return;
        if (agregar_(q_, L"\\GPU Engine(*)\\Utilization Percentage", 0, &c_) != ERROR_SUCCESS) return;
        juntar_(q_); // la primera muestra no da porcentaje
        ok_ = true;
    }
    ~Gpu() {
        if (q_ != nullptr && cerrar_ != nullptr) cerrar_(q_);
        if (dll_ != nullptr) FreeLibrary(dll_);
    }
    json medir() {
        json out = json::object();
        if (!ok_ || juntar_(q_) != ERROR_SUCCESS) return out;
        DWORD bytes = 0, n = 0;
        leer_(c_, PDH_FMT_DOUBLE, &bytes, &n, nullptr);
        if (bytes == 0) return out;
        std::vector<unsigned char> buf(bytes);
        auto* items = reinterpret_cast<PDH_FMT_COUNTERVALUE_ITEM_W*>(buf.data());
        if (leer_(c_, PDH_FMT_DOUBLE, &bytes, &n, items) != ERROR_SUCCESS) return out;
        // Instancias «pid_1234_luid_0x..._phys_0_eng_3_engtype_3D»: las de este proceso, sumadas por tipo.
        const std::string mio = "pid_" + std::to_string(GetCurrentProcessId()) + "_";
        std::map<std::string, double> suma;
        for (DWORD i = 0; i < n; ++i) {
            const std::string nom = utf8(items[i].szName);
            if (nom.rfind(mio, 0) != 0) continue;
            const auto p = nom.find("engtype_");
            if (p == std::string::npos) continue;
            suma[nom.substr(p + 8)] += items[i].FmtValue.doubleValue;
        }
        for (const auto& [k, v] : suma)
            if (v > 0.05) out[k] = std::round(v * 10) / 10;
        return out;
    }

   private:
    HMODULE dll_ = nullptr;
    Abrir abrir_ = nullptr;
    Agregar agregar_ = nullptr;
    Juntar juntar_ = nullptr;
    Leer leer_ = nullptr;
    Cerrar cerrar_ = nullptr;
    PDH_HQUERY q_ = nullptr;
    PDH_HCOUNTER c_ = nullptr;
    bool ok_ = false;
};

#elif defined(__APPLE__)
std::string pila(std::uint64_t) { return {}; }
std::vector<Hilo> hilos() {
    std::vector<Hilo> out;
    thread_act_array_t lista = nullptr;
    mach_msg_type_number_t n = 0;
    if (task_threads(mach_task_self(), &lista, &n) != KERN_SUCCESS) return out;
    for (mach_msg_type_number_t i = 0; i < n; ++i) {
        thread_basic_info_data_t info{};
        mach_msg_type_number_t cuenta = THREAD_BASIC_INFO_COUNT;
        Hilo h;
        h.id = lista[i];
        if (thread_info(lista[i], THREAD_BASIC_INFO, reinterpret_cast<thread_info_t>(&info), &cuenta) == KERN_SUCCESS)
            h.cpuSeg = info.user_time.seconds + info.user_time.microseconds / 1e6 + info.system_time.seconds +
                       info.system_time.microseconds / 1e6;
        if (pthread_t pt = pthread_from_mach_thread_np(lista[i])) {
            char nom[64] = {0};
            pthread_getname_np(pt, nom, sizeof(nom));
            h.nombre = nom;
        }
        mach_port_deallocate(mach_task_self(), lista[i]);
        out.push_back(std::move(h));
    }
    vm_deallocate(mach_task_self(), reinterpret_cast<vm_address_t>(lista), n * sizeof(thread_act_t));
    return out;
}
double cpuProceso() {
    double s = 0;
    for (const auto& h : hilos()) s += h.cpuSeg; // los que terminaron no cuentan: alcanza para el delta
    return s;
}
double memoriaMb() {
    mach_task_basic_info_data_t info{};
    mach_msg_type_number_t n = MACH_TASK_BASIC_INFO_COUNT;
    return task_info(mach_task_self(), MACH_TASK_BASIC_INFO, reinterpret_cast<task_info_t>(&info), &n) == KERN_SUCCESS
               ? info.resident_size / 1048576.0
               : 0;
}
struct Gpu {
    json medir() { return json::object(); } // sin API pública por proceso en macOS
};

#elif defined(__linux__)
std::string pila(std::uint64_t) { return {}; }
std::vector<Hilo> hilos() {
    std::vector<Hilo> out;
    const double tick = static_cast<double>(sysconf(_SC_CLK_TCK));
    DIR* d = opendir("/proc/self/task");
    if (d == nullptr) return out;
    while (dirent* e = readdir(d)) {
        if (e->d_name[0] == '.') continue;
        const std::string base = std::string("/proc/self/task/") + e->d_name;
        Hilo h;
        h.id = std::stoull(e->d_name);
        std::ifstream(base + "/comm") >> h.nombre;
        std::ifstream st(base + "/stat");
        std::string linea;
        std::getline(st, linea);
        // Después del «(nombre)» viene el estado; utime y stime son los campos 14 y 15.
        const auto p = linea.rfind(')');
        if (p != std::string::npos) {
            std::istringstream ls(linea.substr(p + 2));
            std::string x;
            unsigned long long ut = 0, stt = 0;
            for (int i = 3; i <= 13 && ls >> x; ++i) {
            }
            ls >> ut >> stt;
            h.cpuSeg = (ut + stt) / tick;
        }
        out.push_back(std::move(h));
    }
    closedir(d);
    return out;
}
double cpuProceso() {
    double s = 0;
    for (const auto& h : hilos()) s += h.cpuSeg;
    return s;
}
double memoriaMb() {
    std::ifstream f("/proc/self/statm");
    long paginas = 0, rss = 0;
    f >> paginas >> rss;
    return rss * static_cast<double>(sysconf(_SC_PAGESIZE)) / 1048576.0;
}
struct Gpu {
    json medir() { return json::object(); }
};
#else
std::string pila(std::uint64_t) { return {}; }
std::vector<Hilo> hilos() { return {}; }
double cpuProceso() { return 0; }
double memoriaMb() { return 0; }
struct Gpu {
    json medir() { return json::object(); }
};
#endif

// --- El hilo que mide ------------------------------------------------------------

// Nunca se destruyen: el hilo (despegado) puede estar midiendo mientras el
// proceso sale y corre los destructores de lo estático.
std::mutex& gMu = *new std::mutex;
json& gUltimo = *new json(json::object());
std::atomic<bool> gParar{false};
std::thread gHilo;
std::atomic<bool> gArrancado{false};

void medir(std::string proceso, std::filesystem::path logs) {
    std::error_code ec;
    std::filesystem::create_directories(logs, ec);
    const std::filesystem::path archivo = logs / ("perfil-" + proceso + ".jsonl");
    std::ofstream out(archivo, std::ios::app);
    Gpu gpu;
    std::map<std::uint64_t, double> antes;
    std::map<std::uint64_t, std::string> pilas;
    double cpuAntes = cpuProceso();
    auto tAntes = Reloj::now();
    int vuelta = 0;
    while (!gParar) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        const auto t = Reloj::now();
        const double dt = std::chrono::duration<double>(t - tAntes).count();
        tAntes = t;
        const std::vector<Hilo> hs = hilos();
        const double cpu = cpuProceso();
        json lista = json::array();
        std::vector<std::pair<double, const Hilo*>> orden;
        std::map<std::uint64_t, double> ahora;
        for (const Hilo& h : hs) {
            ahora[h.id] = h.cpuSeg;
            const auto a = antes.find(h.id);
            const double uso = a == antes.end() ? 0 : (h.cpuSeg - a->second) / dt;
            if (uso > 0.005) orden.emplace_back(uso, &h);
        }
        antes = std::move(ahora);
        std::sort(orden.begin(), orden.end(), [](const auto& x, const auto& y) { return x.first > y.first; });
        // La pila de los sin nombre que gastan, una vez por hilo y de nuevo cada 30 s.
        if (vuelta % 30 == 0) pilas.clear();
        for (size_t i = 0; i < orden.size() && i < 25; ++i) {
            const Hilo& h = *orden[i].second;
            json fila{{"nombre", h.nombre.empty() ? "(sin nombre)" : limpio(h.nombre)},
                      {"tid", h.id},
                      {"cpu", std::round(orden[i].first * 1000) / 1000}};
            if (h.nombre.empty() && orden[i].first >= 0.015) {
                auto p = pilas.find(h.id);
                if (p == pilas.end()) p = pilas.emplace(h.id, limpio(pila(h.id))).first;
                if (!p->second.empty()) fila["pila"] = p->second;
            }
            lista.push_back(std::move(fila));
        }
        json m{{"t", std::chrono::duration_cast<std::chrono::milliseconds>(
                         std::chrono::system_clock::now().time_since_epoch())
                         .count()},
               {"proceso", proceso},
               {"cpu_nucleos", std::round((cpu - cpuAntes) / dt * 100) / 100},
               {"memoria_mb", std::round(memoriaMb())},
               {"hilos_total", hs.size()},
               {"hilos", lista},
               {"gpu", gpu.medir()}};
        cpuAntes = cpu;
        out << m.dump(-1, ' ', false, json::error_handler_t::replace) << "\n";
        out.flush();
        // Rota a los 50 MB (se queda el último).
        if (std::filesystem::file_size(archivo, ec) > 50u * 1024 * 1024) {
            out.close();
            std::filesystem::rename(archivo, archivo.string() + ".1", ec);
            out.open(archivo, std::ios::trunc);
        }
        if (++vuelta % 10 == 0) {
            std::string resumen =
                i18n::llenar(i18n::traducir("perfil", "perfil %1: %2 núcleos, %3 MB"),
                             {proceso, m["cpu_nucleos"].dump(-1, ' ', false, nlohmann::json::error_handler_t::replace),
                              m["memoria_mb"].dump(-1, ' ', false, nlohmann::json::error_handler_t::replace)});
            for (size_t i = 0; i < lista.size() && i < 5; ++i)
                resumen += " | " + lista[i]["nombre"].get<std::string>() + " " + lista[i]["cpu"].dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
            if (!m["gpu"].empty()) resumen += " | gpu " + m["gpu"].dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
            registro().linea(Nivel::Info, "perfil", 0, resumen);
        }
        std::lock_guard<std::mutex> l(gMu);
        gUltimo = std::move(m);
    }
}

} // namespace

bool Perfil::activo() {
#ifdef BA_PERFILAR
    return true;
#else
    return false;
#endif
}

void Perfil::arrancar(const std::string& proceso, const std::filesystem::path& logsDir) {
    if (!activo() || gArrancado) return;
    // Y las mediciones del motor (cuadros y huecos de cada vista: Composicion.cpp).
    if (std::getenv("BA_MEDIR_VISTAS") == nullptr) {
#ifdef _WIN32
        _putenv_s("BA_MEDIR_VISTAS", "1");
#else
        setenv("BA_MEDIR_VISTAS", "1", 0);
#endif
    }
    registro().linea(Nivel::Aviso, "perfil", 0,
                     i18n::llenar(i18n::traducir("perfil", "build con BA_PERFILAR: se mide la CPU por hilo y la GPU cada "
                                                           "segundo (%1)"),
                                  {"Logs/perfil-" + proceso + ".jsonl"}));
    // El hilo que llama (el principal del proceso: arrancar se llama desde main) con
    // nombre: Qt no se lo pone y salía «(sin nombre)», que es justo el que importa.
#if defined(_WIN32)
    using Poner = HRESULT(WINAPI*)(HANDLE, PCWSTR);
    if (const auto poner = reinterpret_cast<Poner>(reinterpret_cast<void*>(
            GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "SetThreadDescription"))))
        poner(GetCurrentThread(), L"principal");
#elif defined(__APPLE__)
    pthread_setname_np("principal");
#elif defined(__linux__)
    pthread_setname_np(pthread_self(), "principal");
#endif
    gHilo = std::thread(medir, proceso, logsDir);
    // Mide hasta que el proceso termina: un std::thread estático unido al salir haría abortar.
    gHilo.detach();
    gArrancado = true;
}

json Perfil::ultimo() {
    std::lock_guard<std::mutex> l(gMu);
    return gUltimo;
}

void Perfil::parar() { gParar = true; }

} // namespace ba::diag
