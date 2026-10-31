// ============================================================================
// src/app/evento/LineaEdicion.cpp — ver LineaEdicion.hpp
// ============================================================================
#include "evento/LineaEdicion.hpp"

#include "evento/LeerJson.hpp"
#include "i18n/I18n.hpp"

#include <algorithm>
#include <map>
#include <set>

namespace ba::evento::linea {
namespace {

using nlohmann::json;
namespace v1 = ba::evento;
using ba::i18n::llenar;
using ba::i18n::traducir;

bool terminaEn(const std::string& s, const std::string& fin) {
    return s.size() >= fin.size() && s.compare(s.size() - fin.size(), fin.size(), fin) == 0;
}

// --- La proyección ---------------------------------------------------------------------

/// ¿La primera pieza abre el paso? Una escena, un orador o un video que entra con
/// el paso: es lo que en el v1 era el cue que abría el paso (y lleva su id).
bool tieneCabeza(const Paso& p) {
    if (p.piezas.empty()) return false;
    const Pieza& z = p.piezas.front();
    return (z.tipo == "escena" || z.tipo == "orador" || z.tipo == "video") && z.desde.modo == Ancla::Modo::Ms && z.desde.ms == 0;
}

/// El tipo v1 con que se ve una pieza: el suyo si el v1 lo conoce y no abre paso
/// (una escena que no es la del principio abriría otro paso al migrar); si no,
/// nota. La pieza no pierde su tipo: reconciliar() se lo devuelve.
v1::Tipo tipoVisto(const Pieza& z) {
    v1::Tipo t{};
    if (!v1::tipoDeNombre(z.tipo, t) || abrePaso(t)) return v1::Tipo::Nota;
    return t;
}

struct CueVisto {
    v1::Cue cue;
    RefCue ref;
    int64_t offset = 0;
    bool anclada = true; ///< entra sola (no espera el clic)
};

std::vector<CueVisto> cuesDePaso(const Paso& p, bool primeroDelBloque) {
    std::vector<CueVisto> v;
    const bool cabeza = tieneCabeza(p);
    if (cabeza) {
        const Pieza& z = p.piezas.front();
        CueVisto c;
        c.cue.id = p.id;
        v1::tipoDeNombre(z.tipo, c.cue.tipo);
        c.cue.texto = z.texto.empty() ? p.nombre : z.texto;
        c.cue.datos = z.datos;
        c.ref = {p.id, z.id};
        v.push_back(std::move(c));
    } else if (!(primeroDelBloque && terminaEn(p.id, "-inicio") && !p.piezas.empty())) {
        // El paso mismo es el cue: una espera si espera a la persona (abierto, o
        // con algo que decirle), una pausa si no (el «<bloque>-inicio» de la
        // migración no tiene cue propio).
        CueVisto c;
        c.cue.id = p.id;
        c.cue.tipo = p.fin.modo == ModoFin::Abierto || !p.notaOperador.empty() ? v1::Tipo::Espera : v1::Tipo::Pausa;
        c.cue.texto = p.nombre;
        c.ref = {p.id, {}};
        v.push_back(std::move(c));
    }
    int64_t ultimo = 0;
    for (std::size_t i = cabeza ? 1 : 0; i < p.piezas.size(); ++i) {
        const Pieza& z = p.piezas[i];
        CueVisto c;
        c.cue.id = z.id;
        c.cue.tipo = tipoVisto(z);
        // Una que se ve como nota sin decir nada (una PTZ, una del asistente) dice qué
        // es: una nota muda es un reparo grave del guion de cues.
        c.cue.texto = z.texto.empty() && c.cue.tipo == v1::Tipo::Nota && z.tipo != "nota" ? z.tipo : z.texto;
        c.cue.datos = z.datos;
        c.ref = {p.id, z.id};
        // Las que no tienen lugar fijo (antes del fin, cuando pase algo) van donde la anterior.
        const bool conLugar = z.desde.modo == Ancla::Modo::Ms || z.desde.modo == Ancla::Modo::Manual;
        c.offset = conLugar ? z.desde.ms : ultimo;
        ultimo = c.offset;
        c.anclada = z.desde.modo != Ancla::Modo::Manual;
        v.push_back(std::move(c));
    }
    const int64_t dur = p.duracionPlanMs();
    for (std::size_t k = 0; k < v.size(); ++k) {
        const bool ultimoCue = k + 1 == v.size();
        const int64_t fin = ultimoCue ? dur : v[k + 1].offset;
        v[k].cue.duracionSeg = static_cast<int>(std::max<int64_t>(0, fin - v[k].offset) / 1000);
        v[k].cue.automatico = ultimoCue ? p.continuar != Continuar::No : v[k + 1].anclada;
    }
    return v;
}

/// Los pasos que va a armar migrar() con los cues de un bloque v1: {id, índices de sus cues}.
std::vector<std::pair<std::string, std::vector<std::size_t>>> pasosDeV1(const v1::Bloque& b) {
    std::vector<std::pair<std::string, std::vector<std::size_t>>> g;
    for (std::size_t i = 0; i < b.cues.size(); ++i) {
        const v1::Cue& c = b.cues[i];
        if (abrePaso(c.tipo) || g.empty()) g.push_back({abrePaso(c.tipo) ? c.id : b.id + "-inicio", {}});
        g.back().second.push_back(i);
    }
    return g;
}

bool igual(const v1::Cue& a, const v1::Cue& b) {
    return a.id == b.id && a.tipo == b.tipo && a.texto == b.texto && a.datos == b.datos && a.duracionSeg == b.duracionSeg &&
           a.automatico == b.automatico;
}

bool iguales(const std::vector<const v1::Cue*>& a, const std::vector<const v1::Cue*>& b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (!igual(*a[i], *b[i])) return false;
    return true;
}

const v1::Cue* porId(const std::vector<const v1::Cue*>& v, const std::string& id) {
    for (const v1::Cue* c : v)
        if (c->id == id) return c;
    return nullptr;
}

} // namespace

std::pair<int, int> Proyeccion::cueDe(const std::string& paso, const std::string& pieza) const {
    for (std::size_t b = 0; b < refs.size(); ++b)
        for (std::size_t c = 0; c < refs[b].size(); ++c)
            if (refs[b][c].paso == paso && (pieza.empty() || refs[b][c].pieza == pieza))
                return {static_cast<int>(b), static_cast<int>(c)};
    return {-1, -1};
}

const RefCue* Proyeccion::ref(int bloque, int cue) const {
    if (bloque < 0 || cue < 0 || bloque >= static_cast<int>(refs.size())) return nullptr;
    const auto& fila = refs[static_cast<std::size_t>(bloque)];
    return cue < static_cast<int>(fila.size()) ? &fila[static_cast<std::size_t>(cue)] : nullptr;
}

Proyeccion proyectar(const Linea& l) {
    Proyeccion pr;
    for (const Bloque& b : l.bloques) {
        v1::Bloque vb;
        vb.id = b.id;
        vb.nombre = b.nombre;
        vb.nota = b.nota;
        std::vector<RefCue> fila;
        for (std::size_t i = 0; i < b.pasos.size(); ++i)
            for (CueVisto& c : cuesDePaso(b.pasos[i], i == 0)) {
                vb.cues.push_back(std::move(c.cue));
                fila.push_back(std::move(c.ref));
            }
        pr.bloques.push_back(std::move(vb));
        pr.refs.push_back(std::move(fila));
    }
    return pr;
}

Linea reconciliar(const Linea& vieja, const std::vector<v1::Bloque>& v1b) {
    v1::Evento tmp;
    tmp.bloques = v1b;
    Linea n = migrar(tmp);
    const Proyeccion pv = proyectar(vieja);

    // Lo viejo: los cues de cada paso, el cue de cada pieza y el cue propio de cada paso.
    std::map<std::string, std::vector<const v1::Cue*>> cuesViejos;
    std::map<std::string, const v1::Cue*> cueDePieza;
    std::map<std::string, const Pieza*> piezaVieja;
    for (std::size_t b = 0; b < pv.bloques.size(); ++b)
        for (std::size_t c = 0; c < pv.bloques[b].cues.size(); ++c) {
            const RefCue& r = pv.refs[b][c];
            cuesViejos[r.paso].push_back(&pv.bloques[b].cues[c]);
            if (!r.pieza.empty()) cueDePieza.emplace(r.pieza, &pv.bloques[b].cues[c]);
        }
    for (const Bloque& b : vieja.bloques)
        for (const Paso& p : b.pasos)
            for (const Pieza& z : p.piezas)
                piezaVieja.emplace(z.id, &z);
    // Lo nuevo: los cues de cada paso, como los agrupa migrar().
    std::map<std::string, std::vector<const v1::Cue*>> cuesNuevos;
    for (const v1::Bloque& b : v1b)
        for (const auto& [paso, idx] : pasosDeV1(b))
            for (std::size_t i : idx)
                cuesNuevos[paso].push_back(&b.cues[i]);

    for (Bloque& nb : n.bloques) {
        for (const Bloque& ob : vieja.bloques) {
            if (ob.id != nb.id) continue;
            nb.presupuestoSeg = ob.presupuestoSeg;
            nb.inicio = ob.inicio;
            nb.plantilla = ob.plantilla;
            nb.origen = ob.origen;
            break;
        }
        for (Paso& np : nb.pasos) {
            const Paso* op = vieja.paso(np.id);
            if (op == nullptr) continue; // nuevo: tal como lo migró
            const auto& cv = cuesViejos[np.id];
            const auto& cn = cuesNuevos[np.id];
            if (iguales(cv, cn)) {
                np = *op; // no se tocó: vuelve exactamente como estaba
                continue;
            }
            Paso r = *op;
            r.piezas.clear();
            const v1::Cue* propioViejo = porId(cv, op->id);
            const bool cabezaVieja = tieneCabeza(*op);
            for (std::size_t k = 0; k < np.piezas.size(); ++k) {
                const Pieza& z = np.piezas[k];
                // La cabeza migrada se llama «<paso>-<tipo>»; la vieja puede tener otro id.
                const bool cabeza = k == 0 && tieneCabeza(np) && z.id == np.id + "-" + z.tipo;
                const v1::Cue* nc = porId(cn, cabeza ? np.id : z.id);
                const v1::Cue* oc = nullptr;
                const Pieza* oz = nullptr;
                if (cabeza) {
                    if (cabezaVieja) {
                        oz = &op->piezas.front();
                        oc = propioViejo;
                    }
                } else {
                    const auto a = piezaVieja.find(z.id);
                    const auto b = cueDePieza.find(z.id);
                    if (a != piezaVieja.end() && b != cueDePieza.end()) {
                        oz = a->second;
                        oc = b->second;
                    }
                }
                if (oz == nullptr || oc == nullptr || nc == nullptr || nc->tipo != oc->tipo) {
                    r.piezas.push_back(z); // nueva, o cambió de tipo: como la migró
                    continue;
                }
                Pieza w = *oz;
                if (!igual(*nc, *oc)) {
                    // Solo lo que cambió: lo demás queda como estaba en la línea.
                    if (nc->texto != oc->texto) w.texto = z.texto;
                    if (nc->datos != oc->datos) w.datos = z.datos;
                    // La vida y la duración solo siguen al v1 si eran las que da el v1.
                    Pieza derivada;
                    vidaDeCue(*oc, derivada);
                    if (oz->vida == derivada.vida) w.vida = z.vida;
                    if (oz->dura.aJson() == derivada.dura.aJson()) w.dura = z.dura;
                }
                // El lugar en el paso es del v1 (las duraciones de antes pueden haber cambiado).
                if (oz->desde.modo == Ancla::Modo::Ms || oz->desde.modo == Ancla::Modo::Manual) w.desde = z.desde;
                r.piezas.push_back(std::move(w));
            }
            r.duracionPrevistaSeg = np.duracionPrevistaSeg;
            if (op->fin.modo == ModoFin::Duracion) r.fin = np.fin;
            if (propioViejo != nullptr && op->nombre == propioViejo->texto) r.nombre = np.nombre;
            const std::string notaDerivada =
                propioViejo != nullptr && propioViejo->tipo == v1::Tipo::Espera ? propioViejo->texto : std::string();
            if (op->notaOperador == notaDerivada) r.notaOperador = np.notaOperador;
            // Lo que hace con el siguiente es del último cue: si su «automático» no
            // cambió, queda lo de la línea (un «con» o un «sigue» de una pausa).
            if (cv.empty() || cn.empty() || cv.back()->automatico != cn.back()->automatico) r.continuar = np.continuar;
            np = std::move(r);
        }
    }

    // Lo que es de la línea entera no lo dice el v1.
    std::vector<Pista> migradas = std::move(n.pistas);
    n.pistas = vieja.pistas;
    std::set<std::string> usadas;
    for (const Bloque& b : n.bloques)
        for (const Paso& p : b.pasos)
            for (const Pieza& z : p.piezas)
                usadas.insert(z.pista);
    for (Pista& p : migradas)
        if (usadas.count(p.id) && vieja.pista(p.id) == nullptr) n.pistas.push_back(std::move(p));
    if (!vieja.salidas.empty()) n.salidas = vieja.salidas;
    n.estante = vieja.estante;
    n.disparadores = vieja.disparadores;
    n.plantillas = vieja.plantillas;
    return n;
}

// --- Editar ---------------------------------------------------------------------------

namespace {

struct Lugar {
    enum class Que { Nada, Bloque, Paso, Pieza, Pista, Estante, Disparador, Plantilla };
    Que que = Que::Nada;
    std::size_t b = 0, p = 0, z = 0;
};

Lugar buscar(const Linea& l, const std::string& id) {
    for (std::size_t b = 0; b < l.bloques.size(); ++b)
        for (std::size_t p = 0; p < l.bloques[b].pasos.size(); ++p)
            if (l.bloques[b].pasos[p].id == id) return {Lugar::Que::Paso, b, p, 0};
    for (std::size_t b = 0; b < l.bloques.size(); ++b)
        for (std::size_t p = 0; p < l.bloques[b].pasos.size(); ++p)
            for (std::size_t z = 0; z < l.bloques[b].pasos[p].piezas.size(); ++z)
                if (l.bloques[b].pasos[p].piezas[z].id == id) return {Lugar::Que::Pieza, b, p, z};
    for (std::size_t b = 0; b < l.bloques.size(); ++b)
        if (l.bloques[b].id == id) return {Lugar::Que::Bloque, b, 0, 0};
    for (std::size_t i = 0; i < l.pistas.size(); ++i)
        if (l.pistas[i].id == id) return {Lugar::Que::Pista, i, 0, 0};
    for (std::size_t i = 0; i < l.estante.size(); ++i)
        if (l.estante[i].id == id) return {Lugar::Que::Estante, i, 0, 0};
    for (std::size_t i = 0; i < l.disparadores.size(); ++i)
        if (l.disparadores[i].id == id) return {Lugar::Que::Disparador, i, 0, 0};
    for (std::size_t i = 0; i < l.plantillas.size(); ++i)
        if (l.plantillas[i].id == id) return {Lugar::Que::Plantilla, i, 0, 0};
    return {};
}

/// Dónde insertar en una lista por `despuesDe`: sin la clave, al final; "" al
/// principio; un id, después de ese. -1 si ese id no está.
template <class T>
long posicion(const std::vector<T>& v, const json& op) {
    const auto it = op.find("despuesDe");
    if (it == op.end() || it->is_null()) return static_cast<long>(v.size());
    const std::string id = it->is_string() ? it->get<std::string>() : std::string();
    if (id.empty()) return 0;
    for (std::size_t i = 0; i < v.size(); ++i)
        if (v[i].id == id) return static_cast<long>(i) + 1;
    return -1;
}

/// Las claves de `campos` reemplazan las del objeto (en su JSON) y se lee de nuevo.
template <class T>
T cambiado(const T& x, const json& campos) {
    json j = x.aJson();
    for (const auto& [k, v] : campos.items())
        j[k] = v;
    return T::deJson(j);
}

class Editor {
   public:
    Editor(Linea l, const std::function<bool(const std::string&)>& intocable) : l_(std::move(l)), intocable_(intocable) {}

