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

} // namespace ba::gateway
