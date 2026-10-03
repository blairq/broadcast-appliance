// ============================================================================
// src/obs-module/ba_plugin_gst.cpp — los plugins de Python adentro de GStreamer
//
// Un plugin (docs/PLUGINS.md) corre en SU proceso; su video y su audio entran
// y salen por dos módulos de GStreamer que se registran acá:
//
//   - `baplugin`    una TARJETA: cada cuadro (o bloque de audio) va al plugin,
//                   que lo cambia, y vuelve. Va en una tarjeta `gst` del grafo
//                   (TarjetaGst) o en cualquier pipeline.
//   - `bapluginsrc` una ENTRADA: los cuadros (o el audio) que manda el plugin.
//                   Va como origen de `ba_gst_fuente` (con su colchón y el
//                   reloj del estudio), así que una entrada de un plugin es
//                   una entrada como cualquier otra.
//
// CÓMO VIAJAN: los datos por MEMORIA COMPARTIDA (shm_open; la crea el plugin)
// y por un socket local (127.0.0.1, el puerto de medios del plugin) solo
// avisos cortos en JSON. El protocolo está en src/plugins/sdk/ba_plugin/medios.py.
//
// UN PLUGIN NO PUEDE TRABAR NADA: toda espera tiene tope. La tarjeta espera
// la vuelta del cuadro `espera-ms` (25 de fábrica); si no llega, sale el
// último que sí volvió (o el original) y el plugin sigue a su ritmo. Sin el
// plugin (apagado, caído, reiniciándose), la tarjeta deja pasar la imagen y
// reintenta cada segundo; la entrada espera sin bloquear el pipeline.
//
// EL PUERTO: lo anuncia el plugin al arrancar y la app lo pasa con el proc
// global «ba_plugin_puerto(plugin, puerto)»; cambia si el plugin se reinicia,
// por eso se busca en cada reconexión. La propiedad `puerto` lo fija a mano
// (pruebas).
// ============================================================================

#if defined(BA_HAVE_GST)

#include "ba_gst.hpp"

#include <gst/audio/audio.h>
#include <gst/base/gstbasesrc.h>
#include <gst/base/gstbasetransform.h>
#include <gst/base/gstpushsrc.h>
#include <gst/gst.h>
#include <gst/video/video.h>
#include <obs-module.h>
#include <media-io/audio-io.h>

#include <nlohmann/json.hpp>

#include <atomic>
#include <chrono>
#include <cstring>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#if !defined(_WIN32)
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

using json = nlohmann::json;

namespace ba::gst {

// --- Los puertos de los plugins ------------------------------------------------------

namespace {
std::mutex g_puertosLock;
std::map<std::string, int> g_puertos;

int puertoDe(const std::string& plugin) {
    std::lock_guard<std::mutex> l(g_puertosLock);
    auto it = g_puertos.find(plugin);
    return it == g_puertos.end() ? 0 : it->second;
}

gint64 ahoraMs() { return g_get_monotonic_time() / 1000; }
} // namespace

void puertoPlugin(const std::string& plugin, int puerto) {
    std::lock_guard<std::mutex> l(g_puertosLock);
    if (puerto > 0) g_puertos[plugin] = puerto;
    else g_puertos.erase(plugin);
}

// --- El canal con el plugin -----------------------------------------------------------

/// Un socket con líneas JSON y la memoria compartida del plugin.
class CanalPlugin {
   public:
    ~CanalPlugin() { cerrar(); }

    bool abierto() const { return fd_ >= 0; }

    bool conectar(int puerto, std::string& error) {
        cerrar();
#if defined(_WIN32)
        (void)puerto;
        error = "los plugins todavía no andan en Windows";
        return false;
#else
        if (puerto <= 0) {
            error = "el plugin no está corriendo";
            return false;
        }
        fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
        if (fd_ < 0) {
            error = "no se pudo abrir un socket";
            return false;
        }
        sockaddr_in a = {};
        a.sin_family = AF_INET;
        a.sin_port = htons(static_cast<uint16_t>(puerto));
        a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        if (::connect(fd_, reinterpret_cast<sockaddr*>(&a), sizeof a) != 0) {
            error = "el plugin no contesta en el puerto " + std::to_string(puerto);
            cerrar();
            return false;
        }
        int uno = 1;
        setsockopt(fd_, IPPROTO_TCP, TCP_NODELAY, &uno, sizeof uno);
#if defined(SO_NOSIGPIPE)
        setsockopt(fd_, SOL_SOCKET, SO_NOSIGPIPE, &uno, sizeof uno);
#endif
        buf_.clear();
        return true;
#endif
    }

    bool enviar(const json& m) {
#if defined(_WIN32)
        (void)m;
        return false;
#else
        if (fd_ < 0) return false;
        const std::string s = m.dump() + "\n";
        size_t hecho = 0;
        while (hecho < s.size()) {
#if defined(MSG_NOSIGNAL)
            const ssize_t r = ::send(fd_, s.data() + hecho, s.size() - hecho, MSG_NOSIGNAL);
#else
            const ssize_t r = ::send(fd_, s.data() + hecho, s.size() - hecho, 0);
#endif
            if (r <= 0) {
                cerrar();
                return false;
            }
            hecho += static_cast<size_t>(r);
        }
        return true;
#endif
    }

