// ============================================================================
// src/app/evento/EventoStore.cpp — ver EventoStore.hpp
// ============================================================================
#include "evento/EventoStore.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

namespace ba::evento {
namespace {

using nlohmann::json;

constexpr const char* kGuion = "evento.json";

/// Caracteres que Windows no acepta en un nombre de archivo (Linux solo
/// prohibe '/', pero el disco es portable y tiene que abrir en los dos).
bool prohibido(char c) {
    if (static_cast<unsigned char>(c) < 0x20) return true;
    static const std::string kMalos = R"(<>:"/\|?*)";
    return kMalos.find(c) != std::string::npos;
}

std::string ahoraParaNombre() {
    const auto t = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%04d%02d%02d-%02d%02d%02d", tm.tm_year + 1900, tm.tm_mon + 1,
                  tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
    return buf;
}

void ponerErr(std::string* err, const std::string& texto) {
    if (err != nullptr) *err = texto;
}

} // namespace

std::string nombreDeCarpeta(const std::string& nombre) {
    std::string out;
    out.reserve(nombre.size());
    for (const char c : nombre) {
        if (prohibido(c)) continue;
        out.push_back(c == ' ' ? '-' : c);
    }
    // Un nombre que termina en punto o espacio no se puede crear en Windows.
    while (!out.empty() && (out.back() == '.' || out.back() == '-')) out.pop_back();
    while (!out.empty() && out.front() == '-') out.erase(out.begin());
    if (out.size() > 80) out.resize(80);
    return out;
}

EventoStore::EventoStore(fs::path raiz) : raiz_(std::move(raiz)) {
    std::error_code ec;
    fs::create_directories(raiz_, ec);
}

std::vector<Ficha> EventoStore::listar() const {
    std::vector<Ficha> fichas;
    std::error_code ec;
    if (!fs::is_directory(raiz_, ec)) return fichas;
    for (const fs::directory_entry& d : fs::directory_iterator(raiz_, ec)) {
        if (!d.is_directory(ec)) continue;
        const std::string carpeta = d.path().filename().string();
        bool ok = false;
        const Evento e = cargar(carpeta, &ok);
        // Una carpeta sin guion no es un evento: puede ser cualquier cosa que
        // el operador dejo ahi. No se la inventa como evento vacio.
        if (!ok) continue;
        Ficha f;
        f.carpeta = carpeta;
        f.nombre = e.nombre.empty() ? carpeta : e.nombre;
        f.inicio = e.inicio;
        f.duracionSeg = e.duracionSeg();
        f.bloques = e.bloques.size();
        f.cues = e.cantidadDeCues();
        const auto cuando = fs::last_write_time(d.path() / kGuion, ec);
        if (!ec) {
            f.modificadoTic = static_cast<std::int64_t>(cuando.time_since_epoch().count());
            f.tieneFecha = true;
        }
        fichas.push_back(std::move(f));
    }
    std::sort(fichas.begin(), fichas.end(),
              [](const Ficha& a, const Ficha& b) { return a.nombre < b.nombre; });
    return fichas;
}

std::string EventoStore::ultimoTocado() const {
    std::string mejor;
    std::int64_t masReciente = 0;
    bool hay = false;
    for (const Ficha& f : listar()) {
        if (!f.tieneFecha) continue;
        // Ojo con comparar contra cero o contra -1: estos tics son negativos
        // en libstdc++ (ver Ficha::modificadoTic). El primero que aparece
        // marca el piso.
        if (!hay || f.modificadoTic > masReciente) {
            masReciente = f.modificadoTic;
            mejor = f.carpeta;
            hay = true;
        }
    }
    return mejor;
}

