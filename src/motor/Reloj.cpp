// ============================================================================
// src/motor/Reloj.cpp — ver Reloj.hpp
// ============================================================================
#include "Reloj.hpp"
#include "diag/Utf8.hpp"
#include "Mapa.hpp"
#include "Piezas.hpp"

#include "platform/GstEntorno.hpp"

#include <gst/base/gstbasetransform.h>
#include <gst/net/net.h>

#include <algorithm>
#include <cstdio>

namespace ba::motor {

namespace {
std::mutex g_regMu;
std::function<void(Nivel, const std::string&)> g_registro;
} // namespace

bool iniciarGStreamer(std::string* error) {
    static std::once_flag una;
    static bool ok = false;
    static std::string err;
    std::call_once(una, []() {
        if (gst_is_initialized()) {
            ok = true;
            serializarConvertidoresD3D12();
            return;
        }
        if (!ba::platform::prepararGst()) {
            err = "GStreamer no está en el programa";
            return;
        }
        GError* e = nullptr;
        ok = gst_init_check(nullptr, nullptr, &e);
        if (!ok) err = e != nullptr ? ba::diag::utf8Valido(e->message) : "gst_init falló";
        if (e != nullptr) g_error_free(e);
        if (ok) {
            quitarPluginsDelSistema();
            serializarConvertidoresD3D12();
        }
    });
    if (!ok && error != nullptr) *error = err;
    return ok;
}

void quitarPluginsDelSistema() {
    GstRegistry* reg = gst_registry_get();
    for (const char* n : {"directshow"}) {
        if (GstPlugin* p = gst_registry_find_plugin(reg, n)) {
            gst_registry_remove_plugin(reg, p);
            gst_object_unref(p);
        }
    }
}

namespace {
using SetCaps = gboolean (*)(GstBaseTransform*, GstCaps*, GstCaps*);
struct ConvertidorD3D12 {
    const char* fabrica;
    GType tipo = 0;
    SetCaps original = nullptr;
};
// Se llenan una vez (serializarConvertidoresD3D12), antes de que exista ninguno.
ConvertidorD3D12 g_d3d12[] = {{"d3d12convert"}, {"d3d12scale"}, {"d3d12colorconvert"}};
std::recursive_mutex g_d3d12Mu;

gboolean setCapsDeAUno(GstBaseTransform* t, GstCaps* entra, GstCaps* sale) {
    for (const auto& c : g_d3d12)
        if (c.original != nullptr && G_OBJECT_TYPE(t) == c.tipo) {
            std::lock_guard<std::recursive_mutex> l(g_d3d12Mu);
            return c.original(t, entra, sale);
        }
    return FALSE;
}
} // namespace

void serializarConvertidoresD3D12() {
    static std::once_flag una;
    std::call_once(una, []() {
        for (auto& c : g_d3d12) {
            GstElementFactory* f = gst_element_factory_find(c.fabrica);
            if (f == nullptr) continue; // sin D3D12 (macOS, Linux)
            auto* cargada = GST_ELEMENT_FACTORY(gst_plugin_feature_load(GST_PLUGIN_FEATURE(f)));
            gst_object_unref(f);
            if (cargada == nullptr) continue;
            const GType tipo = gst_element_factory_get_element_type(cargada);
            gst_object_unref(cargada);
            if (tipo == 0 || !g_type_is_a(tipo, GST_TYPE_BASE_TRANSFORM)) continue;
            // La clase queda viva (la referencia no se suelta): el puntero cambiado dura.
            auto* k = static_cast<GstBaseTransformClass*>(g_type_class_ref(tipo));
            if (k->set_caps == nullptr || k->set_caps == setCapsDeAUno) continue;
            c.tipo = tipo;
            c.original = k->set_caps;
            k->set_caps = setCapsDeAUno;
        }
    });
}

void ponerRegistro(std::function<void(Nivel, const std::string&)> f) {
    std::lock_guard<std::mutex> l(g_regMu);
    g_registro = std::move(f);
}

void registrar(Nivel n, const std::string& texto) {
    std::lock_guard<std::mutex> l(g_regMu);
    if (g_registro) {
        g_registro(n, texto);
        return;
    }
    std::fprintf(stderr, "[motor] %s%s\n", n == Nivel::Error ? "ERROR: " : n == Nivel::Aviso ? "aviso: " : "",
                 texto.c_str());
}

Reloj::Reloj(const std::string& tipo, const std::string& servidor, int puerto, int dominio) {
    if (tipo.empty() || tipo == "sistema") {
        clock_ = gst_system_clock_obtain(); // monotónico
    } else if (tipo == "ntp") {
        if (servidor.empty())
            error_ = "falta el servidor NTP";
        else
            clock_ = gst_ntp_clock_new("ba-ntp", servidor.c_str(), puerto > 0 ? puerto : 123, 0);
    } else if (tipo == "red") {
        if (servidor.empty())
            error_ = "falta el aparato que publica el reloj";
        else
            clock_ = gst_net_client_clock_new("ba-red", servidor.c_str(), puerto > 0 ? puerto : 8554, 0);
    } else if (tipo == "ptp") {
        static bool ptpIniciado = false;
        if (!ptpIniciado) ptpIniciado = gst_ptp_init(GST_PTP_CLOCK_ID_NONE, nullptr);
        if (!ptpIniciado)
            error_ = "PTP no arrancó: necesita gst-ptp-helper con permisos para los puertos 319/320";
        else
            clock_ = gst_ptp_clock_new("ba-ptp", static_cast<guint>(std::max(0, dominio)));
    } else {
        error_ = "reloj desconocido «" + tipo + "» (sistema, ntp, ptp o red)";
    }
    if (clock_ == nullptr) {
        if (error_.empty()) error_ = "no se pudo crear el reloj " + tipo;
        registrar(Nivel::Error, error_ + ": se usa el del sistema");
        clock_ = gst_system_clock_obtain();
    }
    base_ = gst_clock_get_time(clock_);
}

Reloj::~Reloj() {
    if (clock_ != nullptr) gst_object_unref(clock_);
}

GstClockTime Reloj::ahora() const {
    const GstClockTime t = gst_clock_get_time(clock_);
    return t > base_ ? t - base_ : 0;
}

void Reloj::adoptar(GstElement* pipeline) const {
    registrarPipeline(pipeline); // para el mapa (Mapa.hpp)
    darContextoGpu(pipeline);    // el GL del motor, el mismo para todos
    gst_pipeline_use_clock(GST_PIPELINE(pipeline), clock_);
    gst_element_set_start_time(pipeline, GST_CLOCK_TIME_NONE); // el base_time no lo cambia solo
    gst_element_set_base_time(pipeline, base_);
}

void Reloj::ponerTope(std::int64_t ms) {
    std::lock_guard<std::mutex> l(mu_);
    topeMs_ = ms < 0 ? -1 : ms;
}

std::int64_t Reloj::topeMs() const {
    std::lock_guard<std::mutex> l(mu_);
    return topeMs_ >= 0 ? topeMs_ : kTopeAutomaticoMs;
}

bool Reloj::topeAutomatico() const {
    std::lock_guard<std::mutex> l(mu_);
    return topeMs_ < 0;
}

bool Reloj::llegaTarde(std::int64_t ns) const { return ns > topeMs() * static_cast<std::int64_t>(GST_MSECOND); }

std::int64_t Reloj::anotar(const std::string& quien, std::int64_t ns) {
    {
        std::lock_guard<std::mutex> l(mu_);
        pedidas_[quien] = std::max<std::int64_t>(0, ns);
    }
    return latencia();
}

void Reloj::olvidar(const std::string& quien) {
    std::lock_guard<std::mutex> l(mu_);
    pedidas_.erase(quien);
}

std::int64_t Reloj::latencia() const {
    const std::int64_t tope = topeMs() * static_cast<std::int64_t>(GST_MSECOND);
    std::lock_guard<std::mutex> l(mu_);
    std::int64_t m = 0;
    for (const auto& [x, v] : pedidas_)
        if (v <= tope) m = std::max(m, v); // las que pasan el tope no arrastran
    return m;
}

std::map<std::string, std::int64_t> Reloj::anotadas() const {
    std::lock_guard<std::mutex> l(mu_);
    return pedidas_;
}

} // namespace ba::motor
