// ============================================================================
// src/app/tiempos/Tiempos.cpp — el grafo de demoras (ver Tiempos.hpp)
// ============================================================================
#include "tiempos/Tiempos.hpp"

#include "i18n/I18n.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <set>

namespace ba::app::tiempos {

using json = nlohmann::json;
using ba::i18n::llenar;
using ba::i18n::traducir;

namespace {

double redondo(double ms) { return std::round(ms * 10.0) / 10.0; }
/// Más que esto (para un lado u otro) no es una demora: la fuente trae la hora de otro reloj.
constexpr double kHoraCorridaMs = 60000;

/// Los filtros que no son tarjetas del grafo (los agregan ControlCamaras, SuperRes…):
/// un nombre que se entienda en vez del elemento de GStreamer.
/// La edad medida a la salida de un pipeline (MedidorEdad: {p50_ms, p95_ms}); -1 si no hay.
double edadMedida(const json& st, const char* p = "p50_ms") {
    const json e = st.is_object() ? st.value("edad_salida", json()) : json();
    return e.is_object() && e.contains(p) && e[p].is_number() ? e[p].get<double>() : -1;
}

/// Un nodo de la composición o la mezcla: lo medido manda; lo fijado queda para comparar.
/// `ms` = lo que espera ahí (el colchón: sale junto con las demás entradas, a su hora +
/// la latencia fijada), acumulado = la edad con que sale.
void ponerTramoDeEspera(json& n, double previo, double fijada, double medida, double medidaP95, double cuadroMs) {
    const double acum = medida >= 0 ? medida : std::max(fijada, previo);
    n["ms"] = std::round(std::max(0.0, acum - previo) * 10) / 10;
    n["acumulado_ms"] = std::round(acum * 10) / 10;
    n["fuente"] = medida >= 0 ? "medido" : "declarado";
    n["estado"] = "bien";
    n["detalle"]["que_es"] = traducir("tiempos", "espera en el colchón: cada cuadro sale a su hora de captura + la latencia "
                                                 "fijada, para que todas las entradas salgan juntas (por eso es más que lo que "
                                                 "tarda cada tarjeta)");
    n["detalle"]["espera_ms"] = std::round(std::max(0.0, acum - previo) * 10) / 10;
    if (medida >= 0) {
        n["detalle"]["edad_medida_ms"] = std::round(medida * 10) / 10;
        if (medidaP95 >= 0) n["p95_ms"] = std::round(medidaP95 * 10) / 10;
        const double dif = medida - fijada;
        n["detalle"]["contra_lo_fijado_ms"] = std::round(dif * 10) / 10;
        // Sale más tarde que lo fijado (más de un cuadro): algo de adentro no da abasto.
        if (fijada > 0 && dif > std::max(cuadroMs, 10.0)) {
            n["estado"] = "justo";
            n["detalle"]["aviso"] =
                llenar(traducir("tiempos", "sale %1 ms más tarde que lo fijado"), {std::to_string(static_cast<int>(dif))});
        }
    }
}

std::string tituloDeElemento(const std::string& e) {
    // Se arma en cada llamada: así sale en el idioma de ahora.
    const std::map<std::string, std::string> nombres{
        {"baptz", traducir("tiempos", "PTZ digital")},
        {"baptzgl", traducir("tiempos", "PTZ digital")},
        {"bamejora", traducir("tiempos", "Mejorar imagen")},
        {"basuperres", traducir("tiempos", "Superresolución")},
        {"baonnxsr", traducir("tiempos", "Superresolución")},
        {"baplugin", traducir("tiempos", "Plugin")},
        {"barostros", traducir("tiempos", "Rostros")},
        {"barack", traducir("tiempos", "Rack de audio")},
        {"balimitador", traducir("tiempos", "Limitador")}};
    const auto it = nombres.find(e);
    return it != nombres.end() ? it->second : e;
}

/// Un sí/no de una foto, venga como venga (bool, número o texto): las fotos las arma cada
/// parte y una que cambie el tipo de un campo no puede tumbar la página.
bool si(const json& o, const char* k) {
    if (!o.is_object() || !o.contains(k)) return false;
    const json& v = o[k];
    return v.is_boolean() ? v.get<bool>() : v.is_number() ? v.get<double>() != 0 : v.is_string() ? !v.get<std::string>().empty() : false;
}
/// Lo mismo para un número (texto que no es número: 0).
std::string fabricaDe(const json& el) { return el.value("fabrica", el.value("nombre", std::string())); }
/// Sube o baja de la GPU, o cambia el formato o el tamaño: lo mete la cadena para que lo que
/// sigue reciba lo que acepta.
bool esConversor(const json& el) {
    const std::string f = fabricaDe(el);
    for (const char* x : {"upload", "download", "convert", "scale"})
        if (f.find(x) != std::string::npos) return true;
    return false;
}
/// Colas y capsfilter: no hacen nada con el cuadro (las colas, si retienen, se ven igual).
bool esDePaso(const json& el) {
    const std::string f = fabricaDe(el);
    return f.find("capsfilter") != std::string::npos || f.find("queue") != std::string::npos;
}

/// Los elementos medidos de cada tarjeta (por su instancia en el pipeline): los suyos (los
/// que empiezan con su nombre: algunas arman más de uno, como el PTZ con su tee) y los
/// conversores que la cadena metió justo antes —subir o bajar de la GPU, cambiar el
/// formato—: los pide ella. Las colas y los capsfilter del medio no cortan; otro elemento sí.
std::map<std::string, json> elementosPorTarjeta(const json& analisis, const std::vector<std::string>& instancias) {
    std::map<std::string, json> r;
    json pendientes = json::array();
    for (const auto& el : analisis.value("elementos", json::array())) {
        const std::string nombre = el.value("nombre", std::string());
        std::string suya;
        for (const auto& i : instancias)
            if (nombre.rfind(i, 0) == 0 && i.size() > suya.size()) suya = i;
        if (!suya.empty()) {
            json& v = r[suya];
            if (!v.is_array()) v = json::array();
            for (auto& x : pendientes) v.push_back(std::move(x));
            pendientes = json::array();
            v.push_back(el);
            continue;
        }
        if (esConversor(el)) pendientes.push_back(el);
        else if (!esDePaso(el)) pendientes = json::array();
    }
    return r;
}

double num(const json& o, const char* k, double defecto = 0) {
    if (!o.is_object() || !o.contains(k)) return defecto;
    const json& v = o[k];
    if (v.is_number()) return v.get<double>();
    if (v.is_string()) try { return std::stod(v.get<std::string>()); } catch (...) {}
    return defecto;
}

/// bien | justo | tarde según lo que tarda contra la latencia del estudio (lo que espera la mezcla).
std::string estadoDe(double ms, double latenciaEstudio, bool llegaTarde) {
    if (llegaTarde) return "tarde";
    if (latenciaEstudio <= 0) return "bien";
    if (ms > latenciaEstudio) return "tarde";
    if (ms > 0.8 * latenciaEstudio) return "justo";
    return "bien";
}

struct Armado {
    json nodos = json::array();
    json tramos = json::array();
    std::map<std::string, double> acumulado; ///< id → ms desde la captura (al salir del nodo)
    std::map<std::string, std::vector<std::string>> camino; ///< id → los nodos por los que vino (el más lento)

