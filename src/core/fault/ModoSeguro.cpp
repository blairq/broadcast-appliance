// ============================================================================
// src/core/fault/ModoSeguro.cpp — ver ModoSeguro.hpp
// ============================================================================
#include "fault/ModoSeguro.hpp"

#include "i18n/I18n.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>

namespace ba::fault {

namespace {

struct Info {
    Pieza p;
    const char* clave;
    const char* descripcion;
    int nivel;
};

const Info kInfo[] = {
    {Pieza::Superres, "superres", QT_TRANSLATE_NOOP("modo-seguro", "la superresolución"), 1},
    {Pieza::ClapTerceros, "clap", QT_TRANSLATE_NOOP("modo-seguro", "los plugins CLAP de terceros"), 1},
    {Pieza::PluginsPython, "python", QT_TRANSLATE_NOOP("modo-seguro", "los plugins de Python"), 1},
    {Pieza::Agentes, "agentes", QT_TRANSLATE_NOOP("modo-seguro", "los agentes"), 1},
    {Pieza::Modelos, "modelos", QT_TRANSLATE_NOOP("modo-seguro", "los modelos locales (voz, texto)"), 1},
    {Pieza::Ndi, "ndi", QT_TRANSLATE_NOOP("modo-seguro", "las entradas NDI"), 2},
    {Pieza::CapturaVentana, "ventanas", QT_TRANSLATE_NOOP("modo-seguro", "las capturas de ventana"), 2},
};

const Info& info(Pieza p) {
    for (const Info& i : kInfo)
        if (i.p == p) return i;
    return kInfo[0];
}

std::string minusculas(std::string_view s) {
    std::string out(s);
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

bool contiene(const std::string& s, const char* aguja) { return s.find(aguja) != std::string::npos; }

} // namespace

const std::vector<Pieza>& piezas() {
    static const std::vector<Pieza> todas = [] {
        std::vector<Pieza> v;
        for (const Info& i : kInfo) v.push_back(i.p);
        return v;
    }();
    return todas;
}

const char* clave(Pieza p) { return info(p).clave; }
const char* descripcion(Pieza p) { return info(p).descripcion; }
int nivelDe(Pieza p) { return info(p).nivel; }

std::optional<Pieza> piezaDeClave(std::string_view c) {
    for (const Info& i : kInfo)
        if (c == i.clave) return i.p;
    return std::nullopt;
}

std::vector<Pieza> ModoSeguro::apagadas() const {
    std::vector<Pieza> v;
    for (Pieza p : piezas())
        if (apagada(p)) v.push_back(p);
    return v;
}

std::vector<std::string> ModoSeguro::argumentos() const {
    std::vector<std::string> a;
    if (nivel > 0) a.push_back("--safe-level=" + std::to_string(std::min(nivel, kNivelMaximo)));
    if (!extra.empty()) {
        std::string s = "--safe-off=";
        bool primero = true;
        for (Pieza p : extra) {
            if (!primero) s += ',';
            s += clave(p);
            primero = false;
        }
        a.push_back(s);
    }
    return a;
}

std::string ModoSeguro::aviso() const {
    using ba::i18n::llenar;
    using ba::i18n::traducir;
    if (escenasVacias())
        return traducir("modo-seguro", "La aplicación se cerró varias veces seguidas y arrancó en modo seguro (nivel 3): tus "
                                       "escenas no se cargaron (no se borraron) y no se guarda nada. Cerrá y volvé a abrir para "
                                       "intentar el arranque normal.");
    std::string lista;
    const std::vector<Pieza> v = apagadas();
    for (std::size_t i = 0; i < v.size(); ++i) {
        if (i > 0) lista += i + 1 == v.size() ? traducir("modo-seguro", " y ") : std::string(", ");
        lista += traducir("modo-seguro", descripcion(v[i]));
    }
    return llenar(traducir("modo-seguro", "La aplicación se cerró varias veces seguidas y arrancó en modo seguro (nivel %1): "
                                          "tus escenas están, pero sin %2. Cerrá y volvé a abrir para el arranque normal."),
                  {std::to_string(nivel), lista});
}

ModoSeguro leerArgumentos(int argc, char** argv) {
    ModoSeguro m;
    for (int i = 1; i < argc; ++i) {
        const std::string_view a = argv[i] != nullptr ? argv[i] : "";
        if (a == "--safe-mode") {
            m.nivel = kNivelMaximo; // el de siempre
        } else if (a.rfind("--safe-level=", 0) == 0) {
            m.nivel = std::clamp(std::atoi(std::string(a.substr(13)).c_str()), 0, kNivelMaximo);
        } else if (a.rfind("--safe-off=", 0) == 0) {
            std::string_view resto = a.substr(11);
            while (!resto.empty()) {
                const std::size_t coma = resto.find(',');
                if (const auto p = piezaDeClave(resto.substr(0, coma))) m.extra.insert(*p);
                if (coma == std::string_view::npos) break;
                resto.remove_prefix(coma + 1);
            }
        }
    }
    return m;
}

ModoSeguro& delProceso() {
    static ModoSeguro m;
    return m;
}

bool esDelSistema(std::string_view modulo) {
    const std::string m = minusculas(modulo);
    if (m.empty() || m == "?") return true;
    // macOS, Linux y Windows: la biblioteca de C, los hilos, el cargador, el núcleo,
    // el runtime de C++ y el propio manejador de caídas (que está en el ejecutable,
    // pero lo marca su símbolo: ver `atribuir`).
    static const char* const kSistema[] = {
        "libsystem_", "libdyld", "dyld", "libc++", "libc.so", "libc-", "libpthread", "ld-linux", "libstdc++", "libgcc_s",
        "libunwind", "libobjc", "ntdll", "kernelbase", "kernel32", "ucrtbase", "vcruntime", "msvcp", "libglib-2",
        "libgobject-2", "libgthread", "libdispatch", "corefoundation", "foundation", "libsystem.b",
    };
    for (const char* s : kSistema)
        if (m.rfind(s, 0) == 0) return true;
    return false;
}

std::optional<Pieza> piezaDeTexto(std::string_view texto) {
    const std::string t = minusculas(texto);
    if (t.empty()) return std::nullopt;
    // La superresolución: el plugin de GStreamer y los motores que carga (ONNX con
    // DirectML, ncnn, CoreML/VT); en el historial, la mayoría de las caídas del motor.
    if (contiene(t, "superres") || contiene(t, "onnxruntime") || contiene(t, "directml") || contiene(t, "ncnn") ||
        contiene(t, "bafsr") || contiene(t, "onnxsr") || contiene(t, "coremlsr"))
        return Pieza::Superres;
    // NDI: libNoIDea y el descubrimiento (libmicrodns), adentro de la app.
    if (contiene(t, "noidea") || contiene(t, "microdns") || contiene(t, "bandi") || contiene(t, "ndi::") || contiene(t, "-ndi") ||
        t.rfind("ndi", 0) == 0)
        return Pieza::Ndi;
    if (contiene(t, ".clap") || contiene(t, "clap_entry")) return Pieza::ClapTerceros;
    if (contiene(t, "gstbacaptura") || contiene(t, "screencapturekit") || contiene(t, "ximagesrc") || contiene(t, "pipewire"))
        return Pieza::CapturaVentana;
    if (contiene(t, "sherpa") || contiene(t, "llama") || contiene(t, "ggml") || contiene(t, "whisper"))
        return Pieza::Modelos;
    if (contiene(t, "python")) return Pieza::PluginsPython;
    return std::nullopt;
}

Atribucion atribuir(const std::vector<Marco>& pila, std::string_view nombreHilo) {
    Atribucion a;
    for (const Marco& m : pila) {
        // El propio manejador (diag::Caidas) y abort/raise no son culpables.
        const std::string s = minusculas(m.simbolo);
        if (contiene(s, "ba::diag::") || esDelSistema(m.modulo)) continue;
        if (a.modulo.empty()) a.modulo = m.modulo;
        if (!a.pieza) a.pieza = piezaDeTexto(m.modulo);
        if (!a.pieza) a.pieza = piezaDeTexto(m.simbolo);
        if (a.pieza) break;
    }
    // Un hilo con nombre de pieza que se cae adentro de algo genérico (la app, libc).
    if (!a.pieza) a.pieza = piezaDeTexto(nombreHilo);
    return a;
}

} // namespace ba::fault
