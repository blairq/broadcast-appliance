// src/core/hw/SaludDrivers.cpp — ver SaludDrivers.hpp

#include "hw/SaludDrivers.hpp"
#include "i18n/I18n.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <system_error>

namespace ba::hw {

namespace fs = std::filesystem;
using json = nlohmann::json;

const char* nombreFalla(Falla f) {
    switch (f) {
        case Falla::Error: return "error";
        case Falla::Tiempo: return "tiempo";
        case Falla::Caida: return "caida";
    }
    return "error";
}

json EstadoDriver::comoJson() const {
    json j{{"nombre", nombre}, {"tipo", tipo},        {"firma", firma},   {"rango", rango},
           {"estado", estado}, {"fallas", fallas},    {"fallas_total", fallasTotal},
           {"presente", presente}};
    if (!ultimaFalla.empty()) j["ultima_falla"] = ultimaFalla;
    if (!motivo.empty()) j["motivo"] = motivo;
    if (!detalle.empty()) j["detalle"] = detalle;
    if (!cuando.empty()) j["cuando"] = cuando;
    if (!ultimoOk.empty()) j["ultimo_ok"] = ultimoOk;
    return j;
}

EstadoDriver EstadoDriver::deJson(const std::string& id, const json& j) {
    EstadoDriver e;
    e.id = id;
    if (!j.is_object()) return e;
    e.nombre = j.value("nombre", id);
    e.tipo = j.value("tipo", std::string());
    e.firma = j.value("firma", std::string());
    e.rango = j.value("rango", 0);
    e.estado = j.value("estado", std::string("sin_probar"));
    e.fallas = j.value("fallas", 0);
    e.fallasTotal = j.value("fallas_total", 0);
    e.ultimaFalla = j.value("ultima_falla", std::string());
    e.motivo = j.value("motivo", std::string());
    e.detalle = j.value("detalle", std::string());
    e.cuando = j.value("cuando", std::string());
    e.ultimoOk = j.value("ultimo_ok", std::string());
    e.presente = j.value("presente", true);
    return e;
}

SaludDrivers::SaludDrivers(fs::path archivo, std::string huella, std::string etiqueta)
    : archivo_(std::move(archivo)), huella_(std::move(huella)), etiqueta_(std::move(etiqueta)) {
    leer();
}

namespace {
json leerArchivo(const fs::path& p) {
    if (p.empty()) return json::object();
    std::ifstream in(p, std::ios::binary);
    if (!in) return json::object();
    json j = json::parse(in, nullptr, false);
    return j.is_object() ? j : json::object();
}
} // namespace

void SaludDrivers::leer() {
    const json j = leerArchivo(archivo_);
    drivers_.clear();
    otras_ = json::object();
    primera_.clear();
    sondeoCompleto_.clear();
    const json maquinas = j.value("maquinas", json::object());
    if (!maquinas.is_object()) return;
    for (auto it = maquinas.begin(); it != maquinas.end(); ++it) {
        if (it.key() != huella_) {
            otras_[it.key()] = it.value();
            continue;
        }
        const json& m = it.value();
        primera_ = m.value("primera", std::string());
        sondeoCompleto_ = m.value("sondeo_completo", std::string());
        if (etiqueta_.empty()) etiqueta_ = m.value("etiqueta", std::string());
        const json ds = m.value("drivers", json::object());
        if (ds.is_object())
            for (auto d = ds.begin(); d != ds.end(); ++d) drivers_[d.key()] = EstadoDriver::deJson(d.key(), d.value());
    }
}

void SaludDrivers::guardar() const {
    if (archivo_.empty()) return;
    json ds = json::object();
    for (const auto& [id, e] : drivers_) ds[id] = e.comoJson();
    json maquinas = otras_;
    json esta{{"drivers", ds}};
    if (!etiqueta_.empty()) esta["etiqueta"] = etiqueta_;
    if (!primera_.empty()) esta["primera"] = primera_;
    if (!sondeoCompleto_.empty()) esta["sondeo_completo"] = sondeoCompleto_;
    maquinas[huella_] = esta;
    const json todo{{"version", 1}, {"maquinas", maquinas}};
    std::error_code ec;
    fs::create_directories(archivo_.parent_path(), ec);
    // Escribir aparte y renombrar: un corte de luz a la mitad no deja un archivo roto
    // (y con él, los drivers en cuarentena volviendo a cargarse).
    const fs::path tmp = archivo_.string() + ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) return;
        out << todo.dump(2, ' ', false, json::error_handler_t::replace);
        if (!out) return;
    }
    fs::rename(tmp, archivo_, ec);
    if (ec) {
        fs::remove(archivo_, ec);
        fs::rename(tmp, archivo_, ec);
    }
}

