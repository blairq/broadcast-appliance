// ============================================================================
// src/core/diag/DiarioDeArranque.cpp — ver DiarioDeArranque.hpp
// ============================================================================
#include "diag/DiarioDeArranque.hpp"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <map>
#include <mutex>
#include <set>
#include <sstream>

#include "diag/Registro.hpp"

#if defined(_WIN32)
#include <io.h>
#else
#include <unistd.h>
#endif

namespace ba::diag {
namespace {

namespace fs = std::filesystem;

/// JSON a mano, a propósito: este archivo lo lee y lo escribe el arranque más
/// temprano del proceso, antes que nada, y no puede depender de una biblioteca
/// que todavía no esté lista ni fallar por una excepción.
std::string entreComillas(const std::string& s) {
    std::string o = "\"";
    for (const char c : s) {
        if (c == '"' || c == '\\') o += '\\';
        if (static_cast<unsigned char>(c) < 0x20) continue;
        o += c;
    }
    return o + "\"";
}

/// Busca `"clave"` y devuelve el texto que sigue entre comillas.
std::string texto(const std::string& j, const std::string& clave) {
    const auto k = j.find(entreComillas(clave));
    if (k == std::string::npos) return {};
    const auto a = j.find('"', j.find(':', k) + 1);
    if (a == std::string::npos) return {};
    const auto b = j.find('"', a + 1);
    if (b == std::string::npos) return {};
    return j.substr(a + 1, b - a - 1);
}

int numero(const std::string& j, const std::string& clave) {
    const auto k = j.find(entreComillas(clave));
    if (k == std::string::npos) return 0;
    const auto c = j.find(':', k);
    if (c == std::string::npos) return 0;
    return std::atoi(j.c_str() + c + 1);
}

/// El objeto `{"a":1,"b":2}` que sigue a una clave.
std::map<std::string, int> objetoDeNumeros(const std::string& j, const std::string& clave) {
    std::map<std::string, int> out;
    const auto k = j.find(entreComillas(clave));
    if (k == std::string::npos) return out;
    const auto a = j.find('{', k);
    const auto b = j.find('}', a == std::string::npos ? k : a);
    if (a == std::string::npos || b == std::string::npos) return out;
    std::string cuerpo = j.substr(a + 1, b - a - 1);
    std::size_t i = 0;
    while (true) {
        const auto c1 = cuerpo.find('"', i);
        if (c1 == std::string::npos) break;
        const auto c2 = cuerpo.find('"', c1 + 1);
        if (c2 == std::string::npos) break;
        const auto dp = cuerpo.find(':', c2);
        if (dp == std::string::npos) break;
        out[cuerpo.substr(c1 + 1, c2 - c1 - 1)] = std::atoi(cuerpo.c_str() + dp + 1);
        i = dp + 1;
    }
    return out;
}

std::set<std::string> listaDeTextos(const std::string& j, const std::string& clave) {
    std::set<std::string> out;
    const auto k = j.find(entreComillas(clave));
    if (k == std::string::npos) return out;
    const auto a = j.find('[', k);
    const auto b = j.find(']', a == std::string::npos ? k : a);
    if (a == std::string::npos || b == std::string::npos) return out;
    const std::string cuerpo = j.substr(a + 1, b - a - 1);
    std::size_t i = 0;
    while (true) {
        const auto c1 = cuerpo.find('"', i);
        if (c1 == std::string::npos) break;
        const auto c2 = cuerpo.find('"', c1 + 1);
        if (c2 == std::string::npos) break;
        out.insert(cuerpo.substr(c1 + 1, c2 - c1 - 1));
        i = c2 + 1;
    }
    return out;
}

} // namespace

struct DiarioDeArranque::Impl {
    mutable std::mutex m;
    fs::path archivo;
    int arranque = 1;
    std::string cargando;  ///< lo que se está cargando AHORA
    std::string culpable;  ///< lo que quedó a medias la vez anterior
    std::map<std::string, int> roturas;
    std::set<std::string> apagados;
    bool modoSeguro = false; ///< transitorio: NO se escribe