    /// 1: una línea en `out`; 0: no llegó nada en `timeoutMs`; -1: se cortó.
    int leer(json& out, int timeoutMs) {
#if defined(_WIN32)
        (void)out;
        (void)timeoutMs;
        return -1;
#else
        const gint64 fin = ahoraMs() + timeoutMs;
        for (;;) {
            const size_t nl = buf_.find('\n');
            if (nl != std::string::npos) {
                const std::string linea = buf_.substr(0, nl);
                buf_.erase(0, nl + 1);
                try {
                    out = json::parse(linea);
                } catch (const std::exception&) {
                    out = json::object();
                }
                return 1;
            }
            if (fd_ < 0) return -1;
            const gint64 resta = fin - ahoraMs();
            pollfd p = {fd_, POLLIN, 0};
            const int r = ::poll(&p, 1, static_cast<int>(resta > 0 ? resta : 0));
            if (r == 0) return 0;
            if (r < 0) return 0;
            char tmp[65536];
            const ssize_t n = ::recv(fd_, tmp, sizeof tmp, 0);
            if (n <= 0) {
                cerrar();
                return -1;
            }
            buf_.append(tmp, static_cast<size_t>(n));
        }
#endif
    }

    bool mapear(const std::string& nombre, std::string& error) {
        soltarMemoria();
#if defined(_WIN32)
        (void)nombre;
        error = "sin memoria compartida en Windows todavía";
        return false;
#else
        const int fd = ::shm_open(nombre.c_str(), O_RDWR, 0);
        if (fd < 0) {
            error = "no se pudo abrir la memoria compartida «" + nombre + "»";
            return false;
        }
        struct stat st = {};
        if (::fstat(fd, &st) != 0 || st.st_size <= 0) {
            ::close(fd);
            error = "la memoria compartida está vacía";
            return false;
        }
        void* m = ::mmap(nullptr, static_cast<size_t>(st.st_size), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        ::close(fd);
        if (m == MAP_FAILED) {
            error = "no se pudo mapear la memoria compartida";
            return false;
        }
        mem_ = static_cast<uint8_t*>(m);
        tam_ = static_cast<size_t>(st.st_size);
        return true;
#endif
    }

    uint8_t* mem() const { return mem_; }
    size_t tam() const { return tam_; }

    void cerrar() {
#if !defined(_WIN32)
        if (fd_ >= 0) ::close(fd_);
#endif
        fd_ = -1;
        buf_.clear();
        soltarMemoria();
    }

   private:
    void soltarMemoria() {
#if !defined(_WIN32)
        if (mem_ != nullptr) ::munmap(mem_, tam_);
#endif
        mem_ = nullptr;
        tam_ = 0;
    }

    int fd_ = -1;
    std::string buf_;
    uint8_t* mem_ = nullptr;
    size_t tam_ = 0;
};

namespace {

const char* kCaps =
    "video/x-raw, format=(string){ RGBA, BGRA, RGBx, BGRx, I420, NV12 }, width=(int)[1,16384], height=(int)[1,16384], "
    "framerate=(fraction)[0/1,1000/1]; "
    "audio/x-raw, format=(string)F32LE, layout=(string)interleaved, rate=(int)[1,384000], channels=(int)[1,64]";

// ===================================================================================
// baplugin: la tarjeta
// ===================================================================================

struct Filtro {
    std::mutex lock;
    std::string plugin, tarjeta, parametros = "{}";
    int puerto = 0;
    int esperaMs = 25;
    bool paramsCambiaron = false;

    CanalPlugin canal;
    bool conectado = false;
    gint64 proximoIntento = 0;
    std::string error;
    bool avisado = false;

    bool esVideo = true;
    GstVideoInfo vinfo;
    GstAudioInfo ainfo;
    bool capsListas = false;
    bool capsCambiaron = false;

