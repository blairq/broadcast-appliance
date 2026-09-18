// ============================================================================
// src/app/WindowingPlatform.cpp — ver WindowingPlatform.hpp
// ============================================================================

#include "WindowingPlatform.hpp"

#if defined(__linux__) || defined(__FreeBSD__)

#include <obs-nix-platform.h>
#include <obs.h>

#include <wayland-client.h>
#include <X11/Xlib.h>

#include <cstdlib>

namespace ba::app {

bool selectWindowingPlatform(void*& display, bool& wayland, std::string& err) {
    display = nullptr;
    wayland = false;
    const char* forced = std::getenv("BA_OBS_PLATFORM");
    const std::string want = forced != nullptr ? forced : "";
    const char* wl = std::getenv("WAYLAND_DISPLAY");
    const char* x = std::getenv("DISPLAY");

    if (want != "x11" && wl != nullptr && *wl != '\0') {
        if (wl_display* d = wl_display_connect(nullptr)) {
            obs_set_nix_platform(OBS_NIX_PLATFORM_WAYLAND);
            obs_set_nix_platform_display(d);
            display = d;
            wayland = true;
            blog(LOG_INFO, "ba-engine: plataforma de ventanas Wayland (%s)", wl);
            return true;
        }
        blog(LOG_WARNING, "ba-engine: no se pudo conectar a Wayland (%s); se prueba X11", wl);
    }
    if (want != "wayland" && x != nullptr && *x != '\0') {
        if (Display* d = XOpenDisplay(nullptr)) {
            obs_set_nix_platform(OBS_NIX_PLATFORM_X11_EGL);
            obs_set_nix_platform_display(d);
            display = d;
            blog(LOG_INFO, "ba-engine: plataforma de ventanas X11 (%s)", x);
            return true;
        }
    }
    err =
        "sin sesion grafica: no hay DISPLAY ni WAYLAND_DISPLAY utilizables (el video de libobs necesita GL)";
    return false;
}

void releaseWindowingPlatform(void* display, bool wayland) {
    if (display == nullptr) {
        return;
    }
    if (wayland) {
        wl_display_disconnect(static_cast<wl_display*>(display));
    } else {
        XCloseDisplay(static_cast<Display*>(display));
    }
}

} // namespace ba::app

#else

namespace ba::app {

bool selectWindowingPlatform(void*& display, bool& wayland, std::string&) {
    display = nullptr;
    wayland = false;
    return true; // Windows (D3D11) y macOS no eligen plataforma de ventanas
}

void releaseWindowingPlatform(void*, bool) {}

} // namespace ba::app

#endif
