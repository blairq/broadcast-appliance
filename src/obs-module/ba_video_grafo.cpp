// ============================================================================
// src/obs-module/ba_video_grafo.cpp — El grafo de video de una entrada
//
// Proposito:
//   FILTRO `ba_video_grafo`: corre en la GPU un grafo de TARJETAS armado con
//   cables (como el rack de audio): la entrada (`in`) pasa por tarjetas de
//   una pasada (color, croma, recorte/espejo) y llega a la escena (`out`).
//   Una salida puede ir a varias tarjetas: el grafo se ramifica.
//
//   FUENTE `ba_video_salida`: una tarjeta «Salida» publica la imagen de ese
//   punto del grafo como una fuente con nombre, que se pone en cualquier
//   escena (p. ej. «Cámara 1» al aire sin tocar y «Cámara 1 · croma» en otra).
//
//   El grafo llega como JSON en el ajuste `grafo` (lo arma la app, ver
//   src/app/video/VideoGrafos):
//     {"nodos":[{"id","tipo","params":{…}}], "cables":[{"desde","hacia"}]}
//   tipos: in, out, color, croma, recorte, salida, `obs`: cualquier filtro
//   de video de OBS ({"filtro": id, "ajustes": {…}}), y `gst`: un módulo de
//   GStreamer ({"elemento": nombre, "propiedades": {…}}, ver ba_gst.hpp).
//   Lo que cruza entradas (ver ba_video_estudio.hpp): `toma` publica ese
//   punto ({"nombre"}); `traer` (sin entrada; se calcula al final) trae un
//   compositor ({"compositor": id, "config": "…"}) o la toma de otra entrada
//   ({"toma", "fuente"}). `retardo` atrasa la imagen {"cuadros"} (para
//   sincronizar una cámara con otra o con el audio).
//
// Modelo de threading:
//   `update` (cualquier hilo) arma un Plan nuevo y lo cambia bajo un mutex;
//   `video_render` (hilo grafico) toma el plan vigente. Las tarjetas se
//   calculan UNA vez por cuadro (obs_get_video_frame_time), aunque la fuente
//   se dibuje varias veces (programa, vista previa, multiview). Las salidas
//   se registran por nombre en un mapa global con su propio mutex; sus
//   texturas solo se tocan en el hilo grafico.
//
// Estrategia de errores:
//   Si el efecto no compila, o el grafo no tiene camino a la escena, la imagen
//   pasa como vino (o negro si `out` no recibe nada, que es lo pedido).
// ============================================================================

#include <obs-module.h>
#include <obs.h>

#include "ba_gst.hpp"
#include "ba_video_estudio.hpp"

#include <algorithm>
#include <cstdint>
#include <locale>
#include <sstream>
#include <iterator>
#include <map>
#include <memory>
#include <mutex>
#include <new>
#include <string>
#include <vector>

namespace {

enum class Tipo { Color, Croma, Recorte, Salida, Obs, Gst, Toma, Traer, Retardo };

/// Los cuadros guardados de una tarjeta `retardo` (sobreviven a los cambios del plan).
struct Retardo {
    std::vector<gs_texrender_t*> anillo;
    size_t pos = 0;
    ~Retardo() {
        obs_enter_graphics();
        for (gs_texrender_t* t : anillo) gs_texrender_destroy(t);
        obs_leave_graphics();
    }
};

// --- Tarjetas que corren un filtro de OBS -------------------------------------
//
// Un filtro de libobs trabaja sobre una fuente, no sobre una textura. Para
// correr uno DENTRO del grafo (con ramas), cada tarjeta tiene su fuente
// privada «portador» (`ba_video_portador`) que dibuja la textura que le llega
// a la tarjeta, con el filtro puesto encima: se dibuja el portador y sale la
// textura filtrada. Así sirve cualquier filtro de video de OBS (los de
// obs-filters, Mejorar imagen, el PTZ digital…) sin escribir ninguno.

struct Portador {
    gs_texture_t* tex = nullptr; ///< lo que dibuja ahora (solo en el hilo gráfico)
    std::uint32_t cx = 0, cy = 0;
};

/// El portador y su filtro, vivos mientras alguna versión del plan los use.
struct InstanciaObs {
    std::string filtroId, ajustes; ///< el ajuste aplicado (JSON), para no recrear el filtro
    obs_source_t* portador = nullptr;
    obs_source_t* filtro = nullptr;
    ~InstanciaObs() {
        if (filtro != nullptr && portador != nullptr) obs_source_filter_remove(portador, filtro);
        obs_source_release(filtro);
        obs_source_release(portador);
    }
};

/// Una tarjeta del plan, en el orden en que se calcula.
struct Paso {
    Tipo tipo = Tipo::Color;
    std::string id; ///< el nodo (para informar sus errores)
    int entrada = -1; ///< el paso que la alimenta (-1 = la entrada de la fuente)
    // color
    float brillo = 0.0f, contraste = 1.0f, saturacion = 1.0f, gamma = 1.0f;
    // croma
    float claveCb = 0.0f, claveCr = 0.0f, similitud = 0.4f, suavidad = 0.08f, derrame = 0.1f;
    // recorte
    float izq = 0.0f, arr = 0.0f, der = 0.0f, aba = 0.0f, espejoX = 0.0f, espejoY = 0.0f;
    // salida y toma
    std::string nombre;
    // traer: un compositor, o la toma de otra entrada
    std::string compositor, config, fuente;
    // retardo
    int cuadros = 0;
    std::shared_ptr<Retardo> retardo;
    // obs
    std::shared_ptr<InstanciaObs> obs;
#if defined(BA_HAVE_GST)
    // gst: un módulo de GStreamer
    std::shared_ptr<ba::gst::TarjetaGst> gst;
#endif
};

struct Plan {
    std::vector<Paso> pasos;
    /// Las tarjetas de OBS por id de nodo (para reusarlas en el plan siguiente).
    std::map<std::string, std::shared_ptr<InstanciaObs>> obs;
    std::map<std::string, std::shared_ptr<Retardo>> retardos;
#if defined(BA_HAVE_GST)
    std::map<std::string, std::shared_ptr<ba::gst::TarjetaGst>> gst;
#endif
    /// Lo que llega a la escena: -1 = la entrada tal cual, -2 = nada (negro).
    int escena = -1;
    bool trivial() const { return pasos.empty() && escena == -1; }
};

// --- Las salidas, por nombre -------------------------------------------------

struct Salida {
    obs_source_t* ctx = nullptr;
    std::string fuente;                ///< la entrada de la que sale (para despertarla)
    gs_texrender_t* tex = nullptr;     ///< lo ultimo que le mando el grafo
    std::uint32_t cx = 0, cy = 0;
    std::uint64_t cuadro = 0;          ///< cuando se lleno
    gs_texrender_t* despertar = nullptr; ///< para dibujar la entrada si nadie la dibuja
};

std::mutex g_salidasLock;
std::map<std::string, Salida*>& salidas() {
    static std::map<std::string, Salida*> m;
    return m;
}

// --- El filtro -----------------------------------------------------------------

struct Grafo {
    obs_source_t* ctx = nullptr;
    gs_effect_t* fx = nullptr;
    gs_eparam_t *pImage = nullptr, *pTexel = nullptr, *pBrillo = nullptr, *pContraste = nullptr,
                *pSaturacion = nullptr, *pGamma = nullptr, *pClave = nullptr, *pSimilitud = nullptr,
                *pSuavidad = nullptr, *pDerrame = nullptr, *pBorde = nullptr, *pEspejo = nullptr;

