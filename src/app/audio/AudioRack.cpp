// ============================================================================
// src/app/audio/AudioRack.cpp — ver AudioRack.hpp
// ============================================================================

#include "audio/AudioRack.hpp"

#include <chrono>
#include <thread>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <deque>
#include <set>
#include "ajustes/Ajustes.hpp"

namespace ba::app::audio {

using json = nlohmann::json;

namespace {

float powDb(float p) { return p > 1e-14f ? 10.f * std::log10(p) : -140.f; }

std::string slug(const std::string& s) {
    std::string out;
    for (unsigned char c : s) {
        if (std::isalnum(c))
            out += static_cast<char>(std::tolower(c));
        else if (!out.empty() && out.back() != '-')
            out += '-';
    }
    out = out.substr(0, 24);
    while (!out.empty() && out.back() == '-')
        out.pop_back();
    return out.empty() ? "modulo" : out;
}

const char kB64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

std::string b64(const std::vector<uint8_t>& d) {
    std::string out;
    out.reserve((d.size() + 2) / 3 * 4);
    for (size_t i = 0; i < d.size(); i += 3) {
        const uint32_t v =
            (d[i] << 16) | ((i + 1 < d.size() ? d[i + 1] : 0) << 8) | (i + 2 < d.size() ? d[i + 2] : 0);
        out += kB64[(v >> 18) & 63];
        out += kB64[(v >> 12) & 63];
        out += i + 1 < d.size() ? kB64[(v >> 6) & 63] : '=';
        out += i + 2 < d.size() ? kB64[v & 63] : '=';
    }
    return out;
}

std::vector<uint8_t> unb64(const std::string& s) {
    std::vector<uint8_t> out;
    uint32_t v = 0;
    int bits = 0;
    for (char c : s) {
        const char* p = std::strchr(kB64, c);
        if (c == '=' || p == nullptr || c == 0) continue;
        v = (v << 6) | static_cast<uint32_t>(p - kB64);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back(static_cast<uint8_t>((v >> bits) & 0xFF));
        }
    }
    return out;
}

} // namespace

// --- Nodos --------------------------------------------------------------------------------

struct AudioRack::Node {
    std::string id, kind, label, pluginId;
    std::string device;               // nodo "monitor": id del dispositivo de salida
    bool grabar = false;              // nodo "monitor": grabar lo que le llega
    std::string modo;                 // nodo "texto": "dictado" o "continuo"
    std::string flujo;                // nodo "texto": el nombre de su salida de texto
    json salidas = json::object();    // nodo "texto": archivo, rótulo
    std::string fuente;               // nodo "entrada": de qué fuente trae el audio
    json habla = json::object();      // nodo "habla": flujo, voz, hablante, velocidad
    json traduccion = json::object(); // nodo "traducir": desde, a, nombre, rotulo, archivo, activa, espera_ms
    std::shared_ptr<anillo::Ring> ring; // nodo "entrada": lo que llega de esa fuente
    std::unique_ptr<ClapInstance> clap;
    std::unique_ptr<Analyzer> analyzer;
    std::unique_ptr<Generator> gen;
    std::atomic<bool> bypass{false};
    uint32_t latencia = 0;      // la propia (CLAP_EXT_LATENCY), en muestras
    uint32_t latenciaSalida = 0; // la acumulada desde la entrada hasta su salida
    // Vigía: lo que tarda process() (ms) y el bypass automático por lento.
    std::atomic<float> msUltimo{0}, msMax{0}, msMedio{0};
    int lentos = 0; // bloques lentos seguidos (solo el hilo de audio)
    std::atomic<bool> bypassAuto{false};
    double x = 0, y = 0; // posicion en el patchbay
    // Medidores (el audio escribe potencia/pico lineales; se leen en dB).
    std::atomic<float> inPow{0}, outPow{0}, inPeak{0}, outPeak{0};

    int inputs() const {
        if (kind == "in" || kind == "generator" || kind == "entrada" || kind == "habla" || kind == "traducir")
            return 0;
        if (kind == "plugin") return static_cast<int>(clap->inputs().size());
        return 1;
    }
    int outputs() const {
        if (kind == "out" || kind == "monitor" || kind == "texto" || kind == "traducir") return 0;
        if (kind == "plugin") return static_cast<int>(clap->outputs().size());
        return 1;
    }
    int channels(int port, bool input) const {
        if (kind != "plugin") return 2;
        const auto& ps = input ? clap->inputs() : clap->outputs();
        return port < static_cast<int>(ps.size())
                   ? static_cast<int>(std::clamp<uint32_t>(ps[static_cast<size_t>(port)].channels, 1, 2))
                   : 2;
    }
};

// --- Plan (lo que recorre el hilo de audio) -----------------------------------------------

struct AudioRack::Plan {
    struct Buf {
        std::vector<float> l, r;
        float* ch[2];
        Buf() : l(kMaxFrames, 0.f), r(kMaxFrames, 0.f) {
            ch[0] = l.data();
            ch[1] = r.data();
        }
    };
    /// Una fuente de audio para un canal: de que buffer y de cual de sus canales.
    struct Src {
        int buf = -1;
        int ch = 0;
        // Compensación de latencia: esta rama llega `retardo` muestras antes que la
        // más lenta que se le junta, y se la atrasa eso (línea circular propia).
        uint32_t retardo = 0;
        std::vector<float> linea;
        uint32_t pos = 0;
    };
    struct InPort {
        std::vector<Src> l, r; // que se suma en cada canal (vacio si se lee directo)
        int buf = -1;          // lo que lee el nodo
        bool wired = false;    // tiene al menos un cable
        bool mix = false;      // hay que mezclar en `buf` (si no, se lee directo)
    };
    struct Step {
        Node* n = nullptr;
        std::vector<InPort> ins;
        std::vector<int> outs;
        std::vector<float* const*> inArr, outArr;
        std::shared_ptr<anillo::Ring> ring; // tarjeta de entrada
        // Voz a texto: su salida, su modo y lo que juntó en el último process().
        std::string flujo;
        int modo = 0;
        std::vector<float> txt;
        uint32_t txtFrames = 0;
    };
    std::vector<std::unique_ptr<Buf>> bufs;
    std::vector<Step> steps;
    int inBuf = -1;
    int outRead = -1; // el buffer que termina en la fuente
    int zero = -1;

    int newBuf() {
        bufs.push_back(std::make_unique<Buf>());
        return static_cast<int>(bufs.size()) - 1;
    }
};

// Grilla del patchbay: los nodos se acomodan en celdas para que queden
// alineados y los cables puedan correr por calles rectas entre columnas.
namespace {
constexpr double kGrid = 20.0;  // paso de la grilla
constexpr double kColW = 260.0; // 13 celdas: nodo (178) + calle para los cables
constexpr double kRowH = 260.0;
constexpr double kOrgX = 20.0, kOrgY = 60.0;
constexpr int kCols = 5; // columnas antes de saltar de fila

double snapGrid(double v) { return std::round(v / kGrid) * kGrid; }
} // namespace

// --- Construccion -------------------------------------------------------------------------

AudioRack::AudioRack(std::shared_ptr<ClapLibrary> lib, HostServices* services, double sampleRate)
    : lib_(std::move(lib)), services_(services), sr_(sampleRate) {
    for (const char* k : {"in", "out"}) {
        auto n = std::make_unique<Node>();
        n->id = k;
        n->kind = k;
        n->label = std::string(k) == "in" ? "Entrada" : "Salida";
        n->analyzer = std::make_unique<Analyzer>();
        n->analyzer->setSampleRate(sr_);
        n->x = std::string(k) == "in" ? 20 : 280;
        n->y = 80;
        nodes_.push_back(std::move(n));
    }
    conns_.push_back({"in", 0, "out", 0});
    monL_.assign(kMonitorCap, 0.0f);
    monR_.assign(kMonitorCap, 0.0f);
    rebuild();
    registrar(this, true);
}

AudioRack::~AudioRack() {
    registrar(this, false);
    {
        std::lock_guard<std::mutex> lk(planLock_);
        plan_.reset();
    }
    nodes_.clear(); // los plugins se detienen sin audio (ya no hay plan)
}

