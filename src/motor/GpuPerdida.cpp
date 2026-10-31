// ============================================================================
// src/motor/GpuPerdida.cpp — ver GpuPerdida.hpp
// ============================================================================
#include "GpuPerdida.hpp"

#include "Reloj.hpp"
#include "diag/Utf8.hpp"
#include "i18n/I18n.hpp"
#include "platform/Hilo.hpp"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdio>
#include <mutex>
#include <thread>
#include <utility>

namespace ba::motor {

using ba::i18n::llenar;
using ba::i18n::traducir;
using json = nlohmann::json;

namespace {

struct Estado {
    std::atomic<bool> perdida{false};
    std::atomic<bool> probando{false};
    std::mutex mu; // lo de abajo
    std::string motivo, elemento;
    gint64 desdeUs = 0;
    std::uint64_t perdidas = 0, rearmes = 0;
    std::string rearmeGpu;
    std::int64_t rearmeMs = -1;
    gint64 ultimaPruebaUs = 0;
    /// D3D12: los dispositivos vigilados (con ref) y su aviso.
    std::vector<std::pair<GObject*, gulong>> vigilados;
};
// Nunca se destruye (un estático que se destruye al salir con un hilo usándolo mata el proceso).
Estado& estado() {
    static auto* e = new Estado;
    return *e;
}

constexpr const char* kMotivoD3D12 = "device-removed-reason";

std::string minusculas(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

bool contiene(const std::string& texto, std::initializer_list<const char*> marcas) {
    for (const char* m : marcas)
        if (texto.find(m) != std::string::npos) return true;
    return false;
}

/// El valor de una propiedad entera (gint, guint, glong…) como int64; 0 si no es entera.
std::int64_t entero(GObject* o, const char* prop) {
    GParamSpec* ps = g_object_class_find_property(G_OBJECT_GET_CLASS(o), prop);
    if (ps == nullptr) return 0;
    GValue v = G_VALUE_INIT;
    g_value_init(&v, G_PARAM_SPEC_VALUE_TYPE(ps));
    g_object_get_property(o, prop, &v);
    GValue i = G_VALUE_INIT;
    g_value_init(&i, G_TYPE_INT64);
    std::int64_t r = 0;
    if (g_value_transform(&v, &i)) r = g_value_get_int64(&i);
    g_value_unset(&i);
    g_value_unset(&v);
    return r;
}

std::string hresult(std::int64_t r) {
    char b[16];
    std::snprintf(b, sizeof b, "0x%08X", static_cast<unsigned>(static_cast<std::uint32_t>(r)));
    return b;
}

/// D3D12: lo que dice algún dispositivo vigilado ("" si todos andan).
std::string motivoDeLosDispositivos() {
    // Se leen fuera del candado propio: el dispositivo puede avisar (alCambiarElMotivo, que
    // lo toma) con el suyo tomado.
    std::vector<GObject*> ds;
    {
        auto& e = estado();
        std::lock_guard<std::mutex> l(e.mu);
        for (const auto& [o, h] : e.vigilados) ds.push_back(G_OBJECT(g_object_ref(o)));
    }
    std::string motivo;
    for (GObject* o : ds) {
        if (const std::int64_t r = entero(o, kMotivoD3D12); r != 0 && motivo.empty())
            motivo = llenar(traducir("motor", "D3D12: el dispositivo se perdió (GetDeviceRemovedReason %1)"), {hresult(r)});
        g_object_unref(o);
    }
    return motivo;
}

void alCambiarElMotivo(GObject* o, GParamSpec*, gpointer) {
    // Lo avisa GStreamer cuando una operación de D3D12 falló y GetDeviceRemovedReason dio
    // un error (en el hilo de quien falló): se anota y nada más.
    if (const std::int64_t r = entero(o, kMotivoD3D12); r != 0)
        marcarGpuPerdida(llenar(traducir("motor", "D3D12: el dispositivo se perdió (GetDeviceRemovedReason %1)"), {hresult(r)}),
                         "d3d12device");
}

/// La sospecha, aparte: ¿la GPU del motor todavía entrega? Una a la vez, como mucho cada 5 s.
void probarAparte(const std::string& error, const std::string& elemento) {
    auto& e = estado();
    {
        std::lock_guard<std::mutex> l(e.mu);
        const gint64 ahora = g_get_monotonic_time();
        if (e.ultimaPruebaUs != 0 && ahora - e.ultimaPruebaUs < 5 * G_USEC_PER_SEC) return;
        if (e.probando.exchange(true)) return;
        e.ultimaPruebaUs = ahora;
    }
    std::thread([error, elemento]() {
        ba::platform::nombrarHilo("gpu-prueba");
        if (!gpuEntrega(2000))
            marcarGpuPerdida(llenar(traducir("motor", "la GPU no entrega cuadros después de «%1»"), {error}), elemento);
        estado().probando = false;
    }).detach();
}

} // namespace

ErrorDeGpu clasificarErrorDeGpu(const std::string& fabrica, const std::string& texto) {
    const std::string t = minusculas(texto);
    // Lo que dice D3D12/DXGI y GL_KHR_robustness cuando la placa se fue: venga de quien venga
    // (también de un codificador de la placa: nvh264enc, d3d12h264enc).
    if (contiene(t, {"dxgi_error_device_removed", "dxgi_error_device_hung", "dxgi_error_device_reset", "0x887a0005", "0x887a0006",
                     "0x887a0007", "gl_context_lost", "guilty_context_reset", "innocent_context_reset", "unknown_context_reset"}))
        return ErrorDeGpu::Perdida;
    const std::string f = minusculas(fabrica);
    const bool deGpu = f.rfind("gl", 0) == 0 || f.rfind("d3d12", 0) == 0 || f.rfind("basombragl", 0) == 0 || f.rfind("basombrad3d12", 0) == 0;
    if (!deGpu) return ErrorDeGpu::Ajeno;
    // Dicho con palabras, solo de un elemento de GPU («device lost» también lo dice una
    // placa de audio que se desenchufa).
    if (contiene(t, {"device removed", "device lost", "device hung", "context lost", "context reset", "lost context"}))
        return ErrorDeGpu::Perdida;
    return ErrorDeGpu::Sospecha;
}

bool revisarErrorDeGpu(GstMessage* m) {
    if (m == nullptr || GST_MESSAGE_TYPE(m) != GST_MESSAGE_ERROR) return false;
    auto& e = estado();
    if (e.perdida.load()) return true;
    if (!gpuVideo()) return false; // en la CPU no hay GPU que perder
    GError* err = nullptr;
    gchar* dbg = nullptr;
    gst_message_parse_error(m, &err, &dbg);
    const std::string mensaje = err != nullptr ? ba::diag::utf8Valido(err->message) : std::string();
    const std::string texto = mensaje + " " + (dbg != nullptr ? ba::diag::utf8Valido(dbg) : std::string());
    if (err != nullptr) g_error_free(err);
    g_free(dbg);
    GstObject* src = GST_MESSAGE_SRC(m);
    std::string fabrica;
    if (src != nullptr && GST_IS_ELEMENT(src))
        if (GstElementFactory* f = gst_element_get_factory(GST_ELEMENT(src))) fabrica = GST_OBJECT_NAME(f);
    const std::string elemento = src != nullptr && GST_OBJECT_NAME(src) != nullptr ? GST_OBJECT_NAME(src) : fabrica;
    // D3D12: el dispositivo dice si se perdió (GetDeviceRemovedReason, por GStreamer).
    if (gpu() == Gpu::D3D12)
        if (const std::string d = motivoDeLosDispositivos(); !d.empty()) {
            marcarGpuPerdida(d, elemento);
            return true;
        }
    switch (clasificarErrorDeGpu(fabrica, texto)) {
        case ErrorDeGpu::Perdida:
            marcarGpuPerdida(mensaje.empty() ? texto : mensaje, elemento);
            return true;
        case ErrorDeGpu::Sospecha:
            probarAparte(mensaje, elemento);
            return false;
        case ErrorDeGpu::Ajeno: break;
    }
    return false;
}

bool gpuPerdida() { return estado().perdida.load(std::memory_order_relaxed); }

void marcarGpuPerdida(const std::string& motivo, const std::string& elemento) {
    auto& e = estado();
    {
        std::lock_guard<std::mutex> l(e.mu);
        if (e.perdida.load()) return; // una vez hasta el próximo rearme
        e.motivo = motivo;
        e.elemento = elemento;
        e.desdeUs = g_get_monotonic_time();
        ++e.perdidas;
        e.perdida = true;
    }
    registrar(Nivel::Error, elemento.empty() ? llenar(traducir("motor", "se perdió la GPU del video (%1): %2"), {nombreGpu(), motivo})
                                             : llenar(traducir("motor", "se perdió la GPU del video (%1, %2): %3"),
                                                      {nombreGpu(), elemento, motivo}));
}

json estadoGpu() {
    auto& e = estado();
    std::lock_guard<std::mutex> l(e.mu);
    json j{{"gpu", nombreGpu()}, {"perdida", e.perdida.load()}, {"perdidas", e.perdidas}, {"rearmes", e.rearmes}};
    if (e.perdida.load()) {
        j["motivo"] = e.motivo;
        j["elemento"] = e.elemento;
        j["hace_s"] = static_cast<double>(g_get_monotonic_time() - e.desdeUs) / 1e6;
    }
    if (e.rearmeMs >= 0) j["ultimo_rearme"] = {{"gpu", e.rearmeGpu}, {"ms", e.rearmeMs}};
    return j;
}

Gpu rearmarGpu(bool probarGpu) {
    const gint64 t0 = g_get_monotonic_time();
    const std::string antes = nombreGpu();
    soltarGpu();
    ponerGpuVideo(probarGpu); // la autoprueba de siempre, con un contexto o un dispositivo nuevo
    if (gpu() != Gpu::Ninguna && !gpuEntrega(2000)) {
        // El contexto nuevo se armó pero no entrega (el driver todavía no volvió): a la CPU.
        registrar(Nivel::Aviso, llenar(traducir("motor", "la GPU nueva (%1) no entrega cuadros: el video va a la CPU"), {nombreGpu()}));
        soltarGpu();
        ponerGpuVideo(false);
    }
    const std::int64_t ms = (g_get_monotonic_time() - t0) / 1000;
    auto& e = estado();
    {
        std::lock_guard<std::mutex> l(e.mu);
        e.perdida = false;
        ++e.rearmes;
        e.rearmeGpu = nombreGpu();
        e.rearmeMs = ms;
        e.ultimaPruebaUs = 0;
    }
    registrar(Nivel::Aviso, llenar(traducir("motor", "video del motor rearmado: %1 → %2 en %3 ms"), {antes, nombreGpu(), std::to_string(ms)}));
    return gpu();
}

std::string textoDeGpuPerdida(Gpu g) {
    switch (g) {
        case Gpu::D3D12: return "DXGI_ERROR_DEVICE_REMOVED (0x887A0005): device removed";
        case Gpu::Gl: return "GL_CONTEXT_LOST: context lost";
        default: return {};
    }
}

void vigilarDispositivos(const std::vector<GstContext*>& contextos) {
    auto& e = estado();
    std::lock_guard<std::mutex> l(e.mu);
    for (const auto& [o, h] : e.vigilados) {
        g_signal_handler_disconnect(o, h);
        g_object_unref(o);
    }
    e.vigilados.clear();
    for (GstContext* c : contextos) {
        // El del dispositivo D3D12 («gst.d3d12.device.handle») lo lleva en un campo: se
        // busca cualquier objeto con la propiedad, sin atarse al nombre del campo.
        const GstStructure* s = gst_context_get_structure(c);
        for (gint i = 0; s != nullptr && i < gst_structure_n_fields(s); ++i) {
            const GValue* v = gst_structure_get_value(s, gst_structure_nth_field_name(s, i));
            if (v == nullptr || !G_VALUE_HOLDS_OBJECT(v)) continue;
            GObject* o = static_cast<GObject*>(g_value_get_object(v));
            if (o == nullptr || g_object_class_find_property(G_OBJECT_GET_CLASS(o), kMotivoD3D12) == nullptr) continue;
            if (std::any_of(e.vigilados.begin(), e.vigilados.end(), [o](const auto& x) { return x.first == o; })) continue;
            g_object_ref(o);
            e.vigilados.emplace_back(o, g_signal_connect(o, "notify::device-removed-reason", G_CALLBACK(alCambiarElMotivo), nullptr));
        }
    }
    if (!contextos.empty())
        registrar(Nivel::Info, e.vigilados.empty()
                                   ? traducir("motor", "D3D12: el dispositivo no avisa si se pierde (GStreamer sin «device-removed-reason»): "
                                                       "se detecta por los errores")
                                   : llenar(traducir("motor", "D3D12: vigilando %1 dispositivo(s) por si se pierden"),
                                            {std::to_string(e.vigilados.size())}));
}

} // namespace ba::motor