    /// "" o por qué no.
    std::string aplicar(const json& op) {
        if (!op.is_object()) return traducir("linea", "cada operación es un objeto {op, …}");
        const std::string que = leerTexto(op, "op");
        if (que == "bloque_agregar") return bloqueAgregar(op);
        if (que == "paso_agregar") return pasoAgregar(op);
        if (que == "pieza_agregar") return piezaAgregar(op);
        if (que == "pista_agregar") return pistaAgregar(op);
        if (que == "estante_agregar") return estanteAgregar(op);
        if (que == "disparador_agregar") return disparadorAgregar(op);
        if (que == "plantilla_agregar") return plantillaAgregar(op);
        if (que == "mover") return mover(op);
        if (que == "quitar") return quitar(op);
        if (que == "cambiar") return cambiar(op);
        return llenar(traducir("linea", "no conozco la operación «%1» (bloque_agregar, paso_agregar, pieza_agregar, "
                                        "pista_agregar, estante_agregar, disparador_agregar, plantilla_agregar, mover, quitar, "
                                        "cambiar)"),
                      {que});
    }

    Linea l_;

   private:
    bool tocable(const std::string& paso) const { return !intocable_ || !intocable_(paso); }

    std::string noSeToca(const std::string& paso) const {
        return llenar(traducir("linea", "el paso «%1» ya salió o está al aire: en vivo solo se toca lo que viene"), {paso});
    }

