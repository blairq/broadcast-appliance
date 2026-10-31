// src/emisor/Pedido.cpp — ver el .hpp

#include "Pedido.hpp"

#include "persist/Escritor.hpp"
#include "security/CredentialStore.hpp"

#include <algorithm>
#include <cstdio>
#include <system_error>

namespace ba::emisor {

using json = nlohmann::json;

namespace {

/// `j[k]` si es del tipo de `def`; si no, `def` (un archivo a medias no rompe nada).
template <class T>
T valor(const json& j, const char* k, const T& def) {
    const auto it = j.find(k);
    if (it == j.end()) return def;
    try {
        return it->get<T>();
    } catch (const json::exception&) {
        return def;
    }
}

motor::Transmision::Config transmisionDe(const json& j) {
    motor::Transmision::Config c;
    c.url = valor(j, "url", c.url);
    c.urlRespaldo = valor(j, "url_respaldo", c.urlRespaldo);
    c.kbps = valor(j, "kbps", c.kbps);
    c.kbpsAudio = valor(j, "kbps_audio", c.kbpsAudio);
    c.gopSeg = valor(j, "gop_s", c.gopSeg);
    c.fps = valor(j, "fps", c.fps);
    c.codificadores = valor(j, "codificadores", c.codificadores);
    c.control = motor::controlTasaDe(valor<std::string>(j, "control", "CBR"));
    return c;
}

motor::Grabacion::Config grabacionDe(const json& j) {
    motor::Grabacion::Config c;
    c.carpeta = valor(j, "carpeta", c.carpeta);
    c.nombre = valor(j, "nombre", c.nombre);
    c.tramoSeg = valor(j, "tramo_s", c.tramoSeg);
    c.maxTramos = valor(j, "max_tramos", c.maxTramos);
    c.kbps = valor(j, "kbps", c.kbps);
    c.kbpsAudio = valor(j, "kbps_audio", c.kbpsAudio);
    c.gopSeg = valor(j, "gop_s", c.gopSeg);
    c.fps = valor(j, "fps", c.fps);
    c.codificadores = valor(j, "codificadores", c.codificadores);
    c.colchonSeg = valor(j, "colchon_s", c.colchonSeg);
    c.control = motor::controlTasaDe(valor<std::string>(j, "control", "CBR"));
    c.crf = valor(j, "crf", c.crf);
    c.carpetaRespaldo = valor(j, "carpeta_respaldo", c.carpetaRespaldo);
    c.codificadorPropio = valor(j, "codificador_propio", c.codificadorPropio);
    return c;
}

PedidoRepeticion repeticionDe(const json& j) {
    PedidoRepeticion r;
    r.segundos = std::max(1, valor(j, "segundos", r.segundos));
    r.carpeta = valor(j, "carpeta", r.carpeta);
    r.kbps = valor(j, "kbps", r.kbps);
    r.guardarSeq = valor(j, "guardar_seq", r.guardarSeq);
    r.guardarEn = valor(j, "guardar_en", r.guardarEn);
    return r;
}

/// `j[k]` si es un objeto (lo demás no es un bloque del pedido).
const json* bloque(const json& j, const char* k) {
    const auto it = j.find(k);
    return it != j.end() && it->is_object() ? &*it : nullptr;
}

/// Las claves de un pedido: las URL (llevan la clave del canal), donde estén. Se sacan
/// del JSON (quedan "") y se devuelven por su ruta («/transmision/url»…).
json sacarClaves(json& j) {
    json claves = json::object();
    const json plano = j.flatten(); // con nombre: .items() de un temporal lo usa destruido
    for (const auto& [ruta, v] : plano.items()) {
        const auto barra = ruta.rfind('/');
        const std::string k = barra == std::string::npos ? ruta : ruta.substr(barra + 1);
        if ((k != "url" && k != "url_respaldo") || !v.is_string() || v.get<std::string>().empty()) continue;
        claves[ruta] = v;
        j[json::json_pointer(ruta)] = "";
    }
    return claves;
}

/// Las vuelve a poner donde estaban (solo donde el pedido todavía tiene ese bloque).
void ponerClaves(json& j, const json& claves) {
    if (!claves.is_object()) return;
    for (const auto& [ruta, v] : claves.items()) {
        try {
            const json::json_pointer ptr(ruta);
            if (j.contains(ptr) && j[ptr].is_string() && j[ptr].get<std::string>().empty()) j[ptr] = v;
        } catch (const json::exception&) {
            // una ruta rota en la credencial: se ignora
        }
    }
}

} // namespace

json aJson(const motor::Transmision::Config& c) {
    return {{"url", c.url},         {"url_respaldo", c.urlRespaldo}, {"kbps", c.kbps},
            {"kbps_audio", c.kbpsAudio}, {"gop_s", c.gopSeg},   {"fps", c.fps},
            {"codificadores", c.codificadores}, {"control", motor::nombreControlTasa(c.control)}};
}

json aJson(const motor::Grabacion::Config& c) {
    return {{"carpeta", c.carpeta},
            {"nombre", c.nombre},
            {"tramo_s", c.tramoSeg},
            {"max_tramos", c.maxTramos},
            {"kbps", c.kbps},
            {"kbps_audio", c.kbpsAudio},
            {"gop_s", c.gopSeg},
            {"fps", c.fps},
            {"codificadores", c.codificadores},
            {"colchon_s", c.colchonSeg},
            {"control", motor::nombreControlTasa(c.control)},
            {"crf", c.crf},
            {"carpeta_respaldo", c.carpetaRespaldo},
            {"codificador_propio", c.codificadorPropio}};
}

json aJson(const Pedido& p) {
    json j{{"seq", p.seq},
           {"video", {{"ancho", p.ancho}, {"alto", p.alto}, {"fps_n", p.fpsN}, {"fps_d", p.fpsD}}},
           {"placa_texto", p.placaTexto}};
    if (p.transmision) j["transmision"] = aJson(*p.transmision);
    if (p.grabacion) j["grabacion"] = aJson(*p.grabacion);
    if (p.copia) j["copia"] = aJson(*p.copia);
    if (!p.pistas.empty()) j["pistas"] = p.pistas;
    if (!p.remux.empty()) j["remux"] = p.remux;
    if (!p.extras.empty()) {
        json ex = json::object();
        for (const auto& [id, c] : p.extras) ex[id] = aJson(c);
        j["extras"] = std::move(ex);
    }
    if (p.repeticion)
        j["repeticion"] = {{"segundos", p.repeticion->segundos},
                           {"carpeta", p.repeticion->carpeta},
                           {"kbps", p.repeticion->kbps},
                           {"guardar_seq", p.repeticion->guardarSeq},
                           {"guardar_en", p.repeticion->guardarEn}};
    if (p.limpio) {
        json l = json::object();
        if (p.limpio->transmision) l["transmision"] = aJson(*p.limpio->transmision);
        if (p.limpio->grabacion) l["grabacion"] = aJson(*p.limpio->grabacion);
        j["limpio"] = std::move(l);
    }
    return j;
}

Pedido pedidoDe(const json& j) {
    Pedido p;
    if (!j.is_object()) return p;
    p.seq = valor<std::uint64_t>(j, "seq", 0);
    const json v = valor(j, "video", json::object());
    if (v.is_object()) {
        p.ancho = std::max(16, valor(v, "ancho", p.ancho));
        p.alto = std::max(16, valor(v, "alto", p.alto));
        p.fpsN = std::max(1, valor(v, "fps_n", p.fpsN));
        p.fpsD = std::max(1, valor(v, "fps_d", p.fpsD));
    }
    p.placaTexto = valor(j, "placa_texto", p.placaTexto);
    if (const json* b = bloque(j, "transmision")) p.transmision = transmisionDe(*b);
    if (const json* b = bloque(j, "grabacion")) p.grabacion = grabacionDe(*b);
    if (const json* b = bloque(j, "copia")) p.copia = grabacionDe(*b);
    for (int n : valor(j, "pistas", std::vector<int>{}))
        if (n >= 2 && n <= 6 && std::find(p.pistas.begin(), p.pistas.end(), n) == p.pistas.end()) p.pistas.push_back(n);
    std::sort(p.pistas.begin(), p.pistas.end());
    p.remux = valor(j, "remux", p.remux);
    if (p.remux != "mp4" && p.remux != "mov") p.remux.clear();
    if (const json* ex = bloque(j, "extras"))
        for (const auto& [id, c] : ex->items())
            if (c.is_object()) p.extras[id] = transmisionDe(c);
    if (const json* b = bloque(j, "repeticion")) p.repeticion = repeticionDe(*b);
    if (const json* b = bloque(j, "limpio")) {
        PedidoLimpio l;
        if (const json* t = bloque(*b, "transmision")) l.transmision = transmisionDe(*t);
        if (const json* g = bloque(*b, "grabacion")) l.grabacion = grabacionDe(*g);
        p.limpio = std::move(l);
    }
    return p;
}

std::filesystem::path carpeta(const std::filesystem::path& runtime) { return runtime / "emisor"; }
std::filesystem::path rutaPedido(const std::filesystem::path& runtime, const std::string& id) {
    return carpeta(runtime) / (id + ".json");
}
std::filesystem::path rutaEstado(const std::filesystem::path& runtime, const std::string& id) {
    return carpeta(runtime) / (id + ".estado.json");
}

std::filesystem::path credenciales(const std::filesystem::path& runtime) { return runtime.parent_path() / "Credenciales"; }

std::string nombreCredencial(const std::string& id) {
    // El CredentialStore acepta [a-z0-9._-]: las mayúsculas del id van con un punto
    // adelante («Sala» → «.sala»; el id no tiene puntos, así que no chocan).
    std::string n = "emisor-";
    for (const char c : id) {
        if (c >= 'A' && c <= 'Z') {
            n += '.';
            n += static_cast<char>(c - 'A' + 'a');
        } else {
            n += c;
        }
    }
    if (security::CredentialStore::validName(n)) return n;
    // Uno largo con muchas mayúsculas no entra en 64: su huella (FNV-1a).
    std::uint64_t h = 1469598103934665603ULL;
    for (const char c : id) h = (h ^ static_cast<unsigned char>(c)) * 1099511628211ULL;
    char hex[17];
    std::snprintf(hex, sizeof hex, "%016llx", static_cast<unsigned long long>(h));
    return "emisor-h" + std::string(hex);
}

bool leer(const std::filesystem::path& runtime, const std::string& id, Pedido& p) {
    std::string txt;
    if (!persist::leerArchivo(rutaPedido(runtime, id), txt)) return false;
    json j = json::parse(txt, nullptr, false);
    if (!j.is_object()) return false;
    std::string claves;
    if (security::CredentialStore(credenciales(runtime)).read(nombreCredencial(id), claves))
        ponerClaves(j, json::parse(claves, nullptr, false));
    p = pedidoDe(j);
    return true;
}

bool escribir(const std::filesystem::path& runtime, const std::string& id, const Pedido& p, std::string* error) {
    json j = aJson(p);
    const json claves = sacarClaves(j);
    // Las claves antes que el pedido: el emisor que lea el pedido nuevo las encuentra.
    const security::CredentialStore cs(credenciales(runtime));
    const std::string nombre = nombreCredencial(id);
    if (!claves.empty() ? !cs.write(nombre, claves.dump()) : (cs.exists(nombre) && !cs.remove(nombre))) {
        if (error != nullptr) *error = "Credenciales/" + nombre;
        return false;
    }
    // Privado igual: dice adónde se transmite y dónde se graba.
    return persist::escribir(rutaPedido(runtime, id), j.dump(1, ' ', false, json::error_handler_t::replace),
                             persist::Durabilidad::Critica, persist::Permisos::Privados, error);
}

std::string nombreLibre(const std::filesystem::path& carpeta, const std::string& nombre) {
    const auto ocupado = [&carpeta](const std::string& n) {
        std::error_code ec;
        for (std::filesystem::directory_iterator it(carpeta, ec), fin; !ec && it != fin; it.increment(ec)) {
            const std::string f = it->path().filename().string();
            // Los tramos (<n>-00000.mkv) y las partes de una caída (<n>-parte2-00000.mkv).
            if (f.rfind(n + "-", 0) == 0 && (f.compare(n.size() + 1, 5, "parte") == 0 ||
                                             (f.size() > n.size() + 1 && f[n.size() + 1] >= '0' && f[n.size() + 1] <= '9')))
                return true;
        }
        return false;
    };
    if (!ocupado(nombre)) return nombre;
    for (int k = 2;; ++k) {
        const std::string n = nombre + "-sigue" + std::to_string(k);
        if (!ocupado(n)) return n;
    }
}

} // namespace ba::emisor
