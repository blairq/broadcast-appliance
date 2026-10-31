// src/emisor/Pedido.cpp — ver el .hpp

#include "Pedido.hpp"

#include "persist/Escritor.hpp"

#include <algorithm>
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
    if (j.contains("transmision") && j["transmision"].is_object()) p.transmision = transmisionDe(j["transmision"]);
    if (j.contains("grabacion") && j["grabacion"].is_object()) p.grabacion = grabacionDe(j["grabacion"]);
    if (j.contains("copia") && j["copia"].is_object()) p.copia = grabacionDe(j["copia"]);
    return p;
}

std::filesystem::path carpeta(const std::filesystem::path& runtime) { return runtime / "emisor"; }
std::filesystem::path rutaPedido(const std::filesystem::path& runtime, const std::string& id) {
    return carpeta(runtime) / (id + ".json");
}
std::filesystem::path rutaEstado(const std::filesystem::path& runtime, const std::string& id) {
    return carpeta(runtime) / (id + ".estado.json");
}

bool leer(const std::filesystem::path& ruta, Pedido& p) {
    std::string txt;
    if (!persist::leerArchivo(ruta, txt)) return false;
    const json j = json::parse(txt, nullptr, false);
    if (!j.is_object()) return false;
    p = pedidoDe(j);
    return true;
}

bool escribir(const std::filesystem::path& ruta, const Pedido& p, std::string* error) {
    return persist::escribir(ruta, aJson(p).dump(1, ' ', false, json::error_handler_t::replace), persist::Durabilidad::Critica,
                             persist::Permisos::Privados, error);
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
