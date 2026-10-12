// ============================================================================
// src/core/platform/Process_win.cpp — Procesos hijos en Windows
//
// CreateProcessW con:
//   - bloque de entorno UNICODE armado a mano (spec §3.1.3: sin variables Qt/
//     Python heredadas, PATH minimo),
//   - un Job Object por hijo con KILL_ON_JOB_CLOSE: si el padre muere (crash
//     incluido) el SO cierra el handle del job y mata al hijo y sus nietos,
//   - el hijo se crea SUSPENDIDO, se mete en el job y recien ahi se reanuda:
//     sin eso un nieto lanzado en el primer instante escaparia del job,
//   - lista explicita de handles heredables (vacia): el hijo no se lleva
//     handles del padre aunque sean heredables.
// ============================================================================

#include "platform/Process.hpp"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <algorithm>
#include <cwctype>
#include <set>

namespace ba::platform {

namespace {

std::wstring widen(const std::string& s) {
    if (s.empty()) {
        return {};
    }
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring out(static_cast<std::size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), out.data(), n);
    return out;
}

std::wstring upper(std::wstring s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](wchar_t c) { return static_cast<wchar_t>(std::towupper(c)); });
    return s;
}

/// Cita un argumento con las reglas de CommandLineToArgvW / el CRT de MSVC:
/// las barras invertidas solo son especiales delante de una comilla.
void appendQuoted(std::wstring& cmd, const std::wstring& arg) {
    if (!arg.empty() && arg.find_first_of(L" \t\n\v\"") == std::wstring::npos) {
        cmd += arg;
        return;
    }
    cmd.push_back(L'"');
    for (auto it = arg.begin();; ++it) {
        std::size_t backslashes = 0;
        while (it != arg.end() && *it == L'\\') {
            ++it;
            ++backslashes;
        }
        if (it == arg.end()) {
            cmd.append(backslashes * 2, L'\\');
            break;
        }
        if (*it == L'"') {
            cmd.append(backslashes * 2 + 1, L'\\');
            cmd.push_back(*it);
        } else {
            cmd.append(backslashes, L'\\');
            cmd.push_back(*it);
        }
    }
    cmd.push_back(L'"');
}

/// Bloque de entorno UNICODE ordenado (Windows lo exige ordenado sin
/// distinguir mayusculas) y doble-NUL terminado.
std::wstring buildEnvBlock(const ProcessOptions& opts) {
    std::set<std::wstring> overridden;
    for (const auto& n : opts.envRemove) {
        overridden.insert(upper(widen(n)));
    }
    for (const auto& kv : opts.envSet) {
        overridden.insert(upper(widen(kv.first)));
    }
    std::vector<std::wstring> vars;
    if (opts.inheritEnv) {
        if (wchar_t* block = GetEnvironmentStringsW()) {
            for (const wchar_t* p = block; *p != L'\0'; p += wcslen(p) + 1) {
                const std::wstring entry(p);
                // Las entradas "=C:=C:\..." (cwd por unidad) empiezan con '='.
                const std::size_t eq = entry.find(L'=', 1);
                const std::wstring name = eq == std::wstring::npos ? entry : entry.substr(0, eq);
                if (overridden.count(upper(name)) == 0) {
                    vars.push_back(entry);
                }
            }
            FreeEnvironmentStringsW(block);
        }
    }
    for (const auto& kv : opts.envSet) {
        vars.push_back(widen(kv.first) + L"=" + widen(kv.second));
    }
    std::sort(vars.begin(), vars.end(),
              [](const std::wstring& a, const std::wstring& b) { return upper(a) < upper(b); });
    std::wstring block;
    for (const auto& v : vars) {
        block += v;
        block.push_back(L'\0');
    }
    block.push_back(L'\0');
    if (vars.empty()) {
        block.push_back(L'\0');
    }
    return block;
}

void setErr(std::string* err, const char* what) {
    if (err != nullptr) {
        *err = std::string(what) + " fallo (GetLastError=" + std::to_string(GetLastError()) + ")";
    }
}

} // namespace

Process::~Process() { reset(); }

Process::Process(Process&& other) noexcept
    : pid_(other.pid_), exitCode_(other.exitCode_), adopted_(other.adopted_),
      killOnDestroy_(other.killOnDestroy_), process_(other.process_), job_(other.job_) {
    other.pid_ = 0;
    other.exitCode_.reset();
    other.process_ = nullptr;
    other.job_ = nullptr;
}

Process& Process::operator=(Process&& other) noexcept {
    if (this != &other) {
        reset();
        pid_ = other.pid_;
        exitCode_ = other.exitCode_;
        adopted_ = other.adopted_;
        killOnDestroy_ = other.killOnDestroy_;
        process_ = other.process_;
        job_ = other.job_;
        other.pid_ = 0;
        other.exitCode_.reset();
        other.process_ = nullptr;
        other.job_ = nullptr;
    }
    return *this;
}

