// RecoveryUi.cpp
//
// Implementacion de la UI de recuperacion minima (ver RecoveryUi.hpp).
//
//   - _WIN32: ventana Win32 fija (sin dialog box del sistema) con un static
//     que muestra el estado y un boton "Reiniciar sistema". Corre un bucle de
//     mensajes propio y devuelve la decision del usuario.
//   - no-_WIN32: no hay GUI; loguea el estado (componente "recovery") y
//     devuelve false para que el arbol compile y el flujo se pueda razonar.
//
// Modelo de threading:
//   show() se ejecuta en el thread principal (el mismo que el Supervisor) y es
//   BLOQUEANTE: crea la ventana, entra en el bucle de mensajes y no regresa
//   hasta que el usuario elige (boton -> true) o cierra la ventana (X ->
//   false). No hay threads adicionales; el bucle es el unico consumidor de
//   mensajes de la ventana.
//
// Estrategia de errores:
//   Nunca lanza. Si no se puede registrar la clase de ventana, crear la
//   ventana o crear los controles, se loguea el GetLastError() y se devuelve
//   false (el Supervisor interpreta false como "no reiniciar"). El bucle de
//   mensajes solo termina por PostQuitMessage (boton o WM_DESTROY), asi que no
//   hay salida por error en mitad del loop.

#include "RecoveryUi.hpp"

#include "Log.hpp"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace ba::launcher {

#ifdef _WIN32

namespace {

// Nombre de la clase de ventana (local al proceso).
constexpr const wchar_t kClassName[] = L"BroadcastAppliance.Recovery";
// Titulo de la ventana (spec 3.1.11).
constexpr const wchar_t kWindowTitle[] = L"Broadcast Appliance — Recuperación";
// Etiqueta del boton de accion.
constexpr const wchar_t kButtonLabel[] = L"Reiniciar sistema";
// Id del boton (WM_COMMAND -> LOWORD(wParam)).
constexpr int kIdRestartButton = 1;

// Dimensiones fijas de la ventana y de sus controles (px).
constexpr int kWindowWidth = 460;
constexpr int kWindowHeight = 180;
constexpr int kMargin = 16;
constexpr int kStatusTop = 16;
constexpr int kStatusHeight = 96;
constexpr int kButtonTop = 128;
constexpr int kButtonWidth = 132;
constexpr int kButtonHeight = 32;

// WndProc de la ventana de recuperacion.
//
// El flag de decision (true si el usuario eligio reiniciar) vive en el
// llamador (show) y se comparte por GWLP_USERDATA: la ventana guarda una
// punteria a ese bool local, de modo que no hay estado global ni static.
//
//   - WM_COMMAND con el id del boton y BN_CLICKED: marca true y PostQuitMessage.
//   - WM_DESTROY: PostQuitMessage (el flag queda como estaba: false si no se
//     toco el boton, es decir, se cerro con la X).
//   - resto: DefWindowProcW (incluye WM_CLOSE -> DestroyWindow por defecto).
LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_COMMAND:
            if (LOWORD(wParam) == kIdRestartButton && HIWORD(wParam) == BN_CLICKED) {
                bool* flag = reinterpret_cast<bool*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
                if (flag) {
                    *flag = true;
                }
                PostQuitMessage(0);
            }
            return 0;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        default:
            break;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

} // namespace

bool RecoveryUi::show(const std::wstring& statusText) {
    // Decision del usuario: true solo si se presiono "Reiniciar sistema".
    bool restartChosen = false;

    // --- 1. Registrar la clase de ventana (local) ---------------------------
    WNDCLASSW wc{};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    // MinGW define IDC_ARROW/IDI_APPLICATION como LPSTR (char*), pero
    // LoadCursorW/LoadIconW piden LPCWSTR. MSVC los define como wide. Usamos
    // MAKEINTRESOURCEW(32512) (ambos ids = 32512) para que compile en ambos.
    wc.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
    wc.hIcon = LoadIconW(nullptr, MAKEINTRESOURCEW(32512));
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    wc.lpszClassName = kClassName;

    const ATOM atom = RegisterClassW(&wc);
    if (atom == 0) {
        const DWORD err = GetLastError();
        if (err != ERROR_CLASS_ALREADY_EXISTS) {
            // No se pudo registrar y no es "ya existe": no hay ventana posible.
            BA_LOG_ERROR("recovery", static_cast<int>(err), "RegisterClassW fallo; sin UI de recuperacion");
            return false;
        }
        // ERROR_CLASS_ALREADY_EXISTS: show() se volvio a invocar en el mismo
        // proceso; la clase ya esta registrada y se puede seguir usando.
    }

    // --- 2. Crear la ventana fija (sin redimensionar, sin minimizar) --------
    const DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU;
    HWND hwnd = CreateWindowExW(
        0, kClassName, kWindowTitle, style,
        CW_USEDEFAULT, CW_USEDEFAULT, kWindowWidth, kWindowHeight,
        nullptr, nullptr, wc.hInstance, nullptr);
    if (hwnd == nullptr) {
        const DWORD err = GetLastError();
        BA_LOG_ERROR("recovery", static_cast<int>(err), "CreateWindowExW fallo; sin UI de recuperacion");
        return false;
    }

    // Compartir el flag de decision con la ventana (vive en show, valido todo
    // el bucle de mensajes).
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(&restartChosen));

    // --- 3. Controles hijos --------------------------------------------------
    // Static con el texto de estado (word-wrap a lo ancho del control).
    const int statusWidth = kWindowWidth - 2 * kMargin;
    HWND hStatus = CreateWindowExW(
        0, L"STATIC", statusText.c_str(), WS_CHILD | WS_VISIBLE | SS_LEFT,
        kMargin, kStatusTop, statusWidth, kStatusHeight,
        hwnd, nullptr, wc.hInstance, nullptr);
    if (hStatus == nullptr) {
        const DWORD err = GetLastError();
        BA_LOG_ERROR("recovery", static_cast<int>(err), "no se pudo crear el static de estado");
        DestroyWindow(hwnd);
        return false;
    }

    // Boton "Reiniciar sistema".
    HWND hButton = CreateWindowExW(
        0, L"BUTTON", kButtonLabel, WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | BS_TEXT,
        kMargin, kButtonTop, kButtonWidth, kButtonHeight,
        hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdRestartButton)), wc.hInstance, nullptr);
    if (hButton == nullptr) {
        const DWORD err = GetLastError();
        BA_LOG_ERROR("recovery", static_cast<int>(err), "no se pudo crear el boton de reinicio");
        DestroyWindow(hwnd);
        return false;
    }
    SetFocus(hButton);

    // --- 4. Mostrar y correr el bucle de mensajes ----------------------------
    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);
    BA_LOG_INFO("recovery", 0, std::string("UI de recuperacion mostrada: ") +
                                    std::string(statusText.begin(), statusText.end()));

    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    // El bucle solo termina por PostQuitMessage: boton (true) o WM_DESTROY
    // (false). Devolvemos la decision tal cual.
    return restartChosen;
}

#else // ! _WIN32

bool RecoveryUi::show(const std::wstring& statusText) {
    // Sin GUI fuera de Windows: se loguea el estado y se devuelve false (no
    // reiniciar), de modo que el Supervisor siga su flujo normal.
    BA_LOG_WARN("recovery", 0,
                "UI de recuperacion (sin GUI en esta plataforma): " +
                    std::string(statusText.begin(), statusText.end()));
    return false;
}

#endif // _WIN32

} // namespace ba::launcher