    std::mutex planLock;
    std::shared_ptr<const Plan> plan = std::make_shared<Plan>();

    gs_texrender_t* entrada = nullptr;
    std::vector<gs_texrender_t*> texs; ///< una por paso
    std::uint64_t ultimoCuadro = 0;
    bool listo = false;
    std::uint32_t cx = 0, cy = 0;
    std::string fuente; ///< la entrada (para las tomas)
    std::mutex estadoLock;
    std::map<std::string, std::string> errores; ///< por nodo: por qué una tarjeta no corre
};

float numero(obs_data_t* p, const char* k, double def) {
    return static_cast<float>(obs_data_has_user_value(p, k) ? obs_data_get_double(p, k) : def);
}

/// RGB (0..1) -> Cb, Cr de BT.709 (lo mismo que CromaDe en el efecto).
void cromaDe(float r, float g, float b, float& cb, float& cr) {
    cb = -0.1146f * r - 0.3854f * g + 0.5f * b + 0.5f;
    cr = 0.5f * r - 0.4542f * g - 0.0458f * b + 0.5f;
}

/// El JSON del grafo -> el plan (orden de calculo desde la entrada).
/// La instancia de un filtro de OBS para el nodo `id`: la del plan anterior si
/// es el mismo filtro (con los ajustes al día), o una nueva.
std::shared_ptr<InstanciaObs> instanciaObs(const Plan* anterior, const std::string& id, const std::string& filtroId,
                                           obs_data_t* ajustes) {
    const char* aj = ajustes != nullptr ? obs_data_get_json(ajustes) : "{}";
    if (anterior != nullptr) {
        const auto it = anterior->obs.find(id);
        if (it != anterior->obs.end() && it->second->filtroId == filtroId) {
            if (it->second->ajustes != aj) {
                obs_source_update(it->second->filtro, ajustes);
                it->second->ajustes = aj;
            }
            return it->second;
        }
    }
    // libobs crea igual una fuente con un tipo que no conoce (la deja
    // «faltante» y pasa la imagen sin tocar): se pregunta antes.
    if (obs_source_get_display_name(filtroId.c_str()) == nullptr) {
        blog(LOG_WARNING, "[ba-sources] Grafo de video: no existe el filtro «%s»", filtroId.c_str());
        return nullptr;
    }
    auto inst = std::make_shared<InstanciaObs>();
    inst->filtroId = filtroId;
    inst->ajustes = aj;
    inst->portador = obs_source_create_private("ba_video_portador", ("portador " + id).c_str(), nullptr);
    inst->filtro = obs_source_create_private(filtroId.c_str(), id.c_str(), ajustes);
    if (inst->portador == nullptr || inst->filtro == nullptr) {
        blog(LOG_WARNING, "[ba-sources] Grafo de video: no se pudo crear el filtro «%s»", filtroId.c_str());
        return nullptr;
    }
    obs_source_filter_add(inst->portador, inst->filtro);
    return inst;
}

#if defined(BA_HAVE_GST)
/// Las propiedades de una tarjeta de GStreamer como texto (lo que entiende
/// gst_util_set_object_arg: números, true/false, nombres de enum).
std::map<std::string, std::string> propiedadesGst(obs_data_t* props) {
    std::map<std::string, std::string> out;
    if (props == nullptr) return out;
    for (obs_data_item_t* it = obs_data_first(props); it != nullptr; obs_data_item_next(&it)) {
        const char* k = obs_data_item_get_name(it);
        switch (obs_data_item_gettype(it)) {
        case OBS_DATA_STRING: out[k] = obs_data_item_get_string(it); break;
        case OBS_DATA_BOOLEAN: out[k] = obs_data_item_get_bool(it) ? "true" : "false"; break;
        case OBS_DATA_NUMBER:
            if (obs_data_item_numtype(it) == OBS_DATA_NUM_INT) {
                out[k] = std::to_string(obs_data_item_get_int(it));
            } else {
                // Con punto SIEMPRE: std::to_string sigue el locale de la app
                // (es_AR: «0,5») y GStreamer no lo entiende (la propiedad no cambiaba).
                std::ostringstream o;
                o.imbue(std::locale::classic());
                o.precision(17);
                o << obs_data_item_get_double(it);
                out[k] = o.str();
            }
            break;
        default: break;
        }
    }
    return out;
}

/// La tarjeta de GStreamer del nodo `id`: la del plan anterior si es el mismo
/// módulo (con las propiedades al día, en vivo), o una nueva.
std::shared_ptr<ba::gst::TarjetaGst> tarjetaGst(const Plan* anterior, const std::string& id, const std::string& elemento,
                                                 const std::map<std::string, std::string>& props) {
    if (anterior != nullptr) {
        const auto it = anterior->gst.find(id);
        if (it != anterior->gst.end() && it->second->elemento() == elemento) {
            it->second->ajustar(props);
            return it->second;
        }
    }
    return std::make_shared<ba::gst::TarjetaGst>(elemento, props);
}
#endif

std::shared_ptr<Plan> armarPlan(const char* json, const Plan* anterior) {
    auto plan = std::make_shared<Plan>();
    if (json == nullptr || *json == '\0') return plan;
    obs_data_t* d = obs_data_create_from_json(json);
    if (d == nullptr) return plan;
    struct Nodo {
        std::string id, tipo;
        obs_data_t* params = nullptr;
        std::string desde; ///< quien la alimenta (el primer cable que llega)
    };
    std::vector<Nodo> nodos;
    obs_data_array_t* an = obs_data_get_array(d, "nodos");
    for (size_t i = 0; an != nullptr && i < obs_data_array_count(an); ++i) {
        obs_data_t* n = obs_data_array_item(an, i);
        Nodo x;
        x.id = obs_data_get_string(n, "id");
        x.tipo = obs_data_get_string(n, "tipo");
        x.params = obs_data_get_obj(n, "params");
        nodos.push_back(std::move(x));
        obs_data_release(n);
    }
    obs_data_array_release(an);
    obs_data_array_t* ac = obs_data_get_array(d, "cables");
    for (size_t i = 0; ac != nullptr && i < obs_data_array_count(ac); ++i) {
        obs_data_t* c = obs_data_array_item(ac, i);
        const std::string desde = obs_data_get_string(c, "desde"), hacia = obs_data_get_string(c, "hacia");
        for (auto& n : nodos)
            if (n.id == hacia && n.desde.empty()) n.desde = desde;
        obs_data_release(c);
    }
    obs_data_array_release(ac);
    obs_data_release(d);

    // Desde la entrada hacia adelante: cada nodo se calcula cuando ya se
    // calculo quien lo alimenta. Lo que no cuelga de la entrada no se calcula.
    std::map<std::string, int> indice; // id -> paso (-1 = in)
    for (const auto& n : nodos)
        if (n.tipo == "in") indice[n.id] = -1;
    bool avanzo = true;
    while (avanzo) {
        avanzo = false;
        for (const auto& n : nodos) {
            if (n.tipo == "in" || n.tipo == "out" || indice.count(n.id) || !indice.count(n.desde)) continue;
            Paso p;
            p.id = n.id;
            p.entrada = indice[n.desde];
            obs_data_t* q = n.params;
            if (n.tipo == "color") {
                p.tipo = Tipo::Color;
                p.brillo = numero(q, "brillo", 0.0);
                p.contraste = numero(q, "contraste", 1.0);
                p.saturacion = numero(q, "saturacion", 1.0);
                p.gamma = numero(q, "gamma", 1.0);
            } else if (n.tipo == "croma") {
                p.tipo = Tipo::Croma;
                // El color de fondo como 0xRRGGBB (de fabrica, verde).
                const long long c = q != nullptr && obs_data_has_user_value(q, "clave") ? obs_data_get_int(q, "clave")
                                                                                       : 0x00B140;
                cromaDe(static_cast<float>((c >> 16) & 0xFF) / 255.0f, static_cast<float>((c >> 8) & 0xFF) / 255.0f,
                        static_cast<float>(c & 0xFF) / 255.0f, p.claveCb, p.claveCr);
                p.similitud = numero(q, "similitud", 0.4);
                p.suavidad = numero(q, "suavidad", 0.08);
                p.derrame = numero(q, "derrame", 0.1);
            } else if (n.tipo == "recorte") {
                p.tipo = Tipo::Recorte;
                p.izq = numero(q, "izquierda", 0.0);
                p.arr = numero(q, "arriba", 0.0);
                p.der = numero(q, "derecha", 0.0);
                p.aba = numero(q, "abajo", 0.0);
                p.espejoX = q != nullptr && obs_data_get_bool(q, "espejoH") ? 1.0f : 0.0f;
                p.espejoY = q != nullptr && obs_data_get_bool(q, "espejoV") ? 1.0f : 0.0f;
            } else if (n.tipo == "obs") {
                p.tipo = Tipo::Obs;
                const std::string filtroId = q != nullptr ? obs_data_get_string(q, "filtro") : "";
                obs_data_t* aj = q != nullptr ? obs_data_get_obj(q, "ajustes") : nullptr;
                p.obs = filtroId.empty() ? nullptr : instanciaObs(anterior, n.id, filtroId, aj);
                obs_data_release(aj);
                if (!p.obs) continue; // un filtro que no existe: la tarjeta no se calcula
                plan->obs[n.id] = p.obs;
            } else if (n.tipo == "gst") {
#if defined(BA_HAVE_GST)
                p.tipo = Tipo::Gst;
                const std::string elemento = q != nullptr ? obs_data_get_string(q, "elemento") : "";
                if (elemento.empty()) continue;
                obs_data_t* pr = q != nullptr ? obs_data_get_obj(q, "propiedades") : nullptr;
                p.gst = tarjetaGst(anterior, n.id, elemento, propiedadesGst(pr));
                obs_data_release(pr);
                plan->gst[n.id] = p.gst;
#else
                continue; // sin GStreamer: la tarjeta no se calcula
#endif
            } else if (n.tipo == "salida") {
                p.tipo = Tipo::Salida;
                p.nombre = q != nullptr ? obs_data_get_string(q, "nombre") : "";
            } else if (n.tipo == "toma") {
                p.tipo = Tipo::Toma;
                p.nombre = q != nullptr ? obs_data_get_string(q, "nombre") : "";
                if (p.nombre.empty()) continue;
            } else if (n.tipo == "retardo") {
                p.tipo = Tipo::Retardo;
                p.cuadros = q != nullptr ? static_cast<int>(std::clamp<long long>(obs_data_get_int(q, "cuadros"), 0, 120)) : 0;
                std::shared_ptr<Retardo> r;
                if (anterior != nullptr && anterior->retardos.count(n.id)) r = anterior->retardos.at(n.id);
                if (!r || r->anillo.size() != static_cast<size_t>(p.cuadros) + 1) r = std::make_shared<Retardo>();
                p.retardo = r;
                plan->retardos[n.id] = r;
            } else {
                continue; // un tipo que este filtro no conoce: no se calcula
            }
            indice[n.id] = static_cast<int>(plan->pasos.size());
            plan->pasos.push_back(std::move(p));
            avanzo = true;
        }
    }
    // Los `traer` no cuelgan de la entrada: van al final, cuando las tomas de
    // esta entrada ya se publicaron (un compositor que la recibe la ve fresca).
    for (const auto& n : nodos) {
        if (n.tipo != "traer" || indice.count(n.id)) continue;
        Paso p;
        p.tipo = Tipo::Traer;
        obs_data_t* q = n.params;
        p.compositor = q != nullptr ? obs_data_get_string(q, "compositor") : "";
        p.config = q != nullptr ? obs_data_get_string(q, "config") : "";
        p.nombre = q != nullptr ? obs_data_get_string(q, "toma") : "";
        p.fuente = q != nullptr ? obs_data_get_string(q, "fuente") : "";
        if (!p.compositor.empty()) ba::video::configurarCompositor(p.compositor, p.config.c_str());
        indice[n.id] = static_cast<int>(plan->pasos.size());
        plan->pasos.push_back(std::move(p));
        // Lo que cuelga de un `traer` (una tarjeta después del compositor) también.
        bool mas = true;
        while (mas) {
            mas = false;
            for (const auto& m : nodos) {
                if (m.tipo != "salida" || indice.count(m.id) || !indice.count(m.desde)) continue;
                Paso s;
                s.tipo = Tipo::Salida;
                s.entrada = indice[m.desde];
                s.nombre = m.params != nullptr ? obs_data_get_string(m.params, "nombre") : "";
                indice[m.id] = static_cast<int>(plan->pasos.size());
                plan->pasos.push_back(std::move(s));
                mas = true;
            }
        }
    }
    plan->escena = -2;
    for (const auto& n : nodos)
        if (n.tipo == "out") plan->escena = indice.count(n.desde) ? indice[n.desde] : -2;
    for (auto& n : nodos) obs_data_release(n.params);
    return plan;
}

const char* nombre(void*) { return obs_module_text("Grafo de video (BA)"); }

void actualizar(void* data, obs_data_t* s) {
    auto* g = static_cast<Grafo*>(data);
    std::shared_ptr<const Plan> anterior;
    {
        std::lock_guard<std::mutex> l(g->planLock);
        anterior = g->plan;
    }
    // Las tarjetas de OBS del plan anterior se reusan (mismo filtro: solo se
    // le cambian los ajustes, sin perder su estado).
    std::shared_ptr<const Plan> plan = armarPlan(obs_data_get_string(s, "grafo"), anterior.get());
    {
        std::lock_guard<std::mutex> l(g->planLock);
        g->plan.swap(plan);
        g->listo = false;
    }
    // `plan` es ahora el viejo: se suelta acá, fuera del hilo gráfico (el
    // dibujo lo usa bajo el mismo candado, así que ya no lo está usando).
}

void* crear(obs_data_t* settings, obs_source_t* ctx) {
    auto* g = new (std::nothrow) Grafo();
    if (g == nullptr) return nullptr;
    g->ctx = ctx;
    char* ruta = obs_module_file("video-grafo.effect");
    char* errores = nullptr;
    obs_enter_graphics();
    if (ruta != nullptr) g->fx = gs_effect_create_from_file(ruta, &errores);
    if (g->fx != nullptr) {
        g->pImage = gs_effect_get_param_by_name(g->fx, "image");
        g->pTexel = gs_effect_get_param_by_name(g->fx, "texel");
        g->pBrillo = gs_effect_get_param_by_name(g->fx, "brillo");
        g->pContraste = gs_effect_get_param_by_name(g->fx, "contraste");
        g->pSaturacion = gs_effect_get_param_by_name(g->fx, "saturacion");
        g->pGamma = gs_effect_get_param_by_name(g->fx, "gamma");
        g->pClave = gs_effect_get_param_by_name(g->fx, "clave");
        g->pSimilitud = gs_effect_get_param_by_name(g->fx, "similitud");
        g->pSuavidad = gs_effect_get_param_by_name(g->fx, "suavidad");
        g->pDerrame = gs_effect_get_param_by_name(g->fx, "derrame");
        g->pBorde = gs_effect_get_param_by_name(g->fx, "borde");
        g->pEspejo = gs_effect_get_param_by_name(g->fx, "espejo");
    }
    g->entrada = gs_texrender_create(GS_RGBA, GS_ZS_NONE);
    obs_leave_graphics();
    if (g->fx == nullptr) {
        blog(LOG_WARNING, "[ba-sources] Grafo de video: no se pudo cargar %s (%s); la imagen pasa sin tocar",
             ruta != nullptr ? ruta : "video-grafo.effect", errores != nullptr ? errores : "no esta el archivo");
    }
    bfree(errores);
    bfree(ruta);
    actualizar(g, settings);
    // Por qué una tarjeta no corre (p. ej. un módulo de GStreamer que no
    // arrancó): {"<id del nodo>": "motivo"}, para la interfaz.
    proc_handler_add(obs_source_get_proc_handler(ctx), "void estado(out string errores)",
                     [](void* d, calldata_t* cd) {
                         auto* x = static_cast<Grafo*>(d);
                         obs_data_t* o = obs_data_create();
                         {
                             std::lock_guard<std::mutex> l(x->estadoLock);
                             for (const auto& [id, e] : x->errores) obs_data_set_string(o, id.c_str(), e.c_str());
                         }
                         calldata_set_string(cd, "errores", obs_data_get_json(o));
                         obs_data_release(o);
                     },
                     g);
    return g;
}

void destruir(void* data) {
    auto* g = static_cast<Grafo*>(data);
    obs_enter_graphics();
    gs_effect_destroy(g->fx);
    gs_texrender_destroy(g->entrada);
    for (gs_texrender_t* t : g->texs) gs_texrender_destroy(t);
    obs_leave_graphics();
    delete g;
}

/// Dibuja `tex` (cx x cy) con la tecnica pedida en el render target actual.
void pasada(Grafo* g, gs_texture_t* tex, const char* tecnica) {
    gs_effect_set_texture(g->pImage, tex);
    gs_blend_state_push();
    gs_blend_function(GS_BLEND_ONE, GS_BLEND_ZERO);
    while (gs_effect_loop(g->fx, tecnica)) gs_draw_sprite(tex, 0, g->cx, g->cy);
    gs_blend_state_pop();
}

/// Copia `tex` a la textura de la salida `nombre` (si existe).
void aSalida(Grafo* g, const std::string& nombre, gs_texture_t* tex) {
    std::lock_guard<std::mutex> l(g_salidasLock);
    auto it = salidas().find(nombre);
    if (it == salidas().end()) return;
    Salida* s = it->second;
    gs_texrender_reset(s->tex);
    if (!gs_texrender_begin(s->tex, g->cx, g->cy)) return;
    gs_ortho(0.0f, static_cast<float>(g->cx), 0.0f, static_cast<float>(g->cy), -100.0f, 100.0f);
    pasada(g, tex, "Copiar");
    gs_texrender_end(s->tex);
    s->cx = g->cx;
    s->cy = g->cy;
    s->cuadro = obs_get_video_frame_time();
}

/// Las tarjetas del cuadro actual (una vez por cuadro).
bool procesar(Grafo* g, const Plan& plan) {
    // La entrada, tal como llega a este filtro (con los filtros de antes).
    gs_texrender_reset(g->entrada);
    if (!gs_texrender_begin(g->entrada, g->cx, g->cy)) return false;
    struct vec4 nada;
    vec4_zero(&nada);
    gs_clear(GS_CLEAR_COLOR, &nada, 0.0f, 0);
    gs_ortho(0.0f, static_cast<float>(g->cx), 0.0f, static_cast<float>(g->cy), -100.0f, 100.0f);
    const bool ok = obs_source_process_filter_begin(g->ctx, GS_RGBA, OBS_NO_DIRECT_RENDERING);
    if (ok) {
        gs_blend_state_push();
        gs_blend_function(GS_BLEND_ONE, GS_BLEND_ZERO);
        obs_source_process_filter_tech_end(g->ctx, g->fx, g->cx, g->cy, "Copiar");
        gs_blend_state_pop();
    }
    gs_texrender_end(g->entrada);
    if (!ok) return false;

    while (g->texs.size() < plan.pasos.size()) g->texs.push_back(gs_texrender_create(GS_RGBA, GS_ZS_NONE));
    const auto texDe = [&](int i) -> gs_texture_t* {
        return i < 0 ? gs_texrender_get_texture(g->entrada) : gs_texrender_get_texture(g->texs[static_cast<size_t>(i)]);
    };
    struct vec2 texel;
    vec2_set(&texel, 1.0f / static_cast<float>(g->cx), 1.0f / static_cast<float>(g->cy));
    gs_effect_set_vec2(g->pTexel, &texel);
    for (size_t i = 0; i < plan.pasos.size(); ++i) {
        const Paso& p = plan.pasos[i];
        gs_texture_t* en = texDe(p.entrada);
        // Lo que viene de afuera se pide ANTES de empezar a dibujar este paso:
        // pedirlo puede despertar otra entrada (que dibuja en lo suyo).
        gs_texture_t* externa = nullptr;
        if (p.tipo == Tipo::Traer)
            externa = !p.compositor.empty() ? ba::video::componer(p.compositor, nullptr, nullptr)
                                            : ba::video::leerToma(p.nombre, p.fuente, nullptr, nullptr);
        gs_texrender_t* destino = g->texs[i];
        gs_texrender_reset(destino);
        if (!gs_texrender_begin(destino, g->cx, g->cy)) continue;
        gs_ortho(0.0f, static_cast<float>(g->cx), 0.0f, static_cast<float>(g->cy), -100.0f, 100.0f);
        switch (p.tipo) {
        case Tipo::Color:
            gs_effect_set_float(g->pBrillo, p.brillo);
            gs_effect_set_float(g->pContraste, p.contraste);
            gs_effect_set_float(g->pSaturacion, p.saturacion);
            gs_effect_set_float(g->pGamma, p.gamma);
            pasada(g, en, "Color");
            break;
        case Tipo::Croma: {
            struct vec2 clave;
            vec2_set(&clave, p.claveCb, p.claveCr);
            gs_effect_set_vec2(g->pClave, &clave);
            gs_effect_set_float(g->pSimilitud, p.similitud);
            gs_effect_set_float(g->pSuavidad, p.suavidad);
            gs_effect_set_float(g->pDerrame, p.derrame);
            pasada(g, en, "Croma");
            break;
        }
        case Tipo::Recorte: {
            struct vec4 borde;
            vec4_set(&borde, p.izq, p.arr, p.der, p.aba);
            struct vec2 espejo;
            vec2_set(&espejo, p.espejoX, p.espejoY);
            gs_effect_set_vec4(g->pBorde, &borde);
            gs_effect_set_vec2(g->pEspejo, &espejo);
            pasada(g, en, "Recorte");
            break;
        }
        case Tipo::Salida:
        case Tipo::Toma:
            // Pasa lo que recibe (lo que la siga ve lo mismo) y lo publica.
            pasada(g, en, "Copiar");
            break;
        case Tipo::Traer: {
            struct vec4 negro;
            vec4_set(&negro, 0.0f, 0.0f, 0.0f, 1.0f);
            gs_clear(GS_CLEAR_COLOR, &negro, 0.0f, 0);
            if (externa != nullptr) pasada(g, externa, "Copiar");
            break;
        }
        case Tipo::Retardo: {
            // Un anillo de cuadros + 1: entra el de ahora, sale el de hace N.
            Retardo& r = *p.retardo;
            const size_t n = static_cast<size_t>(p.cuadros) + 1;
            if (r.anillo.size() != n) {
                for (gs_texrender_t* t : r.anillo) gs_texrender_destroy(t);
                r.anillo.clear();
                for (size_t k = 0; k < n; ++k) r.anillo.push_back(gs_texrender_create(GS_RGBA, GS_ZS_NONE));
                r.pos = 0;
            }
            gs_texrender_end(destino); // el anillo se llena fuera de este paso
            gs_texrender_t* guardar = r.anillo[r.pos % n];
            gs_texrender_reset(guardar);
            if (gs_texrender_begin(guardar, g->cx, g->cy)) {
                gs_ortho(0.0f, static_cast<float>(g->cx), 0.0f, static_cast<float>(g->cy), -100.0f, 100.0f);
                pasada(g, en, "Copiar");
                gs_texrender_end(guardar);
            }
            r.pos = (r.pos + 1) % n;
            gs_texture_t* viejo = gs_texrender_get_texture(r.anillo[r.pos % n]);
            gs_texrender_reset(destino);
            if (!gs_texrender_begin(destino, g->cx, g->cy)) continue;
            gs_ortho(0.0f, static_cast<float>(g->cx), 0.0f, static_cast<float>(g->cy), -100.0f, 100.0f);
            // Al empezar, los guardados todavía están vacíos: pasa el de ahora.
            pasada(g, viejo != nullptr ? viejo : en, "Copiar");
            break;
        }
        case Tipo::Gst:
#if defined(BA_HAVE_GST)
            {
                const bool corrio = p.gst->procesar(en, g->cx, g->cy, g->fx);
                std::lock_guard<std::mutex> l(g->estadoLock);
                if (corrio || p.gst->error().empty())
                    g->errores.erase(p.id);
                else
                    g->errores[p.id] = p.gst->error();
                if (corrio) break;
            }
            {
                // Sin GStreamer o con el módulo caído: la imagen pasa como vino.
                gs_texrender_end(destino);
                gs_texrender_reset(destino);
                if (gs_texrender_begin(destino, g->cx, g->cy)) {
                    gs_ortho(0.0f, static_cast<float>(g->cx), 0.0f, static_cast<float>(g->cy), -100.0f, 100.0f);
                    pasada(g, en, "Copiar");
                }
            }
#endif
            break;
        case Tipo::Obs: {
            // El portador dibuja la textura y su filtro de OBS la procesa.
            auto* port = static_cast<Portador*>(obs_obj_get_data(p.obs->portador));
            if (port == nullptr) {
                pasada(g, en, "Copiar");
                break;
            }
            port->tex = en;
            port->cx = g->cx;
            port->cy = g->cy;
            struct vec4 nada;
            vec4_zero(&nada);
            gs_clear(GS_CLEAR_COLOR, &nada, 0.0f, 0);
            gs_blend_state_push();
            gs_blend_function(GS_BLEND_ONE, GS_BLEND_ZERO);
            obs_source_video_render(p.obs->portador);
            gs_blend_state_pop();
            port->tex = nullptr;
            break;
        }
        }
        gs_texrender_end(destino);
        if (p.tipo == Tipo::Salida && !p.nombre.empty()) aSalida(g, p.nombre, gs_texrender_get_texture(destino));
        if (p.tipo == Tipo::Toma) ba::video::publicarToma(p.nombre, g->fuente, gs_texrender_get_texture(destino), g->cx, g->cy);
    }
    return true;
}

void dibujar(void* data, gs_effect_t*) {
    auto* g = static_cast<Grafo*>(data);
    // Todo el dibujo con el plan tomado: `actualizar` no suelta un plan (ni
    // sus filtros de OBS) mientras se dibuja.
    std::lock_guard<std::mutex> l(g->planLock);
    const std::shared_ptr<const Plan> plan = g->plan;
    obs_source_t* objetivo = obs_filter_get_target(g->ctx);
    const std::uint32_t cx = objetivo != nullptr ? obs_source_get_base_width(objetivo) : 0;
    const std::uint32_t cy = objetivo != nullptr ? obs_source_get_base_height(objetivo) : 0;
    if (g->fx == nullptr || cx == 0 || cy == 0 || plan->trivial()) {
        obs_source_skip_video_filter(g->ctx);
        return;
    }
    if (cx != g->cx || cy != g->cy) {
        g->cx = cx;
        g->cy = cy;
        g->listo = false;
    }
    const std::uint64_t cuadro = obs_get_video_frame_time();
    if (cuadro != g->ultimoCuadro || !g->listo) {
        // Mientras se calcula, un compositor que la necesite no la vuelve a
        // despertar (usa la toma del cuadro anterior).
        obs_source_t* padre = obs_filter_get_parent(g->ctx);
        g->fuente = padre != nullptr ? obs_source_get_name(padre) : "";
        ba::video::dibujando(g->fuente, true);
        const bool ok = procesar(g, *plan);
        ba::video::dibujando(g->fuente, false);
        if (!ok) {
            obs_source_skip_video_filter(g->ctx);
            return;
        }
        g->ultimoCuadro = cuadro;
        g->listo = true;
    }
    if (plan->escena == -2) return; // `out` sin nada conectado: no se dibuja nada
    gs_texture_t* tex = plan->escena < 0 ? gs_texrender_get_texture(g->entrada)
                                         : gs_texrender_get_texture(g->texs[static_cast<size_t>(plan->escena)]);
    gs_effect_t* fx = obs_get_base_effect(OBS_EFFECT_DEFAULT);
    gs_effect_set_texture(gs_effect_get_param_by_name(fx, "image"), tex);
    while (gs_effect_loop(fx, "Draw")) gs_draw_sprite(tex, 0, g->cx, g->cy);
}

// --- El portador de una tarjeta de OBS ----------------------------------------

const char* portadorNombre(void*) { return "Portador de tarjeta (BA)"; }
void* portadorCrear(obs_data_t*, obs_source_t*) { return new (std::nothrow) Portador(); }
void portadorDestruir(void* data) { delete static_cast<Portador*>(data); }
std::uint32_t portadorAncho(void* data) { return static_cast<Portador*>(data)->cx; }
std::uint32_t portadorAlto(void* data) { return static_cast<Portador*>(data)->cy; }
void portadorDibujar(void* data, gs_effect_t*) {
    auto* x = static_cast<Portador*>(data);
    if (x->tex == nullptr) return;
    gs_effect_t* fx = obs_get_base_effect(OBS_EFFECT_DEFAULT);
    gs_effect_set_texture(gs_effect_get_param_by_name(fx, "image"), x->tex);
    while (gs_effect_loop(fx, "Draw")) gs_draw_sprite(x->tex, 0, x->cx, x->cy);
}

// --- La fuente «Salida» ------------------------------------------------------

const char* salidaNombre(void*) { return obs_module_text("Salida de grafo de video (BA)"); }

void salidaActualizar(void* data, obs_data_t* s) {
    auto* x = static_cast<Salida*>(data);
    x->fuente = obs_data_get_string(s, "fuente");
}

void* salidaCrear(obs_data_t* settings, obs_source_t* ctx) {
    auto* x = new (std::nothrow) Salida();
    if (x == nullptr) return nullptr;
    x->ctx = ctx;
    obs_enter_graphics();
    x->tex = gs_texrender_create(GS_RGBA, GS_ZS_NONE);
    x->despertar = gs_texrender_create(GS_RGBA, GS_ZS_NONE);
    obs_leave_graphics();
    salidaActualizar(x, settings);
    std::lock_guard<std::mutex> l(g_salidasLock);
    salidas()[obs_source_get_name(ctx)] = x;
    return x;
}

void salidaDestruir(void* data) {
    auto* x = static_cast<Salida*>(data);
    {
        std::lock_guard<std::mutex> l(g_salidasLock);
        for (auto it = salidas().begin(); it != salidas().end();)
            it = it->second == x ? salidas().erase(it) : std::next(it);
    }
    obs_enter_graphics();
    gs_texrender_destroy(x->tex);
    gs_texrender_destroy(x->despertar);
    obs_leave_graphics();
    delete x;
}

/// La entrada de la que sale, con referencia (o nulo).
obs_source_t* origen(Salida* x) { return x->fuente.empty() ? nullptr : obs_get_source_by_name(x->fuente.c_str()); }

std::uint32_t salidaAncho(void* data) {
    auto* x = static_cast<Salida*>(data);
    if (x->cx != 0) return x->cx;
    obs_source_t* o = origen(x);
    const std::uint32_t w = o != nullptr ? obs_source_get_width(o) : 0;
    obs_source_release(o);
    return w;
}

std::uint32_t salidaAlto(void* data) {
    auto* x = static_cast<Salida*>(data);
    if (x->cy != 0) return x->cy;
    obs_source_t* o = origen(x);
    const std::uint32_t h = o != nullptr ? obs_source_get_height(o) : 0;
    obs_source_release(o);
    return h;
}

/// Mientras la salida se ve, su entrada tiene que dar cuadros aunque no este
/// en ninguna escena visible (las camaras solo suben cuadros si se muestran).
void salidaMostrar(void* data) {
    auto* x = static_cast<Salida*>(data);
    if (obs_source_t* o = origen(x)) {
        obs_source_inc_showing(o);
        obs_source_release(o);
    }
}

void salidaOcultar(void* data) {
    auto* x = static_cast<Salida*>(data);
    if (obs_source_t* o = origen(x)) {
        obs_source_dec_showing(o);
        obs_source_release(o);
    }
}

void salidaDibujar(void* data, gs_effect_t*) {
    auto* x = static_cast<Salida*>(data);
    // Si en este cuadro nadie dibujo la entrada, el grafo no corrio: se la
    // dibuja aparte (y su grafo llena esta salida).
    if (x->cuadro != obs_get_video_frame_time()) {
        if (obs_source_t* o = origen(x)) {
            const std::uint32_t w = obs_source_get_width(o), h = obs_source_get_height(o);
            if (w != 0 && h != 0) {
                gs_texrender_reset(x->despertar);
                if (gs_texrender_begin(x->despertar, w, h)) {
                    gs_ortho(0.0f, static_cast<float>(w), 0.0f, static_cast<float>(h), -100.0f, 100.0f);
                    obs_source_video_render(o);
                    gs_texrender_end(x->despertar);
                }
            }
            obs_source_release(o);
        }
    }
    if (x->cx == 0 || x->cy == 0) return;
    gs_texture_t* tex = gs_texrender_get_texture(x->tex);
    if (tex == nullptr) return;
    gs_effect_t* fx = obs_get_base_effect(OBS_EFFECT_DEFAULT);
    gs_effect_set_texture(gs_effect_get_param_by_name(fx, "image"), tex);
    while (gs_effect_loop(fx, "Draw")) gs_draw_sprite(tex, 0, x->cx, x->cy);
}

} // namespace

