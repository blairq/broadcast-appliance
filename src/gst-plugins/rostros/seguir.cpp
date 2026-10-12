// ============================================================================
// src/gst-plugins/rostros/seguir.cpp — baseguir: el seguimiento de una cara
//
// El «cerebro» entre los ojos (barostros) y los músculos (baptz, o una cámara
// motorizada). NO TOCA LA IMAGEN: deja pasar cada cuadro tal cual (en cualquier
// memoria: por eso se registra también como baseguirgl y baseguird3d12, para
// que el motor no le baje la imagen de la GPU). Lo que hace es escuchar:
//
//   «ba/rostros» (de una tarjeta Rostros, antes o después: llega por abajo o
//                por arriba): las caras, con su número, su
//                caja en la toma entera (`toma`) y quién es si se sabe;
//   «ba/ptz»     (de un PTZ antes): qué parte de la toma se está viendo;
//   «ba/control» campo `seguir_objetivo`: cambiar a quién seguir andando,
//
// y decidir el encuadre. Por cada análisis, si hay a quién seguir:
//   - un evento «ba/encuadre» (x, y, zoom, suavidad; o soltar=true) HACIA ABAJO
//     y HACIA ARRIBA (GST_EVENT_CUSTOM_DOWNSTREAM y _UPSTREAM): un baptz que
//     esté después o antes lo sigue solo;
//   - un mensaje «ba-seguir» al bus (JSON: estado, objetivo, la cara, el
//     encuadre): la app lo usa para una cámara motorizada (VISCA, ONVIF), que no
//     está en el pipeline.
// Todo con lo estándar de GStreamer (eventos propios, mensajes de elemento,
// propiedades): se ve y se prueba con las herramientas de siempre.
//
// A QUIÉN (`objetivo`):
//   ""            nadie (pasa sin hacer nada)
//   "id:N"        la cara número N (el que le puso barostros)
//   "persona:X"   la persona X (id de su ficha o su nombre: hace falta que
//                 Personas la reconozca); si se pierde, la vuelve a encontrar
//   "x,y"         la cara más cercana a ese punto de la toma (un clic): queda
//                 enganchada a su número
//   "mayor"       la más grande;  "centro": la más cercana al centro
// Si la cara se pierde, se busca cerca de donde estaba durante `perdida-s`
// segundos; después, se suelta («perdida»).
//
// EL ENCUADRE: la cara en el centro horizontal y un poco arriba del medio
// (`aire`: cuánto del alto de la ventana queda entre el centro de la cara y el
// de la ventana). `zoom` > 0 lo fija; `tamano-cara` > 0 lo calcula para que la
// cara ocupe esa parte del alto; si no, no toca el zoom. `zona-muerta`: lo que
// se puede correr la cara sin mover nada (que no tiemble).
// ============================================================================
#include <gst/base/gstbasetransform.h>
#include <gst/gst.h>

#include <nlohmann/json.hpp>

#include <cmath>
#include <mutex>
#include <string>

G_BEGIN_DECLS
#define BA_TYPE_SEGUIR (ba_seguir_get_type())
G_DECLARE_FINAL_TYPE(BaSeguir, ba_seguir, BA, SEGUIR, GstBaseTransform)
G_END_DECLS

namespace {

struct Est {
    std::mutex mu;
    // Ajustes.
    bool activo = true;
    std::string objetivo;
    double zoom = 0, tamanoCara = 0, aire = 0.12, suavidad = 0.6, zonaMuerta = 0.04, perdidaS = 3.0;
    // Lo que se sabe.
    int enganchada = -1;                  ///< el número de la cara que se sigue
    double ultX = 0.5, ultY = 0.5;        ///< dónde se la vio (centro, en la toma)
    gint64 vistaUs = 0;
    bool siguiendo = false;               ///< mandó un encuadre que todavía no soltó
    double encX = -1, encY = -1, encZ = 0; ///< el último encuadre mandado
    double ventanaZoom = 1.0;
    std::string estado = "libre";
    std::string ultimo = "{}";
    bool objetivoNuevo = false;
};

} // namespace

struct _BaSeguir {
    GstBaseTransform parent;
    Est* st;
};

G_DEFINE_TYPE(BaSeguir, ba_seguir, GST_TYPE_BASE_TRANSFORM)

enum {
    PROP_0,
    PROP_ACTIVO,
    PROP_OBJETIVO,
    PROP_ZOOM,
    PROP_TAMANO,
    PROP_AIRE,
    PROP_SUAVIDAD,
    PROP_ZONA,
    PROP_PERDIDA,
    PROP_ESTADO,
    PROP_SEGUIDO
};

