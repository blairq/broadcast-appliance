// test_placa_que_se_va.cpp — la placa de audio que se desenchufa y vuelve (F1-16), sin sonido.
//
// Un proveedor de dispositivos FALSO registrado en GStreamer (como los de la placa: el
// mismo GstDeviceMonitor que usa el Vigía lo ve) con un micrófono y unos auriculares
// virtuales. El elemento de cada uno tiene la propiedad del id de siempre
// (`unique-id` en macOS, `device` en el resto) y el de salida es un sumidero mudo que
// no arranca si su placa no está y da error si se la llevan andando. Los proveedores
// de verdad quedan apagados (GST_PLUGIN_FEATURE_RANK): nada abre una placa real ni
// suena nada.
//
//   - ENTRADA: el micrófono (un tono sintético detrás) se va → la entrada queda
//     esperándolo, su pico cae a −100 y el disparador `audio_silence` salta; vuelve →
//     se rearma sola, suena de nuevo y el disparador se rearma (salta otra vez si se va).
//   - ESCUCHA: los auriculares elegidos se van con la escucha andando (el sumidero da
//     error) → se rearma por la salida del sistema (en_respaldo); vuelven → regresa a
//     ellos sin que nadie toque nada.
//   - MATRIZ: una salida virtual de la Matriz es, en el motor (F3-08), una escucha SIN
//     respaldo: su placa se va → muda (no a la del sistema: un cable virtual que se fue
//     no termina en los parlantes); vuelve → sale por ella otra vez, sola.
#include "agent/AlertEngine.hpp"

#include "Entrada.hpp"
#include "Escucha.hpp"
#include "Mezcla.hpp"
#include "Piezas.hpp"
#include "Reloj.hpp"

#include <gst/gst.h>

#include <QCoreApplication>
#include <QTemporaryDir>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <functional>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

using namespace ba::motor;
using json = nlohmann::json;

