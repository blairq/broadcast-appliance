// src/app/video/CatalogoGst.cpp — ver CatalogoGst.hpp

#include "video/CatalogoGst.hpp"

#include "platform/GstEntorno.hpp"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <mutex>

#if defined(BA_HAVE_GST)
#include <gst/gst.h>

#include "Sondas.hpp"
#endif

namespace ba::app::video {

using json = nlohmann::json;

namespace {

/// La clase de GStreamer («Filter/Effect/Video») -> qué es para el grafo.
std::string rolDe(const std::string& clase) {
    const auto tiene = [&](const char* x) { return clase.find(x) != std::string::npos; };
    if (tiene("Source")) return "fuente";
    if (tiene("Sink")) return "salida";
    if (tiene("Mixer") || tiene("Compositor")) return "mezcla";
    if (tiene("Analyzer") || tiene("Detector")) return "analisis";
    if (tiene("Converter") || tiene("Scaler")) return "conversor";
    return "filtro";
}

/// Una categoría en castellano para agrupar el catálogo.
std::string categoriaDe(const std::string& elemento, const std::string& clase) {
    const auto tiene = [&](const std::string& s, const char* x) { return s.find(x) != std::string::npos; };
    if (tiene(clase, "Source")) return "Fuentes";
    if (tiene(clase, "Sink")) return "Salidas";
    if (tiene(clase, "Mixer") || tiene(clase, "Compositor")) return "Mezcla y composición";
    if (tiene(clase, "Analyzer") || tiene(clase, "Detector") || tiene(elemento, "detect")) return "Análisis";
    if (tiene(elemento, "frei0r")) return "Frei0r (efectos)";
    if (tiene(elemento, "gl")) return "OpenGL";
    if (tiene(elemento, "balance") || tiene(elemento, "gamma") || tiene(elemento, "color") ||
        tiene(elemento, "lut") || tiene(elemento, "hsv"))
        return "Color";
    if (tiene(elemento, "alpha") || tiene(elemento, "key")) return "Llaves y transparencia";
    if (tiene(elemento, "crop") || tiene(elemento, "flip") || tiene(elemento, "scale") ||
        tiene(elemento, "box") || tiene(elemento, "rotate") || tiene(elemento, "transform") ||
        tiene(elemento, "mirror"))
        return "Geometría";
    if (tiene(elemento, "overlay") || tiene(elemento, "text") || tiene(elemento, "clock") ||
        tiene(elemento, "time"))
        return "Textos y superposiciones";
    if (tiene(elemento, "deinterlace") || tiene(elemento, "interlace") || tiene(elemento, "rate"))
        return "Formato";
    if (tiene(clase, "Converter")) return "Formato";
    return "Efectos";
}

#if defined(BA_HAVE_GST)
std::once_flag g_una;
bool g_ok = false;
std::string g_err;

bool iniciarGst() {
    std::call_once(g_una, []() {
        if (!ba::platform::prepararGst()) {
            g_err = "GStreamer no está en el programa";
            return;
        }
        // Si el registro se rehace acá (y no en el motor): lo mismo que motor::iniciarGStreamer.
        const char* gstDebug = std::getenv("GST_DEBUG");
        const bool delUsuario = gstDebug != nullptr && *gstDebug != '\0';
        const bool rehacer = ba::platform::empezarRegistro();
        GError* e = nullptr;
        g_ok = gst_init_check(nullptr, nullptr, &e);
        if (!g_ok) g_err = e != nullptr ? e->message : "gst_init falló";
        if (e != nullptr) g_error_free(e);
        if (rehacer && !delUsuario) gst_debug_set_threshold_for_name("GST_PLUGIN_LOADING", GST_LEVEL_NONE);
        ba::platform::registroTerminado(g_ok);
        // Lo mismo que motor::quitarPluginsDelSistema (este catálogo no enlaza el motor).
        if (g_ok)
            if (GstPlugin* p = gst_registry_find_plugin(gst_registry_get(), "directshow")) {
                gst_registry_remove_plugin(gst_registry_get(), p);
                gst_object_unref(p);
            }
    });
    return g_ok;
}

std::string meta(GstElementFactory* f, const char* k) {
    const gchar* v = gst_element_factory_get_metadata(f, k);
    return v != nullptr ? v : "";
}

/// ¿Es un módulo de video (y no de audio, ni demuxer, ni red de bajo nivel)?
bool esDeVideo(const std::string& clase) {
    if (clase.find("Video") == std::string::npos && clase.find("Image") == std::string::npos) return false;
    for (const char* no :
         {"Demuxer", "Muxer", "Parser", "Decoder", "Encoder", "Payloader", "Depayloader", "Audio"})
        if (clase.find(no) != std::string::npos) return false;
    return true;
}

json valorJson(const GValue* v) {
    switch (G_TYPE_FUNDAMENTAL(G_VALUE_TYPE(v))) {
        case G_TYPE_BOOLEAN: return static_cast<bool>(g_value_get_boolean(v));
        case G_TYPE_INT: return g_value_get_int(v);
        case G_TYPE_UINT: return g_value_get_uint(v);
        case G_TYPE_INT64: return g_value_get_int64(v);
        case G_TYPE_UINT64: return g_value_get_uint64(v);
        case G_TYPE_LONG: return g_value_get_long(v);
        case G_TYPE_ULONG: return g_value_get_ulong(v);
        case G_TYPE_FLOAT: return g_value_get_float(v);
        case G_TYPE_DOUBLE: return g_value_get_double(v);
        case G_TYPE_STRING: {
            const gchar* s = g_value_get_string(v);
            return s != nullptr ? s : "";
        }
        case G_TYPE_ENUM: {
            GEnumClass* c = G_ENUM_CLASS(g_type_class_ref(G_VALUE_TYPE(v)));
            GEnumValue* e = g_enum_get_value(c, g_value_get_enum(v));
            json r = e != nullptr ? json(e->value_nick) : json(g_value_get_enum(v));
            g_type_class_unref(c);
            return r;
        }
        case G_TYPE_FLAGS: {
            GFlagsClass* c = G_FLAGS_CLASS(g_type_class_ref(G_VALUE_TYPE(v)));
            std::string s;
            const guint val = g_value_get_flags(v);
            for (guint i = 0; i < c->n_values; ++i)
                if (c->values[i].value != 0 && (val & c->values[i].value) == c->values[i].value)
                    s += (s.empty() ? "" : "+") + std::string(c->values[i].value_nick);
            g_type_class_unref(c);
            return s;
        }
        default: return nullptr;
    }
}
#endif

} // namespace

bool iniciarGStreamer(std::string* error) {
#if defined(BA_HAVE_GST)
    const bool ok = iniciarGst();
    if (!ok && error != nullptr) *error = g_err;
    return ok;
#else
    if (error != nullptr) *error = "esta compilación no trae GStreamer";
    return false;
#endif
}

CatalogoGst::CatalogoGst()
    : error_("GStreamer se está iniciando (la primera vez revisa sus módulos: unos segundos)") {}

CatalogoGst::CatalogoGst(const std::filesystem::path& docs) {
    std::ifstream in(docs);
    const json d = json::parse(in, nullptr, false);
    if (d.is_object()) docs_ = d.value("modulos", json::object());
    iniciar();
}

CatalogoGst::CatalogoGst(const json& docs) {
    if (docs.is_object()) docs_ = docs.value("modulos", json::object());
    iniciar();
}

void CatalogoGst::iniciar() {
#if defined(BA_HAVE_GST)
    disponible_ = iniciarGst();
    if (!disponible_) error_ = g_err;
#else
    error_ = "esta compilación no trae GStreamer";
#endif
}

std::string CatalogoGst::version() const {
#if defined(BA_HAVE_GST)
    if (disponible_) {
        gchar* v = gst_version_string();
        std::string s = v;
        g_free(v);
        return s;
    }
#endif
    return {};
}

const json& CatalogoGst::modulos() const {
    if (modulos_.is_array()) return modulos_;
    json out = json::array();
#if defined(BA_HAVE_GST)
    if (!disponible_) return modulos_ = out;
    GList* lista = gst_registry_get_feature_list(gst_registry_get(), GST_TYPE_ELEMENT_FACTORY);
    for (GList* l = lista; l != nullptr; l = l->next) {
        auto* f = GST_ELEMENT_FACTORY(l->data);
        const std::string clase = meta(f, GST_ELEMENT_METADATA_KLASS);
        if (!esDeVideo(clase)) continue;
        const std::string elemento = GST_OBJECT_NAME(f);
        json m{{"elemento", elemento},
               {"nombre", meta(f, GST_ELEMENT_METADATA_LONGNAME)},
               {"clase", clase},
               {"rol", rolDe(clase)},
               {"categoria", categoriaDe(elemento, clase)},
               {"descripcion", meta(f, GST_ELEMENT_METADATA_DESCRIPTION)},
               {"documentacion", meta(f, GST_ELEMENT_METADATA_DOC_URI)}};
        if (GstPlugin* p = gst_plugin_feature_get_plugin(GST_PLUGIN_FEATURE(f))) {
            m["plugin"] = gst_plugin_get_name(p);
            m["licencia"] = gst_plugin_get_license(p) ? gst_plugin_get_license(p) : "";
            m["paquete"] = gst_plugin_get_source(p) ? gst_plugin_get_source(p) : "";
            gst_object_unref(p);
        }
        // Sin documentación: va como tarjeta si es un filtro.
        m["tarjeta"] = m["rol"] == "filtro";
        if (docs_.contains(elemento))
            for (const char* k : {"titulo", "para", "uso", "categoria", "tarjeta", "comoTarjeta"})
                if (docs_[elemento].contains(k)) m[k] = docs_[elemento][k];
        out.push_back(std::move(m));
    }
    gst_plugin_feature_list_free(lista);
    std::sort(out.begin(), out.end(), [](const json& a, const json& b) {
        const std::string ca = a.value("categoria", ""), cb = b.value("categoria", "");
        return ca != cb ? ca < cb : a.value("elemento", "") < b.value("elemento", "");
    });
#endif
    return modulos_ = out;
}

const json& CatalogoGst::modulosAudio() const {
    if (modulosAudio_.is_array()) return modulosAudio_;
    json out = json::array();
#if defined(BA_HAVE_GST)
    if (!disponible_) return modulosAudio_ = out;
    GList* lista = gst_registry_get_feature_list(gst_registry_get(), GST_TYPE_ELEMENT_FACTORY);
    for (GList* l = lista; l != nullptr; l = l->next) {
        auto* f = GST_ELEMENT_FACTORY(l->data);
        const std::string clase = meta(f, GST_ELEMENT_METADATA_KLASS);
        // Un filtro de audio: entra audio y sale audio (efectos, filtros, dinámica, conversión de nivel).
        if (clase.find("Audio") == std::string::npos) continue;
        if (clase.find("Filter") == std::string::npos && clase.find("Effect") == std::string::npos) continue;
        bool no = false;
        for (const char* x : {"Source", "Sink", "Demuxer", "Muxer", "Parser", "Decoder", "Encoder", "Payloader",
                              "Depayloader", "Video", "Mixer"})
            if (clase.find(x) != std::string::npos) no = true;
        if (no) continue;
        const std::string elemento = GST_OBJECT_NAME(f);
        json m{{"elemento", elemento},
               {"nombre", meta(f, GST_ELEMENT_METADATA_LONGNAME)},
               {"clase", clase},
               {"descripcion", meta(f, GST_ELEMENT_METADATA_DESCRIPTION)}};
        if (docs_.contains(elemento))
            for (const char* k : {"titulo", "para", "uso"})
                if (docs_[elemento].contains(k)) m[k] = docs_[elemento][k];
        out.push_back(std::move(m));
    }
    gst_plugin_feature_list_free(lista);
    std::sort(out.begin(), out.end(),
              [](const json& a, const json& b) { return a.value("elemento", "") < b.value("elemento", ""); });
#endif
    return modulosAudio_ = out;
}

json CatalogoGst::modulo(const std::string& elemento) const {
    for (const auto& m : modulos())
        if (m.value("elemento", "") == elemento) return m;
    return json::object();
}

json CatalogoGst::interior(const std::string& elemento) const {
#if defined(BA_HAVE_GST)
    if (!disponible_) return {{"existe", false}};
    GstElementFactory* f = gst_element_factory_find(elemento.c_str());
    if (f == nullptr) return {{"existe", false}};
    const auto meta = [f](const char* k) {
        const gchar* v = gst_element_factory_get_metadata(f, k);
        return std::string(v != nullptr ? v : "");
    };
    const auto ent = ::ba::motor::memoriasPosibles(f, GST_PAD_SINK), sal = ::ba::motor::memoriasPosibles(f, GST_PAD_SRC);
    bool gpu = false;
    for (const auto& m : ent) gpu = gpu || ::ba::motor::esGpu(m);
    for (const auto& m : sal) gpu = gpu || ::ba::motor::esGpu(m);
    GstPlugin* pl = gst_plugin_feature_get_plugin(GST_PLUGIN_FEATURE(f));
    const std::string plugin = pl != nullptr ? gst_plugin_get_name(pl) : "";
    if (pl != nullptr) gst_object_unref(pl);
    gst_object_unref(f);
    return {{"existe", true},
            {"clase", meta(GST_ELEMENT_METADATA_KLASS)},
            {"descripcion", meta(GST_ELEMENT_METADATA_DESCRIPTION)},
            {"autor", meta(GST_ELEMENT_METADATA_AUTHOR)},
            {"plugin", plugin},
            {"memorias", {{"entrada", ent}, {"salida", sal}}},
            {"gpu", gpu}};
#else
    (void)elemento;
    return {{"existe", false}};
#endif
}

json CatalogoGst::propiedades(const std::string& elemento, const json& actuales) const {
    json out = json::array();
#if defined(BA_HAVE_GST)
    if (!disponible_) return out;
    GstElement* e = gst_element_factory_make(elemento.c_str(), nullptr);
    if (e == nullptr) return out;
    guint n = 0;
    GParamSpec** ps = g_object_class_list_properties(G_OBJECT_GET_CLASS(e), &n);
    for (guint i = 0; i < n; ++i) {
        GParamSpec* p = ps[i];
        const std::string nombre = g_param_spec_get_name(p);
        // Lo que no se ajusta desde una tarjeta: las de solo lectura, las de
        // construcción, y las de la maquinaria de GStreamer.
        if (!(p->flags & G_PARAM_WRITABLE) || (p->flags & G_PARAM_CONSTRUCT_ONLY) || nombre == "name" ||
            nombre == "parent" || nombre == "qos")
            continue;
        json r{{"nombre", nombre},
               {"descripcion", g_param_spec_get_blurb(p) ? g_param_spec_get_blurb(p) : ""},
               {"enVivo",
                (p->flags & GST_PARAM_MUTABLE_PLAYING) != 0 || (p->flags & GST_PARAM_CONTROLLABLE) != 0}};
        GValue v = G_VALUE_INIT;
        g_value_init(&v, p->value_type);
        g_object_get_property(G_OBJECT(e), nombre.c_str(), &v);
        r["defecto"] = valorJson(&v);
        g_value_unset(&v);
        const GType t = G_PARAM_SPEC_TYPE(p);
        if (t == G_TYPE_PARAM_BOOLEAN) {
            r["tipo"] = "bool";
        } else if (t == G_TYPE_PARAM_INT) {
            r["tipo"] = "int", r["min"] = G_PARAM_SPEC_INT(p)->minimum,
            r["max"] = G_PARAM_SPEC_INT(p)->maximum;
        } else if (t == G_TYPE_PARAM_UINT) {
            r["tipo"] = "int", r["min"] = G_PARAM_SPEC_UINT(p)->minimum,
            r["max"] = G_PARAM_SPEC_UINT(p)->maximum;
        } else if (t == G_TYPE_PARAM_INT64) {
            r["tipo"] = "int", r["min"] = G_PARAM_SPEC_INT64(p)->minimum,
            r["max"] = G_PARAM_SPEC_INT64(p)->maximum;
        } else if (t == G_TYPE_PARAM_UINT64) {
            r["tipo"] = "int", r["min"] = 0, r["max"] = static_cast<double>(G_PARAM_SPEC_UINT64(p)->maximum);
        } else if (t == G_TYPE_PARAM_LONG) {
            r["tipo"] = "int", r["min"] = G_PARAM_SPEC_LONG(p)->minimum,
            r["max"] = G_PARAM_SPEC_LONG(p)->maximum;
        } else if (t == G_TYPE_PARAM_FLOAT) {
            r["tipo"] = "double", r["min"] = G_PARAM_SPEC_FLOAT(p)->minimum,
            r["max"] = G_PARAM_SPEC_FLOAT(p)->maximum;
        } else if (t == G_TYPE_PARAM_DOUBLE) {
            r["tipo"] = "double", r["min"] = G_PARAM_SPEC_DOUBLE(p)->minimum,
            r["max"] = G_PARAM_SPEC_DOUBLE(p)->maximum;
        } else if (t == G_TYPE_PARAM_STRING) {
            r["tipo"] = "string";
        } else if (t == G_TYPE_PARAM_ENUM || t == G_TYPE_PARAM_FLAGS) {
            r["tipo"] = t == G_TYPE_PARAM_ENUM ? "enum" : "flags";
            json ops = json::array();
            if (t == G_TYPE_PARAM_ENUM) {
                GEnumClass* c = G_PARAM_SPEC_ENUM(p)->enum_class;
                for (guint k = 0; k < c->n_values; ++k)
                    ops.push_back({{"valor", c->values[k].value},
                                   {"nick", c->values[k].value_nick},
                                   {"descripcion", c->values[k].value_name}});
            } else {
                GFlagsClass* c = G_PARAM_SPEC_FLAGS(p)->flags_class;
                for (guint k = 0; k < c->n_values; ++k)
                    ops.push_back({{"valor", c->values[k].value},
                                   {"nick", c->values[k].value_nick},
                                   {"descripcion", c->values[k].value_name}});
            }
            r["opciones"] = ops;
        } else {
            continue; // caps, objetos, punteros: no tienen formulario
        }
        r["valor"] = actuales.is_object() && actuales.contains(nombre) ? actuales[nombre] : r["defecto"];
        out.push_back(std::move(r));
    }
    g_free(ps);
    gst_object_unref(e);
#else
    (void)elemento;
    (void)actuales;
#endif
    return out;
}

} // namespace ba::app::video
