// ============================================================================
// src/app/escritorio/control_chrome.cpp — Chrome y Edge por su protocolo de
// depuración (Chrome DevTools Protocol, ver control.hpp)
//
// Lo que UI Automation no da de una página: la CONSOLA (los errores de
// JavaScript, los console.log), evaluar JavaScript, navegar, recargar, una
// captura de la página entera aunque la ventana esté tapada. Chrome lo expone
// en un puerto local (HTTP para la lista de pestañas, WebSocket para hablar
// con cada una) solo si se lo abre con --remote-debugging-port, y desde Chrome
// 136 solo con un perfil que no sea el de siempre: «abrir» lo lanza con un
// perfil propio del asistente (en los datos de la máquina: sus cookies y
// sesiones no son las del usuario).
//
// Todo acá: un cliente HTTP y WebSocket mínimo sobre Winsock (127.0.0.1, sin
// TLS), y las llamadas de CDP por id.
// ============================================================================
#include "escritorio/control.hpp"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <random>

#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#include <shellapi.h>
#include <shlobj.h>
#endif

namespace bactl {

json herramientasChrome() {
    json t = json::array();
    t.push_back(herramienta(
        "system_chrome",
        "Chrome o Edge por su protocolo de depuración: lo que UI Automation no da de una página. `accion`:\n"
        "- estado: las ventanas de navegador abiertas y si hay uno con depuración (y sus pestañas).\n"
        "- abrir: lanza Chrome (o `navegador`: edge) con depuración y un perfil propio del asistente (no el del "
        "usuario: Chrome no deja depurar su perfil de siempre), con `url` opcional.\n"
        "- pestanas: las pestañas (id, título, url).\n"
        "- consola: los mensajes de la consola de una pestaña (errores, avisos, console.log): los ya guardados y los "
        "que lleguen en `segundos` (2).\n"
        "- evaluar: corre `codigo` (JavaScript) en la pestaña y devuelve el resultado (espera promesas).\n"
        "- navegar (`url`), recargar, captura (la página como imagen, aunque esté tapada).\n"
        "La pestaña: `pestana` (su id, su número desde 1, o parte del título o la url); sin eso, la primera. "
        "`puerto` (9222). Permiso: navegador.",
        esquema({{"accion", {{"type", "string"}, {"enum", {"estado", "abrir", "pestanas", "consola", "evaluar", "navegar", "recargar", "captura"}}}},
                 {"pestana", {{"type", "string"}}},
                 {"codigo", {{"type", "string"}}},
                 {"url", {{"type", "string"}}},
                 {"segundos", {{"type", "number"}, {"minimum", 0}, {"maximum", 30}}},
                 {"navegador", {{"type", "string"}, {"enum", {"chrome", "edge"}}}},
                 {"puerto", {{"type", "integer"}}}},
                {"accion"})));
    return t;
}

#if defined(_WIN32)

namespace {

// --- Red: HTTP y WebSocket a 127.0.0.1 -------------------------------------------------

struct Winsock {
    Winsock() {
        WSADATA w;
        WSAStartup(MAKEWORD(2, 2), &w);
    }
};

SOCKET conectar(int puerto, int msTiempo) {
    static Winsock w;
    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET) return s;
    DWORD t = static_cast<DWORD>(msTiempo);
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&t), sizeof(t));
    setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&t), sizeof(t));
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_port = htons(static_cast<u_short>(puerto));
    inet_pton(AF_INET, "127.0.0.1", &a.sin_addr);
    if (connect(s, reinterpret_cast<sockaddr*>(&a), sizeof(a)) != 0) {
        closesocket(s);
        return INVALID_SOCKET;
    }
    return s;
}

bool mandarTodo(SOCKET s, const char* d, size_t n) {
    while (n > 0) {
        const int k = send(s, d, static_cast<int>(std::min<size_t>(n, 1 << 20)), 0);
        if (k <= 0) return false;
        d += k;
        n -= static_cast<size_t>(k);
    }
    return true;
}

bool recibirN(SOCKET s, char* d, size_t n) {
    while (n > 0) {
        const int k = recv(s, d, static_cast<int>(std::min<size_t>(n, 1 << 20)), 0);
        if (k <= 0) return false;
        d += k;
        n -= static_cast<size_t>(k);
    }
    return true;
}