AudioRack::Node* AudioRack::find(const std::string& id) const {
    for (const auto& n : nodes_)
        if (n->id == id) return n.get();
    return nullptr;
}

bool AudioRack::has(const std::string& id) const { return find(id) != nullptr; }

std::vector<std::string> AudioRack::nodeIds() const {
    std::vector<std::string> out;
    for (const auto& n : nodes_)
        out.push_back(n->id);
    return out;
}

std::string AudioRack::kindOf(const std::string& id) const {
    const Node* n = find(id);
    return n != nullptr ? n->kind : std::string();
}

std::string AudioRack::newId(const std::string& base) const {
    const std::string b = slug(base);
    if (find(b) == nullptr) return b;
    for (int i = 2;; ++i) {
        const std::string id = b + "-" + std::to_string(i);
        if (find(id) == nullptr) return id;
    }
}

int AudioRack::portIndex(const Node& n, const std::string& port, bool input) const {
    const int count = input ? n.inputs() : n.outputs();
    if (port.empty() || port == (input ? "in" : "out")) return count > 0 ? 0 : -1;
    if (input && port == "sc") return count > 1 ? 1 : -1;
    // "in2", "out3"...
    const std::string pre = input ? "in" : "out";
    if (port.rfind(pre, 0) == 0) {
        const int k = std::atoi(port.c_str() + pre.size()) - 1;
        return k >= 0 && k < count ? k : -1;
    }
    return -1;
}

std::string AudioRack::portName(const Node& n, int idx, bool input) const {
    (void)n;
    if (idx == 0) return input ? "in" : "out";
    if (input && idx == 1) return "sc";
    return (input ? "in" : "out") + std::to_string(idx + 1);
}

bool AudioRack::wouldCycle(const std::string& from, const std::string& to) const {
    // ¿Hay camino de `to` a `from`? Entonces from->to cierra un ciclo.
    std::set<std::string> seen;
    std::deque<std::string> q{to};
    while (!q.empty()) {
        const std::string cur = q.front();
        q.pop_front();
        if (cur == from) return true;
        if (!seen.insert(cur).second) continue;
        for (const auto& c : conns_)
            if (c.from == cur) q.push_back(c.to);
    }
    return false;
}

double AudioRack::gridStep() { return kGrid; }

namespace {
int slotOf(double x, double y) {
    const int col = static_cast<int>(std::lround((x - kOrgX) / kColW));
    const int row = static_cast<int>(std::lround((y - kOrgY) / kRowH));
    return std::max(0, row) * kCols + std::clamp(col, 0, kCols - 1);
}
} // namespace

void AudioRack::place(Node& n) {
    // Generadores y escucha: fila propia abajo, uno al lado del otro.
    if (n.kind == "generator" || n.kind == "monitor" || n.kind == "texto" || n.kind == "entrada" ||
        n.kind == "habla" || n.kind == "traducir") {
        double gx = kOrgX, maxY = kOrgY;
        for (const auto& o : nodes_)
            if (o.get() != &n) maxY = std::max(maxY, o->y);
        for (const auto& o : nodes_)
            if (o.get() != &n && (o->kind == "generator" || o->kind == "monitor" || o->kind == "texto" ||
                                  o->kind == "entrada" || o->kind == "habla" || o->kind == "traducir"))
                gx = std::max(gx, o->x + kColW);
        n.x = snapGrid(gx);
        n.y = snapGrid(maxY + kRowH);
        return;
    }
    // El resto sigue a la cadena: celda siguiente a la ultima ocupada, y la
    // salida se corre una mas (cuando la fila se llena, sigue en la de abajo).
    int last = 0;
    for (const auto& o : nodes_)
        if (o.get() != &n && o->id != "out" && o->kind != "generator" && o->kind != "monitor" &&
            o->kind != "texto" && o->kind != "entrada" && o->kind != "habla" && o->kind != "traducir")
            last = std::max(last, slotOf(o->x, o->y));
    setSlot(n, last + 1);
    if (Node* out = find("out"); out != nullptr) setSlot(*out, last + 2);
}

void AudioRack::setSlot(Node& n, int slot) {
    n.x = snapGrid(kOrgX + kColW * (slot % kCols));
    n.y = snapGrid(kOrgY + kRowH * (slot / kCols));
}

void AudioRack::layoutGrid() {
    // Orden: entrada, la cadena principal, lo que quedo patcheado aparte, salida.
    std::vector<std::string> seq{"in"};
    for (const auto& id : mainChain())
        seq.push_back(id);
    for (const auto& n : nodes_) {
        if (n->kind == "in" || n->kind == "out" || n->kind == "generator" || n->kind == "monitor" ||
            n->kind == "texto" || n->kind == "entrada" || n->kind == "habla" || n->kind == "traducir")
            continue;
        if (std::find(seq.begin(), seq.end(), n->id) == seq.end()) seq.push_back(n->id);
    }
    seq.push_back("out");
    for (size_t i = 0; i < seq.size(); ++i)
        if (Node* n = find(seq[i]); n != nullptr) setSlot(*n, static_cast<int>(i));
    // Generadores y escucha: la fila de abajo.
    const int row = (static_cast<int>(seq.size()) + kCols - 1) / kCols;
    int i = 0;
    for (const auto& n : nodes_)
        if (n->kind == "generator" || n->kind == "monitor" || n->kind == "texto" || n->kind == "entrada" ||
            n->kind == "habla" || n->kind == "traducir")
            setSlot(*n, row * kCols + i++);
}

void AudioRack::insertInChain(const std::string& id, const std::string& after) {
    if (after.empty()) {
        // Justo antes de la salida: lo que llegaba a out ahora llega al nodo.
        std::vector<Conn> into;
        for (const auto& c : conns_)
            if (c.to == "out" && c.toPort == 0) into.push_back(c);
        conns_.erase(std::remove_if(conns_.begin(), conns_.end(),
                                    [](const Conn& c) { return c.to == "out" && c.toPort == 0; }),
                     conns_.end());
        if (into.empty()) into.push_back({"in", 0, "out", 0});
        for (const auto& c : into)
            conns_.push_back({c.from, c.fromPort, id, 0});
        conns_.push_back({id, 0, "out", 0});
        return;
    }
    std::vector<Conn> outs;
    for (const auto& c : conns_)
        if (c.from == after && c.fromPort == 0) outs.push_back(c);
    conns_.erase(std::remove_if(conns_.begin(), conns_.end(),
                                [&](const Conn& c) { return c.from == after && c.fromPort == 0; }),
                 conns_.end());
    conns_.push_back({after, 0, id, 0});
    if (outs.empty()) outs.push_back({after, 0, "out", 0});
    for (const auto& c : outs)
        conns_.push_back({id, 0, c.to, c.toPort});
}

std::string AudioRack::addPlugin(const std::string& pluginId, const std::string& label,
                                 const std::string& after, std::string* error, const std::string& idBase) {
    if (!after.empty() && (find(after) == nullptr || after == "out")) {
        if (error != nullptr) *error = "no existe el nodo " + after;
        return {};
    }
    auto inst = ClapInstance::create(lib_, pluginId, services_, error);
    if (!inst) return {};
    if (inst->outputs().empty()) {
        if (error != nullptr) *error = pluginId + " no tiene salida de audio";
        return {};
    }
    if (!inst->activate(sr_, kMaxFrames)) {
        if (error != nullptr) *error = "no se pudo activar " + pluginId;
        return {};
    }
    auto n = std::make_unique<Node>();
    n->kind = "plugin";
    n->pluginId = pluginId;
    n->label = label.empty() ? inst->desc().name : label;
    n->id = newId(!idBase.empty() ? idBase : label.empty() ? inst->desc().name : label);
    n->clap = std::move(inst);
    const std::string id = n->id;
    nodes_.push_back(std::move(n));
    place(*nodes_.back());
    insertInChain(id, after);
    rebuild();
    return id;
}

std::string AudioRack::addAnalyzer(const std::string& label, const std::string& after) {
    if (!after.empty() && (find(after) == nullptr || after == "out")) return {};
    auto n = std::make_unique<Node>();
    n->kind = "analyzer";
    n->label = label.empty() ? "Analizador" : label;
    n->id = newId(label.empty() ? "analizador" : label);
    n->analyzer = std::make_unique<Analyzer>();
    n->analyzer->setSampleRate(sr_);
    const std::string id = n->id;
    nodes_.push_back(std::move(n));
    place(*nodes_.back());
    insertInChain(id, after);
    rebuild();
    return id;
}

