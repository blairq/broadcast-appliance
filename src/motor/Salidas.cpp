// ============================================================================
// src/motor/Salidas.cpp — ver Salidas.hpp
// ============================================================================
#include "Salidas.hpp"
#include "diag/Recursos.hpp"
#include "diag/Utf8.hpp"
#include "i18n/I18n.hpp"

#include "Fallas.hpp"
#include "Piezas.hpp"
#include "Reloj.hpp"

#include <gst/video/video.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <map>

#include "ajustes/Ajustes.hpp"
#include <atomic>
#include <filesystem>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

namespace ba::motor {

using ba::i18n::llenar;
using ba::i18n::traducir;

namespace {
std::atomic<int> g_reintentoTransmisionMaxS{10};
/// 1, 2, 4, 8… s hasta el tope (Configuración → Transmisión). Un solo lugar: estaba copiado dos veces.
std::int64_t esperaReconexionNs(int intentos) {
    const int tope = g_reintentoTransmisionMaxS.load();
    int s = 1;
    for (int i = 1; i < intentos && s < tope; ++i) s *= 2;
    return static_cast<std::int64_t>(std::min(s, tope)) * static_cast<std::int64_t>(GST_SECOND);
}
/// La espera antes de rearmar algo que se cortó solo (una grabación, un codificador):
/// 0,5-1-2-5 s y después cada 5 s. Corta al principio: casi siempre vuelve enseguida.
std::int64_t esperaRearmeNs(int intentos) {
    static constexpr std::int64_t kMs[] = {500, 1000, 2000, 5000};
    return kMs[std::clamp(intentos, 0, 3)] * 1'000'000LL;
}
} // namespace

void ponerReintentoTransmision(int maxS) { g_reintentoTransmisionMaxS = std::clamp(maxS, 1, 300); }

using json = nlohmann::json;

namespace {

std::int64_t ahoraNs() { return g_get_monotonic_time() * 1000; }

const std::vector<std::string>& codificadoresDeLaPlaca() {
#if defined(__APPLE__)
    static const std::vector<std::string> v{"vtenc_h264_hw", "vtenc_h264"};
#elif defined(_WIN32)
    static const std::vector<std::string> v{"nvh264enc", "qsvh264enc", "d3d12h264enc", "amfh264enc", "mfh264enc"};
#else
    static const std::vector<std::string> v{"nvh264enc", "qsvh264enc", "vah264enc", "vah264lpenc"};
#endif
    return v;
}

bool tiene(GstElement* e, const char* p) { return g_object_class_find_property(G_OBJECT_GET_CLASS(e), p) != nullptr; }

/// Pone un número en la propiedad si la tiene (sea del tipo numérico que sea, dentro de su rango).
bool ponerNumero(GstElement* e, const char* prop, double v) {
    GParamSpec* ps = g_object_class_find_property(G_OBJECT_GET_CLASS(e), prop);
    if (ps == nullptr || (ps->flags & G_PARAM_WRITABLE) == 0) return false;
    const auto en = [v](double lo, double hi) { return std::clamp(v, lo, hi); };
    switch (G_PARAM_SPEC_VALUE_TYPE(ps)) {
        case G_TYPE_UINT: {
            auto* p = G_PARAM_SPEC_UINT(ps);
            g_object_set(e, prop, static_cast<guint>(en(p->minimum, p->maximum)), nullptr);
            return true;
        }
        case G_TYPE_INT: {
            auto* p = G_PARAM_SPEC_INT(ps);
            g_object_set(e, prop, static_cast<gint>(en(p->minimum, p->maximum)), nullptr);
            return true;
        }
        case G_TYPE_UINT64: {
            auto* p = G_PARAM_SPEC_UINT64(ps);
            g_object_set(e, prop, static_cast<guint64>(en(static_cast<double>(p->minimum), static_cast<double>(p->maximum))), nullptr);
            return true;
        }
        case G_TYPE_DOUBLE: {
            auto* p = G_PARAM_SPEC_DOUBLE(ps);
            g_object_set(e, prop, en(p->minimum, p->maximum), nullptr);
            return true;
        }
        case G_TYPE_FLOAT: {
            auto* p = G_PARAM_SPEC_FLOAT(ps);
            g_object_set(e, prop, static_cast<gfloat>(en(p->minimum, p->maximum)), nullptr);
            return true;
        }
        default:
            return false;
    }
}

/// Pone en la primera de `props` que tenga el elemento (un enum) el primer valor de
/// `nicks` que exista. Cada familia de codificadores llama distinto a lo mismo
/// (rate-control, rc-mode, pass; cbr, vbr, abr, qvbr, icq, cqp…). true si pudo.
bool ponerEnum(GstElement* e, std::initializer_list<const char*> props, std::initializer_list<const char*> nicks) {
    for (const char* prop : props) {
        GParamSpec* ps = g_object_class_find_property(G_OBJECT_GET_CLASS(e), prop);
        if (ps == nullptr || !G_IS_PARAM_SPEC_ENUM(ps)) continue;
        GEnumClass* clase = G_PARAM_SPEC_ENUM(ps)->enum_class;
        for (const char* n : nicks)
            if (GEnumValue* v = g_enum_get_value_by_nick(clase, n)) {
                g_object_set(e, prop, v->value, nullptr);
                return true;
            }
        return false;
    }
    return false;
}

/// El control de tasa y la calidad, con los nombres de cada familia (lo que no
/// tiene el elemento, no se pone). El bitrate ya está puesto.
void ponerControl(GstElement* e, const std::string& fabrica, const PerfilVideo& p) {
    const double kbps = std::max(100, p.kbps);
    const auto modo = [e](std::initializer_list<const char*> nicks) { return ponerEnum(e, {"rate-control", "rc-mode", "pass"}, nicks); };
    const auto vbv = [e](double maximo, double buffer) {
        g_object_set(e, "option-string",
                     ("vbv-maxrate=" + std::to_string(static_cast<int>(maximo)) + ":vbv-bufsize=" + std::to_string(static_cast<int>(buffer))).c_str(),
                     nullptr);
    };
    switch (p.control) {
        case ControlTasa::CBR:
            modo({"cbr"});
            break;
        case ControlTasa::VBR: {
            const double tope = kbps * 1.5;
            if (fabrica == "x264enc") {
                // x264 no tiene en el elemento un VBR de una pasada: calidad con tope (VBV).
                ponerEnum(e, {"pass"}, {"qual"});
                ponerNumero(e, "quantizer", 21);
                vbv(tope, tope);
                break;
            }
            modo({"vbr", "abr"}); // VideoToolbox: abr
            ponerNumero(e, "max-bitrate", tope);
            if (tiene(e, "data-rate-limits")) g_object_set(e, "data-rate-limits", (std::to_string(static_cast<int>(tope)) + ",1").c_str(), nullptr);
            break;
        }
        case ControlTasa::Calidad: {
            const int crf = std::clamp(p.crf, 10, 35);
            if (fabrica == "x264enc") {
                ponerEnum(e, {"pass"}, {"qual"});
                ponerNumero(e, "quantizer", crf);
                vbv(kbps, kbps * 2);
                break;
            }
            if (tiene(e, "quality") && tiene(e, "data-rate-limits")) {
                // VideoToolbox: calidad 0…1 (con bitrate 0 manda la calidad) y el tope aparte.
                // CRF 14 → 0,87; 18 → 0,73; 23 → 0,57; 28 → 0,40.
                ponerNumero(e, "bitrate", 0);
                ponerNumero(e, "quality", std::clamp(1.0 - (crf - 10) / 30.0, 0.1, 1.0));
                g_object_set(e, "data-rate-limits", (std::to_string(static_cast<int>(kbps)) + ",1").c_str(), nullptr);
                break;
            }
            // NVENC, QSV, D3D12, AMF, MF: el modo de calidad que tenga (con tope si puede) y
            // el número de calidad en la propiedad que use.
            modo({"qvbr", "icq", "cqp", "constqp", "quality"});
            for (const char* q : {"qvbr-quality", "icq-quality", "const-quality", "qp-const", "qp-i", "qp-p", "qp-b", "qp"}) ponerNumero(e, q, crf);
            ponerNumero(e, "max-bitrate", kbps);
            break;
        }
    }
}

// --- La lista negra de codificadores (F1-06) ------------------------------------------------

std::mutex g_negraMu;
std::map<std::string, std::set<std::string>> g_negra; ///< fuente de video (la de una salida) → fábricas que fallaron

void ponerEnListaNegra(const std::string& fuente, const std::string& fabrica) {
    std::lock_guard<std::mutex> l(g_negraMu);
    g_negra[fuente].insert(fabrica);
}

/// El speed-preset de x264 según la CPU libre (medida a lo sumo cada 10 s): x264 es el
/// último recurso, y en una máquina cargada un preset caro se come los cuadros del estudio.
const char* presetSegunCpu() {
    static std::mutex mu;
    static std::unique_ptr<ba::diag::Recursos> recursos;
    static std::int64_t medidoNs = 0;
    static double libre = -1;
    std::lock_guard<std::mutex> l(mu);
    if (medidoNs == 0 || ahoraNs() - medidoNs > 10'000'000'000LL) {
        if (!recursos) recursos = std::make_unique<ba::diag::Recursos>(1);
        recursos->medir(); // la primera vuelta no tiene con qué comparar
        g_usleep(250 * 1000);
        const double cpu = recursos->medir().cpu;
        libre = cpu >= 0 ? 100.0 - cpu : -1;
        medidoNs = ahoraNs();
    }
    return libre < 0 || libre >= 50 ? "veryfast" : libre >= 25 ? "superfast" : "ultrafast";
}

} // namespace

ControlTasa controlTasaDe(const std::string& n) {
    std::string m = n;
    std::transform(m.begin(), m.end(), m.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    if (m == "VBR") return ControlTasa::VBR;
    if (m == "CQP" || m == "CRF" || m == "CALIDAD") return ControlTasa::Calidad;
    return ControlTasa::CBR;
}

const char* nombreControlTasa(ControlTasa c) { return c == ControlTasa::VBR ? "VBR" : c == ControlTasa::Calidad ? "calidad" : "CBR"; }

std::int64_t margenSalidaNs(TipoSalida t) {
    switch (t) {
        case TipoSalida::Srt: return ajustes::msANs("salidas.margenSrtMs", 80);
        case TipoSalida::Rtmp: return ajustes::msANs("salidas.margenRtmpMs", 300);
        case TipoSalida::Grabacion: return ajustes::msANs("salidas.margenGrabacionMs", 500);
    }
    return 300'000'000LL;
}

std::set<std::string> fabricasCaidas(const std::string& fuente) {
    std::lock_guard<std::mutex> l(g_negraMu);
    const auto it = g_negra.find(fuente);
    return it == g_negra.end() ? std::set<std::string>{} : it->second;
}

void olvidarFabricasCaidas() {
    std::lock_guard<std::mutex> l(g_negraMu);
    g_negra.clear();
}

std::string elegirCodificador(const std::vector<std::string>& preferidos, const std::set<std::string>& salvo) {
    const auto& lista = preferidos.empty() ? codificadoresDeLaPlaca() : preferidos;
    const auto sirve = [&](const std::string& f) { return salvo.count(f) == 0 && hayFabrica(f); };
    // Con el video en D3D12, primero los que reciben D3D12Memory (sin bajar cada
    // cuadro): en AMD d3d12h264enc antes que AMF (que pide D3D11), en NVIDIA
    // d3d12h264enc antes que NVENC por CUDA. Dentro de la lista pedida (si el
    // usuario eligió uno solo, ese).
    if (gpu() == Gpu::D3D12)
        for (const auto& f : lista)
            if (sirve(f) && aceptaMemoria(f, "D3D12Memory")) return f;
    for (const auto& f : lista)
        if (sirve(f)) return f;
    // El último recurso no va nunca a la lista negra: sin él no queda nada.
    return hayFabrica("x264enc") ? "x264enc" : std::string();
}

GstElement* crearCodificador(const std::string& fabrica, const PerfilVideo& p) {
    GstElement* e = gst_element_factory_make(fabrica.c_str(), "codificador");
    if (e == nullptr) return nullptr;
    // nvh264enc/qsv/vt/x264: kbps; algunos (d3d12) también en kbps.
    ponerNumero(e, "bitrate", std::max(100, p.kbps));
    const auto g = std::max(1, p.gop);
    if (tiene(e, "max-keyframe-interval")) g_object_set(e, "max-keyframe-interval", g, nullptr); // vtenc
    if (tiene(e, "key-int-max")) g_object_set(e, "key-int-max", static_cast<guint>(g), nullptr); // x264
    if (tiene(e, "gop-size")) g_object_set(e, "gop-size", g, nullptr);                           // nv, qsv, d3d12
    if (tiene(e, "realtime")) g_object_set(e, "realtime", TRUE, nullptr);
    if (tiene(e, "allow-frame-reordering")) g_object_set(e, "allow-frame-reordering", FALSE, nullptr);
    if (fabrica == "x264enc") {
        gst_util_set_object_arg(G_OBJECT(e), "tune", "zerolatency");
        gst_util_set_object_arg(G_OBJECT(e), "speed-preset", "veryfast");
    }
    if (tiene(e, "zerolatency")) g_object_set(e, "zerolatency", TRUE, nullptr); // nvh264enc
    ponerControl(e, fabrica, p);
    return e;
}

// --- Codificación compartida (F1-06, F1-07) -------------------------------------------------
//
// Un pipeline que lee lo crudo de una fuente (el video o un bus de una salida),
// lo codifica y lo publica en un intersink propio («cod:N»). Las grabaciones y
// transmisiones leen de ahí: el intersink es el `tee` (cada una en su pipeline,
// así una que se cae no frena a las demás). Si el codificador da error o deja de
// sacar cuadros, la fábrica va a la lista negra de la fuente y se rearma con la
// siguiente; la generación sube y quien lo lee se rearma (caps nuevas: el
// muxer no acepta otras a mitad de archivo). Lo atiende el tic de cada salida
// que lo lee (varias veces por vuelta está bien).

class Codificacion {
   public:
    struct Config {
        std::string fuente;  ///< el productor de lo crudo
        bool video = true;
        PerfilVideo perfil;  ///< (video)
        int kbpsAudio = 160; ///< (audio)
    };
    Codificacion(Reloj& reloj, Config cfg) : reloj_(reloj), cfg_(std::move(cfg)) {
        static std::atomic<int> n{0};
        productor_ = "cod:" + std::to_string(++n);
    }
    ~Codificacion() {
        std::lock_guard<std::mutex> l(mu_);
        desarmar();
    }
    Codificacion(const Codificacion&) = delete;
    Codificacion& operator=(const Codificacion&) = delete;

    std::string iniciar() {
        std::lock_guard<std::mutex> l(mu_);
        error_ = armar();
        return error_;
    }
    const std::string& productor() const { return productor_; }
    /// El banco de caos (--test): la falla del codificador va a este pipeline, con el candado tomado.
    void aplicarFallas(GstElement* salida, const std::string& cual) {
        std::lock_guard<std::mutex> l(mu_);
        aplicarFallasProgramadas(salida, cual, pipeline_);
    }
    std::uint64_t generacion() const { return generacion_.load(); }
    std::uint64_t perdidos() const { return perdidos_.load(); }
    std::string fabrica() const {
        std::lock_guard<std::mutex> l(mu_);
        return fabrica_;
    }
    /// Que el próximo cuadro sea clave (alguien empezó a leer: un archivo nuevo, una reconexión).
    void pedirClave() {
        std::lock_guard<std::mutex> l(mu_);
        if (cod_ == nullptr) return;
        if (GstPad* sp = gst_element_get_static_pad(cod_, "src")) {
            gst_pad_send_event(sp, gst_video_event_new_upstream_force_key_unit(GST_CLOCK_TIME_NONE, TRUE, 0));
            gst_object_unref(sp);
        }
    }
    /// Lo que tarda el codificador (lo que dice su latencia; si todavía no la sabe, dos cuadros).
    std::int64_t latenciaNs() {
        std::lock_guard<std::mutex> l(mu_);
        if (latNs_ < 0 && cod_ != nullptr)
            if (GstPad* sp = gst_element_get_static_pad(cod_, "src")) {
                GstQuery* q = gst_query_new_latency();
                gboolean vivo = FALSE;
                GstClockTime min = 0, max = 0;
                if (gst_pad_query(sp, q)) {
                    gst_query_parse_latency(q, &vivo, &min, &max);
                    if (GST_CLOCK_TIME_IS_VALID(min)) latNs_ = static_cast<std::int64_t>(min);
                }
                gst_query_unref(q);
                gst_object_unref(sp);
            }
        return latNs_ >= 0 ? latNs_ : 66'000'000LL;
    }
    json estado() const {
        std::lock_guard<std::mutex> l(mu_);
        json j{{"productor", productor_}, {"fuente", cfg_.fuente},     {"tipo", cfg_.video ? "video" : "audio"},
               {"fabrica", fabrica_},     {"generacion", generacion_.load()}, {"perdidos", perdidos_.load()},
               {"error", error_},         {"andando", pipeline_ != nullptr}};
        if (cfg_.video) {
            j["kbps"] = cfg_.perfil.kbps;
            j["control"] = nombreControlTasa(cfg_.perfil.control);
        } else {
            j["kbps"] = cfg_.kbpsAudio;
        }
        return j;
    }

    void tic() {
        std::lock_guard<std::mutex> l(mu_);
        const std::int64_t ahora = ahoraNs();
        if (pipeline_ == nullptr) {
            if (ahora < reintentoNs_) return;
            error_ = armar();
            if (!error_.empty()) reintentoNs_ = ahora + esperaRearmeNs(++intentos_);
            return;
        }
        bool falla = false, deFabrica = false;
        GstBus* bus = gst_element_get_bus(pipeline_);
        while (GstMessage* m = gst_bus_pop(bus)) {
            if (GST_MESSAGE_TYPE(m) == GST_MESSAGE_ERROR) {
                GError* e = nullptr;
                gst_message_parse_error(m, &e, nullptr);
                error_ = e != nullptr ? ba::diag::utf8Valido(e->message) : traducir("motor", "error");
                if (e != nullptr) g_error_free(e);
                GstObject* src = GST_MESSAGE_SRC(m);
                deFabrica = deFabrica || (cod_ != nullptr && (src == GST_OBJECT(cod_) || gst_object_has_as_ancestor(src, GST_OBJECT(cod_))));
                falla = true;
            }
            gst_message_unref(m);
        }
        gst_object_unref(bus);
        // El vigía: entran cuadros y el codificador no saca nada (una sesión de la placa trabada).
        if (!falla && cfg_.video) {
            const std::uint64_t entran = entran_.load(), salen = salen_.load();
            if (salen != salenAntes_) {
                sinSalirDesdeNs_ = 0;
                salenAntes_ = salen;
            } else if (entran != entranAntes_) {
                if (sinSalirDesdeNs_ == 0) sinSalirDesdeNs_ = ahora;
                if (ahora - sinSalirDesdeNs_ > ajustes::sANs("encoder.trabadoS", 3)) {
                    error_ = traducir("motor", "el codificador dejó de sacar cuadros");
                    falla = deFabrica = true;
                }
            }
            entranAntes_ = entran;
        }
        if (!falla) return;
        const std::string cayo = fabrica_;
        desarmar();
        if (deFabrica && cayo != "x264enc") {
            const std::string porQue = error_;
            ponerEnListaNegra(cfg_.fuente, cayo);
            intentos_ = 0;
            error_ = armar();
            registrar(Nivel::Aviso, llenar(traducir("motor", "codificador %1 falló en %2 (%3): sigue con %4"),
                                           {cayo, cfg_.fuente, porQue, error_.empty() ? fabrica_ : error_}));
            if (!error_.empty()) reintentoNs_ = ahora + esperaRearmeNs(++intentos_);
            return;
        }
        registrar(Nivel::Aviso, llenar(traducir("motor", "codificador de %1: %2 (se rearma)"), {cfg_.fuente, error_}));
        reintentoNs_ = ahora + esperaRearmeNs(intentos_++);
    }

   private:
    /// Arma el pipeline (con mu_). Con video, prueba las fábricas en orden: la que no
    /// arranca va a la lista negra y sigue con la próxima.
    std::string armar() {
        desarmar();
        // La cola cruda delante del codificador: si no da abasto, tira lo viejo en vez de
        // atrasar (y lo cuenta). La misma política para video y audio y para todas las
        // salidas que lo comparten (F2-16): stream.colaMs, o la de la grabación si es más larga.
        const std::int64_t colaNs = std::max<std::int64_t>(ajustes::msANs("stream.colaMs", 1000), ajustes::sANs("record.colaEncoderS", 1));
        const std::string cola = "queue name=cola leaky=downstream max-size-time=" + std::to_string(colaNs) + " max-size-buffers=0 max-size-bytes=0 ! ";
        const std::string sale = " ! intersink name=sale producer-name=" + entreComillas(productor_) + " sync=false event-types=\"<>\"";
        if (!cfg_.video) {
            const std::string d = "intersrc producer-name=" + entreComillas(cfg_.fuente) + " ! " + cola + "audioconvert ! avenc_aac bitrate=" +
                                  std::to_string(std::max(64, cfg_.kbpsAudio) * 1000) + " ! aacparse" + sale;
            GstElement* p = nullptr;
            if (std::string e = lanzar(d, &p); !e.empty()) return e;
            return arrancar(p, "avenc_aac");
        }
        for (;;) {
            const std::string fab = elegirCodificador(cfg_.perfil.codificadores, fabricasCaidas(cfg_.fuente));
            if (fab.empty()) return traducir("motor", "no hay codificador de video");
            const std::string d = "intersrc producer-name=" + entreComillas(cfg_.fuente) + " ! " + cola + aCodificador(fab) +
                                  " ! identity name=antes_cod h264parse name=parse config-interval=-1 ! "
                                  "video/x-h264,stream-format=byte-stream,alignment=au" +
                                  sale;
            GstElement* p = nullptr;
            std::string e = lanzar(d, &p);
            if (!e.empty()) return e;
            GstElement* cod = crearCodificador(fab, cfg_.perfil);
            if (cod != nullptr && fab == "x264enc") gst_util_set_object_arg(G_OBJECT(cod), "speed-preset", presetSegunCpu());
            GstElement* antes = gst_bin_get_by_name(GST_BIN(p), "antes_cod");
            GstElement* parse = gst_bin_get_by_name(GST_BIN(p), "parse");
            bool ok = cod != nullptr && antes != nullptr && parse != nullptr;
            if (ok) {
                gst_bin_add(GST_BIN(p), cod);
                ok = gst_element_link_many(antes, cod, parse, nullptr);
            } else if (cod != nullptr) {
                gst_object_unref(cod);
            }
            if (antes != nullptr) gst_object_unref(antes);
            if (parse != nullptr) gst_object_unref(parse);
            if (ok) {
                cod_ = GST_ELEMENT(gst_object_ref(cod));
                // Lo que entra y lo que sale del codificador (el vigía de tic).
                if (GstElement* c = gst_bin_get_by_name(GST_BIN(p), "cola")) {
                    GstPad* sp = gst_element_get_static_pad(c, "src");
                    gst_pad_add_probe(sp, GST_PAD_PROBE_TYPE_BUFFER,
                                      +[](GstPad*, GstPadProbeInfo*, gpointer d) -> GstPadProbeReturn {
                                          ++static_cast<Codificacion*>(d)->entran_;
                                          return GST_PAD_PROBE_OK;
                                      },
                                      this, nullptr);
                    gst_object_unref(sp);
                    gst_object_unref(c);
                }
                GstPad* sp = gst_element_get_static_pad(cod, "src");
                gst_pad_add_probe(sp, GST_PAD_PROBE_TYPE_BUFFER,
                                  +[](GstPad*, GstPadProbeInfo*, gpointer d) -> GstPadProbeReturn {
                                      ++static_cast<Codificacion*>(d)->salen_;
                                      return GST_PAD_PROBE_OK;
                                  },
                                  this, nullptr);
                gst_object_unref(sp);
                e = arrancar(p, fab);
                if (e.empty()) return {};
            } else {
                gst_object_unref(p);
                e = llenar(traducir("motor", "no se pudo enganchar el codificador %1"), {fab});
            }
            // No arrancó: a la lista negra y la próxima (x264enc, el último, no: dice el error).
            if (fab == "x264enc") return e;
            ponerEnListaNegra(cfg_.fuente, fab);
            registrar(Nivel::Aviso, llenar(traducir("motor", "codificador %1 no arrancó en %2 (%3): se prueba otro"), {fab, cfg_.fuente, e}));
        }
    }
    static std::string lanzar(const std::string& d, GstElement** p) {
        GError* e = nullptr;
        *p = gst_parse_launch(d.c_str(), &e);
        if (e == nullptr) return {};
        std::string m = ba::diag::utf8Valido(e->message);
        g_error_free(e);
        if (*p != nullptr) gst_object_unref(*p);
        *p = nullptr;
        return m;
    }
    std::string arrancar(GstElement* p, const std::string& fab) {
        if (GstElement* c = gst_bin_get_by_name(GST_BIN(p), "cola")) {
            g_signal_connect(c, "overrun", G_CALLBACK(+[](GstElement*, gpointer d) { ++static_cast<Codificacion*>(d)->perdidos_; }), this);
            gst_object_unref(c);
        }
        sinRelojPropio(p);
        reloj_.adoptar(p);
        if (gst_element_set_state(p, GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE) {
            gst_element_set_state(p, GST_STATE_NULL);
            gst_object_unref(p);
            if (cod_ != nullptr) gst_object_unref(cod_);
            cod_ = nullptr;
            return llenar(traducir("motor", "el codificador %1 no arrancó"), {fab});
        }
        pipeline_ = p;
        fabrica_ = fab;
        latNs_ = -1;
        entran_ = salen_ = 0;
        entranAntes_ = salenAntes_ = 0;
        sinSalirDesdeNs_ = 0;
        ++generacion_;
        return {};
    }
    void desarmar() {
        if (pipeline_ != nullptr) {
            gst_element_set_state(pipeline_, GST_STATE_NULL);
            gst_object_unref(pipeline_);
            pipeline_ = nullptr;
        }
        if (cod_ != nullptr) gst_object_unref(cod_);
        cod_ = nullptr;
    }

    Reloj& reloj_;
    Config cfg_;
    std::string productor_;
    mutable std::mutex mu_;
    GstElement* pipeline_ = nullptr;
    GstElement* cod_ = nullptr; ///< el codificador (con ref): pedirle claves, saber si el error es suyo
    std::string fabrica_, error_;
    std::atomic<std::uint64_t> generacion_{0}, perdidos_{0}, entran_{0}, salen_{0};
    std::uint64_t entranAntes_ = 0, salenAntes_ = 0;
    std::int64_t sinSalirDesdeNs_ = 0, reintentoNs_ = 0, latNs_ = -1;
    int intentos_ = 0;
};

namespace {
struct Codificaciones {
    std::mutex mu;
    std::map<std::string, std::weak_ptr<Codificacion>> compartidas; ///< por clave (fuente y perfil)
    std::vector<std::weak_ptr<Codificacion>> todas;                 ///< también las propias
};
Codificaciones& codificaciones() {
    static Codificaciones c;
    return c;
}

/// La codificación de esa fuente con ese perfil: la que ya hay (compartida) o una nueva.
/// `propia`: una nueva solo para quien la pide (la copia «independiente»). nullptr y
/// `error` si no arrancó.
std::shared_ptr<Codificacion> codificacionPara(Reloj& reloj, const Codificacion::Config& c, bool propia, std::string* error) {
    std::string clave = std::to_string(reinterpret_cast<std::uintptr_t>(&reloj)) + "|" + c.fuente + "|";
    if (c.video) {
        clave += "v|" + std::to_string(c.perfil.kbps) + "|" + std::to_string(c.perfil.gop) + "|" + nombreControlTasa(c.perfil.control) + "|" +
                 (c.perfil.control == ControlTasa::Calidad ? std::to_string(c.perfil.crf) : std::string()) + "|";
        for (const auto& f : c.perfil.codificadores) clave += f + ",";
    } else {
        clave += "a|" + std::to_string(c.kbpsAudio);
    }
    auto& r = codificaciones();
    std::lock_guard<std::mutex> l(r.mu);
    if (!propia)
        if (auto it = r.compartidas.find(clave); it != r.compartidas.end())
            if (auto s = it->second.lock()) return s;
    auto s = std::make_shared<Codificacion>(reloj, c);
    if (std::string e = s->iniciar(); !e.empty()) {
        if (error != nullptr) *error = e;
        return nullptr;
    }
    if (!propia) r.compartidas[clave] = s;
    r.todas.erase(std::remove_if(r.todas.begin(), r.todas.end(), [](const auto& w) { return w.expired(); }), r.todas.end());
    r.todas.push_back(s);
    return s;
}

/// Deja pasar el video desde un cuadro clave: lo de antes no se decodifica solo (un
/// archivo nuevo, una reconexión, la vuelta de una pausa).
GstPadProbeReturn compuertaClave(GstPad*, GstPadProbeInfo* i, gpointer d) {
    auto* esperar = static_cast<std::atomic<bool>*>(d);
    if (!esperar->load()) return GST_PAD_PROBE_OK;
    GstBuffer* b = GST_PAD_PROBE_INFO_BUFFER(i);
    if (b != nullptr && GST_BUFFER_FLAG_IS_SET(b, GST_BUFFER_FLAG_DELTA_UNIT)) return GST_PAD_PROBE_DROP;
    esperar->store(false);
    return GST_PAD_PROBE_OK;
}
} // namespace

json codificacionesAbiertas() {
    auto& r = codificaciones();
    std::vector<std::shared_ptr<Codificacion>> vivas;
    {
        std::lock_guard<std::mutex> l(r.mu);
        for (const auto& w : r.todas)
            if (auto s = w.lock()) vivas.push_back(std::move(s));
    }
    json out = json::array();
    for (const auto& s : vivas) {
        json j = s->estado();
        j["consumidores"] = s.use_count() - 1; // menos la de acá
        out.push_back(std::move(j));
    }
    return out;
}

// --- Remux ---------------------------------------------------------------------------------

std::string remuxar(const std::string& origen, const std::string& destino, const std::function<void(double)>& progreso) {
    const bool mov = destino.size() > 4 && destino.substr(destino.size() - 4) == ".mov";
    const std::string d = "filesrc location=" + entreComillas(origen) + " ! matroskademux name=d " + (mov ? "qtmux" : "mp4mux") +
                          " name=mux faststart=true ! filesink location=" + entreComillas(destino);
    GError* e = nullptr;
    GstElement* p = gst_parse_launch(d.c_str(), &e);
    if (e != nullptr) {
        std::string m = ba::diag::utf8Valido(e->message);
        g_error_free(e);
        if (p != nullptr) gst_object_unref(p);
        return m;
    }
    GstElement* dmx = gst_bin_get_by_name(GST_BIN(p), "d");
    GstElement* mux = gst_bin_get_by_name(GST_BIN(p), "mux");
    struct Ctx {
        GstElement* pipeline;
        GstElement* mux;
    };
    auto* ctx = new Ctx{p, mux};
    // Cada pista que aparece: su parser y al muxer (sin recodificar).
    g_signal_connect_data(dmx, "pad-added",
                          G_CALLBACK(+[](GstElement*, GstPad* pad, gpointer data) {
                              auto* c = static_cast<Ctx*>(data);
                              GstCaps* caps = gst_pad_get_current_caps(pad);
                              const std::string tipo = caps != nullptr ? gst_structure_get_name(gst_caps_get_structure(caps, 0)) : "";
                              if (caps != nullptr) gst_caps_unref(caps);
                              const char* parser = tipo == "video/x-h264"   ? "h264parse"
                                                   : tipo == "video/x-h265" ? "h265parse"
                                                   : tipo == "audio/mpeg"   ? "aacparse"
                                                                            : nullptr;
                              if (parser == nullptr) return;
                              GstElement* q = gst_element_factory_make("queue", nullptr);
                              GstElement* pr = gst_element_factory_make(parser, nullptr);
                              gst_bin_add_many(GST_BIN(c->pipeline), q, pr, nullptr);
                              GstPad* entra = gst_element_get_static_pad(q, "sink");
                              gst_pad_link(pad, entra);
                              gst_object_unref(entra);
                              gst_element_link(q, pr);
                              GstPad* sale = gst_element_get_static_pad(pr, "src");
                              GstPad* destinoPad =
                                  gst_element_request_pad_simple(c->mux, tipo.rfind("video", 0) == 0 ? "video_%u" : "audio_%u");
                              if (destinoPad != nullptr) {
                                  gst_pad_link(sale, destinoPad);
                                  gst_object_unref(destinoPad);
                              }
                              gst_object_unref(sale);
                              gst_element_sync_state_with_parent(pr);
                              gst_element_sync_state_with_parent(q);
                          }),
                          ctx, [](gpointer data, GClosure*) { delete static_cast<Ctx*>(data); }, static_cast<GConnectFlags>(0));
    gst_element_set_state(p, GST_STATE_PLAYING);
    GstBus* bus = gst_element_get_bus(p);
    std::string error;
    for (;;) {
        GstMessage* m = gst_bus_timed_pop_filtered(bus, 300 * GST_MSECOND,
                                                   static_cast<GstMessageType>(GST_MESSAGE_EOS | GST_MESSAGE_ERROR));
        if (m == nullptr) {
            if (progreso) {
                gint64 pos = 0, dur = 0;
                if (gst_element_query_position(p, GST_FORMAT_TIME, &pos) && gst_element_query_duration(p, GST_FORMAT_TIME, &dur) && dur > 0)
                    progreso(std::clamp(static_cast<double>(pos) / static_cast<double>(dur), 0.0, 1.0));
            }
            continue;
        }
        if (GST_MESSAGE_TYPE(m) == GST_MESSAGE_ERROR) {
            GError* er = nullptr;
            gst_message_parse_error(m, &er, nullptr);
            error = er != nullptr ? ba::diag::utf8Valido(er->message) : traducir("motor", "error");
            if (er != nullptr) g_error_free(er);
        }
        gst_message_unref(m);
        break;
    }
    gst_object_unref(bus);
    gst_element_set_state(p, GST_STATE_NULL);
    gst_object_unref(dmx);
    gst_object_unref(mux);
    gst_object_unref(p);
    if (error.empty() && progreso) progreso(1.0);
    return error;
}

namespace {
/// Engancha cada pista nueva de un demux (o splitmuxsrc) a su parser y al muxer.
void enlazarPistas(GstElement* origen, GstElement* pipeline, GstElement* mux) {
    struct Ctx {
        GstElement* pipeline;
        GstElement* mux;
    };
    g_signal_connect_data(origen, "pad-added",
                          G_CALLBACK(+[](GstElement*, GstPad* pad, gpointer data) {
                              auto* c = static_cast<Ctx*>(data);
                              GstCaps* caps = gst_pad_get_current_caps(pad);
                              if (caps == nullptr) caps = gst_pad_query_caps(pad, nullptr);
                              const std::string tipo =
                                  caps != nullptr && gst_caps_get_size(caps) > 0 ? gst_structure_get_name(gst_caps_get_structure(caps, 0)) : "";
                              if (caps != nullptr) gst_caps_unref(caps);
                              const char* parser = tipo == "video/x-h264"   ? "h264parse"
                                                   : tipo == "video/x-h265" ? "h265parse"
                                                   : tipo == "audio/mpeg"   ? "aacparse"
                                                                            : nullptr;
                              if (parser == nullptr) return;
                              GstElement* q = gst_element_factory_make("queue", nullptr);
                              GstElement* pr = gst_element_factory_make(parser, nullptr);
                              gst_bin_add_many(GST_BIN(c->pipeline), q, pr, nullptr);
                              GstPad* entra = gst_element_get_static_pad(q, "sink");
                              gst_pad_link(pad, entra);
                              gst_object_unref(entra);
                              gst_element_link(q, pr);
                              GstPad* sale = gst_element_get_static_pad(pr, "src");
                              GstPad* destino =
                                  gst_element_request_pad_simple(c->mux, tipo.rfind("video", 0) == 0 ? "video_%u" : "audio_%u");
                              if (destino != nullptr) {
                                  gst_pad_link(sale, destino);
                                  gst_object_unref(destino);
                              }
                              gst_object_unref(sale);
                              gst_element_sync_state_with_parent(pr);
                              gst_element_sync_state_with_parent(q);
                          }),
                          new Ctx{pipeline, mux}, [](gpointer data, GClosure*) { delete static_cast<Ctx*>(data); },
                          static_cast<GConnectFlags>(0));
}

/// Corre un pipeline hasta EOS o error. "" o el error.
std::string correrHastaElFinal(GstElement* p) {
    gst_element_set_state(p, GST_STATE_PLAYING);
    GstBus* bus = gst_element_get_bus(p);
    std::string error;
    GstMessage* m = gst_bus_timed_pop_filtered(bus, static_cast<GstClockTime>(ajustes::sANs("record.replayJuntarS", 120)), static_cast<GstMessageType>(GST_MESSAGE_EOS | GST_MESSAGE_ERROR));
    if (m == nullptr) {
        error = traducir("motor", "no terminó en 2 minutos");
    } else {
        if (GST_MESSAGE_TYPE(m) == GST_MESSAGE_ERROR) {
            GError* er = nullptr;
            gst_message_parse_error(m, &er, nullptr);
            error = er != nullptr ? ba::diag::utf8Valido(er->message) : traducir("motor", "error");
            if (er != nullptr) g_error_free(er);
        }
        gst_message_unref(m);
    }
    gst_object_unref(bus);
    gst_element_set_state(p, GST_STATE_NULL);
    return error;
}
} // namespace

std::string juntar(const std::vector<std::string>& tramos, const std::string& destino) {
    if (tramos.empty()) return traducir("motor", "no hay tramos");
    GstElement* p = gst_pipeline_new("juntar");
    GstElement* src = gst_element_factory_make("splitmuxsrc", nullptr);
    GstElement* mux = gst_element_factory_make("matroskamux", nullptr);
    GstElement* sink = gst_element_factory_make("filesink", nullptr);
    if (src == nullptr || mux == nullptr || sink == nullptr) {
        for (GstElement* e : {src, mux, sink})
            if (e != nullptr) gst_object_unref(e);
        gst_object_unref(p);
        return traducir("motor", "falta splitmuxsrc o matroskamux");
    }
    g_object_set(sink, "location", destino.c_str(), nullptr);
    // La lista de archivos, en orden (en vez de un patrón).
    auto* lista = new std::vector<std::string>(tramos);
    g_signal_connect_data(src, "format-location",
                          G_CALLBACK(+[](GstElement*, gpointer d) -> gchar** {
                              auto* l = static_cast<std::vector<std::string>*>(d);
                              auto** v = g_new0(gchar*, l->size() + 1);
                              for (std::size_t i = 0; i < l->size(); ++i) v[i] = g_strdup((*l)[i].c_str());
                              return v;
                          }),
                          lista, [](gpointer d, GClosure*) { delete static_cast<std::vector<std::string>*>(d); },
                          static_cast<GConnectFlags>(0));
    gst_bin_add_many(GST_BIN(p), src, mux, sink, nullptr);
    gst_element_link(mux, sink);
    enlazarPistas(src, p, mux);
    const std::string e = correrHastaElFinal(p);
    gst_object_unref(p);
    return e;
}

Repeticion::Repeticion(Reloj& reloj, std::string carpetaTramos, int segundos, int kbps)
    : carpeta_(std::move(carpetaTramos)), segundos_(std::clamp(segundos, 4, 600)) {
    Grabacion::Config c;
    c.carpeta = carpeta_;
    c.nombre = "repeticion";
    c.tramoSeg = std::max(1, ajustes::entero("record.replayTramoS", 2));
    c.gopSeg = 1; // un tramo empieza siempre en un cuadro clave
    c.maxTramos = segundos_ / 2 + 2;
    c.kbps = kbps;
    c.colchonSeg = 10;
    grab_ = std::make_unique<Grabacion>(reloj, c);
}

Repeticion::~Repeticion() {
    grab_.reset();
    std::error_code ec;
    std::filesystem::remove_all(carpeta_, ec);
}

std::string Repeticion::iniciar(const Fuentes& f) { return grab_->iniciar(f); }

bool Repeticion::andando() const { return grab_ && grab_->grabando(); }

void Repeticion::tic() {
    if (grab_) grab_->tic();
}

std::string Repeticion::guardar(const std::string& destino) {
    // Los tramos que siguen en disco, menos el que se está escribiendo (el último).
    std::vector<std::string> hay;
    for (const auto& a : grab_->archivos()) {
        std::error_code ec;
        if (std::filesystem::exists(a, ec)) hay.push_back(a);
    }
    if (hay.size() < 2) return traducir("motor", "todavía no hay nada guardado (esperá unos segundos)");
    hay.pop_back();
    const std::size_t n = static_cast<std::size_t>(segundos_ / 2);
    if (hay.size() > n) hay.erase(hay.begin(), hay.end() - static_cast<std::ptrdiff_t>(n));
    return juntar(hay, destino);
}

// --- Grabación ---------------------------------------------------------------------------

namespace {
/// ¿Se puede escribir en la carpeta? La crea si falta y prueba con un archivo de verdad
/// (un disco desenchufado, lleno o de solo lectura no pasa).
bool escribible(const std::string& carpeta) {
    if (carpeta.empty()) return false;
    std::error_code ec;
    const std::filesystem::path dir(carpeta);
    std::filesystem::create_directories(dir, ec);
    const auto prueba = dir / ".ba-prueba-escritura";
    bool ok = false;
    {
        std::ofstream o(prueba, std::ios::binary | std::ios::trunc);
        o << "ok";
        o.flush();
        ok = o.good();
    }
    std::filesystem::remove(prueba, ec);
    return ok;
}

/// El archivo en curso, abierto aparte solo para bajarlo al disco (F1-09): sincronizar
/// cualquier descriptor del archivo baja lo que el sistema tiene de él en memoria.
class ArchivoEnCurso {
   public:
    ~ArchivoEnCurso() { cerrar(); }
    bool abrir(const std::string& ruta) {
        cerrar();
#if defined(_WIN32)
        // La ruta en UTF-8, como la abre filesink (g_fopen).
        gunichar2* w = g_utf8_to_utf16(ruta.c_str(), -1, nullptr, nullptr, nullptr);
        if (w == nullptr) return false;
        h_ = CreateFileW(reinterpret_cast<const wchar_t*>(w), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                         OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        g_free(w);
        return h_ != INVALID_HANDLE_VALUE;
#else
        fd_ = ::open(ruta.c_str(), O_WRONLY);
        return fd_ >= 0;
#endif
    }
    bool bajar() {
#if defined(_WIN32)
        return h_ != INVALID_HANDLE_VALUE && FlushFileBuffers(h_) != 0;
#elif defined(__APPLE__)
        return fd_ >= 0 && ::fsync(fd_) == 0; // F_FULLFSYNC por segundo es demasiado para un video
#else
        return fd_ >= 0 && ::fdatasync(fd_) == 0;
#endif
    }
    void cerrar() {
#if defined(_WIN32)
        if (h_ != INVALID_HANDLE_VALUE) CloseHandle(h_);
        h_ = INVALID_HANDLE_VALUE;
#else
        if (fd_ >= 0) ::close(fd_);
        fd_ = -1;
#endif
    }

   private:
#if defined(_WIN32)
    HANDLE h_ = INVALID_HANDLE_VALUE;
#else
    int fd_ = -1;
#endif
};
} // namespace

Grabacion::Grabacion(Reloj& reloj, Config cfg) : reloj_(reloj), cfg_(std::move(cfg)) {}

Grabacion::~Grabacion() { parar(3000); }

std::string Grabacion::iniciar(const Fuentes& f) {
    std::lock_guard<std::mutex> l(mu_);
    if (pedida_) return {};
    error_.clear();
    {
        std::lock_guard<std::mutex> la(arMu_);
        archivos_.clear();
    }
    if (f.video.empty() && f.audios.empty()) return error_ = traducir("motor", "no hay qué grabar");
    fuentes_ = f;
    parte_ = 1;
    cortes_ = intentos_ = 0;
    caidaDesdeNs_ = 0;
    desbordes_ = 0;
    bytes_ = 0;
    pausada_ = false;
    // Los codificadores: los de la salida con este perfil (compartidos con la
    // transmisión y la copia) o uno propio.
    std::string e;
    if (!f.video.empty()) {
        Codificacion::Config c;
        c.fuente = f.video;
        c.perfil = {cfg_.kbps, cfg_.gopSeg * std::max(1, cfg_.fps), cfg_.control, cfg_.crf, cfg_.codificadores};
        codV_ = codificacionPara(reloj_, c, cfg_.codificadorPropio, &e);
        if (!codV_) return error_ = llenar(traducir("motor", "no hay codificador de video (%1)"), {e});
    }
    for (const auto& a : f.audios) {
        Codificacion::Config c;
        c.fuente = a;
        c.video = false;
        c.kbpsAudio = cfg_.kbpsAudio;
        auto cod = codificacionPara(reloj_, c, cfg_.codificadorPropio, &e);
        if (!cod) {
            codV_.reset();
            codA_.clear();
            return error_ = e;
        }
        codA_.push_back(std::move(cod));
    }
    error_ = armar();
    if (!error_.empty()) {
        codV_.reset();
        codA_.clear();
        return error_;
    }
    pedida_ = true;
    desdeNs_ = ahoraNs();
    // El archivo en curso, al disco cada segundo, en un hilo aparte (F1-09).
    if (ajustes::entero("record.sincronizarMs", 1000) > 0) {
        {
            std::lock_guard<std::mutex> ls(sincMu_);
            sincSeguir_ = true;
        }
        sincronizador_ = std::thread([this]() { bucleSincronizar(); });
    }
    return {};
}

std::string Grabacion::carpetaParaParte() const {
    // La principal mientras se pueda escribir; si no, la de respaldo (la de la copia):
    // un disco que se desenchufa o se llena no deja la salida sin grabar.
    if (escribible(cfg_.carpeta) || cfg_.carpetaRespaldo.empty()) return cfg_.carpeta;
    if (escribible(cfg_.carpetaRespaldo)) return cfg_.carpetaRespaldo;
    return cfg_.carpeta;
}

std::string Grabacion::armar() {
    const std::string carpeta = carpetaParaParte();
    std::error_code ec;
    std::filesystem::create_directories(carpeta, ec);
    // La primera parte con el nombre de siempre; si se cortó y volvió, «-parte2», «-parte3»…
    const std::string nombre = parte_ > 1 ? cfg_.nombre + "-parte" + std::to_string(parte_) : cfg_.nombre;
    {
        std::lock_guard<std::mutex> la(arMu_);
        carpetaParte_ = carpeta;
        nombreParte_ = nombre;
    }
    const std::string patron = (std::filesystem::path(carpeta) / (nombre + "-%05d.mkv")).string();
    // Matroska (aguanta un corte: se puede abrir lo escrito). Tiene que ir en la
    // descripción: las pistas se piden al armar y con «muxer» puesto después ya
    // había creado su mp4mux de fábrica (los archivos salían MP4 con nombre .mkv).
    std::string d = "splitmuxsink name=mux async-finalize=true muxer-factory=matroskamux location=" + entreComillas(patron) +
                    (cfg_.maxTramos > 0 ? " max-files=" + std::to_string(cfg_.maxTramos) : std::string()) + " max-size-time=" +
                    std::to_string(static_cast<guint64>(std::max(0, cfg_.tramoSeg)) * GST_SECOND) + " ";
    // Cada rama: lo ya codificado ! válvula (pausar sin cerrar) ! colchón (lo que
    // aguanta un disco trabado) ! parser ! mux.
    const std::string colchon = " max-size-time=" + std::to_string(static_cast<guint64>(std::max(1, cfg_.colchonSeg)) * GST_SECOND) +
                                " max-size-buffers=0 max-size-bytes=" + std::to_string(std::min<guint64>(static_cast<guint64>(ajustes::entero("record.colchonMB", 512)) << 20, G_MAXUINT)) + " ";
    if (codV_)
        d += "intersrc producer-name=" + entreComillas(codV_->productor()) + " ! valve name=pausa_v ! queue name=colchon_v" + colchon +
             "! h264parse ! mux.video ";
    for (std::size_t i = 0; i < codA_.size(); ++i)
        d += "intersrc producer-name=" + entreComillas(codA_[i]->productor()) + " ! valve name=pausa_a" + std::to_string(i) +
             " ! queue name=colchon_a" + std::to_string(i) + colchon + "! aacparse ! mux.audio_" + std::to_string(i) + " ";
    GError* e = nullptr;
    GstElement* p = gst_parse_launch(d.c_str(), &e);
    if (e != nullptr) {
        std::string m = ba::diag::utf8Valido(e->message);
        g_error_free(e);
        if (p != nullptr) gst_object_unref(p);
        return m;
    }
    // Las válvulas y los colchones: para pausar, medir el atraso y contar lo escrito.
    std::vector<std::string> valv, colch;
    if (codV_) {
        valv.push_back("pausa_v");
        colch.push_back("colchon_v");
    }
    for (std::size_t i = 0; i < codA_.size(); ++i) {
        valv.push_back("pausa_a" + std::to_string(i));
        colch.push_back("colchon_a" + std::to_string(i));
    }
    for (const auto& n : valv)
        if (GstElement* v = gst_bin_get_by_name(GST_BIN(p), n.c_str())) {
            // Que pausada deje pasar el EOS y los segmentos (si no, al parar no cierra).
            if (tiene(v, "drop-mode")) gst_util_set_object_arg(G_OBJECT(v), "drop-mode", "forward-sticky-events");
            if (pausada_) g_object_set(v, "drop", TRUE, nullptr);
            valvulas_.push_back(v);
        }
    for (const auto& n : colch)
        if (GstElement* q = gst_bin_get_by_name(GST_BIN(p), n.c_str())) {
            GstPad* sp = gst_element_get_static_pad(q, "src");
            gst_pad_add_probe(sp, GST_PAD_PROBE_TYPE_BUFFER,
                              +[](GstPad*, GstPadProbeInfo* i, gpointer d) -> GstPadProbeReturn {
                                  static_cast<Grabacion*>(d)->bytes_ += gst_buffer_get_size(GST_PAD_PROBE_INFO_BUFFER(i));
                                  return GST_PAD_PROBE_OK;
                              },
                              this, nullptr);
            gst_object_unref(sp);
            // El colchón lleno = el disco no da abasto hace rato: se pierden cuadros.
            g_signal_connect(q, "overrun", G_CALLBACK(+[](GstElement*, gpointer d) { ++static_cast<Grabacion*>(d)->desbordes_; }), this);
            colchones_.push_back(q);
        }
    // El video entra desde un cuadro clave (el archivo nuevo empieza decodificable).
    if (codV_ && !colchones_.empty()) {
        GstPad* sp = gst_element_get_static_pad(colchones_.front(), "sink");
        gst_pad_add_probe(sp, GST_PAD_PROBE_TYPE_BUFFER, &compuertaClave, &esperarClave_, nullptr);
        gst_object_unref(sp);
    }
    // Cada tramo nuevo: anotarlo.
    GstElement* mux = gst_bin_get_by_name(GST_BIN(p), "mux");
    g_signal_connect(mux, "format-location-full",
                     G_CALLBACK(+[](GstElement*, guint id, GstSample*, gpointer d) -> gchar* {
                         auto* g = static_cast<Grabacion*>(d);
                         char nom[32];
                         std::snprintf(nom, sizeof nom, "-%05u.mkv", id);
                         std::lock_guard<std::mutex> la(g->arMu_); // (mu_ no: parar lo tiene mientras espera el EOS)
                         const std::string ruta = (std::filesystem::path(g->carpetaParte_) / (g->nombreParte_ + nom)).string();
                         g->archivos_.push_back(ruta);
                         return g_strdup(ruta.c_str());
                     }),
                     this);
    edadSalida_.ponerEnEntrada(mux, reloj_); // la edad de lo que entra al archivo (medida)
    gst_object_unref(mux);
    sinRelojPropio(p);
    reloj_.adoptar(p);
    const std::int64_t lat = std::max(fuentes_.latVideoNs, fuentes_.latAudioNs) + (codV_ ? codV_->latenciaNs() : 0) +
                             margenSalidaNs(TipoSalida::Grabacion);
    gst_pipeline_set_latency(GST_PIPELINE(p), static_cast<GstClockTime>(lat));
    latenciaNs_ = lat;
    esperarClave_ = true;
    if (gst_element_set_state(p, GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE) {
        edadSalida_.sacar();
        gst_element_set_state(p, GST_STATE_NULL);
        for (GstElement* x : valvulas_) gst_object_unref(x);
        for (GstElement* x : colchones_) gst_object_unref(x);
        valvulas_.clear();
        colchones_.clear();
        gst_object_unref(p);
        return llenar(traducir("motor", "la grabación no arrancó (%1)"), {carpeta});
    }
    pipeline_ = p;
    if (codV_) {
        generacionV_ = codV_->generacion();
        codificador_ = codV_->fabrica();
        codV_->pedirClave();
    } else {
        codificador_.clear();
    }
    registrar(Nivel::Info, llenar(traducir("motor", "grabación: %1 (%2)"), {patron, codificador_.empty() ? traducir("motor", "solo audio") : codificador_}));
    return {};
}

bool Grabacion::cerrar(int esperaMs) {
    if (pipeline_ == nullptr) return true;
    for (GstElement* v : valvulas_) g_object_set(v, "drop", FALSE, nullptr); // pausada: que pase el EOS
    // EOS por todas las ramas: splitmuxsink cierra el tramo (índice incluido).
    gst_element_send_event(pipeline_, gst_event_new_eos());
    GstBus* bus = gst_element_get_bus(pipeline_);
    GstMessage* m = gst_bus_timed_pop_filtered(bus, static_cast<GstClockTime>(esperaMs) * GST_MSECOND,
                                               static_cast<GstMessageType>(GST_MESSAGE_EOS | GST_MESSAGE_ERROR));
    const bool ok = m != nullptr && GST_MESSAGE_TYPE(m) == GST_MESSAGE_EOS;
    if (m != nullptr) gst_message_unref(m);
    gst_object_unref(bus);
    edadSalida_.sacar();
    gst_element_set_state(pipeline_, GST_STATE_NULL);
    for (GstElement* e : valvulas_) gst_object_unref(e);
    for (GstElement* e : colchones_) gst_object_unref(e);
    valvulas_.clear();
    colchones_.clear();
    gst_object_unref(pipeline_);
    pipeline_ = nullptr;
    return ok;
}

bool Grabacion::parar(int esperaMs) {
    std::lock_guard<std::mutex> l(mu_);
    pedida_ = false;
    if (sincronizador_.joinable()) {
        {
            std::lock_guard<std::mutex> ls(sincMu_);
            sincSeguir_ = false;
        }
        sincCv_.notify_all();
        sincronizador_.join();
    }
    pausada_ = false;
    bool ok = true;
    if (pipeline_ != nullptr) {
        ok = cerrar(esperaMs);
        if (!ok) registrar(Nivel::Aviso, traducir("motor", "grabación: no cerró a tiempo (el último tramo puede quedar sin índice)"));
    }
    codV_.reset();
    codA_.clear();
    return ok;
}

void Grabacion::bucleSincronizar() {
    ArchivoEnCurso archivo;
    std::string ruta;
    std::unique_lock<std::mutex> l(sincMu_);
    while (sincSeguir_) {
        sincCv_.wait_for(l, std::chrono::milliseconds(std::max(200, ajustes::entero("record.sincronizarMs", 1000))), [this]() { return !sincSeguir_; });
        if (!sincSeguir_) break;
        l.unlock();
        std::string actual;
        {
            std::lock_guard<std::mutex> la(arMu_);
            if (!archivos_.empty()) actual = archivos_.back();
        }
        // El tramo cambió: el anterior ya lo cerró el muxer; se abre el nuevo.
        if (actual != ruta) {
            ruta = actual;
            if (ruta.empty() || !archivo.abrir(ruta)) archivo.cerrar();
        }
        if (archivo.bajar()) ++sincronizaciones_;
        l.lock();
    }
}

double Grabacion::atrasoS() const {
    std::lock_guard<std::mutex> l(mu_);
    return static_cast<double>(atrasoNs()) / 1e9;
}

void Grabacion::simularDiscoTrabado(int ms) {
    std::lock_guard<std::mutex> l(mu_);
    if (colchones_.empty()) return;
    GstPad* sp = gst_element_get_static_pad(colchones_.front(), "src");
    gst_pad_add_probe(sp, GST_PAD_PROBE_TYPE_BUFFER,
                      +[](GstPad*, GstPadProbeInfo*, gpointer d) -> GstPadProbeReturn {
                          g_usleep(static_cast<gulong>(GPOINTER_TO_INT(d)) * 1000);
                          return GST_PAD_PROBE_REMOVE;
                      },
                      GINT_TO_POINTER(ms), nullptr);
    gst_object_unref(sp);
}

void Grabacion::pausar(bool si) {
    std::lock_guard<std::mutex> l(mu_);
    if (!pedida_ || pausada_.load() == si) return;
    // Al soltarla, el video vuelve desde un cuadro clave (pedido ya: no espera el GOP).
    if (!si) {
        esperarClave_ = true;
        if (codV_) codV_->pedirClave();
    }
    for (GstElement* v : valvulas_) g_object_set(v, "drop", si ? TRUE : FALSE, nullptr);
    pausada_ = si;
    registrar(Nivel::Info, llenar(si ? traducir("motor", "grabación %1: en pausa (el disco, para otra)") : traducir("motor", "grabación %1: sigue"),
                                  {cfg_.nombre}));
}

bool Grabacion::grabando() const {
    std::lock_guard<std::mutex> l(mu_);
    return pipeline_ != nullptr;
}

bool Grabacion::caida() const {
    std::lock_guard<std::mutex> l(mu_);
    return pedida_ && pipeline_ == nullptr;
}

void Grabacion::tic() {
    std::lock_guard<std::mutex> l(mu_);
    if (!pedida_) return;
    if (codV_) codV_->tic();
    for (const auto& c : codA_) c->tic();
    const std::int64_t ahora = ahoraNs();
    if (pipeline_ == nullptr) {
        // Cortada: la parte que sigue, cuando toque.
        if (ahora < reintentoNs_) return;
        error_ = armar();
        if (!error_.empty()) {
            reintentoNs_ = ahora + esperaRearmeNs(++intentos_);
            return;
        }
        intentos_ = 0;
        // El hueco, a la bitácora (los avisos del motor van al registro y de ahí a la del evento).
        if (caidaDesdeNs_ != 0)
            registrar(Nivel::Aviso, llenar(traducir("motor", "grabación %1: volvió en la parte %2 (%3); hueco de %4 s"),
                                           {cfg_.nombre, std::to_string(parte_), carpetaParte_, std::to_string(std::lround(static_cast<double>(ahora - caidaDesdeNs_) / 1e9))}));
        caidaDesdeNs_ = 0;
        return;
    }
    // --test: el banco de caos (Fallas.hpp); la del codificador, en el que lee esta grabación.
    if (codV_) codV_->aplicarFallas(pipeline_, "grabacion");
    else aplicarFallasProgramadas(pipeline_, "grabacion");
    bool corte = false;
    GstBus* bus = gst_element_get_bus(pipeline_);
    while (GstMessage* m = gst_bus_pop(bus)) {
        if (GST_MESSAGE_TYPE(m) == GST_MESSAGE_ERROR) {
            GError* e = nullptr;
            gst_message_parse_error(m, &e, nullptr);
            error_ = e != nullptr ? ba::diag::utf8Valido(e->message) : traducir("motor", "error");
            registrar(Nivel::Error, llenar(traducir("motor", "grabación (%1): %2"), {GST_OBJECT_NAME(GST_MESSAGE_SRC(m)), error_}));
            if (e != nullptr) g_error_free(e);
            corte = true;
        } else if (GST_MESSAGE_TYPE(m) == GST_MESSAGE_EOS) {
            corte = true; // nadie lo pidió: se cortó
        }
        gst_message_unref(m);
    }
    gst_object_unref(bus);
    if (corte) {
        // Cerrar lo que haya (el MKV escrito se abre igual) y seguir en otra parte, con espera.
        cerrar(500);
        {
            // El tramo que no se llegó a abrir no es un archivo de la grabación.
            std::lock_guard<std::mutex> la(arMu_);
            std::error_code ec;
            archivos_.erase(std::remove_if(archivos_.begin(), archivos_.end(), [&ec](const std::string& a) { return !std::filesystem::exists(a, ec); }),
                            archivos_.end());
        }
        ++parte_;
        ++cortes_;
        caidaDesdeNs_ = ahora;
        intentos_ = 0;
        reintentoNs_ = ahora + esperaRearmeNs(0);
        registrar(Nivel::Error, llenar(traducir("motor", "grabación %1: se cortó (%2); sigue sola en la parte %3"), {cfg_.nombre, error_, std::to_string(parte_)}));
        return;
    }
    // El codificador cambió (el de antes falló y se pasó a otro): las caps nuevas no
    // entran en el mismo archivo. Se cierra bien y sigue en otra parte, ya.
    if (codV_ && codV_->generacion() != generacionV_) {
        cerrar(2000);
        ++parte_;
        error_ = armar();
        if (!error_.empty()) {
            caidaDesdeNs_ = ahora;
            reintentoNs_ = ahora + esperaRearmeNs(intentos_ = 0);
        }
        registrar(Nivel::Aviso, llenar(traducir("motor", "grabación %1: cambió el codificador (%2); sigue en la parte %3"),
                                       {cfg_.nombre, codificador_, std::to_string(parte_)}));
    }
}

json Grabacion::estado() const {
    std::lock_guard<std::mutex> l(mu_);
    return {{"grabando", pipeline_ != nullptr},
            {"caida", pedida_ && pipeline_ == nullptr},
            {"parte", parte_},
            {"cortes", cortes_},
            {"error", error_},
            {"codificador", codificador_},
            {"segundos", pedida_ ? static_cast<double>(ahoraNs() - desdeNs_) / 1e9 : 0.0},
            {"archivos", archivos()},
            {"atraso_s", static_cast<double>(atrasoNs()) / 1e9},
            {"desbordes", desbordes_.load()},
            {"bytes", bytes_.load()},
            {"pausada", pausada_.load()},
            {"sincronizaciones", sincronizaciones_.load()},
            {"edad_salida", edadSalida_.json()}, // la medida (Tiempos)
            {"latencia_ms", static_cast<double>(latenciaNs_.load()) / 1e6}}; // la fijada al pipeline (Tiempos)
}

guint64 Grabacion::atrasoNs() const {
    guint64 max = 0;
    for (GstElement* q : colchones_) {
        guint64 t = 0;
        g_object_get(q, "current-level-time", &t, nullptr);
        max = std::max(max, t);
    }
    return max;
}

std::vector<std::string> Grabacion::archivos() const {
    std::lock_guard<std::mutex> l(arMu_);
    return archivos_;
}

// --- Transmisión ---------------------------------------------------------------------------

namespace {
/// La URL para mostrar: sin la clave de transmisión (lo que va después del último «/» en RTMP)
/// ni la passphrase de SRT.
std::string sinClave(const std::string& u) {
    if (u.rfind("rtmp", 0) == 0) {
        const auto p = u.rfind('/');
        return p == std::string::npos ? u : u.substr(0, p + 1) + "••••";
    }
    const auto q = u.find("passphrase=");
    if (q == std::string::npos) return u;
    auto fin = u.find('&', q);
    return u.substr(0, q) + "passphrase=••••" + (fin == std::string::npos ? std::string() : u.substr(fin));
}
bool esSrt(const std::string& u) { return u.rfind("srt://", 0) == 0; }
bool esRtmp(const std::string& u) { return u.rfind("rtmp://", 0) == 0 || u.rfind("rtmps://", 0) == 0; }
} // namespace

/// Un punto de ingest: su pipeline (lee lo codificado), lo que mandó y su reconexión.
struct Transmision::Envio {
    std::string url;
    bool respaldo = false;
    GstElement* pipeline = nullptr;
    int intentos = 0;
    std::int64_t reintentoNs = 0;
    std::string error;
    std::atomic<std::uint64_t> bytes{0}, cuadros{0};
    std::uint64_t bytesAntes = 0;
    std::int64_t medidoNs = 0, ultimoDatoNs = 0;
    double kbps = 0;
    std::atomic<bool> esperarClave{true};
    std::uint64_t generacionV = 0;
    std::int64_t latenciaNs = 0;
    MedidorEdad edad; ///< la edad de lo que sale (medida)
    bool conectado(std::int64_t ahora) const {
        return pipeline != nullptr && ultimoDatoNs != 0 && ahora - ultimoDatoNs < ajustes::sANs("stream.sinDatosS", 2);
    }
};

Transmision::Transmision(Reloj& reloj, Config cfg) : reloj_(reloj), cfg_(std::move(cfg)) {}

Transmision::~Transmision() { parar(); }

GstPadProbeReturn Transmision::alSalir(GstPad*, GstPadProbeInfo* info, gpointer d) {
    if (GstBuffer* b = GST_PAD_PROBE_INFO_BUFFER(info)) static_cast<Envio*>(d)->bytes += gst_buffer_get_size(b);
    return GST_PAD_PROBE_OK;
}

GstPadProbeReturn Transmision::alMandar(GstPad*, GstPadProbeInfo*, gpointer d) {
    ++static_cast<Envio*>(d)->cuadros;
    return GST_PAD_PROBE_OK;
}

std::string Transmision::armar(Envio& en) {
    desarmar(en);
    const bool srt = esSrt(en.url);
    if (!srt && !esRtmp(en.url)) return traducir("motor", "la dirección tiene que empezar con rtmp://, rtmps:// o srt://");
    // DSCP (F4-06): AF41 (video interactivo) en el byte TOS del socket SRT, salvo que la
    // dirección ya diga otro (`iptos` es una opción de SRT que srtsink toma de la URI).
    const std::string urlSrt = srt && ajustes::siNo("red.dscpSrt", true) && en.url.find("iptos=") == std::string::npos
                                   ? en.url + (en.url.find('?') == std::string::npos ? "?" : "&") + "iptos=136"
                                   : en.url;
    std::string d = srt ? "mpegtsmux name=mux alignment=7 ! identity name=sale ! srtsink name=red wait-for-connection=false uri=" +
                              entreComillas(urlSrt) + " "
                        : "flvmux name=mux streamable=true ! identity name=sale ! rtmp2sink name=red async-connect=true location=" +
                              entreComillas(en.url) + " ";
    // La misma cola para el video y el audio (F2-16): si la red se traba, las dos tiran lo
    // viejo hasta el mismo segundo y a la vuelta salen alineadas.
    const std::string cola = "queue leaky=downstream max-size-time=" + std::to_string(ajustes::msANs("stream.colaMs", 1000)) +
                             " max-size-buffers=0 max-size-bytes=0";
    if (codV_)
        d += "intersrc producer-name=" + entreComillas(codV_->productor()) + " ! " + cola + " name=cv ! h264parse config-interval=-1 ! mux. ";
    if (codA_) d += "intersrc producer-name=" + entreComillas(codA_->productor()) + " ! " + cola + " name=ca ! aacparse ! mux. ";
    GError* e = nullptr;
    GstElement* p = gst_parse_launch(d.c_str(), &e);
    if (e != nullptr) {
        std::string m = ba::diag::utf8Valido(e->message);
        g_error_free(e);
        if (p != nullptr) gst_object_unref(p);
        return m;
    }
    // Lo que tiran las colas (la red o la máquina no dan abasto): cuadros perdidos.
    for (const char* n : {"cv", "ca"})
        if (GstElement* q = gst_bin_get_by_name(GST_BIN(p), n)) {
            g_signal_connect(q, "overrun", G_CALLBACK(+[](GstElement*, gpointer d) { ++*static_cast<std::atomic<std::uint64_t>*>(d); }), &perdidos_);
            if (std::string(n) == "cv") {
                // El video sale desde un cuadro clave (quien recibe decodifica desde el primero) y se cuenta.
                GstPad* sp = gst_element_get_static_pad(q, "src");
                gst_pad_add_probe(sp, GST_PAD_PROBE_TYPE_BUFFER, &compuertaClave, &en.esperarClave, nullptr);
                gst_pad_add_probe(sp, GST_PAD_PROBE_TYPE_BUFFER, &Transmision::alMandar, &en, nullptr);
                gst_object_unref(sp);
            }
            gst_object_unref(q);
        }
    if (GstElement* sale = gst_bin_get_by_name(GST_BIN(p), "sale")) {
        if (GstPad* ps = gst_element_get_static_pad(sale, "src")) {
            gst_pad_add_probe(ps, GST_PAD_PROBE_TYPE_BUFFER, &Transmision::alSalir, &en, nullptr);
            gst_object_unref(ps);
        }
        en.edad.ponerEnEntrada(sale, reloj_); // la edad de lo que sale a la red (medida)
        gst_object_unref(sale);
    }
    sinRelojPropio(p);
    reloj_.adoptar(p);
    // Lo que tardan los buses y el codificador, más el margen de este tipo de salida (F2-06).
    const std::int64_t lat = std::max(fuentes_.latVideoNs, fuentes_.latAudioNs) + (codV_ ? codV_->latenciaNs() : 0) +
                             margenSalidaNs(srt ? TipoSalida::Srt : TipoSalida::Rtmp);
    gst_pipeline_set_latency(GST_PIPELINE(p), static_cast<GstClockTime>(lat));
    en.latenciaNs = lat;
    en.esperarClave = true;
    if (gst_element_set_state(p, GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE) {
        en.edad.sacar();
        gst_element_set_state(p, GST_STATE_NULL);
        gst_object_unref(p);
        return llenar(traducir("motor", "no se pudo conectar con %1"), {sinClave(en.url)});
    }
    en.pipeline = p;
    en.medidoNs = ahoraNs();
    en.bytesAntes = en.bytes.load();
    en.ultimoDatoNs = 0;
    if (codV_) {
        en.generacionV = codV_->generacion();
        codificador_ = codV_->fabrica();
        codV_->pedirClave();
    }
    return {};
}

void Transmision::desarmar(Envio& en) {
    if (en.pipeline == nullptr) return;
    en.edad.sacar();
    gst_element_set_state(en.pipeline, GST_STATE_NULL);
    gst_object_unref(en.pipeline);
    en.pipeline = nullptr;
}

std::string Transmision::iniciar(const Fuentes& f) {
    std::lock_guard<std::mutex> l(mu_);
    if (pedida_) return {};
    // La dirección mal escrita: no se reintenta.
    for (const std::string& u : {cfg_.url, cfg_.urlRespaldo})
        if (!u.empty() && !esSrt(u) && !esRtmp(u)) return traducir("motor", "la dirección tiene que empezar con rtmp://, rtmps:// o srt://");
    if (cfg_.url.empty()) return traducir("motor", "la dirección tiene que empezar con rtmp://, rtmps:// o srt://");
    fuentes_ = f;
    // Los codificadores de la salida con este perfil (compartidos con los demás destinos y
    // la grabación). Calidad constante no sirve para la red: VBR con tope.
    std::string e;
    if (!f.video.empty()) {
        Codificacion::Config c;
        c.fuente = f.video;
        c.perfil = {cfg_.kbps, cfg_.gopSeg * std::max(1, cfg_.fps), cfg_.control == ControlTasa::Calidad ? ControlTasa::VBR : cfg_.control, 18,
                    cfg_.codificadores};
        codV_ = codificacionPara(reloj_, c, false, &e);
        if (!codV_) return llenar(traducir("motor", "no hay codificador de video (%1)"), {e});
    }
    if (!f.audios.empty()) {
        Codificacion::Config c;
        c.fuente = f.audios.front();
        c.video = false;
        c.kbpsAudio = cfg_.kbpsAudio;
        codA_ = codificacionPara(reloj_, c, false, &e);
        if (!codA_) {
            codV_.reset();
            return e;
        }
    }
    pedida_ = true;
    perdidos_ = 0;
    perdidosBase_ = codV_ ? codV_->perdidos() : 0;
    envios_.clear();
    for (const std::string& u : {cfg_.url, cfg_.urlRespaldo}) {
        if (u.empty()) continue;
        auto en = std::make_unique<Envio>();
        en->url = u;
        en->respaldo = !envios_.empty();
        en->error = armar(*en);
        if (!en->error.empty()) {
            en->intentos = 1;
            en->reintentoNs = ahoraNs() + static_cast<std::int64_t>(GST_SECOND);
            registrar(Nivel::Aviso, llenar(traducir("motor", "transmisión: %1 (se reintenta)"), {en->error}));
        } else {
            registrar(Nivel::Info, llenar(en->respaldo ? traducir("motor", "transmisión (respaldo) a %1 (%2)") : traducir("motor", "transmisión a %1 (%2)"),
                                          {sinClave(u), codificador_.empty() ? traducir("motor", "solo audio") : codificador_}));
        }
        envios_.push_back(std::move(en));
    }
    return {};
}

void Transmision::parar() {
    std::lock_guard<std::mutex> l(mu_);
    pedida_ = false;
    for (auto& en : envios_) {
        if (en->pipeline != nullptr) {
            // EOS: rtmp2sink manda el cierre al servidor antes de cortar.
            gst_element_send_event(en->pipeline, gst_event_new_eos());
            GstBus* bus = gst_element_get_bus(en->pipeline);
            if (GstMessage* m = gst_bus_timed_pop_filtered(bus, static_cast<GstClockTime>(ajustes::msANs("stream.cierreMs", 2000)),
                                                           static_cast<GstMessageType>(GST_MESSAGE_EOS | GST_MESSAGE_ERROR)))
                gst_message_unref(m);
            gst_object_unref(bus);
        }
        desarmar(*en);
    }
    envios_.clear();
    codV_.reset();
    codA_.reset();
}

bool Transmision::andando() const {
    std::lock_guard<std::mutex> l(mu_);
    return pedida_;
}

bool Transmision::conectada() const {
    std::lock_guard<std::mutex> l(mu_);
    const std::int64_t ahora = ahoraNs();
    for (const auto& en : envios_)
        if (en->conectado(ahora)) return true;
    return false;
}

void Transmision::tic() {
    std::lock_guard<std::mutex> l(mu_);
    if (!pedida_) return;
    if (codV_) codV_->tic();
    if (codA_) codA_->tic();
    const std::int64_t ahora = ahoraNs();
    for (auto& en : envios_) atender(*en, ahora);
}

void Transmision::atender(Envio& en, std::int64_t ahora) {
    if (en.pipeline == nullptr) {
        if (ahora < en.reintentoNs) return;
        en.error = armar(en);
        if (!en.error.empty()) en.reintentoNs = ahora + esperaReconexionNs(++en.intentos);
        return;
    }
    // --test: el banco de caos (Fallas.hpp); la del codificador, en el que lee esta transmisión.
    if (codV_) codV_->aplicarFallas(en.pipeline, "transmision");
    else aplicarFallasProgramadas(en.pipeline, "transmision");
    // Lo que sale (kbps), una vez por segundo.
    if (ahora - en.medidoNs >= 1'000'000'000LL) {
        const std::uint64_t b = en.bytes.load();
        en.kbps = static_cast<double>(b - en.bytesAntes) * 8.0 / 1000.0 / (static_cast<double>(ahora - en.medidoNs) / 1e9);
        if (b != en.bytesAntes) {
            en.ultimoDatoNs = ahora;
            if (en.intentos > 0) {
                registrar(Nivel::Info, llenar(traducir("motor", "transmisión: volvió (después de %1 intentos)"), {std::to_string(en.intentos)}));
                en.intentos = 0;
            }
        }
        en.bytesAntes = b;
        en.medidoNs = ahora;
    }
    GstBus* bus = gst_element_get_bus(en.pipeline);
    bool caida = false;
    while (GstMessage* m = gst_bus_pop(bus)) {
        if (GST_MESSAGE_TYPE(m) == GST_MESSAGE_ERROR || GST_MESSAGE_TYPE(m) == GST_MESSAGE_EOS) {
            GError* e = nullptr;
            if (GST_MESSAGE_TYPE(m) == GST_MESSAGE_ERROR) gst_message_parse_error(m, &e, nullptr);
            en.error = e != nullptr ? ba::diag::utf8Valido(e->message) : traducir("motor", "se cortó");
            if (e != nullptr) g_error_free(e);
            caida = true;
        }
        gst_message_unref(m);
    }
    gst_object_unref(bus);
    // Mandaba y dejó de mandar (la red trabada): se rearma igual; las colas ya tiraron
    // lo viejo y a la vuelta sale lo de ahora (T9).
    if (!caida && en.ultimoDatoNs != 0 && ahora - en.ultimoDatoNs > ajustes::sANs("stream.rearmarSinDatosS", 4)) {
        en.error = traducir("motor", "no sale nada hace unos segundos");
        caida = true;
    }
    if (caida) {
        registrar(Nivel::Aviso, llenar(traducir("motor", "transmisión: %1 (se reconecta)"), {en.error}));
        desarmar(en);
        en.reintentoNs = ahora + esperaReconexionNs(++en.intentos);
        return;
    }
    // El codificador cambió (el de antes falló): con las caps nuevas, el muxer se rearma ya.
    if (codV_ && codV_->generacion() != en.generacionV) {
        registrar(Nivel::Aviso, llenar(traducir("motor", "transmisión a %1: sigue con el codificador %2"), {sinClave(en.url), codV_->fabrica()}));
        en.error = armar(en);
        if (!en.error.empty()) en.reintentoNs = ahora + esperaReconexionNs(++en.intentos);
    }
}

json Transmision::estadoDe(const Envio& en) const {
    const std::int64_t ahora = ahoraNs();
    const bool conectado = en.conectado(ahora);
    return {{"url", sinClave(en.url)},
            {"conectada", conectado},
            {"reconectando", pedida_ && !conectado && en.intentos > 0},
            {"intentos", en.intentos},
            {"kbps", std::round(en.kbps)},
            {"bytes", en.bytes.load()},
            {"cuadros", en.cuadros.load()},
            {"error", en.error}};
}

json Transmision::estado() const {
    std::lock_guard<std::mutex> l(mu_);
    const std::int64_t ahora = ahoraNs();
    bool alguna = false;
    for (const auto& en : envios_) alguna = alguna || en->conectado(ahora);
    const Envio* p = envios_.empty() ? nullptr : envios_.front().get();
    json j = p != nullptr ? estadoDe(*p) : json{{"url", sinClave(cfg_.url)}, {"kbps", 0.0}, {"bytes", 0}, {"cuadros", 0}, {"intentos", 0}, {"error", std::string()}};
    // Con respaldo, la salida está bien mientras salga por alguno de los dos.
    j["andando"] = pedida_;
    j["conectada"] = alguna;
    j["reconectando"] = pedida_ && !alguna && p != nullptr && p->intentos > 0;
    j["perdidos"] = perdidos_.load() + (codV_ ? codV_->perdidos() - std::min(perdidosBase_, codV_->perdidos()) : 0);
    j["codificador"] = codificador_;
    if (envios_.size() > 1) j["respaldo"] = estadoDe(*envios_[1]);
    j["edad_salida"] = p != nullptr ? p->edad.json() : json(); // la medida (Tiempos)
    j["latencia_ms"] = p != nullptr ? static_cast<double>(p->latenciaNs) / 1e6 : 0.0; // la fijada al pipeline (Tiempos)
    return j;
}

} // namespace ba::motor
