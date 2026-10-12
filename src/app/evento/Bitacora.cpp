// ============================================================================
// src/app/evento/Bitacora.cpp — ver Bitacora.hpp
// ============================================================================
#include "evento/Bitacora.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <fstream>

#if defined(_WIN32)
#include <io.h>
#else
#include <unistd.h>
#endif

namespace ba::evento {

namespace fs = std::filesystem;

namespace {

std::int64_t ahoraMs() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

std::string minusculas(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    // Y las mayúsculas latinas con tilde (Á É Í Ó Ú Ñ Ü…: 0xC3 0x80–0x9E en UTF-8),
    // que tolower no toca: «VOTACIÓN» tiene que encontrar «votación».
    for (std::size_t i = 0; i + 1 < s.size(); ++i) {
        const auto b = static_cast<unsigned char>(s[i + 1]);
        if (static_cast<unsigned char>(s[i]) == 0xC3 && b >= 0x80 && b <= 0x9E && b != 0x97) s[i + 1] = static_cast<char>(b + 0x20);
    }
    return s;
}

/// Escribe las líneas y las baja al disco de verdad (fsync), no solo al caché
/// del sistema: es lo que hace que un corte de luz no se lleve el último segundo.
bool escribir(const fs::path& archivo, const std::vector<std::string>& lineas) {
    std::error_code ec;
    fs::create_directories(archivo.parent_path(), ec);
#if defined(_WIN32)
    FILE* f = _wfopen(archivo.wstring().c_str(), L"ab");
#else
    FILE* f = std::fopen(archivo.string().c_str(), "ab");
#endif
    if (f == nullptr) return false;
    for (const std::string& l : lineas) {
        std::fwrite(l.data(), 1, l.size(), f);
        std::fputc('\n', f);
    }
    std::fflush(f);
#if defined(_WIN32)
    _commit(_fileno(f));
#else
    ::fsync(fileno(f));
#endif
    std::fclose(f);
    return true;
}

} // namespace

const std::vector<std::string>& Bitacora::categorias() {
    static const std::vector<std::string> c{"bus", "mcp", "botonera", "guion", "aviso", "evento", "config"};
    return c;
}

Bitacora::Bitacora() : th_([this] { hilo(); }) {}

Bitacora::~Bitacora() {
    {
        std::lock_guard<std::mutex> g(mu_);
        fin_ = true;
    }
    cv_.notify_all();
    if (th_.joinable()) th_.join();
}

// El cambio de archivo viaja EN LA COLA, como una marca: lo anotado antes
// queda en el evento anterior y lo de después en el nuevo, sin carreras.
static const char kMarca = '\x01';

void Bitacora::setArchivo(const fs::path& archivo) {
    {
        std::lock_guard<std::mutex> g(mu_);
        archivoPedido_ = archivo;
        cola_.push_back(std::string(1, kMarca));
        archivosCola_.push_back(archivo);
    }
    cv_.notify_all();
}

fs::path Bitacora::archivo() const {
    std::lock_guard<std::mutex> g(mu_);
    return archivoPedido_;
}

void Bitacora::setLimpiador(std::function<json(const json&)> fn) {
    std::lock_guard<std::mutex> g(mu_);
    limpiar_ = std::move(fn);
}

void Bitacora::setRegistrar(const std::string& tipo, bool si) {
    std::lock_guard<std::mutex> g(mu_);
    if (si)
        apagadas_.erase(tipo);
    else
        apagadas_.insert(tipo);
}

bool Bitacora::registra(const std::string& tipo) const {
    std::lock_guard<std::mutex> g(mu_);
    return apagadas_.count(tipo) == 0;
}

void Bitacora::anotar(const std::string& tipo, const std::string& origen, const std::string& texto, const json& datos,
                      std::int64_t t) {
    std::function<json(const json&)> limpiar;
    {
        std::lock_guard<std::mutex> g(mu_);
        if (apagadas_.count(tipo) != 0) return;
        if (archivoPedido_.empty()) return;
        limpiar = limpiar_;
    }
    json l{{"t", t != 0 ? t : ahoraMs()}, {"tipo", tipo}};
    if (!origen.empty()) l["origen"] = origen;
    if (!texto.empty()) l["texto"] = texto;
    json d = datos;
    if (limpiar && !d.is_null()) {
        try {
            d = limpiar(d);
        } catch (...) {
            d = json::object();
        }
    }
    if (d.is_object()) {
        for (auto it = d.begin(); it != d.end(); ++it)
            if (!l.contains(it.key())) l[it.key()] = it.value();
    } else if (!d.is_null()) {
        l["valor"] = d;
    }
    std::string linea;
    try {
        linea = l.dump(-1, ' ', false, json::error_handler_t::replace);
    } catch (...) {
        return;
    }
    {
        std::lock_guard<std::mutex> g(mu_);
        cola_.push_back(std::move(linea));
        // Si el disco no responde, no se come la memoria: se pierde lo más viejo.
        // (Nunca una marca de cambio de archivo: van de a pares con archivosCola_.)
        if (cola_.size() > 20000) {
            auto it = std::find_if(cola_.begin(), cola_.end(), [](const std::string& x) { return x.empty() || x[0] != kMarca; });
            if (it != cola_.end()) cola_.erase(it);
        }
    }
    cv_.notify_all();
}

void Bitacora::vaciar() {
    std::unique_lock<std::mutex> l(mu_);
    cv_.notify_all();
    cvVacio_.wait_for(l, std::chrono::seconds(5), [this] { return cola_.empty() && !escribiendo_; });
}

std::uint64_t Bitacora::escritas() const {
    std::lock_guard<std::mutex> g(mu_);
    return escritas_;
}

void Bitacora::hilo() {
    std::unique_lock<std::mutex> l(mu_);
    while (true) {
        // En tandas: lo que se juntó en un segundo va junto, con un solo fsync
        // por archivo.
        cv_.wait_for(l, std::chrono::seconds(1), [this] { return fin_ || cola_.size() >= 200; });
        std::deque<std::string> tanda;
        tanda.swap(cola_);
        std::deque<fs::path> archivos;
        archivos.swap(archivosCola_);
        escribiendo_ = !tanda.empty();
        fs::path destino = archivo_;
        l.unlock();
        std::uint64_t n = 0;
        std::vector<std::string> trozo;
        const auto bajar = [&] {
            if (!trozo.empty() && !destino.empty() && escribir(destino, trozo)) n += trozo.size();
            trozo.clear();
        };
        for (std::string& x : tanda) {
            if (!x.empty() && x[0] == kMarca) {
                bajar();
                destino = archivos.empty() ? fs::path() : archivos.front();
                if (!archivos.empty()) archivos.pop_front();
            } else {
                trozo.push_back(std::move(x));
            }
        }
        bajar();
        l.lock();
        archivo_ = destino;
        escritas_ += n;
        escribiendo_ = false;
        if (cola_.empty()) cvVacio_.notify_all();
        if (fin_ && cola_.empty()) break;
    }
}

Bitacora::json Bitacora::leer(const fs::path& archivo, std::int64_t desdeMs, std::int64_t hastaMs,
                    const std::set<std::string>& tipos, const std::string& texto, std::size_t limite) {
    json out = json::array();
    std::ifstream f(archivo, std::ios::binary);
    if (!f) return out;
    const std::string buscado = minusculas(texto);
    std::deque<json> ultimas;
    std::string linea;
    while (std::getline(f, linea)) {
        if (linea.empty()) continue;
        if (!buscado.empty() && minusculas(linea).find(buscado) == std::string::npos) continue;
        json j = json::parse(linea, nullptr, false);
        if (j.is_discarded() || !j.is_object()) continue;
        const std::int64_t t = j.value("t", std::int64_t{0});
        if (desdeMs != 0 && t < desdeMs) continue;
        if (hastaMs != 0 && t > hastaMs) continue;
        if (!tipos.empty() && tipos.count(j.value("tipo", std::string())) == 0) continue;
        ultimas.push_back(std::move(j));
        if (limite != 0 && ultimas.size() > limite) ultimas.pop_front();
    }
    for (auto& j : ultimas) out.push_back(std::move(j));
    return out;
}

} // namespace ba::evento
