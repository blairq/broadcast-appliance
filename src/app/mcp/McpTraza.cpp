// ============================================================================
// src/app/mcp/McpTraza.cpp — ver McpTraza.hpp
// ============================================================================
#include "mcp/McpTraza.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <mutex>

namespace ba::app::mcp {
namespace {

using nlohmann::json;

/// Claves cuyo VALOR no se escribe. Se compara en minusculas y por contenido,
/// no por igualdad: `apiKey`, `access_token` y `Authorization` tienen que caer
/// todas, y no vale la pena una lista de cada nombre posible.
const char* const kSecretas[] = {"token", "pin",    "clave",         "password", "secret",
                                 "key",   "cookie", "authorization", "bearer"};

bool esSecreta(std::string k) {
    std::transform(k.begin(), k.end(), k.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    for (const char* s : kSecretas)
        if (k.find(s) != std::string::npos) return true;
    return false;
}

/// A partir de aca un valor puede ser un secreto. Por debajo es, casi seguro,
/// un identificador: las claves de API y los tokens no miden ocho letras.
constexpr std::size_t kSecretoMinimo = 16;

/// Si el VALOR parece un secreto: una tira larga y sin espacios.
bool pareceSecreto(const json& v) {
    if (v.is_string()) {
        const std::string s = v.get<std::string>();
        return s.size() >= kSecretoMinimo && s.find(' ') == std::string::npos;
    }
    // Lo que no es texto (un objeto, un numero) no es una credencial, pero un
    // objeto puede tener una adentro: se lo deja pasar y se poda por dentro.
    return !v.is_object() && !v.is_array();
}

constexpr std::size_t kTextoLargo = 180; ///< un texto mas largo se recorta
constexpr std::size_t kListaLarga = 8;   ///< de una lista, los primeros

/// Recorta a `tope` BYTES sin partir un caracter UTF-8 al medio.
///
/// POR QUE NO ALCANZA CON substr. Cortar en el byte 180 puede caer en medio de
/// una «—» o una «ñ», y entonces la cadena deja de ser UTF-8 valido. nlohmann
/// LANZA al serializarla (type_error.316), y esa excepcion salia por la
/// respuesta del servidor: una llamada MCP perfectamente valida contestaba
/// «error interno» porque el REGISTRO no podia escribirse. El registro nunca
/// puede romper lo que registra.
std::string recortar(const std::string& s, std::size_t tope) {
    if (s.size() <= tope) return s;
    std::size_t n = tope;
    // Retroceder mientras se este parado sobre un byte de continuacion (10xxxxxx).
    while (n > 0 && (static_cast<unsigned char>(s[n]) & 0xC0) == 0x80)
        --n;
    return s.substr(0, n) + "…(+" + std::to_string(s.size() - n) + ")";
}

/// Copia el JSON dejandolo legible: sin secretos, sin base64 de medio mega y
/// sin los 128 numeros de una cara.
json podar(const json& j) {
    if (j.is_object()) {
        json out = json::object();
        for (auto it = j.begin(); it != j.end(); ++it) {
            // Y ADEMAS QUE PAREZCA UN SECRETO. En castellano «clave» es tambien
            // la clave de un catalogo: la traza del rack de audio salia
            // ilegible, con cada parametro —«gate», «umbral», «ataque»— tapado
            // como si fuera una contraseña. Un secreto es largo y de una sola
            // pieza; un identificador de catalogo, no.
            if (esSecreta(it.key()) && pareceSecreto(it.value())) {
                out[it.key()] = "«tapado»";
                continue;
            }
            out[it.key()] = podar(it.value());
        }
        return out;
    }
    if (j.is_array()) {
        json out = json::array();
        const std::size_t n = j.size();
        for (std::size_t i = 0; i < std::min(n, kListaLarga); ++i)
            out.push_back(podar(j[i]));
        if (n > kListaLarga) out.push_back("…(+" + std::to_string(n - kListaLarga) + ")");
        return out;
    }
    if (j.is_string()) {
        const std::string s = j.get<std::string>();
        return recortar(s, kTextoLargo);
    }
    return j;
}

std::string ahora() {
    using namespace std::chrono;
    const auto t = system_clock::now();
    const auto ms = duration_cast<milliseconds>(t.time_since_epoch()).count() % 1000;
    const std::time_t s = system_clock::to_time_t(t);
    std::tm tm{};
#if defined(_WIN32)
    gmtime_s(&tm, &s);
#else
    gmtime_r(&s, &tm);
#endif
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%04d-%02d-%02dT%02d:%02d:%02d.%03dZ", tm.tm_year + 1900, tm.tm_mon + 1,
                  tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec, static_cast<int>(ms));
    return buf;
}

} // namespace

struct Traza::Impl {
    std::mutex mutex;
    std::filesystem::path archivo;
    std::ofstream salida;
    std::size_t escritos = 0;
    /// 8 MB y una sola vuelta. Es la traza de UNA sesion de trabajo: guardar
    /// mas es guardar el mes pasado, que no lo mira nadie.
    static constexpr std::size_t kMaxBytes = 8u * 1024u * 1024u;
};

Traza& Traza::la() {
    static Traza t;
    return t;
}

void Traza::abrir(const std::filesystem::path& dir) {
    if (d_ == nullptr) d_ = new Impl();
    std::lock_guard<std::mutex> g(d_->mutex);
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    d_->archivo = dir / "mcp.log";
    d_->salida.open(d_->archivo, std::ios::app);
    d_->escritos = std::filesystem::exists(d_->archivo, ec)
                       ? static_cast<std::size_t>(std::filesystem::file_size(d_->archivo, ec))
                       : 0;
    if (d_->salida) d_->salida << "\n==== " << ahora() << " | traza MCP abierta ====\n" << std::flush;
}

bool Traza::abierta() const { return d_ != nullptr && d_->salida.is_open(); }

void Traza::anotar(const std::string& texto) {
    if (d_ == nullptr) return;
    std::lock_guard<std::mutex> g(d_->mutex);
    if (!d_->salida.is_open()) return;
    if (d_->escritos > Impl::kMaxBytes) {
        d_->salida.close();
        std::error_code ec;
        std::filesystem::rename(d_->archivo, d_->archivo.parent_path() / "mcp.1.log", ec);
        d_->salida.open(d_->archivo, std::ios::trunc);
        d_->escritos = 0;
    }
    const std::string linea = ahora() + " " + texto + "\n";
    d_->salida << linea << std::flush;
    d_->escritos += linea.size();
}

json Traza::limpio(const json& j) { return podar(j); }

std::string Traza::resumir(const json& j, std::size_t tope) {
    if (j.is_null()) return "{}";
    // NO SE LE ESCAPA NADA A QUIEN LLAMA. Esto lo usa el camino de respuesta
    // del servidor: si algo acá lanza, una llamada buena contesta un error que
    // no tiene nada que ver. Ante la duda, se registra peor pero se registra.
    try {
        return recortar(podar(j).dump(-1, ' ', false, nlohmann::json::error_handler_t::replace), tope);
    } catch (const std::exception& e) {
        return std::string("(no se pudo resumir: ") + e.what() + ")";
    }
}

} // namespace ba::app::mcp