namespace {
int g_fallas = 0;
#define CHECK(c, m)                                                   \
    do {                                                              \
        if (!(c)) {                                                   \
            std::printf("FALLO: %s (linea %d)\n", m, __LINE__);       \
            ++g_fallas;                                               \
        } else {                                                      \
            std::printf("ok: %s\n", m);                               \
        }                                                             \
    } while (0)

// --- La placa falsa: un bin con la propiedad del id y un fakesink adentro -------------

std::mutex g_mu;
std::set<std::string> g_enchufadas;       ///< las placas que «están»
std::set<GstElement*> g_sumiderosVivos;   ///< los que arrancaron (para darles error al irse)

struct PlacaFalsa {
    GstBin parent;
    gchar* id;
};
struct PlacaFalsaClass {
    GstBinClass parent_class;
};
GType placa_falsa_get_type();
G_DEFINE_TYPE(PlacaFalsa, placa_falsa, GST_TYPE_BIN)

void placa_falsa_set(GObject* o, guint, const GValue* v, GParamSpec*) {
    auto* p = reinterpret_cast<PlacaFalsa*>(o);
    g_free(p->id);
    p->id = g_value_dup_string(v);
}
void placa_falsa_get(GObject* o, guint, GValue* v, GParamSpec*) { g_value_set_string(v, reinterpret_cast<PlacaFalsa*>(o)->id); }
void placa_falsa_finalize(GObject* o) {
    g_free(reinterpret_cast<PlacaFalsa*>(o)->id);
    G_OBJECT_CLASS(placa_falsa_parent_class)->finalize(o);
}
GstStateChangeReturn placa_falsa_cambio(GstElement* e, GstStateChange t) {
    auto* p = reinterpret_cast<PlacaFalsa*>(e);
    if (t == GST_STATE_CHANGE_NULL_TO_READY && p->id != nullptr) {
        std::lock_guard<std::mutex> l(g_mu);
        if (!g_enchufadas.count(p->id)) return GST_STATE_CHANGE_FAILURE; // como una placa que no está
    }
    const GstStateChangeReturn r = GST_ELEMENT_CLASS(placa_falsa_parent_class)->change_state(e, t);
    std::lock_guard<std::mutex> l(g_mu);
    if (t == GST_STATE_CHANGE_READY_TO_PAUSED) g_sumiderosVivos.insert(e);
    if (t == GST_STATE_CHANGE_PAUSED_TO_READY) g_sumiderosVivos.erase(e);
    return r;
}
void placa_falsa_class_init(PlacaFalsaClass* k) {
    auto* go = G_OBJECT_CLASS(k);
    go->set_property = placa_falsa_set;
    go->get_property = placa_falsa_get;
    go->finalize = placa_falsa_finalize;
    g_object_class_install_property(go, 1, g_param_spec_string(propiedadDeDispositivo(), "id", "id de la placa", nullptr,
                                                               static_cast<GParamFlags>(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
    GST_ELEMENT_CLASS(k)->change_state = placa_falsa_cambio;
    static GstStaticPadTemplate entra = GST_STATIC_PAD_TEMPLATE("sink", GST_PAD_SINK, GST_PAD_ALWAYS, GST_STATIC_CAPS_ANY);
    gst_element_class_add_static_pad_template(GST_ELEMENT_CLASS(k), &entra);
    gst_element_class_set_static_metadata(GST_ELEMENT_CLASS(k), "Placa falsa", "Sink/Audio", "pruebas", "ba");
}
void placa_falsa_init(PlacaFalsa* p) {
    p->id = nullptr;
    GstElement* fs = gst_element_factory_make("fakesink", nullptr);
    g_object_set(fs, "sync", TRUE, nullptr);
    gst_bin_add(GST_BIN(p), fs);
    GstPad* sp = gst_element_get_static_pad(fs, "sink");
    gst_element_add_pad(GST_ELEMENT(p), gst_ghost_pad_new("sink", sp));
    gst_object_unref(sp);
    GST_OBJECT_FLAG_SET(p, GST_ELEMENT_FLAG_SINK);
}

// --- El dispositivo y su proveedor --------------------------------------------------

struct DispositivoFalso {
    GstDevice parent;
    gchar* id;
};
struct DispositivoFalsoClass {
    GstDeviceClass parent_class;
};
GType dispositivo_falso_get_type();
G_DEFINE_TYPE(DispositivoFalso, dispositivo_falso, GST_TYPE_DEVICE)
GstElement* dispositivo_falso_crear(GstDevice* d, const gchar* nombre) {
    GstElement* e = GST_ELEMENT(g_object_new(placa_falsa_get_type(), "name", nombre, nullptr));
    g_object_set(e, propiedadDeDispositivo(), reinterpret_cast<DispositivoFalso*>(d)->id, nullptr);
    return e;
}
void dispositivo_falso_finalize(GObject* o) {
    g_free(reinterpret_cast<DispositivoFalso*>(o)->id);
    G_OBJECT_CLASS(dispositivo_falso_parent_class)->finalize(o);
}
void dispositivo_falso_class_init(DispositivoFalsoClass* k) {
    G_OBJECT_CLASS(k)->finalize = dispositivo_falso_finalize;
    GST_DEVICE_CLASS(k)->create_element = dispositivo_falso_crear;
}
void dispositivo_falso_init(DispositivoFalso* d) { d->id = nullptr; }

struct ProveedorFalso {
    GstDeviceProvider parent;
};
struct ProveedorFalsoClass {
    GstDeviceProviderClass parent_class;
};
GType proveedor_falso_get_type();
G_DEFINE_TYPE(ProveedorFalso, proveedor_falso, GST_TYPE_DEVICE_PROVIDER)
std::atomic<GstDeviceProvider*> g_proveedor{nullptr};
gboolean proveedor_falso_start(GstDeviceProvider*) { return TRUE; } // vigila: avisa con device_add/remove
void proveedor_falso_class_init(ProveedorFalsoClass* k) {
    GST_DEVICE_PROVIDER_CLASS(k)->start = proveedor_falso_start;
    gst_device_provider_class_set_static_metadata(GST_DEVICE_PROVIDER_CLASS(k), "Placas falsas", "Audio/Source/Sink", "pruebas", "ba");
}
void proveedor_falso_init(ProveedorFalso* p) { g_proveedor = GST_DEVICE_PROVIDER(p); }

gboolean registrar(GstPlugin* plugin) {
    return gst_element_register(plugin, "baplacafalsa", GST_RANK_NONE, placa_falsa_get_type()) &&
           gst_device_provider_register(plugin, "baproveedorfalso", GST_RANK_PRIMARY, proveedor_falso_get_type());
}

std::map<std::string, GstDevice*> g_dispositivos;

void enchufar(const std::string& id, const char* clase) {
    {
        std::lock_guard<std::mutex> l(g_mu);
        g_enchufadas.insert(id);
    }
    GstCaps* caps = gst_caps_new_any(); // sin caps, el monitor no la cuenta (gst_caps_can_intersect)
    auto* d = static_cast<DispositivoFalso*>(
        g_object_new(dispositivo_falso_get_type(), "display-name", id.c_str(), "device-class", clase, "caps", caps, nullptr));
    gst_caps_unref(caps);
    d->id = g_strdup(id.c_str());
    g_dispositivos[id] = GST_DEVICE(gst_object_ref(d));
    gst_device_provider_device_add(g_proveedor.load(), GST_DEVICE(d)); // se queda con la referencia flotante
}

/// Se la llevan: deja de estar, el monitor avisa y lo que la usaba andando da error (como CoreAudio/WASAPI).
void desenchufar(const std::string& id) {
    std::vector<GstElement*> vivos;
    {
        std::lock_guard<std::mutex> l(g_mu);
        g_enchufadas.erase(id);
        for (GstElement* e : g_sumiderosVivos) {
            gchar* x = nullptr;
            g_object_get(e, propiedadDeDispositivo(), &x, nullptr);
            if (x != nullptr && id == x) vivos.push_back(GST_ELEMENT(gst_object_ref(e)));
            g_free(x);
        }
    }
    for (GstElement* e : vivos) {
        GError* err = g_error_new_literal(GST_RESOURCE_ERROR, GST_RESOURCE_ERROR_NOT_FOUND, "la placa se desenchufó");
        gst_element_post_message(e, gst_message_new_error(GST_OBJECT(e), err, "prueba"));
        g_error_free(err);
        gst_object_unref(e);
    }
    gst_device_provider_device_remove(g_proveedor.load(), g_dispositivos[id]);
    gst_object_unref(g_dispositivos[id]);
    g_dispositivos.erase(id);
}

// --- El banco -------------------------------------------------------------------------

std::int64_t ahoraMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

/// Corre `ms` (o hasta que `listo`) con los tic del motor y de los disparadores.
struct Banco {
    std::vector<std::function<void()>> tics;
    bool correr(int ms, const std::function<bool()>& listo = nullptr) {
        const auto fin = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
        while (std::chrono::steady_clock::now() < fin) {
            for (auto& t : tics) t();
            if (listo && listo()) return true;
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
        return listo ? listo() : true;
    }
};
} // namespace

int main(int argc, char** argv) {
#if defined(BA_TEST_GST_RAIZ)
    g_setenv("BA_GST_RAIZ", BA_TEST_GST_RAIZ, FALSE);
#endif
    // Ninguna placa de verdad: solo las falsas (nada se abre ni suena).
    g_setenv("GST_PLUGIN_FEATURE_RANK",
             "osxaudiodeviceprovider:NONE,avfdeviceprovider:NONE,wasapi2deviceprovider:NONE,wasapideviceprovider:NONE,"
             "mfdeviceprovider:NONE,ksdeviceprovider:NONE,directsoundsrcdeviceprovider:NONE,pulsedeviceprovider:NONE,"
             "alsadeviceprovider:NONE,pipewiredeviceprovider:NONE,v4l2deviceprovider:NONE,decklinkdeviceprovider:NONE",
             TRUE);
    QCoreApplication app(argc, argv);
    std::string err;
    if (!iniciarGStreamer(&err)) {
        std::printf("SKIP: sin GStreamer (%s)\n", err.c_str());
        return 77;
    }
    for (const char* e : {"intersink", "intersrc", "audiomixer", "level", "audiotestsrc", "fakesink"})
        if (!hayFabrica(e)) {
            std::printf("SKIP: falta %s\n", e);
            return 77;
        }
    gst_plugin_register_static(GST_VERSION_MAJOR, GST_VERSION_MINOR, "baplacasfalsas", "placas de audio falsas (pruebas)", registrar,
                               "1.0", "LGPL", "ba", "ba", "https://example.invalid");
    // El proveedor (uno por proceso, lo crea su fábrica): se pide antes de que arranque el Vigía.
    GstDeviceProvider* prov = gst_device_provider_factory_get_by_name("baproveedorfalso");
    CHECK(prov != nullptr && g_proveedor.load() == prov, "el proveedor falso está registrado");
    if (prov == nullptr) return 1;
    enchufar("mic-falso", "Audio/Source");
    enchufar("auris-falsos", "Audio/Sink");

    Reloj reloj;
    Lienzo lienzo;
    auto programa = std::make_unique<Mezcla>(reloj, "programa", Mezcla::Opciones{false, false, true, ""});
    // El micrófono: lo que entra es un tono sintético; el dispositivo dice si está.
    auto mic = std::make_unique<Entrada>(reloj, lienzo, "mic",
                                         json{{"pipeline_audio", "audiotestsrc is-live=true wave=sine freq=440 volume=0.25"},
                                              {"microfono", "mic-falso"},
                                              {"audio", true}});
    programa->agregar("mic");
    Escucha escucha(reloj, "baplacafalsa");
    escucha.ponerDispositivo("auris-falsos");
    escucha.escuchar(programa->productor(), reloj.latencia());
    // Una salida virtual de la Matriz (Motor::matrizBus): la misma escucha, sin respaldo.
    Escucha salidaMatriz(reloj, "baplacafalsa");
    salidaMatriz.sinRespaldo(true);
    salidaMatriz.ponerDispositivo("auris-falsos");
    salidaMatriz.escuchar(programa->productor(), reloj.latencia(), Escucha::Margen::Oido);

    // Los disparadores, con el pico de la entrada como en la app (StudioViewModel).
    QTemporaryDir dir;
    ba::app::agent::AlertInputs in;
    in.audio = [&]() { return std::vector<ba::app::agent::AlertInputs::Audio>{{"Mic", static_cast<double>(mic->estado().value("pico_audio_db", -100.0)), false}}; };
    in.nowMs = []() { return ahoraMs(); };
    ba::app::agent::AlertEngine alertas(in, (dir.path() + "/alertas.json").toStdString());
    alertas.clear();
    std::vector<json> disparos;
    QObject::connect(&alertas, &ba::app::agent::AlertEngine::fired, [&](const json& ev) { disparos.push_back(ev); });
    CHECK(!alertas.create({{"type", "audio_silence"}, {"params", {{"source", "Mic"}, {"belowDb", -55}, {"forSec", 2}, {"onlyWhenLive", false}}}, {"cooldownSec", 0}})
               .contains("error"),
          "disparador de silencio en el micrófono");

    Banco banco;
    banco.tics = {[&]() { mic->tic(); }, [&]() { programa->tic(); }, [&]() { escucha.tic(reloj.latencia()); },
                  [&]() { salidaMatriz.tic(reloj.latencia()); }, [&]() { alertas.tick(); }};
    const auto picoMic = [&]() { return mic->estado().value("pico_audio_db", -100.0); };
    const auto escuchaEn = [&](bool respaldo) {
        const json e = escucha.estado();
        return e.value("anda", false) && e.value("en_respaldo", !respaldo) == respaldo && e.value("pico_db", -120.0) > -30;
    };

    // --- Arranca con todo enchufado -------------------------------------------------------
    CHECK(banco.correr(4000, [&]() { return picoMic() > -20 && escuchaEn(false); }), "el micrófono suena y la escucha va por los auriculares");
    std::printf("   mic %.1f dB; escucha %s\n", picoMic(), escucha.estado().dump().c_str());
    CHECK(disparos.empty(), "con audio, el silencio no salta");

    // --- ENTRADA: se va el micrófono -------------------------------------------------------
    desenchufar("mic-falso");
    CHECK(banco.correr(3000, [&]() { return picoMic() <= -99; }), "se fue: la entrada deja de dar audio");
    CHECK(mic->estado().value("error", std::string()).find("mic-falso") != std::string::npos, "y dice que espera al micrófono");
    CHECK(banco.correr(4000, [&]() { return !disparos.empty(); }), "audio_silence salta");
    if (!disparos.empty()) CHECK(disparos[0].value("type", "") == "audio_silence", "es el de silencio del micrófono");
    enchufar("mic-falso", "Audio/Source");
    CHECK(banco.correr(20000, [&]() { return picoMic() > -20; }), "volvió: la entrada se rearma sola y suena");
    std::printf("   mic de vuelta: %.1f dB\n", picoMic());
    const std::size_t antes = disparos.size();
    banco.correr(2500);
    CHECK(disparos.size() == antes, "con audio de nuevo no salta");
    desenchufar("mic-falso");
    CHECK(banco.correr(8000, [&]() { return disparos.size() > antes; }), "el disparador se rearmó: si se va otra vez, salta otra vez");
    enchufar("mic-falso", "Audio/Source");
    CHECK(banco.correr(20000, [&]() { return picoMic() > -20; }), "y vuelve otra vez");

    // --- ESCUCHA y MATRIZ: se van los auriculares andando ---------------------------------
    const auto matrizSuena = [&]() {
        const json e = salidaMatriz.estado();
        return e.value("anda", false) && !e.value("en_respaldo", true) && e.value("pico_db", -120.0) > -30;
    };
    CHECK(banco.correr(4000, matrizSuena), "matriz: la salida virtual sale por su placa");
    desenchufar("auris-falsos");
    CHECK(banco.correr(5000, [&]() { return escuchaEn(true); }), "la escucha se rearma por la salida del sistema");
    std::printf("   escucha sin auriculares: %s\n", escucha.estado().dump().c_str());
    CHECK(banco.correr(5000, [&]() { return !salidaMatriz.estado().value("anda", true); }), "matriz: sin su placa, muda");
    CHECK(!salidaMatriz.estado().value("en_respaldo", true), "matriz: y no va por la del sistema");
    std::printf("   salida virtual sin su placa: %s\n", salidaMatriz.estado().dump().c_str());
    enchufar("auris-falsos", "Audio/Sink");
    CHECK(banco.correr(5000, [&]() { return escuchaEn(false); }), "volvieron: la escucha regresa a ellos sola");
    CHECK(banco.correr(5000, matrizSuena), "matriz: volvió la placa, la salida virtual sale por ella sola");

    salidaMatriz.apagar();
    salidaMatriz.esperar();
    escucha.apagar();
    escucha.esperar();
    programa->quitar("mic");
    mic.reset();
    programa.reset();
    gst_object_unref(prov);
    std::printf(g_fallas ? "\n%d fallas\n" : "\ntodo ok\n", g_fallas);
    return g_fallas ? 1 : 0;
}