namespace {

struct Cara {
    int id = 0;
    double x = 0, y = 0, w = 0, h = 0, conf = 0;
    std::string nombre, persona;
    double cx() const { return x + w / 2; }
    double cy() const { return y + h / 2; }
};

std::vector<Cara> leerCaras(const nlohmann::json& j) {
    std::vector<Cara> out;
    for (const auto& c : j.value("caras", nlohmann::json::array())) {
        Cara k;
        k.id = c.value("id", 0);
        const nlohmann::json t = c.contains("toma") ? c["toma"] : c; // en la toma entera si se sabe
        k.x = t.value("x", 0.0);
        k.y = t.value("y", 0.0);
        k.w = t.value("ancho", 0.0);
        k.h = t.value("alto", 0.0);
        k.conf = c.value("confianza", 0.0);
        k.nombre = c.value("nombre", std::string());
        k.persona = c.value("persona", std::string());
        out.push_back(std::move(k));
    }
    return out;
}

/// Cuál seguir (índice en `caras`, -1 = ninguna). Bajo el lock.
int elegir(Est* st, const std::vector<Cara>& caras, gint64 ahora) {
    const std::string& o = st->objetivo;
    auto porId = [&](int id) {
        for (std::size_t i = 0; i < caras.size(); ++i)
            if (caras[i].id == id) return static_cast<int>(i);
        return -1;
    };
    auto masCerca = [&](double x, double y, double tope) {
        int mejor = -1;
        double dmin = tope;
        for (std::size_t i = 0; i < caras.size(); ++i) {
            const double d = std::hypot(caras[i].cx() - x, caras[i].cy() - y);
            if (d < dmin) {
                dmin = d;
                mejor = static_cast<int>(i);
            }
        }
        return mejor;
    };
    if (st->objetivoNuevo) {
        st->objetivoNuevo = false;
        st->enganchada = -1;
        if (o.rfind("id:", 0) == 0) {
            st->enganchada = std::atoi(o.c_str() + 3);
        } else if (o.find(',') != std::string::npos && o.rfind("persona:", 0) != 0) {
            st->ultX = g_ascii_strtod(o.c_str(), nullptr);
            st->ultY = g_ascii_strtod(o.c_str() + o.find(',') + 1, nullptr);
            const int i = masCerca(st->ultX, st->ultY, 10.0);
            if (i >= 0) st->enganchada = caras[static_cast<size_t>(i)].id;
            st->vistaUs = ahora;
        }
    }
    int i = st->enganchada >= 0 ? porId(st->enganchada) : -1;
    if (i < 0 && o.rfind("persona:", 0) == 0) {
        const std::string quien = o.substr(8);
        for (std::size_t k = 0; k < caras.size() && i < 0; ++k)
            if ((!caras[k].persona.empty() && caras[k].persona == quien) || (!caras[k].nombre.empty() && caras[k].nombre == quien))
                i = static_cast<int>(k);
    }
    if (i < 0 && st->enganchada < 0 && (o == "mayor" || o == "centro") && !caras.empty()) {
        if (o == "mayor") {
            i = 0;
            for (std::size_t k = 1; k < caras.size(); ++k)
                if (caras[k].h * caras[k].w > caras[static_cast<size_t>(i)].h * caras[static_cast<size_t>(i)].w)
                    i = static_cast<int>(k);
        } else {
            i = masCerca(0.5, 0.5, 10.0);
        }
    }
    // Perdida hace poco: la más cercana a donde estaba (barostros pudo cambiarle el número).
    if (i < 0 && st->vistaUs > 0 && ahora - st->vistaUs < static_cast<gint64>(st->perdidaS * 1e6))
        i = masCerca(st->ultX, st->ultY, 0.15);
    if (i >= 0) st->enganchada = caras[static_cast<size_t>(i)].id;
    return i;
}

void publicar(BaSeguir* self, GstEvent* ev, const std::string& js) {
    GstBaseTransform* bt = GST_BASE_TRANSFORM(self);
    if (ev != nullptr) {
        // El mismo encuadre HACIA ARRIBA (GST_EVENT_CUSTOM_UPSTREAM): lo recibe un
        // PTZ que esté antes en la cadena. Hacia abajo, uno que esté después.
        GstEvent* arriba = gst_event_new_custom(GST_EVENT_CUSTOM_UPSTREAM, gst_structure_copy(gst_event_get_structure(ev)));
        gst_pad_push_event(GST_BASE_TRANSFORM_SINK_PAD(bt), arriba);
        gst_pad_push_event(GST_BASE_TRANSFORM_SRC_PAD(bt), ev);
    }
    GstStructure* s = gst_structure_new("ba-seguir", "json", G_TYPE_STRING, js.c_str(), nullptr);
    gst_element_post_message(GST_ELEMENT(self), gst_message_new_element(GST_OBJECT(self), s));
}

void alCaras(BaSeguir* self, const char* texto) {
    Est* st = self->st;
    const nlohmann::json j = nlohmann::json::parse(texto != nullptr ? texto : "", nullptr, false);
    if (!j.is_object()) return;
    const std::vector<Cara> caras = leerCaras(j);
    const gint64 ahora = g_get_monotonic_time();
    GstEvent* ev = nullptr;
    std::string js;
    {
        std::lock_guard<std::mutex> l(st->mu);
        if (!st->activo || st->objetivo.empty()) {
            if (st->siguiendo) {
                st->siguiendo = false;
                st->estado = "libre";
                ev = gst_event_new_custom(GST_EVENT_CUSTOM_DOWNSTREAM,
                                          gst_structure_new("ba/encuadre", "soltar", G_TYPE_BOOLEAN, TRUE, nullptr));
                js = nlohmann::json{{"estado", "libre"}, {"objetivo", st->objetivo}}.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
                st->ultimo = js;
            }
        } else {
            const int i = elegir(st, caras, ahora);
            nlohmann::json out{{"objetivo", st->objetivo}};
            if (i >= 0) {
                const Cara& c = caras[static_cast<size_t>(i)];
                st->ultX = c.cx();
                st->ultY = c.cy();
                st->vistaUs = ahora;
                double z = st->zoom > 0 ? st->zoom
                           : st->tamanoCara > 0 && c.h > 0 ? std::clamp(st->tamanoCara / c.h, 1.0, 8.0)
                                                           : 0.0;
                const double zRef = z > 0 ? z : std::max(1.0, st->ventanaZoom);
                const double x = c.cx(), y = c.cy() + st->aire / zRef;
                // Zona muerta: si la cara apenas se movió, el encuadre queda (no tiembla).
                const double muerta = st->zonaMuerta / zRef;
                const bool mover = !st->siguiendo || std::fabs(x - st->encX) > muerta || std::fabs(y - st->encY) > muerta ||
                                   (z > 0 && std::fabs(z - st->encZ) > 0.05 * z);
                if (mover) {
                    st->encX = x;
                    st->encY = y;
                    st->encZ = z;
                }
                st->siguiendo = true;
                st->estado = "siguiendo";
                ev = gst_event_new_custom(GST_EVENT_CUSTOM_DOWNSTREAM,
                                          gst_structure_new("ba/encuadre", "x", G_TYPE_DOUBLE, st->encX, "y", G_TYPE_DOUBLE,
                                                            st->encY, "zoom", G_TYPE_DOUBLE, st->encZ, "suavidad",
                                                            G_TYPE_DOUBLE, st->suavidad, "id", G_TYPE_INT, c.id, nullptr));
                out["estado"] = "siguiendo";
                out["cara"] = {{"id", c.id}, {"x", c.x}, {"y", c.y}, {"ancho", c.w}, {"alto", c.h}, {"confianza", c.conf}};
                if (!c.nombre.empty()) out["cara"]["nombre"] = c.nombre;
                if (!c.persona.empty()) out["cara"]["persona"] = c.persona;
                out["encuadre"] = {{"x", st->encX}, {"y", st->encY}, {"zoom", st->encZ}, {"suavidad", st->suavidad}};
            } else if (st->vistaUs > 0 && ahora - st->vistaUs < static_cast<gint64>(st->perdidaS * 1e6)) {
                st->estado = "buscando";
                out["estado"] = "buscando";
            } else {
                st->estado = st->vistaUs > 0 ? "perdida" : "buscando";
                out["estado"] = st->estado;
                if (st->siguiendo) {
                    st->siguiendo = false;
                    ev = gst_event_new_custom(GST_EVENT_CUSTOM_DOWNSTREAM,
                                              gst_structure_new("ba/encuadre", "soltar", G_TYPE_BOOLEAN, TRUE, nullptr));
                }
            }
            js = out.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
            st->ultimo = js;
        }
    }
    if (!js.empty()) publicar(self, ev, js);
}

gboolean alEvento(GstBaseTransform* bt, GstEvent* e) {
    auto* self = BA_SEGUIR(bt);
    if (GST_EVENT_TYPE(e) == GST_EVENT_CUSTOM_DOWNSTREAM || GST_EVENT_TYPE(e) == GST_EVENT_CUSTOM_DOWNSTREAM_OOB) {
        const GstStructure* s = gst_event_get_structure(e);
        if (s != nullptr && gst_structure_has_name(s, "ba/ptz")) {
            std::lock_guard<std::mutex> l(self->st->mu);
            gst_structure_get_double(s, "zoom", &self->st->ventanaZoom);
        } else if (s != nullptr && gst_structure_has_name(s, "ba/control")) {
            if (const gchar* o = gst_structure_get_string(s, "seguir_objetivo")) {
                std::lock_guard<std::mutex> l(self->st->mu);
                self->st->objetivo = o;
                self->st->objetivoNuevo = true;
            }
        }
        // ba/rostros: después de mandarlo río abajo (lo que venga ve las caras antes que el encuadre).
        const bool caras = s != nullptr && gst_structure_has_name(s, "ba/rostros");
        std::string texto = caras && gst_structure_get_string(s, "json") ? gst_structure_get_string(s, "json") : "";
        const gboolean ok = GST_BASE_TRANSFORM_CLASS(ba_seguir_parent_class)->sink_event(bt, e);
        if (caras) alCaras(self, texto.c_str());
        return ok;
    }
    return GST_BASE_TRANSFORM_CLASS(ba_seguir_parent_class)->sink_event(bt, e);
}

/// De abajo: las caras de un Rostros que esté DESPUÉS (las manda también hacia arriba).
gboolean alEventoArriba(GstBaseTransform* bt, GstEvent* e) {
    auto* self = BA_SEGUIR(bt);
    const GstStructure* s = gst_event_get_structure(e);
    const bool caras = GST_EVENT_TYPE(e) == GST_EVENT_CUSTOM_UPSTREAM && s != nullptr && gst_structure_has_name(s, "ba/rostros");
    std::string texto = caras && gst_structure_get_string(s, "json") ? gst_structure_get_string(s, "json") : "";
    const gboolean ok = GST_BASE_TRANSFORM_CLASS(ba_seguir_parent_class)->src_event(bt, e);
    if (caras) alCaras(self, texto.c_str());
    return ok;
}

GstFlowReturn pasar(GstBaseTransform*, GstBuffer*) { return GST_FLOW_OK; }

void ponerProp(GObject* o, guint id, const GValue* v, GParamSpec* ps) {
    Est* st = BA_SEGUIR(o)->st;
    std::lock_guard<std::mutex> l(st->mu);
    switch (id) {
        case PROP_ACTIVO: st->activo = g_value_get_boolean(v); break;
        case PROP_OBJETIVO:
            st->objetivo = g_value_get_string(v) != nullptr ? g_value_get_string(v) : "";
            st->objetivoNuevo = true;
            break;
        case PROP_ZOOM: st->zoom = g_value_get_double(v); break;
        case PROP_TAMANO: st->tamanoCara = g_value_get_double(v); break;
        case PROP_AIRE: st->aire = g_value_get_double(v); break;
        case PROP_SUAVIDAD: st->suavidad = g_value_get_double(v); break;
        case PROP_ZONA: st->zonaMuerta = g_value_get_double(v); break;
        case PROP_PERDIDA: st->perdidaS = g_value_get_double(v); break;
        default: G_OBJECT_WARN_INVALID_PROPERTY_ID(o, id, ps); break;
    }
}

void leerProp(GObject* o, guint id, GValue* v, GParamSpec* ps) {
    Est* st = BA_SEGUIR(o)->st;
    std::lock_guard<std::mutex> l(st->mu);
    switch (id) {
        case PROP_ACTIVO: g_value_set_boolean(v, st->activo); break;
        case PROP_OBJETIVO: g_value_set_string(v, st->objetivo.c_str()); break;
        case PROP_ZOOM: g_value_set_double(v, st->zoom); break;
        case PROP_TAMANO: g_value_set_double(v, st->tamanoCara); break;
        case PROP_AIRE: g_value_set_double(v, st->aire); break;
        case PROP_SUAVIDAD: g_value_set_double(v, st->suavidad); break;
        case PROP_ZONA: g_value_set_double(v, st->zonaMuerta); break;
        case PROP_PERDIDA: g_value_set_double(v, st->perdidaS); break;
        case PROP_ESTADO: g_value_set_string(v, st->estado.c_str()); break;
        case PROP_SEGUIDO: g_value_set_string(v, st->ultimo.c_str()); break;
        default: G_OBJECT_WARN_INVALID_PROPERTY_ID(o, id, ps); break;
    }
}

} // namespace

