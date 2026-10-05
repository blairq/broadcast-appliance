// SPDX-License-Identifier: GPL-2.0-or-later
//
// Atajos globales por sistema (ver Acciones.hpp):
//   macOS:   RegisterEventHotKey (Carbon). No pide permiso de Accesibilidad.
//   Windows: RegisterHotKey + el filtro de mensajes nativos de Qt (WM_HOTKEY).
//   Linux:   todavía no (X11 necesitaría XGrabKey y Wayland el portal GlobalShortcuts).
#include "control/Acciones.hpp"

#include <QKeySequence>
#include <QMetaObject>
#include <QCoreApplication>

#include <map>

#if defined(__APPLE__)
#include <Carbon/Carbon.h>
#elif defined(_WIN32)
#include <QAbstractNativeEventFilter>
#include <windows.h>
#endif

namespace ba::app::control {

namespace {
/// La tecla y los modificadores de un texto de QKeySequence («Ctrl+Shift+T»).
bool partir(const std::string& tecla, Qt::Key& key, Qt::KeyboardModifiers& mods) {
    const QKeySequence seq(QString::fromStdString(tecla));
    if (seq.isEmpty()) return false;
    const QKeyCombination c = seq[0];
    key = c.key();
    mods = c.keyboardModifiers();
    return key != Qt::Key_unknown;
}
} // namespace

#if defined(__APPLE__)

namespace {
int codigoMac(Qt::Key k) {
    static const std::map<int, int> m = {
        {Qt::Key_A, kVK_ANSI_A}, {Qt::Key_B, kVK_ANSI_B}, {Qt::Key_C, kVK_ANSI_C}, {Qt::Key_D, kVK_ANSI_D},
        {Qt::Key_E, kVK_ANSI_E}, {Qt::Key_F, kVK_ANSI_F}, {Qt::Key_G, kVK_ANSI_G}, {Qt::Key_H, kVK_ANSI_H},
        {Qt::Key_I, kVK_ANSI_I}, {Qt::Key_J, kVK_ANSI_J}, {Qt::Key_K, kVK_ANSI_K}, {Qt::Key_L, kVK_ANSI_L},
        {Qt::Key_M, kVK_ANSI_M}, {Qt::Key_N, kVK_ANSI_N}, {Qt::Key_O, kVK_ANSI_O}, {Qt::Key_P, kVK_ANSI_P},
        {Qt::Key_Q, kVK_ANSI_Q}, {Qt::Key_R, kVK_ANSI_R}, {Qt::Key_S, kVK_ANSI_S}, {Qt::Key_T, kVK_ANSI_T},
        {Qt::Key_U, kVK_ANSI_U}, {Qt::Key_V, kVK_ANSI_V}, {Qt::Key_W, kVK_ANSI_W}, {Qt::Key_X, kVK_ANSI_X},
        {Qt::Key_Y, kVK_ANSI_Y}, {Qt::Key_Z, kVK_ANSI_Z}, {Qt::Key_0, kVK_ANSI_0}, {Qt::Key_1, kVK_ANSI_1},
        {Qt::Key_2, kVK_ANSI_2}, {Qt::Key_3, kVK_ANSI_3}, {Qt::Key_4, kVK_ANSI_4}, {Qt::Key_5, kVK_ANSI_5},
        {Qt::Key_6, kVK_ANSI_6}, {Qt::Key_7, kVK_ANSI_7}, {Qt::Key_8, kVK_ANSI_8}, {Qt::Key_9, kVK_ANSI_9},
        {Qt::Key_F1, kVK_F1}, {Qt::Key_F2, kVK_F2}, {Qt::Key_F3, kVK_F3}, {Qt::Key_F4, kVK_F4},
        {Qt::Key_F5, kVK_F5}, {Qt::Key_F6, kVK_F6}, {Qt::Key_F7, kVK_F7}, {Qt::Key_F8, kVK_F8},
        {Qt::Key_F9, kVK_F9}, {Qt::Key_F10, kVK_F10}, {Qt::Key_F11, kVK_F11}, {Qt::Key_F12, kVK_F12},
        {Qt::Key_F13, kVK_F13}, {Qt::Key_F14, kVK_F14}, {Qt::Key_F15, kVK_F15}, {Qt::Key_F16, kVK_F16},
        {Qt::Key_F17, kVK_F17}, {Qt::Key_F18, kVK_F18}, {Qt::Key_F19, kVK_F19}, {Qt::Key_F20, kVK_F20},
        {Qt::Key_Space, kVK_Space}, {Qt::Key_Return, kVK_Return}, {Qt::Key_Enter, kVK_ANSI_KeypadEnter},
        {Qt::Key_Escape, kVK_Escape}, {Qt::Key_Left, kVK_LeftArrow}, {Qt::Key_Right, kVK_RightArrow},
        {Qt::Key_Up, kVK_UpArrow}, {Qt::Key_Down, kVK_DownArrow}, {Qt::Key_PageUp, kVK_PageUp},
        {Qt::Key_PageDown, kVK_PageDown}, {Qt::Key_Home, kVK_Home}, {Qt::Key_End, kVK_End},
        {Qt::Key_Tab, kVK_Tab}, {Qt::Key_Period, kVK_ANSI_Period}, {Qt::Key_Comma, kVK_ANSI_Comma}};
    auto it = m.find(k);
    return it == m.end() ? -1 : it->second;
}

class AtajosMac : public AtajosGlobales {
   public:
    AtajosMac() {
        EventTypeSpec tipo{kEventClassKeyboard, kEventHotKeyPressed};
        InstallApplicationEventHandler(&AtajosMac::manejar, 1, &tipo, this, &handler_);
    }
    ~AtajosMac() override {
        limpiar();
        if (handler_ != nullptr) RemoveEventHandler(handler_);
    }
    bool registrar(const std::string& tecla, std::function<void()> fn) override {
        Qt::Key k;
        Qt::KeyboardModifiers mods;
        if (!partir(tecla, k, mods)) return false;
        const int codigo = codigoMac(k);
        if (codigo < 0) return false;
        UInt32 m = 0;
        // En la Mac, Qt llama «Ctrl» a Comando y «Meta» a Control.
        if (mods & Qt::ControlModifier) m |= cmdKey;
        if (mods & Qt::MetaModifier) m |= controlKey;
        if (mods & Qt::AltModifier) m |= optionKey;
        if (mods & Qt::ShiftModifier) m |= shiftKey;
        const UInt32 id = ++proximo_;
        EventHotKeyID hid{'BAhk', id};
        EventHotKeyRef ref = nullptr;
        if (RegisterEventHotKey(static_cast<UInt32>(codigo), m, hid, GetApplicationEventTarget(), 0, &ref) != noErr)
            return false;
        teclas_[id] = {ref, std::move(fn)};
        return true;
    }
    void limpiar() override {
        for (auto& [id, t] : teclas_)
            if (t.first != nullptr) UnregisterEventHotKey(t.first);
        teclas_.clear();
    }