/// GET http://127.0.0.1:puerto/ruta → el cuerpo (vacío si no contesta).
std::string httpGet(int puerto, const std::string& ruta) {
    SOCKET s = conectar(puerto, 3000);
    if (s == INVALID_SOCKET) return {};
    const std::string pedido = "GET " + ruta + " HTTP/1.1\r\nHost: 127.0.0.1:" + std::to_string(puerto) + "\r\nConnection: close\r\n\r\n";
    std::string r;
    if (mandarTodo(s, pedido.data(), pedido.size())) {
        char buf[16384];
        int k;
        while ((k = recv(s, buf, sizeof(buf), 0)) > 0) r.append(buf, static_cast<size_t>(k));
    }
    closesocket(s);
    const auto cuerpo = r.find("\r\n\r\n");
    return cuerpo == std::string::npos ? std::string() : r.substr(cuerpo + 4);
}

/// Un WebSocket de cliente (texto; responde los ping).
class WebSocket {
   public:
    ~WebSocket() {
        if (s_ != INVALID_SOCKET) closesocket(s_);
    }
    bool abrir(int puerto, const std::string& ruta, std::string& error) {
        s_ = conectar(puerto, 15000);
        if (s_ == INVALID_SOCKET) {
            error = "no contesta el puerto " + std::to_string(puerto);
            return false;
        }
        unsigned char clave[16];
        std::random_device rd;
        for (auto& c : clave) c = static_cast<unsigned char>(rd());
        const std::string pedido = "GET " + ruta + " HTTP/1.1\r\nHost: 127.0.0.1:" + std::to_string(puerto) +
                                   "\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Key: " + base64(clave, 16) +
                                   "\r\nSec-WebSocket-Version: 13\r\n\r\n";
        if (!mandarTodo(s_, pedido.data(), pedido.size())) {
            error = "no pude mandar el saludo";
            return false;
        }
        std::string r;
        char c;
        while (r.find("\r\n\r\n") == std::string::npos && r.size() < 8192) {
            if (recv(s_, &c, 1, 0) != 1) break;
            r.push_back(c);
        }
        if (r.find(" 101 ") == std::string::npos) {
            error = "Chrome no aceptó la conexión: " + r.substr(0, r.find("\r\n"));
            return false;
        }
        return true;
    }
    bool mandar(const std::string& texto) {
        std::string f;
        f.push_back(static_cast<char>(0x81)); // FIN + texto
        const size_t n = texto.size();
        if (n < 126) {
            f.push_back(static_cast<char>(0x80 | n));
        } else if (n < 65536) {
            f.push_back(static_cast<char>(0x80 | 126));
            f.push_back(static_cast<char>((n >> 8) & 0xFF));
            f.push_back(static_cast<char>(n & 0xFF));
        } else {
            f.push_back(static_cast<char>(0x80 | 127));
            for (int i = 7; i >= 0; --i) f.push_back(static_cast<char>((static_cast<unsigned long long>(n) >> (8 * i)) & 0xFF));
        }
        unsigned char m[4];
        std::random_device rd;
        for (auto& b : m) b = static_cast<unsigned char>(rd());
        f.append(reinterpret_cast<char*>(m), 4);
        const size_t ini = f.size();
        f += texto;
        for (size_t i = 0; i < n; ++i) f[ini + i] = static_cast<char>(f[ini + i] ^ m[i % 4]);
        return mandarTodo(s_, f.data(), f.size());
    }
    /// El próximo mensaje de texto entero (false si se cortó o se venció el tiempo).
    bool recibir(std::string& out) {
        out.clear();
        for (;;) {
            unsigned char h[2];
            if (!recibirN(s_, reinterpret_cast<char*>(h), 2)) return false;
            const int op = h[0] & 0x0F;
            const bool fin = (h[0] & 0x80) != 0;
            unsigned long long n = h[1] & 0x7F;
            if (n == 126) {
                unsigned char e[2];
                if (!recibirN(s_, reinterpret_cast<char*>(e), 2)) return false;
                n = (static_cast<unsigned long long>(e[0]) << 8) | e[1];
            } else if (n == 127) {
                unsigned char e[8];
                if (!recibirN(s_, reinterpret_cast<char*>(e), 8)) return false;
                n = 0;
                for (unsigned char b : e) n = (n << 8) | b;
            }
            if (n > (256ULL << 20)) return false; // 256 MB: algo anda mal
            std::string datos(static_cast<size_t>(n), '\0');
            if (n > 0 && !recibirN(s_, datos.data(), static_cast<size_t>(n))) return false;
            if (op == 8) return false; // cerró
            if (op == 9) {             // ping → pong
                std::string f;
                f.push_back(static_cast<char>(0x8A));
                f.push_back(static_cast<char>(0x80));
                f.append(4, '\0');
                mandarTodo(s_, f.data(), f.size());
                continue;
            }
            if (op == 10) continue;
            out += datos;
            if (fin) return true;
        }
    }
    void tiempo(int ms) {
        DWORD t = static_cast<DWORD>(ms);
        setsockopt(s_, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&t), sizeof(t));
    }

   private:
    SOCKET s_ = INVALID_SOCKET;
};

