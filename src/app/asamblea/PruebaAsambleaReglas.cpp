// SPDX-License-Identifier: GPL-2.0-or-later
// asamblea/PruebaAsambleaReglas.cpp — ver el .hpp

#include "asamblea/PruebaAsambleaReglas.hpp"

#include "i18n/I18nTexto.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <set>

namespace ba::app::asamblea::prueba {

using ba::i18n::llenar;
using ba::i18n::traducir;

namespace {

bool esDePrueba(const json& i) { return i.is_object() && i.value("prueba", false) == true; }

std::string minus(std::string s) {
    for (auto& c : s) c = static_cast<char>(::tolower(static_cast<unsigned char>(c)));
    return s;
}

/// «01», «001»…: con tantos dígitos como hagan falta para `n` (y al menos dos).
std::string conCeros(int k, int n) {
    const int digitos = std::max(2, static_cast<int>(std::to_string(std::max(1, n)).size()));
    std::string s = std::to_string(k);
    while (static_cast<int>(s.size()) < digitos) s.insert(s.begin(), '0');
    return s;
}

int pct(const json& j, const char* k, int siNo) {
    if (!j.contains(k)) return siNo;
    const json& v = j[k];
    if (!v.is_number()) return -1;
    const double x = v.get<double>();
    return x < 0 || x > 100 ? -1 : static_cast<int>(std::lround(x));
}

std::string numero(double v) {
    char b[32];
    std::snprintf(b, sizeof b, "%g", v);
    return b;
}

} // namespace

std::string comportamientoDeJson(const json& j, Comportamiento& c) {
    if (j.is_null()) return {};
    if (!j.is_object()) return traducir("asamblea", "el comportamiento va como un objeto");
    struct Campo {
        const char* clave;
        int* destino;
    };
    const Campo campos[] = {{"afuera_pct", &c.afueraPct},       {"piden_palabra_pct", &c.pidenPalabraPct},
                            {"votan_pct", &c.votanPct},         {"reconectan_pct", &c.reconectanPct},
                            {"pin_errado_pct", &c.pinErradoPct}, {"con_usuario_pct", &c.conUsuarioPct}};
    for (const Campo& f : campos) {
        const int v = pct(j, f.clave, *f.destino);
        if (v < 0) return llenar(traducir("asamblea", "%1: un porcentaje de 0 a 100"), {f.clave});
        *f.destino = v;
    }
    if (j.contains("pin_errado_intentos")) {
        const int n = j["pin_errado_intentos"].is_number() ? j["pin_errado_intentos"].get<int>() : -1;
        if (n < 1 || n > 20) return traducir("asamblea", "pin_errado_intentos: de 1 a 20 (con 5, el gateway lo bloquea)");
        c.pinErradoIntentos = n;
    }
    if (j.contains("entrada_cada_ms")) {
        const int n = j["entrada_cada_ms"].is_number() ? j["entrada_cada_ms"].get<int>() : -1;
        if (n < 0 || n > 60000) return traducir("asamblea", "entrada_cada_ms: de 0 a 60000");
        c.entradaCadaMs = n;
    }
    if (j.contains("habilitar_solos_s")) {
        const int n = j["habilitar_solos_s"].is_number() ? j["habilitar_solos_s"].get<int>() : -1;
        if (n < 0 || n > 3600) return traducir("asamblea", "habilitar_solos_s: de 0 (el operador) a 3600");
        c.habilitarSolosS = n;
    }
    if (j.contains("programa")) {
        if (!j["programa"].is_boolean()) return traducir("asamblea", "programa: sí o no");
        c.programa = j["programa"].get<bool>();
    }
    if (j.contains("voto")) {
        // «azar» o «opcion:proporcion,…», o un objeto {opcion: proporción}.
        std::string v;
        if (j["voto"].is_string()) {
            v = j["voto"].get<std::string>();
        } else if (j["voto"].is_object()) {
            for (auto it = j["voto"].begin(); it != j["voto"].end(); ++it) {
                if (!it.value().is_number() || it.value().get<double>() < 0)
                    return traducir("asamblea", "voto: cada opción con una proporción positiva");
                v += (v.empty() ? "" : ",") + it.key() + ":" + numero(it.value().get<double>());
            }
        } else {
            return traducir("asamblea", "voto: «azar» o las proporciones (afirmativo:60,negativo:30,abstencion:10)");
        }
        if (v.empty()) v = "azar";
        if (v != "azar") {
            double suma = 0;
            std::size_t p = 0;
            while (p < v.size()) {
                std::size_t fin = v.find(',', p);
                if (fin == std::string::npos) fin = v.size();
                const std::string par = v.substr(p, fin - p);
                const std::size_t dos = par.find(':');
                char* resto = nullptr;
                const double x = dos == std::string::npos ? -1 : std::strtod(par.c_str() + dos + 1, &resto);
                if (dos == std::string::npos || dos == 0 || x < 0 || (resto && *resto))
                    return traducir("asamblea", "voto: «azar» o las proporciones (afirmativo:60,negativo:30,abstencion:10)");
                suma += x;
                p = fin + 1;
            }
            if (suma <= 0) return traducir("asamblea", "voto: alguna opción con proporción mayor que cero");
        }
        c.voto = v;
    }
    return {};
}

json aJson(const Comportamiento& c) {
    return {{"afuera_pct", c.afueraPct},
            {"piden_palabra_pct", c.pidenPalabraPct},
            {"votan_pct", c.votanPct},
            {"voto", c.voto},
            {"reconectan_pct", c.reconectanPct},
            {"pin_errado_pct", c.pinErradoPct},
            {"pin_errado_intentos", c.pinErradoIntentos},
            {"con_usuario_pct", c.conUsuarioPct},
            {"entrada_cada_ms", c.entradaCadaMs},
            {"habilitar_solos_s", c.habilitarSolosS},
            {"programa", c.programa}};
}

int integrantesReales(const json& integrantes) {
    int n = 0;
    for (const json& i : integrantes.is_array() ? integrantes : json::array()) n += esDePrueba(i) ? 0 : 1;
    return n;
}

std::vector<std::string> aBorrar(const json& integrantes) {
    std::vector<std::string> ids;
    for (const json& i : integrantes.is_array() ? integrantes : json::array())
        if (esDePrueba(i) && !i.value("id", std::string()).empty()) ids.push_back(i.value("id", std::string()));
    return ids;
}

std::string puedeGenerar(const json& integrantes, int n) {
    if (n < 1) return traducir("asamblea", "la prueba necesita al menos un integrante");
    if (n > kMaxIntegrantes)
        return llenar(traducir("asamblea", "la prueba admite hasta %1 integrantes simulados"), {std::to_string(kMaxIntegrantes)});
    const int reales = integrantesReales(integrantes);
    if (reales > 0)
        return llenar(traducir("asamblea", "el padrón de este evento tiene %1 integrantes reales: la prueba de asamblea "
                                           "corre en un evento de prueba, para no tocar el padrón, el quórum ni el acta "
                                           "(creá uno vacío y abrilo)"),
                      {std::to_string(reales)});
    if (!aBorrar(integrantes).empty())
        return traducir("asamblea", "quedan integrantes de una prueba anterior: terminala (o limpiala) antes de empezar otra");
    return {};
}

json integrantesNuevos(const json& integrantes, int n) {
    std::set<std::string> ids, bancas, nombres;
    for (const json& i : integrantes.is_array() ? integrantes : json::array()) {
        ids.insert(minus(i.value("id", std::string())));
        bancas.insert(minus(i.value("banca", std::string())));
        nombres.insert(minus(i.value("nombre", std::string())));
    }
    json out = json::array();
    int k = 1;
    while (static_cast<int>(out.size()) < n && k < 100000) {
        const std::string num = conCeros(k++, n);
        const std::string id = "prueba-" + num, banca = "P" + num,
                          nombre = llenar(traducir("asamblea", "Prueba %1"), {num});
        if (ids.count(minus(id)) || bancas.count(minus(banca)) || nombres.count(minus(nombre))) continue;
        out.push_back({{"id", id},
                       {"nombre", nombre},
                       {"banca", banca},
                       {"cargo_oficial", traducir("asamblea", "Integrante de prueba")},
                       {"bloque", traducir("asamblea", "Prueba")},
                       {"prueba", true}});
    }
    return out;
}

json recintoPara(int n) {
    n = std::max(1, n);
    // Más ancha que alta (como un recinto visto desde la cabina), sin pasarse de la grilla.
    int columnas = std::clamp(static_cast<int>(std::ceil(std::sqrt(n * 2.0))), 4, 60);
    int filas = (n + columnas - 1) / columnas;
    if (filas > 40) {
        filas = 40;
        columnas = std::min(60, (n + filas - 1) / filas);
    }
    return {{"filas", std::max(1, filas)}, {"columnas", columnas}, {"auto", true}};
}

std::string usuarioDe(const std::string& banca) { return "prueba" + minus(banca.size() > 1 ? banca.substr(1) : banca); }

json archivoHinchada(const json& accesos) {
    json lista = json::array();
    for (const json& a : accesos.is_array() ? accesos : json::array()) {
        if (!a.is_object() || a.value("link", std::string()).empty()) continue;
        json x{{"id", a.value("id", std::string())},
               {"nombre", a.value("nombre", std::string())},
               {"banca", a.value("banca", std::string())},
               {"link", a.value("link", std::string())},
               {"pin", a.value("pin", std::string())}};
        if (!a.value("usuario", std::string()).empty() && !a.value("clave", std::string()).empty()) {
            x["usuario"] = a["usuario"];
            x["clave"] = a["clave"];
        }
        lista.push_back(x);
    }
    return {{"version", 1},
            {"aviso", "Credenciales de PRUEBA de la sesión remota: valen solo mientras dura la prueba (la app las revoca "
                      "al terminar). No las compartas."},
            {"integrantes", lista}};
}

namespace {
std::vector<std::string> guion(const Comportamiento& c) {
    return {"--afuera",        std::to_string(c.afueraPct),        "--piden-palabra", std::to_string(c.pidenPalabraPct),
            "--votan",         std::to_string(c.votanPct),         "--voto",          c.voto.empty() ? "azar" : c.voto,
            "--reconectan",    std::to_string(c.reconectanPct),    "--pin-errado",    std::to_string(c.pinErradoPct),
            "--pin-errado-intentos", std::to_string(c.pinErradoIntentos), "--con-usuario", std::to_string(c.conUsuarioPct),
            "--programa",      c.programa ? "si" : "no",           "--entrada-cada",  std::to_string(c.entradaCadaMs) + "ms"};
}
} // namespace

std::vector<std::string> argumentosHinchada(const Comportamiento& c, const std::string& url, const std::string& archivo,
                                            int minutos, const std::string& salida, const std::string& grabado) {
    std::vector<std::string> a = {"--servidor", "integrantes", "--url", url, "--inseguro", "--integrantes", archivo,
                                  // Nadie codifica: el que tiene la palabra manda en loop su video con su número.
                                  "--modo", "grabado"};
    if (!grabado.empty()) a.insert(a.end(), {"--grabado", grabado});
    const auto g = guion(c);
    a.insert(a.end(), g.begin(), g.end());
    a.insert(a.end(), {"--durante", std::to_string(std::max(1, minutos)) + "m"});
    if (!salida.empty()) a.insert(a.end(), {"--salida", salida});
    a.insert(a.end(), {"--vigilar", "ba-gateway,ba-app"});
    return a;
}

std::string comandoExterno(const Comportamiento& c, const std::string& url, int minutos) {
    std::string s = "hinchada --servidor integrantes --url " + url + " --inseguro --integrantes integrantes.json --modo grabado";
    for (const auto& x : guion(c)) s += " " + x;
    return s + " --durante " + std::to_string(std::max(1, minutos)) + "m";
}

json vivo(const json& participantes, const json& votacion, const std::vector<std::string>& ids, int pedidos) {
    const std::set<std::string> mios(ids.begin(), ids.end());
    int conectados = 0, esperando = 0, habilitados = 0, afuera = 0, bloqueados = 0, enFila = 0, conPalabra = 0;
    for (const json& p : participantes.is_array() ? participantes : json::array()) {
        if (!mios.count(p.value("id", std::string()))) continue;
        const bool con = p.value("conectado", false);
        conectados += con;
        const std::string e = p.value("estado", std::string());
        esperando += e == "esperando";
        habilitados += e == "habilitado";
        afuera += con && p.value("lugar", std::string()) == "afuera";
        bloqueados += p.value("bloqueado", false);
        enFila += p.value("turno", 0) > 0;
        conPalabra += p.value("palabra", false);
    }
    int votaron = 0;
    json porOpcion = json::object();
    for (const json& v : votacion.value("votos", json::array()))
        if (mios.count(v.value("id", std::string()))) {
            ++votaron;
            const std::string o = v.value("opcion", std::string());
            porOpcion[o] = porOpcion.value(o, 0) + 1;
        }
    return {{"pedidos", pedidos},
            {"conectados", conectados},
            {"esperando", esperando},
            {"habilitados", habilitados},
            {"afuera", afuera},
            {"bloqueados", bloqueados},
            {"enLaFila", enFila},
            {"conPalabra", conPalabra},
            {"votaron", votaron},
            {"porOpcion", porOpcion},
            {"votacion", {{"estado", votacion.value("estado", std::string("sin"))}, {"numero", votacion.value("numero", 0)}}}};
}

std::string nota(const json& v, const json& h) {
    const json a = h.is_object() ? h.value("asamblea", json::object()) : json::object();
    std::string s = llenar(traducir("asamblea", "%1 integrantes de prueba: entraron %2, habilitados %3, pidieron la palabra %4, "
                                                "votaron %5, bloqueados %6"),
                           {std::to_string(v.value("pedidos", 0)),
                            std::to_string(a.value("entraron", v.value("conectados", 0))),
                            std::to_string(a.value("habilitados", v.value("habilitados", 0))),
                            std::to_string(a.value("pidieronPalabra", v.value("enLaFila", 0))),
                            std::to_string(a.value("votaron", v.value("votaron", 0))),
                            std::to_string(a.value("bloqueados", v.value("bloqueados", 0)))});
    const json lat = a.value("latencias", json::object());
    const json espera = lat.value("esperaHabilitacionS", json::object());
    if (espera.value("n", 0) > 0)
        s += llenar(traducir("asamblea", "; espera hasta que los habilitan p50 %1 s"), {numero(espera.value("p50", 0.0))});
    const json voto = lat.value("votoConfirmadoMs", json::object());
    if (voto.value("n", 0) > 0) s += llenar(traducir("asamblea", "; voto confirmado p50 %1 ms"), {numero(voto.value("p50", 0.0))});
    return s;
}

} // namespace ba::app::asamblea::prueba
