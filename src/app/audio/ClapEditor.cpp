// ============================================================================
// src/app/audio/ClapEditor.cpp — ver ClapEditor.hpp
// ============================================================================

#include "audio/ClapEditor.hpp"

#include <clap/clap.h>

#include "audio/ClapHost.hpp"

#ifdef _WIN32
#include <windows.h>
#else
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#endif

namespace ba::app::audio {

namespace {

const clap_plugin_gui_t* guiOf(const clap_plugin* p) {
    return static_cast<const clap_plugin_gui_t*>(p->get_extension(p, CLAP_EXT_GUI));
}

} // namespace

#ifdef _WIN32

struct ClapEditor::Native {
    HWND hwnd = nullptr;
};

namespace {

constexpr wchar_t kClass[] = L"BAClapEditor";

LRESULT CALLBACK wndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    auto* inst = reinterpret_cast<ClapInstance*>(GetWindowLongPtrW(h, GWLP_USERDATA));
    if (msg == WM_CLOSE && inst != nullptr) {
        inst->closeEditor(); // destruye la ventana
        return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

std::wstring widen(const std::string& s) {
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    std::wstring w(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, w.data(), n);
    return w;
}

} // namespace

std::unique_ptr<ClapEditor> ClapEditor::open(ClapInstance& inst, const std::string& title,
                                             std::string* error) {
    const clap_plugin* p = inst.plugin_;
    const clap_plugin_gui_t* gui = guiOf(p);
    if (gui == nullptr || !gui->is_api_supported(p, CLAP_WINDOW_API_WIN32, false)) {
        if (error != nullptr) *error = "este modulo no tiene editor grafico";
        return nullptr;
    }
    static bool registered = false;
    if (!registered) {
        WNDCLASSW wc{};
        wc.lpfnWndProc = wndProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = kClass;
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        RegisterClassW(&wc);
        registered = true;
    }
    std::unique_ptr<ClapEditor> ed(new ClapEditor());
    ed->inst_ = &inst;
    ed->n_ = std::make_unique<Native>();
    if (!gui->create(p, CLAP_WINDOW_API_WIN32, false)) {
        if (error != nullptr) *error = "el modulo no pudo crear su editor";
        return nullptr;
    }
    ed->created_ = true;
    uint32_t w = 800, h = 500;
    gui->get_size(p, &w, &h);
    RECT r{0, 0, static_cast<LONG>(w), static_cast<LONG>(h)};
    const DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    AdjustWindowRect(&r, style, FALSE);
    ed->n_->hwnd = CreateWindowExW(0, kClass, widen(title).c_str(), style, CW_USEDEFAULT, CW_USEDEFAULT,
                                   r.right - r.left, r.bottom - r.top, nullptr, nullptr,
                                   GetModuleHandleW(nullptr), nullptr);
    if (ed->n_->hwnd == nullptr) {
        if (error != nullptr) *error = "no se pudo crear la ventana";
        return nullptr;
    }
    SetWindowLongPtrW(ed->n_->hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(&inst));
    clap_window_t win{};
    win.api = CLAP_WINDOW_API_WIN32;
    win.win32 = ed->n_->hwnd;
    if (!gui->set_parent(p, &win)) {
        if (error != nullptr) *error = "el modulo no acepto la ventana";
        return nullptr;
    }
    gui->show(p);
    ShowWindow(ed->n_->hwnd, SW_SHOW);
    return ed;
}

ClapEditor::~ClapEditor() {
    if (created_) guiOf(inst_->plugin_)->destroy(inst_->plugin_);
    if (n_ && n_->hwnd != nullptr) {
        SetWindowLongPtrW(n_->hwnd, GWLP_USERDATA, 0);
        DestroyWindow(n_->hwnd);
    }
}

void ClapEditor::resize(uint32_t width, uint32_t height) {
    if (!n_ || n_->hwnd == nullptr) return;
    RECT r{0, 0, static_cast<LONG>(width), static_cast<LONG>(height)};
    AdjustWindowRect(&r, static_cast<DWORD>(GetWindowLongW(n_->hwnd, GWL_STYLE)), FALSE);
    SetWindowPos(n_->hwnd, nullptr, 0, 0, r.right - r.left, r.bottom - r.top, SWP_NOMOVE | SWP_NOZORDER);
    guiOf(inst_->plugin_)->set_size(inst_->plugin_, width, height);
}

void ClapEditor::raise() {
    if (n_ && n_->hwnd != nullptr) SetForegroundWindow(n_->hwnd);
}

#else // X11

struct ClapEditor::Native {
    Display* dpy = nullptr;
    Window win = 0;
    Atom wmDelete = 0;
    int fd = -1;
};

std::unique_ptr<ClapEditor> ClapEditor::open(ClapInstance& inst, const std::string& title,
                                             std::string* error) {
    const clap_plugin* p = inst.plugin_;
    const clap_plugin_gui_t* gui = guiOf(p);
    if (gui == nullptr || !gui->is_api_supported(p, CLAP_WINDOW_API_X11, false)) {
        if (error != nullptr) *error = "este modulo no tiene editor grafico";
        return nullptr;
    }
    if (inst.services_ == nullptr) {
        if (error != nullptr) *error = "sin servicios de interfaz";
        return nullptr;
    }
    std::unique_ptr<ClapEditor> ed(new ClapEditor());
    ed->inst_ = &inst;
    ed->n_ = std::make_unique<Native>();
    Native& n = *ed->n_;
    n.dpy = XOpenDisplay(nullptr);
    if (n.dpy == nullptr) {
        if (error != nullptr)
            *error = "no hay servidor X (DISPLAY): el editor de LSP necesita X11 o XWayland";
        return nullptr;
    }
    if (!gui->create(p, CLAP_WINDOW_API_X11, false)) {
        if (error != nullptr) *error = "el modulo no pudo crear su editor";
        return nullptr;
    }
    ed->created_ = true;
    uint32_t w = 800, h = 500;
    gui->get_size(p, &w, &h);
    const int screen = DefaultScreen(n.dpy);
    n.win = XCreateSimpleWindow(n.dpy, RootWindow(n.dpy, screen), 0, 0, w, h, 0, BlackPixel(n.dpy, screen),
                                BlackPixel(n.dpy, screen));
    XStoreName(n.dpy, n.win, title.c_str());
    // Titulo UTF-8 (tildes) para los gestores de ventanas modernos.
    const Atom netName = XInternAtom(n.dpy, "_NET_WM_NAME", False);
    const Atom utf8 = XInternAtom(n.dpy, "UTF8_STRING", False);
    XChangeProperty(n.dpy, n.win, netName, utf8, 8, PropModeReplace,
                    reinterpret_cast<const unsigned char*>(title.c_str()), static_cast<int>(title.size()));
    n.wmDelete = XInternAtom(n.dpy, "WM_DELETE_WINDOW", False);
    XSetWMProtocols(n.dpy, n.win, &n.wmDelete, 1);
    if (!gui->can_resize(p)) {
        XSizeHints* hints = XAllocSizeHints();
        hints->flags = PMinSize | PMaxSize;
        hints->min_width = hints->max_width = static_cast<int>(w);
        hints->min_height = hints->max_height = static_cast<int>(h);
        XSetWMNormalHints(n.dpy, n.win, hints);
        XFree(hints);
    }
    XSelectInput(n.dpy, n.win, StructureNotifyMask);
    XMapWindow(n.dpy, n.win);
    XFlush(n.dpy);

    clap_window_t win{};
    win.api = CLAP_WINDOW_API_X11;
    win.x11 = n.win;
    if (!gui->set_parent(p, &win)) {
        if (error != nullptr) *error = "el modulo no acepto la ventana";
        return nullptr;
    }
    gui->show(p);
    XFlush(n.dpy);

    // Eventos de la ventana (cerrar, cambiar de tamaño) en el hilo principal.
    n.fd = ConnectionNumber(n.dpy);
    ClapEditor* self = ed.get();
    inst.services_->addFd(n.fd, CLAP_POSIX_FD_READ, [self](uint32_t) {
        Native& nn = *self->n_;
        bool close = false;
        while (XPending(nn.dpy) > 0) {
            XEvent ev;
            XNextEvent(nn.dpy, &ev);
            if (ev.type == ClientMessage && static_cast<Atom>(ev.xclient.data.l[0]) == nn.wmDelete)
                close = true;
            if (ev.type == ConfigureNotify) {
                const clap_plugin_gui_t* g = guiOf(self->inst_->plugin_);
                uint32_t cw = static_cast<uint32_t>(ev.xconfigure.width);
                uint32_t ch = static_cast<uint32_t>(ev.xconfigure.height);
                if (g->can_resize(self->inst_->plugin_) && g->adjust_size(self->inst_->plugin_, &cw, &ch))
                    g->set_size(self->inst_->plugin_, cw, ch);
            }
        }
        if (close) {
            ClapInstance* inst = self->inst_;
            inst->services_->runOnMain([inst]() { inst->closeEditor(); });
        }
    });
    return ed;
}

ClapEditor::~ClapEditor() {
    if (created_) guiOf(inst_->plugin_)->destroy(inst_->plugin_);
    if (n_ && n_->dpy != nullptr) {
        if (n_->fd >= 0 && inst_->services_ != nullptr) inst_->services_->removeFd(n_->fd);
        if (n_->win != 0) XDestroyWindow(n_->dpy, n_->win);
        XCloseDisplay(n_->dpy);
    }
}

void ClapEditor::resize(uint32_t width, uint32_t height) {
    if (!n_ || n_->dpy == nullptr) return;
    XResizeWindow(n_->dpy, n_->win, width, height);
    XFlush(n_->dpy);
    guiOf(inst_->plugin_)->set_size(inst_->plugin_, width, height);
}

void ClapEditor::raise() {
    if (!n_ || n_->dpy == nullptr) return;
    XRaiseWindow(n_->dpy, n_->win);
    XFlush(n_->dpy);
}

#endif

} // namespace ba::app::audio