// --- CDP ---------------------------------------------------------------------------------

/// Una sesión con una pestaña: llamadas por id; los eventos que llegan en el medio se guardan.
class Cdp {
   public:
    bool abrir(int puerto, const std::string& wsUrl, std::string& error) {
        // ws://127.0.0.1:9222/devtools/page/ID → la ruta
        const auto p = wsUrl.find("/devtools/");
        if (p == std::string::npos) {
            error = "la pestaña no tiene dirección de depuración";
            return false;
        }
        return ws_.abrir(puerto, wsUrl.substr(p), error);
    }
    /// Llama y espera la respuesta (hasta `ms`); {} si no llegó, {"error"} si Chrome dio error.
    json llamar(const std::string& metodo, const json& params = json::object(), int ms = 15000) {
        const int id = ++id_;
        if (!ws_.mandar(json{{"id", id}, {"method", metodo}, {"params", params}}.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace))) return {{"error", "no se pudo mandar"}};
        ws_.tiempo(ms);
        const auto fin = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
        std::string m;
        while (std::chrono::steady_clock::now() < fin && ws_.recibir(m)) {
            json j = json::parse(m, nullptr, false);
            if (!j.is_object()) continue;
            if (j.value("id", -1) == id) return j.contains("error") ? json{{"error", j["error"].value("message", std::string("error"))}} : j.value("result", json::object());
            if (j.contains("method")) eventos.push_back(std::move(j));
        }
        return {{"error", "Chrome no contestó a " + metodo}};
    }
    /// Junta los eventos que lleguen durante `ms`.
    void escuchar(int ms) {
        const auto fin = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
        std::string m;
        for (;;) {
            const auto falta = std::chrono::duration_cast<std::chrono::milliseconds>(fin - std::chrono::steady_clock::now()).count();
            if (falta <= 0) break;
            ws_.tiempo(static_cast<int>(std::max<long long>(50, falta)));
            if (!ws_.recibir(m)) break;
            json j = json::parse(m, nullptr, false);
            if (j.is_object() && j.contains("method")) eventos.push_back(std::move(j));
        }
    }
    std::vector<json> eventos;

   private:
    WebSocket ws_;
    int id_ = 0;
};

std::string minus(std::string s) {
    for (char& c : s) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
    return s;
}

/// Las pestañas (type page) del navegador con depuración en `puerto`; vacío si no hay.
json pestanas(int puerto) {
    const json j = json::parse(httpGet(puerto, "/json/list"), nullptr, false);
    json out = json::array();
    if (!j.is_array()) return out;
    for (const auto& t : j)
        if (t.value("type", std::string()) == "page") out.push_back(t);
    return out;
}

/// La pestaña de `sel` (id, número desde 1, o parte del título o la url); la primera si vacío.
bool elegir(const json& ps, const std::string& sel, json& out) {
    if (ps.empty()) return false;
    if (sel.empty()) {
        out = ps[0];
        return true;
    }
    for (const auto& t : ps)
        if (t.value("id", std::string()) == sel) return out = t, true;
    if (std::all_of(sel.begin(), sel.end(), [](char c) { return c >= '0' && c <= '9'; })) {
        const size_t n = static_cast<size_t>(std::stoul(sel));
        if (n >= 1 && n <= ps.size()) return out = ps[n - 1], true;
    }
    const std::string s = minus(sel);
    for (const auto& t : ps)
        if (minus(t.value("title", std::string())).find(s) != std::string::npos ||
            minus(t.value("url", std::string())).find(s) != std::string::npos)
            return out = t, true;
    return false;
}