void Process::reset() {
    // Cerrar el job con KILL_ON_JOB_CLOSE mata al hijo si sigue vivo: mismo
    // contrato que la version POSIX.
    if (job_ != nullptr) {
        CloseHandle(static_cast<HANDLE>(job_));
        job_ = nullptr;
    }
    if (process_ != nullptr) {
        CloseHandle(static_cast<HANDLE>(process_));
        process_ = nullptr;
    }
    pid_ = 0;
    exitCode_.reset();
    adopted_ = false;
    killOnDestroy_ = false;
}

bool Process::attach(std::uint32_t pid, Process& out) {
    out.reset();
    HANDLE h = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_TERMINATE, FALSE, pid);
    if (h == nullptr) {
        return false;
    }
    DWORD code = 0;
    if (!GetExitCodeProcess(h, &code) || code != STILL_ACTIVE) {
        CloseHandle(h);
        return false;
    }
    out.pid_ = pid;
    out.process_ = h;
    out.adopted_ = true;
    return true;
}

bool Process::spawn(const ProcessOptions& opts, Process& out, std::string* err) {
    out.reset();

    std::wstring cmd;
    appendQuoted(cmd, opts.exe.wstring());
    for (const auto& a : opts.args) {
        cmd.push_back(L' ');
        appendQuoted(cmd, widen(a));
    }
    std::vector<wchar_t> cmdBuf(cmd.begin(), cmd.end());
    cmdBuf.push_back(L'\0');
    std::wstring env = buildEnvBlock(opts);

    HANDLE job = CreateJobObjectW(nullptr, nullptr);
    if (job == nullptr) {
        setErr(err, "CreateJobObjectW");
        return false;
    }
    if (opts.killWithParent) {
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION info{};
        info.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        SetInformationJobObject(job, JobObjectExtendedLimitInformation, &info, sizeof(info));
    }

    // Lista de atributos con 0 handles heredables: bInheritHandles=TRUE es
    // necesario para los std handles, pero la lista limita QUE se hereda.
    SIZE_T attrSize = 0;
    InitializeProcThreadAttributeList(nullptr, 1, 0, &attrSize);
    std::vector<unsigned char> attrBuf(attrSize);
    auto* attrs = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attrBuf.data());
    InitializeProcThreadAttributeList(attrs, 1, 0, &attrSize);
    // Archivo de salida: handle HEREDABLE en modo agregar (FILE_APPEND_DATA:
    // no pisa lo que ya habia).
    HANDLE outFile = nullptr;
    if (!opts.outputFile.empty()) {
        SECURITY_ATTRIBUTES sa{};
        sa.nLength = sizeof(sa);
        sa.bInheritHandle = TRUE;
        outFile =
            CreateFileW(opts.outputFile.wstring().c_str(), FILE_APPEND_DATA | SYNCHRONIZE,
                        FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (outFile == INVALID_HANDLE_VALUE) {
            setErr(err, "CreateFileW(outputFile)");
            DeleteProcThreadAttributeList(attrs);
            CloseHandle(job);
            return false;
        }
    }
    // La entrada estándar con datos (stdinData): un pipe; el hijo hereda solo el lado de leer.
    HANDLE inRd = nullptr, inWr = nullptr;
    if (!opts.stdinData.empty()) {
        SECURITY_ATTRIBUTES sa{};
        sa.nLength = sizeof(sa);
        sa.bInheritHandle = TRUE;
        if (!CreatePipe(&inRd, &inWr, &sa, 1 << 16)) {
            setErr(err, "CreatePipe(stdin)");
            if (outFile != nullptr) CloseHandle(outFile);
            DeleteProcThreadAttributeList(attrs);
            CloseHandle(job);
            return false;
        }
        SetHandleInformation(inWr, HANDLE_FLAG_INHERIT, 0);
    }
    HANDLE inherit[4];
    DWORD nInherit = 0;
    if (outFile != nullptr) {
        inherit[nInherit++] = outFile;
    }
    if (inRd != nullptr) {
        inherit[nInherit++] = inRd;
    }
    for (DWORD std : {STD_INPUT_HANDLE, STD_OUTPUT_HANDLE, STD_ERROR_HANDLE}) {
        if (outFile != nullptr && std != STD_INPUT_HANDLE) {
            continue; // stdout/stderr van al archivo
        }
        if (inRd != nullptr && std == STD_INPUT_HANDLE) {
            continue; // la entrada es el pipe
        }
        HANDLE h = GetStdHandle(std);
        DWORD flags = 0;
        if (h != nullptr && h != INVALID_HANDLE_VALUE && GetHandleInformation(h, &flags) &&
            (flags & HANDLE_FLAG_INHERIT) != 0 &&
            std::find(inherit, inherit + nInherit, h) == inherit + nInherit) {
            inherit[nInherit++] = h;
        }
    }
    if (nInherit > 0) {
        UpdateProcThreadAttribute(attrs, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, inherit,
                                  nInherit * sizeof(HANDLE), nullptr, nullptr);
    }

    STARTUPINFOEXW si{};
    si.StartupInfo.cb = sizeof(si);
    // Sin STARTF_USESHOWWINDOW a proposito. Ese campo no es "ocultar la
    // consola": es el nCmdShow que el hijo hereda, y Windows se lo aplica a la
    // PRIMERA ventana que muestre. Con SW_HIDE, la ventana de A se creaba bien
    // (posicionada, showCmd=SW_SHOWNORMAL) pero sin WS_VISIBLE: el dashboard
    // no aparecia nunca al abrir desde el launcher. La consola la suprime
    // CREATE_NO_WINDOW, mas abajo, que es lo que `hideWindow` quiere decir.
    if (outFile != nullptr || inRd != nullptr) {
        si.StartupInfo.dwFlags |= STARTF_USESTDHANDLES;
        si.StartupInfo.hStdInput = inRd != nullptr ? inRd : GetStdHandle(STD_INPUT_HANDLE);
        si.StartupInfo.hStdOutput = outFile != nullptr ? outFile : GetStdHandle(STD_OUTPUT_HANDLE);
        si.StartupInfo.hStdError = outFile != nullptr ? outFile : GetStdHandle(STD_ERROR_HANDLE);
    }
    PROCESS_INFORMATION pi{};
    const std::wstring workDir = opts.workDir.wstring();
    const DWORD flags = CREATE_SUSPENDED | CREATE_UNICODE_ENVIRONMENT | EXTENDED_STARTUPINFO_PRESENT |
                        (opts.hideWindow ? CREATE_NO_WINDOW : 0);
    const BOOL ok = CreateProcessW(opts.exe.wstring().c_str(), cmdBuf.data(), nullptr, nullptr,
                                   nInherit > 0 ? TRUE : FALSE, flags, env.data(),
                                   workDir.empty() ? nullptr : workDir.c_str(), &si.StartupInfo, &pi);
    DeleteProcThreadAttributeList(attrs);
    if (outFile != nullptr) {
        CloseHandle(outFile); // el hijo tiene su copia
    }
    if (inRd != nullptr) {
        CloseHandle(inRd); // ídem
    }
    if (!ok) {
        setErr(err, "CreateProcessW");
        if (inWr != nullptr) CloseHandle(inWr);
        CloseHandle(job);
        return false;
    }
    if (!AssignProcessToJobObject(job, pi.hProcess)) {
        setErr(err, "AssignProcessToJobObject");
        if (inWr != nullptr) CloseHandle(inWr);
        TerminateProcess(pi.hProcess, 1);
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        CloseHandle(job);
        return false;
    }
    ResumeThread(pi.hThread);
    CloseHandle(pi.hThread);
    if (inWr != nullptr) {
        // entra entero en el pipe (64 KiB): no espera al hijo; al cerrar, el hijo lee el fin
        DWORD escritos = 0;
        WriteFile(inWr, opts.stdinData.data(), static_cast<DWORD>(opts.stdinData.size()), &escritos, nullptr);
        CloseHandle(inWr);
    }

    out.pid_ = pi.dwProcessId;
    out.process_ = pi.hProcess;
    out.job_ = job;
    out.killOnDestroy_ = opts.killWithParent;
    return true;
}

