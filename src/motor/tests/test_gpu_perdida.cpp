// test_gpu_perdida.cpp — la GPU del video que se pierde (F3-09, GpuPerdida.hpp)
//
//   1. Qué dice cada error (sin GStreamer): DXGI_ERROR_DEVICE_REMOVED y GL_CONTEXT_LOST
//      vengan de quien vengan; «device lost» solo de un elemento de GPU (una placa de
//      audio también lo dice); un error cualquiera de un elemento de GPU es sospecha; uno
//      de otro elemento, ajeno.
//   2. D3D12 sin Windows: un objeto con «device-removed-reason» (como el GstD3D12Device)
//      en un contexto: cuando la propiedad pasa a 0x887A0005 (lo que hace GStreamer al
//      ver GetDeviceRemovedReason), la GPU queda perdida.
//   3. Con la GPU del motor (GL en la Mac; D3D12 en Windows):
//      - la GPU sana entrega (gpuEntrega);
//      - un error sin motivo de un elemento de GPU: se prueba aparte y, como entrega,
//        NO se marca perdida (sin falsos positivos);
//      - la falla inyectada (diag_falla video.gpu) en el programa al aire: perdida en
//        menos de un segundo y en el estado del motor;
//      - el rearme como lo hace la app (apagar, rearmarGpu, armar de nuevo): el primer
//        cuadro real vuelve; se mide lo que dura el hueco (lo que el emisor cubre con su
//        placa, F3-01) y tiene que ser < 3 s;
//      - la caída a la CPU (rearmarGpu sin GPU): el motor arma y entrega igual.

#include "GpuPerdida.hpp"
#include "Motor.hpp"
#include "Piezas.hpp"
#include "fault/FallasProgramadas.hpp"

#include <gst/gst.h>

#include <chrono>
#include <cstdio>
#include <thread>

using namespace ba::motor;
using json = nlohmann::json;

