// ============================================================================
// src/app/evento/LineaProductor.cpp — ver LineaProductor.hpp
// ============================================================================
#include "evento/LineaProductor.hpp"

#include "evento/LeerJson.hpp"
#include "i18n/I18n.hpp"

#include <algorithm>
#include "evento/LineaPlantillas.hpp"

#include <cctype>
#include <cstdio>
#include <set>

namespace ba::evento::linea {

namespace {

using nlohmann::json;
using ba::i18n::llenar;
using ba::i18n::traducir;

std::string seg(int64_t ms) { return std::to_string(ms / 1000); }

bool futuro(const PasoResuelto& pr) { return pr.estado == EstadoPaso::Proximo || pr.estado == EstadoPaso::Futuro; }

/// ¿Se le puede cambiar el largo? Los que duran lo que dicen o los abiertos (el
/// orador: se le cambia lo previsto, que es lo que se le avisa).
bool deLargoCambiable(const Paso& p) {
    return (p.fin.modo == ModoFin::Duracion || p.fin.modo == ModoFin::Abierto) && !tieneEtiqueta(p, "fijo");
}

/// La operación para que un paso dure `nuevoSeg`.
json opLargo(const Paso& p, int64_t nuevoSeg) {
    json campos{{"duracionPrevistaSeg", nuevoSeg}};
    if (p.fin.modo == ModoFin::Duracion) campos["fin"] = {{"duracion", nuevoSeg}};
    return {{"op", "cambiar"}, {"id", p.id}, {"campos", campos}};
}

/// ¿Hay algo con ese id en la línea (bloque, paso, pieza, pista o estante)?
bool existeId(const Linea& l, const std::string& id) {
    for (const Bloque& b : l.bloques) {
        if (b.id == id) return true;
        for (const Paso& p : b.pasos) {
            if (p.id == id) return true;
            for (const Pieza& z : p.piezas)
                if (z.id == id) return true;
        }
    }
    for (const Pista& p : l.pistas)
        if (p.id == id) return true;
    for (const ItemEstante& i : l.estante)
        if (i.id == id) return true;
    return false;
}

/// Un id que no choca con la línea ni con los ya usados en este lote.
std::string idLibre(const Linea& l, std::set<std::string>& usados, const std::string& base) {
    std::string id = base;
    for (int n = 2; existeId(l, id) || usados.count(id); ++n) id = base + "-" + std::to_string(n);
    usados.insert(id);
    return id;
}

// --- Recortes ---------------------------------------------------------------------------

struct Recorte {
    json ops = json::array();
    int64_t hechoMs = 0;
    json pasos = json::array(); ///< [{paso, nombre, antesSeg, despuesSeg}]
};

/// Saca hasta `necesarioMs` de `cands`, de la prioridad más baja a la más alta y,
/// dentro de cada una, en proporción a lo que se le puede sacar a cada paso.
Recorte recortar(const std::vector<const Paso*>& cands, int64_t necesarioMs) {
    Recorte r;
    std::map<int, std::vector<const Paso*>> porPrioridad;
    for (const Paso* p : cands)
        if (deLargoCambiable(*p) && recortablePct(*p) > 0 && p->duracionPlanMs() >= 2000) porPrioridad[prioridadDe(*p)].push_back(p);
    int64_t resta = necesarioMs;
    for (const auto& [prio, ps] : porPrioridad) {
        if (resta <= 0) break;
        int64_t margen = 0;
        for (const Paso* p : ps) margen += p->duracionPlanMs() * recortablePct(*p) / 100;
        if (margen <= 0) continue;
        const int64_t toma = std::min(resta, margen);
        for (const Paso* p : ps) {
            const int64_t max = p->duracionPlanMs() * recortablePct(*p) / 100;
            // En proporción, redondeado a segundos para arriba (que alcance), sin pasarse del máximo.
            const int64_t corte = std::min(max, (max * toma + margen - 1) / margen);
            const int64_t s = std::min((corte + 999) / 1000, max / 1000);
            if (s <= 0) continue;
            const int64_t antes = p->duracionPlanMs() / 1000;
            r.ops.push_back(opLargo(*p, antes - s));
            r.pasos.push_back({{"paso", p->id}, {"nombre", p->nombre}, {"antesSeg", antes}, {"despuesSeg", antes - s}});
            r.hechoMs += s * 1000;
        }
        resta = necesarioMs - r.hechoMs;
    }
    return r;
}

/// El paso que se hace cargo de toda la diferencia: el de prioridad más baja y,
/// entre esos, el más largo, que quede con al menos 30 s. nullptr si ninguno.
const Paso* quienAbsorbe(const std::vector<const Paso*>& cands, int64_t necesarioMs) {
    const Paso* mejor = nullptr;
    for (const Paso* p : cands) {
        if (!deLargoCambiable(*p) || p->duracionPlanMs() - necesarioMs < 30000) continue;
        if (!mejor || prioridadDe(*p) < prioridadDe(*mejor) ||
            (prioridadDe(*p) == prioridadDe(*mejor) && p->duracionPlanMs() > mejor->duracionPlanMs()))
            mejor = p;
    }
    return mejor;
}

/// El que se estira para tapar un hueco: el más flexible (más recortable) y, si
/// empatan, el último (el que está más cerca del hueco).
const Paso* quienEstira(const std::vector<const Paso*>& cands) {
    const Paso* mejor = nullptr;
    for (const Paso* p : cands)
        if (deLargoCambiable(*p) && (!mejor || recortablePct(*p) >= recortablePct(*mejor))) mejor = p;
    return mejor;
}

json opcion(const std::string& id, const char* tipo, const std::string& texto, int64_t ganaMs, json ops) {
    return {{"id", id}, {"tipo", tipo}, {"texto", texto}, {"ganaSeg", ganaMs / 1000}, {"ops", std::move(ops)}};
}

/// Un paso de relleno de `ms`, para insertar con paso_agregar.
json pasoRelleno(const Linea& l, std::set<std::string>& usados, int64_t ms) {
    const int64_t s = std::max<int64_t>(1, ms / 1000);
    return {{"id", idLibre(l, usados, "relleno")},
            {"nombre", traducir("linea", "Relleno")},
            {"fin", {{"duracion", s}}},
            {"duracionPrevistaSeg", s},
            {"etiquetas", {"relleno", "opcional", "recortable:90"}},
            {"notaOperador", traducir("linea", "Relleno para llegar a horario: música, video institucional o la placa")}};
}

/// Dónde va un paso nuevo justo ANTES de `pasoId`: {bloque, despuesDe} (despuesDe
/// null = al final del bloque). Si es el primero de su bloque y hay uno antes, al
/// final de ese (así no le roba el inicio del bloque).
std::pair<std::string, json> lugarAntesDe(const Linea& l, const std::string& pasoId) {
    for (std::size_t b = 0; b < l.bloques.size(); ++b)
        for (std::size_t p = 0; p < l.bloques[b].pasos.size(); ++p) {
            if (l.bloques[b].pasos[p].id != pasoId) continue;
            if (p > 0) return {l.bloques[b].id, l.bloques[b].pasos[p - 1].id};
            if (b > 0) return {l.bloques[b - 1].id, nullptr};
            return {l.bloques[b].id, ""};
        }
    return {{}, nullptr};
}

json opPasoAgregar(const std::string& bloque, const json& despuesDe, json paso) {
    json op{{"op", "paso_agregar"}, {"bloque", bloque}, {"paso", std::move(paso)}};
    if (!despuesDe.is_null()) op["despuesDe"] = despuesDe;
    return op;
}

/// La operación que corre el hard start de un paso `minutos` (o null si no se puede).
json opMoverHora(const Linea& l, const Paso& p, int minutos) {
    const Bloque* b = l.bloqueDe(p.id);
    if (p.inicio.modo == ModoInicio::Hora) {
        Inicio i = p.inicio;
        i.hora = sumarMinutos(i.hora, minutos);
        if (i.hora.empty()) return nullptr;
        return {{"op", "cambiar"}, {"id", p.id}, {"campos", {{"inicio", i.aJson()}}}};
    }
    // El hard start es del bloque (su primer paso no dice otra cosa).
    if (b && b->inicio && b->inicio->modo == ModoInicio::Hora && !b->pasos.empty() && b->pasos.front().id == p.id) {
        Inicio i = *b->inicio;
        i.hora = sumarMinutos(i.hora, minutos);
        if (i.hora.empty()) return nullptr;
        return {{"op", "cambiar"}, {"id", b->id}, {"campos", {{"inicio", i.aJson()}}}};
    }
    return nullptr;
}

/// Las opciones para cubrir `faltaMs` (positivo: no se llega) con los pasos `cands`.
/// `prefijo` va en los ids de las opciones.
json opcionesFalta(const Linea& l, const std::vector<const Paso*>& cands, int64_t faltaMs, const std::string& prefijo,
                   Recorte* recorteOut) {
    json ops = json::array();
    Recorte rc = recortar(cands, faltaMs);
    if (!rc.ops.empty()) {
        ops.push_back(opcion(prefijo + "recortar", "recortar",
                             rc.hechoMs >= faltaMs
                                 ? llenar(traducir("linea", "recortar %1 pasos (los recortables, por prioridad): se ganan %2 s"),
                                          {std::to_string(rc.ops.size()), seg(rc.hechoMs)})
                                 : llenar(traducir("linea", "recortar %1 pasos (los recortables, por prioridad): se ganan %2 s de %3"),
                                          {std::to_string(rc.ops.size()), seg(rc.hechoMs), seg(faltaMs)}),
                             rc.hechoMs, rc.ops));
        ops.back()["pasos"] = rc.pasos;
    }
    if (const Paso* p = quienAbsorbe(cands, faltaMs)) {
        const int64_t antes = p->duracionPlanMs() / 1000, despues = antes - (faltaMs + 999) / 1000;
        ops.push_back(opcion(prefijo + "absorber", "absorber",
                             llenar(traducir("linea", "que «%1» absorba la diferencia: de %2 s a %3 s"),
                                    {p->nombre.empty() ? p->id : p->nombre, std::to_string(antes), std::to_string(despues)}),
                             (antes - despues) * 1000, json::array({opLargo(*p, despues)})));
        ops.back()["paso"] = p->id;
    }
    if (recorteOut) *recorteOut = std::move(rc);
    return ops;
}

} // namespace

// --- Etiquetas -------------------------------------------------------------------------

bool tieneEtiqueta(const Paso& p, const std::string& e) { return std::find(p.etiquetas.begin(), p.etiquetas.end(), e) != p.etiquetas.end(); }

namespace {
/// El número de una etiqueta «nombre:N» (o `def` si no está o no es un número).
int numeroDeEtiqueta(const Paso& p, const std::string& nombre, int def) {
    const std::string pre = nombre + ":";
    for (const std::string& e : p.etiquetas)
        if (e.compare(0, pre.size(), pre) == 0) {
            try {
                return std::stoi(e.substr(pre.size()));
            } catch (...) {
                return def;
            }
        }
    return def;
}
} // namespace

int recortablePct(const Paso& p) {
    if (tieneEtiqueta(p, "fijo")) return 0;
    const int n = numeroDeEtiqueta(p, "recortable", tieneEtiqueta(p, "recortable") ? 50 : 0);
    return std::clamp(n, 0, 90);
}

int prioridadDe(const Paso& p) { return std::clamp(numeroDeEtiqueta(p, "prioridad", 5), 1, 9); }

std::string sumarMinutos(const std::string& hhmm, int minutos) {
    int h = 0, m = 0;
    char dos = 0;
    if (std::sscanf(hhmm.c_str(), "%d%c%d", &h, &dos, &m) != 3 || dos != ':' || h < 0 || h > 47 || m < 0 || m > 59) return {};
    int t = ((h * 60 + m + minutos) % 1440 + 1440) % 1440;
    char out[8];
    std::snprintf(out, sizeof out, "%02d:%02d", t / 60, t % 60);
    return out;
}

// --- Ajustar ----------------------------------------------------------------------------

json proponerAjuste(const Linea& l, const Resolucion& r, const std::string& salida, int64_t objetivoFinMs) {
    json out{{"ops", json::array()}, {"saltear", json::array()}, {"recortaSeg", 0}, {"faltaSeg", 0}, {"overSeg", 0},
             {"opciones", json::array()}, {"hardStarts", json::array()}};
    const SalidaResuelta* sr = r.salida(salida);
    if (sr == nullptr) {
        out["texto"] = llenar(traducir("linea", "no hay una salida «%1»"), {salida});
        return out;
    }
    std::set<std::string> usados; // los ids nuevos de este pedido (rellenos)

    // Los pasos que vienen, en orden, y dónde está cada uno.
    std::vector<const Paso*> vienen;
    std::map<std::string, std::size_t> pos;
    for (const PasoResuelto& pr : sr->pasos) {
        if (!futuro(pr)) continue;
        if (const Paso* p = l.paso(pr.paso)) {
            pos[p->id] = vienen.size();
            vienen.push_back(p);
        }
    }

    // --- Cada hard start que viene, con su overlap o su hueco -----------------------------
    // Del último hard start anclado a su hora para adelante es lo que mueve el fin.
    std::size_t desdeFin = 0;
    for (const HardStart& h : sr->hardStarts) {
        const auto it = pos.find(h.paso);
        if (it == pos.end()) continue;
        const Paso& p = *vienen[it->second];
        const std::vector<const Paso*> antes(vienen.begin(), vienen.begin() + static_cast<long>(it->second));
        if (h.gapMs >= 0) desdeFin = it->second;
        json hs{{"paso", h.paso}, {"nombre", p.nombre}, {"gapSeg", h.gapMs / 1000}, {"politica", nombreDe(h.politica)},
                {"opciones", json::array()}};
        const std::string pre = "hs-" + h.paso + "-";
        if (h.gapMs < 0) {
            const int64_t falta = -h.gapMs;
            hs["texto"] = llenar(traducir("linea", "a «%1» se llega %2 s tarde"), {p.nombre.empty() ? p.id : p.nombre, seg(falta)});
            for (json& o : opcionesFalta(l, antes, falta, pre, nullptr)) hs["opciones"].push_back(std::move(o));
            const int min = static_cast<int>((falta + 59999) / 60000);
            if (json op = opMoverHora(l, p, min); !op.is_null())
                hs["opciones"].push_back(opcion(pre + "mover_hora", "mover_hora",
                                                llenar(traducir("linea", "correr la hora de «%1» %2 min más tarde"),
                                                       {p.nombre.empty() ? p.id : p.nombre, std::to_string(min)}),
                                                falta, json::array({op})));
        } else if (h.gapMs >= 60000) {
            const int64_t sobra = h.gapMs;
            hs["texto"] = llenar(traducir("linea", "antes de «%1» sobran %2 s"), {p.nombre.empty() ? p.id : p.nombre, seg(sobra)});
            const auto [bloque, despuesDe] = lugarAntesDe(l, p.id);
            if (!bloque.empty())
                hs["opciones"].push_back(opcion(pre + "relleno", "relleno",
                                                llenar(traducir("linea", "insertar un relleno de %1 s antes de «%2»"),
                                                       {seg(sobra), p.nombre.empty() ? p.id : p.nombre}),
                                                sobra, json::array({opPasoAgregar(bloque, despuesDe, pasoRelleno(l, usados, sobra))})));
            if (const Paso* e = quienEstira(antes)) {
                const int64_t a = e->duracionPlanMs() / 1000, d = a + sobra / 1000;
                hs["opciones"].push_back(opcion(pre + "estirar", "estirar",
                                                llenar(traducir("linea", "que «%1» se estire: de %2 s a %3 s"),
                                                       {e->nombre.empty() ? e->id : e->nombre, std::to_string(a), std::to_string(d)}),
                                                sobra, json::array({opLargo(*e, d)})));
            }
            if (const int min = static_cast<int>(sobra / 60000); min > 0)
                if (json op = opMoverHora(l, p, -min); !op.is_null())
                    hs["opciones"].push_back(opcion(pre + "mover_hora", "mover_hora",
                                                    llenar(traducir("linea", "adelantar la hora de «%1» %2 min"),
                                                           {p.nombre.empty() ? p.id : p.nombre, std::to_string(min)}),
                                                    int64_t{min} * 60000, json::array({op})));
        } else {
            continue; // a horario: nada que proponer
        }
        out["hardStarts"].push_back(std::move(hs));
    }

    // --- El fin de la salida contra el objetivo ---------------------------------------------
    const int64_t objetivo = objetivoFinMs > 0 ? objetivoFinMs : sr->finPlaneadoMs;
    if (objetivo <= 0) {
        out["texto"] = traducir("linea", "no hay un fin planeado: decí a qué hora hay que terminar (`hasta`)");
        return out;
    }
    out["objetivoMs"] = objetivo;
    const int64_t over = sr->finMs - objetivo;
    out["overSeg"] = over / 1000;
    const std::vector<const Paso*> alFin(vienen.begin() + static_cast<long>(std::min(desdeFin, vienen.size())), vienen.end());
    if (over <= 0) {
        const int64_t sobra = -over;
        out["texto"] = llenar(traducir("linea", "se llega con %1 s de sobra: no hace falta recortar"), {seg(sobra)});
        if (sobra >= 60000 && !vienen.empty()) {
            const Paso* ultimo = vienen.back();
            const Bloque* b = l.bloqueDe(ultimo->id);
            if (b)
                out["opciones"].push_back(opcion("relleno", "relleno",
                                                 llenar(traducir("linea", "insertar un relleno de %1 s al final"), {seg(sobra)}),
                                                 sobra, json::array({opPasoAgregar(b->id, ultimo->id, pasoRelleno(l, usados, sobra))})));
            if (const Paso* e = quienEstira(alFin)) {
                const int64_t a = e->duracionPlanMs() / 1000, d = a + sobra / 1000;
                out["opciones"].push_back(opcion("estirar", "estirar",
                                                 llenar(traducir("linea", "que «%1» se estire: de %2 s a %3 s"),
                                                        {e->nombre.empty() ? e->id : e->nombre, std::to_string(a), std::to_string(d)}),
                                                 sobra, json::array({opLargo(*e, d)})));
            }
        }
        return out;
    }

    Recorte rc;
    for (json& o : opcionesFalta(l, alFin, over, "", &rc)) out["opciones"].push_back(std::move(o));
    out["ops"] = rc.ops;
    out["recortaSeg"] = rc.hechoMs / 1000;
    // Si recortar no alcanza: lo opcional que viene, hasta cubrir lo que falta.
    int64_t falta = std::max<int64_t>(0, over - rc.hechoMs);
    json quitar = json::array();
    int64_t salteado = 0;
    for (const Paso* p : alFin) {
        if (falta - salteado <= 0) break;
        if (!tieneEtiqueta(*p, "opcional")) continue;
        out["saltear"].push_back({{"paso", p->id}, {"nombre", p->nombre}, {"durSeg", p->duracionPlanMs() / 1000}});
        quitar.push_back({{"op", "quitar"}, {"id", p->id}});
        salteado += p->duracionPlanMs();
    }
    if (!quitar.empty())
        out["opciones"].push_back(opcion("saltear", "saltear",
                                         llenar(traducir("linea", "saltear %1 pasos opcionales (en vivo, linea_saltar cuando "
                                                                  "lleguen; o sacarlos de la línea): se ganan %2 s"),
                                                {std::to_string(quitar.size()), seg(salteado)}),
                                         salteado, quitar));
    falta = std::max<int64_t>(0, falta - salteado);
    out["faltaSeg"] = falta / 1000;
    if (out["opciones"].empty())
        out["texto"] = llenar(traducir("linea", "vamos %1 s pasados y en lo que viene no hay nada recortable ni opcional: "
                                                "hay que decidir a mano qué acortar"),
                              {seg(over)});
    else
        out["texto"] = llenar(traducir("linea", "vamos %1 s pasados: recortando lo recortable se ganan %2 s; faltan %3 s"),
                              {seg(over), seg(rc.hechoMs), std::to_string(out["faltaSeg"].get<int64_t>())});
    return out;
}

// --- Plantillas --------------------------------------------------------------------------

std::vector<Plantilla> plantillasDeFabrica() {
    // Los textos de los pasos son del evento (los ve el operador y salen en los
    // zócalos): van en castellano y la persona los cambia en la línea. Lo que
    // traduce la app es lo que dice ella misma.
    static const json fabrica = json::parse(R"([
      {"id": "orador", "nombre": "Orador", "pasos": [
        {"id": "orador", "nombre": "{nombre}", "fin": {"abierto": true}, "duracionPrevistaSeg": 300,
         "guion": "{guion}", "notaOperador": "{nota}", "etiquetas": ["recortable"],
         "piezas": [
           {"id": "escena", "pista": "escena", "tipo": "escena", "texto": "{escena}", "datos": {"escena": "{escena}"}},
           {"id": "palabra", "pista": "sala", "tipo": "orador", "texto": "Habla {nombre}", "datos": {"participante": "{participante}"}},
           {"id": "zocalo", "pista": "graficos", "tipo": "grafico", "texto": "Zócalo: {nombre}",
            "desde": {"ms": 3000}, "dura": {"ms": 8000},
            "datos": {"fuente": "Zócalo", "grafico": "zocalo", "valores": {"nombre": "{nombre}", "cargo": "{cargo}"}}}
         ]}
      ]},
      {"id": "punto", "nombre": "Punto del orden del día", "pasos": [
        {"id": "punto", "nombre": "Punto {n}: {titulo}", "fin": {"abierto": true}, "duracionPrevistaSeg": 600,
         "notaOperador": "{expediente}",
         "piezas": [
           {"id": "escena", "pista": "escena", "tipo": "escena", "texto": "{escena}", "datos": {"escena": "{escena}"}},
           {"id": "zocalo", "pista": "graficos", "tipo": "grafico", "texto": "Punto {n}: {titulo}",
            "desde": {"ms": 2000}, "dura": {"ms": 10000},
            "datos": {"fuente": "Zócalo", "grafico": "zocalo", "valores": {"nombre": "Punto {n}", "cargo": "{titulo}"}}},
           {"id": "tratar", "pista": "asamblea", "tipo": "asamblea", "texto": "Se trata el punto {n}",
            "datos": {"accion": "tratar", "n": "{n}"}}
         ]}
      ]},
      {"id": "votacion", "nombre": "Votación", "pasos": [
        {"id": "votacion", "nombre": "Votación: {titulo}", "fin": {"abierto": true}, "duracionPrevistaSeg": 300,
         "piezas": [
           {"id": "abrir", "pista": "asamblea", "tipo": "asamblea", "texto": "Abrir la votación", "datos": {"accion": "abrir_votacion", "titulo": "{titulo}"}},
           {"id": "resultado", "pista": "graficos", "tipo": "grafico", "texto": "Resultado de la votación",
            "desde": {"cuando": "bus:sesion/votacion.estado == 'cerrada'"},
            "datos": {"fuente": "Resultado", "grafico": "votacion"}}
         ]}
      ]}
    ])");
    std::vector<Plantilla> v;
    for (const json& j : fabrica) v.push_back(Plantilla::deJson(j));
    return v;
}

