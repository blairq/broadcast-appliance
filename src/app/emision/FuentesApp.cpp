// ============================================================================
// src/app/emision/FuentesApp.cpp — ver FuentesApp.hpp
// ============================================================================
#include "emision/FuentesApp.hpp"
#include "i18n/I18n.hpp"

#include <atomic>
#include <mutex>

namespace ba::app::fuentes {

using json = nlohmann::json;

namespace {
std::mutex gMu;
std::map<std::string, Tipo> gTipos;
std::map<std::string, Instancia> gVivas; // por nombre
std::uint64_t gProximo = 1;
std::uint64_t gVersion = 0;
Salida gSalida;
Cambiar gCambiar;
Consultas gConsultas;
std::atomic<BroadcastEngine*> gMotor{nullptr};
std::map<std::string, std::map<int, json>> gExtras;
std::function<void(const std::string&)> gAvisoExtras;
} // namespace

BroadcastEngine* motorApp() { return gMotor.load(); }
void ponerMotorApp(BroadcastEngine* e) { gMotor = e; }

void registrar(Tipo t) {
    std::lock_guard<std::mutex> l(gMu);
    gTipos[t.id] = std::move(t);
}

bool tipo(const std::string& id, Tipo& out) {
    std::lock_guard<std::mutex> l(gMu);
    auto it = gTipos.find(id);
    if (it == gTipos.end()) return false;
    out = it->second;
    return true;
}

std::vector<Tipo> tipos() {
    std::lock_guard<std::mutex> l(gMu);
    std::vector<Tipo> v;
    for (const auto& [k, t] : gTipos)
        v.push_back(t);
    return v;
}

json conDefectos(const std::string& id, const json& ajustes) {
    json d;
    {
        std::lock_guard<std::mutex> l(gMu);
        auto it = gTipos.find(id);
        d = it != gTipos.end() ? it->second.defectos : json::object();
    }
    if (!d.is_object()) d = json::object();
    if (ajustes.is_object())
        for (auto it = ajustes.begin(); it != ajustes.end(); ++it) {
            if (it->is_object() && d.contains(it.key()) && d[it.key()].is_object())
                d[it.key()].update(*it); // la letra: lo que falta queda de fábrica
            else
                d[it.key()] = *it;
        }
    return d;
}

void alta(const std::string& nombre, const std::string& t, const json& ajustes) {
    const json a = conDefectos(t, ajustes);
    std::lock_guard<std::mutex> l(gMu);
    Instancia& i = gVivas[nombre];
    if (i.id == 0) i.id = gProximo++;
    i.nombre = nombre;
    i.tipo = t;
    i.ajustes = a;
    i.version = ++gVersion;
}

void baja(const std::string& nombre) {
    std::lock_guard<std::mutex> l(gMu);
    gVivas.erase(nombre);
}

void bajaTodas() {
    std::lock_guard<std::mutex> l(gMu);
    gVivas.clear();
}

std::vector<Instancia> de(const std::string& t) {
    std::lock_guard<std::mutex> l(gMu);
    std::vector<Instancia> v;
    for (const auto& [n, i] : gVivas)
        if (i.tipo == t) v.push_back(i);
    return v;
}

void ponerSalida(Salida s) {
    std::lock_guard<std::mutex> l(gMu);
    gSalida = std::move(s);
}

void empujar(std::uint64_t id, const std::uint8_t* bgra, int ancho, int alto, int stride) {
    std::string nombre;
    Salida s;
    {
        std::lock_guard<std::mutex> l(gMu);
        for (const auto& [n, i] : gVivas)
            if (i.id == id) nombre = n;
        s = gSalida;
    }
    if (nombre.empty() || !s || bgra == nullptr || ancho <= 0 || alto <= 0) return;
    s(nombre, bgra, ancho, alto, stride > 0 ? stride : ancho * 4);
}

void ponerCambiar(Cambiar c) {
    std::lock_guard<std::mutex> l(gMu);
    gCambiar = std::move(c);
}

bool cambiarAjustes(const std::string& nombre, const json& cambios) {
    Cambiar c;
    {
        std::lock_guard<std::mutex> l(gMu);
        c = gCambiar;
    }
    return c && c(nombre, cambios);
}

void ponerConsultas(Consultas c) {
    std::lock_guard<std::mutex> l(gMu);
    gConsultas = std::move(c);
}

std::vector<std::pair<std::string, std::string>> listar() {
    Consultas c;
    {
        std::lock_guard<std::mutex> l(gMu);
        c = gConsultas;
    }
    return c.listar ? c.listar() : std::vector<std::pair<std::string, std::string>>{};
}

json ajustesDe(const std::string& nombre) {
    Consultas c;
    {
        std::lock_guard<std::mutex> l(gMu);
        c = gConsultas;
    }
    json a = c.ajustes ? c.ajustes(nombre) : json::object();
    return a.is_object() ? a : json::object();
}

std::string propiedadElemento(const std::string& entrada, const std::string& elemento, const std::string& prop,
                              const std::string& valor) {
    Consultas c;
    {
        std::lock_guard<std::mutex> l(gMu);
        c = gConsultas;
    }
    return c.propiedad ? c.propiedad(entrada, elemento, prop, valor) : ba::i18n::traducir("emision", "no hay motor");
}

json propiedadesElemento(const std::string& entrada, const std::string& elemento) {
    Consultas c;
    {
        std::lock_guard<std::mutex> l(gMu);
        c = gConsultas;
    }
    json o = c.propiedades ? c.propiedades(entrada, elemento) : json::object();
    return o.is_object() ? o : json::object();
}

bool crearGlobal(const std::string& nombre, const std::string& tipo, const json& ajustes, bool monitor,
                 const std::vector<int>& pistas) {
    Consultas c;
    {
        std::lock_guard<std::mutex> l(gMu);
        c = gConsultas;
    }
    return c.crearGlobal && c.crearGlobal(nombre, tipo, ajustes, monitor, pistas);
}

void quitarGlobal(const std::string& nombre) {
    Consultas c;
    {
        std::lock_guard<std::mutex> l(gMu);
        c = gConsultas;
    }
    if (c.quitarGlobal) c.quitarGlobal(nombre);
}

void ponerFiltrosExtra(const std::string& entrada, int orden, const json& filtros) {
    std::function<void(const std::string&)> aviso;
    {
        std::lock_guard<std::mutex> l(gMu);
        auto& m = gExtras[entrada];
        const bool vacio = !filtros.is_array() || filtros.empty();
        const auto it = m.find(orden);
        if (vacio ? it == m.end() : (it != m.end() && it->second == filtros)) return; // igual que antes
        if (vacio)
            m.erase(orden);
        else
            m[orden] = filtros;
        aviso = gAvisoExtras;
    }
    if (aviso) aviso(entrada);
}

json filtrosExtra(const std::string& entrada) {
    std::lock_guard<std::mutex> l(gMu);
    json out = json::array();
    auto it = gExtras.find(entrada);
    if (it != gExtras.end())
        for (const auto& [orden, fs] : it->second)
            for (const auto& f : fs) out.push_back(f);
    return out;
}

void ponerAvisoExtras(std::function<void(const std::string&)> fn) {
    std::lock_guard<std::mutex> l(gMu);
    gAvisoExtras = std::move(fn);
}

} // namespace ba::app::fuentes
