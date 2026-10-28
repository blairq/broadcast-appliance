// ============================================================================
// src/gateway/Prompter.cpp — ver Prompter.hpp
// ============================================================================
#include "Prompter.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <vector>

namespace ba::gateway {
namespace {

using nlohmann::json;

/// La IP del cliente, en texto. Es para que el operador reconozca cuál de los
/// aparatos de la sala es, así que alcanza con el punto decimal.
std::string ipTexto(std::uint32_t redes) {
    const auto b = reinterpret_cast<const unsigned char*>(&redes);
    return std::to_string(b[0]) + "." + std::to_string(b[1]) + "." + std::to_string(b[2]) + "." +
           std::to_string(b[3]);
}

int64_t ahoraMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

/// Separa en palabras contando cualquier racha de espacios como separador.
/// No intenta ser un tokenizador: lo unico que importa es que la cuenta sea la
/// MISMA de los dos lados (servidor y navegador), asi que las dos hacen esto.
bool esEspacio(unsigned char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
}

} // namespace

int Prompter::contarPalabras(const std::string& texto) {
    int n = 0;
    bool dentro = false;
    for (const char ch : texto) {
        if (esEspacio(static_cast<unsigned char>(ch))) {
            dentro = false;
        } else if (!dentro) {
            dentro = true;
            ++n;
        }
    }
    return n;
}

void Prompter::cargar(const std::string& titulo, const std::string& texto) {
    std::lock_guard<std::mutex> lock(m_);
    titulo_ = titulo;
    texto_ = texto;
    palabras_ = contarPalabras(texto);
    posicion_ = 0;
    corriendo_ = false; // un texto nuevo NUNCA arranca solo
    desdeMs_ = ahoraMs();
    aviso_.clear();

    // Los parrafos, para poder saltar: un parrafo empieza despues de un
    // renglon vacio (o al principio del texto).
    parrafos_.clear();
    int palabra = 0;
    bool dentro = false;
    bool arranca = true;
    int saltos = 0;
    for (const char ch : texto) {
        if (ch == '\n') {
            ++saltos;
            dentro = false;
            if (saltos >= 2) arranca = true;
            continue;
        }
        if (esEspacio(static_cast<unsigned char>(ch))) {
            dentro = false;
            continue;
        }
        if (!dentro) {
            if (arranca) {
                parrafos_.push_back(palabra);
                arranca = false;
            }
            dentro = true;
            ++palabra;
        }
        saltos = 0;
    }
    ++version_;
}

void Prompter::fijarPosicion(int64_t ahora) {
    if (corriendo_ && palabras_ > 0) {
        const double transcurrido = static_cast<double>(ahora - desdeMs_) / 60000.0;
        posicion_ = std::min<double>(palabras_, posicion_ + transcurrido * ppm_);
    }
    desdeMs_ = ahora;
}

bool Prompter::comandar(const json& cmd) {
    if (!cmd.is_object()) return false;
    std::lock_guard<std::mutex> lock(m_);
    const int64_t ahora = ahoraMs();
    bool algo = false;

    // Siempre se congela la posicion primero: cambiar la velocidad (o parar)
    // sin hacerlo haria saltar el texto hacia atras o hacia adelante.
    fijarPosicion(ahora);

    if (cmd.contains("parrafo") && cmd["parrafo"].is_number_integer()) {
        const int i = cmd["parrafo"].get<int>();
        if (i >= 0 && i < static_cast<int>(parrafos_.size())) {
            posicion_ = parrafos_[static_cast<std::size_t>(i)];
            algo = true;
        }
    }
    if (cmd.contains("posicion") && cmd["posicion"].is_number()) {
        posicion_ = std::clamp(cmd["posicion"].get<double>(), 0.0, static_cast<double>(palabras_));
        algo = true;
    }
    if (cmd.contains("ppm") && cmd["ppm"].is_number_integer()) {
        // 40 es una lectura muy pausada; 300 ya no se puede seguir con los
        // ojos. Fuera de eso es un error de quien lo manda, no una preferencia.
        ppm_ = std::clamp(cmd["ppm"].get<int>(), 40, 300);
        algo = true;
    }
    if (cmd.contains("corriendo") && cmd["corriendo"].is_boolean()) {
        corriendo_ = cmd["corriendo"].get<bool>();
        algo = true;
    }
    if (cmd.contains("tamano") && cmd["tamano"].is_number_integer()) {
        tamano_ = std::clamp(cmd["tamano"].get<int>(), 1, 10);
        algo = true;
    }
    if (cmd.contains("espejo") && cmd["espejo"].is_boolean()) {
        espejo_ = cmd["espejo"].get<bool>();
        algo = true;
    }
    if (cmd.contains("aviso") && cmd["aviso"].is_string()) {
        aviso_ = cmd["aviso"].get<std::string>();
        if (aviso_.size() > 120) aviso_.resize(120);
        avisoMs_ = ahora;
        algo = true;
    }
    if (algo) ++version_;
    return algo;
}

void Prompter::pararEnSeco() {
    std::lock_guard<std::mutex> lock(m_);
    fijarPosicion(ahoraMs());
    corriendo_ = false;
    ++version_;
}

int Prompter::duracionSeg() const {
    std::lock_guard<std::mutex> lock(m_);
    if (palabras_ <= 0 || ppm_ <= 0) return 0;
    return static_cast<int>(static_cast<double>(palabras_) * 60.0 / ppm_);
}

json Prompter::estadoLocked() const {
    return json{{"titulo", titulo_},
                {"texto", texto_},
                {"palabras", palabras_},
                {"parrafos", parrafos_},
                {"posicion", posicion_},
                {"ppm", ppm_},
                {"corriendo", corriendo_},
                // `desdeMs` y `ahoraMs` juntos: la vista calcula su propia
                // posicion aunque su reloj este corrido respecto del servidor.
                {"desdeMs", desdeMs_},
                {"ahoraMs", ahoraMs()},
                {"tamano", tamano_},
                {"espejo", espejo_},
                {"aviso", aviso_},
                {"avisoMs", avisoMs_},
                {"version", version_},
                {"duracionSeg", palabras_ > 0 && ppm_ > 0
                                    ? static_cast<int>(static_cast<double>(palabras_) * 60.0 / ppm_)
                                    : 0}};
}

json Prompter::estado() const {
    std::lock_guard<std::mutex> lock(m_);
    return estadoLocked();
}

void Prompter::visto(const std::string& id, const std::string& vista, const std::string& nombre,
                     std::uint32_t ip) {
    if (id.empty() || id.size() > 64) return;
    std::lock_guard<std::mutex> lock(m_);
    const int64_t ahora = ahoraMs();
    // Se limpia al anotar (no hay temporizador acá): lo viejo se va solo.
    for (auto it = clientes_.begin(); it != clientes_.end();)
        it = ahora - it->second.vistoMs > kVivoMs() * 15 ? clientes_.erase(it) : std::next(it);
    // Con la tabla llena, uno nuevo reemplaza al que hace más que no aparece.
    if (clientes_.size() >= kMaxClientes && clientes_.find(id) == clientes_.end()) {
        auto viejo = clientes_.begin();
        for (auto it = clientes_.begin(); it != clientes_.end(); ++it)
            if (it->second.vistoMs < viejo->second.vistoMs) viejo = it;
        clientes_.erase(viejo);
    }
    Cliente& c = clientes_[id];
    c.vista = vista == "control" ? "control" : "orador";
    // Un nombre vacío no borra el que ya había: la página lo manda solo
    // cuando alguien lo escribió.
    if (!nombre.empty()) c.nombre = nombre.substr(0, 40);
    c.ip = ip;
    c.vistoMs = ahora;
}

json Prompter::clientes() const {
    std::lock_guard<std::mutex> lock(m_);
    const int64_t ahora = ahoraMs();
    std::vector<json> vivos;
    for (const auto& [id, c] : clientes_) {
        const int64_t hace = ahora - c.vistoMs;
        if (hace > kVivoMs()) continue;
        vivos.push_back(json{
            {"id", id}, {"vista", c.vista}, {"nombre", c.nombre}, {"ip", ipTexto(c.ip)}, {"haceMs", hace}});
    }
    // Los oradores primero: es lo que hay que mirar antes de que alguien suba
    // al atril. Dentro de cada grupo, el que apareció hace menos.
    std::sort(vivos.begin(), vivos.end(), [](const json& a, const json& b) {
        const bool ao = a["vista"] == "orador", bo = b["vista"] == "orador";
        if (ao != bo) return ao;
        return a["haceMs"].get<int64_t>() < b["haceMs"].get<int64_t>();
    });
    return json(vivos);
}

// --- Varios -----------------------------------------------------------------------

std::string Prompters::idValido(const std::string& id) {
    if (id.empty()) return kPrincipal;
    if (id.size() > 32) return {};
    for (const unsigned char c : id)
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-')) return {};
    return id;
}

std::shared_ptr<Prompter> Prompters::de(const std::string& id) {
    const std::string k = idValido(id);
    if (k.empty()) return nullptr;
    std::lock_guard<std::mutex> lock(m_);
    auto it = todos_.find(k);
    if (it != todos_.end()) return it->second;
    if (todos_.size() >= kMax) return nullptr;
    return todos_.emplace(k, std::make_shared<Prompter>()).first->second;
}

json Prompters::lista() const {
    std::lock_guard<std::mutex> lock(m_);
    json out = json::array();
    const auto fila = [](const std::string& id, const Prompter& p) {
        const json e = p.estado(), c = p.clientes();
        int oradores = 0;
        for (const auto& x : c)
            if (x.value("vista", std::string()) == "orador") ++oradores;
        return json{{"id", id},
                    {"titulo", e.value("titulo", std::string())},
                    {"palabras", e.value("palabras", 0)},
                    {"corriendo", e.value("corriendo", false)},
                    {"duracionSeg", p.duracionSeg()},
                    {"oradores", oradores},
                    {"controles", static_cast<int>(c.size()) - oradores}};
    };
    if (auto it = todos_.find(kPrincipal); it != todos_.end()) out.push_back(fila(it->first, *it->second));
    for (const auto& [id, p] : todos_)
        if (id != kPrincipal) out.push_back(fila(id, *p));
    return out;
}

bool Prompters::quitar(const std::string& id) {
    const std::string k = idValido(id);
    if (k.empty() || k == kPrincipal) return false;
    std::lock_guard<std::mutex> lock(m_);
    return todos_.erase(k) > 0;
}

} // namespace ba::gateway