std::string AudioRack::addOutput(const std::string& device, const std::string& label) {
    // Una sola por rack: si ya hay, se le cambia el dispositivo destino.
    if (Node* e = find("salida-audio"); e != nullptr) {
        e->device = device;
        e->label = label.empty() ? "Salida de audio" : label;
        return e->id;
    }
    auto n = std::make_unique<Node>();
    n->id = "salida-audio";
    n->kind = "monitor";
    n->device = device;
    n->label = label.empty() ? "Salida de audio" : label;
    // Debajo de la salida: es el otro destino posible del audio.
    const Node* out = find("out");
    n->x = out != nullptr ? out->x : 280;
    n->y = (out != nullptr ? out->y : 80) + 150;
    nodes_.push_back(std::move(n));
    rebuild(); // sin cable: lo patchea el operador desde donde quiera oir
    return "salida-audio";
}

bool AudioRack::setGrabar(bool grabar) {
    Node* n = find("salida-audio");
    if (n == nullptr) return false;
    n->grabar = grabar;
    return true;
}

bool AudioRack::grabar() const {
    const Node* n = find("salida-audio");
    return n != nullptr && n->grabar;
}

std::string AudioRack::addTexto(const std::string& modo, const std::string& flujo) {
    const std::string m = modo == "continuo" ? "continuo" : "dictado";
    auto n = std::make_unique<Node>();
    n->id = newId("voz-a-texto");
    n->kind = "texto";
    n->modo = m;
    n->flujo = flujo;
    n->label = "Voz a texto: " + flujo;
    const std::string id = n->id;
    nodes_.push_back(std::move(n));
    place(*nodes_.back());
    // Se conecta a lo mismo que llega a la mezcla: lo que se oye, ya
    // procesado (compuerta, EQ...). Después se repatchea donde se quiera.
    std::string desde = "in", puerto = "out";
    for (const Conn& c : conns_)
        if (c.to == "out") {
            if (const Node* a = find(c.from); a != nullptr) {
                desde = c.from;
                puerto = portName(*a, c.fromPort, false);
            }
            break;
        }
    rebuild();
    std::string e;
    connect(desde, puerto, id, "in", &e);
    return id;
}

bool AudioRack::setTextoModo(const std::string& id, const std::string& modo) {
    Node* n = find(id);
    if (n == nullptr || n->kind != "texto") return false;
    n->modo = modo == "continuo" ? "continuo" : "dictado";
    rebuild();
    return true;
}

bool AudioRack::setTextoFlujo(const std::string& id, const std::string& flujo) {
    Node* n = find(id);
    if (n == nullptr || n->kind != "texto") return false;
    if (n->label == "Voz a texto: " + n->flujo || n->label.rfind("Voz a texto", 0) == 0)
        n->label = "Voz a texto: " + flujo;
    n->flujo = flujo;
    rebuild();
    return true;
}

bool AudioRack::setTextoSalidas(const std::string& id, const json& salidas) {
    Node* n = find(id);
    if (n == nullptr || n->kind != "texto" || !salidas.is_object()) return false;
    n->salidas = salidas;
    return true;
}

std::vector<AudioRack::TextoInfo> AudioRack::textos() const {
    std::vector<TextoInfo> out;
    for (const auto& n : nodes_)
        if (n->kind == "texto") out.push_back({n->id, n->flujo, n->modo, n->salidas});
    return out;
}

std::string AudioRack::textoModo() const {
    for (const auto& n : nodes_)
        if (n->kind == "texto") return n->modo;
    return {};
}

void AudioRack::textosCapturados(
    const std::function<void(const std::string&, int, const float*, uint32_t)>& fn) {
    std::unique_lock<std::mutex> lk(planLock_, std::try_to_lock);
    if (!lk.owns_lock() || !plan_) return;
    for (auto& st : plan_->steps)
        if (st.modo > 0 && st.txtFrames > 0) fn(st.flujo, st.modo, st.txt.data(), st.txtFrames);
}

bool AudioRack::hasOutput() const { return find("salida-audio") != nullptr; }

std::string AudioRack::outputDevice() const {
    const Node* n = find("salida-audio");
    return n != nullptr ? n->device : std::string();
}

const float* AudioRack::monitorPlane(int ch) const { return ch == 0 ? monL_.data() : monR_.data(); }

std::string AudioRack::addEntrada(const std::string& fuente, const std::string& label) {
    auto n = std::make_unique<Node>();
    n->kind = "entrada";
    n->fuente = fuente;
    n->label = label.empty() ? "Entrada: " + fuente : label;
    n->id = newId("entrada");
    const std::string id = n->id;
    nodes_.push_back(std::move(n));
    place(*nodes_.back());
    rebuild();
    return id;
}

bool AudioRack::setEntradaFuente(const std::string& id, const std::string& fuente) {
    Node* n = find(id);
    if (n == nullptr || n->kind != "entrada") return false;
    if (n->label == "Entrada: " + n->fuente) n->label = "Entrada: " + fuente;
    n->fuente = fuente;
    n->ring.reset(); // el anillo nuevo lo trae RackManager
    rebuild();
    return true;
}

std::vector<std::pair<std::string, std::string>> AudioRack::entradas() const {
    std::vector<std::pair<std::string, std::string>> out;
    for (const auto& n : nodes_)
        if (n->kind == "entrada") out.emplace_back(n->id, n->fuente);
    return out;
}

void AudioRack::setEntradaAnillo(const std::string& id, std::shared_ptr<anillo::Ring> ring) {
    Node* n = find(id);
    if (n == nullptr || (n->kind != "entrada" && n->kind != "habla") || n->ring == ring) return;
    n->ring = std::move(ring);
    rebuild();
}

std::string AudioRack::addHabla(const json& opciones) {
    auto n = std::make_unique<Node>();
    n->kind = "habla";
    n->id = newId("texto-a-voz");
    n->habla = json{{"flujo", ""}, {"voz", ""}, {"hablante", 0}, {"velocidad", 1.0}};
    if (opciones.is_object())
        for (auto it = opciones.begin(); it != opciones.end(); ++it)
            n->habla[it.key()] = it.value();
    n->label = "Texto a voz";
    const std::string id = n->id;
    nodes_.push_back(std::move(n));
    place(*nodes_.back());
    rebuild();
    return id;
}

bool AudioRack::setHablaOpciones(const std::string& id, const json& cambios) {
    Node* n = find(id);
    if (n == nullptr || n->kind != "habla" || !cambios.is_object()) return false;
    for (auto it = cambios.begin(); it != cambios.end(); ++it)
        n->habla[it.key()] = it.value();
    return true;
}

json AudioRack::hablas() const {
    json out = json::array();
    for (const auto& n : nodes_)
        if (n->kind == "habla") {
            json h = n->habla;
            h["id"] = n->id;
            out.push_back(h);
        }
    return out;
}

namespace {
/// «Traducir → EN» (el título de la tarjeta dice a qué idioma).
std::string etiquetaTraduccion(const json& t) {
    std::string a = t.value("a", std::string());
    for (char& c : a)
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return a.empty() ? "Traducir" : "Traducir → " + a;
}
} // namespace

std::string AudioRack::addTraduccion(const json& opciones) {
    auto n = std::make_unique<Node>();
    n->kind = "traducir";
    n->id = newId("traducir");
    n->traduccion = json{{"desde", ""},    {"idioma_desde", "es"}, {"a", "en"},
                         {"nombre", ""},   {"rotulo", ""},         {"archivo", false},
                         {"activa", true}, {"espera_ms", 600},     {"maximo_ms", 8000}};
    if (opciones.is_object())
        for (auto it = opciones.begin(); it != opciones.end(); ++it)
            n->traduccion[it.key()] = it.value();
    n->label = etiquetaTraduccion(n->traduccion);
    const std::string id = n->id;
    nodes_.push_back(std::move(n));
    place(*nodes_.back());
    rebuild();
    return id;
}