    json& nodo(const std::string& id, const std::string& tipo, const std::string& titulo, int columna,
               const std::string& medio, bool activo = true) {
        nodos.push_back({{"id", id}, {"tipo", tipo}, {"titulo", titulo}, {"columna", columna}, {"medio", medio}, {"activo", activo},
                         {"ms", nullptr}, {"p95_ms", nullptr}, {"acumulado_ms", nullptr}, {"fuente", "sin_medir"},
                         {"estado", "sin_datos"}, {"detalle", json::object()}});
        return nodos.back();
    }
    void tramo(const std::string& desde, const std::string& hacia, const std::string& medio) {
        tramos.push_back({{"desde", desde}, {"hacia", hacia}, {"medio", medio},
                          {"ms", acumulado.count(desde) ? json(redondo(acumulado[desde])) : json()}});
    }
    /// El nodo sigue al más lento de los que le llegan (el camino que manda).
    void seguir(const std::string& id, const std::vector<std::string>& desde) {
        double max = -1;
        std::string peor;
        for (const auto& d : desde)
            if (acumulado.count(d) && acumulado[d] > max) {
                max = acumulado[d];
                peor = d;
            }
        if (!peor.empty()) {
            camino[id] = camino[peor];
            camino[id].push_back(id);
        } else {
            camino[id] = {id};
        }
    }
};

/// Un paso de la cadena de una entrada (tarjeta o módulo de audio) con lo que tarda.
/// La entrada se queda con lo que no es de ellos (captura, decodificación, colas).
struct Paso {
    std::string id, tipo, titulo;
    double ms = -1; ///< < 0: sin medir
    json detalle = json::object();
};

} // namespace

json armar(const Fotos& f) {
    Armado a;
    const json& m = f.motor;
    const json estudio = m.value("estudio", json::object());
    const double latEstudio = num(estudio, "latencia_ms", 0.0);
    const json entradas = m.value("entradas", json::object());
    const json video = m.value("video", json::object());
    const json programaV = video.value("programa", json::object());
    const json programaA = m.value("programa", json::object());

    // --- Entradas, con sus tarjetas (video) y sus módulos (audio) -------------------------
    std::set<std::string> conVideo, conAudio;
    for (const auto& [id, e] : entradas.items()) {
        if (!e.is_object()) continue;
        const bool daV = si(e, "video"), daA = si(e, "audio");
        if (!daV && !daA) continue;
        const json ev = e.value("edad_video", json()), ea = e.value("edad_audio", json());
        // Activa: le están llegando datos (las que están en reposo —un invitado sin conectar,
        // una placa que no se usa— la página las esconde salvo que se pidan).
        const double sinDatos = num(e, "sin_datos_s", -1);
        const auto espera = f.enEspera.find(id);
        const bool activa = sinDatos >= 0 && sinDatos < 2.0 && espera == f.enEspera.end();
        const json filtros = e.value("filtros", json::array());
        // El análisis por elemento de esta entrada (si se hizo): lo que agrega cada paso.
        const auto pl = f.pipelines.find(id);
        const json* analisis = pl != f.pipelines.end() ? &pl->second : nullptr;
        std::map<std::string, json> deTarjeta;
        if (analisis != nullptr) {
            std::vector<std::string> inst;
            for (const auto& fl : e.value("filtros", json::array()))
                if (!fl.value("instancia", std::string()).empty()) inst.push_back(fl["instancia"].get<std::string>());
            deTarjeta = elementosPorTarjeta(*analisis, inst);
        }
        json diagnostico;
        if (analisis != nullptr) {
            std::map<std::string, TarjetaEnPipeline> tj;
            for (const auto& fl : filtros) {
                const std::string i = fl.value("instancia", std::string());
                if (i.empty()) continue;
                const auto t = f.titulos.find(fl.value("id", std::string()));
                tj[i] = {"tarjeta:" + id + ":" + fl.value("id", std::string()),
                         t != f.titulos.end() && !t->second.empty() ? t->second : tituloDeElemento(fl.value("elemento", std::string()))};
            }
            diagnostico = diagnosticar(*analisis, tj);
        }

        for (const bool esAudio : {false, true}) {
            if ((esAudio && !daA) || (!esAudio && !daV)) continue;
            const json edad = esAudio ? ea : ev;
            const std::string medio = esAudio ? "audio" : "video";
            const std::string idE = "entrada:" + id + (esAudio ? ":audio" : "");
            // Los pasos de esta rama.
            std::vector<Paso> pasos;
            std::vector<Paso> ramas; ///< lo que escucha una copia (voz a texto, traducir…): no atrasa
            for (const auto& fl : filtros) {
                if (!fl.is_object() || si(fl, "audio") != esAudio) continue;
                Paso p;
                p.id = "tarjeta:" + id + ":" + fl.value("id", std::string());
                p.tipo = "tarjeta";
                const auto t = f.titulos.find(fl.value("id", std::string()));
                p.titulo = t != f.titulos.end() && !t->second.empty() ? t->second : tituloDeElemento(fl.value("elemento", std::string()));
                if (fl.contains("ms") && fl["ms"].is_number()) p.ms = fl["ms"].get<double>();
                p.detalle = {{"elemento", fl.value("elemento", std::string())},
                             {"andando", !fl.contains("andando") || si(fl, "andando")},
                             {"caps", fl.value("caps", json())}};
                if (fl.contains("info") && fl["info"].is_object())
                    for (const char* k : {"motor-usado", "proveedor-usado", "error"})
                        if (fl["info"].contains(k)) p.detalle[k] = fl["info"][k];
                // Lo medido por las sondas (ver elementosPorTarjeta): cuenta lo que tarda Y lo
                // que retiene; manda sobre el ms-por-cuadro que informe.
                const std::string inst = fl.value("instancia", std::string());
                if (!inst.empty()) p.detalle["instancia"] = inst;
                if (const auto dt = deTarjeta.find(inst); !inst.empty() && dt != deTarjeta.end()) {
                    double suma = 0;
                    json cruces = json::array();
                    for (const auto& el : dt->second) {
                        suma += el.value("agrega_ms", 0.0);
                        if (el.contains("cruce")) cruces.push_back(el["cruce"]);
                    }
                    if (p.ms >= 0) p.detalle["ms_por_cuadro"] = p.ms;
                    p.ms = suma;
                    p.detalle["medido_sondas"] = true;
                    p.detalle["elementos"] = dt->second;
                    // Con qué memoria entra y sale (la primera y la última que se conocen: una
                    // rama que no sigue, como la del tee del PTZ, no tiene salida).
                    std::string memE, memS;
                    for (const auto& el : dt->second) {
                        if (memE.empty()) memE = el.value("memoria_entrada", std::string());
                        if (!el.value("memoria_salida", std::string()).empty()) memS = el["memoria_salida"].get<std::string>();
                    }
                    p.detalle["memoria_entrada"] = memE;
                    p.detalle["memoria_salida"] = memS;
                    if (!cruces.empty()) {
                        p.detalle["cruces"] = cruces;
                        p.detalle["cruce"] = cruces.back();
                    }
                }
                pasos.push_back(std::move(p));
            }
            if (esAudio)
                if (const auto r = f.racks.find(id); r != f.racks.end()) {
                    const double sr = std::max(1.0, r->second.value("frecuenciaMuestreo", 48000.0));
                    for (const auto& n : r->second.value("nodos", json::array())) {
                        const std::string tn = n.value("tipo", std::string());
                        if (tn == "texto" || tn == "habla" || tn == "traducir") {
                            Paso p;
                            p.id = "modulo:" + id + ":" + n.value("id", std::string());
                            p.tipo = "derivacion";
                            p.titulo = !n.value("etiqueta", std::string()).empty() ? n.value("etiqueta", std::string())
                                       : tn == "texto" ? traducir("tiempos", "Voz a texto")
                                       : tn == "habla" ? traducir("tiempos", "Texto a voz")
                                                       : traducir("tiempos", "Traducción");
                            p.detalle = {{"que", traducir("tiempos", "escucha una copia del audio: no atrasa lo que sale")},
                                         {"tipo", tn}};
                            for (const char* k : {"flujo", "modo", "nombre"})
                                if (n.contains(k)) p.detalle[k] = n[k];
                            ramas.push_back(std::move(p));
                            continue;
                        }
                        if (tn != "plugin") continue;
                        Paso p;
                        p.id = "modulo:" + id + ":" + n.value("id", std::string());
                        p.tipo = "modulo";
                        p.titulo = !n.value("etiqueta", std::string()).empty() ? n.value("etiqueta", std::string())
                                                                               : n.value("nombre", n.value("modulo", std::string()));
                        // Lo que tarda en procesar un bloque, más la latencia que declara (lookahead).
                        const double lat = num(n, "latencia_muestras", 0.0) * 1000.0 / sr;
                        p.ms = num(n, "ms_medio", 0.0) + lat;
                        p.detalle = {{"modulo", n.value("modulo", std::string())},
                                     {"ms_proceso", num(n, "ms_medio", 0.0)},
                                     {"ms_max", num(n, "ms_max", 0.0)},
                                     {"ms_bloque", num(n, "ms_bloque", 0.0)},
                                     {"latencia_ms", redondo(lat)},
                                     {"bypass", si(n, "bypass")}};
                        pasos.push_back(std::move(p));
                    }
                }
            double suma = 0;
            for (const auto& p : pasos)
                if (p.ms > 0) suma += p.ms;

            json& ne = a.nodo(idE, "entrada", e.value("nombre", id), 0, medio, activa);
            ne["detalle"] = {{"entrada", id},
                             {"declarada_ms", num(e, "declarada_ms", 0.0)},
                             {"pide_ms", num(e, "latencia_ms", 0.0)},
                             {"llega_tarde", si(e, "llega_tarde")},
                             {"con_su_hora", si(e, "ntp_sync")},
                             {"origen", e.value("origen", json())},
                             {"sale", e.value("sale", json())},
                             {"sin_datos_s", num(e, "sin_datos_s", -1.0)},
                             {"rearmada_hace_s", num(e, "rearmada_hace_s", -1.0)}};
            // El recorrido medido de la entrada (todos sus elementos, la memoria de cada paso
            // y los cruces GPU↔memoria): lo que muestra la tarjeta al tocarla.
            if (analisis != nullptr) {
                ne["detalle"]["pipeline"] = *analisis;
                ne["detalle"]["diagnostico"] = diagnostico;
            }
            if (espera != f.enEspera.end()) ne["detalle"]["en_espera"] = espera->second;
            // La hora que trae, corrida (más de un minuto para un lado u otro): no es una demora,
            // es una fuente con otro reloj. Se marca y no se dibuja el número (estiraría todo).
            const double edadCruda = edad.is_object() ? num(edad, "p50_ms", 0.0) : 0.0;
            const bool corrida = edad.is_object() && std::fabs(edadCruda) > kHoraCorridaMs;
            const bool medida = edad.is_object() && !corrida;
            // El desfase (a mano, sincronizar, el retardo del audio) corre la HORA del cuadro: la edad
            // medida queda corrida eso (con +200, −196 ms). Lo que tarda de verdad es edad + desfase;
            // y queda listo para mostrarse recién a su hora (la escena lo espera): el mayor de los dos.
            // + el retraso propio (una entrada con su propia demora: su hora va corrida eso).
            const double desfase = num(e, "desfase_ms", 0.0) + num(e, esAudio ? "desfase_audio_ms" : "desfase_video_ms", 0.0) +
                                   num(e, "retraso_propio_ms", 0.0);
            const double total = medida ? num(edad, "p50_ms", 0.0) + desfase : -1;
            if (medida) {
                // La entrada: lo que no es de sus pasos (captura, decodificación, colas).
                const double propio = std::max(0.0, total - suma);
                ne["ms"] = redondo(propio);
                ne["p95_ms"] = redondo(num(edad, "p95_ms", 0.0) + desfase);
                ne["fuente"] = "medido";
                ne["estado"] = estadoDe(num(edad, "p95_ms", 0.0) + desfase, latEstudio, si(e, "llega_tarde"));
                ne["detalle"]["edad_ms"] = redondo(total);
                if (desfase != 0) ne["detalle"]["desfase_ms"] = redondo(desfase);
                a.acumulado[idE] = propio;
                ne["acumulado_ms"] = redondo(propio);
            }
            if (corrida) {
                ne["fuente"] = "hora_corrida";
                ne["estado"] = "tarde";
                ne["detalle"]["edad_ms"] = redondo(edadCruda);
                ne["detalle"]["aviso"] = llenar(traducir("tiempos", "la hora que trae está corrida %1 s del reloj del estudio: "
                                                                    "no es una demora, es otro reloj"),
                                                {std::to_string(static_cast<long long>(edadCruda / 1000))});
            }
            a.seguir(idE, {});
            std::string previo = idE;
            for (const auto& p : pasos) {
                json& np = a.nodo(p.id, p.tipo, p.titulo, 1, medio, activa);
                np["detalle"] = p.detalle;
                if (p.ms >= 0) {
                    np["ms"] = redondo(p.ms);
                    np["fuente"] = "medido";
                    np["estado"] = "bien";
                }
                a.tramo(previo, p.id, medio);
                if (a.acumulado.count(previo)) a.acumulado[p.id] = a.acumulado[previo] + std::max(0.0, p.ms);
                if (a.acumulado.count(p.id)) np["acumulado_ms"] = redondo(a.acumulado[p.id]);
                a.seguir(p.id, {previo});
                previo = p.id;
            }
            // Lo medido al salir de la entrada manda (los pasos sin medir quedan adentro); con
            // desfase, la escena la tiene recién a su hora.
            if (medida) a.acumulado[previo] = std::max(total, desfase);
            // Las ramas (escuchan una copia): colgadas de la entrada, sin sumar al camino.
            for (const auto& p : ramas) {
                json& nr = a.nodo(p.id, p.tipo, p.titulo, 1, medio, activa);
                nr["detalle"] = p.detalle;
                nr["fuente"] = "derivacion";
                nr["estado"] = "bien";
                a.tramo(idE, p.id, medio);
            }
            (esAudio ? conAudio : conVideo).insert(previo + "\x1f" + id);
        }
    }
    // El último nodo de cada rama, por entrada.
    std::map<std::string, std::string> finV, finA;
    for (const auto& s : conVideo) finV[s.substr(s.find('\x1f') + 1)] = s.substr(0, s.find('\x1f'));
    for (const auto& s : conAudio) finA[s.substr(s.find('\x1f') + 1)] = s.substr(0, s.find('\x1f'));

    // --- Escena al aire (la composición del programa) y la mezcla ------------------------
    const double cuadroMs = 1000.0 / std::max(1.0, num(f.motor.value("estudio", json::object()), "fps", 30.0));
    std::vector<std::string> fuentesV, fuentesA;
    if (programaV.is_object() && !programaV.empty()) {
        const std::string id = "escena:programa";
        const std::string esc = video.value("al_aire", std::string());
        json& n = a.nodo(id, "escena", esc.empty() ? traducir("tiempos", "Programa") : esc, 2, "video");
        const double lat = num(programaV, "latencia_salida_ms", num(programaV, "latencia_ms", 0.0));
        n["detalle"] = {{"escena", esc}, {"latencia_fijada_ms", lat}, {"gpu", programaV.value("gpu", json())},
                        {"en_transicion", si(programaV, "en_transicion")}};
        std::vector<std::string> desde;
        // Con qué edad llega cada entrada a la composición (medido en su intersrc): lo que
        // esperó desde que salió de la entrada es su colchón, y va en el tramo.
        double previo = 0;
        json llegan = json::array();
        for (const auto& c : programaV.value("capas", json::array())) {
            const std::string e = c.value("entrada", std::string());
            const auto it = finV.find(e);
            if (it == finV.end() || std::find(desde.begin(), desde.end(), it->second) != desde.end()) continue;
            a.tramo(it->second, id, "video");
            desde.push_back(it->second);
            const double sale = a.acumulado.count(it->second) ? a.acumulado[it->second] : -1;
            const json el = c.value("edad_llega", json());
            const double llega = el.is_object() ? num(el, "p50_ms", -1.0) : -1.0;
            if (llega >= 0 && sale >= 0) {
                json& t = a.tramos.back();
                t["llega_ms"] = redondo(llega);
                t["espera_ms"] = redondo(std::max(0.0, llega - sale));
                llegan.push_back({{"entrada", e}, {"sale_ms", redondo(sale)}, {"llega_ms", redondo(llega)},
                                  {"espera_ms", redondo(std::max(0.0, llega - sale))}});
                previo = std::max(previo, llega);
            } else if (sale >= 0) {
                previo = std::max(previo, sale);
            }
        }
        if (!llegan.empty()) n["detalle"]["llegan"] = llegan;
        ponerTramoDeEspera(n, previo, lat, edadMedida(programaV), edadMedida(programaV, "p95_ms"), cuadroMs);
        if (!llegan.empty()) {
            n["detalle"]["que_es"] = traducir("tiempos", "lo que agrega el compositor: su latencia y esperar a la capa que llega "
                                                         "última (el colchón de cada entrada ya está en su tramo)");
            // Lo fijado al compositor no incluye el colchón de las entradas (está en los tramos):
            // lo que se vigila es lo que agrega él, que no pase de dos cuadros.
            n["detalle"].erase("aviso");
            n["estado"] = "bien";
            const double propio = n["ms"].get<double>();
            if (propio > 2 * cuadroMs + 5) {
                n["estado"] = "justo";
                n["detalle"]["aviso"] = llenar(traducir("tiempos", "el compositor agrega %1 ms (más de dos cuadros): algo de "
                                                                   "adentro no da abasto"),
                                               {std::to_string(static_cast<int>(propio))});
            }
        }
        a.acumulado[id] = n["acumulado_ms"].get<double>();
        a.seguir(id, desde);
        fuentesV.push_back(id);
    }
    if (programaA.is_object() && !programaA.empty()) {
        const std::string id = "mezcla:programa";
        json& n = a.nodo(id, "mezcla", traducir("tiempos", "Mezcla del programa"), 2, "audio");
        const double lat = num(programaA, "latencia_salida_ms", num(programaA, "latencia_ms", 0.0));
        n["detalle"] = {{"latencia_fijada_ms", lat}};
        std::vector<std::string> desde;
        for (const auto& [e, fin] : finA) {
            a.tramo(fin, id, "audio");
            desde.push_back(fin);
        }
        double previo = 0;
        for (const auto& d : desde)
            if (a.acumulado.count(d)) previo = std::max(previo, a.acumulado[d]);
        // El audio va en bloques de 10 ms: un par de bloques arriba de lo fijado es lo normal.
        ponerTramoDeEspera(n, previo, lat, edadMedida(programaA), edadMedida(programaA, "p95_ms"), 20.0);
        a.acumulado[id] = n["acumulado_ms"].get<double>();
        a.seguir(id, desde);
        fuentesA.push_back(id);
    }

    // --- Salidas ---------------------------------------------------------------------------
    json salidas = json::array();
    const auto salida = [&](const std::string& id, const std::string& tipo, const std::string& titulo, const json& st,
                            json detalle) {
        json& n = a.nodo(id, tipo, titulo, 3, "ambos");
        std::vector<std::string> desde;
        for (const auto& v : fuentesV) {
            a.tramo(v, id, "video");
            desde.push_back(v);
        }
        for (const auto& v : fuentesA) {
            a.tramo(v, id, "audio");
            desde.push_back(v);
        }
        double previo = 0;
        for (const auto& d : desde)
            if (a.acumulado.count(d)) previo = std::max(previo, a.acumulado[d]);
        // El pipeline de la salida entrega a la hora del cuadro + su latencia: ese es el total
        // fijado; si se midió la edad de lo que sale (MedidorEdad), manda lo medido.
        const double medida = edadMedida(st);
        const double total = medida >= 0 ? medida : std::max(num(st, "latencia_ms", 0.0), previo);
        a.acumulado[id] = total;
        n["ms"] = redondo(std::max(0.0, total - previo));
        n["acumulado_ms"] = redondo(total);
        n["fuente"] = medida >= 0 ? "medido" : num(st, "latencia_ms", 0.0) > 0 ? "declarado" : "sin_medir";
        if (medida >= 0) {
            detalle["edad_medida_ms"] = redondo(medida);
            detalle["latencia_fijada_ms"] = num(st, "latencia_ms", 0.0);
        }
        n["estado"] = num(st, "perdidos", 0) > 0 ? "justo" : "bien";
        for (const char* k : {"kbps", "cuadros", "perdidos", "codificador", "conectada", "atraso_s", "desbordes", "url"})
            if (st.contains(k)) detalle[k] = st[k];
        n["detalle"] = std::move(detalle);
        a.seguir(id, desde);
        salidas.push_back({{"id", id}, {"titulo", titulo}, {"total_ms", redondo(total)}, {"fuente", n["fuente"]},
                           {"camino", a.camino[id]}});
    };
    const json& fs = f.salidas;
    const json principal = fs.value("principal", json::object());
    if (si(principal, "andando")) salida("salida:transmision", "transmision", traducir("tiempos", "Transmisión"), principal, json::object());
    const json extras = fs.value("extras", json::object());
    for (const auto& [x, st] : extras.items())
        if (si(st, "andando"))
            salida("salida:transmision:" + x, "transmision", llenar(traducir("tiempos", "Transmisión %1"), {x}), st,
                   {{"destino", st.value("destino", std::string())}});
    const json grab = fs.value("grabacion", json::object());
    if (si(grab, "activa"))
        salida("salida:grabacion", "grabacion", traducir("tiempos", "Grabación"), grab.value("estado", json::object()),
               {{"archivo", grab.value("archivo", std::string())}});

    return {{"nodos", a.nodos},
            {"tramos", a.tramos},
            {"salidas", salidas},
            {"estudio", {{"latencia_ms", latEstudio}, {"tope_ms", num(estudio, "tope_ms", 0.0)}}}};
}

namespace {
bool gpu(const std::string& m) { return m == "gl" || m == "d3d12" || m == "d3d11" || m == "cuda" || m == "va"; }
std::string nombreMemoria(const std::string& m) {
    if (m == "gl") return "GPU (GL)";
    if (m == "d3d12") return "GPU (D3D12)";
    if (m == "d3d11") return "GPU (D3D11)";
    if (m == "cuda") return "GPU (CUDA)";
    if (m == "va") return "GPU (VA)";
    if (m == "dmabuf") return "DMABuf";
    if (m == "sistema") return "RAM";
    return m;
}
std::string msTexto(double ms) {
    char b[32];
    std::snprintf(b, sizeof b, ms < 10 ? "%.1f ms" : "%.0f ms", ms);
    return b;
}
} // namespace

json diagnosticar(const json& pipeline, const std::map<std::string, TarjetaEnPipeline>& tarjetas) {
    const json elems = pipeline.value("elementos", json::array());
    // De quién es cada elemento (los suyos y los conversores que pide).
    std::vector<std::string> inst;
    for (const auto& [i, t] : tarjetas) inst.push_back(i);
    std::map<std::string, std::string> duenio; // elemento → instancia
    for (const auto& [i, lista] : elementosPorTarjeta(pipeline, inst))
        for (const auto& el : lista) duenio[el.value("nombre", std::string())] = i;
    const auto tarjetasDe = [&](const std::vector<std::string>& nombres) {
        json r = json::array();
        std::vector<std::string> vistas;
        for (const auto& n : nombres) {
            const auto d = duenio.find(n);
            if (d == duenio.end() || std::find(vistas.begin(), vistas.end(), d->second) != vistas.end()) continue;
            vistas.push_back(d->second);
            const auto t = tarjetas.find(d->second);
            if (t != tarjetas.end()) r.push_back({{"id", t->second.id}, {"titulo", t->second.titulo}});
        }
        return r;
    };
    const auto quien = [&](const json& el) {
        const std::string n = el.value("nombre", std::string());
        const auto d = duenio.find(n);
        if (d != duenio.end())
            if (const auto t = tarjetas.find(d->second); t != tarjetas.end()) return "«" + t->second.titulo + "»";
        return "«" + n + "»";
    };
    // El período de un cuadro (de las caps: «… 30 fps»).
    double cuadro = 0;
    for (const auto& el : elems) {
        const std::string c = el.value("caps_salida", std::string());
        const auto p = c.find(" fps");
        if (p == std::string::npos) continue;
        const auto i = c.rfind(' ', p - 1);
        const double fps = std::atof(c.substr(i + 1, p - i - 1).c_str());
        if (fps > 0) {
            cuadro = 1000.0 / fps;
            break;
        }
    }
    json hallazgos = json::array();
    const auto hallazgo = [&](const char* tipo, const char* gravedad, const std::string& texto, double ms,
                              const std::vector<std::string>& nombres) {
        hallazgos.push_back({{"tipo", tipo}, {"gravedad", gravedad}, {"texto", texto}, {"ms", redondo(ms)},
                             {"elementos", nombres}, {"tarjetas", tarjetasDe(nombres)}});
    };

    // 1) Baja de la GPU y vuelve a subir: lo del medio trabaja en RAM.
    double total = 0;
    int cruces = 0, idas = 0;
    int bajo = -1;
    for (int i = 0; i < static_cast<int>(elems.size()); ++i) {
        const json& el = elems[static_cast<size_t>(i)];
        // La fuente de la salida de la entrada (su intersrc) trae la edad de la entrada: ya contada.
        if (!(el.value("fuente", false) && el.value("nombre", std::string()).rfind("salida/", 0) == 0))
            total += el.value("agrega_ms", 0.0);
        const std::string cruce = el.value("cruce", std::string());
        if (cruce.empty()) continue;
        ++cruces;
        if (cruce == "baja") {
            bajo = i;
            continue;
        }
        if (bajo < 0) continue;
        ++idas;
        const json& b = elems[static_cast<size_t>(bajo)];
        std::vector<std::string> nombres{b.value("nombre", std::string())};
        double ms = b.value("agrega_ms", 0.0) + el.value("agrega_ms", 0.0);
        std::string porque;
        for (int k = bajo + 1; k < i; ++k) {
            const json& m = elems[static_cast<size_t>(k)];
            nombres.push_back(m.value("nombre", std::string()));
            if (esConversor(m)) {
                ms += m.value("agrega_ms", 0.0);
                continue;
            }
            if (esDePaso(m)) continue;
            bool puedeGpu = false, cualquiera = false;
            for (const auto& x : m.value("puede_entrada", json::array())) {
                puedeGpu = puedeGpu || gpu(x.get<std::string>());
                cualquiera = cualquiera || x == "cualquiera";
            }
            porque += (porque.empty() ? "" : "; ") +
                      llenar(puedeGpu     ? traducir("tiempos", "%1 puede trabajar en la GPU pero negoció RAM (lo que tiene al "
                                                                "lado la obligó)")
                             : cualquiera ? traducir("tiempos", "%1 acepta cualquier memoria pero negoció RAM")
                                          : traducir("tiempos", "%1 trabaja en RAM: sus pads solo aceptan memoria del sistema"),
                             {quien(m)});
        }
        nombres.push_back(el.value("nombre", std::string()));
        hallazgo("ida_y_vuelta", "aviso",
                 porque.empty()
                     ? llenar(traducir("tiempos", "El video baja de la %1 en «%2» y vuelve a subir en «%3». Cada cruce copia "
                                                  "el cuadro entero (%4 entre bajar y subir, más CPU y ancho de memoria)."),
                              {nombreMemoria(b.value("memoria_entrada", std::string())), b.value("nombre", std::string()),
                               el.value("nombre", std::string()), msTexto(ms)})
                     : llenar(traducir("tiempos", "El video baja de la %1 en «%2» y vuelve a subir en «%3»: %4. Cada cruce "
                                                  "copia el cuadro entero (%5 entre bajar y subir, más CPU y ancho de memoria)."),
                              {nombreMemoria(b.value("memoria_entrada", std::string())), b.value("nombre", std::string()),
                               el.value("nombre", std::string()), porque, msTexto(ms)}),
                 ms, nombres);
        bajo = -1;
    }
    if (bajo >= 0) {
        const json& b = elems[static_cast<size_t>(bajo)];
        hallazgo("termina_en_ram", "info",
                 llenar(traducir("tiempos", "Desde «%1» el video sigue en RAM hasta la salida de la entrada (%2 lo bajó de la "
                                            "GPU y nadie lo vuelve a subir)."),
                        {b.value("nombre", std::string()), quien(b)}),
                 b.value("agrega_ms", 0.0), {b.value("nombre", std::string())});
    }

    // 2) Lo que retiene, 3) lo que declara contra lo que mide, 4) conversiones en la CPU.
    double colchonPlaca = 0;
    std::vector<std::string> colchonNombres;
    for (const auto& el : elems) {
        if (!el.contains("agrega_ms")) continue;
        const std::string n = el.value("nombre", std::string());
        // La cadena de la placa «sin señal»: el traspaso y el conmutador (input-selector; en
        // versiones viejas, fallbackswitch), juntos (el colchón).
        if (el.value("traspaso", false) ||
            (n.rfind("salida/", 0) == 0 && (fabricaDe(el) == "fallbackswitch" || fabricaDe(el) == "input-selector"))) {
            colchonPlaca += el.value("agrega_ms", 0.0);
            colchonNombres.push_back(n);
            continue;
        }
        if (el.value("fuente", false)) {
            // La fuente: la edad con que ya sale el cuadro. La de la salida (su intersrc) repite la
            // de la entrada; la de la cámara es la captura (sensor, USB, driver, sistema).
            if (n.rfind("salida/", 0) == 0) continue;
            const double ms = el.value("agrega_ms", 0.0);
            if (ms > std::max(cuadro > 0 ? cuadro * 2 : 66.0, 50.0))
                hallazgo("captura_lenta", "aviso",
                         llenar(traducir("tiempos", "La fuente entrega los cuadros con %1 de edad desde que se tomaron (la "
                                                    "cámara, el USB, el driver y el sistema): es lo que más pesa y no lo agrega "
                                                    "ninguna tarjeta. Probá otro modo de la cámara (crudo o MJPEG, menos "
                                                    "resolución o más cps)."),
                                {msTexto(ms)}),
                         ms, {n});
            continue;
        }
        if (el.contains("cambia_la_hora_ms")) {
            const double c = el.value("cambia_la_hora_ms", 0.0);
            hallazgo("cambia_la_hora", "aviso",
                     llenar(traducir("tiempos", "%1 publica los cuadros con otra hora: salen %2 «más jóvenes» que como "
                                                "entraron. Lo que se mide después queda corrido eso; si es un mezclador de "
                                                "video, el contenido queda %2 más tarde que el audio en las salidas."),
                            {quien(el), msTexto(c)}),
                     c, {n});
            continue;
        }
        const double ms = el.value("agrega_ms", 0.0);
        const double umbral = cuadro > 0 ? cuadro * 1.5 : 40.0;
        if (ms > umbral) {
            const bool mucho = cuadro > 0 ? ms > cuadro * 3 : ms > 100;
            std::string t = llenar(traducir("tiempos", "%1 agrega %2"), {quien(el), msTexto(ms)});
            if (cuadro > 0)
                t += " " + traducir("tiempos", "(≈%n cuadro)", nullptr, static_cast<int>(ms / cuadro + 0.5));
            t += el.contains("nivel_cola_ms")
                     ? llenar(traducir("tiempos", ": la cola guarda %1 de video"), {msTexto(el.value("nivel_cola_ms", 0.0))})
                     : traducir("tiempos", ": tarda o retiene cuadros (un filtro temporal que guarda cuadros cuenta aunque "
                                           "procese rápido)");
            hallazgo("retiene", mucho ? "problema" : "aviso", t, ms, {n});
        }
        if (el.contains("declara_ms") && !esDePaso(el)) {
            const double dec = el.value("declara_ms", 0.0);
            const double tol = std::max(5.0, cuadro * 0.5);
            if (ms - dec > tol && ms > 5)
                hallazgo("declara_menos", "aviso",
                         llenar(traducir("tiempos", "%1 tarda %2 pero le declara %3 al pipeline: la compensación del estudio "
                                                    "no lo sabe; si lo que falta se acerca al colchón de la entrada, llega "
                                                    "tarde a la mezcla."),
                                {quien(el), msTexto(ms), msTexto(dec)}),
                         ms - dec, {n});
            else if (dec - ms > std::max(20.0, cuadro))
                hallazgo("declara_de_mas", "info",
                         llenar(traducir("tiempos", "%1 declara %2 pero tarda %3: el estudio espera de más por él."),
                                {quien(el), msTexto(dec), msTexto(ms)}),
                         dec - ms, {n});
        }
        const std::string fab = fabricaDe(el);
        const bool deGpu = fab.rfind("gl", 0) == 0 || fab.rfind("d3d", 0) == 0 || fab.rfind("cuda", 0) == 0 ||
                           fab.rfind("va", 0) == 0;
        if (esConversor(el) && !deGpu && !gpu(el.value("memoria_entrada", std::string())) && ms >= 1.0 &&
            el.value("caps_entrada", std::string()) != el.value("caps_salida", std::string()))
            hallazgo("conversion_cpu", ms >= 5 ? "aviso" : "info",
                     duenio.count(n)
                         ? llenar(traducir("tiempos", "«%1» convierte en la CPU (%2 → %3): %4; lo pide %5"),
                                  {n, el.value("caps_entrada", std::string()), el.value("caps_salida", std::string()),
                                   msTexto(ms), quien(el)})
                         : llenar(traducir("tiempos", "«%1» convierte en la CPU (%2 → %3): %4"),
                                  {n, el.value("caps_entrada", std::string()), el.value("caps_salida", std::string()),
                                   msTexto(ms)}),
                     ms, {n});
    }
    if (colchonPlaca > std::max(10.0, cuadro * 0.5))
        hallazgo("colchon_placa", colchonPlaca > std::max(cuadro * 2.5, 60.0) ? "aviso" : "info",
                 llenar(traducir("tiempos", "Antes de la composición, la cadena de la placa «sin señal» retiene %1: el "
                                            "conmutador no deja pasar el cuadro apenas llega (un fallbackswitch espera la "
                                            "latencia declarada + un margen). Es el colchón de esta entrada; la composición "
                                            "ya espera sola a la más lenta."),
                        {msTexto(colchonPlaca)}),
                 colchonPlaca, colchonNombres);
    const auto peso = [](const json& h) {
        const std::string g = h.value("gravedad", std::string());
        return g == "problema" ? 0 : g == "aviso" ? 1 : 2;
    };
    std::stable_sort(hallazgos.begin(), hallazgos.end(), [&](const json& a, const json& b) {
        if (peso(a) != peso(b)) return peso(a) < peso(b);
        return a.value("ms", 0.0) > b.value("ms", 0.0);
    });
    return {{"resumen",
             {{"elementos", elems.size()}, {"total_ms", redondo(total)}, {"cruces", cruces}, {"idas_y_vueltas", idas},
              {"cuadro_ms", redondo(cuadro)}}},
            {"hallazgos", hallazgos}};
}

} // namespace ba::app::tiempos
