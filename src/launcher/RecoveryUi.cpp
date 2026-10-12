// RecoveryUi.cpp
//
// Implementacion de la UI de recuperacion minima (ver RecoveryUi.hpp).
//
//   - _WIN32: ventana Win32 fija (sin dialog box del sistema) con un static de
//     estado (word-wrap), una linea opcional de integridad y tres botones
//     ("Reintentar", "Forzar reinicio", "Salir"). Corre un bucle de mensajes
//     propio y devuelve la decision del usuario.
//   - no-_WIN32: no hay GUI; loguea el incidente (componente "recovery") y
//     devuelve Retry para que el arbol compile y el flujo se pueda razonar.
//
// Modelo de threading:
//   present() se ejecuta en el thread principal (el del Supervisor) y es
//   BLOQUEANTE: crea la ventana, entra en el bucle de mensajes y no regresa
//   hasta que el usuario elige (boton) o cierra la ventana (X -> Dismissed).
//   No hay threads adicionales; el bucle es el unico consumidor de mensajes.
//
// Estrategia de errores:
//   Nunca lanza. Si no se puede registrar la clase de ventana, crear la
//   ventana o crear los controles, se loguea el GetLastError() y se devuelve
//   Dismissed (el Supervisor lo interpreta como "continuar con el flujo
//   normal"). El bucle de mensajes solo termina por PostQuitMessage (boton o
//   WM_DESTROY), asi que no hay salida por error en mitad del loop.

#include "RecoveryUi.hpp"

#include <cstdio>

#include "Log.hpp"

#include <string>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace ba::launcher {

namespace {

#ifdef _WIN32

// Convierte un wstring a UTF-8 (para Log, que toma std::string).
std::string toUtf8(const std::wstring& p) {
    if (p.empty()) {
        return std::string();
    }
    const int n =
        WideCharToMultiByte(CP_UTF8, 0, p.c_str(), static_cast<int>(p.size()), nullptr, 0, nullptr, nullptr);
    std::string out;
    if (n > 0) {
        out.resize(static_cast<std::size_t>(n));
        WideCharToMultiByte(CP_UTF8, 0, p.c_str(), static_cast<int>(p.size()), out.data(), n, nullptr,
                            nullptr);
    }
    return out;
}

// Nombre de la clase de ventana (local al proceso).
constexpr const wchar_t kClassName[] = L"BroadcastAppliance.Recovery";
// Titulo por defecto (se usa si Incident::title esta vacio).
constexpr const wchar_t kDefaultTitle[] = L"Broadcast Appliance — Recuperación";
// Etiquetas de los botones de accion.
constexpr const wchar_t kRetryLabel[] = L"Reintentar";
constexpr const wchar_t kForceRestartLabel[] = L"Forzar reinicio";
constexpr const wchar_t kExitLabel[] = L"Salir";
// Ids de los botones (WM_COMMAND -> LOWORD(wParam)).
constexpr int kIdRetry = 1;
constexpr int kIdForceRestart = 2;
constexpr int kIdExit = 3;

// Medidas del ÁREA ÚTIL (px). Antes eran las de la ventana entera (520x240 con el
// título y los bordes): el área útil quedaba en ~200 px de alto y los botones, que
// iban de 190 a 222, salían afuera del cuadro. Ahora el texto se mide con la letra
// de los diálogos, los botones van debajo y la ventana se agranda para contenerlo.
constexpr int kClientWidth = 560;
constexpr int kMargin = 18;
constexpr int kGap = 12;
constexpr int kButtonWidth = 160;
constexpr int kButtonGap = 10;
constexpr int kButtonHeight = 32;

// Construye el texto de estado a partir del incidente (solo presentacion).
std::wstring buildStatus(const Incident& inc) {
    std::wstring s;
    if (inc.childADown || inc.childBDown) {
        s += L"El supervisor detecto un problema en los procesos hijos:";
        s += L"\n\n";
        if (inc.childADown) {
            s += L"• Proceso A (aplicacion): CAIDO";
            if (inc.appCodeValid) {
                s += L" (codigo de salida " + std::to_wstring(inc.appExitCode) + L")";
            }
            s += L".\n";
        }
        if (inc.childBDown) {
            s += L"• Proceso B (gateway): CAIDO";
            if (inc.gwCodeValid) {
                s += L" (codigo de salida " + std::to_wstring(inc.gatewayExitCode) + L")";
            }
            s += L".\n";
        }
    } else if (inc.showIntegrity) {
        s += L"Se detectaron problemas de integridad en el disco.";
    } else {
        s += L"El supervisor requiere una accion del operador.";
    }
    s += L"\n\nElija una accion:";
    return s;
}

// Linea de estado de integridad del disco (solo si procede mostrarla).
std::wstring buildIntegrityLine(const Incident& inc) {
    if (inc.integrity.ok) {
        return L"Integridad del disco: OK (" + std::to_wstring(inc.integrity.checked) +
               L" archivos verificados).";
    }
    return L"Integridad del disco: " + std::to_wstring(inc.integrity.checked) + L" verificados, " +
           std::to_wstring(inc.integrity.missing) + L" ausentes, " + std::to_wstring(inc.integrity.mismatch) +
           L" con hash distinto.";
}

// WndProc de la ventana de recuperacion.
//
// La decision (RecoveryAction) vive en el llamador (present) y se comparte por
// GWLP_USERDATA: la ventana guarda una punteria a ese enum local, de modo que
// no hay estado global ni static.
//
//   - WM_COMMAND con el id de un boton y BN_CLICKED: escribe la accion y
//     PostQuitMessage.
//   - WM_DESTROY: PostQuitMessage (la accion queda Dismissed si no se toco un
//     boton, es decir, se cerro con la X).
//   - resto: DefWindowProcW (incluye WM_CLOSE -> DestroyWindow por defecto).
LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_COMMAND: {
            if (HIWORD(wParam) == BN_CLICKED) {
                const int id = LOWORD(wParam);
                RecoveryAction* action =
                    reinterpret_cast<RecoveryAction*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
                if (action != nullptr) {
                    if (id == kIdRetry) {
                        *action = RecoveryAction::Retry;
                    } else if (id == kIdForceRestart) {
                        *action = RecoveryAction::ForceRestart;
                    } else if (id == kIdExit) {
                        *action = RecoveryAction::Exit;
                    }
                }
                PostQuitMessage(0);
            }
            return 0;
        }
        case WM_DESTROY: PostQuitMessage(0); return 0;
        default: break;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

#endif // _WIN32

} // namespace