std::string EventoStore::crear(const std::string& nombre, std::string* err) {
    const std::string base = nombreDeCarpeta(nombre);
    if (base.empty()) {
        ponerErr(err, "ese nombre no sirve para una carpeta");
        return {};
    }
    std::error_code ec;
    // Si ya hay una con ese nombre, se le agrega un numero: pisar el evento
    // de otro dia por tener el mismo titulo seria imperdonable.
    std::string carpeta = base;
    for (int i = 2; fs::exists(raiz_ / carpeta, ec) && i < 1000; ++i)
        carpeta = base + "-" + std::to_string(i);

    const fs::path dir = raiz_ / carpeta;
    if (!fs::create_directories(dir, ec)) {
        ponerErr(err, "no se pudo crear la carpeta: " + ec.message());
        return {};
    }
    for (const char* sub : {"Medios", "Discursos", "Respaldos"})
        fs::create_directories(dir / sub, ec);

    Evento e;
    e.nombre = nombre;
    if (!guardar(carpeta, e, err)) return {};
    return carpeta;
}

Evento EventoStore::cargar(const std::string& carpeta, bool* ok, std::string* err) const {
    if (ok != nullptr) *ok = false;
    const fs::path archivo = raiz_ / carpeta / kGuion;
    std::ifstream in(archivo);
    if (!in) {
        ponerErr(err, "no hay guion en " + archivo.string());
        return {};
    }
    std::stringstream ss;
    ss << in.rdbuf();
    const json j = json::parse(ss.str(), nullptr, false);
    if (j.is_discarded()) {
        // El archivo esta ahi pero roto. No se devuelve un evento vacio como
        // si nada: el operador tiene que enterarse ANTES de empezar a editar
        // encima, porque el respaldo del guardado siguiente seria el roto.
        ponerErr(err, "el guion esta dañado (JSON invalido): mira en Respaldos/");
        return {};
    }
    if (ok != nullptr) *ok = true;
    return Evento::deJson(j);
}

bool EventoStore::guardar(const std::string& carpeta, const Evento& e, std::string* err) const {
    const fs::path dir = raiz_ / carpeta;
    std::error_code ec;
    if (!fs::is_directory(dir, ec)) {
        ponerErr(err, "no existe la carpeta del evento");
        return false;
    }
    const fs::path archivo = dir / kGuion;

    // Respaldo del que estaba. Que falle el respaldo no impide guardar, pero
    // se avisa: es peor quedarse sin poder guardar en medio de un evento.
    if (fs::exists(archivo, ec)) {
        fs::create_directories(dir / "Respaldos", ec);
        fs::copy_file(archivo, dir / "Respaldos" / ("evento-" + ahoraParaNombre() + ".json"),
                      fs::copy_options::overwrite_existing, ec);
    }

    // Se escribe a un temporal y se renombra: si se corta la luz en el medio,
    // el guion que estaba sigue entero.
    const fs::path tmp = dir / "evento.json.nuevo";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) {
            ponerErr(err, "no se pudo escribir en la carpeta del evento");
            return false;
        }
        out << e.aJson().dump(2) << "\n";
        if (!out.good()) {
            ponerErr(err, "no se pudo escribir el guion entero");
            return false;
        }
    }
    fs::rename(tmp, archivo, ec);
    if (ec) {
        ponerErr(err, "no se pudo reemplazar el guion: " + ec.message());
        fs::remove(tmp, ec);
        return false;
    }
    return true;
}

bool EventoStore::borrar(const std::string& carpeta, std::string* err) const {
    if (carpeta.empty() || carpeta.find("..") != std::string::npos ||
        carpeta.find('/') != std::string::npos || carpeta.find('\\') != std::string::npos) {
        ponerErr(err, "nombre de carpeta invalido");
        return false;
    }
    std::error_code ec;
    const fs::path dir = raiz_ / carpeta;
    if (!fs::is_directory(dir, ec)) {
        ponerErr(err, "no existe esa carpeta");
        return false;
    }
    fs::remove_all(dir, ec);
    if (ec) {
        ponerErr(err, "no se pudo borrar: " + ec.message());
        return false;
    }
    return true;
}

} // namespace ba::evento
