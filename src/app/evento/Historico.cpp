// ============================================================================
// src/app/evento/Historico.cpp — ver Historico.hpp
// ============================================================================
#include "evento/Historico.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <set>

#include "evento/Bitacora.hpp"

namespace ba::evento {

namespace fs = std::filesystem;

namespace {

nlohmann::json lista(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    nlohmann::json j = f ? nlohmann::json::parse(f, nullptr, false) : nlohmann::json();
    return j.is_array() ? j : nlohmann::json::array();
}

std::string minusculas(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

/// Un pedazo de la línea alrededor de lo encontrado, sin partir un carácter UTF-8.
std::string recorte(const std::string& l, std::size_t donde, std::size_t largo) {
    std::size_t ini = donde > 80 ? donde - 80 : 0;
    std::size_t fin = std::min(l.size(), donde + largo + 120);
    while (ini > 0 && (static_cast<unsigned char>(l[ini]) & 0xC0) == 0x80) --ini;
    while (fin < l.size() && (static_cast<unsigned char>(l[fin]) & 0xC0) == 0x80) ++fin;
    return (ini > 0 ? "…" : "") + l.substr(ini, fin - ini) + (fin < l.size() ? "…" : "");
}

bool esEvento(const fs::path& d) {
    std::error_code ec;
    return fs::exists(d / "evento.json", ec);
}

} // namespace

nlohmann::json Historico::resumen(const fs::path& evento) {
    std::error_code ec;
    json r{{"grabaciones", lista(evento / "grabaciones.json").size()},
           {"transmisiones", lista(evento / "transmisiones.json").size()}};
    std::size_t textos = 0;
    for (const auto& e : fs::directory_iterator(evento / "Transcripciones", ec))
        textos += e.is_regular_file() ? 1 : 0;
    r["transcripciones"] = textos;
    const auto b = fs::file_size(evento / "bitacora.jsonl", ec);
    r["bitacora_bytes"] = ec ? 0 : b;
    r["sellado"] = fs::exists(evento / "sellos.json", ec);
    bool acta = false;
    for (const auto& e : fs::directory_iterator(evento, ec))
        acta = acta || e.path().filename().string().rfind("acta-", 0) == 0;
    r["acta"] = acta;
    r["config"] = fs::exists(evento / "config" / "inicio.json", ec);
    std::uintmax_t bytes = 0;
    for (fs::recursive_directory_iterator it(evento, ec), fin; !ec && it != fin; it.increment(ec))
        if (it->is_regular_file(ec)) bytes += it->file_size(ec);
    r["bytes"] = bytes;
    return r;
}

nlohmann::json Historico::grabaciones(const fs::path& evento) {
    const json escenas = Bitacora::leer(evento / "bitacora.jsonl", 0, 0, {"bus"}, "aire/escena", 0);
    json grab = json::array();
    std::error_code ec;
    for (json g : lista(evento / "grabaciones.json")) {
        const std::int64_t d = g.value("desde_ms", std::int64_t{0});
        const std::int64_t h = g.value("hasta_ms", std::int64_t{0});
        if (h != 0) g["duracion_seg"] = (h - d) / 1000;
        else g["en_curso"] = true;
        json caps = json::array();
        std::string escenaAntes;
        for (const json& e : escenas) {
            if (e.value("origen", std::string()) != "aire/escena") continue;
            const std::int64_t t = e.value("t", std::int64_t{0});
            const std::string esc = e.value("escena", std::string());
            if (t <= d) {
                escenaAntes = esc;
                continue;
            }
            if (h != 0 && t > h) break;
            if (caps.empty() && !escenaAntes.empty()) caps.push_back(json{{"seg", 0}, {"escena", escenaAntes}});
            caps.push_back(json{{"seg", (t - d) / 1000}, {"escena", esc}});
        }
        if (caps.empty() && !escenaAntes.empty()) caps.push_back(json{{"seg", 0}, {"escena", escenaAntes}});
        g["capitulos"] = caps;
        g["existe"] = fs::exists(g.value("archivo", std::string()), ec);
        grab.push_back(std::move(g));
    }
    json textos = json::array();
    for (const auto& e : fs::directory_iterator(evento / "Transcripciones", ec))
        if (e.is_regular_file())
            textos.push_back(json{{"archivo", e.path().filename().string()}, {"ruta", e.path().string()}, {"bytes", e.file_size()}});
    return json{{"grabaciones", grab}, {"transmisiones", lista(evento / "transmisiones.json")}, {"transcripciones", textos}};
}

nlohmann::json Historico::archivos(const fs::path& evento, std::size_t tope) {
    json out = json::array();
    std::error_code ec;
    for (fs::recursive_directory_iterator it(evento, ec), fin; !ec && it != fin; it.increment(ec)) {
        const std::string rel = fs::relative(it->path(), evento, ec).generic_string();
        if (it->is_directory(ec)) {
            if (rel == "Respaldos") it.disable_recursion_pending();
            continue;
        }
        out.push_back(json{{"archivo", rel}, {"bytes", it->file_size(ec)}});
        if (out.size() >= tope) break;
    }
    std::sort(out.begin(), out.end(), [](const json& a, const json& b) { return a["archivo"] < b["archivo"]; });
    return out;
}

nlohmann::json Historico::buscar(const fs::path& raiz, const std::string& texto, std::size_t limite,
                                 const std::atomic<bool>* cancelar) {
    json out = json::array();
    const std::string q = minusculas(texto);
    if (q.size() < 2) return out;
    std::error_code ec;
    std::vector<fs::path> eventos;
    for (fs::recursive_directory_iterator it(raiz, ec), fin; !ec && it != fin; it.increment(ec)) {
        if (!it->is_directory(ec)) continue;
        const std::string n = it->path().filename().string();
        if (!n.empty() && n[0] == '.') {
            it.disable_recursion_pending();
            continue;
        }
        if (esEvento(it->path())) {
            eventos.push_back(it->path());
            it.disable_recursion_pending(); // adentro de un evento no hay otros
        }
    }
    std::sort(eventos.begin(), eventos.end());
    std::reverse(eventos.begin(), eventos.end()); // lo más nuevo primero (las carpetas van por fecha)
    const auto enArchivo = [&](const fs::path& ev, const fs::path& p) {
        std::ifstream f(p, std::ios::binary);
        std::string l;
        std::size_t n = 0;
        while (std::getline(f, l)) {
            ++n;
            if (cancelar != nullptr && cancelar->load()) return;
            const std::string lm = minusculas(l);
            const auto donde = lm.find(q);
            if (donde == std::string::npos) continue;
            json r{{"evento", fs::relative(ev, raiz, ec).generic_string()},
                   {"archivo", fs::relative(p, ev, ec).generic_string()},
                   {"linea", n},
                   {"texto", recorte(l, donde, q.size())}};
            if (!l.empty() && l[0] == '{') {
                const json j = json::parse(l, nullptr, false);
                if (j.is_object() && j.contains("t")) r["t"] = j["t"];
            }
            out.push_back(std::move(r));
            if (out.size() >= limite) return;
        }
    };
    for (const fs::path& ev : eventos) {
        if (out.size() >= limite || (cancelar != nullptr && cancelar->load())) break;
        std::vector<fs::path> donde{ev / "bitacora.jsonl", ev / "evento.json", ev / "ficha.json"};
        for (const char* sub : {"Transcripciones", "asamblea", "Discursos"})
            for (const auto& e : fs::directory_iterator(ev / sub, ec))
                if (e.is_regular_file()) donde.push_back(e.path());
        for (const auto& e : fs::directory_iterator(ev, ec))
            if (e.path().filename().string().rfind("acta-", 0) == 0 && e.path().extension() == ".md") donde.push_back(e.path());
        for (const fs::path& p : donde) {
            if (out.size() >= limite) break;
            const std::string ext = p.extension().string();
            if (ext != ".jsonl" && ext != ".json" && ext != ".txt" && ext != ".srt" && ext != ".vtt" && ext != ".md")
                continue;
            if (fs::exists(p, ec)) enArchivo(ev, p);
        }
    }
    return out;
}

std::string Historico::exportar(const fs::path& evento, const fs::path& objetos, const fs::path& destino,
                                bool conGrabaciones, fs::path* copia) {
    std::error_code ec;
    if (!fs::is_directory(evento, ec)) return "no está la carpeta del evento";
    fs::path dst = destino / evento.filename();
    for (int i = 2; fs::exists(dst, ec); ++i) dst = destino / (evento.filename().string() + "-" + std::to_string(i));
    fs::create_directories(dst, ec);
    if (ec) return "no se pudo crear " + dst.string() + ": " + ec.message();
    for (fs::recursive_directory_iterator it(evento, ec), fin; !ec && it != fin; it.increment(ec)) {
        const fs::path rel = fs::relative(it->path(), evento, ec);
        const std::string r = rel.generic_string();
        if (it->is_directory(ec)) {
            if (r == "Respaldos" || (!conGrabaciones && r == "Grabaciones")) it.disable_recursion_pending();
            else fs::create_directories(dst / rel, ec);
            continue;
        }
        if (r.size() > 4 && r.compare(r.size() - 4, 4, ".tmp") == 0) continue;
        std::error_code e2;
        fs::copy_file(it->path(), dst / rel, fs::copy_options::overwrite_existing, e2);
        if (e2) return "no se pudo copiar " + r + ": " + e2.message();
    }
    // Lo que nombran las fotos de configuración, para que se lean sin Eventos/.objetos.
    std::set<std::string> shas;
    for (const char* f : {"inicio.json", "cierre.json", "ultima.json"}) {
        std::ifstream in(evento / "config" / f, std::ios::binary);
        const json j = in ? json::parse(in, nullptr, false) : json();
        if (!j.is_object()) continue;
        const json archivos = j.value("archivos", json::object()); // no .items() de un temporal
        for (const auto& [k, v] : archivos.items()) shas.insert(v.value("sha", std::string()));
    }
    for (const std::string& s : shas) {
        if (s.empty()) continue;
        fs::create_directories(dst / "config" / "objetos", ec);
        std::error_code e2;
        fs::copy_file(objetos / (s + ".json"), dst / "config" / "objetos" / (s + ".json"), fs::copy_options::skip_existing, e2);
    }
    if (copia != nullptr) *copia = dst;
    return {};
}

} // namespace ba::evento
