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
#include "i18n/I18n.hpp"

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

// UTF-8 a wstring (los textos traducidos vienen en UTF-8; Win32 los pide anchos).
std::wstring aWide(const std::string& s) {
    if (s.empty()) return std::wstring();
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring out;
    if (n > 0) {
        out.resize(static_cast<std::size_t>(n));
        MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), out.data(), n);
    }
    return out;
}

// Nombre de la clase de ventana (local al proceso).
constexpr const wchar_t kClassName[] = L"BroadcastAppliance.Recovery";
// El titulo por defecto (si Incident::title esta vacio) y las etiquetas de los
// botones se traducen al mostrarlos (present).
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
    using ba::i18n::llenar;
    using ba::i18n::traducir;
    std::string s;
    if (inc.childADown || inc.childBDown) {
        s += traducir("recuperacion", "El supervisor detecto un problema en los procesos hijos:");
        s += "\n\n";
        if (inc.childADown) {
            s += inc.appCodeValid
                     ? llenar(traducir("recuperacion", "• Proceso A (aplicacion): CAIDO (codigo de salida %1)."),
                              {std::to_string(inc.appExitCode)})
                     : traducir("recuperacion", "• Proceso A (aplicacion): CAIDO.");
            s += "\n";
        }
        if (inc.childBDown) {
            s += inc.gwCodeValid
                     ? llenar(traducir("recuperacion", "• Proceso B (gateway): CAIDO (codigo de salida %1)."),
                              {std::to_string(inc.gatewayExitCode)})
                     : traducir("recuperacion", "• Proceso B (gateway): CAIDO.");
            s += "\n";
        }
    } else if (inc.showIntegrity) {
        s += traducir("recuperacion", "Se detectaron problemas de integridad en el disco.");
    } else {
        s += traducir("recuperacion", "El supervisor requiere una accion del operador.");
    }
    s += "\n\n";
    s += traducir("recuperacion", "Elija una accion:");
    return aWide(s);
}

// Linea de estado de integridad del disco (solo si procede mostrarla).
std::wstring buildIntegrityLine(const Incident& inc) {
    using ba::i18n::llenar;
    using ba::i18n::traducir;
    if (inc.integrity.ok) {
        return aWide(llenar(traducir("recuperacion", "Integridad del disco: OK (%1 archivos verificados)."),
                            {std::to_string(inc.integrity.checked)}));
    }
    return aWide(
        llenar(traducir("recuperacion", "Integridad del disco: %1 verificados, %2 ausentes, %3 con hash distinto."),
               {std::to_string(inc.integrity.checked), std::to_string(inc.integrity.missing),
                std::to_string(inc.integrity.mismatch)}));
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
            BA_LOG_ERROR("recovery", static_cast<int>(err),
                         ba::i18n::traducir("recovery", "RegisterClassW fallo; sin UI de recuperacion"));
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
    const std::wstring title = inc.title.empty() ? aWide(ba::i18n::traducir("recuperacion", "Broadcast Appliance — Recuperación")) : inc.title;
    HWND hwnd = CreateWindowExW(0, kClassName, title.c_str(), style, x, y, winW, winH, nullptr, nullptr,
                                wc.hInstance, nullptr);
    if (hwnd == nullptr) {
        const DWORD err = GetLastError();
        BA_LOG_ERROR("recovery", static_cast<int>(err),
                     ba::i18n::traducir("recovery", "CreateWindowExW fallo; sin UI de recuperacion"));
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
        BA_LOG_ERROR("recovery", static_cast<int>(err),
                     ba::i18n::traducir("recovery", "no se pudo crear el static de estado"));
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
            BA_LOG_WARN("recovery", static_cast<int>(err),
                        ba::i18n::traducir("recovery", "no se pudo crear el static de integridad"));
        }
    }

    // Botones de accion (Reintentar / Forzar reinicio / Salir), centrados abajo.
    const std::wstring retryLabel = aWide(ba::i18n::traducir("recuperacion", "Reintentar"));
    const std::wstring forceLabel = aWide(ba::i18n::traducir("recuperacion", "Forzar reinicio"));
    const std::wstring exitLabel = aWide(ba::i18n::traducir("recuperacion", "Salir"));
    HWND hRetry = conLetra(CreateWindowExW(0, L"BUTTON", retryLabel.c_str(),
                                           WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON | BS_TEXT, buttonsLeft,
                                           buttonTop, kButtonWidth, kButtonHeight, hwnd,
                                           reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdRetry)), wc.hInstance, nullptr));
    HWND hForce = conLetra(CreateWindowExW(0, L"BUTTON", forceLabel.c_str(),
                                           WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON | BS_TEXT,
                                           buttonsLeft + kButtonWidth + kButtonGap, buttonTop, kButtonWidth, kButtonHeight,
                                           hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdForceRestart)),
                                           wc.hInstance, nullptr));
    HWND hExit = conLetra(CreateWindowExW(0, L"BUTTON", exitLabel.c_str(),
                                          WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON | BS_TEXT,
                                          buttonsLeft + 2 * (kButtonWidth + kButtonGap), buttonTop, kButtonWidth,
                                          kButtonHeight, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdExit)),
                                          wc.hInstance, nullptr));
    if (hRetry == nullptr || hForce == nullptr || hExit == nullptr) {
        const DWORD err = GetLastError();
        BA_LOG_ERROR("recovery", static_cast<int>(err),
                     ba::i18n::traducir("recovery", "no se pudo crear un boton de accion"));
        DestroyWindow(hwnd);
        return RecoveryAction::Dismissed;
    }
    SetFocus(hRetry);

    // --- 5. Mostrar y correr el bucle de mensajes ----------------------------
    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);
    BA_LOG_INFO("recovery", 0,
                ba::i18n::llenar(ba::i18n::traducir("recovery", "UI de recuperacion mostrada: %1"), {toUtf8(status)}));

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
    using ba::i18n::traducir;
    std::string ctx = traducir("recovery", "UI de recuperacion (sin GUI en esta plataforma)");
    if (inc.childADown) {
        ctx += "; " + traducir("recovery", "A caido");
    }
    if (inc.childBDown) {
        ctx += "; " + traducir("recovery", "B caido");
    }
    if (inc.showIntegrity && !inc.integrity.ok) {
        ctx += "; " + traducir("recovery", "integridad con fallos");
    }
    BA_LOG_WARN("recovery", 0, ctx);