void SaludDrivers::sondeoCompleto(const std::string& cuando) {
    if (sondeoCompleto_.empty()) sondeoCompleto_ = cuando;
    if (primera_.empty()) primera_ = cuando;
    guardar();
}

std::vector<std::string> SaludDrivers::novedades(const std::vector<DriverListado>& hay) {
    std::vector<std::string> probar;
    std::map<std::string, bool> estan;
    for (const DriverListado& l : hay) {
        estan[l.id] = true;
        auto it = drivers_.find(l.id);
        if (it == drivers_.end()) {
            EstadoDriver e;
            e.id = l.id;
            e.nombre = l.nombre.empty() ? l.id : l.nombre;
            e.tipo = l.tipo;
            e.firma = l.firma;
            e.rango = l.rango;
            drivers_[l.id] = e;
            probar.push_back(l.id);
            continue;
        }
        EstadoDriver& e = it->second;
        if (!l.nombre.empty()) e.nombre = l.nombre;
        e.tipo = l.tipo;
        e.rango = l.rango;
        e.presente = true;
        if (e.firma != l.firma) {
            // Otra versión u otro archivo: lo aprendido de la anterior no vale.
            e.firma = l.firma;
            e.estado = "sin_probar";
            e.fallas = 0;
            e.motivo.clear();
            e.ultimaFalla.clear();
            probar.push_back(l.id);
        } else if (e.estado == "cuarentena" || e.estado == "anda") {
            // Cuarentena: no se carga solo. Anda: se usa directo, sin volver a probarlo.
        } else {
            probar.push_back(l.id); // sin_probar o falla (todavía no llegó al umbral)
        }
    }
    for (auto& [id, e] : drivers_)
        if (estan.count(id) == 0) e.presente = false;
    guardar();
    return probar;
}

void SaludDrivers::exito(const std::string& id, const std::string& detalle, const std::string& cuando) {
    EstadoDriver& e = drivers_[id];
    if (e.id.empty()) {
        e.id = id;
        e.nombre = id;
    }
    e.estado = "anda";
    e.fallas = 0;
    e.motivo.clear();
    e.ultimaFalla.clear();
    e.detalle = detalle;
    e.cuando = cuando;
    e.ultimoOk = cuando;
    e.presente = true;
    e.unaVez = false;
    guardar();
}

bool SaludDrivers::falla(const std::string& id, Falla tipo, const std::string& motivo, const std::string& cuando) {
    EstadoDriver& e = drivers_[id];
    if (e.id.empty()) {
        e.id = id;
        e.nombre = id;
    }
    const bool antes = e.estado == "cuarentena";
    ++e.fallas;
    ++e.fallasTotal;
    e.ultimaFalla = nombreFalla(tipo);
    e.motivo = motivo;
    e.cuando = cuando;
    e.presente = true;
    e.unaVez = false;
    e.estado = e.fallas >= umbral_ ? "cuarentena" : "falla";
    guardar();
    return !antes && e.estado == "cuarentena";
}

void SaludDrivers::ausente(const std::string& id) {
    auto it = drivers_.find(id);
    if (it == drivers_.end()) return;
    it->second.presente = false;
    it->second.unaVez = false;
    guardar();
}

std::string SaludDrivers::reintentar(const std::string& id) {
    auto it = drivers_.find(id);
    if (it == drivers_.end()) return ba::i18n::llenar(ba::i18n::traducir("drivers", "no hay un driver «%1» en esta máquina"), {id});
    if (!it->second.presente)
        return ba::i18n::llenar(ba::i18n::traducir("drivers", "«%1» no está instalado ahora"), {it->second.nombre});
    it->second.unaVez = true;
    return {};
}

bool SaludDrivers::enCuarentena(const std::string& id) const {
    const auto it = drivers_.find(id);
    return it != drivers_.end() && it->second.estado == "cuarentena";
}