std::wstring entorno(const wchar_t* v) {
    wchar_t b[MAX_PATH];
    const DWORD n = GetEnvironmentVariableW(v, b, MAX_PATH);
    return n > 0 && n < MAX_PATH ? std::wstring(b, n) : std::wstring();
}

/// El ejecutable de Chrome o Edge (de las carpetas de siempre), o vacío.
std::wstring ejecutableNavegador(const std::string& cual) {
    std::vector<std::wstring> c;
    if (cual == "edge") {
        for (const wchar_t* v : {L"ProgramFiles(x86)", L"ProgramFiles", L"LOCALAPPDATA"})
            if (auto b = entorno(v); !b.empty()) c.push_back(b + L"\\Microsoft\\Edge\\Application\\msedge.exe");
    } else {
        for (const wchar_t* v : {L"ProgramFiles", L"ProgramFiles(x86)", L"LOCALAPPDATA"})
            if (auto b = entorno(v); !b.empty()) c.push_back(b + L"\\Google\\Chrome\\Application\\chrome.exe");
    }
    for (const auto& x : c)
        if (GetFileAttributesW(x.c_str()) != INVALID_FILE_ATTRIBUTES) return x;
    return {};
}

std::string textoDeArgs(const json& args) {
    std::string out;
    for (const auto& x : args) {
        if (!out.empty()) out += ' ';
        if (x.contains("value")) out += x["value"].is_string() ? x["value"].get<std::string>() : x["value"].dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
        else if (x.contains("description")) out += x.value("description", std::string());
        else out += x.value("type", std::string());
    }
    return out;
}

/// Los mensajes de consola de los eventos (Console, Log, Runtime), sin repetir.
json mensajes(const std::vector<json>& evs) {
    json out = json::array();
    std::vector<std::string> vistos;
    const auto poner = [&](std::string nivel, const std::string& texto, std::string donde) {
        // El mismo mensaje llega por Console y por Runtime (con «dónde» distinto): uno solo.
        if (nivel == "warning") nivel = "warn";
        const std::string k = nivel + "|" + texto.substr(0, 300);
        if (donde.size() > 160) donde = donde.substr(0, 120) + "…" + donde.substr(donde.size() - 30);
        if (std::find(vistos.begin(), vistos.end(), k) != vistos.end()) return;
        vistos.push_back(k);
        json m{{"nivel", nivel}, {"texto", texto.size() > 2000 ? texto.substr(0, 2000) + "…" : texto}};
        if (!donde.empty()) m["donde"] = donde;
        out.push_back(m);
    };
    for (const auto& e : evs) {
        const std::string m = e.value("method", std::string());
        const json p = e.value("params", json::object());
        if (m == "Console.messageAdded") {
            const json x = p.value("message", json::object());
            const std::string url = x.value("url", std::string());
            poner(x.value("level", std::string("log")), x.value("text", std::string()),
                  url.empty() ? "" : url + ":" + std::to_string(x.value("line", 0)));
        } else if (m == "Log.entryAdded") {
            const json x = p.value("entry", json::object());
            const std::string url = x.value("url", std::string());
            poner(x.value("level", std::string("info")), x.value("text", std::string()),
                  url.empty() ? "" : url + ":" + std::to_string(x.value("lineNumber", 0)));
        } else if (m == "Runtime.consoleAPICalled") {
            std::string donde;
            const json st = p.value("stackTrace", json::object()).value("callFrames", json::array());
            if (!st.empty()) donde = st[0].value("url", std::string()) + ":" + std::to_string(st[0].value("lineNumber", 0) + 1);
            poner(p.value("type", std::string("log")), textoDeArgs(p.value("args", json::array())), donde);
        } else if (m == "Runtime.exceptionThrown") {
            const json x = p.value("exceptionDetails", json::object());
            std::string t = x.value("text", std::string());
            if (x.contains("exception")) t += " " + x["exception"].value("description", std::string());
            poner("error", t, x.value("url", std::string()) + ":" + std::to_string(x.value("lineNumber", 0) + 1));
        }
    }
    return out;
}

} // namespace

