// ============================================================================
// src/app/tiempos/Tiempos.cpp — el grafo de demoras (ver Tiempos.hpp)
// ============================================================================
#include "tiempos/Tiempos.hpp"

#include <algorithm>
#include <cmath>
#include <set>

namespace ba::app::tiempos {

using json = nlohmann::json;

namespace {

double redondo(double ms) { return std::round(ms * 10.0) / 10.0; }
/// Más que esto (para un lado u otro) no es una demora: la fuente trae la hora de otro reloj.
constexpr double kHoraCorridaMs = 60000;

/// Los filtros que no son tarjetas del grafo (los agregan ControlCamaras, SuperRes…):
/// un nombre que se entienda en vez del elemento de GStreamer.
std::string tituloDeElemento(const std::string& e) {
    static const std::map<std::string, std::string> nombres{
        {"baptz", "PTZ digital"},       {"baptzgl", "PTZ digital"},  {"bamejora", "Mejorar imagen"},
        {"basuperres", "Superresolución"}, {"baonnxsr", "Superresolución"}, {"baplugin", "Plugin"},
        {"barostros", "Rostros"},       {"barack", "Rack de audio"}, {"balimitador", "Limitador"}};
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
        const bool activa = sinDatos >= 0 && sinDatos < 2.0;
        const json filtros = e.value("filtros", json::array());

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
                                       : tn == "texto" ? std::string("Voz a texto") : tn == "habla" ? std::string("Texto a voz") : std::string("Traducción");
                            p.detalle = {{"que", "escucha una copia del audio: no atrasa lo que sale"}, {"tipo", tn}};
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
            // La hora que trae, corrida (más de un minuto para un lado u otro): no es una demora,
            // es una fuente con otro reloj. Se marca y no se dibuja el número (estiraría todo).
            const double edadCruda = edad.is_object() ? num(edad, "p50_ms", 0.0) : 0.0;
            const bool corrida = edad.is_object() && std::fabs(edadCruda) > kHoraCorridaMs;
            const bool medida = edad.is_object() && !corrida;
            // El desfase (a mano, sincronizar, el retardo del audio) corre la HORA del cuadro: la edad
            // medida queda corrida eso (con +200, −196 ms). Lo que tarda de verdad es edad + desfase;
            // y queda listo para mostrarse recién a su hora (la escena lo espera): el mayor de los dos.
            const double desfase = num(e, "desfase_ms", 0.0) + num(e, esAudio ? "desfase_audio_ms" : "desfase_video_ms", 0.0);
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
                ne["detalle"]["aviso"] = "la hora que trae está corrida " + std::to_string(static_cast<long long>(edadCruda / 1000)) +
                                         " s del reloj del estudio: no es una demora, es otro reloj";
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
    std::vector<std::string> fuentesV, fuentesA;
    if (programaV.is_object() && !programaV.empty()) {
        const std::string id = "escena:programa";
        const std::string esc = video.value("al_aire", std::string());
        json& n = a.nodo(id, "escena", esc.empty() ? std::string("Programa") : esc, 2, "video");
        const double lat = num(programaV, "latencia_salida_ms", num(programaV, "latencia_ms", 0.0));
        n["detalle"] = {{"escena", esc}, {"latencia_fijada_ms", lat}, {"gpu", programaV.value("gpu", json())},
                        {"en_transicion", si(programaV, "en_transicion")}};
        std::vector<std::string> desde;
        for (const auto& c : programaV.value("capas", json::array())) {
            const std::string e = c.value("entrada", std::string());
            if (const auto it = finV.find(e); it != finV.end() && std::find(desde.begin(), desde.end(), it->second) == desde.end()) {
                a.tramo(it->second, id, "video");
                desde.push_back(it->second);
            }
        }
        double previo = 0;
        for (const auto& d : desde)
            if (a.acumulado.count(d)) previo = std::max(previo, a.acumulado[d]);
        const double acum = std::max(lat, previo);
        a.acumulado[id] = acum;
        n["ms"] = redondo(std::max(0.0, acum - previo));
        n["acumulado_ms"] = redondo(acum);
        n["fuente"] = "declarado";
        n["estado"] = "bien";
        a.seguir(id, desde);
        fuentesV.push_back(id);
    }
    if (programaA.is_object() && !programaA.empty()) {
        const std::string id = "mezcla:programa";
        json& n = a.nodo(id, "mezcla", "Mezcla del programa", 2, "audio");
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
        const double acum = std::max(lat, previo);
        a.acumulado[id] = acum;
        n["ms"] = redondo(std::max(0.0, acum - previo));
        n["acumulado_ms"] = redondo(acum);
        n["fuente"] = "declarado";
        n["estado"] = "bien";
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
        // El pipeline de la salida entrega a la hora del cuadro + su latencia: ese es el total.
        const double total = std::max(num(st, "latencia_ms", 0.0), previo);
        a.acumulado[id] = total;
        n["ms"] = redondo(std::max(0.0, total - previo));
        n["acumulado_ms"] = redondo(total);
        n["fuente"] = num(st, "latencia_ms", 0.0) > 0 ? "declarado" : "sin_medir";
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
    if (si(principal, "andando")) salida("salida:transmision", "transmision", "Transmisión", principal, json::object());
    const json extras = fs.value("extras", json::object());
    for (const auto& [x, st] : extras.items())
        if (si(st, "andando"))
            salida("salida:transmision:" + x, "transmision", "Transmisión " + x, st, {{"destino", st.value("destino", std::string())}});
    const json grab = fs.value("grabacion", json::object());
    if (si(grab, "activa"))
        salida("salida:grabacion", "grabacion", "Grabación", grab.value("estado", json::object()),
               {{"archivo", grab.value("archivo", std::string())}});

    return {{"nodos", a.nodos},
            {"tramos", a.tramos},
            {"salidas", salidas},
            {"estudio", {{"latencia_ms", latEstudio}, {"tope_ms", num(estudio, "tope_ms", 0.0)}}}};
}

} // namespace ba::app::tiempos