   private:
    static OSStatus manejar(EventHandlerCallRef, EventRef ev, void* datos) {
        auto* yo = static_cast<AtajosMac*>(datos);
        EventHotKeyID hid{};
        if (GetEventParameter(ev, kEventParamDirectObject, typeEventHotKeyID, nullptr, sizeof(hid), nullptr, &hid) != noErr)
            return eventNotHandledErr;
        auto it = yo->teclas_.find(hid.id);
        if (it == yo->teclas_.end()) return eventNotHandledErr;
        auto fn = it->second.second;
        QMetaObject::invokeMethod(QCoreApplication::instance(), [fn]() { fn(); }, Qt::QueuedConnection);
        return noErr;
    }
    EventHandlerRef handler_ = nullptr;
    UInt32 proximo_ = 0;
    std::map<UInt32, std::pair<EventHotKeyRef, std::function<void()>>> teclas_;
};
} // namespace

std::unique_ptr<AtajosGlobales> AtajosGlobales::crear() { return std::make_unique<AtajosMac>(); }

#elif defined(_WIN32)

namespace {
UINT vkWin(Qt::Key k) {
    if (k >= Qt::Key_A && k <= Qt::Key_Z) return static_cast<UINT>('A' + (k - Qt::Key_A));
    if (k >= Qt::Key_0 && k <= Qt::Key_9) return static_cast<UINT>('0' + (k - Qt::Key_0));
    if (k >= Qt::Key_F1 && k <= Qt::Key_F24) return static_cast<UINT>(VK_F1 + (k - Qt::Key_F1));
    switch (k) {
        case Qt::Key_Space: return VK_SPACE;
        case Qt::Key_Return: case Qt::Key_Enter: return VK_RETURN;
        case Qt::Key_Escape: return VK_ESCAPE;
        case Qt::Key_Left: return VK_LEFT;
        case Qt::Key_Right: return VK_RIGHT;
        case Qt::Key_Up: return VK_UP;
        case Qt::Key_Down: return VK_DOWN;
        case Qt::Key_PageUp: return VK_PRIOR;
        case Qt::Key_PageDown: return VK_NEXT;
        case Qt::Key_Home: return VK_HOME;
        case Qt::Key_End: return VK_END;
        case Qt::Key_Tab: return VK_TAB;
        default: return 0;
    }
}

class AtajosWin : public AtajosGlobales, public QAbstractNativeEventFilter {
   public:
    AtajosWin() { QCoreApplication::instance()->installNativeEventFilter(this); }
    ~AtajosWin() override {
        limpiar();
        if (QCoreApplication::instance() != nullptr) QCoreApplication::instance()->removeNativeEventFilter(this);
    }
    bool registrar(const std::string& tecla, std::function<void()> fn) override {
        Qt::Key k;
        Qt::KeyboardModifiers mods;
        if (!partir(tecla, k, mods)) return false;
        const UINT vk = vkWin(k);
        if (vk == 0) return false;
        UINT m = MOD_NOREPEAT;
        if (mods & Qt::ControlModifier) m |= MOD_CONTROL;
        if (mods & Qt::AltModifier) m |= MOD_ALT;
        if (mods & Qt::ShiftModifier) m |= MOD_SHIFT;
        if (mods & Qt::MetaModifier) m |= MOD_WIN;
        const int id = ++proximo_;
        if (!RegisterHotKey(nullptr, id, m, vk)) return false;
        teclas_[id] = std::move(fn);
        return true;
    }
    void limpiar() override {
        for (auto& [id, fn] : teclas_) UnregisterHotKey(nullptr, id);
        teclas_.clear();
    }
    bool nativeEventFilter(const QByteArray& tipo, void* mensaje, qintptr*) override {
        if (tipo != "windows_generic_MSG") return false;
        const MSG* msg = static_cast<const MSG*>(mensaje);
        if (msg->message != WM_HOTKEY) return false;
        auto it = teclas_.find(static_cast<int>(msg->wParam));
        if (it == teclas_.end()) return false;
        it->second();
        return true;
    }

   private:
    int proximo_ = 0x3000;
    std::map<int, std::function<void()>> teclas_;
};
} // namespace

std::unique_ptr<AtajosGlobales> AtajosGlobales::crear() { return std::make_unique<AtajosWin>(); }

#else

std::unique_ptr<AtajosGlobales> AtajosGlobales::crear() { return nullptr; }

#endif

} // namespace ba::app::control