std::optional<int> Process::wait(int timeoutMs) {
    if (exitCode_.has_value()) {
        return exitCode_;
    }
    if (process_ == nullptr) {
        return std::nullopt;
    }
    const DWORD ms = timeoutMs < 0 ? INFINITE : static_cast<DWORD>(timeoutMs);
    if (WaitForSingleObject(static_cast<HANDLE>(process_), ms) != WAIT_OBJECT_0) {
        return std::nullopt;
    }
    DWORD code = 0;
    GetExitCodeProcess(static_cast<HANDLE>(process_), &code);
    exitCode_ = static_cast<int>(code);
    return exitCode_;
}

bool Process::terminate() { return kill(); }

bool Process::kill() {
    if (!valid()) {
        return false;
    }
    // Al job entero: alcanza tambien a los nietos, como el kill al grupo POSIX.
    if (job_ != nullptr && TerminateJobObject(static_cast<HANDLE>(job_), 1)) {
        return true;
    }
    return TerminateProcess(static_cast<HANDLE>(process_), 1) != FALSE;
}

void exitWhenParentDies() {
    // Nada que hacer: el Job Object del padre (KILL_ON_JOB_CLOSE) ya mata a
    // este proceso si el padre muere.
}

void suppressCrashDialogs() { SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX); }

} // namespace ba::platform