    std::string idLibre(const std::string& id) const {
        if (id.empty()) return traducir("linea", "falta el id");
        if (buscar(l_, id).que != Lugar::Que::Nada) return llenar(traducir("linea", "ya hay algo con el id «%1»"), {id});
        return {};
    }

    std::string bloqueAgregar(const json& op) {
        if (!op.contains("bloque") || !op["bloque"].is_object()) return traducir("linea", "falta `bloque`");
        Bloque b = Bloque::deJson(op["bloque"]);
        if (std::string e = idLibre(b.id); !e.empty()) return e;
        for (const Paso& p : b.pasos)
            if (std::string e = idLibre(p.id); !e.empty()) return e;
        const long i = posicion(l_.bloques, op);
        if (i < 0) return llenar(traducir("linea", "no hay un bloque «%1»"), {leerTexto(op, "despuesDe")});
        l_.bloques.insert(l_.bloques.begin() + i, std::move(b));
        return {};
    }

    std::string pasoAgregar(const json& op) {
        if (!op.contains("paso") || !op["paso"].is_object()) return traducir("linea", "falta `paso`");
        const Lugar b = buscar(l_, leerTexto(op, "bloque"));
        if (b.que != Lugar::Que::Bloque) return llenar(traducir("linea", "no hay un bloque «%1»"), {leerTexto(op, "bloque")});
        Paso p = Paso::deJson(op["paso"]);
        if (std::string e = idLibre(p.id); !e.empty()) return e;
        for (const Pieza& z : p.piezas)
            if (std::string e = idLibre(z.id); !e.empty()) return e;
        auto& pasos = l_.bloques[b.b].pasos;
        const long i = posicion(pasos, op);
        if (i < 0) return llenar(traducir("linea", "no hay un paso «%1» en ese bloque"), {leerTexto(op, "despuesDe")});
        pasos.insert(pasos.begin() + i, std::move(p));
        return {};
    }