bool AudioRack::setTraduccionOpciones(const std::string& id, const json& cambios) {
    Node* n = find(id);
    if (n == nullptr || n->kind != "traducir" || !cambios.is_object()) return false;
    for (auto it = cambios.begin(); it != cambios.end(); ++it)
        n->traduccion[it.key()] = it.value();
    n->label = etiquetaTraduccion(n->traduccion);
    return true;
}

json AudioRack::traducciones() const {
    json out = json::array();
    for (const auto& n : nodes_)
        if (n->kind == "traducir") {
            json t = n->traduccion;
            t["id"] = n->id;
            out.push_back(t);
        }
    return out;
}

std::string AudioRack::addGenerator(const std::string& label) {
    auto n = std::make_unique<Node>();
    n->kind = "generator";
    n->label = label.empty() ? "Generador" : label;
    n->id = newId(label.empty() ? "generador" : label);
    n->gen = std::make_unique<Generator>();
    n->gen->setSampleRate(sr_);
    Generator::Config silent;
    silent.kind = Generator::Kind::Silence;
    n->gen->configure(silent);
    const std::string id = n->id;
    nodes_.push_back(std::move(n));
    place(*nodes_.back());
    rebuild();
    return id;
}

bool AudioRack::remove(const std::string& id, std::string* error) {
    if (id == "in" || id == "out") {
        if (error != nullptr) *error = "la entrada y la salida no se quitan";
        return false;
    }
    Node* n = find(id);
    if (n == nullptr) {
        if (error != nullptr) *error = "no existe el nodo " + id;
        return false;
    }
    // Puente: lo que entraba por su entrada principal va a donde salia.
    std::vector<Conn> srcs, dsts;
    for (const auto& c : conns_) {
        if (c.to == id && c.toPort == 0) srcs.push_back(c);
        if (c.from == id && c.fromPort == 0) dsts.push_back(c);
    }
    conns_.erase(std::remove_if(conns_.begin(), conns_.end(),
                                [&](const Conn& c) { return c.from == id || c.to == id; }),
                 conns_.end());
    for (const auto& s : srcs)
        for (const auto& d : dsts) {
            const bool dup = std::any_of(conns_.begin(), conns_.end(), [&](const Conn& c) {
                return c.from == s.from && c.fromPort == s.fromPort && c.to == d.to && c.toPort == d.toPort;
            });
            if (!dup && !wouldCycle(s.from, d.to)) conns_.push_back({s.from, s.fromPort, d.to, d.toPort});
        }
    // Primero sale del audio (plan nuevo), despues se destruye aca.
    std::unique_ptr<Node> dead;
    for (auto it = nodes_.begin(); it != nodes_.end(); ++it)
        if ((*it)->id == id) {
            dead = std::move(*it);
            nodes_.erase(it);
            break;
        }
    rebuild();
    dead.reset();
    return true;
}

bool AudioRack::enParalelo(const std::string& id) {
    if (id == "in" || id == "out" || find(id) == nullptr) return false;
    std::vector<Conn> srcs, dsts;
    for (const auto& c : conns_) {
        if (c.to == id && c.toPort == 0) srcs.push_back(c);
        if (c.from == id && c.fromPort == 0) dsts.push_back(c);
    }
    conns_.erase(std::remove_if(conns_.begin(), conns_.end(),
                                [&](const Conn& c) { return c.from == id && c.fromPort == 0; }),
                 conns_.end());
    for (const auto& s : srcs)
        for (const auto& d : dsts) {
            const bool dup = std::any_of(conns_.begin(), conns_.end(), [&](const Conn& c) {
                return c.from == s.from && c.fromPort == s.fromPort && c.to == d.to && c.toPort == d.toPort;
            });
            if (!dup && !wouldCycle(s.from, d.to)) conns_.push_back({s.from, s.fromPort, d.to, d.toPort});
        }
    rebuild();
    return true;
}

bool AudioRack::connect(const std::string& from, const std::string& fromPort, const std::string& to,
                        const std::string& toPort, std::string* error, int fromCh, int toCh) {
    Node* a = find(from);
    Node* b = find(to);
    if (a == nullptr || b == nullptr) {
        if (error != nullptr) *error = "no existe " + std::string(a == nullptr ? from : to);
        return false;
    }
    const int pa = portIndex(*a, fromPort, false);
    const int pb = portIndex(*b, toPort, true);
    if (pa < 0 || pb < 0) {
        if (error != nullptr)
            *error = pa < 0 ? from + " no tiene la salida '" + fromPort + "'"
                            : to + " no tiene la entrada '" + toPort + "'";
        return false;
    }
    if (from == to || wouldCycle(from, to)) {
        if (error != nullptr) *error = "ese cable cerraria un ciclo";
        return false;
    }
    // Un cable de puerto entero reemplaza a los de canal sueltos entre los
    // mismos puertos (y al reves): si no, se sumarian dos veces.
    const bool whole = fromCh < 0 || toCh < 0;
    for (const auto& c : conns_)
        if (c.from == from && c.fromPort == pa && c.to == to && c.toPort == pb && c.fromCh == fromCh &&
            c.toCh == toCh)
            return true;
    conns_.erase(std::remove_if(conns_.begin(), conns_.end(),
                                [&](const Conn& c) {
                                    if (c.from != from || c.fromPort != pa || c.to != to || c.toPort != pb)
                                        return false;
                                    const bool cWhole = c.fromCh < 0 || c.toCh < 0;
                                    return whole || cWhole || c.toCh == toCh;
                                }),
                 conns_.end());
    conns_.push_back({from, pa, to, pb, whole ? -1 : fromCh, whole ? -1 : toCh});
    rebuild();
    return true;
}

bool AudioRack::disconnect(const std::string& from, const std::string& fromPort, const std::string& to,
                           const std::string& toPort, int fromCh, int toCh) {
    Node* a = find(from);
    Node* b = find(to);
    if (a == nullptr || b == nullptr) return false;
    const int pa = portIndex(*a, fromPort, false);
    const int pb = portIndex(*b, toPort, true);
    const size_t before = conns_.size();
    conns_.erase(std::remove_if(conns_.begin(), conns_.end(),
                                [&](const Conn& c) {
                                    return c.from == from && c.to == to && (pa < 0 || c.fromPort == pa) &&
                                           (pb < 0 || c.toPort == pb) && (fromCh < 0 || c.fromCh == fromCh) &&
                                           (toCh < 0 || c.toCh == toCh);
                                }),
                 conns_.end());
    if (conns_.size() == before) return false;
    rebuild();
    return true;
}

bool AudioRack::setBypass(const std::string& id, bool bypass) {
    Node* n = find(id);
    if (n == nullptr || n->kind != "plugin") return false;
    n->bypass = bypass;
    return true;
}

bool AudioRack::rename(const std::string& id, const std::string& label) {
    Node* n = find(id);
    if (n == nullptr || label.empty()) return false;
    n->label = label;
    return true;
}

bool AudioRack::setPosition(const std::string& id, double x, double y) {
    Node* n = find(id);
    if (n == nullptr) return false;
    n->x = snapGrid(std::max(0.0, x));
    n->y = snapGrid(std::max(0.0, y));
    return true;
}

ClapInstance* AudioRack::plugin(const std::string& id) {
    Node* n = find(id);
    return n != nullptr && n->kind == "plugin" ? n->clap.get() : nullptr;
}

int AudioRack::cerrarEditores() {
    int n = 0;
    for (auto& node : nodes_)
        if (node->clap && node->clap->editorOpen()) {
            node->clap->closeEditor();
            ++n;
        }
    return n;
}

Analyzer* AudioRack::analyzer(const std::string& id) {
    Node* n = find(id);
    return n != nullptr ? n->analyzer.get() : nullptr;
}

Generator* AudioRack::generator(const std::string& id) {
    Node* n = find(id);
    return n != nullptr ? n->gen.get() : nullptr;
}

// --- Plan ---------------------------------------------------------------------------------

