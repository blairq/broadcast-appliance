// ============================================================================
// src/app/term/Pty_win.cpp — Pty con ConPTY (Windows 10 1809+)
// ============================================================================

#include "term/Pty.hpp"

#include <QDir>
#include <QMetaObject>
#include <QProcessEnvironment>
#include <QRegularExpression>

#include <atomic>
#include <thread>
#include <vector>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace ba::app::term {

struct Pty::Impl {
    HPCON pc = nullptr;
    HANDLE inWrite = INVALID_HANDLE_VALUE; // nosotros -> programa
    HANDLE outRead = INVALID_HANDLE_VALUE; // programa -> nosotros
    PROCESS_INFORMATION pi{};
    std::thread reader;
    std::thread waiter;
    std::atomic<bool> alive{false};
};

namespace {

std::wstring quoteArg(const QString& a) {
    if (!a.isEmpty() && !a.contains(QRegularExpression("[\\s\"]"))) return a.toStdWString();
    std::wstring out = L"\"";
    int slashes = 0;
    for (const QChar c : a) {
        if (c == '\\') {
            ++slashes;
        } else if (c == '"') {
            out.append(static_cast<size_t>(slashes * 2 + 1), L'\\');
            out.push_back(L'"');
            slashes = 0;
            continue;
        } else {
            slashes = 0;
        }
        out.push_back(static_cast<wchar_t>(c.unicode()));
    }
    out.append(static_cast<size_t>(slashes * 2), L'\\');
    out.push_back(L'"');
    return out;
}

} // namespace

Pty::Pty(QObject* parent) : QObject(parent), d_(std::make_unique<Impl>()) {}

Pty::~Pty() { terminate(); }

bool Pty::start(const Options& opt, QString* error) {
    terminate();
    HANDLE inRead = INVALID_HANDLE_VALUE;
    HANDLE outWrite = INVALID_HANDLE_VALUE;
    if (!CreatePipe(&inRead, &d_->inWrite, nullptr, 0) || !CreatePipe(&d_->outRead, &outWrite, nullptr, 0)) {
        if (error != nullptr) *error = "CreatePipe";
        return false;
    }
    COORD size{static_cast<SHORT>(opt.cols), static_cast<SHORT>(opt.rows)};
    if (FAILED(CreatePseudoConsole(size, inRead, outWrite, 0, &d_->pc))) {
        if (error != nullptr) *error = "CreatePseudoConsole (hace falta Windows 10 1809 o posterior)";
        CloseHandle(inRead);
        CloseHandle(outWrite);
        return false;
    }
    CloseHandle(inRead);
    CloseHandle(outWrite);

    STARTUPINFOEXW si{};
    si.StartupInfo.cb = sizeof(si);
    SIZE_T attrSize = 0;
    InitializeProcThreadAttributeList(nullptr, 1, 0, &attrSize);
    std::vector<char> attrBuf(attrSize);
    si.lpAttributeList = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attrBuf.data());
    InitializeProcThreadAttributeList(si.lpAttributeList, 1, 0, &attrSize);
    UpdateProcThreadAttribute(si.lpAttributeList, 0, PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE, d_->pc,
                              sizeof(d_->pc), nullptr, nullptr);

    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    for (const QString& k : opt.unsetEnv)
        env.remove(k);
    for (auto it = opt.env.begin(); it != opt.env.end(); ++it)
        env.insert(it.key(), it.value());
    env.insert("COLORTERM", "truecolor");
    std::wstring envBlock;
    for (const QString& kv : env.toStringList()) {
        envBlock += kv.toStdWString();
        envBlock.push_back(L'\0');
    }
    envBlock.push_back(L'\0');

    std::wstring cmd = quoteArg(QDir::toNativeSeparators(opt.program));
    for (const QString& a : opt.args)
        cmd += L" " + quoteArg(a);
    const std::wstring cwd = QDir::toNativeSeparators(opt.workDir).toStdWString();
    const BOOL ok = CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE,
                                   EXTENDED_STARTUPINFO_PRESENT | CREATE_UNICODE_ENVIRONMENT, envBlock.data(),
                                   cwd.empty() ? nullptr : cwd.c_str(), &si.StartupInfo, &d_->pi);
    DeleteProcThreadAttributeList(si.lpAttributeList);
    if (!ok) {
        if (error != nullptr) *error = QString("CreateProcess: error %1").arg(GetLastError());
        terminate();
        return false;
    }
    d_->alive = true;
    HANDLE outRead = d_->outRead;
    d_->reader = std::thread([this, outRead]() {
        char buf[65536];
        DWORD n = 0;
        while (ReadFile(outRead, buf, sizeof(buf), &n, nullptr) && n > 0) {
            const QByteArray chunk(buf, static_cast<qsizetype>(n));
            QMetaObject::invokeMethod(this, [this, chunk]() { emit output(chunk); }, Qt::QueuedConnection);
        }
    });
    HANDLE proc = d_->pi.hProcess;
    d_->waiter = std::thread([this, proc]() {
        WaitForSingleObject(proc, INFINITE);
        DWORD code = 0;
        GetExitCodeProcess(proc, &code);
        QMetaObject::invokeMethod(
            this,
            [this, code]() {
                if (!d_->alive.exchange(false)) return;
                emit finished(static_cast<int>(code));
            },
            Qt::QueuedConnection);
    });
    return true;
}

void Pty::write(const QByteArray& data) {
    if (d_->inWrite == INVALID_HANDLE_VALUE) return;
    DWORD n = 0;
    WriteFile(d_->inWrite, data.constData(), static_cast<DWORD>(data.size()), &n, nullptr);
}

void Pty::resize(int cols, int rows) {
    if (d_->pc != nullptr && cols > 0 && rows > 0)
        ResizePseudoConsole(d_->pc, COORD{static_cast<SHORT>(cols), static_cast<SHORT>(rows)});
}

void Pty::terminate() {
    const bool wasAlive = d_->alive.exchange(false);
    if (d_->pi.hProcess != nullptr) {
        TerminateProcess(d_->pi.hProcess, 1);
    }
    if (d_->pc != nullptr) {
        ClosePseudoConsole(d_->pc); // cierra la salida: el lector termina
        d_->pc = nullptr;
    }
    if (d_->inWrite != INVALID_HANDLE_VALUE) CloseHandle(d_->inWrite);
    d_->inWrite = INVALID_HANDLE_VALUE;
    if (d_->reader.joinable()) d_->reader.join();
    if (d_->waiter.joinable()) d_->waiter.join();
    if (d_->outRead != INVALID_HANDLE_VALUE) CloseHandle(d_->outRead);
    d_->outRead = INVALID_HANDLE_VALUE;
    if (d_->pi.hProcess != nullptr) CloseHandle(d_->pi.hProcess);
    if (d_->pi.hThread != nullptr) CloseHandle(d_->pi.hThread);
    d_->pi = {};
    if (wasAlive) emit finished(1);
}

bool Pty::running() const { return d_->alive; }

qint64 Pty::pid() const { return d_->pi.dwProcessId; }

} // namespace ba::app::term

#include "moc_Pty.cpp"