    std::string piezaAgregar(const json& op) {
        if (!op.contains("pieza") || !op["pieza"].is_object()) return traducir("linea", "falta `pieza`");
        const std::string pasoId = leerTexto(op, "paso");
        const Lugar p = buscar(l_, pasoId);
        if (p.que != Lugar::Que::Paso) return llenar(traducir("linea", "no hay un paso «%1»"), {pasoId});
        if (!tocable(pasoId)) return noSeToca(pasoId);
        Pieza z = Pieza::deJson(op["pieza"]);
        if (std::string e = idLibre(z.id); !e.empty()) return e;
        auto& piezas = l_.bloques[p.b].pasos[p.p].piezas;
        const long i = posicion(piezas, op);
        if (i < 0) return llenar(traducir("linea", "no hay una pieza «%1» en ese paso"), {leerTexto(op, "despuesDe")});
        piezas.insert(piezas.begin() + i, std::move(z));
        return {};
    }

    std::string pistaAgregar(const json& op) {
        if (!op.contains("pista") || !op["pista"].is_object()) return traducir("linea", "falta `pista`");
        Pista p = Pista::deJson(op["pista"]);
        if (std::string e = idLibre(p.id); !e.empty()) return e;
        l_.pistas.push_back(std::move(p));
        return {};
    }

    std::string estanteAgregar(const json& op) {
        if (!op.contains("item") || !op["item"].is_object()) return traducir("linea", "falta `item`");
        ItemEstante it = ItemEstante::deJson(op["item"]);
        if (std::string e = idLibre(it.id); !e.empty()) return e;
        if (!it.pieza && !it.paso) return traducir("linea", "el ítem del estante lleva una `pieza` o un `paso`");
        l_.estante.push_back(std::move(it));
        return {};
    }