namespace {

/// Las marcas {clave} de un texto (no las {{…}} de la botonera).
void marcasDe(const std::string& s, std::set<std::string>& out) {
    for (std::size_t i = 0; (i = s.find('{', i)) != std::string::npos; ++i) {
        if (i + 1 < s.size() && s[i + 1] == '{') {
            const std::size_t f = s.find("}}", i);
            if (f == std::string::npos) return;
            i = f + 1;
            continue;
        }
        const std::size_t f = s.find('}', i);
        if (f == std::string::npos) return;
        const std::string k = s.substr(i + 1, f - i - 1);
        if (!k.empty() && std::all_of(k.begin(), k.end(), [](unsigned char c) { return std::isalnum(c) || c == '_'; })) out.insert(k);
        i = f;
    }
}

void marcasDe(const json& j, std::set<std::string>& out) {
    if (j.is_string()) marcasDe(j.get<std::string>(), out);
    else if (j.is_array() || j.is_object())
        for (const json& x : j) marcasDe(x, out);
}

/// ¿El dato de la fila falta (no está, es null o es un texto vacío)?
bool sinDato(const json& fila, const std::string& clave) {
    const auto it = fila.find(clave);
    return it == fila.end() || it->is_null() || (it->is_string() && it->get<std::string>().empty());
}

/// ¿A esta pieza le falta lo que la hace ser? Un dato de primer nivel que es SOLO una
/// marca sin valor («escena»: "{escena}", «participante»: "{participante}"): sin eso
/// la pieza no tiene sentido. Una marca dentro de un texto queda vacía y la pieza va.
bool faltaLoEsencial(const Pieza& z, const json& fila) {
    if (!z.datos.is_object()) return false;
    for (const auto& [k, v] : z.datos.items()) {
        if (!v.is_string()) continue;
        const std::string t = v.get<std::string>();
        if (t.size() < 3 || t.front() != '{' || t.back() != '}' || t[1] == '{') continue;
        if (sinDato(fila, t.substr(1, t.size() - 2))) return true;
    }
    return false;
}

/// La pista para una pieza de plantilla: la que se llama así; si no, la de ese
/// tipo en la salida (o la global); si no, una nueva (va en `ops`).
std::string pistaPara(const Linea& l, const std::string& pedida, const std::string& salida, std::vector<Pista>& nuevas, json& ops) {
    if (pedida.empty()) return pedida;
    if (l.pista(pedida)) return pedida;
    for (const Pista& p : nuevas)
        if (p.id == pedida) return pedida;
    const bool porSalida = pistaPorSalida(pedida);
    const auto sirve = [&](const Pista& p) { return p.tipo == pedida && (porSalida ? p.salida == salida : p.salida.empty()); };
    for (const Pista& p : l.pistas)
        if (sirve(p)) return p.id;
    for (const Pista& p : nuevas)
        if (sirve(p)) return p.id;
    Pista p;
    p.tipo = pedida;
    p.salida = porSalida ? salida : std::string();
    p.id = porSalida ? pedida + "-" + salida : pedida;
    p.nombre = pedida;
    ops.push_back({{"op", "pista_agregar"}, {"pista", p.aJson()}});
    nuevas.push_back(p);
    return p.id;
}

/// La plantilla `id`: la del evento o, si no, la de fábrica. nullptr y `error` si
/// no está o no tiene pasos.
const Plantilla* plantillaDe(const Linea& l, const std::vector<Plantilla>& fabrica, const std::string& id, std::string& error) {
    const Plantilla* pl = nullptr;
    for (const Plantilla& p : l.plantillas)
        if (!pl && p.id == id) pl = &p;
    for (const Plantilla& p : fabrica)
        if (!pl && p.id == id) pl = &p;
    if (!pl) {
        std::string hay;
        for (const Plantilla& p : l.plantillas) hay += (hay.empty() ? "" : ", ") + p.id;
        for (const Plantilla& p : fabrica) hay += (hay.empty() ? "" : ", ") + p.id;
        error = llenar(traducir("linea", "no hay una plantilla «%1» (hay: %2)"), {id, hay});
    } else if (pl->pasos.empty()) {
        error = llenar(traducir("linea", "la plantilla «%1» no tiene pasos"), {pl->id});
        pl = nullptr;
    }
    return pl;
}

} // namespace