#ifdef _WIN32

RecoveryAction RecoveryUi::present(const Incident& inc) {
    // Decision del operador: Dismissed salvo que se elija un boton.
    RecoveryAction action = RecoveryAction::Dismissed;

    // --- 1. Registrar la clase de ventana (local) ---------------------------
    WNDCLASSW wc{};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    // MinGW define IDC_ARROW/IDI_APPLICATION como LPSTR (char*), pero
    // LoadCursorW/LoadIconW piden LPCWSTR. MSVC los define como wide. Usamos
    // MAKEINTRESOURCEW(32512) (ambos ids = 32512) para que compile en ambos.
    wc.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
    wc.hIcon = LoadIconW(nullptr, MAKEINTRESOURCEW(32512));
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1); // el de los diálogos: los textos no quedan en una franja gris
    wc.lpszClassName = kClassName;

    const ATOM atom = RegisterClassW(&wc);
    if (atom == 0) {
        const DWORD err = GetLastError();
        if (err != ERROR_CLASS_ALREADY_EXISTS) {
            BA_LOG_ERROR("recovery", static_cast<int>(err), "RegisterClassW fallo; sin UI de recuperacion");
            return RecoveryAction::Dismissed;
        }
        // ERROR_CLASS_ALREADY_EXISTS: present() se volvio a invocar en el mismo
        // proceso; la clase ya esta registrada y se puede seguir usando.
    }

    // --- 2. Medir: la letra de los diálogos de Windows y el alto de cada texto ---
    NONCLIENTMETRICSW ncm{};
    ncm.cbSize = sizeof(ncm);
    HFONT font = nullptr;
    if (SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0)) font = CreateFontIndirectW(&ncm.lfMessageFont);
    const bool fontPropia = font != nullptr; // la de stock no se libera
    if (font == nullptr) font = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
    const int contentWidth = kClientWidth - 2 * kMargin;
    const auto altoDe = [&](const std::wstring& texto) {
        HDC dc = GetDC(nullptr);
        HGDIOBJ antes = SelectObject(dc, font);
        RECT r{0, 0, contentWidth, 0};
        DrawTextW(dc, texto.c_str(), static_cast<int>(texto.size()), &r, DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX);
        SelectObject(dc, antes);
        ReleaseDC(nullptr, dc);
        return static_cast<int>(r.bottom - r.top) + 4;
    };
    const std::wstring status = buildStatus(inc);
    const std::wstring integrityLine = inc.showIntegrity ? buildIntegrityLine(inc) : std::wstring();
    const int statusTop = kMargin;
    const int statusHeight = altoDe(status);
    const int integrityTop = statusTop + statusHeight + kGap;
    const int integrityHeight = inc.showIntegrity ? altoDe(integrityLine) : 0;
    const int buttonTop = (inc.showIntegrity ? integrityTop + integrityHeight : statusTop + statusHeight) + kGap + 6;
    const int clientHeight = buttonTop + kButtonHeight + kMargin;
    const int buttonsLeft = (kClientWidth - (3 * kButtonWidth + 2 * kButtonGap)) / 2;

    // --- 3. La ventana: del tamaño que pide el área útil, centrada -------------
    const DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU;
    RECT marco{0, 0, kClientWidth, clientHeight};
    AdjustWindowRectEx(&marco, style, FALSE, 0);
    const int winW = marco.right - marco.left, winH = marco.bottom - marco.top;
    const int x = (GetSystemMetrics(SM_CXSCREEN) - winW) / 2, y = (GetSystemMetrics(SM_CYSCREEN) - winH) / 3;
    const std::wstring title = inc.title.empty() ? std::wstring(kDefaultTitle) : inc.title;
    HWND hwnd = CreateWindowExW(0, kClassName, title.c_str(), style, x, y, winW, winH, nullptr, nullptr,
                                wc.hInstance, nullptr);
    if (hwnd == nullptr) {
        const DWORD err = GetLastError();
        BA_LOG_ERROR("recovery", static_cast<int>(err), "CreateWindowExW fallo; sin UI de recuperacion");
        return RecoveryAction::Dismissed;
    }

    // Compartir la decision con la ventana (vive en present, valida todo el
    // bucle de mensajes).
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(&action));

    // --- 4. Controles hijos, con la letra de los diálogos ----------------------
    const auto conLetra = [font](HWND h) {
        if (h != nullptr) SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        return h;
    };
    HWND hStatus = conLetra(CreateWindowExW(0, L"STATIC", status.c_str(), WS_CHILD | WS_VISIBLE | SS_LEFT | SS_NOPREFIX,
                                            kMargin, statusTop, contentWidth, statusHeight, hwnd, nullptr,
                                            wc.hInstance, nullptr));
    if (hStatus == nullptr) {
        const DWORD err = GetLastError();
        BA_LOG_ERROR("recovery", static_cast<int>(err), "no se pudo crear el static de estado");
        DestroyWindow(hwnd);
        return RecoveryAction::Dismissed;
    }

    // Linea opcional de integridad (solo si procede).
    if (inc.showIntegrity) {
        HWND hIntegrity = conLetra(CreateWindowExW(0, L"STATIC", integrityLine.c_str(),
                                                   WS_CHILD | WS_VISIBLE | SS_LEFT | SS_NOPREFIX, kMargin, integrityTop,
                                                   contentWidth, integrityHeight, hwnd, nullptr, wc.hInstance, nullptr));
        if (hIntegrity == nullptr) {
            const DWORD err = GetLastError();
            BA_LOG_WARN("recovery", static_cast<int>(err), "no se pudo crear el static de integridad");
        }
    }

    // Botones de accion (Reintentar / Forzar reinicio / Salir), centrados abajo.
    HWND hRetry = conLetra(CreateWindowExW(0, L"BUTTON", kRetryLabel,
                                           WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON | BS_TEXT, buttonsLeft,
                                           buttonTop, kButtonWidth, kButtonHeight, hwnd,
                                           reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdRetry)), wc.hInstance, nullptr));
    HWND hForce = conLetra(CreateWindowExW(0, L"BUTTON", kForceRestartLabel,
                                           WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON | BS_TEXT,
                                           buttonsLeft + kButtonWidth + kButtonGap, buttonTop, kButtonWidth, kButtonHeight,
                                           hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdForceRestart)),
                                           wc.hInstance, nullptr));
    HWND hExit = conLetra(CreateWindowExW(0, L"BUTTON", kExitLabel,
                                          WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON | BS_TEXT,
                                          buttonsLeft + 2 * (kButtonWidth + kButtonGap), buttonTop, kButtonWidth,
                                          kButtonHeight, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdExit)),
                                          wc.hInstance, nullptr));
    if (hRetry == nullptr || hForce == nullptr || hExit == nullptr) {
        const DWORD err = GetLastError();
        BA_LOG_ERROR("recovery", static_cast<int>(err), "no se pudo crear un boton de accion");
        DestroyWindow(hwnd);
        return RecoveryAction::Dismissed;
    }
    SetFocus(hRetry);

    // --- 5. Mostrar y correr el bucle de mensajes ----------------------------
    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);
    BA_LOG_INFO("recovery", 0, std::string("UI de recuperacion mostrada: ") + toUtf8(status));

    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    if (fontPropia) DeleteObject(font); // el diálogo puede volver a aparecer: no se acumulan
    // El bucle solo termina por PostQuitMessage: boton (Retry/ForceRestart/Exit)
    // o WM_DESTROY (Dismissed). Devolvemos la decision tal cual.
    return action;
}