    std::string disparadorAgregar(const json& op) {
        if (!op.contains("disparador") || !op["disparador"].is_object()) return traducir("linea", "falta `disparador`");
        Disparador d = Disparador::deJson(op["disparador"]);
        if (std::string e = idLibre(d.id); !e.empty()) return e;
        if (d.cuando.empty()) return traducir("linea", "el disparador lleva `cuando` (la condición)");
        if (std::string e = errorDeHacer(d.hacer); !e.empty()) return e;
        l_.disparadores.push_back(std::move(d));
        return {};
    }

    /// Una plantilla de pasos para el evento (p. ej. la de fábrica con la que se armó el
    /// orden del día: así queda con el evento y se re-expande con ella).
    std::string plantillaAgregar(const json& op) {
        if (!op.contains("plantilla") || !op["plantilla"].is_object()) return traducir("linea", "falta `plantilla`");
        Plantilla t = Plantilla::deJson(op["plantilla"]);
        if (std::string e = idLibre(t.id); !e.empty()) return e;
        if (t.pasos.empty()) return traducir("linea", "la plantilla lleva al menos un paso");
        l_.plantillas.push_back(std::move(t));
        return {};
    }

    std::string bloqueTocable(const Bloque& b) const {
        for (const Paso& p : b.pasos)
            if (!tocable(p.id)) return noSeToca(p.id);
        return {};
    }

    std::string mover(const json& op) {
        const std::string id = leerTexto(op, "id");
        const Lugar x = buscar(l_, id);
        switch (x.que) {
            case Lugar::Que::Bloque: {
                if (std::string e = bloqueTocable(l_.bloques[x.b]); !e.empty()) return e;
                Bloque b = std::move(l_.bloques[x.b]);
                l_.bloques.erase(l_.bloques.begin() + static_cast<long>(x.b));
                const long i = posicion(l_.bloques, op);
                if (i < 0) return llenar(traducir("linea", "no hay un bloque «%1»"), {leerTexto(op, "despuesDe")});
                l_.bloques.insert(l_.bloques.begin() + i, std::move(b));
                return {};
            }
            case Lugar::Que::Paso: {
                if (!tocable(id)) return noSeToca(id);
                const std::string destino = op.contains("bloque") ? leerTexto(op, "bloque") : l_.bloques[x.b].id;
                Paso p = std::move(l_.bloques[x.b].pasos[x.p]);
                l_.bloques[x.b].pasos.erase(l_.bloques[x.b].pasos.begin() + static_cast<long>(x.p));
                const Lugar b = buscar(l_, destino);
                if (b.que != Lugar::Que::Bloque) return llenar(traducir("linea", "no hay un bloque «%1»"), {destino});
                auto& pasos = l_.bloques[b.b].pasos;
                const long i = posicion(pasos, op);
                if (i < 0) return llenar(traducir("linea", "no hay un paso «%1» en ese bloque"), {leerTexto(op, "despuesDe")});
                pasos.insert(pasos.begin() + i, std::move(p));
                return {};
            }
            case Lugar::Que::Pieza: {
                const std::string origen = l_.bloques[x.b].pasos[x.p].id;
                const std::string destino = op.contains("paso") ? leerTexto(op, "paso") : origen;
                if (!tocable(origen)) return noSeToca(origen);
                if (!tocable(destino)) return noSeToca(destino);
                auto& de = l_.bloques[x.b].pasos[x.p].piezas;
                Pieza z = std::move(de[x.z]);
                de.erase(de.begin() + static_cast<long>(x.z));
                const Lugar p = buscar(l_, destino);
                if (p.que != Lugar::Que::Paso) return llenar(traducir("linea", "no hay un paso «%1»"), {destino});
                auto& a = l_.bloques[p.b].pasos[p.p].piezas;
                const long i = posicion(a, op);
                if (i < 0) return llenar(traducir("linea", "no hay una pieza «%1» en ese paso"), {leerTexto(op, "despuesDe")});
                a.insert(a.begin() + i, std::move(z));
                return {};
            }
            default: return llenar(traducir("linea", "no hay un bloque, paso ni pieza «%1»"), {id});
        }
    }

    std::string quitar(const json& op) {
        const std::string id = leerTexto(op, "id");
        const Lugar x = buscar(l_, id);
        switch (x.que) {
            case Lugar::Que::Bloque:
                if (std::string e = bloqueTocable(l_.bloques[x.b]); !e.empty()) return e;
                l_.bloques.erase(l_.bloques.begin() + static_cast<long>(x.b));
                return {};
            case Lugar::Que::Paso:
                if (!tocable(id)) return noSeToca(id);
                l_.bloques[x.b].pasos.erase(l_.bloques[x.b].pasos.begin() + static_cast<long>(x.p));
                return {};
            case Lugar::Que::Pieza: {
                auto& p = l_.bloques[x.b].pasos[x.p];
                if (!tocable(p.id)) return noSeToca(p.id);
                p.piezas.erase(p.piezas.begin() + static_cast<long>(x.z));
                return {};
            }
            case Lugar::Que::Pista:
                for (const Bloque& b : l_.bloques)
                    for (const Paso& p : b.pasos)
                        for (const Pieza& z : p.piezas)
                            if (z.pista == id)
                                return llenar(traducir("linea", "la pista «%1» la usa la pieza «%2»"), {id, z.id});
                l_.pistas.erase(l_.pistas.begin() + static_cast<long>(x.b));
                return {};
            case Lugar::Que::Estante: l_.estante.erase(l_.estante.begin() + static_cast<long>(x.b)); return {};
            case Lugar::Que::Disparador: l_.disparadores.erase(l_.disparadores.begin() + static_cast<long>(x.b)); return {};
            case Lugar::Que::Plantilla:
                for (const Bloque& b : l_.bloques)
                    if (b.plantilla == id) return llenar(traducir("linea", "la plantilla «%1» la usa el bloque «%2»"), {id, b.id});
                l_.plantillas.erase(l_.plantillas.begin() + static_cast<long>(x.b));
                return {};
            default: return llenar(traducir("linea", "no hay nada con el id «%1»"), {id});
        }
    }

