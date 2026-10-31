// src/app/evento/LineaVista.cpp — ver LineaVista.hpp
#include "evento/LineaVista.hpp"

#include "evento/LeerJson.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace ba::evento::linea {

using nlohmann::json;

namespace {

/// Las pistas de una salida van en el orden de 3.2: primero la escena.
int ordenDeTipo(const std::string& tipo) {
    static const char* const orden[] = {"escena", "graficos", "dsk", "audio", "medios", "asamblea", "sala",
                                        "prompter", "pantallas", "ptz", "asistente", "notas"};
    for (int i = 0; i < static_cast<int>(std::size(orden)); ++i)
        if (tipo == orden[i]) return i;
    return 99;
}

/// Cómo entra un paso, para la columna «entrada»: el modo de su inicio, salvo el GO
/// que en realidad entra solo porque el anterior sigue (el `automatico` del resolvedor).
const char* entradaDe(const Paso& p, const PasoResuelto& pr) {
    switch (p.inicio.modo) {
        case ModoInicio::Hora: return "hora";
        case ModoInicio::Cuando: return "cuando";
        case ModoInicio::Ancla: return "ancla";
        case ModoInicio::Tras:
        case ModoInicio::Con: return "auto";
        case ModoInicio::Go: return pr.automatico ? "auto" : "go";
    }
    return "go";
}

/// Lo que se lee de una pieza en un chip: su texto o, sin texto, lo principal de sus datos.
std::string textoDe(const Pieza& z) {
    if (!z.texto.empty()) return z.texto;
    for (const char* k : {"grafico", "escena", "archivo", "nombre", "herramienta", "texto"})
        if (const std::string t = leerTexto(z.datos, k); !t.empty()) return t;
    return z.tipo;
}

/// Lo real de un paso: lo que estuvo (o está) al aire la última vez que entró.
const Instancia* instanciaDe(const ContextoVista& ctx, const std::string& salida, const std::string& paso) {
    const Instancia* out = nullptr;
    for (const Instancia& i : ctx.instancias)
        if (i.salida == salida && i.paso == paso) out = &i;
    return out;
}

json horaJ(const ContextoVista& ctx, int64_t ms) {
    if (ms == kNunca) return nullptr;
    return ctx.hora ? json(ctx.hora(ms)) : json(ms);
}

json miniJ(const ContextoVista& ctx, const std::string& escena, FuenteMini f) {
    if (ctx.miniatura) return ctx.miniatura(escena, f);
    return {{"fuente", nombreDe(f)}, {"escena", escena}};
}

bool editableJ(const ContextoVista& ctx, const std::string& paso, json& j) {
    std::string porque;
    const bool ok = !ctx.editable || ctx.editable(paso, porque);
    j["editable"] = ok;
    if (!ok) j["porque"] = porque;
    return ok;
}

/// Los chips de un paso: sus piezas menos la escena de la salida (esa es la caja).
json chipsDe(const Linea& l, const Paso& p, const std::string& salida, const ContextoVista& ctx, bool alAire) {
    json out = json::array();
    for (const Pieza& z : p.piezas) {
        const Pista* pi = l.pista(z.pista);
        if (z.tipo == "escena" && (!pi || pi->salida.empty() || pi->salida == salida)) continue;
        out.push_back({{"id", z.id},
                       {"tipo", z.tipo},
                       {"texto", textoDe(z)},
                       {"manual", z.desde.modo == Ancla::Modo::Manual},
                       {"disparada", alAire && ctx.disparadas.count(z.id) > 0}});
    }
    return out;
}

} // namespace

std::string escenaDe(const Linea& l, const Paso& p, const std::string& salida) {
    for (const Pieza& z : p.piezas) {
        if (z.tipo != "escena") continue;
        const Pista* pi = l.pista(z.pista);
        if (pi && !pi->salida.empty() && pi->salida != salida) continue;
        if (std::string e = leerTexto(z.datos, "escena"); !e.empty()) return e;
    }
    return {};
}

const char* nombreDe(FuenteMini f) {
    switch (f) {
        case FuenteMini::Programa: return "programa";
        case FuenteMini::Pvw: return "pvw";
        case FuenteMini::Viva: return "viva";
        case FuenteMini::Foto: return "foto";
        case FuenteMini::Ninguna: break;
    }
    return "";
}