#else // ! _WIN32

RecoveryAction RecoveryUi::present(const Incident& inc) {
    // Sin GUI fuera de Windows: se loguea el incidente y se devuelve Retry
    // (continuar), de modo que el Supervisor siga su flujo normal.
    std::string ctx = "UI de recuperacion (sin GUI en esta plataforma)";
    if (inc.childADown) {
        ctx += "; A caido";
    }
    if (inc.childBDown) {
        ctx += "; B caido";
    }
    if (inc.showIntegrity && !inc.integrity.ok) {
        ctx += "; integridad con fallos";
    }
    BA_LOG_WARN("recovery", 0, ctx);
#if defined(__APPLE__)
    // macOS: dialogo nativo con osascript (decision D4). Si nadie contesta en
    // 5 minutos se reintenta solo: el launcher nunca queda colgado de la UI.
    std::string msg = "Broadcast Appliance tuvo un problema.";
    if (inc.childADown) msg += "\\nLa interfaz/motor (A) se cerro.";
    if (inc.childBDown) msg += "\\nLa sala de invitados (B) se cerro.";
    if (inc.showIntegrity && !inc.integrity.ok)
        msg += "\\nHay archivos del programa con fallas de integridad.";
    std::string safe;
    for (char ch : msg) {
        if (ch == '"' || ch == '\'') continue; // sin comillas: va dentro de un literal de AppleScript
        safe += ch;
    }
    const std::string cmd =
        "osascript -e 'display dialog \"" + safe +
        "\" with title \"Broadcast Appliance\" buttons {\"Salir\", \"Reiniciar todo\", \"Reintentar\"} "
        "default button \"Reintentar\" with icon caution giving up after 300' 2>/dev/null";
    std::string out;
    if (FILE* f = ::popen(cmd.c_str(), "r")) {
        char buf[256];
        while (std::fgets(buf, sizeof(buf), f) != nullptr)
            out += buf;
        ::pclose(f);
    }
    if (out.find("button returned:Salir") != std::string::npos) {
        // Salir corta la transmisión y la grabación: se confirma (por defecto, no).
        std::string conf;
        if (FILE* f = ::popen("osascript -e 'display dialog \"¿Cerrar todo el programa? Se corta la transmisión y la "
                              "grabación.\" with title \"Broadcast Appliance\" buttons {\"Cerrar todo\", \"Seguir\"} "
                              "default button \"Seguir\" with icon stop giving up after 60' 2>/dev/null", "r")) {
            char buf[256];
            while (std::fgets(buf, sizeof(buf), f) != nullptr)
                conf += buf;
            ::pclose(f);
        }
        if (conf.find("button returned:Cerrar todo") != std::string::npos) return RecoveryAction::Exit;
        return RecoveryAction::Retry;
    }
    if (out.find("button returned:Reiniciar todo") != std::string::npos) return RecoveryAction::ForceRestart;
#endif
    return RecoveryAction::Retry;
}

#endif // _WIN32

} // namespace ba::launcher