    /// Escribe y **sincroniza**. El fsync no es una ceremonia: sin él, lo que
    /// se anotó justo antes de un cuelgue duro se queda en el buffer del
    /// sistema y se pierde exactamente en el caso para el que existe esto.
    void guardar() const {
        std::error_code ec;
        fs::create_directories(archivo.parent_path(), ec);
        std::ostringstream o;
        o << "{\n  \"arranque\": " << arranque;
        if (!cargando.empty()) o << ",\n  \"cargando\": " << entreComillas(cargando);
        o << ",\n  \"roturas\": {";
        bool primero = true;
        for (const auto& [k, v] : roturas) {
            o << (primero ? "" : ",") << entreComillas(k) << ":" << v;
            primero = false;
        }
        o << "},\n  \"apagados\": [";
        primero = true;
        for (const auto& a : apagados) {
            o << (primero ? "" : ",") << entreComillas(a);
            primero = false;
        }
        o << "]\n}\n";
        const std::string s = o.str();

        // Por la ruta ANCHA en Windows: `archivo.string()` la pasa a la
        // pagina de codigos del sistema y una carpeta con acentos
        // (C:\Users\Jose...) deja de abrirse. `path::c_str()` ya es wchar_t
        // ahi y char en el resto.
#if defined(_WIN32)
        std::FILE* f = _wfopen(archivo.c_str(), L"wb");
#else
        std::FILE* f = std::fopen(archivo.c_str(), "wb");
#endif
        if (f == nullptr) return;
        std::fwrite(s.data(), 1, s.size(), f);
        std::fflush(f);
#if defined(_WIN32)
        _commit(_fileno(f));
#else
        fsync(fileno(f));
#endif
        std::fclose(f);
    }
};

DiarioDeArranque::DiarioDeArranque(fs::path archivo) : d_(std::make_unique<Impl>()) {
    d_->archivo = std::move(archivo);

    std::string previo;
    {
        std::ifstream f(d_->archivo, std::ios::binary);
        if (f) previo.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    }
    if (!previo.empty()) {
        d_->arranque = numero(previo, "arranque") + 1;
        d_->roturas = objetoDeNumeros(previo, "roturas");
        d_->apagados = listaDeTextos(previo, "apagados");
        // `cargando` presente = el arranque anterior no llegó al final. Eso es
        // el hallazgo entero de este archivo.
        d_->culpable = texto(previo, "cargando");
        if (!d_->culpable.empty()) {
            const int n = ++d_->roturas[d_->culpable];
            if (n >= kRoturasParaApagar) d_->apagados.insert(d_->culpable);
            registro().linea(Nivel::Aviso, "arranque", 0,
                             "«" + d_->culpable + "» se llevó puesto el arranque anterior (van " +
                                 std::to_string(n) + ")" +
                                 (n >= kRoturasParaApagar ? "; queda apagado" : ""));
        }
    }
    d_->cargando.clear();
    d_->guardar();
}

DiarioDeArranque::~DiarioDeArranque() = default;

const std::string& DiarioDeArranque::culpable() const { return d_->culpable; }
int DiarioDeArranque::arranque() const { return d_->arranque; }

bool DiarioDeArranque::apagado(const std::string& id) const {
    std::lock_guard<std::mutex> g(d_->m);
    return d_->modoSeguro || d_->apagados.count(id) > 0;
}

void DiarioDeArranque::setModoSeguro(bool si) {
    std::lock_guard<std::mutex> g(d_->m);
    d_->modoSeguro = si;
}

bool DiarioDeArranque::modoSeguro() const {
    std::lock_guard<std::mutex> g(d_->m);
    return d_->modoSeguro;
}

int DiarioDeArranque::roturas(const std::string& id) const {
    std::lock_guard<std::mutex> g(d_->m);
    const auto it = d_->roturas.find(id);
    return it == d_->roturas.end() ? 0 : it->second;
}

void DiarioDeArranque::empieza(const std::string& id) {
    std::lock_guard<std::mutex> g(d_->m);
    d_->cargando = id;
    d_->guardar();
}

void DiarioDeArranque::termina(const std::string& id) {
    std::lock_guard<std::mutex> g(d_->m);
    if (d_->cargando != id) return; // ya lo cerró otro, o nunca se abrió
    d_->cargando.clear();
    // Cargó bien: se le perdonan las roturas anteriores. Si no, algo que
    // rompió una vez hace meses quedaría con el antecedente para siempre.
    d_->roturas.erase(id);
    d_->guardar();
}

void DiarioDeArranque::arranqueOk() {
    std::lock_guard<std::mutex> g(d_->m);
    d_->cargando.clear();
    d_->guardar();
}

void DiarioDeArranque::setApagado(const std::string& id, bool apagado) {
    std::lock_guard<std::mutex> g(d_->m);
    if (apagado) {
        d_->apagados.insert(id);
    } else {
        d_->apagados.erase(id);
        d_->roturas.erase(id); // prenderlo a mano le borra los antecedentes
    }
    d_->guardar();
}

std::vector<std::string> DiarioDeArranque::apagados() const {
    std::lock_guard<std::mutex> g(d_->m);
    return {d_->apagados.begin(), d_->apagados.end()};
}

// --- El del proceso ---------------------------------------------------------

namespace {
std::unique_ptr<DiarioDeArranque>& elDiario() {
    static std::unique_ptr<DiarioDeArranque> d;
    return d;
}
} // namespace

void abrirDiario(const fs::path& archivo) { elDiario() = std::make_unique<DiarioDeArranque>(archivo); }

DiarioDeArranque& diario() {
    if (!elDiario()) {
        // Sin abrir: uno en memoria que no molesta a nadie. Es preferible a
        // explotar en un camino de arranque.
        elDiario() = std::make_unique<DiarioDeArranque>(fs::path());
    }
    return *elDiario();
}

Cargando::Cargando(std::string id) : id_(std::move(id)) {
    saltear_ = diario().apagado(id_);
    if (saltear_) {
        registro().linea(Nivel::Aviso, "arranque", 0,
                         "no se carga «" + id_ + "»: rompió el arranque " +
                             std::to_string(diario().roturas(id_)) +
                             " veces. Se prende de nuevo desde el visor del registro.");
        return;
    }
    diario().empieza(id_);
}

Cargando::~Cargando() {
    if (!saltear_) diario().termina(id_);
}

} // namespace ba::diag
