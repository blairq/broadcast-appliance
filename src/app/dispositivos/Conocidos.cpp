// src/app/dispositivos/Conocidos.cpp — ver Conocidos.hpp

#include "dispositivos/Conocidos.hpp"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <fstream>
#include <system_error>

namespace ba::app::dispositivos {

using json = nlohmann::json;

namespace {
std::atomic<Conocidos*> gMaquina{nullptr};

std::string recortar(const std::string& s) {
    const auto a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return {};
    const auto b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

/// Sin distinguir mayúsculas (ASCII): «Mic atril» y «mic atril» son el mismo nombre para el operador.
std::string minusculas(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}
} // namespace

bool seRecuerda(const std::string& tipo) {
    return tipo == "camara_local" || tipo == "microfono" || tipo == "salida_audio" || tipo == "ndi" ||
           tipo == "camara_red";
}

json Conocido::comoJson() const {
    json j{{"tipo", tipo}, {"tecnico", tecnico}, {"valor", valor}};
    if (!nombre.empty()) j["nombre"] = nombre;
    if (!config.empty()) j["config"] = config;
    if (!desde.empty()) j["desde"] = desde;
    return j;
}

Conocidos::Conocidos(std::filesystem::path archivo) : archivo_(std::move(archivo)) { leer(); }

void Conocidos::leer() {
    std::lock_guard<std::mutex> l(mu_);
    todos_.clear();
    if (archivo_.empty()) return;
    std::ifstream f(archivo_);
    if (!f) return;
    const json j = json::parse(f, nullptr, false);
    if (!j.is_object()) return;
    const json eq = j.value("equipos", json::object());
    if (!eq.is_object()) return;
    for (auto it = eq.begin(); it != eq.end(); ++it) {
        if (!it->is_object()) continue;
        Conocido c;
        c.id = it.key();
        c.tipo = it->value("tipo", std::string());
        c.tecnico = it->value("tecnico", std::string());
        c.valor = it->value("valor", std::string());
        c.nombre = it->value("nombre", std::string());
        c.desde = it->value("desde", std::string());
        const json cfg = it->value("config", json::object());
        if (cfg.is_object()) c.config = cfg;
        todos_[c.id] = std::move(c);
    }
}

bool Conocidos::guardar() const {
    if (archivo_.empty()) return true;
    json eq = json::object();
    for (const auto& [id, c] : todos_)
        eq[id] = c.comoJson();
    std::error_code ec;
    std::filesystem::create_directories(archivo_.parent_path(), ec);
    // A un temporal y después se reemplaza: un corte de luz a mitad de la
    // escritura no deja el archivo cortado (y la máquina sin nombres).
    std::filesystem::path tmp = archivo_;
    tmp += ".tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f) return false;
        f << json{{"version", 1}, {"equipos", eq}}.dump(2, ' ', false, json::error_handler_t::replace);
        if (!f) return false;
    }
    std::filesystem::rename(tmp, archivo_, ec);
    return !ec;
}

bool Conocidos::visto(const std::string& id, const std::string& tipo, const std::string& tecnico,
                      const std::string& valor, const std::string& cuando) {
    if (id.empty() || !seRecuerda(tipo)) return false;
    std::lock_guard<std::mutex> l(mu_);
    auto it = todos_.find(id);
    if (it == todos_.end()) {
        Conocido c;
        c.id = id;
        c.tipo = tipo;
        c.tecnico = tecnico;
        c.valor = valor;
        c.desde = cuando;
        todos_[id] = std::move(c);
        guardar();
        return true;
    }
    Conocido& c = it->second;
    if (c.tipo == tipo && c.tecnico == tecnico && c.valor == valor) return false;
    c.tipo = tipo;
    if (!tecnico.empty()) c.tecnico = tecnico;
    if (!valor.empty()) c.valor = valor;
    guardar();
    return true;
}

std::string Conocidos::renombrar(const std::string& id, const std::string& nombre0) {
    const std::string nombre = recortar(nombre0);
    if (nombre.size() > 80) return "el nombre es muy largo (hasta 80 letras)";
    std::lock_guard<std::mutex> l(mu_);
    auto it = todos_.find(id);
    if (it == todos_.end()) return "no hay un dispositivo «" + id + "» (equipos_listar)";
    if (!nombre.empty())
        for (const auto& [otro, c] : todos_)
            if (otro != id && minusculas(c.mostrar()) == minusculas(nombre))
                return "ya hay un dispositivo que se llama «" + c.mostrar() + "»: elegí otro nombre";
    // Ponerle el mismo nombre que da el sistema es no ponerle nombre: así, si el
    // sistema lo cambia (otra versión del driver), lo sigue.
    it->second.nombre = nombre == it->second.tecnico ? std::string() : nombre;
    return guardar() ? std::string() : "no se pudo guardar el nombre en " + archivo_.string();
}

std::string Conocidos::configurar(const std::string& id, const json& config) {
    if (!config.is_object()) return "la configuración tiene que ser un objeto";
    std::lock_guard<std::mutex> l(mu_);
    auto it = todos_.find(id);
    if (it == todos_.end()) return "no hay un dispositivo «" + id + "» (equipos_listar)";
    for (auto c = config.begin(); c != config.end(); ++c) {
        if (c->is_null() || (c->is_string() && c->get<std::string>().empty()))
            it->second.config.erase(c.key());
        else
            it->second.config[c.key()] = *c;
    }
    return guardar() ? std::string() : "no se pudo guardar en " + archivo_.string();
}

std::string Conocidos::olvidar(const std::string& id) {
    std::lock_guard<std::mutex> l(mu_);
    if (todos_.erase(id) == 0) return "no hay un dispositivo «" + id + "»";
    return guardar() ? std::string() : "no se pudo guardar en " + archivo_.string();
}

bool Conocidos::hay(const std::string& id) const {
    std::lock_guard<std::mutex> l(mu_);
    return todos_.count(id) > 0;
}

Conocido Conocidos::uno(const std::string& id) const {
    std::lock_guard<std::mutex> l(mu_);
    const auto it = todos_.find(id);
    return it == todos_.end() ? Conocido{} : it->second;
}

Conocido Conocidos::porValor(const std::string& tipo, const std::string& valor) const {
    std::lock_guard<std::mutex> l(mu_);
    for (const auto& [id, c] : todos_)
        if (c.tipo == tipo && c.valor == valor) return c;
    return {};
}

std::vector<Conocido> Conocidos::todos() const {
    std::lock_guard<std::mutex> l(mu_);
    std::vector<Conocido> out;
    out.reserve(todos_.size());
    for (const auto& [id, c] : todos_)
        out.push_back(c);
    return out;
}

std::string Conocidos::nombreDe(const std::string& id, const std::string& tecnico) const {
    std::lock_guard<std::mutex> l(mu_);
    const auto it = todos_.find(id);
    return it == todos_.end() || it->second.nombre.empty() ? tecnico : it->second.nombre;
}

void Conocidos::setDeLaMaquina(Conocidos* c) { gMaquina.store(c); }

std::string Conocidos::nombreParaMostrar(const std::string& id, const std::string& tecnico) {
    Conocidos* c = gMaquina.load();
    return c != nullptr ? c->nombreDe(id, tecnico) : tecnico;
}

} // namespace ba::app::dispositivos