    std::string cambiar(const json& op) {
        const std::string id = leerTexto(op, "id");
        if (!op.contains("campos") || !op["campos"].is_object()) return traducir("linea", "falta `campos`");
        const json& campos = op["campos"];
        if (campos.contains("id")) return traducir("linea", "el id no se cambia: quitalo y agregalo con otro");
        const Lugar x = buscar(l_, id);
        switch (x.que) {
            case Lugar::Que::Bloque:
                if (campos.contains("pasos")) return traducir("linea", "los pasos de un bloque se cambian con paso_agregar, mover y quitar");
                l_.bloques[x.b] = cambiado(l_.bloques[x.b], campos);
                return {};
            case Lugar::Que::Paso:
                if (!tocable(id)) return noSeToca(id);
                l_.bloques[x.b].pasos[x.p] = cambiado(l_.bloques[x.b].pasos[x.p], campos);
                return {};
            case Lugar::Que::Pieza: {
                auto& p = l_.bloques[x.b].pasos[x.p];
                if (!tocable(p.id)) return noSeToca(p.id);
                p.piezas[x.z] = cambiado(p.piezas[x.z], campos);
                return {};
            }
            case Lugar::Que::Pista: l_.pistas[x.b] = cambiado(l_.pistas[x.b], campos); return {};
            case Lugar::Que::Estante: l_.estante[x.b] = cambiado(l_.estante[x.b], campos); return {};
            case Lugar::Que::Disparador: {
                const Disparador d = cambiado(l_.disparadores[x.b], campos);
                if (std::string e = errorDeHacer(d.hacer); !e.empty()) return e;
                l_.disparadores[x.b] = d;
                return {};
            }
            case Lugar::Que::Plantilla: l_.plantillas[x.b] = cambiado(l_.plantillas[x.b], campos); return {};
            default: return llenar(traducir("linea", "no hay nada con el id «%1»"), {id});
        }
    }