void AudioRack::rebuild() {
    auto plan = std::make_unique<Plan>();
    plan->zero = plan->newBuf();
    // Orden topologico (Kahn); los nodos sin cables entran igual.
    std::map<std::string, int> indeg;
    for (const auto& n : nodes_)
        indeg[n->id] = 0;
    for (const auto& c : conns_)
        ++indeg[c.to];
    std::deque<Node*> ready;
    for (const auto& n : nodes_)
        // «Traducir» no tiene audio: no entra al proceso.
        if (indeg[n->id] == 0 && n->kind != "traducir") ready.push_back(n.get());
    std::vector<Node*> order;
    while (!ready.empty()) {
        Node* n = ready.front();
        ready.pop_front();
        order.push_back(n);
        for (const auto& c : conns_)
            if (c.from == n->id && --indeg[c.to] == 0) ready.push_back(find(c.to));
    }
    // Latencia acumulada (en orden topológico): la de cada nodo es la de su
    // entrada más lenta más la propia. En una mezcla, las ramas más rápidas se
    // atrasan para llegar juntas (así un compresor con lookahead en paralelo no
    // hace filtro peine con la señal seca).
    for (Node* n : order) {
        uint32_t entra = 0;
        for (const auto& c : conns_)
            if (c.to == n->id)
                if (const Node* a = find(c.from)) entra = std::max(entra, a->latenciaSalida);
        n->latencia = n->kind == "plugin" && n->clap ? n->clap->latencia() : 0;
        n->latenciaSalida = entra + n->latencia;
        if (n->kind == "out") latenciaTotal_ = entra;
    }
    // Tope de la compensación de latencia entre ramas (2 s a 48 kHz: más es un error).
    const uint32_t kMaxRetardo = static_cast<uint32_t>(std::max(0, ajustes::entero("audio.compensacionMaxMs", 2000))) * 48u;
    const auto retardoDe = [&](const std::string& desde, const std::string& hacia) -> uint32_t {
        const Node* a = find(desde);
        const Node* b = find(hacia);
        if (a == nullptr || b == nullptr) return 0;
        const uint32_t llega = b->latenciaSalida - b->latencia;
        return std::min(kMaxRetardo, llega > a->latenciaSalida ? llega - a->latenciaSalida : 0);
    };
    // Buffers de salida de cada nodo.
    std::map<std::pair<std::string, int>, int> outBuf;
    for (Node* n : order)
        for (int p = 0; p < n->outputs(); ++p)
            outBuf[{n->id, p}] = plan->newBuf();
    for (Node* n : order) {
        Plan::Step st;
        st.n = n;
        for (int p = 0; p < n->inputs(); ++p) {
            Plan::InPort ip;
            for (const auto& c : conns_) {
                if (c.to != n->id || c.toPort != p) continue;
                auto it = outBuf.find({c.from, c.fromPort});
                if (it == outBuf.end()) continue;
                const int b = it->second;
                const uint32_t ret = retardoDe(c.from, c.to);
                const auto src = [ret](int buf, int ch) {
                    Plan::Src s;
                    s.buf = buf;
                    s.ch = ch;
                    s.retardo = ret;
                    if (ret > 0) s.linea.assign(ret, 0.0f);
                    return s;
                };
                if (c.fromCh < 0 || c.toCh < 0) { // puerto entero: L->L, R->R
                    ip.l.push_back(src(b, 0));
                    ip.r.push_back(src(b, 1));
                } else if (c.toCh == 0) {
                    ip.l.push_back(src(b, c.fromCh));
                } else {
                    ip.r.push_back(src(b, c.fromCh));
                }
            }
            ip.wired = !ip.l.empty() || !ip.r.empty();
            // Un solo cable de puerto entero: se lee el buffer de la fuente sin
            // copiar (el caso de siempre). Todo lo demas se mezcla aparte.
            const bool plain = ip.l.size() == 1 && ip.r.size() == 1 && ip.l[0].buf == ip.r[0].buf &&
                               ip.l[0].ch == 0 && ip.r[0].ch == 1 && ip.l[0].retardo == 0;
            if (!ip.wired) {
                ip.buf = plan->zero;
            } else if (plain) {
                ip.buf = ip.l[0].buf;
                ip.l.clear();
                ip.r.clear();
            } else {
                ip.buf = plan->newBuf();
                ip.mix = true;
            }
            st.ins.push_back(std::move(ip));
        }
        for (int p = 0; p < n->outputs(); ++p)
            st.outs.push_back(outBuf[{n->id, p}]);
        if (n->kind == "entrada" || n->kind == "habla") st.ring = n->ring;
        if (n->kind == "texto") {
            st.flujo = n->flujo;
            st.modo = n->modo == "continuo" ? 2 : 1;
            st.txt.assign(kMonitorCap, 0.0f);
        }
        if (n->kind == "in") plan->inBuf = st.outs[0];
        if (n->kind == "out") plan->outRead = st.ins[0].buf;
        plan->steps.push_back(std::move(st));
    }
    // Punteros por puerto (los buffers ya no se mueven).
    for (auto& st : plan->steps) {
        for (auto& ip : st.ins)
            st.inArr.push_back(plan->bufs[static_cast<size_t>(ip.buf)]->ch);
        for (int ob : st.outs)
            st.outArr.push_back(plan->bufs[static_cast<size_t>(ob)]->ch);
    }
    std::unique_ptr<Plan> old;
    {
        std::lock_guard<std::mutex> lk(planLock_);
        old = std::move(plan_);
        plan_ = std::move(plan);
    }
}

namespace {

void meterInto(std::atomic<float>& pow, std::atomic<float>& peak, float* const* ch, uint32_t frames,
               double alpha) {
    double sq = 0;
    float pk = 0;
    for (int c = 0; c < 2; ++c)
        for (uint32_t i = 0; i < frames; ++i) {
            sq += static_cast<double>(ch[c][i]) * ch[c][i];
            pk = std::max(pk, std::fabs(ch[c][i]));
        }
    // AES: el RMS de un seno igual a su pico (x2 en potencia).
    const float blockPow = static_cast<float>(2.0 * sq / (2.0 * frames));
    pow.store(static_cast<float>(pow.load() * (1 - alpha) + blockPow * alpha), std::memory_order_relaxed);
    peak.store(std::max(pk, peak.load() * static_cast<float>(1 - alpha)), std::memory_order_relaxed);
}

} // namespace

namespace {
std::atomic<int> g_umbralPct{50}, g_bloques{20};
// El candado y la lista viven lo que el proceso (nunca se destruyen): el hilo vigía
// está suelto y sigue corriendo mientras el proceso termina; con un std::mutex
// global, al destruirse los globales su lock fallaba («mutex lock failed:
// Invalid argument») y la app se caía al cerrar (2026-10-13, al reiniciar para
// abrir un evento nuevo).
std::mutex& racksMu() {
    static auto* m = new std::mutex();
    return *m;
}
std::set<AudioRack*>* g_racks = nullptr;
std::atomic<bool> g_vigiaAndando{false};
std::int64_t ahoraNsRack() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch())
        .count();
}
} // namespace

void AudioRack::registrar(AudioRack* r, bool alta) {
    std::lock_guard<std::mutex> l(racksMu());
    if (g_racks == nullptr) g_racks = new std::set<AudioRack*>();
    if (alta)
        g_racks->insert(r);
    else
        g_racks->erase(r);
    // Un solo hilo vigía para todos los racks: cada 50 ms mira si alguno está
    // adentro de un process() hace más de 200 ms.
    if (alta && !g_vigiaAndando.exchange(true))
        std::thread([] {
            for (;;) {
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
                std::lock_guard<std::mutex> l2(racksMu());
                const std::int64_t ahora = ahoraNsRack();
                for (AudioRack* x : *g_racks) {
                    const std::int64_t desde = x->enProcesoDesde_.load();
                    if (desde != 0 && ahora - desde > 200'000'000LL && !x->colgado_.exchange(true)) {
                        std::lock_guard<std::mutex> l3(x->saltadosMu_);
                        x->saltados_.push_back("rack colgado (un módulo no volvió en 200 ms)");
                    }
                }
            }
        }).detach();
}

void AudioRack::rehacerTodos() {
    std::vector<AudioRack*> todos;
    {
        std::lock_guard<std::mutex> l(racksMu());
        if (g_racks != nullptr) todos.assign(g_racks->begin(), g_racks->end());
    }
    for (AudioRack* r : todos) r->rebuild();
}

AudioRack::json AudioRack::vigia() { return {{"umbral_pct", g_umbralPct.load()}, {"bloques", g_bloques.load()}}; }

