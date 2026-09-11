// ChildProcess.cpp
//
// Implementacion del lanzamiento de procesos hijos (ver ChildProcess.hpp).
// Win32 real bajo _WIN32; fuera de Windows es un stub que loguea y "arranca"
// un hijo ficticio para que el arbol compile y el flujo se pueda probar.

#include "ChildProcess.hpp"

#include "Log.hpp"

#ifdef _WIN32
#include <vector>
#endif

namespace ba::launcher {

namespace {

#ifdef _WIN32
// Construye un bloque de entorno saneado (spec 3.1.3): sin QT_PLUGIN_PATH,
// QT_QPA_PLATFORM_PLUGIN_PATH, QT_QPA_PLATFORM, QML2_IMPORT_PATH, PYTHONHOME,
// PYTHONPATH; PATH minimo = System32 + directorios propios.
std::wstring buildSanitizedEnv(const std::wstring& ownDir) {
    std::wstring block;
    auto appendVar = [&block](const wchar_t* name, const std::wstring& value) {
        if (value.empty()) {
            return;
        }
        block += name;
        block += L"=";
        block += value;
        block += L'\0';
    };

    std::wstring systemRoot = L"C:\\Windows";
    {
        const int n = GetEnvironmentVariableW(L"SystemRoot", nullptr, 0);
        if (n > 0) {
            std::vector<wchar_t> buf(static_cast<size_t>(n));
            GetEnvironmentVariableW(L"SystemRoot", buf.data(), n);
            systemRoot.assign(buf.data(), n - 1);
        }
    }
    const std::wstring system32 = systemRoot + L"\\system32";

    appendVar(L"SystemRoot", systemRoot);
    appendVar(L"SystemDrive", systemRoot.substr(0, 2));
    appendVar(L"PATH", system32 + L";" + ownDir);
    appendVar(L"TEMP", L"%TEMP%"); // se resuelve abajo si existe
    appendVar(L"TMP", L"%TEMP%");

    // Laptops hibridas: forzar GPU de alto rendimiento (spec 4.2).
    appendVar(L"NvOptimusEnablement", L"1");
    appendVar(L"AmdPowerXpressRequestHighPerformance", L"1");

    // TEMP/TMP reales si existen (evita dejar la literal %TEMP%).
    {
        const int n = GetEnvironmentVariableW(L"TEMP", nullptr, 0);
        if (n > 0) {
            std::vector<wchar_t> buf(static_cast<size_t>(n));
            GetEnvironmentVariableW(L"TEMP", buf.data(), n);
            const std::wstring temp(buf.data(), n - 1);
            // Reemplaza los dos %TEMP% por el valor real.
            const std::wstring fixed = (systemRoot + L"\\system32;" + ownDir); // PATH ya puesto
            (void)fixed;
            block += L"TEMP=" + temp + L"\0";
            block += L"TMP=" + temp + L"\0";
        }
    }
    block += L'\0'; // doble NUL final.
    return block;
}
#endif

} // namespace

ChildProcess::~ChildProcess() { close(); }

void ChildProcess::close() {
#ifdef _WIN32
    if (hProcess_) {
        CloseHandle(hProcess_);
        hProcess_ = nullptr;
    }
#endif
    pid_ = 0;
}

bool ChildProcess::alive() const {
#ifdef _WIN32
    if (!hProcess_) {
        return false;
    }
    return WaitForSingleObject(static_cast<HANDLE>(hProcess_), 0) == WAIT_TIMEOUT;
#else
    (void)hProcess_;
    return pid_ != 0;
#endif
}

std::uint32_t ChildProcess::pid() const { return pid_; }

void* ChildProcess::handle() const { return hProcess_; }

bool ChildProcess::attach(std::uint32_t pid) {
    close();
    pid_ = pid;
#ifdef _WIN32
    hProcess_ = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE, FALSE, pid);
    if (!hProcess_) {
        pid_ = 0;
        return false;
    }
    return true;
#else
    return pid != 0;
#endif
}

std::uint32_t ChildProcess::exitCode() const {
#ifdef _WIN32
    if (!hProcess_) {
        return 0;
    }
    DWORD code = 0;
    if (GetExitCodeProcess(static_cast<HANDLE>(hProcess_), &code)) {
        return code;
    }
    return 0;
#else
    (void)hProcess_;
    return 0;
#endif
}

bool ChildProcess::start(const Config& cfg) {
    name_ = cfg.name;
    close();

#ifndef _WIN32
    // Stub no-Windows: simula un hijo vivo para poder probar el flujo.
    pid_ = 1000 + static_cast<std::uint32_t>(cfg.name.size());
    BA_LOG_INFO("child", 0,
                "stub: hijo '" + std::string(cfg.name.begin(), cfg.name.end()) + "' simulado (no-Win)");
    return true;
#else
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE; // GUI, sin consola visible.

    PROCESS_INFORMATION pi{};
    std::wstring cmdLine = L"\"" + cfg.exePath + L"\"";
    if (!cfg.args.empty()) {
        cmdLine += L" " + cfg.args;
    }
    std::vector<wchar_t> cmdBuf(cmdLine.begin(), cmdLine.end());
    cmdBuf.push_back(L'\0');

    const std::wstring envBlock = buildSanitizedEnv(cfg.workDir);
    std::vector<wchar_t> envBuf(envBlock.begin(), envBlock.end());
    // Asegura doble NUL final.
    if (envBuf.empty() || envBuf.back() != L'\0') {
        envBuf.push_back(L'\0');
    }

    const DWORD baseFlags = CREATE_UNICODE_ENVIRONMENT | CREATE_NO_WINDOW;
    const DWORD attempts[2] = {baseFlags | CREATE_BREAKAWAY_FROM_JOB, baseFlags};

    for (DWORD flags : attempts) {
        BOOL ok =
            CreateProcessW(cfg.exePath.c_str(), cmdBuf.data(), nullptr, nullptr, FALSE, flags, envBuf.data(),
                           cfg.workDir.empty() ? nullptr : cfg.workDir.c_str(), &si, &pi);
        if (ok) {
            hProcess_ = pi.hProcess;
            pid_ = static_cast<std::uint32_t>(pi.dwProcessId);
            if (pi.hThread) {
                CloseHandle(pi.hThread);
            }
            BA_LOG_INFO("child", 0,
                        "hijo '" + std::string(cfg.name.begin(), cfg.name.end()) +
                            "' lanzado pid=" + std::to_string(pid_) + " flags=0x" + std::to_string(flags));
            return true;
        }
        const DWORD err = GetLastError();
        BA_LOG_WARN("child", static_cast<int>(err),
                    "CreateProcessW fallo para '" + std::string(cfg.name.begin(), cfg.name.end()) +
                        "' (flags=0x" + std::to_string(flags) + "); reintento");
    }
    BA_LOG_ERROR("child", static_cast<int>(GetLastError()),
                 "no se pudo lanzar '" + std::string(cfg.name.begin(), cfg.name.end()) + "'");
    return false;
#endif
}

} // namespace ba::launcher