namespace {
int fallos = 0;
#define CHECK(cond, msg)                                                             \
    do {                                                                             \
        if (!(cond)) {                                                               \
            std::printf("FALLO: %s (%s, linea %d)\n", msg, #cond, __LINE__);         \
            ++fallos;                                                                \
        }                                                                            \
    } while (0)

using Hora = std::chrono::steady_clock;
double ms(Hora::time_point a, Hora::time_point b) { return std::chrono::duration<double, std::milli>(b - a).count(); }
void esperar(int n) { std::this_thread::sleep_for(std::chrono::milliseconds(n)); }

bool esColor(const Cuadro& c, int r, int g, int b) {
    if (c.bgra.empty() || c.ancho <= 0) return false;
    const std::uint8_t* p = c.bgra.data() + static_cast<std::size_t>(c.alto / 2) * c.stride + static_cast<std::size_t>(c.ancho / 2) * 4;
    return std::abs(p[2] - r) < 40 && std::abs(p[1] - g) < 40 && std::abs(p[0] - b) < 40;
}

/// Un motor con una escena de un color al aire y el programa a la vista.
struct Estudio {
    Motor m;
    explicit Estudio(Motor::Config c) : m(std::move(c)) {
        CHECK(m.iniciar().empty(), "el motor arranca");
        CHECK(m.agregarEntrada("fondo", json{{"tipo", "color"}, {"color", "#20C040"}, {"ancho", 640}, {"alto", 360}}).empty(), "el fondo");
        m.definirEscena({{"nombre", "Al aire"}, {"capas", {{{"entrada", "fondo"}}}}});
        m.alAire("Al aire");
        CHECK(m.verPedir("programa", 320, 180).empty(), "ver el programa");
    }
    /// Hasta que el programa muestra la escena (o el tope): cuándo.
    bool primerCuadro(int topeMs = 8000) {
        Cuadro c;
        std::uint64_t serie = 0;
        const auto tope = Hora::now() + std::chrono::milliseconds(topeMs);
        while (Hora::now() < tope) {
            if (m.verUltimo("programa", c, serie) && esColor(c, 0x20, 0xC0, 0x40)) return true;
            esperar(2);
        }
        return false;
    }
};

Motor::Config config() {
    Motor::Config c;
    c.lienzo.ancho = 640;
    c.lienzo.alto = 360;
    c.prefijoAnillos = "";
    c.sinkEscucha = "fakesink";
    return c;
}

// --- Un «dispositivo D3D12» de mentira: solo la propiedad que mira el motor ------------------
struct FalsoDisp {
    GObject parent;
    gint motivo;
};
struct FalsoDispClass {
    GObjectClass parent_class;
};
G_DEFINE_TYPE(FalsoDisp, falso_disp, G_TYPE_OBJECT)
void falso_disp_init(FalsoDisp* d) { d->motivo = 0; }
void falsoPoner(GObject* o, guint, const GValue* v, GParamSpec*) { reinterpret_cast<FalsoDisp*>(o)->motivo = g_value_get_int(v); }
void falsoLeer(GObject* o, guint, GValue* v, GParamSpec*) { g_value_set_int(v, reinterpret_cast<FalsoDisp*>(o)->motivo); }
void falso_disp_class_init(FalsoDispClass* k) {
    auto* o = G_OBJECT_CLASS(k);
    o->set_property = falsoPoner;
    o->get_property = falsoLeer;
    g_object_class_install_property(o, 1, g_param_spec_int("device-removed-reason", "", "", G_MININT, G_MAXINT, 0, G_PARAM_READWRITE));
}

GstMessage* errorDe(GstElement* e, const char* texto) {
    GError* err = g_error_new_literal(GST_STREAM_ERROR, GST_STREAM_ERROR_FAILED, texto);
    GstMessage* m = gst_message_new_error(GST_OBJECT(e), err, "prueba");
    g_error_free(err);
    return m;
}
} // namespace

int main() {
#if defined(BA_TEST_GST_RAIZ)
    g_setenv("BA_GST_RAIZ", BA_TEST_GST_RAIZ, FALSE);
#endif
    // 1. Qué dice cada error.
    CHECK(clasificarErrorDeGpu("d3d12convert", "Failed: DXGI_ERROR_DEVICE_REMOVED") == ErrorDeGpu::Perdida, "DXGI device removed");
    CHECK(clasificarErrorDeGpu("nvh264enc", "hr 0x887a0005") == ErrorDeGpu::Perdida, "el código, de un codificador de la placa");
    CHECK(clasificarErrorDeGpu("glvideomixer", "GL_CONTEXT_LOST") == ErrorDeGpu::Perdida, "GL context lost");
    CHECK(clasificarErrorDeGpu("glupload", "Context lost while uploading") == ErrorDeGpu::Perdida, "con palabras, de un elemento GL");
    CHECK(clasificarErrorDeGpu("osxaudiosrc", "device lost") == ErrorDeGpu::Ajeno, "«device lost» de una placa de audio: ajeno");
    CHECK(clasificarErrorDeGpu("d3d12compositor", "Internal data stream error") == ErrorDeGpu::Sospecha, "de GPU sin motivo: sospecha");
    CHECK(clasificarErrorDeGpu("x264enc", "Internal data stream error") == ErrorDeGpu::Ajeno, "de otro: ajeno");

    std::string err;
    if (!iniciarGStreamer(&err)) {
        std::printf("SKIP (el resto): sin GStreamer (%s)\n", err.c_str());
        return fallos == 0 ? 77 : 1;
    }
    ponerGpuVideo(true);
    const Gpu g = gpu();
    std::printf("GPU del motor: %s\n", nombreGpu());

    // 2. D3D12 sin Windows: el aviso del dispositivo.
    {
        auto* d = static_cast<GObject*>(g_object_new(falso_disp_get_type(), nullptr));
        GstContext* c = gst_context_new("gst.d3d12.device.handle", TRUE);
        gst_structure_set(gst_context_writable_structure(c), "device", G_TYPE_OBJECT, d, nullptr);
        vigilarDispositivos({c});
        CHECK(!gpuPerdida(), "el dispositivo anda");
        g_object_set(d, "device-removed-reason", static_cast<gint>(0x887A0005u), nullptr);
        CHECK(gpuPerdida(), "el dispositivo dice que se perdió: perdida");
        const json e = estadoGpu();
        std::printf("estado: %s\n", e.dump().c_str());
        CHECK(e.value("motivo", "").find("0x887A0005") != std::string::npos, "con el motivo de GetDeviceRemovedReason");
        vigilarDispositivos({});
        gst_context_unref(c);
        g_object_unref(d);
        rearmarGpu(g != Gpu::Ninguna); // limpia la marca y vuelve a la GPU de antes
        CHECK(!gpuPerdida() && gpu() == g, "rearmada, con la GPU de antes");
    }

    if (g == Gpu::Ninguna) {
        std::printf("SKIP (3): el motor no tiene GPU acá\n");
        if (fallos == 0) std::printf("OK\n");
        return fallos == 0 ? 0 : 1;
    }

    // 3a. La GPU sana entrega; un error sin motivo de un elemento de GPU no la marca.
    CHECK(gpuEntrega(), "la GPU sana entrega");
    {
        GstElement* mx = gst_element_factory_make(g == Gpu::D3D12 ? "d3d12compositor" : "glvideomixer", nullptr);
        gst_object_ref_sink(mx);
        GstMessage* m = errorDe(mx, "Internal data stream error");
        CHECK(!revisarErrorDeGpu(m), "una sospecha no la marca enseguida");
        gst_message_unref(m);
        esperar(2500); // la prueba aparte
        CHECK(!gpuPerdida(), "la GPU entrega: no se perdió (sin falsos positivos)");
        gst_object_unref(mx);
    }

    // 3b. La falla inyectada en el programa al aire.
    double hueco = 0;
    {
        auto est = std::make_unique<Estudio>(config());
        CHECK(est->primerCuadro(), "el programa muestra la escena");
        CHECK(ba::fault::FallasProgramadas::instancia().programar("video", "gpu", 0).empty(), "programar la GPU perdida");
        const auto t0 = Hora::now();
        while (!gpuPerdida() && ms(t0, Hora::now()) < 3000) esperar(5);
        const auto tDetectada = Hora::now();
        CHECK(gpuPerdida(), "la GPU quedó perdida");
        std::printf("detectada en %.0f ms\n", ms(t0, tDetectada));
        CHECK(ms(t0, tDetectada) < 1000, "en menos de un segundo");
        {
            const json e = est->m.estado();
            CHECK(e["gpu"].value("perdida", false), "y en el estado del motor");
            std::printf("estado del motor: %s\n", e["gpu"].dump().c_str());
        }
        // 3c. El rearme, como la app: apagar, GPU nueva, armar de nuevo.
        est.reset();
        const Gpu nueva = rearmarGpu();
        CHECK(nueva == g, "la GPU nueva es de la misma clase");
        CHECK(!gpuPerdida(), "sin la marca");
        est = std::make_unique<Estudio>(config());
        CHECK(est->primerCuadro(), "el programa vuelve con la GPU nueva");
        const auto tVuelve = Hora::now();
        hueco = ms(t0, tVuelve);
        std::printf("hueco del programa (falla → primer cuadro real con la GPU nueva): %.0f ms (rearme %.0f ms)\n", hueco,
                    ms(tDetectada, tVuelve));
        CHECK(hueco < 3000, "menos de 3 s de placa");
        CHECK(estadoGpu().value("rearmes", 0) >= 2, "los rearmes se cuentan");
    }

    // 3d. A la CPU (la GPU nueva no anda).
    {
        const auto t0 = Hora::now();
        CHECK(rearmarGpu(false) == Gpu::Ninguna, "sin GPU: a la CPU");
        auto est = std::make_unique<Estudio>(config());
        CHECK(est->primerCuadro(), "el motor en la CPU entrega igual");
        std::printf("a la CPU: primer cuadro en %.0f ms\n", ms(t0, Hora::now()));
        est.reset();
        CHECK(rearmarGpu() == g, "y de vuelta a la GPU");
    }

    if (fallos == 0) std::printf("OK\n");
    return fallos == 0 ? 0 : 1;
}