json desdePlantilla(const Linea& l, const DesdePlantilla& pedido) {
    json out{{"ok", false}, {"ops", json::array()}, {"avisos", json::array()}, {"pasos", json::array()}};
    const std::vector<Plantilla> fabrica = plantillasDeFabrica();
    std::string error;
    const Plantilla* pl = plantillaDe(l, fabrica, pedido.plantilla, error);
    if (!pl) {
        out["error"] = error;
        return out;
    }
    const Bloque* destino = nullptr;
    if (!pedido.bloque.empty()) {
        for (const Bloque& b : l.bloques)
            if (b.id == pedido.bloque) destino = &b;
        if (!destino) {
            out["error"] = llenar(traducir("linea", "no hay un bloque «%1»"), {pedido.bloque});
            return out;
        }
    }
    const std::string salida = pedido.salida.empty() ? l.salidasEfectivas().front() : pedido.salida;
    const std::string prefijo = pedido.prefijo.empty() ? pl->id : pedido.prefijo;
    const json filas = pedido.datos.is_array() && !pedido.datos.empty() ? pedido.datos : json::array({json::object()});
    // Todas las marcas de la plantilla: las que una fila no trae van vacías.
    std::set<std::string> marcas;
    marcasDe(pl->aJson(), marcas);

    std::set<std::string> usados;
    std::vector<Pista> nuevas;
    json pistasOps = json::array();
    json pasos = json::array();
    for (std::size_t i = 0; i < filas.size(); ++i) {
        const json fila = filas[i].is_object() ? filas[i] : json::object();
        // Las piezas a las que les falta lo esencial no van (y se avisa).
        Plantilla t = *pl;
        for (Paso& p : t.pasos) {
            std::vector<Pieza> quedan;
            for (Pieza& z : p.piezas) {
                if (!faltaLoEsencial(z, fila)) quedan.push_back(std::move(z));
                else
                    out["avisos"].push_back(llenar(traducir("linea", "fila %1: la pieza «%2» de «%3» no va (le falta un dato)"),
                                                   {std::to_string(i + 1), z.id, p.id}));
            }
            p.piezas = std::move(quedan);
        }
        std::map<std::string, std::string> valores;
        for (const std::string& k : marcas)
            valores[k] = sinDato(fila, k) ? std::string() : fila[k].is_string() ? fila[k].get<std::string>() : fila[k].dump();
        // Un prefijo por fila que no choque con nada de la línea ni de este lote.
        const auto choca = [&](const std::string& pre) {
            for (const Paso& p : t.pasos) {
                if (existeId(l, pre + "-" + p.id) || usados.count(pre + "-" + p.id)) return true;
                for (const Pieza& z : p.piezas)
                    if (existeId(l, pre + "-" + z.id) || usados.count(pre + "-" + z.id)) return true;
            }
            return false;
        };
        std::string pre = prefijo + "-" + std::to_string(i + 1);
        for (int n = 2; choca(pre); ++n) pre = prefijo + "-" + std::to_string(i + 1) + "-" + std::to_string(n);
        const int64_t tiempo = std::max(leerNumero(fila, "tiempoSeg"), leerNumero(fila, "duracionSeg"));
        for (Paso& p : instanciarPlantilla(t, pre, valores)) {
            usados.insert(p.id);
            for (Pieza& z : p.piezas) {
                usados.insert(z.id);
                z.pista = pistaPara(l, z.pista, salida, nuevas, pistasOps);
            }
            if (salida != l.salidasEfectivas().front() && p.salida.empty()) p.salida = salida;
            // El tiempo de la fila (el de cada orador, el de cada punto).
            if (tiempo > 0) {
                p.duracionPrevistaSeg = static_cast<int>(tiempo);
                if (p.fin.modo == ModoFin::Duracion) p.fin.duracionSeg = static_cast<int>(tiempo);
            }
            out["pasos"].push_back(p.id);
            pasos.push_back(p.aJson());
        }
    }
    json ops = pistasOps;
    if (!destino) {
        json b{{"id", idLibre(l, usados, prefijo + "-bloque")},
               {"nombre", pedido.nombreBloque.empty() ? pl->nombre : pedido.nombreBloque},
               {"pasos", pasos}};
        ops.push_back({{"op", "bloque_agregar"}, {"bloque", b}});
    } else {
        json despues = pedido.alPrincipio ? json("") : pedido.despuesDe.empty() ? json() : json(pedido.despuesDe);
        for (json& p : pasos) {
            const std::string id = p["id"];
            ops.push_back(opPasoAgregar(destino->id, despues, std::move(p)));
            despues = id;
        }
    }
    out["ops"] = std::move(ops);
    out["ok"] = true;
    return out;
}