bool atenderChrome(const json& id, const std::string& nombre, const json& a, const Permisos& p, json& out) {
    if (nombre != "system_chrome") return false;
    if (!p.navegador) return out = sinPermiso(id, "navegador"), true;
    const std::string accion = a.value("accion", std::string("estado"));
    const int puerto = a.value("puerto", 9222);
    const json ps = pestanas(puerto);
    const auto sinDepuracion = [&]() {
        return resultadoTexto(id,
                              "No hay un navegador con depuración en el puerto " + std::to_string(puerto) +
                                  ". El Chrome que ya está abierto no deja conectarse (es el perfil del usuario): "
                                  "system_chrome {accion: abrir, url} abre uno propio del asistente.",
                              true);
    };

    if (accion == "estado") {
        json navs = json::array();
        for (const auto& w : ventanas()) {
            const std::string app = minus(ejecutableDe(w.pid));
            if (app == "chrome.exe" || app == "msedge.exe" || app == "brave.exe" || app == "firefox.exe")
                navs.push_back({{"id", reinterpret_cast<long long>(w.hwnd)}, {"titulo", w.titulo}, {"app", app}});
        }
        json d{{"ventanas_de_navegador", navs}, {"depuracion", !ps.empty()}, {"puerto", puerto}};
        if (!ps.empty()) {
            const json v = json::parse(httpGet(puerto, "/json/version"), nullptr, false);
            if (v.is_object()) d["version"] = v.value("Browser", std::string());
            json l = json::array();
            for (size_t i = 0; i < ps.size(); ++i)
                l.push_back({{"n", i + 1}, {"id", ps[i].value("id", "")}, {"titulo", ps[i].value("title", "")}, {"url", ps[i].value("url", "")}});
            d["pestanas"] = l;
        } else {
            d["nota"] = "para la consola o JavaScript: windows_chrome {accion: abrir} (un Chrome con depuración y perfil "
                        "propio). Para leer o tocar la página sin eso: windows_ui_leer / windows_ui_texto.";
        }
        out = resultadoDatos(id, d);
        return true;
    }

    if (accion == "abrir") {
        if (!ps.empty()) {
            out = resultadoTexto(id, "Ya hay un navegador con depuración en el puerto " + std::to_string(puerto) + " (" +
                                         std::to_string(ps.size()) + " pestañas). Para otra página: navegar.");
            return true;
        }
        const std::string cual = a.value("navegador", std::string("chrome"));
        std::wstring exe = ejecutableNavegador(cual);
        if (exe.empty() && cual == "chrome") exe = ejecutableNavegador("edge"); // sin Chrome, Edge (siempre está)
        if (exe.empty()) return out = resultadoTexto(id, "no encontré Chrome ni Edge instalados", true), true;
        // Un perfil propio del asistente: Chrome no deja depurar el perfil de siempre.
        std::wstring perfil;
        if (const std::string r = dataRoot(); !r.empty()) perfil = aW(r) + L"\\Navegador-asistente";
        else perfil = entorno(L"LOCALAPPDATA") + L"\\BroadcastAppliance\\Navegador-asistente";
        SHCreateDirectoryExW(nullptr, perfil.c_str(), nullptr);
        std::wstring args = L"--remote-debugging-port=" + std::to_wstring(puerto) + L" --user-data-dir=\"" + perfil +
                            L"\" --no-first-run --no-default-browser-check";
        if (const std::string u = a.value("url", std::string()); !u.empty()) args += L" \"" + aW(u) + L"\"";
        SHELLEXECUTEINFOW ei{};
        ei.cbSize = sizeof(ei);
        ei.lpVerb = L"open";
        ei.lpFile = exe.c_str();
        ei.lpParameters = args.c_str();
        ei.nShow = SW_SHOWNORMAL;
        if (!ShellExecuteExW(&ei)) return out = resultadoTexto(id, "no se pudo abrir el navegador (error " + std::to_string(GetLastError()) + ")", true), true;
        // Que levante el puerto.
        json ya;
        for (int i = 0; i < 50 && ya.empty(); ++i) {
            Sleep(200);
            ya = pestanas(puerto);
        }
        if (ya.empty()) return out = resultadoTexto(id, "El navegador se abrió pero no levantó la depuración en el puerto " + std::to_string(puerto) + ".", true), true;
        out = resultadoTexto(id, "Abierto " + aU(exe.substr(exe.find_last_of(L'\\') + 1)) + " con depuración (perfil del asistente), " +
                                     std::to_string(ya.size()) + " pestaña(s). Ahora: consola, evaluar, navegar.");
        return true;
    }

    if (ps.empty()) return out = sinDepuracion(), true;
    if (accion == "pestanas") {
        json l = json::array();
        for (size_t i = 0; i < ps.size(); ++i)
            l.push_back({{"n", i + 1}, {"id", ps[i].value("id", "")}, {"titulo", ps[i].value("title", "")}, {"url", ps[i].value("url", "")}});
        out = resultadoDatos(id, {{"pestanas", l}});
        return true;
    }
    json t;
    if (!elegir(ps, a.value("pestana", std::string()), t))
        return out = resultadoTexto(id, "no hay una pestaña «" + a.value("pestana", std::string()) + "» (pestanas las lista)", true), true;
    Cdp c;
    std::string error;
    if (!c.abrir(puerto, t.value("webSocketDebuggerUrl", std::string()), error))
        return out = resultadoTexto(id, error + " (¿otra herramienta tiene abierta las DevTools de esa pestaña?)", true), true;
    const std::string cual = "«" + t.value("title", std::string()) + "»";

    if (accion == "consola") {
        // Console y Log mandan lo ya guardado al habilitarse; Runtime, lo que venga.
        c.llamar("Runtime.enable");
        c.llamar("Log.enable");
        c.llamar("Console.enable");
        c.escuchar(static_cast<int>(std::clamp(a.value("segundos", 2.0), 0.0, 30.0) * 1000));
        const json ms = mensajes(c.eventos);
        int errores = 0;
        for (const auto& m : ms) errores += m.value("nivel", std::string()) == "error" ? 1 : 0;
        out = resultadoDatos(id, {{"pestana", t.value("title", "")}, {"url", t.value("url", "")}, {"mensajes", ms},
                                  {"total", ms.size()}, {"errores", errores}});
        return true;
    }
    if (accion == "evaluar") {
        const std::string codigo = a.value("codigo", std::string());
        if (codigo.empty()) return out = resultadoTexto(id, "falta `codigo`", true), true;
        const json r = c.llamar("Runtime.evaluate",
                                {{"expression", codigo}, {"returnByValue", true}, {"awaitPromise", true}, {"userGesture", true}}, 30000);
        if (r.contains("error")) return out = resultadoTexto(id, r.value("error", std::string()), true), true;
        if (r.contains("exceptionDetails")) {
            const json x = r["exceptionDetails"];
            std::string t2 = x.value("text", std::string());
            if (x.contains("exception")) t2 += " " + x["exception"].value("description", std::string());
            out = resultadoTexto(id, "Error de JavaScript en " + cual + ": " + t2, true);
            return true;
        }
        const json v = r.value("result", json::object());
        json d{{"tipo", v.value("type", std::string())}};
        if (v.contains("value")) d["valor"] = v["value"];
        else if (v.contains("description")) d["valor"] = v["description"];
        out = resultadoDatos(id, d);
        return true;
    }
    if (accion == "navegar") {
        const std::string u = a.value("url", std::string());
        if (u.empty()) return out = resultadoTexto(id, "falta `url`", true), true;
        const json r = c.llamar("Page.navigate", {{"url", u}});
        if (r.contains("error") || r.contains("errorText"))
            return out = resultadoTexto(id, "no se pudo ir a " + u + ": " + r.value("error", r.value("errorText", std::string())), true), true;
        out = resultadoTexto(id, "Navegando a " + u + " en " + cual + ".");
        return true;
    }
    if (accion == "recargar") {
        const json r = c.llamar("Page.reload", {{"ignoreCache", true}});
        if (r.contains("error")) return out = resultadoTexto(id, r.value("error", std::string()), true), true;
        out = resultadoTexto(id, "Recargando " + cual + ".");
        return true;
    }
    if (accion == "captura") {
        const json r = c.llamar("Page.captureScreenshot", {{"format", "png"}}, 30000);
        if (r.contains("error") || !r.contains("data")) return out = resultadoTexto(id, "no se pudo capturar " + cual, true), true;
        out = resultadoImagen(id, r.value("data", std::string()), "Página " + cual);
        return true;
    }
    out = resultadoTexto(id, "acción desconocida: " + accion, true);
    return true;
}

#else

bool atenderChrome(const json& id, const std::string& nombre, const json&, const Permisos&, json& out) {
    if (nombre != "system_chrome") return false;
    out = resultadoTexto(id, todaviaNo(nombre), true);
    return true;
}

#endif

} // namespace bactl