std::map<std::string, FuenteMini> presupuestoMiniaturas(const Linea& l, const SalidaResuelta& sr, int vivas) {
    std::map<std::string, FuenteMini> out;
    const auto escena = [&](const PasoResuelto& pr) {
        const Paso* p = l.paso(pr.paso);
        return p ? escenaDe(l, *p, sr.salida) : std::string();
    };
    // Primero lo que ya existe: el programa y el PVW.
    for (const PasoResuelto& pr : sr.pasos) {
        if (pr.estado != EstadoPaso::AlAire && pr.estado != EstadoPaso::Proximo) continue;
        const std::string e = escena(pr);
        if (e.empty()) continue;
        FuenteMini& f = out[e];
        if (f == FuenteMini::Ninguna || pr.estado == EstadoPaso::AlAire)
            f = pr.estado == EstadoPaso::AlAire ? FuenteMini::Programa : FuenteMini::Pvw;
    }
    // Las que siguen: las primeras `vivas` escenas nuevas, vivas; el resto (y lo que ya pasó), fotos.
    int quedan = vivas;
    for (const PasoResuelto& pr : sr.pasos) {
        const std::string e = escena(pr);
        if (e.empty() || out.count(e)) continue;
        if (pr.estado == EstadoPaso::Futuro && quedan > 0) {
            out[e] = FuenteMini::Viva;
            --quedan;
        } else {
            out[e] = FuenteMini::Foto;
        }
    }
    return out;
}

FuenteMini fuenteDe(const PasoResuelto& pr, const std::string& escena, const std::map<std::string, FuenteMini>& presupuesto) {
    if (pr.estado == EstadoPaso::AlAire) return FuenteMini::Programa;
    if (escena.empty()) return FuenteMini::Ninguna;
    if (pr.estado == EstadoPaso::Proximo) return FuenteMini::Pvw;
    const auto it = presupuesto.find(escena);
    return it == presupuesto.end() ? FuenteMini::Foto : it->second;
}

// --- La escaleta ------------------------------------------------------------------------

json escaleta(const Linea& l, const Resolucion& r, const ContextoVista& ctx) {
    json out = json::array();
    const SalidaResuelta* sr = r.salida(ctx.salida);
    if (sr == nullptr) return out;
    const auto presupuesto = presupuestoMiniaturas(l, *sr);
    int numero = 0;
    for (const Bloque& b : l.bloques) {
        json pasos = json::array();
        for (const Paso& p : b.pasos) {
            const PasoResuelto* pr = sr->paso(p.id);
            if (pr == nullptr) continue; // no sale por esta salida
            const std::string escena = escenaDe(l, p, ctx.salida);
            const bool alAire = pr->estado == EstadoPaso::AlAire;
            json jp{{"id", p.id},
                    {"numero", ++numero},
                    {"nombre", p.nombre},
                    {"bloque", b.id},
                    {"estado", nombreDe(pr->estado)},
                    {"entra", entradaDe(p, *pr)},
                    {"frente", horaJ(ctx, pr->frenteMs)},
                    {"fin", horaJ(ctx, pr->finMs)},
                    {"previstoSeg", p.duracionPlanMs() / 1000},
                    {"tarde", pr->tarde},
                    {"estimado", pr->estimado},
                    {"hold", pr->enHold},
                    {"escena", escena},
                    {"mini", miniJ(ctx, escena, fuenteDe(*pr, escena, presupuesto))},
                    {"chips", chipsDe(l, p, ctx.salida, ctx, alAire)},
                    {"guion", p.guion},
                    {"nota", p.notaOperador},
                    {"pendiente", ctx.pendientes.count(p.id) > 0}};
            if (p.inicio.modo == ModoInicio::Hora) {
                jp["entraHora"] = p.inicio.hora;
                jp["politica"] = nombreDe(p.inicio.politica);
            }
            if (p.inicio.modo == ModoInicio::Ancla) jp["entraDe"] = p.inicio.salida + "/" + p.inicio.paso;
            if (p.inicio.modo == ModoInicio::Cuando) jp["entraDe"] = p.inicio.condicion;
            if (pr->backMs) jp["back"] = horaJ(ctx, *pr->backMs);
            // Lo real: lo que estuvo (o lleva) al aire; el over, contra lo previsto.
            int64_t over = 0;
            if (const Instancia* i = instanciaDe(ctx, ctx.salida, p.id)) {
                const int64_t real = (i->hastaMs > 0 ? i->hastaMs : ctx.ahoraMs) - i->desdeMs;
                jp["realSeg"] = real / 1000;
                over = real - p.duracionPlanMs();
            }
            jp["overSeg"] = over > 0 ? over / 1000 : 0;
            editableJ(ctx, p.id, jp);
            pasos.push_back(std::move(jp));
        }
        if (pasos.empty()) continue;
        json jb{{"id", b.id}, {"nombre", b.nombre}, {"pasos", std::move(pasos)}, {"pendiente", ctx.pendientes.count(b.id) > 0}};
        for (const BloqueResuelto& br : sr->bloques)
            if (br.bloque == b.id) {
                jb["desde"] = horaJ(ctx, br.desdeMs);
                jb["hasta"] = horaJ(ctx, br.hastaMs);
                jb["presupuestoSeg"] = br.presupuestoMs / 1000;
                jb["overUnderSeg"] = br.overUnderMs / 1000;
            }
        out.push_back(std::move(jb));
    }
    return out;
}

