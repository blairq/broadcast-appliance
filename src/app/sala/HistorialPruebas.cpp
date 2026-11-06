// src/app/sala/HistorialPruebas.cpp — el historial de pruebas de la máquina (ver el .hpp)

#include "sala/HistorialPruebas.hpp"

#include <algorithm>
#include <cstdio>
#include <set>
#include <sstream>
#include <system_error>

#include "persist/ColaEscritura.hpp"
#include "sala/CapacidadReglas.hpp"

namespace ba::app::sala::historial {

using json = nlohmann::json;
namespace fs = std::filesystem;

namespace {
const std::set<std::string>& clavesPrivadas() {
    static const std::set<std::string> k{"credencial", "codigo", "codigoInternet", "clave", "token", "pin",
                                         "comando", "comandoWindows", "comando_externo", "secreto", "HINCHADA_PIN"};
    return k;
}
/// Un número del informe (en `a.b.c`), -1 si no está.
double numero(const json& j, std::initializer_list<const char*> camino) {
    const json* x = &j;
    for (const char* k : camino) {
        if (!x->is_object() || !x->contains(k)) return -1;
        x = &(*x)[k];
    }
    return x->is_number() ? x->get<double>() : -1;
}
std::string texto(const json& j, std::initializer_list<const char*> camino) {
    const json* x = &j;
    for (const char* k : camino) {
        if (!x->is_object() || !x->contains(k)) return {};
        x = &(*x)[k];
    }
    return x->is_string() ? x->get<std::string>() : std::string();
}
std::string csvCampo(const json& v) {
    std::string s = v.is_string() ? v.get<std::string>() : v.is_null() ? std::string() : v.dump();
    if (s.find_first_of(",\"\n") != std::string::npos) {
        std::string q = "\"";
        for (char c : s) q += c == '"' ? std::string("\"\"") : std::string(1, c);
        return q + "\"";
    }
    return s;
}
} // namespace

void limpiarPrivado(json& j) {
    if (j.is_object()) {
        for (auto it = j.begin(); it != j.end();) {
            if (clavesPrivadas().count(it.key())) {
                it = j.erase(it);
            } else {
                limpiarPrivado(it.value());
                ++it;
            }
        }
    } else if (j.is_array()) {
        for (json& x : j) limpiarPrivado(x);
    }
}

json sobre(json informe, const std::string& tipo, const std::string& version, int64_t ahoraMs) {
    if (!informe.is_object()) informe = json::object();
    limpiarPrivado(informe);
    informe["tipo"] = tipo;
    informe["programa"] = {{"version", version}};
    if (!informe.contains("fecha") || !informe["fecha"].is_number()) informe["fecha"] = informe.value("inicioMs", ahoraMs);
    return informe;
}

std::string nombreArchivo(int64_t fechaMs, const std::string& maquina, const std::string& tipo) {
    return capacidad::nombreArchivo(fechaMs, maquina, tipo);
}

bool nombreValido(const std::string& a) {
    if (a.empty() || a.size() > 200 || a.find("..") != std::string::npos) return false;
    if (a.find('/') != std::string::npos || a.find('\\') != std::string::npos || a.find(':') != std::string::npos) return false;
    return a.size() > 5 && a.compare(a.size() - 5, 5, ".json") == 0;
}

json resumen(const json& inf, const std::string& archivo) {
    const std::string tipo = inf.value("tipo", std::string());
    json r{{"archivo", archivo},
           {"fecha", inf.value("fecha", int64_t{0})},
           {"tipo", tipo},
           {"maquina", inf.value("maquina", texto(inf, {"fichas", "local", "nombre"}))},
           {"frase", inf.value("frase", inf.value("nota", std::string()))},
           {"version", texto(inf, {"programa", "version"})}};
    // La otra máquina: la medida (remoto) o las que generaron (recibida, la de sala con varias).
    std::string remota = texto(inf, {"fichas", "remota", "nombre"});
    if (tipo == "remoto") {
        // La capacidad de otro equipo: `maquina` es la medida (la otra) y `generador`, esta.
        if (remota.empty()) remota = inf.value("maquina", std::string());
        r["maquina"] = inf.value("generador", texto(inf, {"fichas", "local", "nombre"}));
    }
    if (remota.empty() && inf.contains("generadoras") && inf["generadoras"].is_array()) {
        for (const json& g : inf["generadoras"]) remota += (remota.empty() ? "" : ", ") + g.value("maquina", std::string());
    }
    if (remota.empty() && inf.contains("generadores") && inf["generadores"].is_array()) {
        for (const json& g : inf["generadores"]) remota += (remota.empty() ? "" : ", ") + g.value("maquina", std::string());
    }
    r["remota"] = remota;
    if (const json res = inf.value("resultado", json::object()); res.is_object() && !res.empty()) {
        r["conMargen"] = res.value("conMargen", 0);
        r["maximo"] = res.value("maximo", 0);
        r["alMenos"] = res.value("alMenos", false);
        r["limita"] = res.value("limitaTexto", res.value("limita", std::string()));
    }
    // Cuántos: los pedidos de la prueba de sala, el tope de la rampa, los que llegaron.
    double n = numero(inf, {"pedidos"});
    if (n < 0) n = numero(inf, {"tope"});
    if (n < 0) n = numero(inf, {"maxEntraron"});
    if (n < 0) n = numero(inf, {"invitados"});
    r["participantes"] = n < 0 ? -1 : static_cast<int>(n);
    if (inf.contains("decodificar") && inf["decodificar"].is_object()) r["decodificar"] = inf["decodificar"].value("modo", std::string());
    else if (const std::string d = texto(inf, {"probador", "decodificar", "modo"}); !d.empty()) r["decodificar"] = d;
    if (inf.contains("bien")) r["bien"] = inf.value("bien", false);
    return r;
}

json comparar(const json& a, const json& b) {
    // Lo que tiene sentido poner lado a lado (lo que no está queda -1 / vacío).
    struct Medida {
        const char* clave;
        std::vector<const char*> camino;
    };
    static const std::vector<Medida> medidas = {
        {"participantes", {"pedidos"}},
        {"conVideo", {"conVideo"}},
        {"fallidos", {"fallidos"}},
        {"cortes", {"cortes"}},
        {"conMargen", {"resultado", "conMargen"}},
        {"maximo", {"resultado", "maximo"}},
        {"cpuAppPico", {"picos", "cpuApp"}},
        {"cpuGatewayPico", {"picos", "cpuGateway"}},
        {"cpuMaquinaPico", {"picos", "cpuMaquina"}},
        {"cpuProbadorProm", {"promedios", "cpuGenerador"}},
        {"cpuProbadorPico", {"picos", "cpuGenerador"}},
        {"subidaPicoKbps", {"picos", "kbpsSubida"}},
        {"bajadaPicoKbps", {"picos", "kbpsBajada"}},
        {"redEntraPicoMbps", {"picos", "redEntraMbps"}},
        {"redSalePicoMbps", {"picos", "redSaleMbps"}},
        {"memLibreMinMb", {"picos", "memLibreMinMb"}},
        {"hdP50Ms", {"hd", "p50"}},
        {"aVideoP95Ms", {"aVideoMs", "p95"}},
    };
    const auto valor = [](const json& j, const std::vector<const char*>& camino) {
        const json* x = &j;
        for (const char* k : camino) {
            if (!x->is_object() || !x->contains(k)) return -1.0;
            x = &(*x)[k];
        }
        return x->is_number() ? x->get<double>() : -1.0;
    };
    json out = json::array();
    const json ra = resumen(a, ""), rb = resumen(b, "");
    for (const char* k : {"tipo", "maquina", "remota", "version", "decodificar"})
        out.push_back({{"clave", k}, {"a", ra.value(k, json())}, {"b", rb.value(k, json())}});
    out.push_back({{"clave", "fecha"}, {"a", ra.value("fecha", 0)}, {"b", rb.value("fecha", 0)}});
    for (const Medida& m : medidas) {
        double va = valor(a, m.camino), vb = valor(b, m.camino);
        if (std::string(m.clave) == "participantes") {
            va = ra.value("participantes", -1);
            vb = rb.value("participantes", -1);
        }
        if (va < 0 && vb < 0) continue;
        out.push_back({{"clave", m.clave}, {"a", va}, {"b", vb}});
    }
    // La CPU de las máquinas: sus modelos (la ficha), para no comparar peras con manzanas.
    out.push_back({{"clave", "cpu"}, {"a", texto(a, {"fichas", "local", "cpu", "modelo"})}, {"b", texto(b, {"fichas", "local", "cpu", "modelo"})}});
    return out;
}

std::string csv(const json& resumenes) {
    static const std::vector<const char*> cols = {"fecha", "tipo", "maquina", "remota", "participantes", "conMargen", "maximo",
                                                  "limita", "decodificar", "version", "frase", "archivo"};
    std::ostringstream o;
    for (std::size_t i = 0; i < cols.size(); ++i) o << (i ? "," : "") << cols[i];
    o << "\n";
    if (!resumenes.is_array()) return o.str();
    for (const json& r : resumenes) {
        for (std::size_t i = 0; i < cols.size(); ++i) o << (i ? "," : "") << csvCampo(r.value(cols[i], json()));
        o << "\n";
    }
    return o.str();
}

json listar(const fs::path& datos, int max) {
    std::vector<json> filas;
    std::set<std::string> vistos;
    for (const char* carpeta : {kCarpeta, kCarpetaVieja}) {
        std::error_code ec;
        for (const auto& e : fs::directory_iterator(datos / carpeta, ec)) {
            const std::string nombre = e.path().filename().string();
            if (!e.is_regular_file(ec) || !nombreValido(nombre) || vistos.count(nombre)) continue;
            std::string s;
            if (!ba::persist::cola().leer(e.path(), s)) continue;
            const json j = json::parse(s, nullptr, false);
            // Sin tipo no es del historial (la prueba de asamblea sin evento deja ahí su copia).
            if (!j.is_object() || !j.contains("tipo")) continue;
            vistos.insert(nombre);
            filas.push_back(resumen(j, nombre));
        }
    }
    std::sort(filas.begin(), filas.end(), [](const json& a, const json& b) { return a.value("fecha", int64_t{0}) > b.value("fecha", int64_t{0}); });
    json out = json::array();
    for (json& f : filas) {
        if (static_cast<int>(out.size()) >= max) break;
        out.push_back(std::move(f));
    }
    return out;
}

json leer(const fs::path& datos, const std::string& archivo) {
    if (!nombreValido(archivo)) return nullptr;
    for (const char* carpeta : {kCarpeta, kCarpetaVieja}) {
        std::string s;
        if (!ba::persist::cola().leer(datos / carpeta / archivo, s)) continue;
        json j = json::parse(s, nullptr, false);
        if (j.is_object()) {
            limpiarPrivado(j);
            return j;
        }
    }
    return nullptr;
}

} // namespace ba::app::sala::historial