    bool ocupado = false; ///< hay un cuadro en el plugin que todavía no volvió
    uint64_t n = 0;
    std::vector<uint8_t> ultimo; ///< el último que volvió a tiempo… o tarde
    bool hayUltimo = false;
    uint64_t cuadros = 0, procesados = 0, tarde = 0, pasados = 0;
};

struct BaPluginFiltro {
    GstBaseTransform padre;
    Filtro* f;
};
struct BaPluginFiltroClass {
    GstBaseTransformClass padre;
};

GType ba_plugin_filtro_get_type();
G_DEFINE_TYPE(BaPluginFiltro, ba_plugin_filtro, GST_TYPE_BASE_TRANSFORM)

enum { P_0, P_PLUGIN, P_TARJETA, P_PARAMETROS, P_PUERTO, P_ESPERA, P_ESTADO };

json cabeceraDe(const Filtro& f, size_t tam) {
    json h{{"tam", tam}};
    if (f.esVideo) {
        h["medio"] = "video";
        h["formato"] = gst_video_format_to_string(GST_VIDEO_INFO_FORMAT(&f.vinfo));
        h["ancho"] = GST_VIDEO_INFO_WIDTH(&f.vinfo);
        h["alto"] = GST_VIDEO_INFO_HEIGHT(&f.vinfo);
        h["stride"] = GST_VIDEO_INFO_PLANE_STRIDE(&f.vinfo, 0);
    } else {
        h["medio"] = "audio";
        h["tasa"] = GST_AUDIO_INFO_RATE(&f.ainfo);
        h["canales"] = GST_AUDIO_INFO_CHANNELS(&f.ainfo);
    }
    return h;
}

json parametrosDe(const Filtro& f) {
    try {
        json p = json::parse(f.parametros.empty() ? "{}" : f.parametros);
        return p.is_object() ? p : json::object();
    } catch (const std::exception&) {
        return json::object();
    }
}

/// Espera la respuesta a un pedido de memoria (`ok` + `shm`) y la mapea.
bool recibirMemoria(Filtro& f, std::string& error) {
    json r;
    const gint64 fin = ahoraMs() + 2000;
    while (ahoraMs() < fin) {
        const int k = f.canal.leer(r, static_cast<int>(fin - ahoraMs()));
        if (k < 0) {
            error = "el plugin cortó la conexión";
            return false;
        }
        if (k == 0) break;
        if (r.contains("c")) continue; // la vuelta de un cuadro viejo
        if (!r.value("ok", false)) {
            error = r.value("error", std::string("el plugin no aceptó la tarjeta"));
            return false;
        }
        return f.canal.mapear(r.value("shm", std::string()), error);
    }
    error = "el plugin no contestó";
    return false;
}

void desconectar(Filtro& f, const std::string& error, gint64 esperaMs) {
    f.canal.cerrar();
    f.conectado = false;
    f.ocupado = false;
    f.hayUltimo = false;
    f.error = error;
    f.proximoIntento = ahoraMs() + esperaMs;
}

bool asegurarConexion(BaPluginFiltro* yo, Filtro& f, size_t tam) {
    if (f.conectado && f.canal.abierto()) {
        if (f.capsCambiaron || f.canal.tam() < tam) {
            f.capsCambiaron = false;
            std::string e;
            f.ocupado = false;
            f.hayUltimo = false;
            if (!f.canal.enviar({{"caps", cabeceraDe(f, tam)}}) || !recibirMemoria(f, e)) {
                desconectar(f, e.empty() ? "se cortó" : e, 1000);
                return false;
            }
        }
        return true;
    }
    if (ahoraMs() < f.proximoIntento) return false;
    if (f.plugin.empty() || f.tarjeta.empty()) {
        desconectar(f, "faltan `plugin` y `tarjeta`", 1000);
        return false;
    }
    std::string e;
    const int puerto = f.puerto > 0 ? f.puerto : puertoDe(f.plugin);
    if (!f.canal.conectar(puerto, e)) {
        desconectar(f, e + " («" + f.plugin + "»)", 1000);
        return false;
    }
    json h = cabeceraDe(f, tam);
    h["rol"] = "tarjeta";
    h["nombre"] = f.tarjeta;
    h["parametros"] = parametrosDe(f);
    f.paramsCambiaron = false;
    f.capsCambiaron = false;
    if (!f.canal.enviar(h) || !recibirMemoria(f, e)) {
        desconectar(f, e, 1000);
        if (!f.avisado && yo != nullptr) {
            f.avisado = true;
            GST_ELEMENT_WARNING(yo, RESOURCE, OPEN_READ, ("plugin «%s», tarjeta «%s»: %s", f.plugin.c_str(), f.tarjeta.c_str(), e.c_str()),
                                (nullptr));
        }
        return false;
    }
    f.conectado = true;
    f.ocupado = false;
    f.hayUltimo = false;
    f.error.clear();
    f.avisado = false;
    return true;
}

/// Lee las vueltas que haya (sin esperar más de `ms`). true si volvió el último que se mandó.
bool recogerVuelta(Filtro& f, int ms, bool& conError) {
    json r;
    const gint64 fin = ahoraMs() + ms;
    for (;;) {
        const gint64 resta = fin - ahoraMs();
        const int k = f.canal.leer(r, static_cast<int>(resta > 0 ? resta : 0));
        if (k < 0) {
            desconectar(f, "el plugin cortó la conexión", 1000);
            return false;
        }
        if (k == 0) return false;
        if (r.value("c", static_cast<uint64_t>(0)) == f.n) {
            conError = r.contains("error");
            if (conError) f.error = r.value("error", std::string());
            return true;
        }
    }
}

GstFlowReturn filtroTransformar(GstBaseTransform* t, GstBuffer* buf) {
    auto* yo = reinterpret_cast<BaPluginFiltro*>(t);
    Filtro& f = *yo->f;
    std::lock_guard<std::mutex> l(f.lock);
    if (!f.capsListas) return GST_FLOW_OK;
    GstMapInfo m;
    if (!gst_buffer_map(buf, &m, GST_MAP_READWRITE)) return GST_FLOW_OK;
    ++f.cuadros;
    const size_t tam = m.size;
    bool hecho = false;
    if (asegurarConexion(yo, f, tam) && f.canal.mem() != nullptr && f.canal.tam() >= tam) {
        if (f.paramsCambiaron) {
            f.paramsCambiaron = false;
            f.canal.enviar({{"parametros", parametrosDe(f)}});
        }
        bool conError = false;
        // Lo que estaba en el plugin y volvió tarde: queda como el último.
        if (f.ocupado && recogerVuelta(f, 0, conError)) {
            f.ocupado = false;
            if (!conError && f.esVideo) {
                f.ultimo.assign(f.canal.mem(), f.canal.mem() + tam);
                f.hayUltimo = true;
            }
        }
        if (f.conectado && !f.ocupado) {
            std::memcpy(f.canal.mem(), m.data, tam);
            json c{{"c", ++f.n}, {"pts", GST_BUFFER_PTS_IS_VALID(buf) ? static_cast<long long>(GST_BUFFER_PTS(buf)) : -1}};
            if (!f.esVideo) c["muestras"] = tam / (4 * static_cast<size_t>(std::max(1, GST_AUDIO_INFO_CHANNELS(&f.ainfo))));
            if (f.canal.enviar(c)) {
                f.ocupado = true;
                conError = false;
                if (recogerVuelta(f, f.esperaMs, conError)) {
                    f.ocupado = false;
                    if (!conError) {
                        std::memcpy(m.data, f.canal.mem(), tam);
                        ++f.procesados;
                        hecho = true;
                    }
                } else {
                    ++f.tarde;
                }
            } else {
                desconectar(f, "se cortó", 1000);
            }
        }
        if (!hecho && f.hayUltimo && f.ultimo.size() == tam) {
            std::memcpy(m.data, f.ultimo.data(), tam);
            hecho = true;
        }
    }
    if (!hecho) ++f.pasados;
    gst_buffer_unmap(buf, &m);
    return GST_FLOW_OK;
}

gboolean filtroCaps(GstBaseTransform* t, GstCaps* in, GstCaps*) {
    Filtro& f = *reinterpret_cast<BaPluginFiltro*>(t)->f;
    std::lock_guard<std::mutex> l(f.lock);
    const GstStructure* s = gst_caps_get_structure(in, 0);
    f.esVideo = gst_structure_has_name(s, "video/x-raw");
    const bool ok = f.esVideo ? gst_video_info_from_caps(&f.vinfo, in) : gst_audio_info_from_caps(&f.ainfo, in);
    f.capsListas = ok;
    f.capsCambiaron = true;
    return ok;
}

gboolean filtroParar(GstBaseTransform* t) {
    Filtro& f = *reinterpret_cast<BaPluginFiltro*>(t)->f;
    std::lock_guard<std::mutex> l(f.lock);
    desconectar(f, {}, 0);
    return TRUE;
}

void filtroPoner(GObject* o, guint id, const GValue* v, GParamSpec* ps) {
    Filtro& f = *reinterpret_cast<BaPluginFiltro*>(o)->f;
    std::lock_guard<std::mutex> l(f.lock);
    switch (id) {
    case P_PLUGIN:
        f.plugin = g_value_get_string(v) ? g_value_get_string(v) : "";
        desconectar(f, {}, 0);
        break;
    case P_TARJETA:
        f.tarjeta = g_value_get_string(v) ? g_value_get_string(v) : "";
        desconectar(f, {}, 0);
        break;
    case P_PARAMETROS:
        f.parametros = g_value_get_string(v) ? g_value_get_string(v) : "{}";
        f.paramsCambiaron = true;
        break;
    case P_PUERTO:
        f.puerto = g_value_get_int(v);
        desconectar(f, {}, 0);
        break;
    case P_ESPERA: f.esperaMs = g_value_get_int(v); break;
    default: G_OBJECT_WARN_INVALID_PROPERTY_ID(o, id, ps);
    }
}

void filtroLeer(GObject* o, guint id, GValue* v, GParamSpec* ps) {
    Filtro& f = *reinterpret_cast<BaPluginFiltro*>(o)->f;
    std::lock_guard<std::mutex> l(f.lock);
    switch (id) {
    case P_PLUGIN: g_value_set_string(v, f.plugin.c_str()); break;
    case P_TARJETA: g_value_set_string(v, f.tarjeta.c_str()); break;
    case P_PARAMETROS: g_value_set_string(v, f.parametros.c_str()); break;
    case P_PUERTO: g_value_set_int(v, f.puerto); break;
    case P_ESPERA: g_value_set_int(v, f.esperaMs); break;
    case P_ESTADO: {
        const json e{{"conectado", f.conectado}, {"error", f.error},       {"cuadros", f.cuadros},
                     {"procesados", f.procesados}, {"tarde", f.tarde}, {"sin_procesar", f.pasados}};
        g_value_set_string(v, e.dump().c_str());
        break;
    }
    default: G_OBJECT_WARN_INVALID_PROPERTY_ID(o, id, ps);
    }
}

void filtroFinalizar(GObject* o) {
    delete reinterpret_cast<BaPluginFiltro*>(o)->f;
    G_OBJECT_CLASS(ba_plugin_filtro_parent_class)->finalize(o);
}

void ba_plugin_filtro_class_init(BaPluginFiltroClass* k) {
    auto* go = G_OBJECT_CLASS(k);
    go->set_property = filtroPoner;
    go->get_property = filtroLeer;
    go->finalize = filtroFinalizar;
    const auto rw = static_cast<GParamFlags>(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
    g_object_class_install_property(go, P_PLUGIN, g_param_spec_string("plugin", "Plugin", "El id del plugin", "", rw));
    g_object_class_install_property(go, P_TARJETA, g_param_spec_string("tarjeta", "Tarjeta", "La tarjeta del plugin", "", rw));
    g_object_class_install_property(
        go, P_PARAMETROS, g_param_spec_string("parametros", "Parámetros", "Los ajustes de la tarjeta (JSON)", "{}",
                                              static_cast<GParamFlags>(rw | GST_PARAM_MUTABLE_PLAYING)));
    g_object_class_install_property(go, P_PUERTO, g_param_spec_int("puerto", "Puerto", "El de medios del plugin (0: el que anuncie)", 0, 65535, 0, rw));
    g_object_class_install_property(
        go, P_ESPERA,
        g_param_spec_int("espera-ms", "Espera", "Cuánto esperar la vuelta de cada cuadro (después, sale el último)", 0, 1000, 25,
                         static_cast<GParamFlags>(rw | GST_PARAM_MUTABLE_PLAYING)));
    g_object_class_install_property(go, P_ESTADO,
                                    g_param_spec_string("estado", "Estado", "Conexión y cuadros (JSON)", "{}",
                                                        static_cast<GParamFlags>(G_PARAM_READABLE | G_PARAM_STATIC_STRINGS)));
    auto* ge = GST_ELEMENT_CLASS(k);
    gst_element_class_set_static_metadata(ge, "Plugin de Broadcast Appliance (tarjeta)", "Filter/Effect/Video/Audio",
                                          "Pasa cada cuadro o bloque de audio por una tarjeta de un plugin de Python",
                                          "Broadcast Appliance");
    GstCaps* c = gst_caps_from_string(kCaps);
    gst_element_class_add_pad_template(ge, gst_pad_template_new("sink", GST_PAD_SINK, GST_PAD_ALWAYS, c));
    gst_element_class_add_pad_template(ge, gst_pad_template_new("src", GST_PAD_SRC, GST_PAD_ALWAYS, c));
    gst_caps_unref(c);
    auto* bt = GST_BASE_TRANSFORM_CLASS(k);
    bt->transform_ip = filtroTransformar;
    bt->set_caps = filtroCaps;
    bt->stop = filtroParar;
    bt->passthrough_on_same_caps = FALSE;
}

void ba_plugin_filtro_init(BaPluginFiltro* yo) {
    yo->f = new Filtro();
    gst_video_info_init(&yo->f->vinfo);
    gst_audio_info_init(&yo->f->ainfo);
    gst_base_transform_set_in_place(GST_BASE_TRANSFORM(yo), TRUE);
    gst_base_transform_set_passthrough(GST_BASE_TRANSFORM(yo), FALSE);
}

// ===================================================================================
// bapluginsrc: la entrada
// ===================================================================================

struct Fuente {
    std::mutex lock; // propiedades
    std::string plugin, entrada, medio = "video";
    int puerto = 0;
    std::atomic<bool> cortar{false};

    CanalPlugin canal; // solo el hilo de create()
    bool conectado = false;
    gint64 proximoIntento = 0;
    json cab;
    bool esVideo = true;
    GstVideoInfo vinfo;
    int canales = 2;
    std::string error;
    std::atomic<uint64_t> recibidos{0};
};

struct BaPluginFuente {
    GstPushSrc padre;
    Fuente* f;
};
struct BaPluginFuenteClass {
    GstPushSrcClass padre;
};

GType ba_plugin_fuente_get_type();
G_DEFINE_TYPE(BaPluginFuente, ba_plugin_fuente, GST_TYPE_PUSH_SRC)

enum { F_0, F_PLUGIN, F_ENTRADA, F_MEDIO, F_PUERTO, F_ESTADO };

bool capsDeCabecera(BaPluginFuente* yo, Fuente& f, const json& m) {
    GstCaps* caps = nullptr;
    if (f.esVideo) {
        const GstVideoFormat fmt = gst_video_format_from_string(m.value("formato", std::string("RGBA")).c_str());
        const int w = m.value("ancho", 0), h = m.value("alto", 0);
        if (fmt == GST_VIDEO_FORMAT_UNKNOWN || w <= 0 || h <= 0) return false;
        gst_video_info_set_format(&f.vinfo, fmt, static_cast<guint>(w), static_cast<guint>(h));
        int num = 30, den = 1;
        gst_util_double_to_fraction(m.value("fps", 30.0), &num, &den);
        GST_VIDEO_INFO_FPS_N(&f.vinfo) = num;
        GST_VIDEO_INFO_FPS_D(&f.vinfo) = den;
        caps = gst_video_info_to_caps(&f.vinfo);
    } else {
        GstAudioInfo a;
        f.canales = std::max(1, m.value("canales", 2));
        gst_audio_info_set_format(&a, GST_AUDIO_FORMAT_F32LE, m.value("tasa", 48000), f.canales, nullptr);
        caps = gst_audio_info_to_caps(&a);
    }
    const bool ok = gst_base_src_set_caps(GST_BASE_SRC(yo), caps);
    gst_caps_unref(caps);
    return ok;
}

/// Copia un cuadro empaquetado (como lo arma el plugin) a un buffer con los strides de GStreamer.
void copiarCuadro(const Fuente& f, const uint8_t* origen, size_t tamOrigen, GstBuffer* buf) {
    GstVideoFrame v;
    GstVideoInfo info = f.vinfo;
    if (!gst_video_frame_map(&v, &info, buf, GST_MAP_WRITE)) return;
    const int w = GST_VIDEO_INFO_WIDTH(&info), h = GST_VIDEO_INFO_HEIGHT(&info);
    const int cw = (w + 1) / 2, ch = (h + 1) / 2;
    const GstVideoFormat fmt = GST_VIDEO_INFO_FORMAT(&info);
    struct Plano {
        size_t off, stride, ancho, filas;
    };
    std::vector<Plano> planos;
    if (fmt == GST_VIDEO_FORMAT_I420) {
        planos = {{0, size_t(w), size_t(w), size_t(h)},
                  {size_t(w) * h, size_t(cw), size_t(cw), size_t(ch)},
                  {size_t(w) * h + size_t(cw) * ch, size_t(cw), size_t(cw), size_t(ch)}};
    } else if (fmt == GST_VIDEO_FORMAT_NV12) {
        planos = {{0, size_t(w), size_t(w), size_t(h)}, {size_t(w) * h, size_t(cw) * 2, size_t(cw) * 2, size_t(ch)}};
    } else {
        const size_t st = f.cab.value("stride", w * 4);
        planos = {{0, st, size_t(w) * 4, size_t(h)}};
    }
    for (size_t p = 0; p < planos.size() && p < GST_VIDEO_FRAME_N_PLANES(&v); ++p) {
        auto* dst = static_cast<uint8_t*>(GST_VIDEO_FRAME_PLANE_DATA(&v, p));
        const size_t dstStride = static_cast<size_t>(GST_VIDEO_FRAME_PLANE_STRIDE(&v, p));
        const Plano& pl = planos[p];
        for (size_t y = 0; y < pl.filas; ++y) {
            const size_t o = pl.off + y * pl.stride;
            if (o + pl.ancho > tamOrigen) break;
            std::memcpy(dst + y * dstStride, origen + o, std::min(pl.ancho, dstStride));
        }
    }
    gst_video_frame_unmap(&v);
}

GstFlowReturn fuenteCrear(GstPushSrc* s, GstBuffer** out) {
    auto* yo = reinterpret_cast<BaPluginFuente*>(s);
    Fuente& f = *yo->f;
    std::string plugin, entrada, medio;
    int puertoFijo = 0;
    {
        std::lock_guard<std::mutex> l(f.lock);
        plugin = f.plugin;
        entrada = f.entrada;
        medio = f.medio;
        puertoFijo = f.puerto;
    }
    f.esVideo = medio != "audio";
    for (;;) {
        if (f.cortar) return GST_FLOW_FLUSHING;
        if (!f.conectado) {
            if (ahoraMs() < f.proximoIntento) {
                g_usleep(50'000);
                continue;
            }
            std::string e;
            const int puerto = puertoFijo > 0 ? puertoFijo : puertoDe(plugin);
            if (!f.canal.conectar(puerto, e) || !f.canal.enviar({{"rol", "entrada"}, {"nombre", entrada}, {"medio", medio}})) {
                f.error = e;
                f.proximoIntento = ahoraMs() + 500;
                continue;
            }
            f.conectado = true;
            f.error.clear();
        }
        json m;
        const int k = f.canal.leer(m, 100);
        if (k == 0) continue;
        if (k < 0) {
            f.conectado = false;
            f.proximoIntento = ahoraMs() + 500;
            continue;
        }
        if (m.contains("error")) {
            f.error = m.value("error", std::string());
            f.canal.cerrar();
            f.conectado = false;
            f.proximoIntento = ahoraMs() + 2000;
            continue;
        }
        if (m.contains("shm")) {
            std::string e;
            if (!f.canal.mapear(m.value("shm", std::string()), e) || !capsDeCabecera(yo, f, m)) {
                f.error = e.empty() ? "formato que no se entiende" : e;
                f.canal.cerrar();
                f.conectado = false;
                f.proximoIntento = ahoraMs() + 2000;
                continue;
            }
            f.cab = m;
            continue;
        }
        if (!m.contains("c") || f.canal.mem() == nullptr) continue;
        const size_t tam = std::min(m.value("tam", static_cast<size_t>(0)), f.canal.tam());
        GstBuffer* buf = nullptr;
        if (f.esVideo) {
            buf = gst_buffer_new_allocate(nullptr, GST_VIDEO_INFO_SIZE(&f.vinfo), nullptr);
            copiarCuadro(f, f.canal.mem(), tam, buf);
        } else {
            const size_t bytes = std::min(tam, m.value("muestras", static_cast<size_t>(0)) * 4 * static_cast<size_t>(f.canales));
            buf = gst_buffer_new_allocate(nullptr, bytes, nullptr);
            gst_buffer_fill(buf, 0, f.canal.mem(), bytes);
        }
        f.canal.enviar({{"c", m["c"]}});
        ++f.recibidos;
        *out = buf;
        return GST_FLOW_OK;
    }
}

gboolean fuenteNegociar(GstBaseSrc*) { return TRUE; } // las caps las pone el plugin (capsDeCabecera)

gboolean fuenteCortar(GstBaseSrc* s) {
    reinterpret_cast<BaPluginFuente*>(s)->f->cortar = true;
    return TRUE;
}
gboolean fuenteSeguir(GstBaseSrc* s) {
    reinterpret_cast<BaPluginFuente*>(s)->f->cortar = false;
    return TRUE;
}
gboolean fuenteParar(GstBaseSrc* s) {
    Fuente& f = *reinterpret_cast<BaPluginFuente*>(s)->f;
    f.canal.cerrar();
    f.conectado = false;
    f.proximoIntento = 0;
    return TRUE;
}

void fuentePoner(GObject* o, guint id, const GValue* v, GParamSpec* ps) {
    Fuente& f = *reinterpret_cast<BaPluginFuente*>(o)->f;
    std::lock_guard<std::mutex> l(f.lock);
    const char* s = G_VALUE_HOLDS_STRING(v) ? g_value_get_string(v) : nullptr;
    switch (id) {
    case F_PLUGIN: f.plugin = s ? s : ""; break;
    case F_ENTRADA: f.entrada = s ? s : ""; break;
    case F_MEDIO: f.medio = s ? s : "video"; break;
    case F_PUERTO: f.puerto = g_value_get_int(v); break;
    default: G_OBJECT_WARN_INVALID_PROPERTY_ID(o, id, ps);
    }
}

void fuenteLeer(GObject* o, guint id, GValue* v, GParamSpec* ps) {
    Fuente& f = *reinterpret_cast<BaPluginFuente*>(o)->f;
    std::lock_guard<std::mutex> l(f.lock);
    switch (id) {
    case F_PLUGIN: g_value_set_string(v, f.plugin.c_str()); break;
    case F_ENTRADA: g_value_set_string(v, f.entrada.c_str()); break;
    case F_MEDIO: g_value_set_string(v, f.medio.c_str()); break;
    case F_PUERTO: g_value_set_int(v, f.puerto); break;
    case F_ESTADO:
        g_value_set_string(v, json{{"conectado", f.conectado}, {"error", f.error}, {"recibidos", f.recibidos.load()}}.dump().c_str());
        break;
    default: G_OBJECT_WARN_INVALID_PROPERTY_ID(o, id, ps);
    }
}

void fuenteFinalizar(GObject* o) {
    delete reinterpret_cast<BaPluginFuente*>(o)->f;
    G_OBJECT_CLASS(ba_plugin_fuente_parent_class)->finalize(o);
}

void ba_plugin_fuente_class_init(BaPluginFuenteClass* k) {
    auto* go = G_OBJECT_CLASS(k);
    go->set_property = fuentePoner;
    go->get_property = fuenteLeer;
    go->finalize = fuenteFinalizar;
    const auto rw = static_cast<GParamFlags>(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
    g_object_class_install_property(go, F_PLUGIN, g_param_spec_string("plugin", "Plugin", "El id del plugin", "", rw));
    g_object_class_install_property(go, F_ENTRADA, g_param_spec_string("entrada", "Entrada", "La entrada del plugin", "", rw));
    g_object_class_install_property(go, F_MEDIO, g_param_spec_string("medio", "Medio", "video o audio", "video", rw));
    g_object_class_install_property(go, F_PUERTO, g_param_spec_int("puerto", "Puerto", "El de medios del plugin (0: el que anuncie)", 0, 65535, 0, rw));
    g_object_class_install_property(go, F_ESTADO,
                                    g_param_spec_string("estado", "Estado", "Conexión y cuadros (JSON)", "{}",
                                                        static_cast<GParamFlags>(G_PARAM_READABLE | G_PARAM_STATIC_STRINGS)));
    auto* ge = GST_ELEMENT_CLASS(k);
    gst_element_class_set_static_metadata(ge, "Plugin de Broadcast Appliance (entrada)", "Source/Video/Audio",
                                          "El video o el audio que manda un plugin de Python", "Broadcast Appliance");
    GstCaps* c = gst_caps_from_string(kCaps);
    gst_element_class_add_pad_template(ge, gst_pad_template_new("src", GST_PAD_SRC, GST_PAD_ALWAYS, c));
    gst_caps_unref(c);
    auto* bs = GST_BASE_SRC_CLASS(k);
    bs->negotiate = fuenteNegociar;
    bs->unlock = fuenteCortar;
    bs->unlock_stop = fuenteSeguir;
    bs->stop = fuenteParar;
    GST_PUSH_SRC_CLASS(k)->create = fuenteCrear;
}

void ba_plugin_fuente_init(BaPluginFuente* yo) {
    yo->f = new Fuente();
    gst_video_info_init(&yo->f->vinfo);
    gst_base_src_set_live(GST_BASE_SRC(yo), TRUE);
    gst_base_src_set_format(GST_BASE_SRC(yo), GST_FORMAT_TIME);
    gst_base_src_set_do_timestamp(GST_BASE_SRC(yo), TRUE);
}

// ===================================================================================
// ba_plugin_audio: un filtro de audio de OBS que pasa por una tarjeta de audio
// del plugin (el micrófono de una fuente cualquiera, sin GStreamer de por medio)
// ===================================================================================

struct FiltroAudio {
    obs_source_t* ctx = nullptr;
    Filtro f;
    std::vector<float> intercalado;
};

const char* audioNombre(void*) { return "Plugin de Python (audio)"; }

void audioActualizar(void* d, obs_data_t* s) {
    auto* a = static_cast<FiltroAudio*>(d);
    std::lock_guard<std::mutex> l(a->f.lock);
    const std::string plugin = obs_data_get_string(s, "plugin"), tarjeta = obs_data_get_string(s, "tarjeta"),
                      params = obs_data_get_string(s, "parametros");
    if (plugin != a->f.plugin || tarjeta != a->f.tarjeta) {
        a->f.plugin = plugin;
        a->f.tarjeta = tarjeta;
        desconectar(a->f, {}, 0);
    }
    if (params != a->f.parametros) {
        a->f.parametros = params.empty() ? "{}" : params;
        a->f.paramsCambiaron = true;
    }
    a->f.esperaMs = static_cast<int>(obs_data_get_int(s, "espera_ms"));
}

void* audioCrear(obs_data_t* s, obs_source_t* ctx) {
    auto* a = new FiltroAudio();
    a->ctx = ctx;
    a->f.esVideo = false;
    gst_audio_info_init(&a->f.ainfo);
    const audio_output_info* oi = obs_get_audio() ? audio_output_get_info(obs_get_audio()) : nullptr;
    const int canales = oi ? static_cast<int>(get_audio_channels(oi->speakers)) : 2;
    gst_audio_info_set_format(&a->f.ainfo, GST_AUDIO_FORMAT_F32LE, oi ? static_cast<int>(oi->samples_per_sec) : 48000, canales, nullptr);
    a->f.capsListas = true;
    audioActualizar(a, s);
    return a;
}

void audioDestruir(void* d) {
    auto* a = static_cast<FiltroAudio*>(d);
    {
        std::lock_guard<std::mutex> l(a->f.lock);
        desconectar(a->f, {}, 0);
    }
    delete a;
}

/// Mismo ida y vuelta que la tarjeta: espera la vuelta `espera_ms` (10 de
/// fábrica); si no llega, el bloque sale como vino (el audio no se repite).
obs_audio_data* audioFiltrar(void* d, obs_audio_data* au) {
    auto* a = static_cast<FiltroAudio*>(d);
    Filtro& f = a->f;
    std::lock_guard<std::mutex> l(f.lock);
    const int canales = GST_AUDIO_INFO_CHANNELS(&f.ainfo);
    const size_t n = au->frames;
    if (n == 0 || canales <= 0) return au;
    const size_t tam = n * static_cast<size_t>(canales) * sizeof(float);
    if (!asegurarConexion(nullptr, f, std::max(tam, static_cast<size_t>(48000 * canales * 4 / 4))) || f.canal.mem() == nullptr ||
        f.canal.tam() < tam)
        return au;
    if (f.paramsCambiaron) {
        f.paramsCambiaron = false;
        f.canal.enviar({{"parametros", parametrosDe(f)}});
    }
    bool conError = false;
    if (f.ocupado && recogerVuelta(f, 0, conError)) f.ocupado = false;
    if (f.ocupado || !f.conectado) return au;
    auto* mem = reinterpret_cast<float*>(f.canal.mem());
    for (size_t i = 0; i < n; ++i)
        for (int c = 0; c < canales; ++c) {
            const auto* plano = reinterpret_cast<const float*>(au->data[c]);
            mem[i * canales + c] = plano != nullptr ? plano[i] : 0.0f;
        }
    if (!f.canal.enviar({{"c", ++f.n}, {"pts", static_cast<long long>(au->timestamp)}, {"muestras", n}})) {
        desconectar(f, "se cortó", 1000);
        return au;
    }
    f.ocupado = true;
    ++f.cuadros;
    if (recogerVuelta(f, f.esperaMs, conError)) {
        f.ocupado = false;
        if (!conError) {
            for (size_t i = 0; i < n; ++i)
                for (int c = 0; c < canales; ++c)
                    if (auto* plano = reinterpret_cast<float*>(au->data[c])) plano[i] = mem[i * canales + c];
            ++f.procesados;
        }
    } else {
        ++f.tarde;
    }
    return au;
}

obs_properties_t* audioPropiedades(void*) {
    obs_properties_t* p = obs_properties_create();
    obs_properties_add_text(p, "plugin", "Plugin", OBS_TEXT_DEFAULT);
    obs_properties_add_text(p, "tarjeta", "Tarjeta de audio", OBS_TEXT_DEFAULT);
    obs_properties_add_text(p, "parametros", "Parámetros (JSON)", OBS_TEXT_MULTILINE);
    obs_properties_add_int(p, "espera_ms", "Espera (ms)", 0, 100, 1);
    return p;
}

void audioPorDefecto(obs_data_t* s) {
    obs_data_set_default_string(s, "parametros", "{}");
    obs_data_set_default_int(s, "espera_ms", 10);
}

obs_source_info g_filtroAudio = {};

} // namespace

void registrarFiltroAudioPlugin() {
    g_filtroAudio.id = "ba_plugin_audio";
    g_filtroAudio.type = OBS_SOURCE_TYPE_FILTER;
    g_filtroAudio.output_flags = OBS_SOURCE_AUDIO;
    g_filtroAudio.get_name = audioNombre;
    g_filtroAudio.create = audioCrear;
    g_filtroAudio.destroy = audioDestruir;
    g_filtroAudio.update = audioActualizar;
    g_filtroAudio.filter_audio = audioFiltrar;
    g_filtroAudio.get_properties = audioPropiedades;
    g_filtroAudio.get_defaults = audioPorDefecto;
    obs_register_source(&g_filtroAudio);
}

void registrarElementosPlugin() {
    // La superresolución (basuperres, bafsr, baonnxsr, bavtsuperres) es un
    // plugin de GStreamer aparte: src/gst-plugins/superres.
    gst_element_register(nullptr, "baplugin", GST_RANK_NONE, ba_plugin_filtro_get_type());
    gst_element_register(nullptr, "bapluginsrc", GST_RANK_NONE, ba_plugin_fuente_get_type());
}

} // namespace ba::gst

#endif // BA_HAVE_GST
