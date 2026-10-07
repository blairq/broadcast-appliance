// ============================================================================
// src/motor/Entrada.cpp — ver Entrada.hpp
// ============================================================================
#include "Entrada.hpp"

#include "OrigenRed.hpp"
#include "Reloj.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <deque>
#include <map>
#include <mutex>
#include <thread>

namespace ba::motor {

using json = nlohmann::json;

namespace {

// --- La placa «sin señal», dibujada una vez ------------------------------------------
// Cada entrada tenía su textoverlay solo para rotular UN cuadro, y textoverlay
// arma su propio mapa de fuentes de pango en cada instancia: recorre todas las
// tipografías del sistema (~30-60 ms en la Mac, más con fontconfig). Con veinte
// entradas era más de la mitad de lo que tardaba en armar las escenas al
// arrancar. Acá hay UN textoverlay para todo el motor y cada texto se dibuja una
// sola vez; la entrada recibe el cuadro hecho por un appsrc.
constexpr int kPlacaAncho = 640, kPlacaAlto = 360;

} // namespace

/// Un texto para dibujar (la placa «sin señal» y los rótulos de texto).
struct Rotulo {
    std::string texto, fuente = "Sans Bold 22", color = "0xffffffff", colorContorno = "0xff000000";
    std::string ha = "center", va = "center";
    bool contorno = false;
    int ancho = kPlacaAncho, alto = kPlacaAlto;
    std::uint32_t fondo = 0xff1c1c22; ///< ARGB
    std::string clave() const {
        return texto + '\x1f' + fuente + '\x1f' + color + '\x1f' + colorContorno + '\x1f' + ha + '\x1f' + va + '\x1f' +
               (contorno ? "1" : "0") + '\x1f' + std::to_string(ancho) + 'x' + std::to_string(alto) + '\x1f' + std::to_string(fondo);
    }
};

namespace {

GstBuffer* textoDibujado(const Rotulo& r) {
    // Nunca se destruyen: un std::mutex estático que se destruye al salir
    // mientras otro hilo lo usa es «mutex lock failed» y el proceso muere.
    static auto* mu = new std::mutex;
    static auto* hechas = new std::map<std::string, GstBuffer*>;
    static auto* orden = new std::deque<std::string>; // los más viejos adelante
    static GstElement* dibujante = nullptr;
    static GstClockTime pts = 0;
    std::lock_guard<std::mutex> l(*mu);
    const std::string clave = r.clave();
    if (auto it = hechas->find(clave); it != hechas->end()) return gst_buffer_ref(it->second);

    // El fondo, en BGRA (B, G, R, A).
    const gsize bytes = static_cast<gsize>(r.ancho) * r.alto * 4;
    GstBuffer* fondo = gst_buffer_new_allocate(nullptr, bytes, nullptr);
    if (GstMapInfo m; gst_buffer_map(fondo, &m, GST_MAP_WRITE)) {
        const std::uint8_t b = r.fondo & 0xff, g = (r.fondo >> 8) & 0xff, rr = (r.fondo >> 16) & 0xff, a = r.fondo >> 24;
        for (gsize i = 0; i + 3 < m.size; i += 4) {
            m.data[i] = b;
            m.data[i + 1] = g;
            m.data[i + 2] = rr;
            m.data[i + 3] = a;
        }
        gst_buffer_unmap(fondo, &m);
    }
    GstBuffer* hecha = fondo;
    if (dibujante == nullptr && hayFabrica("textoverlay")) {
        GError* e = nullptr;
        // auto-resize=false: el tamaño ya viene en puntos de la letra pedida.
        // Prendido (lo de fábrica), textoverlay la agranda según el ancho (toma
        // 640 de referencia): un rótulo de 1152 de ancho salía casi al doble,
        // partido en dos renglones y cortado arriba.
        dibujante = gst_parse_launch("appsrc name=entra format=time ! textoverlay name=rotulo draw-shadow=false wrap-mode=word"
                                     " auto-resize=false"
                                     " ! appsink name=sale sync=false",
                                     &e);
        if (e != nullptr) {
            g_error_free(e);
            if (dibujante != nullptr) gst_object_unref(dibujante);
            dibujante = nullptr;
        } else {
            gst_element_set_state(dibujante, GST_STATE_PLAYING);
        }
    }
    if (dibujante != nullptr) {
        GstElement* entra = gst_bin_get_by_name(GST_BIN(dibujante), "entra");
        GstElement* rotulo = gst_bin_get_by_name(GST_BIN(dibujante), "rotulo");
        GstElement* sale = gst_bin_get_by_name(GST_BIN(dibujante), "sale");
        GstCaps* caps = gst_caps_new_simple("video/x-raw", "format", G_TYPE_STRING, "BGRA", "width", G_TYPE_INT, r.ancho,
                                            "height", G_TYPE_INT, r.alto, "framerate", GST_TYPE_FRACTION, 1, 1, nullptr);
        g_object_set(entra, "caps", caps, nullptr);
        gst_caps_unref(caps);
        g_object_set(rotulo, "text", r.texto.c_str(), "font-desc", r.fuente.c_str(), "draw-outline", r.contorno, nullptr);
        gst_util_set_object_arg(G_OBJECT(rotulo), "color", r.color.c_str());
        gst_util_set_object_arg(G_OBJECT(rotulo), "outline-color", r.colorContorno.c_str());
        gst_util_set_object_arg(G_OBJECT(rotulo), "halignment", r.ha.c_str());
        gst_util_set_object_arg(G_OBJECT(rotulo), "valignment", r.va.c_str());
        GstBuffer* in = gst_buffer_ref(fondo);
        GST_BUFFER_PTS(in) = pts;
        GST_BUFFER_DURATION(in) = GST_SECOND;
        pts += GST_SECOND;
        GstFlowReturn res = GST_FLOW_OK;
        g_signal_emit_by_name(entra, "push-buffer", in, &res);
        gst_buffer_unref(in);
        GstSample* muestra = nullptr;
        if (res == GST_FLOW_OK) g_signal_emit_by_name(sale, "try-pull-sample", static_cast<GstClockTime>(2 * GST_SECOND), &muestra);
        if (muestra != nullptr) {
            if (GstBuffer* b = gst_sample_get_buffer(muestra)) {
                hecha = gst_buffer_ref(b);
                gst_buffer_unref(fondo);
            }
            gst_sample_unref(muestra);
        }
        gst_object_unref(entra);
        gst_object_unref(rotulo);
        gst_object_unref(sale);
    }
    // Los últimos 64 (un rótulo que el operador va escribiendo deja uno por versión).
    (*hechas)[clave] = hecha;
    orden->push_back(clave);
    while (orden->size() > 64) {
        auto it = hechas->find(orden->front());
        if (it != hechas->end()) {
            gst_buffer_unref(it->second);
            hechas->erase(it);
        }
        orden->pop_front();
    }
    return gst_buffer_ref(hecha);
}

GstBuffer* placaDibujada(const std::string& texto) {
    Rotulo r;
    r.texto = texto;
    return textoDibujado(r);
}

std::uint64_t ahoraNs() { return static_cast<std::uint64_t>(g_get_monotonic_time()) * 1000ULL; }

/// Un valor que puede venir como objeto/arreglo JSON o como texto JSON.
json comoJson(const json& v) {
    if (v.is_string()) {
        const std::string s = v.get<std::string>();
        return s.empty() ? json() : json::parse(s, nullptr, false);
    }
    return v;
}

/// Lo que se pasa a la cadena de una propiedad numérica que puede venir como texto.
std::int64_t entero(const json& a, const char* k, std::int64_t def) {
    if (!a.contains(k)) return def;
    const json& v = a[k];
    if (v.is_number()) return v.get<std::int64_t>();
    if (v.is_string()) {
        try {
            return std::stoll(v.get<std::string>());
        } catch (...) {
        }
    }
    return def;
}

constexpr const char* kAudio = "audio/x-raw,format=F32LE,layout=interleaved,rate=48000,channels=2";

/// "#RRGGBB" / "#AARRGGBB" / número ARGB → 0xAARRGGBB (texto para gst).
std::string argb(const json& v, const char* def) {
    std::uint32_t c = 0;
    if (v.is_number_integer()) {
        c = static_cast<std::uint32_t>(v.get<std::int64_t>());
    } else if (v.is_string() && !v.get<std::string>().empty()) {
        std::string t = v.get<std::string>();
        if (t[0] == '#') t = t.substr(1);
        if (t.rfind("0x", 0) == 0) t = t.substr(2);
        try {
            c = static_cast<std::uint32_t>(std::stoul(t, nullptr, 16));
            if (t.size() <= 6) c |= 0xFF000000u;
        } catch (...) {
            return def;
        }
    } else {
        return def;
    }
    char b[16];
    std::snprintf(b, sizeof b, "0x%08x", c);
    return b;
}

std::string conComillas(const std::string& v) {
    std::string r = "\"";
    for (char c : v) {
        if (c == '\\' || c == '"') r += '\\';
        r += c;
    }
    return r + "\"";
}

} // namespace

Entrada::Entrada(Reloj& reloj, const Lienzo& lienzo, std::string id, const json& ajustes)
    : reloj_(reloj), lienzo_(lienzo), id_(std::move(id)) {
    std::lock_guard<std::mutex> l(mu_);
    leer(ajustes);
    armarSalida();
    armar();
}

Entrada::~Entrada() {
    std::lock_guard<std::mutex> l(mu_);
    desarmar();
    desarmarSalida();
    reloj_.olvidar(id_);
}

void Entrada::desarmarSalida() {
    if (salidaV_ == nullptr) return;
    gst_element_set_state(salidaV_, GST_STATE_NULL);
    gst_object_unref(salidaV_);
    salidaV_ = nullptr;
}

void Entrada::armarSalida() {
    // Sin placa: los archivos (parados no se ven) y lo que pinta la app (manda solo cuando cambia).
    const bool quiere = daVideo() && !medio_ && !estatica_ && hayFabrica("fallbackswitch");
    conPlaca_ = quiere;
    if (!quiere) {
        desarmarSalida();
        return;
    }
    // Sin nadie que la use, no se arma (ponerEnUso la arma cuando hace falta).
    if (salidaV_ != nullptr || !enUso_) return;
    // Rearmada a pedido con la señal llegando: que espere el primer cuadro (si
    // salta a la placa enseguida, el preview mostraba «sin señal» un instante).
    bool inmediata = true;
    {
        std::lock_guard<std::mutex> lm(medMu_);
        inmediata = ultimoDatoNs_ == 0 || ahoraNs() - ultimoDatoNs_ > 1'000'000'000ULL;
    }
    // La placa: chica (la composición la escala), con el nombre, a los cuadros del lienzo.
    const std::string fps = std::to_string(lienzo_.fpsN) + "/" + std::to_string(lienzo_.fpsD);
    // Un cuadro (ya rotulado: placaDibujada) que se congela: antes rotulaba 30
    // cuadros por segundo por entrada aunque nadie la mirara (docs/MAPA-GPU-CPU.md).
    const std::string placa = "appsrc name=placa format=time caps=video/x-raw,format=BGRA,width=" + std::to_string(kPlacaAncho) +
                              ",height=" + std::to_string(kPlacaAlto) + ",framerate=" + fps + " ! " +
                              (gpuVideo() ? subirGpu() + " ! " + texturaGpu(",pixel-aspect-ratio=1/1")
                                          : "videoconvert ! " + capsColorDelLienzo(lienzo_) + ",pixel-aspect-ratio=1/1") +
                              " ! imagefreeze name=congela is-live=true ! fs.sink_1";
    const std::string d = "fallbackswitch name=fs timeout=3000000000 immediate-fallback=" +
                          std::string(inmediata ? "true" : "false") + " ! intersink name=sale producer-name=" +
                          entreComillas(productorVideo()) + " sync=false event-types=\"<>\" intersrc producer-name=" +
                          entreComillas(id_ + ":crudo:video") + " max-time=2500000000 ! fs.sink_0 " +
                          placa;
    GError* e = nullptr;
    GstElement* p = gst_parse_launch(d.c_str(), &e);
    if (e != nullptr) {
        registrar(Nivel::Aviso, id_ + ": la placa no se pudo armar (" + e->message + "): sin placa");
        g_error_free(e);
        if (p != nullptr) gst_object_unref(p);
        return;
    }
    if (GstElement* fuente = gst_bin_get_by_name(GST_BIN(p), "placa")) {
        GstBuffer* hecha = placaDibujada((nombre_.empty() ? id_ : nombre_) + " — sin señal");
        GstBuffer* b = gst_buffer_copy(hecha); // comparte la memoria; las horas son de esta
        gst_buffer_unref(hecha);
        GST_BUFFER_PTS(b) = 0;
        GST_BUFFER_DURATION(b) = GST_CLOCK_TIME_NONE;
        GstFlowReturn r = GST_FLOW_OK;
        g_signal_emit_by_name(fuente, "push-buffer", b, &r);
        gst_buffer_unref(b);
        g_signal_emit_by_name(fuente, "end-of-stream", &r);
        gst_object_unref(fuente);
    }
    // LA PLACA DESCANSA mientras la señal llega: imagefreeze la repite a 30
    // cuadros/s y el fallbackswitch la tiraba (con 40 entradas, ~1.200 cuadros por
    // segundo para nada). Pasa uno por segundo (que el fallbackswitch tenga uno
    // fresco cuando lo necesite) y, ya en la placa, todos.
    if (GstElement* fs = gst_bin_get_by_name(GST_BIN(p), "fs")) {
        g_signal_connect(fs, "notify::active-pad",
                         G_CALLBACK(+[](GObject* o, GParamSpec*, gpointer d) {
                             GstPad* activo = nullptr;
                             g_object_get(o, "active-pad", &activo, nullptr);
                             static_cast<Entrada*>(d)->placaActiva_ =
                                 activo != nullptr && std::string(GST_PAD_NAME(activo)) == "sink_1";
                             if (activo != nullptr) gst_object_unref(activo);
                         }),
                         this);
        gst_object_unref(fs);
    }
    if (GstElement* cg = gst_bin_get_by_name(GST_BIN(p), "congela")) {
        if (GstPad* pad = gst_element_get_static_pad(cg, "src")) {
            gst_pad_add_probe(
                pad, GST_PAD_PROBE_TYPE_BUFFER,
                [](GstPad*, GstPadProbeInfo*, gpointer d) -> GstPadProbeReturn {
                    auto* e = static_cast<Entrada*>(d);
                    if (e->placaActiva_.load(std::memory_order_relaxed)) return GST_PAD_PROBE_OK;
                    const std::int64_t ahora = g_get_monotonic_time();
                    if (ahora - e->ultimaPlacaUs_.load(std::memory_order_relaxed) < 1'000'000) return GST_PAD_PROBE_DROP;
                    e->ultimaPlacaUs_.store(ahora, std::memory_order_relaxed);
                    return GST_PAD_PROBE_OK;
                },
                this, nullptr);
            gst_object_unref(pad);
        }
        gst_object_unref(cg);
    }
    if (GstElement* fs = gst_bin_get_by_name(GST_BIN(p), "fs")) {
        g_object_set(fs, "min-upstream-latency", static_cast<guint64>(std::max<std::int64_t>(reloj_.latencia(), 0) + 100 * GST_MSECOND), nullptr);
        gst_object_unref(fs);
    }
    reloj_.adoptar(p);
    fuentesDePruebaEnHora(p, reloj_.ahora());
    gst_element_set_state(p, GST_STATE_PLAYING);
    salidaV_ = p;
    revisarTareas_ = 150; // el appsrc de la placa manda un cuadro y termina
}

void Entrada::ponerEnUso(bool si) {
    enUso_ = si;
    std::lock_guard<std::mutex> l(mu_);
    if (!si) {
        if (sinUsoDesdeUs_ == 0) sinUsoDesdeUs_ = g_get_monotonic_time();
        return;
    }
    sinUsoDesdeUs_ = 0;
    if (conPlaca_ && salidaV_ == nullptr) {
        const std::int64_t t0 = g_get_monotonic_time();
        armarSalida();
        registrar(Nivel::Info, id_ + ": placa rearmada en " + std::to_string((g_get_monotonic_time() - t0) / 1000) + " ms");
    }
}

bool Entrada::enPlaca() const {
    std::lock_guard<std::mutex> l(mu_);
    return enPlacaSinLock();
}

bool Entrada::enPlacaSinLock() const {
    if (salidaV_ == nullptr) return false;
    GstElement* fs = gst_bin_get_by_name(GST_BIN(salidaV_), "fs");
    if (fs == nullptr) return false;
    GstPad* activo = nullptr;
    g_object_get(fs, "active-pad", &activo, nullptr);
    const bool placa = activo != nullptr && std::string(GST_PAD_NAME(activo)) == "sink_1";
    if (activo != nullptr) gst_object_unref(activo);
    gst_object_unref(fs);
    return placa;
}

void Entrada::leer(const json& a) {
    uri_ = a.value("uri", std::string());
    descripcion_ = a.value("pipeline", std::string());
    descripcionAudio_ = a.value("pipeline_audio", std::string());
    camara_ = a.value("camara", std::string());
    camaraModo_ = a.value("camara_modo", std::string());
    microfono_ = a.value("microfono", std::string());
    rack_ = a.value("rack", std::string());
    normalizar_ = a.value("normalizar", true);
    conAudio_ = a.value("audio", true);
    estudio_ = a.value("estudio", true);
    ntpSync_ = a.value("ntp_sync", true);
    latenciaMs_ = entero(a, "latencia", 0);
    desfaseMs_ = entero(a, "desfase", 0);
    desfaseAudioMs_ = entero(a, "desfase_audio", 0);
    desfaseVideoMs_ = std::max<std::int64_t>(0, entero(a, "desfase_video", 0));
    superres_ = a.value("superres", false);
    superresMotor_ = a.value("superres_motor", std::string());
    superresModelo_ = a.value("superres_modelo", std::string());
    medio_ = a.value("medio", false);
    nombre_ = a.value("nombre", std::string());
    bucle_ = a.value("looping", false);
    limpiarAlTerminar_ = a.value("clear_on_media_end", true);
    if (medio_) {
        const std::string f = a.value("local_file", std::string());
        if (!f.empty()) {
            if (gchar* u = gst_filename_to_uri(f.c_str(), nullptr)) {
                uri_ = u;
                g_free(u);
            }
        }
    }
    // Los tipos: arman su propia cadena (lo que antes daba OBS).
    const std::string tipo = a.value("tipo", std::string());
    deApp_ = tipo == "app";
    estatica_ = tipo == "color" || tipo == "imagen" || tipo == "texto" || tipo == "app";
    const int w = a.value("ancho", lienzo_.ancho), h = a.value("alto", lienzo_.alto);
    const std::string tam = ",width=" + std::to_string(std::max(2, w)) + ",height=" + std::to_string(std::max(2, h));
    bool alfaTipo = false;
    if (tipo == "color") {
        // Un cuadro, congelado: no cambia (antes, 30 cuadros por segundo del lienzo).
        descripcion_ = "videotestsrc num-buffers=1 pattern=solid-color foreground-color=" + argb(a.value("color", json()), "0xff000000") +
                       " ! video/x-raw,format=BGRA" + tam + ",framerate=" + std::to_string(lienzo_.fpsN) + "/" +
                       std::to_string(lienzo_.fpsD) + " ! imagefreeze is-live=true";
        alfaTipo = true;
        conAudio_ = false;
    } else if (tipo == "imagen") {
        descripcion_ = "filesrc location=" + conComillas(a.value("archivo", std::string())) +
                       " ! decodebin ! videoconvert ! video/x-raw,format=BGRA ! imagefreeze is-live=true";
        alfaTipo = true;
        conAudio_ = false;
    } else if (tipo == "texto") {
        // Dibujado UNA vez con el dibujante compartido (textoDibujado): cada
        // textoverlay propio armaba su mapa de tipografías (~50 ms por rótulo).
        rotulo_ = std::make_shared<Rotulo>();
        rotulo_->texto = a.value("texto", std::string());
        rotulo_->fuente = a.value("fuente", std::string("Sans")) + (a.value("negrita", true) ? " Bold " : " ") +
                          std::to_string(a.value("tamano", 48));
        const std::string ha = a.value("alineacion", std::string("centro"));
        const std::string va = a.value("vertical", std::string("centro"));
        rotulo_->ha = ha == "izquierda" ? "left" : ha == "derecha" ? "right" : "center";
        rotulo_->va = va == "arriba" ? "top" : va == "abajo" ? "bottom" : "center";
        rotulo_->color = argb(a.value("color", json()), "0xffffffff");
        rotulo_->contorno = a.value("contorno", false);
        rotulo_->colorContorno = argb(a.value("color_contorno", json()), "0xff000000");
        rotulo_->ancho = std::max(2, w);
        rotulo_->alto = std::max(2, h);
        rotulo_->fondo = 0x00000000;
        descripcion_ = "appsrc name=ba_texto format=time caps=video/x-raw,format=BGRA" + tam + ",framerate=" +
                       std::to_string(lienzo_.fpsN) + "/" + std::to_string(lienzo_.fpsD) + " ! imagefreeze is-live=true";
        alfaTipo = true;
        conAudio_ = false;
    } else if (tipo == "app") {
        descripcion_ = "appsrc name=ba_app is-live=true format=time do-timestamp=true max-bytes=0 block=false ! "
                       "imagefreeze is-live=true allow-replace=true";
        alfaTipo = true;
        conAudio_ = false;
    }
    // En la GPU, lo estático se sube UNA vez (antes del imagefreeze, que repite la textura).
    if (estatica_ && gpuVideo()) {
        const auto i = descripcion_.rfind("imagefreeze");
        if (i != std::string::npos)
            descripcion_.insert(i, subirGpu() + " ! " + texturaGpu() + " ! ");
    }
    if (tipo == "pantalla") {
        const int i = a.value("indice", 0);
#if defined(__APPLE__)
        descripcion_ = "avfvideosrc capture-screen=true capture-screen-cursor=" + std::string(a.value("cursor", true) ? "true" : "false") +
                       " device-index=" + std::to_string(i);
#elif defined(_WIN32)
        descripcion_ = std::string(hayFabrica("d3d12screencapturesrc") ? "d3d12screencapturesrc" : "d3d11screencapturesrc") +
                       " monitor-index=" + std::to_string(i) + " show-cursor=" + (a.value("cursor", true) ? "true" : "false");
#else
        descripcion_ = hayFabrica("pipewiresrc") ? "pipewiresrc" : "ximagesrc use-damage=false show-pointer=" +
                                                                       std::string(a.value("cursor", true) ? "true" : "false");
#endif
        conAudio_ = false;
    }
    const json fl = comoJson(a.value("filtros", json()));
    filtrosV_ = leerFiltros(fl.is_object() ? fl.value("video", json::array()) : json::array());
    filtrosA_ = leerFiltros(fl.is_object() ? fl.value("audio", json::array()) : json::array());
    alfa_ = alfaTipo || (fl.is_object() && fl.value("alfa", false));
    for (auto& f : leerFiltros(comoJson(a.value("filtros_audio", json()))))
        filtrosA_.push_back(std::move(f));
}

bool Entrada::daVideo() const { return !camara_.empty() || !descripcion_.empty() || (!uri_.empty()); }
bool Entrada::daAudio() const { return conAudio_ && (!descripcionAudio_.empty() || !uri_.empty()); }

void Entrada::actualizar(const json& ajustes) {
    std::lock_guard<std::mutex> l(mu_);
    const std::string firmaAntes = firma_;
    const auto vAntes = filtrosV_;
    const auto aAntes = filtrosA_;
    leer(ajustes);
    // La firma de lo que obliga a rearmar (todo menos las propiedades vivas de los filtros).
    const std::string firma = uri_ + "|" + descripcion_ + "|" + descripcionAudio_ + "|" + camara_ + "|" +
                              camaraModo_ + "|" + microfono_ + "|" + rack_ + "|" + (normalizar_ ? "n" : "") +
                              (conAudio_ ? "a" : "") + (medio_ ? "m" : "") + "|" + std::to_string(latenciaMs_) + "|" +
                              superresMotor_ + superresModelo_ + (superres_ ? "s" : "") + "|" +
                              firmaDe(filtrosV_, filtrosA_, alfa_);
    if (firma == firmaAntes && pipeline_ != nullptr) {
        // Lo mismo: solo las propiedades que se cambian andando, el desfase y el bucle.
        ponerEnVivo(pipeline_, filtrosV_, "ba_f_");
        ponerEnVivo(pipeline_, filtrosA_, "ba_fa_");
        corregirOffset();
        return;
    }
    firma_ = firma;
    entrelazada_ = false; // otra señal: se vuelve a mirar
    desarmar();
    armarSalida(); // pudo pasar a tener (o dejar de tener) video, o ser un archivo
    armar();
}

void Entrada::desarmar() {
    if (pipeline_ != nullptr) {
        guardarEstados();
        gst_element_set_state(pipeline_, GST_STATE_NULL);
        gst_object_unref(pipeline_);
        pipeline_ = nullptr;
    }
    if (!origenRed_.empty()) {
        origen_red::soltar(origenRed_);
        origenRed_.clear();
    }
    baseAplicada_ = GST_CLOCK_TIME_NONE;
    appAncho_ = appAlto_ = 0;
    realineadoV_ = realineadoA_ = 0;
    medVAntes_ = medAAntes_ = 0;
}

void Entrada::guardarEstados() {
    // Convención: un elemento de la cadena con propiedad `estado` (texto, de
    // lectura y escritura) la conserva cuando la cadena se rearma.
    GstIterator* it = gst_bin_iterate_elements(GST_BIN(pipeline_));
    GValue v = G_VALUE_INIT;
    while (gst_iterator_next(it, &v) == GST_ITERATOR_OK) {
        auto* e = GST_ELEMENT(g_value_get_object(&v));
        GParamSpec* ps = g_object_class_find_property(G_OBJECT_GET_CLASS(e), "estado");
        if (ps != nullptr && ps->value_type == G_TYPE_STRING && (ps->flags & G_PARAM_READWRITE) == G_PARAM_READWRITE) {
            gchar* t = nullptr;
            g_object_get(e, "estado", &t, nullptr);
            gchar* nombre = gst_object_get_name(GST_OBJECT(e));
            if (t != nullptr && nombre != nullptr) estados_[nombre] = t;
            g_free(t);
            g_free(nombre);
        }
        g_value_reset(&v);
    }
    g_value_unset(&v);
    gst_iterator_free(it);
}

void Entrada::programarReintento() {
    static const std::uint64_t kEsperas[] = {200'000'000ULL, 500'000'000ULL, 1'000'000'000ULL, 2'000'000'000ULL};
    reintentoNs_ = ahoraNs() + kEsperas[std::min(reintentos_, 3)];
    ++reintentos_;
}

void Entrada::armar() {
    desarmar();
    controlEnviado_ = false; // la cadena nueva lo recibe apenas pasen cuadros
    segmentoListo_ = false;
    avisos_.clear();
    error_.clear();
    if (uri_.empty() && descripcion_.empty() && descripcionAudio_.empty() && camara_.empty()) return;
    // Un dispositivo del equipo que no está: a esperar que aparezca (el vigía avisa).
    const auto esperar = [this](const std::string& que) {
        if (!esperaDispositivo_) registrar(Nivel::Aviso, id_ + ": " + que + ": se espera a que se conecte");
        error_ = que + ": esperando que se conecte";
        esperaDispositivo_ = true;
        genVista_ = Vigia::el().generacion();
        esperaDesdeNs_ = ahoraNs();
    };
    if (!microfono_.empty() && microfono_ != "default" && !Vigia::el().hay(false, microfono_)) {
        esperar("el micrófono " + microfono_);
        return;
    }
    std::string origenV = descripcion_;
    if (!camara_.empty()) {
        origenV = origenDeCamara(camara_, camaraModo_, lienzo_);
        if (origenV.empty()) {
            esperar("la cámara " + camara_);
            return;
        }
    }
    if (esperaDispositivo_) registrar(Nivel::Info, id_ + ": apareció el dispositivo, se conecta");
    esperaDispositivo_ = false;

    // NORMALIZAR (de fábrica): desentrelaza si hace falta, lleva a los cuadros
    // del lienzo, al color del estudio y a píxel cuadrado. Una cámara rara entra
    // como las demás. El tamaño queda: lo escala la composición.
    const std::string fps = std::to_string(lienzo_.fpsN) + "/" + std::to_string(lienzo_.fpsD);
    std::string capsVideo = "video/x-raw";
    std::string antes = "videoconvert ! ";
    if (normalizar_) {
        antes = "deinterlace mode=auto ! videorate skip-to-first=true ! videoconvertscale ! ";
        capsVideo = capsColorDelLienzo(lienzo_) + ",interlace-mode=progressive,pixel-aspect-ratio=1/1,framerate=" + fps;
    }
    // LOS FILTROS (las tarjetas del grafo). La superresolución vieja es uno más.
    std::vector<Filtro> filtrosV = filtrosV_;
    if (filtrosV.empty()) {
        const std::string motor = !superresMotor_.empty() ? superresMotor_ : (superres_ ? "apple" : "");
        Filtro sr{"superres", "", "superres", "", false, {}};
        if (motor == "apple")
            sr.elemento = "bavtsuperres";
        else if (motor == "fsr")
            sr.elemento = "bafsr";
        else if (motor == "onnx" && !superresModelo_.empty()) {
            sr.elemento = "baonnxsr";
            sr.props.emplace_back("modelo", superresModelo_);
        }
        if (!sr.elemento.empty()) filtrosV.push_back(std::move(sr));
    }
    const bool usaGpu = gpuVideo();
    const std::string cadenaV = cadenaDe(filtrosV, "videoconvert", "ba_f_", avisos_, usaGpu);
    const std::string cola = "queue leaky=downstream max-size-buffers=1 max-size-time=0 max-size-bytes=0 ! ";
    if (!cadenaV.empty() && !usaGpu) {
        // ANTES de `videorate` (no se procesa cada cuadro repetido), con una cola que
        // descarta lo viejo (si un filtro tarda, no se acumula demora). Si el primer
        // filtro trabaja en la placa, va pegado al decodificador.
        const bool placaPrimero = !filtrosV.front().memoria.empty();
        if (placaPrimero) {
            antes = cola + cadenaV + "videoconvert ! " +
                    (normalizar_ ? "deinterlace mode=auto ! videorate skip-to-first=true ! videoconvertscale ! " : "");
        } else {
            const std::string cadena =
                std::string(normalizar_ ? "videoconvertscale ! video/x-raw,pixel-aspect-ratio=1/1 ! " : "") + cola +
                cadenaV + "videoconvert ! ";
            antes = normalizar_ ? "deinterlace mode=auto ! " + cadena + "videorate skip-to-first=true ! videoconvertscale ! " : cadena;
        }
    }
    if (alfa_) {
        // Con transparencia (croma, recorte): BGRA.
        capsVideo = "video/x-raw,format=BGRA";
        if (normalizar_) capsVideo += ",interlace-mode=progressive,pixel-aspect-ratio=1/1,framerate=" + fps;
    }
    if (usaGpu) {
        // EN LA GPU (docs/MAPA-GPU-CPU.md): la cámara y el decodificador entregan
        // memoria de GPU directo a la subida (que la deja pasar: glupload, d3d12upload)
        // y la entrada publica una textura RGBA. La normalización la hace la composición (escala cada capa);
        // acá solo los cuadros del lienzo (videorate no toca píxeles). Los filtros
        // de GL (y baptz → baptzgl) trabajan en la textura; para uno de CPU se baja
        // ahí y se sube de nuevo al final. Lo estático ya viene subido (leer).
        // Una señal entrelazada (la sonda del origen la descubre y rearma) se
        // desentrelaza en GL; a una progresiva no se le pone nada.
        const std::string textura = texturaGpu();
        const std::string gl = texturaGpu(normalizar_ && !estatica_ ? ",framerate=" + fps : std::string());
        const char* desentrelazador = gpu() == Gpu::D3D12 ? "d3d12deinterlace" : "gldeinterlace method=greedyh";
        const bool desentrelazar = normalizar_ && entrelazada_ && !estatica_ &&
                                   hayFabrica(gpu() == Gpu::D3D12 ? "d3d12deinterlace" : "gldeinterlace");
        antes = (estatica_ ? std::string() : subirGpu() + " ! " + textura + " ! ") +
                (desentrelazar ? std::string(desentrelazador) + " ! " : "") + (cadenaV.empty() ? "" : cola + cadenaV) +
                (normalizar_ && !estatica_ ? "videorate skip-to-first=true ! " : "");
        capsVideo = (cadenaV.empty() && estatica_ ? std::string() : subirGpu() + " ! ") + gl;
    }
    std::string cadenaA = cadenaDe(filtrosA_, "audioconvert", "ba_fa_", avisos_);
    rackFaltaDesde_ = 0;
    if (!rack_.empty()) {
        if (hayFabrica("barack")) {
            cadenaA = std::string("audioconvert ! audioresample ! ") + kAudio + " ! barack name=ba_rack fuente=" +
                      entreComillas(rack_) + " ! " + cadenaA;
        } else {
            avisos_.push_back("el rack de audio todavía no está (la app no registró barack): se agrega cuando esté");
            rackFaltaDesde_ = ahoraNs();
        }
    }
    // Un archivo marca su ritmo (no es en vivo); lo en vivo entrega apenas tiene.
    // El EOS no pasa a los consumidores (event-types vacío): un pad del mezclador
    // que recibe EOS queda terminado para siempre, y un archivo que terminó vuelve
    // a sonar con reiniciar (o la entrada se rearma).
    const std::string ritmo = medio_ ? "identity name=ba_ritmo_%s sync=true ! " : "";
    const auto salida = [&](const char* tipo) {
        std::string r = ritmo;
        if (!r.empty()) r.replace(r.find("%s"), 2, tipo);
        return r + "intersink name=ba_i" + std::string(tipo) + " producer-name=" +
               entreComillas(std::string(tipo) == "v" ? productorCrudoVideo() : productorAudio()) +
               " sync=false event-types=\"<>\"";
    };
    std::string completo;
    if (!uri_.empty() && !medio_ && origen_red::esDeRed(uri_)) {
        // Una cámara de red: la conexión es compartida con las otras entradas de la
        // misma dirección (ver OrigenRed.hpp); acá se toma lo ya decodificado.
        origenRed_ = origen_red::usar(reloj_, uri_, static_cast<int>(latenciaMs_), ntpSync_ && !ntpApagadoSolo_);
        completo = "intersrc producer-name=" + entreComillas(origenRed_ + ":video") + " ! identity name=ba_origen ! " + antes +
                   capsVideo + " ! " + salida("v");
        if (conAudio_)
            completo += " intersrc producer-name=" + entreComillas(origenRed_ + ":audio") + " ! " + cadenaA +
                        "audioconvert ! audioresample ! " + kAudio + " ! " + salida("a");
    } else if (!uri_.empty()) {
        // Un archivo: solo las ramas de lo que tiene (una rama sin datos nunca termina),
        // con los pads por nombre (si no, el audio de un mp3 se engancha en la del video).
        bool hayV = true, hayA = conAudio_;
        std::string padV = "ba_dec.", padA = "ba_dec.";
        if (medio_) {
            if (!queTieneElArchivo(uri_, hayV, hayA, error_)) {
                estadoMedio_ = "error";
                registrar(Nivel::Aviso, id_ + ": " + error_);
                return;
            }
            hayA = hayA && conAudio_;
            padV = "ba_dec.video_0";
            padA = "ba_dec.audio_0";
        }
        completo = "uridecodebin3 name=ba_dec uri=" + entreComillas(uri_);
        if (hayV) completo += " " + padV + " ! identity name=ba_origen ! " + antes + capsVideo + " ! " + salida("v");
        if (hayA)
            completo += " " + padA + " ! " + cadenaA + "audioconvert ! audioresample ! " + kAudio + " ! " +
                        salida("a");
    } else {
        // Lo estático termina en un imagefreeze, que ya tiene su hilo: sin cola.
        if (!origenV.empty())
            completo = origenV + (estatica_ ? " ! identity name=ba_origen ! " : " ! queue name=ba_origen ! ") + antes + capsVideo +
                       " ! " + salida("v");
        if (!descripcionAudio_.empty() && conAudio_)
            completo += std::string(completo.empty() ? "" : " ") + descripcionAudio_ + " ! queue ! " + cadenaA +
                        "audioconvert ! audioresample ! " + kAudio + " ! " + salida("a");
    }
    GError* err = nullptr;
    GstElement* p = gst_parse_launch(completo.c_str(), &err);
    if (err != nullptr) {
        error_ = err->message;
        g_error_free(err);
        if (p != nullptr) gst_object_unref(p);
        registrar(Nivel::Aviso, id_ + ": " + error_);
        programarReintento();
        return;
    }
    if (GstElement* dec = gst_bin_get_by_name(GST_BIN(p), "ba_dec")) {
        // RTSP y SRT: la latencia de SU jitter buffer, y en RTSP la hora de captura del que manda.
        g_signal_connect(dec, "source-setup",
                         G_CALLBACK(+[](GstElement*, GstElement* origen, gpointer d) {
                             auto* e = static_cast<Entrada*>(d);
                             GObjectClass* k = G_OBJECT_GET_CLASS(origen);
                             const std::string lat = std::to_string(std::max<std::int64_t>(e->latenciaMs_, 20));
                             const bool ntp = e->ntpSync_ && !e->ntpApagadoSolo_;
                             if (g_object_class_find_property(k, "latency") != nullptr)
                                 gst_util_set_object_arg(G_OBJECT(origen), "latency", lat.c_str());
                             if (g_object_class_find_property(k, "ntp-sync") != nullptr)
                                 gst_util_set_object_arg(G_OBJECT(origen), "ntp-sync", ntp ? "true" : "false");
                             if (g_object_class_find_property(k, "buffer-mode") != nullptr)
                                 gst_util_set_object_arg(G_OBJECT(origen), "buffer-mode", ntp ? "synced" : "slave");
                         }),
                         this);
        gst_object_unref(dec);
    }
    // Cómo llega la señal (antes de normalizar): para el diagnóstico.
    if (GstElement* q = gst_bin_get_by_name(GST_BIN(p), "ba_origen")) {
        if (GstPad* pad = gst_element_get_static_pad(q, "sink")) {
            gst_pad_add_probe(
                pad, GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM,
                [](GstPad*, GstPadProbeInfo* info, gpointer d) -> GstPadProbeReturn {
                    GstEvent* ev = GST_PAD_PROBE_INFO_EVENT(info);
                    if (GST_EVENT_TYPE(ev) == GST_EVENT_CAPS) {
                        GstCaps* c = nullptr;
                        gst_event_parse_caps(ev, &c);
                        auto* e = static_cast<Entrada*>(d);
                        const json desc = describirCaps(c);
                        // En la GPU no hay `deinterlace mode=auto`: si llega entrelazada, rearmar con gldeinterlace/d3d12deinterlace.
                        if (gpuVideo() && !e->entrelazada_ && c != nullptr && gst_caps_get_size(c) > 0) {
                            const char* im = gst_structure_get_string(gst_caps_get_structure(c, 0), "interlace-mode");
                            if (im != nullptr && std::string(im) != "progressive") {
                                e->entrelazada_ = true;
                                e->rearmarEntrelazada_ = true;
                            }
                        }
                        std::lock_guard<std::mutex> l(e->medMu_);
                        e->origen_ = desc;
                    }
                    return GST_PAD_PROBE_OK;
                },
                this, nullptr);
            gst_object_unref(pad);
        }
        gst_object_unref(q);
    }
    // SIN USAR, A 2 CUADROS POR SEGUNDO: una entrada que no está en ninguna
    // composición ni vista no procesa sus filtros (PTZ, color, superresolución…)
    // a 30 cuadros por segundo. Todo lo de GL va por un hilo: ocho cámaras
    // trabajando para nadie atrasaban el programa (bajaba a 15-20 cuadros/s).
    // Como libobs, que solo dibuja lo que se ve. Dos por segundo alcanzan para la
    // miniatura y para saber que hay señal; en uso, todos.
    if (GstElement* q = gst_bin_get_by_name(GST_BIN(p), "ba_origen")) {
        if (GstPad* pad = gst_element_get_static_pad(q, "src")) {
            gst_pad_add_probe(
                pad, GST_PAD_PROBE_TYPE_BUFFER,
                [](GstPad*, GstPadProbeInfo*, gpointer d) -> GstPadProbeReturn {
                    auto* e = static_cast<Entrada*>(d);
                    if (e->enUso_.load(std::memory_order_relaxed)) return GST_PAD_PROBE_OK;
                    const std::int64_t ahora = g_get_monotonic_time();
                    if (ahora - e->ultimoPasoUs_.load(std::memory_order_relaxed) < 500'000) return GST_PAD_PROBE_DROP;
                    e->ultimoPasoUs_.store(ahora, std::memory_order_relaxed);
                    return GST_PAD_PROBE_OK;
                },
                this, nullptr);
            gst_object_unref(pad);
        }
        gst_object_unref(q);
    }
    // El rótulo de texto, ya dibujado: un cuadro y fin (lo repite imagefreeze).
    if (rotulo_)
        if (GstElement* t = gst_bin_get_by_name(GST_BIN(p), "ba_texto")) {
            GstBuffer* hecho = textoDibujado(*rotulo_);
            GstBuffer* b = gst_buffer_copy(hecho);
            gst_buffer_unref(hecho);
            GST_BUFFER_PTS(b) = 0;
            GST_BUFFER_DURATION(b) = GST_CLOCK_TIME_NONE;
            GstFlowReturn r = GST_FLOW_OK;
            g_signal_emit_by_name(t, "push-buffer", b, &r);
            gst_buffer_unref(b);
            g_signal_emit_by_name(t, "end-of-stream", &r);
            gst_object_unref(t);
        }
    // Las propiedades de cada filtro (las que no tiene, se avisan).
    for (const auto* lista : {&filtrosV, &filtrosA_})
        for (const auto& x : *lista) {
            const std::string n = (lista == &filtrosV ? "ba_f_" : "ba_fa_") + x.nombre;
            if (GstElement* e = gst_bin_get_by_name(GST_BIN(p), n.c_str())) {
                ponerPropiedades(e, x, &avisos_);
                // Lo que tenía antes de rearmar (el encuadre del PTZ digital…).
                if (auto it = estados_.find(n);
                    it != estados_.end() && g_object_class_find_property(G_OBJECT_GET_CLASS(e), "estado") != nullptr)
                    g_object_set(e, "estado", it->second.c_str(), nullptr);
                gst_object_unref(e);
            }
        }
    for (const auto& a : avisos_) registrar(Nivel::Aviso, id_ + ": " + a);
    // Lo que sale: cuán viejo (la latencia de la cadena), si llegan datos y cómo.
    for (const char* n : {"ba_iv", "ba_ia"})
        if (GstElement* e = gst_bin_get_by_name(GST_BIN(p), n)) {
            if (GstPad* pad = gst_element_get_static_pad(e, "sink")) {
                gst_pad_add_probe(pad, static_cast<GstPadProbeType>(GST_PAD_PROBE_TYPE_BUFFER | GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM),
                                  &Entrada::alSalir, this, nullptr);
                gst_object_unref(pad);
            }
            gst_object_unref(e);
        }
    sinRelojPropio(p);
    if (!medio_) fuentesDePruebaEnHora(p, reloj_.ahora());
    if (medio_) {
        // Un archivo: el reloj del estudio pero su propio base_time (los saltos lo cambian).
        gst_pipeline_use_clock(GST_PIPELINE(p), reloj_.clock());
    } else {
        reloj_.adoptar(p);
    }
    {
        std::lock_guard<std::mutex> l(medMu_);
        edades_.clear();
        edadesA_.clear();
        cuadros_ = bloques_ = 0;
        ultimoDatoNs_ = 0;
        vistosDatos_ = 0;
        datosDesdeNs_ = ahoraNs();
        edadP50_ = edadP95_ = 0;
        sale_ = json::object();
    }
    pipeline_ = p;
    latencia_.olvidar(); // lo que respondía la cadena anterior ya no vale
    corregirOffset();
    if (gst_element_set_state(p, GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE) {
        error_ = "el pipeline no arrancó";
        desarmar();
        if (medio_) estadoMedio_ = "error";
        programarReintento();
        return;
    }
    if (medio_) estadoMedio_ = "playing";
    revisarTareas_ = 150; // un rótulo, un color, una imagen: un cuadro y su fuente termina
}

GstPadProbeReturn Entrada::alSalir(GstPad* pad, GstPadProbeInfo* info, gpointer d) {
    auto* e = static_cast<Entrada*>(d);
    const bool esVideo = std::string(GST_OBJECT_NAME(GST_OBJECT_PARENT(pad))) == "ba_iv";
    if ((info->type & GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM) != 0) {
        GstEvent* ev = GST_PAD_PROBE_INFO_EVENT(info);
        if (esVideo && GST_EVENT_TYPE(ev) == GST_EVENT_CAPS) {
            GstCaps* c = nullptr;
            gst_event_parse_caps(ev, &c);
            const json desc = describirCaps(c);
            std::lock_guard<std::mutex> l(e->medMu_);
            e->sale_ = desc;
        }
        return GST_PAD_PROBE_OK;
    }
    GstBuffer* b = GST_PAD_PROBE_INFO_BUFFER(info);
    if (b == nullptr) return GST_PAD_PROBE_OK;
    // Lo en vivo usa el reloj y el base_time del estudio: la edad es ahora − hora del cuadro.
    double edad = -1e9;
    if (!e->medio_ && GST_BUFFER_PTS_IS_VALID(b)) {
        if (GstEvent* ev = gst_pad_get_sticky_event(pad, GST_EVENT_SEGMENT, 0)) {
            const GstSegment* seg = nullptr;
            gst_event_parse_segment(ev, &seg);
            const GstClockTime rt = gst_segment_to_running_time(seg, GST_FORMAT_TIME, GST_BUFFER_PTS(b));
            gst_event_unref(ev);
            if (GST_CLOCK_TIME_IS_VALID(rt))
                edad = (static_cast<double>(e->reloj_.ahora()) - static_cast<double>(rt)) / 1e6;
        }
    }
    std::lock_guard<std::mutex> l(e->medMu_);
    e->ultimoDatoNs_ = ahoraNs();
    if (esVideo) {
        ++e->cuadros_;
        if (edad > -1e8) {
            e->edades_.push_back(edad);
            while (e->edades_.size() > 300) e->edades_.pop_front();
        }
    } else {
        ++e->bloques_;
        if (edad > -1e8) {
            e->edadesA_.push_back(edad);
            while (e->edadesA_.size() > 300) e->edadesA_.pop_front();
        }
        if (e->cuadros_ == 0 && edad > -1e8) { // solo audio: cuenta sus bloques
            e->edades_.push_back(edad);
            while (e->edades_.size() > 300) e->edades_.pop_front();
        }
    }
    return GST_PAD_PROBE_OK;
}

void Entrada::corregirOffset() {
    if (pipeline_ == nullptr) return;
    const gint64 desf = desfaseMs_ * static_cast<gint64>(GST_MSECOND);
    const GstClockTime base = gst_element_get_base_time(pipeline_);
    for (const char* n : {"ba_iv", "ba_ia"})
        if (GstElement* e = gst_bin_get_by_name(GST_BIN(pipeline_), n)) {
            // En el pad de SALIDA del elemento anterior: ahí GStreamer reenvía los
            // eventos fijos (el segmento) con el offset nuevo enseguida; en el de
            // entrada de intersink recién valía con el próximo segmento (y tras una
            // pausa no viene ninguno: todo llegaba tarde y el mezclador lo tiraba).
            if (GstPad* pad = gst_element_get_static_pad(e, "sink")) {
                if (GstPad* antes = gst_pad_get_peer(pad)) {
                        const gint64 dif = medio_ ? static_cast<gint64>(base) - static_cast<gint64>(reloj_.base())
                                                  : (std::string(n) == "ba_iv" ? realineadoV_ : realineadoA_);
                    const gint64 propio = (std::string(n) == "ba_ia" ? desfaseAudioMs_ : desfaseVideoMs_) *
                                          static_cast<gint64>(GST_MSECOND);
                    gst_pad_set_offset(antes, dif + desf + propio);
                    gst_object_unref(antes);
                }
                gst_object_unref(pad);
            }
            gst_object_unref(e);
        }
    baseAplicada_ = base;
}

void Entrada::medirLatencia() {
    if (medio_ || pipeline_ == nullptr) return;
    const std::uint64_t ahora = ahoraNs();
    if (ahora - consultaNs_ < 1'000'000'000ULL) return;
    consultaNs_ = ahora;
    // La consulta de latencia, EN OTRO HILO: esto corre en el bucle del motor
    // con su candado tomado, y una fuente que se está conectando (una cámara de
    // red al arrancar o al volver) la tenía segundos sin responder. Mientras
    // tanto todo lo que pasa por el motor esperaba, la interfaz incluida.
    // Acá se lee lo último que respondió; la próxima pregunta sale cuando
    // contestó la anterior.
    if (const std::int64_t ns = latencia_.consultar(pipeline_); ns >= 0) declaradoMs_ = static_cast<double>(ns) / 1e6;
    // Lo medido: el percentil 95 de lo viejo que salen los últimos ~10 s (no todos
    // los elementos declaran lo que demoran: vtdec guarda cuadros y no lo dice).
    std::uint64_t datos = 0;
    {
        std::lock_guard<std::mutex> l(medMu_);
        datos = cuadros_ > 0 ? cuadros_ : bloques_ / 2;
        if (edades_.size() >= 30) {
            std::vector<double> v(edades_.begin(), edades_.end());
            std::sort(v.begin(), v.end());
            edadP50_ = v[v.size() / 2];
            edadP95_ = std::clamp(v[v.size() * 95 / 100], 0.0, 5000.0);
        }
    }
    // Una fuente en vivo con SUS horas (empieza en 0, como videotestsrc, o un
    // emisor sin hora de captura): lo que entrega está corrido segundos del
    // estudio. Se corrige con el offset del pad, como un archivo; cada rama por
    // su lado (el video y el audio de una entrada pueden venir de lados distintos).
    {
        double medV = 0, medA = 0;
        bool hayV = false, hayA = false;
        {
            std::lock_guard<std::mutex> l(medMu_);
            const auto mediana = [](const std::deque<double>& d, double& m) {
                if (d.size() < 10) return false;
                std::vector<double> v(d.begin(), d.end());
                std::nth_element(v.begin(), v.begin() + static_cast<std::ptrdiff_t>(v.size() / 2), v.end());
                m = v[v.size() / 2];
                return true;
            };
            hayV = cuadros_ > 0 && mediana(edades_, medV);
            hayA = mediana(edadesA_, medA);
        }
        // Solo si es estable: una fuente que arrancó atrasada y se está poniendo al
        // día (videotestsrc en vivo sale a las apuradas hasta alcanzar la hora) no se toca.
        const bool estableV = hayV && std::fabs(medV - medVAntes_) < 150.0;
        const bool estableA = hayA && std::fabs(medA - medAAntes_) < 150.0;
        if (hayV) medVAntes_ = medV;
        if (hayA) medAAntes_ = medA;
        bool corrigio = false;
        // EL FRENO: una cámara de red cuyas horas saltan entre dos referencias (la
        // del RTP y la del RTCP cuando llega) hacía ir y venir el realineo cada
        // pocos segundos (+1030 ms, −1009 ms…), y cada vuelta corta el audio y el
        // video de esa entrada. Entre una corrección y la siguiente, 10 s; si una
        // deshace a la anterior (signo contrario dentro de 20 s), esa rama queda
        // quieta un minuto.
        const std::int64_t ahoraUs = g_get_monotonic_time();
        const auto puede = [&](Realineo& r, double med, const char* que) {
            if (ahoraUs < r.quietoHastaUs || ahoraUs - r.ultimoUs < 10'000'000) return false;
            if (r.ultimoUs != 0 && ahoraUs - r.ultimoUs < 20'000'000 && (med > 0) != (r.ultimo > 0)) {
                r.quietoHastaUs = ahoraUs + 60'000'000;
                registrar(Nivel::Aviso, id_ + ": las horas de su " + que + " van y vienen (" +
                                            std::to_string(static_cast<int>(r.ultimo)) + " ms, " +
                                            std::to_string(static_cast<int>(med)) + " ms): no se realinea por un minuto");
                return false;
            }
            r.ultimoUs = ahoraUs;
            r.ultimo = med;
            return true;
        };
        if (estableV && std::fabs(medV) > 1000.0 && puede(realineoV_, medV, "video")) {
            realineadoV_ += static_cast<std::int64_t>((medV - 10.0) * 1e6);
            registrar(Nivel::Info, id_ + ": su video trae sus propias horas (corridas " + std::to_string(static_cast<int>(medV)) +
                                       " ms del estudio): se realinea");
            corrigio = true;
        }
        if (estableA && std::fabs(medA) > 1000.0 && puede(realineoA_, medA, "audio")) {
            realineadoA_ += static_cast<std::int64_t>((medA - 10.0) * 1e6);
            registrar(Nivel::Info, id_ + ": su audio trae sus propias horas (corridas " + std::to_string(static_cast<int>(medA)) +
                                       " ms del estudio): se realinea");
            corrigio = true;
        }
        if (corrigio) {
            corregirOffset();
            std::lock_guard<std::mutex> l(medMu_);
            edades_.clear();
            edadesA_.clear();
            edadP50_ = edadP95_ = 0;
            medVAntes_ = medAAntes_ = 0;
            return;
        }
    }
    const std::int64_t piso = latenciaMs_ * static_cast<std::int64_t>(GST_MSECOND);
    const double necesitaMs = std::max(declaradoMs_ + 15.0, edadP95_ + 10.0);
    const std::int64_t propia = std::max<std::int64_t>(piso, static_cast<std::int64_t>(necesitaMs * 1e6));
    const std::int64_t pide = datos > 30 ? propia : piso;
    pide_ = pide;
    const bool tarde = estudio_ && reloj_.llegaTarde(pide);
    if (tarde && !llegaTarde_)
        registrar(Nivel::Aviso, id_ + " llega tarde (" + std::to_string(pide / 1000000) + " ms, el tope del estudio es " +
                                    std::to_string(reloj_.topeMs()) + " ms): va con su propia demora");
    llegaTarde_ = tarde;
    if (estudio_)
        reloj_.anotar(id_, pide);
    else
        reloj_.olvidar(id_);
    // RTSP: la hora de la cámara (RTCP) muy corrida hace que los cuadros salgan «del
    // futuro» (edad negativa). Sostenido: se deja de usar su hora y se rearma.
    if (ntpSync_ && !ntpApagadoSolo_ && uri_.rfind("rtsp", 0) == 0 && datos > 60 && edadP50_ < -400) {
        registrar(Nivel::Aviso, id_ + ": la hora de la cámara está corrida " + std::to_string(static_cast<int>(-edadP50_)) +
                                    " ms; se deja de usar (ntp-sync)");
        ntpApagadoSolo_ = true;
        desarmar();
        reintentoNs_ = ahora;
    }
}

void Entrada::vigilarDatos() {
    if (medio_ || pipeline_ == nullptr || esperaDispositivo_) return;
    const std::uint64_t ahora = ahoraNs();
    std::uint64_t datos = 0;
    {
        std::lock_guard<std::mutex> l(medMu_);
        datos = cuadros_ + bloques_;
    }
    if (datos != vistosDatos_) {
        if (datos > 0) reintentos_ = 0; // llegan datos: la espera vuelve a empezar
        vistosDatos_ = datos;
        datosDesdeNs_ = ahora;
        return;
    }
    if (vistosDatos_ == 0 || ahora - datosDesdeNs_ < 10'000'000'000ULL) return;
    registrar(Nivel::Aviso, id_ + ": no llegan datos hace 10 s sin error: se rearma");
    error_ = "no llegaban datos (se rearma)";
    desarmar();
    programarReintento();
}

void Entrada::tic() {
    std::lock_guard<std::mutex> l(mu_);
    if (!controlEnviado_ && !control_.empty() && pipeline_ != nullptr) {
        std::uint64_t c = 0;
        {
            std::lock_guard<std::mutex> lm(medMu_);
            c = cuadros_;
        }
        if (c > 0) controlEnviado_ = enviarControl();
    }

    // LA PLACA SE DESARMA si hace 15 s que nadie usa la entrada: sus hilos
    // (intersrc, imagefreeze, el fallbackswitch) y su trabajo, por entrada que nadie
    // mira. La cadena sigue andando (a 2 cuadros/s): la alarma de «sin señal» la
    // mira a ella. Rearmarla tarda ~2 ms (ponerEnUso).
    if (salidaV_ != nullptr && !enUso_ && sinUsoDesdeUs_ != 0 && g_get_monotonic_time() - sinUsoDesdeUs_ > 15'000'000) {
        const std::int64_t t0 = g_get_monotonic_time();
        desarmarSalida();
        registrar(Nivel::Info, id_ + ": placa en reposo (nadie la usa) en " + std::to_string((g_get_monotonic_time() - t0) / 1000) + " ms");
    }
    if (salidaV_ != nullptr) {
        GstBus* bus = gst_element_get_bus(salidaV_);
        while (GstMessage* m = gst_bus_pop(bus)) {
            if (GST_MESSAGE_TYPE(m) == GST_MESSAGE_ERROR) {
                GError* e = nullptr;
                gst_message_parse_error(m, &e, nullptr);
                registrar(Nivel::Aviso, id_ + " (placa): " + (e != nullptr ? e->message : "error"));
                if (e != nullptr) g_error_free(e);
            }
            gst_message_unref(m);
        }
        gst_object_unref(bus);
    }
    // Las fuentes de un cuadro ya terminaron: sus hilos, de vuelta (unos 3 s
    // después de armar, revisando cada 200 ms; un archivo no: se rebobina).
    static const bool soltar = g_getenv("BA_SIN_SOLTAR_TAREAS") == nullptr;
    if (soltar && revisarTareas_ > 0 && --revisarTareas_ % 10 == 0) {
        if (!medio_) soltarTareasTerminadas(pipeline_);
        soltarTareasTerminadas(salidaV_);
    }
    const bool deDispositivo = !camara_.empty() || (!microfono_.empty() && microfono_ != "default");
    if (deDispositivo) Vigia::el().revisar();
    if (pipeline_ == nullptr && esperaDispositivo_) {
        // Esperando su dispositivo: se arma cuando el vigía avisa (o cada 15 s, de respaldo).
        if (Vigia::el().generacion() != genVista_ || ahoraNs() - esperaDesdeNs_ > 15'000'000'000ULL) armar();
        return;
    }
    // Lo está usando y se fue: a esperar que vuelva.
    if (pipeline_ != nullptr && deDispositivo && Vigia::el().generacion() != genVista_) {
        genVista_ = Vigia::el().generacion();
        const bool esta = !camara_.empty() ? Vigia::el().hay(true, camara_) : Vigia::el().hay(false, microfono_);
        if (!esta) {
            armar(); // queda esperando
            return;
        }
    }
    if (pipeline_ == nullptr) {
        if (reintentoNs_ != 0 && ahoraNs() >= reintentoNs_) {
            reintentoNs_ = 0;
            armar();
        }
        return;
    }
    vigilarDatos();
    if (pipeline_ == nullptr) return;
    medirLatencia();
    if (pipeline_ == nullptr) return;
    if (rearmarEntrelazada_.exchange(false)) {
        registrar(Nivel::Info, id_ + ": la señal llega entrelazada: se desentrelaza en la GPU");
        armar();
        return;
    }
    // Se armó sin el rack: cada segundo se mira si ya está.
    if (rackFaltaDesde_ != 0 && ahoraNs() - rackFaltaDesde_ > 1'000'000'000ULL) {
        if (hayFabrica("barack")) {
            armar();
            return;
        }
        rackFaltaDesde_ = ahoraNs();
    }
    if (medio_) {
        std::uint64_t datos = 0;
        {
            std::lock_guard<std::mutex> lm(medMu_);
            datos = cuadros_ + bloques_;
        }
        // Un salto pedido con la cadena quieta: apenas la rearmada pasa datos.
        if (buscarNs_ >= 0 && datos > 0) {
            irANs(buscarNs_);
            buscarNs_ = -1;
        }
        // `looping`: vueltas sin cortes con saltos por segmentos.
        if (bucle_ && !segmentoListo_ && datos > 0) {
            gint64 pos = 0;
            gst_element_query_position(pipeline_, GST_FORMAT_TIME, &pos);
            segmentoListo_ = gst_element_seek(
                pipeline_, 1.0, GST_FORMAT_TIME,
                static_cast<GstSeekFlags>(GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_SEGMENT | GST_SEEK_FLAG_ACCURATE),
                GST_SEEK_TYPE_SET, pos, GST_SEEK_TYPE_SET, GST_CLOCK_TIME_NONE);
        }
        // Cada play después de una pausa o un salto cambia el base_time: el offset lo sigue.
        if (gst_element_get_base_time(pipeline_) != baseAplicada_) corregirOffset();
    }
    // EL BUS: todos los mensajes que juntó.
    GstBus* bus = gst_element_get_bus(pipeline_);
    if (bus == nullptr) return;
    GstElement* esta = pipeline_;
    while (pipeline_ == esta) {
        GstMessage* m = gst_bus_pop(bus);
        if (m == nullptr) break;
        procesarMensaje(m);
        gst_message_unref(m);
    }
    gst_object_unref(bus);
}

void Entrada::procesarMensaje(GstMessage* m) {
    switch (GST_MESSAGE_TYPE(m)) {
        case GST_MESSAGE_LATENCY: recalcularLatenciaAparte(pipeline_); return;
        case GST_MESSAGE_CLOCK_LOST:
            gst_element_set_state(pipeline_, GST_STATE_PAUSED);
            gst_element_set_state(pipeline_, GST_STATE_PLAYING);
            return;
        case GST_MESSAGE_ASYNC_DONE:
            if (medio_) corregirOffset();
            return;
        case GST_MESSAGE_WARNING: {
            GError* e = nullptr;
            gst_message_parse_warning(m, &e, nullptr);
            if (e != nullptr) {
                registrar(Nivel::Info, id_ + " (aviso de " + GST_OBJECT_NAME(GST_MESSAGE_SRC(m)) + "): " + e->message);
                g_error_free(e);
            }
            return;
        }
        case GST_MESSAGE_SEGMENT_DONE:
            // `looping`: otra vuelta, sin flush: sigue de corrido.
            if (medio_ && bucle_)
                gst_element_seek(pipeline_, 1.0, GST_FORMAT_TIME, GST_SEEK_FLAG_SEGMENT, GST_SEEK_TYPE_SET, 0,
                                 GST_SEEK_TYPE_SET, GST_CLOCK_TIME_NONE);
            return;
        case GST_MESSAGE_EOS:
            if (medio_) {
                if (bucle_) { // no anduvo el salto por segmentos: al principio con flush
                    irANs(0);
                    segmentoListo_ = false;
                    return;
                }
                estadoMedio_ = "ended";
                gst_element_set_state(pipeline_, GST_STATE_PAUSED);
                return;
            }
            error_ = "terminó (EOS)";
            desarmar();
            programarReintento();
            return;
        case GST_MESSAGE_ERROR: {
            GError* e = nullptr;
            gst_message_parse_error(m, &e, nullptr);
            error_ = e != nullptr ? e->message : "error";
            if (e != nullptr) g_error_free(e);
            // Al registro: el error nuevo enseguida; el mismo, a lo sumo una vez por minuto.
            const std::uint64_t ahora = ahoraNs();
            if (error_ != errorAnotado_ || ahora - errorAnotadoNs_ > 60'000'000'000ULL) {
                registrar(Nivel::Aviso, id_ + ": " + error_ + " (se reintenta" +
                                            (error_ == errorAnotado_ ? "; sigue igual)" : ")"));
                errorAnotado_ = error_;
                errorAnotadoNs_ = ahora;
            }
            if (medio_) estadoMedio_ = "error";
            desarmar();
            programarReintento();
            return;
        }
        default: return;
    }
}

bool Entrada::empujar(const std::uint8_t* bgra, int ancho, int alto, int stride) {
    std::lock_guard<std::mutex> l(mu_);
    if (!deApp_ || pipeline_ == nullptr || bgra == nullptr || ancho <= 0 || alto <= 0) return false;
    GstElement* src = gst_bin_get_by_name(GST_BIN(pipeline_), "ba_app");
    if (src == nullptr) return false;
    if (ancho != appAncho_ || alto != appAlto_) {
        GstCaps* c = gst_caps_new_simple("video/x-raw", "format", G_TYPE_STRING, "BGRA", "width", G_TYPE_INT, ancho, "height",
                                         G_TYPE_INT, alto, "framerate", GST_TYPE_FRACTION, 0, 1, "pixel-aspect-ratio",
                                         GST_TYPE_FRACTION, 1, 1, "interlace-mode", G_TYPE_STRING, "progressive", nullptr);
        // (sin pixel-aspect-ratio ni interlace-mode, imagefreeze no negociaba con la normalización)
        g_object_set(src, "caps", c, nullptr);
        gst_caps_unref(c);
        appAncho_ = ancho;
        appAlto_ = alto;
    }
    const std::size_t fila = static_cast<std::size_t>(ancho) * 4;
    GstBuffer* b = gst_buffer_new_allocate(nullptr, fila * static_cast<std::size_t>(alto), nullptr);
    GstMapInfo m;
    if (gst_buffer_map(b, &m, GST_MAP_WRITE)) {
        for (int y = 0; y < alto; ++y)
            std::memcpy(m.data + fila * static_cast<std::size_t>(y), bgra + static_cast<std::size_t>(stride) * static_cast<std::size_t>(y), fila);
        gst_buffer_unmap(b, &m);
    }
    GstFlowReturn r = GST_FLOW_OK;
    g_signal_emit_by_name(src, "push-buffer", b, &r);
    gst_buffer_unref(b);
    gst_object_unref(src);
    return r == GST_FLOW_OK;
}

// --- Medios ---------------------------------------------------------------------------

void Entrada::irANs(std::int64_t ns) {
    if (pipeline_ == nullptr) return;
    gst_element_seek_simple(pipeline_, GST_FORMAT_TIME, static_cast<GstSeekFlags>(GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_ACCURATE),
                            std::max<std::int64_t>(0, ns));
}

std::string Entrada::controlMedio(const std::string& accion) {
    std::lock_guard<std::mutex> l(mu_);
    if (!medio_) return "no es un medio";
    if (accion == "play" || accion == "pausa") {
        const bool pausar = accion == "pausa";
        if (pipeline_ == nullptr) {
            if (!pausar) armar();
            return error_;
        }
        if (!pausar && estadoMedio_ == "ended") irANs(0);
        gst_element_set_state(pipeline_, pausar ? GST_STATE_PAUSED : GST_STATE_PLAYING);
        estadoMedio_ = pausar ? "paused" : "playing";
        return {};
    }
    if (accion == "reiniciar") {
        if (pipeline_ == nullptr) {
            armar();
            return error_;
        }
        irANs(0);
        segmentoListo_ = false; // el salto con flush deshizo el segmento de `looping`
        gst_element_set_state(pipeline_, GST_STATE_PLAYING);
        estadoMedio_ = "playing";
        return {};
    }
    if (accion == "parar") {
        if (pipeline_ != nullptr) {
            gst_element_set_state(pipeline_, GST_STATE_PAUSED);
            irANs(0);
        }
        estadoMedio_ = "stopped";
        return {};
    }
    return "acción: play, pausa, reiniciar o parar";
}

void Entrada::irA(std::int64_t ms) {
    std::lock_guard<std::mutex> l(mu_);
    const std::int64_t ns = std::max<std::int64_t>(0, ms) * static_cast<std::int64_t>(GST_MSECOND);
    if (pipeline_ == nullptr) {
        armar();
        buscarNs_ = ns;
        return;
    }
    irANs(ns);
    segmentoListo_ = false;
    if (estadoMedio_ == "ended" || estadoMedio_ == "stopped") {
        gst_element_set_state(pipeline_, GST_STATE_PLAYING);
        estadoMedio_ = "playing";
    }
}

std::string Entrada::estadoMedio() const {
    std::lock_guard<std::mutex> l(mu_);
    if (pipeline_ == nullptr && !error_.empty()) return "error";
    if (pipeline_ == nullptr && estadoMedio_ != "stopped") return "none";
    return estadoMedio_;
}

std::int64_t Entrada::tiempoMs() const {
    std::lock_guard<std::mutex> l(mu_);
    if (buscarNs_ >= 0) return buscarNs_ / static_cast<std::int64_t>(GST_MSECOND);
    gint64 pos = 0;
    if (pipeline_ == nullptr || !gst_element_query_position(pipeline_, GST_FORMAT_TIME, &pos)) return 0;
    return pos / static_cast<gint64>(GST_MSECOND);
}

std::int64_t Entrada::duracionMs() const {
    std::lock_guard<std::mutex> l(mu_);
    gint64 dur = 0;
    if (pipeline_ == nullptr || !gst_element_query_duration(pipeline_, GST_FORMAT_TIME, &dur)) return 0;
    return dur / static_cast<gint64>(GST_MSECOND);
}

json Entrada::estado() const {
    std::lock_guard<std::mutex> l(mu_);
    json j{{"id", id_},
           {"nombre", nombre_.empty() ? id_ : nombre_},
           {"anda", pipeline_ != nullptr},
           {"error", error_},
           {"avisos", avisos_},
           {"esperando_dispositivo", esperaDispositivo_},
           {"video", daVideo()},
           {"audio", daAudio()},
           {"estudio", estudio_},
           {"llega_tarde", llegaTarde_.load()},
           {"latencia_ms", static_cast<double>(pide_.load()) / 1e6},
           {"declarada_ms", declaradoMs_},
           {"desfase_ms", desfaseMs_},
           {"desfase_audio_ms", desfaseAudioMs_},
           {"desfase_video_ms", desfaseVideoMs_},
           {"placa", conPlaca_},
           {"placa_armada", salidaV_ != nullptr},
           {"en_placa", enPlacaSinLock()}};
    {
        std::lock_guard<std::mutex> lm(medMu_);
        j["cuadros"] = cuadros_;
        j["bloques_audio"] = bloques_;
        j["edad_p50_ms"] = edadP50_;
        j["edad_p95_ms"] = edadP95_;
        j["origen"] = origen_;
        j["sale"] = sale_;
        j["sin_datos_s"] = ultimoDatoNs_ == 0 ? -1.0 : static_cast<double>(ahoraNs() - ultimoDatoNs_) / 1e9;
    }
    if (medio_) {
        j["medio"] = estadoMedio_;
        j["bucle"] = bucle_;
        j["limpiar_al_terminar"] = limpiarAlTerminar_;
    }
    j["normalizado"] = normalizar_;
    j["alfa"] = alfa_;
    j["ntp_sync"] = ntpSync_ && !ntpApagadoSolo_;
    j["filtros"] = estadoFiltros();
    return j;
}

void Entrada::control(const json& campos) {
    std::lock_guard<std::mutex> l(mu_);
    if (!campos.is_object()) return;
    for (auto it = campos.begin(); it != campos.end(); ++it) control_[it.key()] = *it;
    controlEnviado_ = enviarControl();
}

bool Entrada::enviarControl() {
    if (pipeline_ == nullptr || control_.empty()) return false;
    GstStructure* st = gst_structure_new_empty("ba/control");
    for (auto it = control_.begin(); it != control_.end(); ++it) {
        const json& v = *it;
        if (v.is_string())
            gst_structure_set(st, it.key().c_str(), G_TYPE_STRING, v.get<std::string>().c_str(), nullptr);
        else if (v.is_boolean())
            gst_structure_set(st, it.key().c_str(), G_TYPE_BOOLEAN, static_cast<gboolean>(v.get<bool>()), nullptr);
        else if (v.is_number_integer())
            gst_structure_set(st, it.key().c_str(), G_TYPE_INT, v.get<int>(), nullptr);
        else if (v.is_number())
            gst_structure_set(st, it.key().c_str(), G_TYPE_DOUBLE, v.get<double>(), nullptr);
    }
    bool ok = false;
    // Entra por la cola del origen y recorre toda la cadena.
    if (GstElement* q = gst_bin_get_by_name(GST_BIN(pipeline_), "ba_origen")) {
        if (GstPad* pad = gst_element_get_static_pad(q, "sink")) {
            ok = gst_pad_send_event(pad, gst_event_new_custom(GST_EVENT_CUSTOM_DOWNSTREAM_OOB, gst_structure_copy(st)));
            gst_object_unref(pad);
        }
        gst_object_unref(q);
    }
    gst_structure_free(st);
    return ok;
}

json Entrada::estadoFiltros() const {
    // Cada filtro: su elemento y lo que informe (lo que tarda, el motor, por qué no anda).
    json fs = json::array();
    for (const auto* l : {&filtrosV_, &filtrosA_})
        for (const auto& fl : *l) {
            json e{{"id", fl.id}, {"elemento", fl.elemento}, {"audio", l == &filtrosA_}};
            const std::string n = (l == &filtrosV_ ? "ba_f_" : "ba_fa_") + fl.nombre;
            GstElement* el = pipeline_ != nullptr ? gst_bin_get_by_name(GST_BIN(pipeline_), n.c_str()) : nullptr;
            e["andando"] = el != nullptr;
            if (el == nullptr) {
                fs.push_back(e);
                continue;
            }
            // Lo que informa (sus propiedades de solo lectura), sea el elemento que sea.
            GObjectClass* k = G_OBJECT_GET_CLASS(el);
            guint np = 0;
            GParamSpec** pss = g_object_class_list_properties(k, &np);
            json info = json::object();
            for (guint i = 0; i < np; ++i) {
                GParamSpec* ps = pss[i];
                if ((ps->flags & G_PARAM_READABLE) == 0) continue;
                if (ps->owner_type == GST_TYPE_ELEMENT || ps->owner_type == GST_TYPE_OBJECT || ps->owner_type == G_TYPE_OBJECT)
                    continue;
                const bool soloLectura = (ps->flags & G_PARAM_WRITABLE) == 0;
                if (!soloLectura && std::string(ps->name) != "ms-por-cuadro") continue;
                GValue v = G_VALUE_INIT;
                g_value_init(&v, ps->value_type);
                g_object_get_property(G_OBJECT(el), ps->name, &v);
                if (G_VALUE_HOLDS_STRING(&v)) {
                    const char* t = g_value_get_string(&v);
                    if (t != nullptr && *t != '\0') info[ps->name] = t;
                } else if (G_VALUE_HOLDS_DOUBLE(&v)) {
                    info[ps->name] = g_value_get_double(&v);
                } else if (G_VALUE_HOLDS_INT(&v)) {
                    info[ps->name] = g_value_get_int(&v);
                } else if (G_VALUE_HOLDS_BOOLEAN(&v)) {
                    info[ps->name] = static_cast<bool>(g_value_get_boolean(&v));
                }
                g_value_unset(&v);
            }
            g_free(pss);
            if (info.contains("ms-por-cuadro")) e["ms"] = info["ms-por-cuadro"];
            for (const char* prop : {"error", "motor-usado", "proveedor-usado", "unidades-usadas"})
                if (info.contains(prop)) e[prop] = info[prop];
            e["info"] = info;
            // Y lo que negoció: el formato que sale.
            if (GstPad* pad = gst_element_get_static_pad(el, "src")) {
                if (GstCaps* c = gst_pad_get_current_caps(pad)) {
                    gchar* t = gst_caps_to_string(c);
                    e["caps"] = t;
                    g_free(t);
                    gst_caps_unref(c);
                }
                gst_object_unref(pad);
            }
            gst_object_unref(el);
            fs.push_back(e);
        }
    return fs;
}

std::string Entrada::propiedad(const std::string& elemento, const std::string& prop, const std::string& valor) {
    std::lock_guard<std::mutex> l(mu_);
    if (pipeline_ == nullptr) return "la entrada no está andando";
    GstElement* el = gst_bin_get_by_name(GST_BIN(pipeline_), elemento.c_str());
    if (el == nullptr) return "no hay un elemento «" + elemento + "» en la entrada";
    std::string err;
    if (g_object_class_find_property(G_OBJECT_GET_CLASS(el), prop.c_str()) == nullptr)
        err = "«" + elemento + "» no tiene la propiedad «" + prop + "»";
    else
        gst_util_set_object_arg(G_OBJECT(el), prop.c_str(), valor.c_str());
    gst_object_unref(el);
    return err;
}

json Entrada::leerPropiedades(const std::string& elemento) const {
    std::lock_guard<std::mutex> l(mu_);
    json o = json::object();
    if (pipeline_ == nullptr) return o;
    GstElement* el = gst_bin_get_by_name(GST_BIN(pipeline_), elemento.c_str());
    if (el == nullptr) return o;
    guint np = 0;
    GParamSpec** pss = g_object_class_list_properties(G_OBJECT_GET_CLASS(el), &np);
    for (guint i = 0; i < np; ++i) {
        GParamSpec* ps = pss[i];
        if ((ps->flags & G_PARAM_READABLE) == 0) continue;
        if (ps->owner_type == GST_TYPE_ELEMENT || ps->owner_type == GST_TYPE_OBJECT || ps->owner_type == G_TYPE_OBJECT)
            continue;
        GValue v = G_VALUE_INIT;
        g_value_init(&v, ps->value_type);
        g_object_get_property(G_OBJECT(el), ps->name, &v);
        if (G_VALUE_HOLDS_STRING(&v)) {
            const char* t = g_value_get_string(&v);
            o[ps->name] = t != nullptr ? t : "";
        } else if (G_VALUE_HOLDS_DOUBLE(&v)) {
            o[ps->name] = g_value_get_double(&v);
        } else if (G_VALUE_HOLDS_INT(&v)) {
            o[ps->name] = g_value_get_int(&v);
        } else if (G_VALUE_HOLDS_BOOLEAN(&v)) {
            o[ps->name] = static_cast<bool>(g_value_get_boolean(&v));
        }
        g_value_unset(&v);
    }
    g_free(pss);
    gst_object_unref(el);
    return o;
}

} // namespace ba::motor