extern "C" {

static struct obs_source_info ba_video_grafo_info = {};
static struct obs_source_info ba_video_salida_info = {};
static struct obs_source_info ba_video_portador_info = {};

/// Registra el filtro del grafo y la fuente de sus salidas. La llama
/// `ba_register_video_filters` (ba_mejora_imagen.cpp).
void ba_register_video_grafo(void) {
    ba_video_grafo_info.id = "ba_video_grafo";
    ba_video_grafo_info.type = OBS_SOURCE_TYPE_FILTER;
    ba_video_grafo_info.output_flags = OBS_SOURCE_VIDEO;
    ba_video_grafo_info.get_name = nombre;
    ba_video_grafo_info.create = crear;
    ba_video_grafo_info.destroy = destruir;
    ba_video_grafo_info.update = actualizar;
    ba_video_grafo_info.video_render = dibujar;
    obs_register_source(&ba_video_grafo_info);

    ba_video_salida_info.id = "ba_video_salida";
    ba_video_salida_info.type = OBS_SOURCE_TYPE_INPUT;
    ba_video_salida_info.output_flags = OBS_SOURCE_VIDEO | OBS_SOURCE_CUSTOM_DRAW;
    ba_video_salida_info.get_name = salidaNombre;
    ba_video_salida_info.create = salidaCrear;
    ba_video_salida_info.destroy = salidaDestruir;
    ba_video_salida_info.update = salidaActualizar;
    ba_video_salida_info.get_width = salidaAncho;
    ba_video_salida_info.get_height = salidaAlto;
    ba_video_salida_info.show = salidaMostrar;
    ba_video_salida_info.hide = salidaOcultar;
    ba_video_salida_info.video_render = salidaDibujar;
    obs_register_source(&ba_video_salida_info);

    ba_video_portador_info.id = "ba_video_portador";
    ba_video_portador_info.type = OBS_SOURCE_TYPE_INPUT;
    // Interno: no se ofrece para agregar a mano.
    ba_video_portador_info.output_flags = OBS_SOURCE_VIDEO | OBS_SOURCE_CUSTOM_DRAW | OBS_SOURCE_CAP_DISABLED;
    ba_video_portador_info.get_name = portadorNombre;
    ba_video_portador_info.create = portadorCrear;
    ba_video_portador_info.destroy = portadorDestruir;
    ba_video_portador_info.get_width = portadorAncho;
    ba_video_portador_info.get_height = portadorAlto;
    ba_video_portador_info.video_render = portadorDibujar;
    obs_register_source(&ba_video_portador_info);
}

} // extern "C"