std::string AudioRack::ponerVigia(const json& v) {
    if (v.contains("umbral_pct")) g_umbralPct = std::clamp(v.value("umbral_pct", 50), 10, 200);
    if (v.contains("bloques")) g_bloques = std::clamp(v.value("bloques", 20), 1, 1000);
    return {};
}

bool AudioRack::rearmar(const std::string& nodo) {
    colgado_ = false;
    Node* n = find(nodo);
    if (n == nullptr) return false;
    n->bypassAuto = false;
    n->msMax = 0;
    return true;
}

std::vector<std::string> AudioRack::tomarSaltados() {
    std::lock_guard<std::mutex> l(saltadosMu_);
    std::vector<std::string> v;
    v.swap(saltados_);
    return v;
}

void AudioRack::process(float* const* planes, int channels, uint32_t frames) {
    monFrames_ = 0;
    // Colgado (un módulo no volvió a tiempo): la señal pasa seca sin entrar.
    if (colgado_.load(std::memory_order_relaxed)) return;
    std::unique_lock<std::mutex> lk(planLock_, std::try_to_lock);
    if (!lk.owns_lock() || !plan_ || plan_->inBuf < 0 || plan_->outRead < 0 || channels < 1) return;
    Plan& P = *plan_;
    for (auto& st : P.steps)
        st.txtFrames = 0;
    for (uint32_t off = 0; off < frames; off += kMaxFrames) {
        const uint32_t n = std::min(kMaxFrames, frames - off);
        const double alpha = std::min(1.0, n / (0.3 * sr_));
        Plan::Buf& in = *P.bufs[static_cast<size_t>(P.inBuf)];
        std::memcpy(in.l.data(), planes[0] + off, n * sizeof(float));
        std::memcpy(in.r.data(), planes[channels > 1 ? 1 : 0] + off, n * sizeof(float));
        for (auto& st : P.steps) {
            Node& node = *st.n;
            for (auto& ip : st.ins) {
                if (!ip.mix) continue;
                Plan::Buf& dst = *P.bufs[static_cast<size_t>(ip.buf)];
                std::memset(dst.l.data(), 0, n * sizeof(float));
                std::memset(dst.r.data(), 0, n * sizeof(float));
                const auto add = [&](std::vector<Plan::Src>& srcs, std::vector<float>& out) {
                    for (Plan::Src& s : srcs) {
                        const Plan::Buf& src = *P.bufs[static_cast<size_t>(s.buf)];
                        const std::vector<float>& from = s.ch == 0 ? src.l : src.r;
                        if (s.retardo == 0) {
                            for (uint32_t i = 0; i < n; ++i)
                                out[i] += from[i];
                            continue;
                        }
                        for (uint32_t i = 0; i < n; ++i) { // línea de retardo: sale lo de hace `retardo`
                            out[i] += s.linea[s.pos];
                            s.linea[s.pos] = from[i];
                            if (++s.pos == s.retardo) s.pos = 0;
                        }
                    }
                };
                add(ip.l, dst.l);
                add(ip.r, dst.r);
            }
            if (node.kind == "in") {
                node.analyzer->feed(st.outArr[0], 2, n);
                meterInto(node.outPow, node.outPeak, st.outArr[0], n, alpha);
            } else if (node.kind == "out") {
                node.analyzer->feed(st.inArr[0], 2, n);
                meterInto(node.inPow, node.inPeak, st.inArr[0], n, alpha);
            } else if (node.kind == "analyzer") {
                node.analyzer->feed(st.inArr[0], 2, n);
                for (int c = 0; c < 2; ++c)
                    std::memcpy(st.outArr[0][c], st.inArr[0][c], n * sizeof(float));
                meterInto(node.inPow, node.inPeak, st.inArr[0], n, alpha);
                meterInto(node.outPow, node.outPeak, st.outArr[0], n, alpha);
            } else if (node.kind == "monitor") {
                // Sumidero: copia para la salida de audio del sistema. Lo que
                // no entre en el buffer se descarta (la escucha es best-effort).
                // Sin ningun cable no se junta nada: asi la salida de audio no
                // recibe silencio permanente cuando la escucha esta suelta.
                const bool wired = !st.ins.empty() && st.ins[0].wired;
                const uint32_t room = wired && off < kMonitorCap ? std::min(n, kMonitorCap - off) : 0;
                if (room > 0) {
                    std::memcpy(monL_.data() + off, st.inArr[0][0], room * sizeof(float));
                    std::memcpy(monR_.data() + off, st.inArr[0][1], room * sizeof(float));
                    monFrames_ = off + room;
                }
                meterInto(node.inPow, node.inPeak, st.inArr[0], n, alpha);
            } else if (node.kind == "texto") {
                // Sumidero como la salida: mono (promedio de los dos canales)
                // para el reconocedor. Sin cable no junta nada.
                const bool wired = !st.ins.empty() && st.ins[0].wired;
                const uint32_t room = wired && off < kMonitorCap ? std::min(n, kMonitorCap - off) : 0;
                for (uint32_t i = 0; i < room; ++i)
                    st.txt[off + i] = 0.5f * (st.inArr[0][0][i] + st.inArr[0][1][i]);
                if (room > 0) st.txtFrames = off + room;
                meterInto(node.inPow, node.inPeak, st.inArr[0], n, alpha);
            } else if (node.kind == "entrada" || node.kind == "habla") {
                // Lo que llegó de la otra fuente (silencio si todavía no hay).
                if (!st.ring || !st.ring->pop(st.outArr[0][0], st.outArr[0][1], n)) {
                    std::memset(st.outArr[0][0], 0, n * sizeof(float));
                    std::memset(st.outArr[0][1], 0, n * sizeof(float));
                }
                meterInto(node.outPow, node.outPeak, st.outArr[0], n, alpha);
            } else if (node.kind == "generator") {
                node.gen->render(st.outArr[0], 2, n);
                meterInto(node.outPow, node.outPeak, st.outArr[0], n, alpha);
            } else if (node.kind == "plugin") {
                if (node.bypass.load(std::memory_order_relaxed) || node.bypassAuto.load(std::memory_order_relaxed)) {
                    // Anulado (a mano o por el vigía): NO se llama al módulo —un
                    // módulo trabado o lento no puede tocar el aire— y sale la
                    // entrada tal cual.
                    for (size_t p = 0; p < st.outArr.size(); ++p)
                        for (int c = 0; c < 2; ++c) {
                            if (p == 0 && !st.inArr.empty())
                                std::memcpy(st.outArr[p][c], st.inArr[0][c], n * sizeof(float));
                            else
                                std::memset(st.outArr[p][c], 0, n * sizeof(float));
                        }
                } else {
                    const std::int64_t t0 = ahoraNsRack();
                    enProcesoDesde_.store(t0, std::memory_order_relaxed);
                    node.clap->process(st.inArr.data(), st.outArr.data(), n);
                    enProcesoDesde_.store(0, std::memory_order_relaxed);
                    const float ms = static_cast<float>(ahoraNsRack() - t0) / 1e6f;
                    node.msUltimo.store(ms, std::memory_order_relaxed);
                    if (ms > node.msMax.load(std::memory_order_relaxed)) node.msMax.store(ms, std::memory_order_relaxed);
                    node.msMedio.store(node.msMedio.load(std::memory_order_relaxed) * 0.98f + ms * 0.02f,
                                       std::memory_order_relaxed);
                    const double bloqueMs = n * 1000.0 / sr_;
                    if (ms > bloqueMs * g_umbralPct.load(std::memory_order_relaxed) / 100.0) {
                        if (++node.lentos >= g_bloques.load(std::memory_order_relaxed)) {
                            node.bypassAuto.store(true, std::memory_order_relaxed);
                            node.lentos = 0;
                            std::unique_lock<std::mutex> ls(saltadosMu_, std::try_to_lock);
                            if (ls.owns_lock()) saltados_.push_back(node.label.empty() ? node.id : node.label);
                        }
                    } else {
                        node.lentos = 0;
                    }
                    // Salida mono de un modulo: R = L.
                    if (node.channels(0, false) == 1)
                        std::memcpy(st.outArr[0][1], st.outArr[0][0], n * sizeof(float));
                }
                if (!st.inArr.empty()) meterInto(node.inPow, node.inPeak, st.inArr[0], n, alpha);
                meterInto(node.outPow, node.outPeak, st.outArr[0], n, alpha);
            }
        }
        const Plan::Buf& out = *P.bufs[static_cast<size_t>(P.outRead)];
        std::memcpy(planes[0] + off, out.l.data(), n * sizeof(float));
        if (channels > 1) std::memcpy(planes[1] + off, out.r.data(), n * sizeof(float));
    }
}