// --- La conducción ---------------------------------------------------------------------

json conduccion(const Linea& l, const Resolucion& r, const ContextoVista& ctx, json estado) {
    if (!estado.is_object()) estado = json::object();
    const SalidaResuelta* sr = r.salida(ctx.salida);
    if (sr == nullptr) return estado;
    const auto presupuesto = presupuestoMiniaturas(l, *sr);
    if (const PasoResuelto* pr = sr->paso(sr->alAire); pr && estado["alAire"].is_object()) {
        json& a = estado["alAire"];
        if (const Paso* p = l.paso(pr->paso)) {
            const std::string escena = escenaDe(l, *p, ctx.salida);
            a["escena"] = escena;
            a["mini"] = miniJ(ctx, escena, FuenteMini::Programa);
            a["guion"] = p->guion;
            a["nota"] = p->notaOperador;
            a["pasado"] = leerNumero(a, "restanteSeg") < 0;
            json adlibs = json::array();
            for (const json& c : chipsDe(l, *p, ctx.salida, ctx, true))
                if (c.value("manual", false)) adlibs.push_back(c);
            a["adlibs"] = std::move(adlibs);
            if (const Bloque* b = l.bloqueDe(p->id))
                for (const BloqueResuelto& br : sr->bloques)
                    if (br.bloque == b->id)
                        estado["bloque"] = {{"id", b->id},
                                            {"nombre", b->nombre},
                                            {"presupuestoSeg", br.presupuestoMs / 1000},
                                            {"usadoSeg", std::max<int64_t>(0, ctx.ahoraMs - br.desdeMs) / 1000},
                                            {"overUnderSeg", br.overUnderMs / 1000}};
        }
    }
    if (const PasoResuelto* pr = sr->paso(sr->proximo); pr && estado["proximo"].is_object()) {
        json& x = estado["proximo"];
        if (const Paso* p = l.paso(pr->paso)) {
            const std::string escena = escenaDe(l, *p, ctx.salida);
            x["escena"] = escena;
            x["mini"] = miniJ(ctx, escena, fuenteDe(*pr, escena, presupuesto));
            x["entra"] = entradaDe(*p, *pr);
            x["faltaSeg"] = std::max<int64_t>(0, pr->frenteMs - ctx.ahoraMs) / 1000;
            x["previstoSeg"] = p->duracionPlanMs() / 1000;
        }
    }
    if (estado["proximoHardStart"].is_object()) {
        json& h = estado["proximoHardStart"];
        const int64_t gap = leerNumero(h, "gapSeg");
        h["nivel"] = gap < 0 ? "overlap" : gap < 60 ? "justo" : "sobra";
        if (const Paso* p = l.paso(leerTexto(h, "paso"))) h["nombre"] = p->nombre;
    }
    return estado;
}

// --- El estante -------------------------------------------------------------------------