#if defined(__APPLE__)
    // macOS: dialogo nativo con osascript (decision D4). Si nadie contesta en
    // 5 minutos se reintenta solo: el launcher nunca queda colgado de la UI.
    // Los botones van traducidos, y la respuesta de osascript («button returned:<etiqueta>»)
    // se compara con la etiqueta traducida.
    const auto sinComillas = [](const std::string& t) {
        std::string safe;
        for (char ch : t) {
            if (ch == '"' || ch == '\'') continue; // sin comillas: va dentro de un literal de AppleScript
            safe += ch;
        }
        return safe;
    };
    std::string msg = traducir("recuperacion", "Broadcast Appliance tuvo un problema.");
    if (inc.childADown) msg += "\\n" + traducir("recuperacion", "La interfaz/motor (A) se cerro.");
    if (inc.childBDown) msg += "\\n" + traducir("recuperacion", "La sala de invitados (B) se cerro.");
    if (inc.showIntegrity && !inc.integrity.ok)
        msg += "\\n" + traducir("recuperacion", "Hay archivos del programa con fallas de integridad.");
    const std::string safe = sinComillas(msg);
    const std::string bSalir = sinComillas(traducir("recuperacion", "Salir"));
    const std::string bReiniciar = sinComillas(traducir("recuperacion", "Reiniciar todo"));
    const std::string bReintentar = sinComillas(traducir("recuperacion", "Reintentar"));
    const std::string cmd = "osascript -e 'display dialog \"" + safe +
                            "\" with title \"Broadcast Appliance\" buttons {\"" + bSalir + "\", \"" + bReiniciar +
                            "\", \"" + bReintentar + "\"} default button \"" + bReintentar +
                            "\" with icon caution giving up after 300' 2>/dev/null";
    std::string out;
    if (FILE* f = ::popen(cmd.c_str(), "r")) {
        char buf[256];
        while (std::fgets(buf, sizeof(buf), f) != nullptr)
            out += buf;
        ::pclose(f);
    }
    if (out.find("button returned:" + bSalir) != std::string::npos) {
        // Salir corta la transmisión y la grabación: se confirma (por defecto, no).
        const std::string bCerrar = sinComillas(traducir("recuperacion", "Cerrar todo"));
        const std::string bSeguir = sinComillas(traducir("recuperacion", "Seguir"));
        const std::string pregunta =
            sinComillas(traducir("recuperacion", "¿Cerrar todo el programa? Se corta la transmisión y la grabación."));
        const std::string cmdConf = "osascript -e 'display dialog \"" + pregunta +
                                    "\" with title \"Broadcast Appliance\" buttons {\"" + bCerrar + "\", \"" +
                                    bSeguir + "\"} default button \"" + bSeguir +
                                    "\" with icon stop giving up after 60' 2>/dev/null";
        std::string conf;
        if (FILE* f = ::popen(cmdConf.c_str(), "r")) {
            char buf[256];
            while (std::fgets(buf, sizeof(buf), f) != nullptr)
                conf += buf;
            ::pclose(f);
        }
        if (conf.find("button returned:" + bCerrar) != std::string::npos) return RecoveryAction::Exit;
        return RecoveryAction::Retry;
    }
    if (out.find("button returned:" + bReiniciar) != std::string::npos) return RecoveryAction::ForceRestart;
#endif
    return RecoveryAction::Retry;
}

#endif // _WIN32

} // namespace ba::launcher