// --- Descripcion / persistencia -----------------------------------------------------------

json AudioRack::meters(const std::string& id) const {
    const Node* n = find(id);
    if (n == nullptr) return nullptr;
    auto r1 = [](double v) { return std::round(v * 10) / 10; };
    json m;
    const float ip = n->inPow.load(), op = n->outPow.load();
    if (n->inputs() > 0) {
        m["entradaDb"] = r1(powDb(ip));
        m["picoEntradaDb"] = r1(powDb(n->inPeak.load() * n->inPeak.load()));
    }
    if (n->outputs() > 0) {
        m["salidaDb"] = r1(powDb(op));
        m["picoSalidaDb"] = r1(powDb(n->outPeak.load() * n->outPeak.load()));
    }
    if (n->inputs() > 0 && n->outputs() > 0 && ip > 1e-9f)
        m["cambioDb"] = r1(powDb(op) - powDb(ip)); // negativo = reduccion
    return m;
}

json AudioRack::describe() const {
    json nodes = json::array();
    for (const auto& n : nodes_) {
        json j{{"id", n->id}, {"tipo", n->kind}, {"etiqueta", n->label}, {"x", n->x}, {"y", n->y}};
        // Puertos: el id con el que se cablean, mas el nombre real del modulo
        // y si son mono o estereo (la interfaz lo muestra en modo completo).
        json ins = json::array(), outs = json::array(), insInfo = json::array(), outsInfo = json::array();
        const auto info = [&](int p, bool input) {
            json d{{"puerto", portName(*n, p, input)}, {"canales", n->channels(p, input)}};
            if (n->kind == "plugin") {
                const auto& ps = input ? n->clap->inputs() : n->clap->outputs();
                if (p < static_cast<int>(ps.size())) d["nombre"] = ps[static_cast<size_t>(p)].name;
            }
            return d;
        };
        for (int p = 0; p < n->inputs(); ++p) {
            ins.push_back(portName(*n, p, true));
            insInfo.push_back(info(p, true));
        }
        for (int p = 0; p < n->outputs(); ++p) {
            outs.push_back(portName(*n, p, false));
            outsInfo.push_back(info(p, false));
        }
        j["entradas"] = ins;
        j["salidas"] = outs;
        j["puertosEntrada"] = insInfo;
        j["puertosSalida"] = outsInfo;
        if (n->kind == "plugin") {
            j["modulo"] = n->pluginId;
            j["nombre"] = n->clap->desc().name;
            j["bypass"] = n->bypass.load();
            j["bypass_auto"] = n->bypassAuto.load();
            j["ms_ultimo"] = std::round(n->msUltimo.load() * 100) / 100;
            j["ms_medio"] = std::round(n->msMedio.load() * 100) / 100;
            j["ms_max"] = std::round(n->msMax.load() * 100) / 100;
            j["ms_bloque"] = std::round(kMaxFrames * 1000.0 / sr_ * 100) / 100;
            j["editorAbierto"] = n->clap->editorOpen();
            j["latencia_muestras"] = n->latencia;
        }
        if (n->kind == "generator") {
            const auto c = n->gen->config();
            j["senal"] = {
                {"tipo", Generator::kindName(c.kind)}, {"frecuencia", c.freq}, {"nivelDb", c.levelDb}};
        }
        if (n->kind == "monitor") {
            j["dispositivo"] = n->device;
            j["grabar"] = n->grabar;
        }
        if (n->kind == "texto") {
            j["modo"] = n->modo;
            j["flujo"] = n->flujo;
            j["salidas"] = n->salidas;
        }
        if (n->kind == "entrada") j["fuente"] = n->fuente;
        if (n->kind == "habla") j["habla"] = n->habla;
        if (n->kind == "traducir") j["traduccion"] = n->traduccion;
        j["medidores"] = meters(n->id);
        nodes.push_back(std::move(j));
    }
    json conns = json::array();
    for (const auto& c : conns_) {
        const Node* a = find(c.from);
        const Node* b = find(c.to);
        if (a == nullptr || b == nullptr) continue;
        conns.push_back({{"desde", c.from},
                         {"salida", portName(*a, c.fromPort, false)},
                         {"hacia", c.to},
                         {"entrada", portName(*b, c.toPort, true)},
                         {"canalSalida", c.fromCh},
                         {"canalEntrada", c.toCh}});
    }
    return json{{"nodos", nodes},
                {"cables", conns},
                {"frecuenciaMuestreo", sr_},
                {"colgado", colgado_.load()},
                {"vigia", vigia()},
                {"latencia_muestras", latenciaTotal_.load()},
                {"latencia_ms", std::round(latenciaTotal_.load() * 10000.0 / sr_) / 10}};
}

json AudioRack::save() const {
    json nodes = json::array();
    for (const auto& n : nodes_) {
        json j{{"id", n->id}, {"tipo", n->kind}, {"etiqueta", n->label}, {"x", n->x}, {"y", n->y}};
        if (n->kind == "plugin") {
            j["modulo"] = n->pluginId;
            j["bypass"] = n->bypass.load();
            j["estado"] = b64(n->clap->saveState());
        }
        if (n->kind == "monitor") {
            j["dispositivo"] = n->device;
            j["grabar"] = n->grabar;
        }
        if (n->kind == "texto") {
            j["modo"] = n->modo;
            j["flujo"] = n->flujo;
            j["salidas"] = n->salidas;
        }
        if (n->kind == "entrada") j["fuente"] = n->fuente;
        if (n->kind == "habla") j["habla"] = n->habla;
        if (n->kind == "traducir") j["traduccion"] = n->traduccion;
        if (n->kind == "generator") {
            const auto c = n->gen->config();
            j["senal"] = {{"tipo", Generator::kindName(c.kind)},
                          {"frecuencia", c.freq},
                          {"frecuenciaFin", c.freqEnd},
                          {"segundos", c.seconds},
                          {"nivelDb", c.levelDb}};
        }
        nodes.push_back(std::move(j));
    }
    json conns = json::array();
    for (const auto& c : conns_) {
        const Node* a = find(c.from);
        const Node* b = find(c.to);
        if (a == nullptr || b == nullptr) continue;
        conns.push_back({{"desde", c.from},
                         {"salida", portName(*a, c.fromPort, false)},
                         {"hacia", c.to},
                         {"entrada", portName(*b, c.toPort, true)},
                         {"canalSalida", c.fromCh},
                         {"canalEntrada", c.toCh}});
    }
    return json{{"version", 1}, {"nodos", nodes}, {"cables", conns}};
}