    const std::function<bool(const std::string&)>& intocable_;
};

} // namespace

Edicion editar(const Linea& l, const json& ops, const std::function<bool(const std::string&)>& intocable) {
    Edicion r;
    if (!ops.is_array() || ops.empty()) {
        r.error = traducir("linea", "`ops` es una lista con al menos una operación");
        return r;
    }
    Editor ed(l, intocable);
    for (std::size_t i = 0; i < ops.size(); ++i) {
        if (std::string e = ed.aplicar(ops[i]); !e.empty()) {
            r.error = llenar(traducir("linea", "operación %1: %2 (no se aplicó nada)"), {std::to_string(i + 1), e});
            return r;
        }
    }
    r.ok = true;
    r.linea = std::move(ed.l_);
    r.diff = diferencias(l, r.linea);
    return r;
}

namespace {

struct Item {
    std::string tipo, padre;
    json j;
};

/// Todo lo que tiene id, aplanado: id → {tipo, padre, json sin los hijos}; y por
/// padre, el orden de sus hijos.
void aplanar(const Linea& l, std::map<std::string, Item>& m, std::map<std::string, std::vector<std::string>>& orden) {
    for (const Bloque& b : l.bloques) {
        json jb = b.aJson();
        jb.erase("pasos");
        m[b.id] = {"bloque", "", jb};
        orden[""].push_back(b.id);
        for (const Paso& p : b.pasos) {
            json jp = p.aJson();
            jp.erase("piezas");
            m[p.id] = {"paso", b.id, jp};
            orden[b.id].push_back(p.id);
            for (const Pieza& z : p.piezas) {
                m[z.id] = {"pieza", p.id, z.aJson()};
                orden[p.id].push_back(z.id);
            }
        }
    }
    for (const Pista& p : l.pistas)
        m[p.id] = {"pista", "", p.aJson()};
    for (const ItemEstante& i : l.estante)
        m[i.id] = {"estante", "", i.aJson()};
    for (const Disparador& d : l.disparadores)
        m[d.id] = {"disparador", "", d.aJson()};
    for (const Plantilla& t : l.plantillas)
        m[t.id] = {"plantilla", "", t.aJson()};
}

/// Los de `a` que siguen en `b`, en el orden de cada uno.
std::vector<std::string> comunes(const std::vector<std::string>& a, const std::map<std::string, Item>& otro, const std::string& padre) {
    std::vector<std::string> v;
    for (const std::string& id : a) {
        const auto it = otro.find(id);
        if (it != otro.end() && it->second.padre == padre) v.push_back(id);
    }
    return v;
}

} // namespace

json diferencias(const Linea& antes, const Linea& despues) {
    std::map<std::string, Item> ma, md;
    std::map<std::string, std::vector<std::string>> oa, od;
    aplanar(antes, ma, oa);
    aplanar(despues, md, od);
    json out = json::array();
    std::set<std::string> movidos;
    for (const auto& [padre, hijos] : od) {
        const auto ita = oa.find(padre);
        if (ita == oa.end()) continue;
        const std::vector<std::string> ca = comunes(ita->second, md, padre);
        const std::vector<std::string> cd = comunes(hijos, ma, padre);
        if (ca == cd) continue;
        for (std::size_t i = 0; i < cd.size() && i < ca.size(); ++i)
            if (cd[i] != ca[i]) movidos.insert(cd[i]);
    }
    for (const auto& [id, x] : ma)
        if (!md.count(id)) out.push_back({{"que", "quitado"}, {"tipo", x.tipo}, {"id", id}, {"antes", x.j}});
    for (const auto& [id, x] : md) {
        const auto it = ma.find(id);
        if (it == ma.end()) {
            out.push_back({{"que", "agregado"}, {"tipo", x.tipo}, {"id", id}, {"en", x.padre}, {"despues", x.j}});
            continue;
        }
        if (it->second.padre != x.padre || movidos.count(id))
            out.push_back({{"que", "movido"}, {"tipo", x.tipo}, {"id", id}, {"de", it->second.padre}, {"a", x.padre}});
        if (it->second.j != x.j) out.push_back({{"que", "cambiado"}, {"tipo", x.tipo}, {"id", id}, {"antes", it->second.j}, {"despues", x.j}});
    }
    return out;
}

// --- Ver ------------------------------------------------------------------------------

std::string escenaDePaso(const Linea& l, const Paso& p) {
    for (const Pieza& z : p.piezas) {
        const Pista* pi = l.pista(z.pista);
        const std::string tipo = z.tipo.empty() && pi ? pi->tipo : z.tipo;
        if (tipo == "escena" && z.desde.modo != Ancla::Modo::Manual) return leerTexto(z.datos, "escena");
    }
    return {};
}

json proximosPorCamara(const Linea& l, const json& vistas, const std::function<std::vector<std::string>(const std::string&)>& camarasDeEscena) {
    json out = json::object();
    const auto sumar = [&](const std::string& camara, json x) {
        if (camara.empty()) return;
        if (!out.contains(camara)) out[camara] = json::array();
        out[camara].push_back(std::move(x));
    };
    for (const json& v : vistas.is_array() ? vistas : json::array()) {
        const std::string salida = leerTexto(v, "salida");
        for (const json& b : v.value("bloques", json::array()))
            for (const json& jp : b.value("pasos", json::array())) {
                const std::string estado = leerTexto(jp, "estado");
                if (estado != "alAire" && estado != "proximo" && estado != "futuro") continue;
                const Paso* p = l.paso(leerTexto(jp, "id"));
                if (!p) continue;
                json base{{"salida", salida},          {"paso", p->id},           {"nombre", p->nombre},
                          {"estado", estado},          {"frente", jp.value("frente", json())}};
                if (jp.contains("enSeg")) base["enSeg"] = jp["enSeg"];
                const std::string escena = leerTexto(jp, "escena");
                std::set<std::string> yaEsta;
                if (!escena.empty() && camarasDeEscena) {
                    base["escena"] = escena;
                    for (const std::string& c : camarasDeEscena(escena))
                        if (yaEsta.insert(c).second) sumar(c, base);
                }
                // Las piezas PTZ del paso: la cámara se mueve aunque no salga en la escena.
                for (const Pieza& z : p->piezas) {
                    const Pista* pi = l.pista(z.pista);
                    if ((z.tipo.empty() && pi ? pi->tipo : z.tipo) != "ptz") continue;
                    const json c = z.datos.value("camara", json());
                    const std::string cam = c.is_string() ? c.get<std::string>() : c.is_number() ? c.dump() : std::string();
                    if (cam.empty()) continue;
                    json x = base;
                    x["ptz"] = {{"pieza", z.id}, {"texto", z.texto}, {"datos", z.datos}};
                    if (yaEsta.insert(cam).second) sumar(cam, std::move(x));
                    else
                        for (json& y : out[cam])
                            if (y["paso"] == p->id && y["salida"] == salida) y["ptz"] = x["ptz"];
                }
            }
    }
    // Por lo que falta: lo que está al aire primero, después lo que entra antes.
    for (auto& [cam, lista] : out.items()) {
        std::stable_sort(lista.begin(), lista.end(), [](const json& a, const json& b) {
            const int64_t ea = a["estado"] == "alAire" ? -1 : a.value("enSeg", int64_t{1} << 40);
            const int64_t eb = b["estado"] == "alAire" ? -1 : b.value("enSeg", int64_t{1} << 40);
            return ea < eb;
        });
        (void)cam;
    }
    return out;
}

json vista(const Linea& l, const Resolucion& r, const std::string& salida, int64_t desdeMs, int64_t hastaMs, const std::string& nivel,
           int64_t ahoraMs, const Hora& hora) {
    const auto H = [&](int64_t ms) -> json {
        if (ms == kNunca) return nullptr;
        return hora ? json(hora(ms)) : json(ms);
    };
    const SalidaResuelta* sr = r.salida(salida);
    if (sr == nullptr) return nullptr;
    const auto seCruza = [&](int64_t a, int64_t b) { return (desdeMs <= 0 || b >= desdeMs) && (hastaMs <= 0 || a <= hastaMs); };
    json out{{"salida", salida},     {"ahora", H(ahoraMs)},         {"alAire", sr->alAire}, {"proximo", sr->proximo},
             {"fin", H(sr->finMs)}, {"overUnderSeg", sr->overUnderMs / 1000}};
    if (sr->finPlaneadoMs > 0) out["finPlaneado"] = H(sr->finPlaneadoMs);
    json bloques = json::array();
    for (const Bloque& b : l.bloques) {
        const BloqueResuelto* br = nullptr;
        for (const BloqueResuelto& x : sr->bloques)
            if (x.bloque == b.id) br = &x;
        if (br == nullptr || !seCruza(br->desdeMs, br->hastaMs)) continue;
        json jb{{"id", b.id},
                {"nombre", b.nombre},
                {"desde", H(br->desdeMs)},
                {"hasta", H(br->hastaMs)},
                {"presupuestoSeg", br->presupuestoMs / 1000},
                {"overUnderSeg", br->overUnderMs / 1000}};
        if (nivel != "bloques") {
            json pasos = json::array();
            for (const Paso& p : b.pasos) {
                const PasoResuelto* pr = sr->paso(p.id);
                if (pr == nullptr) continue;
                if (pr->estado != EstadoPaso::AlAire && pr->estado != EstadoPaso::Salteado && !seCruza(pr->frenteMs, pr->finMs)) continue;
                json jp{{"id", p.id},          {"nombre", p.nombre},  {"estado", nombreDe(pr->estado)}, {"entra", nombreDe(pr->como)},
                        {"automatico", pr->automatico}, {"previstoSeg", p.duracionPlanMs() / 1000}};
                if (pr->estado != EstadoPaso::Salteado) {
                    jp["frente"] = H(pr->frenteMs);
                    jp["fin"] = H(pr->finMs);
                    jp["durSeg"] = (pr->finMs - pr->frenteMs) / 1000;
                }
                if (pr->backMs) jp["back"] = H(*pr->backMs);
                if (pr->tarde) jp["tarde"] = true;
                if (pr->estimado) jp["estimado"] = true;
                if (pr->enHold) jp["hold"] = true;
                if (pr->cortadoPorHora) jp["cortadoPorHora"] = true;
                if (p.inicio.modo == ModoInicio::Hora) {
                    jp["hora"] = p.inicio.hora;
                    jp["politica"] = nombreDe(p.inicio.politica);
                }
                if (!p.salida.empty() && p.salida != salida) jp["salidaDelPaso"] = p.salida;
                if (!p.notaOperador.empty()) jp["nota"] = p.notaOperador;
                // Para las superficies (la escaleta, la cámara, el presentador): la escena
                // que pone el paso y cuánto falta para que entre.
                if (const std::string e = escenaDePaso(l, p); !e.empty()) jp["escena"] = e;
                if ((pr->estado == EstadoPaso::Proximo || pr->estado == EstadoPaso::Futuro) && pr->frenteMs > ahoraMs)
                    jp["enSeg"] = (pr->frenteMs - ahoraMs) / 1000;
                if (!p.etiquetas.empty()) jp["etiquetas"] = p.etiquetas;
                if (nivel == "piezas") {
                    json zs = json::array();
                    for (const Pieza& z : p.piezas) {
                        json jz{{"id", z.id},  {"tipo", z.tipo}, {"pista", z.pista}, {"texto", z.texto},
                                {"vida", nombreDe(z.vida)}, {"desde", z.desde.aJson()}, {"datos", z.datos}};
                        for (const PiezaResuelta& x : r.piezas)
                            if (x.salida == salida && x.paso == p.id && x.pieza == z.id) {
                                jz["entra"] = H(x.desdeMs);
                                jz["sale"] = H(x.hastaMs);
                            }
                        zs.push_back(std::move(jz));
                    }
                    jp["piezas"] = std::move(zs);
                } else {
                    json tipos = json::array();
                    for (const Pieza& z : p.piezas)
                        tipos.push_back(z.tipo);
                    jp["piezas"] = std::move(tipos);
                }
                pasos.push_back(std::move(jp));
            }
            jb["pasos"] = std::move(pasos);
        }
        bloques.push_back(std::move(jb));
    }
    out["bloques"] = std::move(bloques);
    // El estante (los adlibs), para quien dispara a mano: la escaleta web, la botonera.
    if (nivel != "bloques") {
        json es = json::array();
        for (const ItemEstante& i : l.estante) {
            json x{{"id", i.id}, {"boton", i.boton}, {"tecla", i.tecla}, {"color", i.color}, {"tipo", i.paso ? "paso" : "pieza"}};
            x["nombre"] = i.paso ? i.paso->nombre : i.pieza ? (i.pieza->texto.empty() ? i.pieza->tipo : i.pieza->texto) : std::string();
            es.push_back(std::move(x));
        }
        out["estante"] = std::move(es);
    }
    json hs = json::array();
    for (const HardStart& h : sr->hardStarts)
        hs.push_back({{"paso", h.paso}, {"hora", H(h.horaMs)}, {"politica", nombreDe(h.politica)}, {"gapSeg", h.gapMs / 1000}});
    out["hardStarts"] = std::move(hs);
    json cs = json::array();
    for (const Conflicto& c : r.conflictos) {
        if (c.salida != salida && c.otraSalida != salida) continue;
        cs.push_back({{"tipo", c.tipo == Conflicto::Tipo::Ptz ? "ptz" : "grafico"},
                      {"recurso", c.recurso},
                      {"salida", c.salida},
                      {"pieza", c.pieza},
                      {"otraSalida", c.otraSalida},
                      {"otroPaso", c.otroPaso},
                      {"cuando", H(c.ms)},
                      {"armada", c.armada}});
    }
    out["conflictos"] = std::move(cs);
    return out;
}

} // namespace ba::evento::linea