json bloqueDeOrdenDelDia(const Linea& l, const std::string& plantilla, const json& puntos, const std::string& nombreBloque,
                         const std::string& salida) {
    json out{{"ok", false}, {"ops", json::array()}, {"avisos", json::array()}, {"pasos", json::array()}};
    const std::vector<Plantilla> fabrica = plantillasDeFabrica();
    std::string error;
    const Plantilla* pl = plantillaDe(l, fabrica, plantilla, error);
    if (!pl) {
        out["error"] = error;
        return out;
    }
    // Se expande sobre una copia (con la plantilla, si es de fábrica) y se lleva el
    // bloque armado como un lote: así se prueba y lo confirma una persona.
    Linea c = l;
    const bool delEvento = std::any_of(l.plantillas.begin(), l.plantillas.end(), [&](const Plantilla& p) { return p.id == pl->id; });
    if (!delEvento) c.plantillas.push_back(*pl);
    std::set<std::string> usados;
    Bloque b;
    b.id = idLibre(l, usados, "orden-del-dia");
    b.nombre = nombreBloque.empty() ? traducir("linea", "Orden del día") : nombreBloque;
    b.plantilla = pl->id;
    b.origen = "asamblea/orden-del-dia";
    c.bloques.push_back(b);
    expandirOrdenDelDia(c, puntos, {});
    Bloque armado = c.bloques.back();
    if (armado.pasos.empty()) {
        out["error"] = traducir("linea", "el orden del día no tiene puntos por tratar");
        return out;
    }
    const std::string s = salida.empty() ? l.salidasEfectivas().front() : salida;
    std::vector<Pista> nuevas;
    for (Paso& p : armado.pasos) {
        for (Pieza& z : p.piezas) z.pista = pistaPara(l, z.pista, s, nuevas, out["ops"]);
        out["pasos"].push_back(p.id);
    }
    // La de fábrica se suma a las del evento en el mismo lote: si el orden del día
    // cambia, el bloque se re-expande con ella (y el evento la lleva consigo).
    if (!delEvento) out["ops"].push_back({{"op", "plantilla_agregar"}, {"plantilla", pl->aJson()}});
    out["ops"].push_back({{"op", "bloque_agregar"}, {"bloque", armado.aJson()}});
    out["bloque"] = armado.id;
    out["ok"] = true;
    return out;
}