bool SaludDrivers::sePuedeCargar(const std::string& id) const {
    const auto it = drivers_.find(id);
    return it == drivers_.end() || it->second.estado != "cuarentena" || it->second.unaVez;
}

std::vector<EstadoDriver> SaludDrivers::todos() const {
    std::vector<EstadoDriver> out;
    for (const auto& [id, e] : drivers_) out.push_back(e);
    return out;
}

std::optional<EstadoDriver> SaludDrivers::uno(const std::string& id) const {
    const auto it = drivers_.find(id);
    if (it == drivers_.end()) return std::nullopt;
    return it->second;
}

int SaludDrivers::cuantosAndan() const {
    int n = 0;
    for (const auto& [id, e] : drivers_) n += e.presente && e.estado == "anda" ? 1 : 0;
    return n;
}

int SaludDrivers::cuantosEnCuarentena() const {
    int n = 0;
    for (const auto& [id, e] : drivers_) n += e.presente && e.estado == "cuarentena" ? 1 : 0;
    return n;
}

std::vector<std::string> SaludDrivers::cuarentenaDe(const fs::path& archivo, const std::string& huella,
                                                    const std::string& prefijo) {
    std::vector<std::string> out;
    const json j = leerArchivo(archivo);
    const json ds = j.value("maquinas", json::object()).value(huella, json::object()).value("drivers", json::object());
    if (!ds.is_object()) return out;
    for (auto it = ds.begin(); it != ds.end(); ++it)
        if (it.key().rfind(prefijo, 0) == 0 && it.value().is_object() &&
            it.value().value("estado", std::string()) == "cuarentena")
            out.push_back(it.key());
    return out;
}

namespace {
std::string minusculas(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}
/// La última línea no vacía (lo que escribió el driver justo antes de fallar), corta.
std::string ultimaLinea(const std::string& s) {
    std::string linea;
    std::size_t fin = s.size();
    while (fin > 0) {
        std::size_t ini = s.rfind('\n', fin - 1);
        ini = ini == std::string::npos ? 0 : ini + 1;
        linea = s.substr(ini, fin - ini);
        while (!linea.empty() && (linea.back() == '\r' || linea.back() == ' ')) linea.pop_back();
        if (!linea.empty()) break;
        if (ini == 0) break;
        fin = ini - 1;
    }
    if (linea.size() > 160) linea = linea.substr(0, 157) + "…";
    return linea;
}
} // namespace

std::string motivoLegible(Falla tipo, const std::string& nombre, const std::string& mensaje, const std::string& salida,
                          int limiteMs) {
    // JackRouter (y lo que va encima, como SAR) sin el servidor de JACK: el caso de
    // la notebook. Lo dice por stderr («Open error: is JACK server running?»), no
    // siempre por el mensaje de error del driver. El nombre solo no alcanza: un
    // JackRouter que se colgó por otra cosa se cuenta como colgado.
    (void)nombre;
    if (minusculas(mensaje + "\n" + salida).find("jack") != std::string::npos)
        return ba::i18n::traducir("drivers", "no contesta: ¿está corriendo el servidor de JACK?");
    switch (tipo) {
        case Falla::Tiempo:
            return ba::i18n::llenar(ba::i18n::traducir("drivers", "no contestó en %1 s: se colgó al cargarlo"),
                                    {std::to_string((limiteMs + 500) / 1000)});
        case Falla::Caida: {
            const std::string l = ultimaLinea(salida);
            return l.empty() ? ba::i18n::traducir("drivers", "se cayó el proceso que lo probaba (el driver lo tumbó)")
                             : ba::i18n::llenar(ba::i18n::traducir("drivers", "se cayó el proceso que lo probaba (el "
                                                                              "driver lo tumbó): %1"),
                                                {l});
        }
        case Falla::Error: break;
    }
    if (!mensaje.empty()) return ba::i18n::llenar(ba::i18n::traducir("drivers", "no arrancó: %1"), {mensaje});
    const std::string l = ultimaLinea(salida);
    return l.empty() ? ba::i18n::traducir("drivers", "no arrancó (no dijo por qué)")
                     : ba::i18n::llenar(ba::i18n::traducir("drivers", "no arrancó: %1"), {l});
}

} // namespace ba::hw
