// test_mcp_herramientas.cpp — cada herramienta del registro (docs/mcp.json) por el despachador de verdad
//
// Era una parte de test_mcp.cpp (ver McpPrueba.hpp): los mismos chequeos, en su programa.
#include "RutasPrueba.h"

#include "diag/Registro.hpp"

#include <fstream>
#include <map>
#include <set>

#include "McpPrueba.hpp"

using namespace ba::app::mcp;
using namespace ba::app::mcp::prueba;

namespace {
// --- Lo genérico de cada herramienta del registro (docs/mcp.json) ------------------

/// Un esquema de propiedad bien formado (lo que McpCore::validate sabe leer). "" si está bien.
std::string propiedadMal(const json& p, const std::string& ruta);

std::string objetoMal(const json& s, const std::string& ruta) {
    const json props = s.value("properties", json::object());
    if (!props.is_object()) return ruta + "properties no es un objeto";
    const json req = s.value("required", json::array());
    if (!req.is_array()) return ruta + "required no es una lista";
    for (const auto& r : req) {
        if (!r.is_string()) return ruta + "required con algo que no es un nombre";
        if (!props.contains(r.get<std::string>())) return ruta + "required `" + r.get<std::string>() + "` no está en properties";
    }
    for (auto it = props.begin(); it != props.end(); ++it)
        if (std::string m = propiedadMal(it.value(), ruta + it.key()); !m.empty()) return m;
    return {};
}

std::string propiedadMal(const json& p, const std::string& ruta) {
    static const std::set<std::string> kTipos{"string", "integer", "number", "boolean", "array", "object", "null"};
    if (!p.is_object()) return ruta + ": no es un objeto";
    if (p.contains("type")) {
        const json tipos = p["type"].is_array() ? p["type"] : json::array({p["type"]});
        if (tipos.empty()) return ruta + ": type vacío";
        for (const auto& t : tipos)
            if (!t.is_string() || !kTipos.count(t.get<std::string>())) return ruta + ": type " + t.dump();
    }
    if (p.contains("enum") && (!p["enum"].is_array() || p["enum"].empty())) return ruta + ": enum vacío";
    if (p.contains("minimum") && p.contains("maximum") && p["minimum"].is_number() && p["maximum"].is_number() &&
        p["minimum"].get<double>() > p["maximum"].get<double>())
        return ruta + ": minimum > maximum";
    if (p.contains("items")) {
        if (!p["items"].is_object()) return ruta + ": items no es un objeto";
        if (std::string m = propiedadMal(p["items"], ruta + "[]"); !m.empty()) return m;
    }
    if (p.contains("properties") || p.contains("required")) return objetoMal(p, ruta + ".");
    return {};
}

/// Un valor que el esquema acepta (el primero del enum, el mínimo, lo obligatorio adentro).
json ejemploDe(const json& p) {
    if (p.contains("enum") && p["enum"].is_array() && !p["enum"].empty()) return p["enum"][0];
    std::string tipo = "string";
    if (p.contains("type")) tipo = p["type"].is_array() ? p["type"][0].get<std::string>() : p["type"].get<std::string>();
    if (tipo == "integer" || tipo == "number") {
        double v = p.contains("minimum") ? p["minimum"].get<double>() : 1;
        if (p.contains("maximum") && v > p["maximum"].get<double>()) v = p["maximum"].get<double>();
        return tipo == "integer" ? json(static_cast<long long>(v)) : json(v);
    }
    if (tipo == "boolean") return true;
    if (tipo == "array") return json::array();
    if (tipo == "null") return nullptr;
    if (tipo == "object") {
        json o = json::object();
        const json props = p.value("properties", json::object());
        for (const auto& r : p.value("required", json::array()))
            o[r.get<std::string>()] = ejemploDe(props.value(r.get<std::string>(), json::object()));
        return o;
    }
    return "x";
}

/// Un valor de OTRO tipo que el que pide (para ver que se rechaza).
json otroTipo(const json& p) {
    if (!p.contains("type") || !p["type"].is_string()) return nullptr; // sin tipo (o varios): cualquiera vale
    const std::string t = p["type"].get<std::string>();
    if (t == "string") return 7;
    if (t == "integer" || t == "number" || t == "boolean" || t == "array") return json::object({{"no", "es"}});
    if (t == "object") return "texto";
    return nullptr; // varios tipos o ninguno: no se prueba
}
} // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    std::printf("== test_mcp_herramientas ==\n");

    // --- 10) Cada herramienta del registro (F3-02), por el despachador de verdad --------
    // docs/mcp.json es lo que el código declara (test_mcp_registro lo garantiza): acá cada
    // una pasa por McpCore con su esquema y su clase, con un run que solo cuenta. Lo que se
    // prueba es lo que el despachador hace con lo declarado, para TODAS, no para un ejemplo.
    {
        // Del árbol fuente: $BA_FUENTES o el de la compilación (RutasPrueba.h). Si no está o
        // no se lee, es un FALLO y se sigue con lo demás (antes, `value` sobre un json
        // descartado tiraba una excepción y la prueba se caía: 0xC0000409 en Windows).
        const char* rutaInv = ba_prueba_fuente("docs/mcp.json");
        json inv = json::object();
        if (rutaInv != nullptr) {
            std::ifstream f(rutaInv);
            inv = json::parse(f, nullptr, false);
        }
        const bool leido = inv.is_object() && inv.contains("herramientas") && inv["herramientas"].is_array() &&
                           inv["herramientas"].size() > 500;
        CHECK(leido, "el registro (docs/mcp.json) se lee");
        if (leido) {
            McpCore reg;
            reg.setApprovalPolicy(ApprovalPolicy::Untrusted);
            int sincronizadas = 0;
            reg.setSincronizador([&sincronizadas]() { ++sincronizadas; });
            bool hayAire = false;
            reg.setHayAire([&hayAire]() { return hayAire; });
            std::map<std::string, int> corridas;
            const auto claseDe = [](const std::string& s) {
                for (Clase c : {Clase::Lectura, Clase::Escritura, Clase::Aire, Clase::Ui})
                    if (s == nombreClase(c)) return c;
                return Clase::SinDeclarar;
            };
            const auto reglaDe = [](const std::string& s) {
                for (AireSi a : {AireSi::Siempre, AireSi::AjustesQueCortan, AireSi::RelojNuevo, AireSi::MasterOPrograma,
                                 AireSi::SacarDelAire, AireSi::AlActivar, AireSi::ChatEnFlujo, AireSi::BancaDirecto,
                                 AireSi::CamaraForzada})
                    if (s == McpAire::nombre(a)) return a;
                return AireSi::Siempre;
            };
            int fallas = 0; // se cuentan todas, se muestran las primeras
            const auto fallo = [&fallas](const std::string& n, const std::string& que) {
                if (++fallas <= 25) std::printf("FALLO: %s: %s\n", n.c_str(), que.c_str());
            };
            // Ochocientas llamadas por vuelta: al registro solo lo grave (se restaura al final).
            const ba::diag::Nivel nivelAntes = ba::diag::registro().nivel();
            ba::diag::registro().setNivel(ba::diag::Nivel::Error);
            std::vector<json> todas;
            for (const auto& h : inv.value("herramientas", json::array())) {
                const std::string n = h.value("nombre", std::string());
                if (reg.findTool(n) != nullptr) continue; // las del núcleo (help, help_search): las prueba lo de arriba
                // Lo que el lector no pudo evaluar sin la app (`origen`: sin_leer) no es el esquema de verdad.
                const bool esquemaReal = h.value("origen", json::object()).value("esquema", std::string()) != "sin_leer";
                if (esquemaReal) {
                    const json& s = h["esquema"];
                    if (!s.is_object() || s.value("type", std::string()) != "object")
                        fallo(n, "el esquema no es {type: object}");
                    else if (std::string m = objetoMal(s, ""); !m.empty())
                        fallo(n, "esquema: " + m);
                }
                const Clase c = claseDe(h.value("clase", std::string()));
                if (c == Clase::SinDeclarar) fallo(n, "sin clase");
                if ((c == Clase::Aire) != h.contains("aire")) fallo(n, "`aire` sin clase aire (o al revés)");
                Tool t;
                t.name = n;
                t.category = h.value("categoria", std::string());
                t.inputSchema = h["esquema"];
                t.clase = c;
                t.porSalida = h.value("porSalida", false);
                t.reversible = h.value("reversible", false);
                if (c == Clase::Aire) {
                    t.porQueAire = "registro";
                    t.aireSi = reglaDe(h["aire"].value("cuando", std::string()));
                }
                t.run = [&corridas, n](const json&, Done done) {
                    ++corridas[n];
                    done(ToolResult::ok("ok"));
                };
                reg.addTool(std::move(t));
                if (esquemaReal && n != "security_status") todas.push_back(h); // security_status la resuelve el despacho
            }
            const auto llamar = [&reg](const std::string& n, const json& args, const CallContext& c) {
                json r;
                reg.callTool({{"name", n}, {"arguments", args}}, [&r](json x) { r = std::move(x); }, c);
                return r;
            };
            int conObligatorios = 0, conTipo = 0, lecturas = 0, deAire = 0;
            for (const json& h : todas) {
                const std::string n = h["nombre"];
                const json& s = h["esquema"];
                const json props = s.value("properties", json::object());
                const json req = s.value("required", json::array());
                json valido = json::object();
                for (const auto& r : req) valido[r.get<std::string>()] = ejemploDe(props.value(r.get<std::string>(), json::object()));
                const CallContext limpia{"limpia", "", "asistente"};
                // Sin un obligatorio: no corre, y el error dice cuál.
                if (!req.empty()) {
                    ++conObligatorios;
                    json falta = valido;
                    falta.erase(req[0].get<std::string>());
                    const int antes = corridas[n];
                    const json r = llamar(n, falta, limpia);
                    if (!r.value("isError", false) || corridas[n] != antes || textOf(r).find(req[0].get<std::string>()) == std::string::npos)
                        fallo(n, "sin el obligatorio `" + req[0].get<std::string>() + "` corrió o no lo dijo");
                }
                // Con un tipo equivocado: no corre.
                for (auto it = props.begin(); it != props.end(); ++it) {
                    const json malo = otroTipo(it.value());
                    if (malo.is_null() || it.value().contains("enum")) continue;
                    ++conTipo;
                    json args = valido;
                    args[it.key()] = malo;
                    const int antes = corridas[n];
                    const json r = llamar(n, args, limpia);
                    if (!r.value("isError", false) || corridas[n] != antes) fallo(n, "`" + it.key() + "` con otro tipo corrió");
                    break;
                }
                // Válida y con un argumento que no existe: corre, y si tiene argumentos, se avisa.
                json conDeMas = valido;
                conDeMas["argumento_que_no_existe"] = 1;
                const int antes = corridas[n], sincAntes = sincronizadas;
                const json r = llamar(n, conDeMas, limpia);
                if (r.value("isError", false) || corridas[n] != antes + 1)
                    fallo(n, "con argumentos válidos no corrió: " + textOf(r).substr(0, 160));
                else if (!props.empty() && textOf(r).find("argumento_que_no_existe") == std::string::npos)
                    fallo(n, "un argumento que no existe pasó callado");
                // Las de lectura no cambian la foto: el despacho no persiste ni refresca las vistas
                // tras ellas; todas las demás, sí (setSincronizador).
                const bool lectura = h["clase"] == "lectura";
                lecturas += lectura;
                if (lectura != (sincronizadas == sincAntes))
                    fallo(n, lectura ? "lectura: el despacho sincronizó como si hubiera cambiado algo"
                                     : "cambia algo y el despacho no sincronizó");
                // Al aire, pedida por el asistente (política nunca): solo las de aire «siempre» esperan.
                if (h["clase"] == "aire" && h["aire"].value("cuando", std::string()) == "siempre") {
                    ++deAire;
                    reg.setApprovalPolicy(ApprovalPolicy::Never);
                    hayAire = true;
                    const int a = corridas[n];
                    const json x = llamar(n, valido, limpia);
                    if (corridas[n] != a || !x.value("structuredContent", json::object()).value("necesita_confirmacion", false))
                        fallo(n, "de aire, al aire y sin nadie que confirme: corrió");
                    hayAire = false;
                    reg.setApprovalPolicy(ApprovalPolicy::Untrusted);
                }
            }
            // En una sesión que leyó contenido de invitados, sin a quién preguntar: la lectura
            // corre, lo demás no (la clase declarada decide, no el nombre).
            reg.taint("sucia", "prueba");
            int bloqueadas = 0, corrieron = 0;
            for (const json& h : todas) {
                const std::string n = h["nombre"];
                json valido = json::object();
                const json props = h["esquema"].value("properties", json::object());
                for (const auto& r : h["esquema"].value("required", json::array()))
                    valido[r.get<std::string>()] = ejemploDe(props.value(r.get<std::string>(), json::object()));
                const int a = corridas[n];
                llamar(n, valido, CallContext{"sucia", "", "asistente"});
                const bool corrio = corridas[n] == a + 1;
                (corrio ? corrieron : bloqueadas)++;
                if ((h["clase"] == "lectura") != corrio)
                    fallo(n, corrio ? "en una sesión contaminada corrió sin aprobación" : "lectura bloqueada en una sesión contaminada");
            }
            ba::diag::registro().setNivel(nivelAntes);
            g_failures += fallas;
            std::printf("  registro: %zu herramientas por el despacho (%d con obligatorios, %d con tipo, %d de lectura, "
                        "%d de aire; contaminada: %d corrieron, %d esperaron)%s\n",
                        todas.size(), conObligatorios, conTipo, lecturas, deAire, corrieron, bloqueadas,
                        fallas ? "" : ": OK");
        }
    }

    if (g_failures == 0) {
        std::printf("test_mcp_herramientas: OK\n");
        return 0;
    }
    std::printf("test_mcp_herramientas: %d fallo(s)\n", g_failures);
    return 1;
}