json lineaDeGuia(const Linea& l, const ba::evento::Evento& guia) { return lineaDeGuia(l, migrar(guia)); }

json lineaDeGuia(const Linea& l, Linea g) {
    json out{{"ok", true}, {"ops", json::array()}, {"avisos", json::array()}, {"bloques", json::array()}};
    const std::string salida = l.salidasEfectivas().front();
    std::set<std::string> usados;
    std::map<std::string, std::string> renombrar;
    const auto libre = [&](const std::string& id) {
        const std::string nuevo = idLibre(l, usados, id);
        if (nuevo != id) renombrar[id] = nuevo;
        return nuevo;
    };
    // Las pistas: las de la guía son de su primera salida; van a la de esta línea.
    // Cada una se busca por tipo en esta línea (como las de las plantillas).
    std::map<std::string, std::string> pistas;
    std::vector<Pista> nuevas;
    const std::string deGuia = g.salidasEfectivas().front();
    for (const Pista& p : g.pistas) pistas[p.id] = pistaPara(l, p.tipo, salida, nuevas, out["ops"]);
    for (Bloque& b : g.bloques) {
        b.id = libre(b.id);
        for (Paso& p : b.pasos) {
            p.id = libre(p.id);
            if (p.salida == deGuia) p.salida.clear(); // la primera de la guía es la primera de acá

            for (Pieza& z : p.piezas) {
                z.id = libre(z.id);
                if (pistas.count(z.pista)) z.pista = pistas[z.pista];
            }
        }
    }
    for (Bloque& b : g.bloques) {
        for (Paso& p : b.pasos) {
            if (renombrar.count(p.inicio.de)) p.inicio.de = renombrar[p.inicio.de];
            if (renombrar.count(p.inicio.paso)) p.inicio.paso = renombrar[p.inicio.paso];
        }
        out["bloques"].push_back(b.id);
        out["ops"].push_back({{"op", "bloque_agregar"}, {"bloque", b.aJson()}});
    }
    if (!renombrar.empty())
        out["avisos"].push_back(llenar(traducir("linea", "%1 ids ya estaban en la línea y se renombraron"), {std::to_string(renombrar.size())}));
    return out;
}

// --- El vigía ---------------------------------------------------------------------------

bool Vigia::debeProponer(const std::string& salida, int overSeg, int64_t ahoraMs) {
    PorSalida& s = salidas[salida];
    if (overSeg * 2 < umbralSeg) {
        s = {}; // se recuperó: lo que se avisó ya no cuenta
        return false;
    }
    if (overSeg < umbralSeg) return false;
    const bool primera = s.avisadoSeg == 0;
    const bool crecio = overSeg >= s.avisadoSeg + escalonSeg;
    const bool silencio = ahoraMs - s.avisadoMs >= silencioMs;
    if (!primera && !crecio && !silencio) return false;
    s.avisadoSeg = overSeg;
    s.avisadoMs = ahoraMs;
    return true;
}

} // namespace ba::evento::linea
