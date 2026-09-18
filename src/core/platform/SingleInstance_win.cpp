// ============================================================================
// src/core/platform/SingleInstance_win.cpp — mutex con nombre (Windows)
// ============================================================================

#include "platform/SingleInstance.hpp"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace ba::platform {

SingleInstance::~SingleInstance() { release(); }

SingleInstance::SingleInstance(SingleInstance&& other) noexcept : mutex_(other.mutex_) {
    other.mutex_ = nullptr;
}

SingleInstance& SingleInstance::operator=(SingleInstance&& other) noexcept {
    if (this != &other) {
        release();
        mutex_ = other.mutex_;
        other.mutex_ = nullptr;
    }
    return *this;
}

bool SingleInstance::acquire(const std::string& name, SingleInstance& out, std::string* err) {
    out.release();
    std::wstring full = L"Local\\BroadcastAppliance.";
    for (char c : name) {
        full.push_back(static_cast<wchar_t>(c));
    }
    HANDLE h = CreateMutexW(nullptr, TRUE, full.c_str());
    if (h == nullptr) {
        if (err != nullptr) {
            *err = "CreateMutexW fallo (GetLastError=" + std::to_string(GetLastError()) + ")";
        }
        return false;
    }
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        CloseHandle(h);
        if (err != nullptr) {
            *err = "ya hay otra instancia en ejecucion";
        }
        return false;
    }
    out.mutex_ = h;
    return true;
}

bool SingleInstance::held() const { return mutex_ != nullptr; }

void SingleInstance::release() {
    if (mutex_ != nullptr) {
        ReleaseMutex(static_cast<HANDLE>(mutex_));
        CloseHandle(static_cast<HANDLE>(mutex_));
        mutex_ = nullptr;
    }
}

} // namespace ba::platform