namespace {
/// La tecla de cada ítem: la suya, o F1…F8 a los primeros ocho que no tienen.
std::vector<std::string> teclasDelEstante(const Linea& l) {
    std::set<std::string> usadas;
    for (const ItemEstante& it : l.estante)
        if (!it.tecla.empty()) usadas.insert(it.tecla);
    std::vector<std::string> out;
    int f = 1;
    for (const ItemEstante& it : l.estante) {
        if (!it.tecla.empty()) {
            out.push_back(it.tecla);
            continue;
        }
        while (f <= 8 && usadas.count("F" + std::to_string(f))) ++f;
        out.push_back(f <= 8 ? "F" + std::to_string(f++) : std::string());
    }
    return out;
}
} // namespace

json estante(const Linea& l) {
    json out = json::array();
    const std::vector<std::string> teclas = teclasDelEstante(l);
    for (std::size_t i = 0; i < l.estante.size(); ++i) {
        const ItemEstante& it = l.estante[i];
        std::string texto = it.boton, tipo;
        if (it.pieza) {
            if (texto.empty()) texto = textoDe(*it.pieza);
            tipo = it.pieza->tipo;
        } else if (it.paso) {
            if (texto.empty()) texto = it.paso->nombre;
            tipo = "paso";
        }
        out.push_back({{"id", it.id}, {"texto", texto.empty() ? it.id : texto}, {"tipo", tipo}, {"color", it.color},
                       {"tecla", teclas[i]}, {"esPaso", it.paso.has_value()}});
    }
    return out;
}

std::string itemDeTecla(const Linea& l, const std::string& tecla) {
    const std::vector<std::string> teclas = teclasDelEstante(l);
    for (std::size_t i = 0; i < teclas.size(); ++i)
        if (!tecla.empty() && teclas[i] == tecla) return l.estante[i].id;
    return {};
}

// --- La Línea multipista -----------------------------------------------------------------

double Ventana::x(int64_t ms) const {
    if (largo() <= 0) return 0;
    return static_cast<double>(ms - desdeMs) / static_cast<double>(largo());
}

int64_t Ventana::ms(double x) const { return desdeMs + static_cast<int64_t>(std::llround(x * static_cast<double>(largo()))); }

json regla(const Ventana& v, int64_t medianocheMs, const Hora& hora) {
    json out = json::array();
    if (v.largo() <= 0) return out;
    // El paso más chico que deja a lo sumo ~10 marcas con hora.
    static const int64_t pasos[] = {1000, 5000, 10000, 15000, 30000, 60000, 120000, 300000, 600000, 900000, 1800000, 3600000};
    int64_t paso = pasos[std::size(pasos) - 1];
    for (int64_t p : pasos)
        if (v.largo() / p <= 10) {
            paso = p;
            break;
        }
    const int64_t menor = paso / 2;
    // Alineadas a la medianoche del evento: caen en las horas redondas de su huso.
    const int64_t desde = v.desdeMs - medianocheMs;
    int64_t t = medianocheMs + (desde >= 0 ? (desde + menor - 1) / menor : -((-desde) / menor)) * menor;
    for (; t <= v.hastaMs; t += menor) {
        const bool mayor = (t - medianocheMs) % paso == 0;
        json m{{"x", v.x(t)}, {"mayor", mayor}};
        if (mayor) {
            std::string h = hora ? hora(t) : std::to_string(t);
            // Con marcas de un minuto o más, los segundos sobran.
            if (paso >= 60000 && h.size() == 8) h = h.substr(0, 5);
            m["texto"] = h;
        }
        out.push_back(std::move(m));
    }
    return out;
}