static void ba_seguir_init(BaSeguir* self) {
    self->st = new Est();
    gst_base_transform_set_passthrough(GST_BASE_TRANSFORM(self), TRUE);
    gst_base_transform_set_in_place(GST_BASE_TRANSFORM(self), TRUE);
}

static void ba_seguir_finalize(GObject* o) {
    delete BA_SEGUIR(o)->st;
    G_OBJECT_CLASS(ba_seguir_parent_class)->finalize(o);
}

static void ba_seguir_class_init(BaSeguirClass* k) {
    auto* go = G_OBJECT_CLASS(k);
    go->set_property = ponerProp;
    go->get_property = leerProp;
    go->finalize = ba_seguir_finalize;
    const auto rw = static_cast<GParamFlags>(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_PLAYING);
    const auto ro = static_cast<GParamFlags>(G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
    g_object_class_install_property(go, PROP_ACTIVO, g_param_spec_boolean("activo", "Activo", "Seguir (si no, pasa sin hacer nada)", TRUE, rw));
    g_object_class_install_property(go, PROP_OBJETIVO, g_param_spec_string("objetivo", "Objetivo", "\"\" | id:N | persona:X | x,y | mayor | centro", "", rw));
    g_object_class_install_property(go, PROP_ZOOM, g_param_spec_double("zoom", "Zoom", "El zoom que busca (0 = no tocarlo)", 0, 8, 0, rw));
    g_object_class_install_property(go, PROP_TAMANO, g_param_spec_double("tamano-cara", "Tamaño de la cara", "Qué parte del alto ocupa la cara (0 = no ajustar el zoom)", 0, 1, 0, rw));
    g_object_class_install_property(go, PROP_AIRE, g_param_spec_double("aire", "Aire", "Cuánto queda la cara arriba del centro (fracción del alto)", -0.5, 0.5, 0.12, rw));
    g_object_class_install_property(go, PROP_SUAVIDAD, g_param_spec_double("suavidad", "Suavidad", "Constante de tiempo del movimiento (s)", 0.05, 10, 0.6, rw));
    g_object_class_install_property(go, PROP_ZONA, g_param_spec_double("zona-muerta", "Zona muerta", "Lo que se corre la cara sin mover nada (fracción)", 0, 0.5, 0.04, rw));
    g_object_class_install_property(go, PROP_PERDIDA, g_param_spec_double("perdida-s", "Espera al perderla", "Segundos que la busca donde estaba", 0, 60, 3, rw));
    g_object_class_install_property(go, PROP_ESTADO, g_param_spec_string("estado", "Estado", "libre | siguiendo | buscando | perdida", "libre", ro));
    g_object_class_install_property(go, PROP_SEGUIDO, g_param_spec_string("seguido", "Seguido", "Lo último (JSON)", "{}", ro));
    auto* ek = GST_ELEMENT_CLASS(k);
    gst_element_class_set_static_metadata(ek, "Seguimiento", "Filter/Analyzer/Video",
                                          "Elige una cara y decide el encuadre para el PTZ (no toca la imagen)",
                                          "Broadcast Appliance");
    GstCaps* caps = gst_caps_from_string("video/x-raw(ANY)");
    gst_element_class_add_pad_template(ek, gst_pad_template_new("sink", GST_PAD_SINK, GST_PAD_ALWAYS, caps));
    gst_element_class_add_pad_template(ek, gst_pad_template_new("src", GST_PAD_SRC, GST_PAD_ALWAYS, caps));
    gst_caps_unref(caps);
    auto* bk = GST_BASE_TRANSFORM_CLASS(k);
    bk->sink_event = alEvento;
    bk->src_event = alEventoArriba;
    bk->transform_ip = pasar;
    bk->passthrough_on_same_caps = TRUE;
}

extern "C" gboolean ba_registrar_seguir(GstPlugin* p) {
    // Una sola clase, tres nombres: el motor busca «<elemento>gl» (o d3d12) para
    // no bajar la imagen de la GPU, y este acepta cualquier memoria.
    bool ok = gst_element_register(p, "baseguir", GST_RANK_NONE, BA_TYPE_SEGUIR) &&
              gst_element_register(p, "baseguirgl", GST_RANK_NONE, BA_TYPE_SEGUIR);
#if defined(_WIN32)
    ok = ok && gst_element_register(p, "baseguird3d12", GST_RANK_NONE, BA_TYPE_SEGUIR);
#endif
    return ok;
}