bool AudioRack::load(const json& j, std::string* error) {
    if (!j.is_object() || !j.contains("nodos")) {
        if (error != nullptr) *error = "rack invalido";
        return false;
    }
    // Todo nuevo: se vacia (fuera del audio primero) y se arma de cero.
    std::vector<std::unique_ptr<Node>> old;
    for (auto it = nodes_.begin(); it != nodes_.end();) {
        if ((*it)->kind != "in" && (*it)->kind != "out") {
            old.push_back(std::move(*it));
            it = nodes_.erase(it);
        } else {
            ++it;
        }
    }
    conns_.clear();
    rebuild();
    old.clear();
    std::string firstError;
    for (const auto& nj : j["nodos"]) {
        const std::string kind = nj.value("tipo", "");
        const std::string id = nj.value("id", "");
        if (Node* io = find(id); io != nullptr && (kind == "in" || kind == "out")) {
            io->x = nj.value("x", io->x);
            io->y = nj.value("y", io->y);
            continue;
        }
        auto n = std::make_unique<Node>();
        n->id = id.empty() ? newId(kind) : id;
        n->kind = kind;
        n->label = nj.value("etiqueta", kind);
        n->x = nj.value("x", 0.0);
        n->y = nj.value("y", 0.0);
        if (kind == "plugin") {
            std::string e;
            n->pluginId = nj.value("modulo", "");
            n->clap = ClapInstance::create(lib_, n->pluginId, services_, &e);
            if (!n->clap || !n->clap->activate(sr_, kMaxFrames)) {
                if (firstError.empty()) firstError = n->pluginId + ": " + e;
                continue;
            }
            n->clap->loadState(unb64(nj.value("estado", "")));
            n->bypass = nj.value("bypass", false);
        } else if (kind == "analyzer") {
            n->analyzer = std::make_unique<Analyzer>();
            n->analyzer->setSampleRate(sr_);
        } else if (kind == "texto") {
            n->modo = nj.value("modo", "dictado") == "continuo" ? "continuo" : "dictado";
            n->flujo = nj.value("flujo", ""); // racks viejos: lo pone RackManager
            n->salidas = nj.value("salidas", json::object());
        } else if (kind == "entrada") {
            n->fuente = nj.value("fuente", "");
        } else if (kind == "habla") {
            n->habla = nj.value("habla", json::object());
        } else if (kind == "traducir") {
            n->traduccion = nj.value("traduccion", json::object());
        } else if (kind == "monitor") {
            n->id = "salida-audio"; // una sola por rack
            n->device = nj.value("dispositivo", "");
            n->grabar = nj.value("grabar", false);
            // Racks guardados por versiones anteriores: el nodo se llamaba
            // "Escucha" y no tenia dispositivo.
            if (n->label == "Escucha" || n->label.empty()) n->label = "Salida de audio";
        } else if (kind == "generator") {
            n->gen = std::make_unique<Generator>();
            n->gen->setSampleRate(sr_);
            Generator::Config c;
            const json s = nj.value("senal", json::object());
            Generator::parseKind(s.value("tipo", "silencio"), c.kind);
            c.freq = s.value("frecuencia", 1000.0);
            c.freqEnd = s.value("frecuenciaFin", 20000.0);
            c.seconds = s.value("segundos", 10.0);
            c.levelDb = s.value("nivelDb", -20.0);
            n->gen->configure(c);
        } else {
            continue;
        }
        nodes_.push_back(std::move(n));
    }
    for (const auto& cj : j.value("cables", json::array())) {
        const Node* a = find(cj.value("desde", ""));
        const Node* b = find(cj.value("hacia", ""));
        if (a == nullptr || b == nullptr) continue;
        const int pa = portIndex(*a, cj.value("salida", "out"), false);
        const int pb = portIndex(*b, cj.value("entrada", "in"), true);
        int ca = cj.value("canalSalida", -1);
        int cb = cj.value("canalEntrada", -1);
        if (ca < 0 || cb < 0) ca = cb = -1; // puerto entero
        if (pa >= 0 && pb >= 0 && !wouldCycle(a->id, b->id)) conns_.push_back({a->id, pa, b->id, pb, ca, cb});
    }
    rebuild();
    if (!firstError.empty() && error != nullptr)
        *error = "algunos modulos no se pudieron cargar: " + firstError;
    return true;
}

// --- Medicion fuera de linea -------------------------------------------------------------

std::vector<std::string> AudioRack::mainChain() const {
    // De "in" siguiendo el primer cable de la salida principal hasta "out".
    std::vector<std::string> chain;
    std::string cur = "in";
    std::set<std::string> seen;
    while (cur != "out" && seen.insert(cur).second) {
        std::string next;
        for (const auto& c : conns_)
            if (c.from == cur && c.fromPort == 0 && c.toPort == 0) {
                next = c.to;
                break;
            }
        if (next.empty()) break;
        if (next != "out") chain.push_back(next);
        cur = next;
    }
    return chain;
}

json AudioRack::measure(const std::string& kind, const std::string& node, double levelDb,
                        std::string* error) {
    std::vector<std::string> ids = node.empty() ? mainChain() : std::vector<std::string>{node};
    // Clones de los modulos (mismo estado), sin interfaz ni audio en vivo.
    std::vector<std::unique_ptr<ClapInstance>> clones;
    std::vector<std::string> used;
    for (const auto& id : ids) {
        Node* n = find(id);
        if (n == nullptr) {
            if (error != nullptr) *error = "no existe el nodo " + id;
            return nullptr;
        }
        if (n->kind != "plugin" || n->bypass) continue;
        std::string e;
        auto c = ClapInstance::create(lib_, n->pluginId, nullptr, &e);
        if (!c || !c->activate(sr_, kMaxFrames)) {
            if (error != nullptr) *error = "no se pudo clonar " + id + ": " + e;
            return nullptr;
        }
        c->loadState(n->clap->saveState());
        clones.push_back(std::move(c));
        used.push_back(id);
    }
    // Buffers: 2 puertos x 2 canales por etapa (principal + sidechain en silencio).
    const uint32_t N = kMaxFrames;
    std::vector<std::vector<float>> mem(8, std::vector<float>(N, 0.f));
    float* a[2] = {mem[0].data(), mem[1].data()};
    float* b[2] = {mem[2].data(), mem[3].data()};
    float* sc[2] = {mem[4].data(), mem[5].data()};
    float* sco[2] = {mem[6].data(), mem[7].data()};
    Generator gen;
    gen.setSampleRate(sr_);
    // Corre `seconds` de señal por la cadena; devuelve (RMS entrada, RMS salida)
    // en dBFS AES de los ultimos `tail` segundos.
    auto run = [&](const Generator::Config& cfg, double seconds, double tail) {
        gen.configure(cfg);
        const uint32_t total = static_cast<uint32_t>(seconds * sr_);
        const uint32_t tailFrom = total - static_cast<uint32_t>(tail * sr_);
        double sin = 0, sout = 0;
        uint64_t cnt = 0;
        for (uint32_t done = 0; done < total; done += N) {
            gen.render(a, 2, N);
            double si = 0;
            for (uint32_t i = 0; i < N; ++i)
                si += static_cast<double>(a[0][i]) * a[0][i];
            float** cur = a;
            float** nxt = b;
            for (auto& c : clones) {
                float* const* ins[2] = {cur, sc};
                float* const* outs[2] = {nxt, sco};
                c->process(ins, outs, N);
                std::swap(cur, nxt);
            }
            if (done >= tailFrom) {
                double so = 0;
                for (uint32_t i = 0; i < N; ++i)
                    so += static_cast<double>(cur[0][i]) * cur[0][i];
                sin += si;
                sout += so;
                cnt += N;
            }
        }
        const double pin = cnt > 0 ? 2 * sin / cnt : 0, pout = cnt > 0 ? 2 * sout / cnt : 0;
        return std::make_pair(10 * std::log10(std::max(pin, 1e-14)), 10 * std::log10(std::max(pout, 1e-14)));
    };
    json out{{"modulos", used}, {"medicion", kind}};
    auto r1 = [](double v) { return std::round(v * 10) / 10; };
    if (kind == "respuesta" || kind == "response") {
        json pts = json::array();
        for (float f : Analyzer::bandCenters()) {
            if (f > sr_ * 0.45) break;
            Generator::Config cfg;
            cfg.kind = Generator::Kind::Sine;
            cfg.freq = f;
            cfg.levelDb = levelDb;
            const auto [li, lo] = run(cfg, 0.3, 0.15);
            pts.push_back({{"hz", f}, {"gananciaDb", r1(lo - li)}});
        }
        out["nivelDb"] = levelDb;
        out["puntos"] = pts;
    } else if (kind == "curva" || kind == "curve") {
        json pts = json::array();
        for (int lv = -80; lv <= 0; lv += 2) {
            Generator::Config cfg;
            cfg.kind = Generator::Kind::Sine;
            cfg.freq = 1000;
            cfg.levelDb = lv;
            const auto [li, lo] = run(cfg, 0.5, 0.1);
            pts.push_back({{"entradaDb", lv}, {"salidaDb", r1(lo)}, {"cambioDb", r1(lo - li)}});
        }
        out["puntos"] = pts;
    } else {
        if (error != nullptr) *error = "medicion: 'respuesta' o 'curva'";
        return nullptr;
    }
    return out;
}

} // namespace ba::app::audio
