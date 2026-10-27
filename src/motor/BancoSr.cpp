// ============================================================================
// src/motor/BancoSr.cpp — ver BancoSr.hpp
// ============================================================================
#include "BancoSr.hpp"
#include "diag/Utf8.hpp"
#include "i18n/I18n.hpp"

#include <gst/gst.h>

#include <algorithm>
#include <chrono>
#include <map>
#include <mutex>
#include <thread>

namespace ba::motor {

using ba::i18n::llenar;
using ba::i18n::traducir;

namespace {

struct Sondas {
    std::mutex mu;
    std::map<GstClockTime, gint64> entrada; ///< PTS -> cuándo entró
    std::vector<double> ms;                 ///< lo que tardó cada uno (entrada -> salida)
    std::atomic<int> salidos{0};
    std::atomic<gint64> primero{0};         ///< cuándo salió el primero
    std::atomic<bool> midiendo{false};
};

GstPadProbeReturn alEntrar(GstPad*, GstPadProbeInfo* info, gpointer d) {
    auto* s = static_cast<Sondas*>(d);
    GstBuffer* b = GST_PAD_PROBE_INFO_BUFFER(info);
    if (b == nullptr || !GST_BUFFER_PTS_IS_VALID(b)) return GST_PAD_PROBE_OK;
    std::lock_guard<std::mutex> l(s->mu);
    s->entrada[GST_BUFFER_PTS(b)] = g_get_monotonic_time();
    while (s->entrada.size() > 256) s->entrada.erase(s->entrada.begin());
    return GST_PAD_PROBE_OK;
}

GstPadProbeReturn alSalir(GstPad*, GstPadProbeInfo* info, gpointer d) {
    auto* s = static_cast<Sondas*>(d);
    GstBuffer* b = GST_PAD_PROBE_INFO_BUFFER(info);
    if (b == nullptr) return GST_PAD_PROBE_OK;
    const gint64 ahora = g_get_monotonic_time();
    gint64 cero = 0;
    s->primero.compare_exchange_strong(cero, ahora);
    if (!s->midiendo.load()) return GST_PAD_PROBE_OK;
    ++s->salidos;
    if (!GST_BUFFER_PTS_IS_VALID(b)) return GST_PAD_PROBE_OK;
    std::lock_guard<std::mutex> l(s->mu);
    const auto it = s->entrada.find(GST_BUFFER_PTS(b));
    if (it != s->entrada.end()) {
        s->ms.push_back(static_cast<double>(ahora - it->second) / 1000.0);
        s->entrada.erase(s->entrada.begin(), std::next(it));
    }
    return GST_PAD_PROBE_OK;
}

std::string leerTexto(GstElement* e, const char* prop) {
    if (g_object_class_find_property(G_OBJECT_GET_CLASS(e), prop) == nullptr) return {};
    gchar* x = nullptr;
    g_object_get(e, prop, &x, nullptr);
    std::string r = x != nullptr ? x : "";
    g_free(x);
    return r;
}

double leerMs(GstElement* e) {
    if (g_object_class_find_property(G_OBJECT_GET_CLASS(e), "ms-por-cuadro") == nullptr) return 0;
    gdouble ms = 0;
    g_object_get(e, "ms-por-cuadro", &ms, nullptr);
    return ms;
}

/// {cuadros, suma de ms} de la red del elemento; {-1, 0} si no lo publica.
std::pair<long long, double> leerTotales(GstElement* e) {
    if (g_object_class_find_property(G_OBJECT_GET_CLASS(e), "cuadros-red") == nullptr ||
        g_object_class_find_property(G_OBJECT_GET_CLASS(e), "ms-suma") == nullptr)
        return {-1, 0};
    guint64 n = 0;
    gdouble suma = 0;
    g_object_get(e, "cuadros-red", &n, "ms-suma", &suma, nullptr);
    return {static_cast<long long>(n), suma};
}

std::string conComillas(const std::string& v) {
    std::string o = "\"";
    for (char c : v) {
        if (c == '"' || c == '\\') o += '\\';
        o += c;
    }
    return o + "\"";
}

} // namespace

MedidaSr medirSuperRes(const CandidatoSr& c, int ancho, int alto, int fpsN, int fpsD, double segundos, double esperaMax,
                       const std::atomic<bool>* cancelar) {
    MedidaSr m;
    GstElementFactory* f = gst_element_factory_find(c.elemento.c_str());
    if (f == nullptr) {
        m.error = llenar(traducir("motor", "GStreamer no tiene el elemento «%1»"), {c.elemento});
        return m;
    }
    gst_object_unref(f);
    std::string props;
    for (const auto& [k, v] : c.propiedades) props += " " + k + "=" + conComillas(v);
    const std::string desc = "videotestsrc is-live=true pattern=ball ! video/x-raw,format=NV12,width=" +
                             std::to_string(ancho) + ",height=" + std::to_string(alto) + ",framerate=" +
                             std::to_string(fpsN) + "/" + std::to_string(std::max(1, fpsD)) +
                             " ! " + c.elemento + " name=sr" + props + " ! fakesink sync=false";
    GError* err = nullptr;
    GstElement* p = gst_parse_launch(desc.c_str(), &err);
    if (err != nullptr) {
        m.error = ba::diag::utf8Valido(err->message);
        g_error_free(err);
        if (p != nullptr) gst_object_unref(p);
        return m;
    }
    GstElement* sr = gst_bin_get_by_name(GST_BIN(p), "sr");
    Sondas s;
    GstPad* entra = gst_element_get_static_pad(sr, "sink");
    GstPad* sale = gst_element_get_static_pad(sr, "src");
    if (entra != nullptr) gst_pad_add_probe(entra, GST_PAD_PROBE_TYPE_BUFFER, alEntrar, &s, nullptr);
    if (sale != nullptr) gst_pad_add_probe(sale, GST_PAD_PROBE_TYPE_BUFFER, alSalir, &s, nullptr);
    GstBus* bus = gst_element_get_bus(p);
    const auto cancelado = [&] { return cancelar != nullptr && cancelar->load(); };
    const auto errorDelBus = [&]() -> bool {
        while (GstMessage* msg = gst_bus_pop_filtered(bus, static_cast<GstMessageType>(GST_MESSAGE_ERROR))) {
            GError* e = nullptr;
            gst_message_parse_error(msg, &e, nullptr);
            m.error = e != nullptr ? ba::diag::utf8Valido(e->message) : traducir("motor", "error");
            if (e != nullptr) g_error_free(e);
            gst_message_unref(msg);
            return true;
        }
        return false;
    };
    const gint64 t0 = g_get_monotonic_time();
    if (gst_element_set_state(p, GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE) m.error = traducir("motor", "el pipeline no arrancó");
    // Hasta que la red procese su primer cuadro (abrir o compilar la red), o, en
    // uno sincrónico, hasta que salga el primero.
    const bool porRed = !c.sincronico && leerTotales(sr).first >= 0;
    const auto listo = [&] { return porRed ? leerTotales(sr).first > 0 : s.primero.load() != 0; };
    gint64 conError = 0; // desde cuándo el elemento dice que no anda
    while (m.error.empty() && !cancelado() && !listo() && g_get_monotonic_time() - t0 < static_cast<gint64>(esperaMax * 1e6)) {
        if (errorDelBus()) break;
        // Si el elemento ya dijo por qué no anda (una red sin variante para este
        // tamaño, un tamaño que el sistema no acepta), no se espera el resto.
        if (!leerTexto(sr, "error").empty()) {
            if (conError == 0) conError = g_get_monotonic_time();
            if (g_get_monotonic_time() - conError > 2000000) break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    if (m.error.empty() && !cancelado() && !listo()) {
        const std::string e = leerTexto(sr, "error");
        m.error = !e.empty() ? e
                             : llenar(porRed ? traducir("motor", "la red no procesó ningún cuadro en %1 s")
                                             : traducir("motor", "no salió ningún cuadro en %1 s"),
                                      {std::to_string(static_cast<int>(esperaMax))});
    }
    if (m.error.empty()) m.abrirSeg = static_cast<double>(g_get_monotonic_time() - t0) / 1e6;
    // Un segundo para que se asiente (el segundo cuadro de una red recién abierta
    // también suele ser lento).
    if (porRed && m.error.empty() && !cancelado()) std::this_thread::sleep_for(std::chrono::seconds(1));
    const auto [n0, s0] = leerTotales(sr);
    // Medir.
    if (m.error.empty() && !cancelado()) {
        {
            std::lock_guard<std::mutex> l(s.mu);
            s.ms.clear();
        }
        s.midiendo = true;
        const gint64 t1 = g_get_monotonic_time();
        while (!cancelado() && g_get_monotonic_time() - t1 < static_cast<gint64>(segundos * 1e6)) {
            if (errorDelBus()) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        s.midiendo = false;
        m.cuadros = s.salidos.load();
        const auto [n1, s1] = leerTotales(sr);
        if (porRed) {
            if (n1 > n0) {
                m.ms = (s1 - s0) / static_cast<double>(n1 - n0);
                m.cuadros = static_cast<int>(n1 - n0);
                m.como = "red";
            } else {
                const std::string e = leerTexto(sr, "error");
                m.error = e.empty() ? llenar(traducir("motor", "la red no terminó ningún cuadro en %1 s"),
                                             {std::to_string(static_cast<int>(segundos))})
                                    : llenar(traducir("motor", "la red no terminó ningún cuadro en %1 s: %2"),
                                             {std::to_string(static_cast<int>(segundos)), e});
            }
        } else if (!c.sincronico && leerMs(sr) > 0) {
            m.ms = leerMs(sr); // un elemento propio que solo da el promedio
            m.como = "elemento";
        } else {
            std::lock_guard<std::mutex> l(s.mu);
            if (!s.ms.empty()) {
                std::vector<double> v = s.ms;
                std::nth_element(v.begin(), v.begin() + static_cast<long>(v.size() / 2), v.end());
                m.ms = v[v.size() / 2];
                m.como = "sondas";
            }
        }
        m.acelerador = leerTexto(sr, "acelerador");
        const std::string e = leerTexto(sr, "error");
        if (!e.empty() && m.ms <= 0) m.error = e;
    }
    if (cancelado() && m.error.empty()) m.error = "cancelado";
    gst_element_set_state(p, GST_STATE_NULL);
    if (entra != nullptr) gst_object_unref(entra);
    if (sale != nullptr) gst_object_unref(sale);
    gst_object_unref(bus);
    gst_object_unref(sr);
    gst_object_unref(p);
    return m;
}

} // namespace ba::motor