json multipista(const Linea& l, const Resolucion& r, const ContextoVista& ctx, const Ventana& v, int64_t medianocheMs) {
    const auto seCruza = [&](int64_t a, int64_t b) { return b >= v.desdeMs && a <= v.hastaMs; };
    // Hasta dónde se dibuja algo que no termina: un poco más allá del borde.
    const int64_t borde = v.hastaMs + v.largo();
    const auto hasta = [&](int64_t ms) { return std::min(ms, borde); };
    std::set<std::pair<std::string, std::string>> enConflicto;
    for (const Conflicto& c : r.conflictos) {
        enConflicto.insert({c.salida, c.pieza});
        enConflicto.insert({c.otraSalida, c.otraPieza});
    }
    json out{{"ahora", v.x(ctx.ahoraMs)}, {"regla", regla(v, medianocheMs, ctx.hora)}};
    json hardStarts = json::array();
    json salidas = json::array();
    int64_t finPlaneado = 0;
    // Las pistas de cada salida, en el orden de 3.2; las globales aparte.
    std::vector<const Pista*> pistas;
    for (const Pista& p : l.pistas)
        if (!p.oculta) pistas.push_back(&p);
    std::stable_sort(pistas.begin(), pistas.end(), [](const Pista* a, const Pista* b) { return ordenDeTipo(a->tipo) < ordenDeTipo(b->tipo); });

    const auto bloqueDePieza = [&](const PiezaResuelta& z, const std::string& salida) -> json {
        const Paso* p = l.paso(z.paso);
        const Pieza* m = nullptr;
        if (p)
            for (const Pieza& x : p->piezas)
                if (x.id == z.pieza) m = &x;
        const SalidaResuelta* sr = r.salida(salida.empty() ? z.salida : salida);
        const PasoResuelto* pr = sr ? sr->paso(z.paso) : nullptr;
        json j{{"id", z.pieza},
               {"paso", z.paso},
               {"tipo", m ? m->tipo : std::string()},
               {"texto", m ? textoDe(*m) : z.pieza},
               {"vida", m ? nombreDe(m->vida) : "paso"},
               {"x0", v.x(z.desdeMs)},
               {"x1", v.x(hasta(z.hastaMs))},
               {"abierta", z.hastaMs == kNunca || z.hastaAbierto},
               // «sigue»: vive más allá de su paso (el borde lo dice).
               {"cruza", pr && z.hastaMs > pr->finMs},
               {"emitida", z.emitida},
               {"estimado", z.estimado},
               {"conflicto", enConflicto.count({z.salida, z.pieza}) > 0},
               {"pendiente", ctx.pendientes.count(z.pieza) > 0}};
        editableJ(ctx, z.paso, j);
        return j;
    };

    for (const SalidaResuelta& sr : r.salidas) {
        finPlaneado = std::max(finPlaneado, sr.finPlaneadoMs);
        const auto presupuesto = presupuestoMiniaturas(l, sr);
        for (const HardStart& h : sr.hardStarts) {
            if (h.horaMs < v.desdeMs || h.horaMs > v.hastaMs) continue;
            const Paso* p = l.paso(h.paso);
            hardStarts.push_back({{"x", v.x(h.horaMs)},
                                  {"salida", sr.salida},
                                  {"paso", h.paso},
                                  {"nombre", p ? p->nombre : h.paso},
                                  {"hora", horaJ(ctx, h.horaMs)},
                                  {"gapSeg", h.gapMs / 1000},
                                  {"nivel", h.gapMs < 0 ? "overlap" : h.gapMs < 60000 ? "justo" : "sobra"},
                                  {"politica", nombreDe(h.politica)}});
        }
        json carriles = json::array();
        bool hayEscena = false;
        for (const Pista* pi : pistas) {
            if (pi->salida != sr.salida) continue;
            json c{{"id", pi->id}, {"tipo", pi->tipo}, {"nombre", pi->nombre.empty() ? pi->tipo : pi->nombre}, {"color", pi->color}};
            json items = json::array();
            if (pi->tipo == "escena" && !hayEscena) {
                hayEscena = true;
                c["escena"] = true;
                // Las cajas de los pasos, con lo emitido sólido y el fantasma de lo previsto.
                for (const PasoResuelto& pr : sr.pasos) {
                    if (pr.estado == EstadoPaso::Salteado || !seCruza(pr.frenteMs, pr.finMs)) continue;
                    const Paso* p = l.paso(pr.paso);
                    if (p == nullptr) continue;
                    const std::string escena = escenaDe(l, *p, sr.salida);
                    const int64_t plan = p->duracionPlanMs();
                    const bool emitido = pr.estado == EstadoPaso::AlAire || pr.estado == EstadoPaso::Pasado;
                    json j{{"id", p->id},
                           {"paso", p->id},
                           {"nombre", p->nombre},
                           {"estado", nombreDe(pr.estado)},
                           {"entra", entradaDe(*p, pr)},
                           {"x0", v.x(pr.frenteMs)},
                           {"x1", v.x(hasta(pr.finMs))},
                           {"frente", horaJ(ctx, pr.frenteMs)},
                           {"previstoSeg", plan / 1000},
                           {"emitido", emitido},
                           {"hold", pr.enHold},
                           {"tarde", pr.tarde},
                           {"estimado", pr.estimado},
                           {"escena", escena},
                           {"mini", miniJ(ctx, escena, fuenteDe(pr, escena, presupuesto))},
                           {"pendiente", ctx.pendientes.count(p->id) > 0}};
                    if (p->inicio.modo == ModoInicio::Hora) j["entraHora"] = p->inicio.hora;
                    if (emitido) {
                        const int64_t real = pr.finMs - pr.frenteMs;
                        j["realSeg"] = real / 1000;
                        // El fantasma: el largo previsto, debajo de la caja real.
                        j["xPrevisto"] = v.x(pr.frenteMs + plan);
                        if (real > plan && plan > 0) j["over"] = conSigno((real - plan) / 1000);
                    }
                    json manuales = json::array();
                    for (const Pieza& z : p->piezas)
                        if (z.desde.modo == Ancla::Modo::Manual)
                            manuales.push_back({{"id", z.id},
                                                {"texto", textoDe(z)},
                                                {"tipo", z.tipo},
                                                {"disparada", pr.estado == EstadoPaso::AlAire && sr.salida == ctx.salida &&
                                                                  ctx.disparadas.count(z.id) > 0}});
                    j["manuales"] = std::move(manuales);
                    bool conflicto = false;
                    for (const Pieza& z : p->piezas) conflicto = conflicto || enConflicto.count({sr.salida, z.id}) > 0;
                    j["conflicto"] = conflicto;
                    editableJ(ctx, p->id, j);
                    items.push_back(std::move(j));
                }
            } else {
                for (const PiezaResuelta& z : r.piezas)
                    if (z.pista == pi->id && z.salida == sr.salida && seCruza(z.desdeMs, hasta(z.hastaMs)))
                        items.push_back(bloqueDePieza(z, sr.salida));
            }
            c["items"] = std::move(items);
            carriles.push_back(std::move(c));
        }
        salidas.push_back({{"salida", sr.salida}, {"carriles", std::move(carriles)}});
    }
    // Las globales: una vez, con lo de todas las salidas.
    json globales = json::array();
    for (const Pista* pi : pistas) {
        if (!pi->salida.empty()) continue;
        json items = json::array();
        std::set<std::string> vistas; // una pieza compartida está en cada salida: va una vez
        for (const PiezaResuelta& z : r.piezas)
            if (z.pista == pi->id && seCruza(z.desdeMs, hasta(z.hastaMs)) && vistas.insert(z.paso + "/" + z.pieza).second)
                items.push_back(bloqueDePieza(z, {}));
        globales.push_back(
            {{"id", pi->id}, {"tipo", pi->tipo}, {"nombre", pi->nombre.empty() ? pi->tipo : pi->nombre}, {"color", pi->color}, {"items", std::move(items)}});
    }
    out["hardStarts"] = std::move(hardStarts);
    out["salidas"] = std::move(salidas);
    out["globales"] = std::move(globales);
    if (finPlaneado > 0 && finPlaneado >= v.desdeMs && finPlaneado <= v.hastaMs) out["finPlaneado"] = v.x(finPlaneado);
    return out;
}

std::string conSigno(int64_t seg) {
    const char* signo = seg < 0 ? "-" : "+";
    const int64_t s = std::llabs(seg);
    const int64_t h = s / 3600, m = (s % 3600) / 60, x = s % 60;
    char buf[32];
    if (h > 0)
        std::snprintf(buf, sizeof buf, "%s%lld:%02lld:%02lld", signo, static_cast<long long>(h), static_cast<long long>(m),
                      static_cast<long long>(x));
    else
        std::snprintf(buf, sizeof buf, "%s%lld:%02lld", signo, static_cast<long long>(m), static_cast<long long>(x));
    return buf;
}

} // namespace ba::evento::linea
