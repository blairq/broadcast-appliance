// test_motor.cpp — el motor de GStreamer sin libobs: reloj, entradas y el bus PROGRAMA
//
// Con el GStreamer del bundle (sin él: SKIP):
//   - dos pipelines adoptados por el reloj del estudio tienen el mismo base_time;
//   - una entrada en vivo (tono) llega al bus PROGRAMA con su nivel (seno 0,25 = −15 dBFS);
//   - una segunda que entra andando suma (+3 dB) sin cortar ni rebobinar;
//   - fader, mute y la ganancia de escena con fundido;
//   - sacar una rama andando no corta las demás;
//   - un archivo (medio) suena por el bus, termina (ended) y al reiniciar vuelve a
//     sonar: su base_time cambió y el offset del pad lo sigue.

#include "Entrada.hpp"
#include "Mezcla.hpp"
#include "Piezas.hpp"
#include "Reloj.hpp"

#include <gst/gst.h>

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <thread>
#include <vector>

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

struct Banco {
    std::vector<Entrada*> entradas;
    Mezcla* mezcla = nullptr;
    void correr(int ms) {
        const auto fin = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
        while (std::chrono::steady_clock::now() < fin) {
            for (Entrada* e : entradas) e->tic();
            if (mezcla != nullptr) mezcla->tic();
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
    }
    double rms() const { return mezcla->estado().value("rms_db", -120.0); }
};

bool cerca(double a, double b, double tol) { return std::fabs(a - b) <= tol; }

/// Un WAV de `seg` segundos con un seno de 0,25 (para el medio).
bool hacerWav(const std::string& ruta, double seg) {
    const int bloques = static_cast<int>(seg * 48000 / 1024);
    const std::string d = "audiotestsrc wave=sine freq=330 volume=0.25 samplesperbuffer=1024 num-buffers=" +
                          std::to_string(bloques) +
                          " ! audio/x-raw,format=S16LE,rate=48000,channels=2 ! wavenc ! filesink location=" + entreComillas(ruta);
    GError* e = nullptr;
    GstElement* p = gst_parse_launch(d.c_str(), &e);
    if (e != nullptr) {
        g_error_free(e);
        return false;
    }
    gst_element_set_state(p, GST_STATE_PLAYING);
    GstBus* bus = gst_element_get_bus(p);
    GstMessage* m = gst_bus_timed_pop_filtered(bus, 10 * GST_SECOND, static_cast<GstMessageType>(GST_MESSAGE_EOS | GST_MESSAGE_ERROR));
    const bool ok = m != nullptr && GST_MESSAGE_TYPE(m) == GST_MESSAGE_EOS;
    if (m != nullptr) gst_message_unref(m);
    gst_object_unref(bus);
    gst_element_set_state(p, GST_STATE_NULL);
    gst_object_unref(p);
    return ok;
}
} // namespace

int main() {
#if defined(BA_TEST_GST_RAIZ)
    g_setenv("BA_GST_RAIZ", BA_TEST_GST_RAIZ, FALSE);
#endif
#if defined(BA_TEST_GST_PLUGINS)
    g_setenv("BA_GST_PLUGINS_EXTRA", BA_TEST_GST_PLUGINS, FALSE); // los nuestros (balimitador), sin instalar
#endif
    std::string err;
    if (!iniciarGStreamer(&err)) {
        std::printf("SKIP: sin GStreamer (%s)\n", err.c_str());
        return 77;
    }
    for (const char* e : {"intersink", "intersrc", "audiomixer", "level"})
        if (!hayFabrica(e)) {
            std::printf("SKIP: falta %s\n", e);
            return 77;
        }

    Reloj reloj;
    CHECK(reloj.valido(), "el reloj del estudio");
    {
        GstElement* a = gst_pipeline_new("a");
        GstElement* b = gst_pipeline_new("b");
        reloj.adoptar(a);
        reloj.adoptar(b);
        CHECK(gst_element_get_base_time(a) == gst_element_get_base_time(b), "mismo base_time");
        CHECK(gst_element_get_base_time(a) == reloj.base(), "el del estudio");
        gst_object_unref(a);
        gst_object_unref(b);
    }
    // El tope: una que pasa no arrastra.
    reloj.anotar("x", 100 * GST_MSECOND);
    reloj.anotar("y", 900 * GST_MSECOND);
    CHECK(reloj.latencia() == static_cast<std::int64_t>(100 * GST_MSECOND), "la que pasa el tope no arrastra");
    CHECK(reloj.llegaTarde(900 * GST_MSECOND), "y avisa que llega tarde");
    reloj.olvidar("x");
    reloj.olvidar("y");

    Lienzo lienzo;
    Banco banco;
    auto programa = std::make_unique<Mezcla>(reloj, "programa", Mezcla::Opciones{true, true, true, ""});
    CHECK(programa->anda(), "el bus PROGRAMA arranca");
    banco.mezcla = programa.get();
    banco.correr(300);
    CHECK(banco.rms() < -60, "sin entradas: silencio, pero andando");

    const json tono1{{"pipeline_audio", "audiotestsrc is-live=true wave=sine freq=440 volume=0.25"}};
    auto e1 = std::make_unique<Entrada>(reloj, lienzo, "tono1", tono1);
    banco.entradas.push_back(e1.get());
    programa->agregar("tono1");
    banco.correr(1500);
    const json st1 = e1->estado();
    std::printf("tono1: %s\n", st1.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace).c_str());
    CHECK(st1.value("anda", false), "la entrada anda");
    CHECK(st1.value("bloques_audio", 0) > 20, "da audio");
    std::printf("programa con un tono: %.2f dBFS (latencia %.0f ms)\n", banco.rms(), programa->estado().value("latencia_ms", 0.0));
    CHECK(cerca(banco.rms(), -15.05, 1.0), "un seno de 0,25 sale a -15 dBFS");
    {
        // Los niveles del Mezclador: lo que entrega la entrada y el maestro del bus, por canal.
        float pico[2], rms[2];
        e1->nivelAudio(pico, rms);
        std::printf("tono1 por canal: pico %.2f/%.2f, RMS %.2f/%.2f dB\n", pico[0], pico[1], rms[0], rms[1]);
        CHECK(cerca(pico[0], -12.04, 0.5) && cerca(pico[1], -12.04, 0.5), "la entrada: pico -12 dB en L y R");
        CHECK(cerca(rms[0], -15.05, 0.7) && cerca(rms[1], -15.05, 0.7), "la entrada: RMS -15 dB en L y R");
        double mp[2], mr[2];
        programa->nivelMaestro(mp, mr);
        std::printf("maestro por canal: pico %.2f/%.2f, RMS %.2f/%.2f dB\n", mp[0], mp[1], mr[0], mr[1]);
        CHECK(cerca(mp[0], -12.04, 0.5) && cerca(mp[1], -12.04, 0.5), "el maestro: pico -12 dB en L y R");
        CHECK(cerca(mr[0], -15.05, 0.7) && cerca(mr[1], -15.05, 0.7), "el maestro: RMS -15 dB en L y R");
    }

    // Una segunda que entra andando: suma (+3 dB, frecuencias distintas).
    const json tono2{{"pipeline_audio", "audiotestsrc is-live=true wave=sine freq=1000 volume=0.25"}};
    auto e2 = std::make_unique<Entrada>(reloj, lienzo, "tono2", tono2);
    banco.entradas.push_back(e2.get());
    programa->agregar("tono2");
    banco.correr(1500);
    std::printf("programa con dos: %.2f dBFS\n", banco.rms());
    CHECK(cerca(banco.rms(), -12.04, 1.0), "la que entra andando suma +3 dB");
    const json pe = programa->estado();
    CHECK(pe["entradas"].contains("tono1") && pe["entradas"].contains("tono2"), "las dos en el estado");

    programa->ponerNivel("tono2", 0.0);
    banco.correr(600);
    CHECK(cerca(banco.rms(), -15.05, 1.0), "fader a 0: queda una");
    programa->ponerMute("tono1", true);
    banco.correr(600);
    CHECK(banco.rms() < -60, "mute: silencio");
    programa->ponerMute("tono1", false);
    programa->ponerNivel("tono2", 1.0);
    programa->ponerEscena("tono1", 0.0, 400);
    banco.correr(300);
    const double aMitad = programa->estado()["entradas"]["tono1"].value("escena", -1.0);
    CHECK(aMitad > 0.0 && aMitad < 1.0, "el fundido de escena va de a poco");
    banco.correr(900);
    std::printf("tono1 fuera de escena: %.2f dBFS\n", banco.rms());
    CHECK(cerca(banco.rms(), -15.05, 1.0), "fuera de escena: queda la otra");

    // Sacar una rama andando: las demás siguen.
    programa->quitar("tono2");
    banco.correr(600);
    CHECK(!programa->tiene("tono2"), "se sacó");
    CHECK(banco.rms() < -60, "tono1 sigue fuera de escena: silencio");
    programa->ponerEscena("tono1", 1.0, 0);
    banco.correr(600);
    CHECK(cerca(banco.rms(), -15.05, 1.0), "y la otra sigue sonando");
    programa->ponerEscena("tono1", 0.0, 0);

    // Un archivo: suena, termina, se reinicia (otro base_time: el offset lo sigue).
    const std::string wav = (std::filesystem::temp_directory_path() / "ba_test_motor.wav").string();
    CHECK(hacerWav(wav, 4.0), "se hizo el WAV");
    auto m = std::make_unique<Entrada>(reloj, lienzo, "medio", json{{"medio", true}, {"local_file", wav}});
    banco.entradas.push_back(m.get());
    programa->agregar("medio");
    banco.correr(1000);
    std::printf("medio: %s, %.2f dBFS, %lld ms de %lld\n", m->estadoMedio().c_str(), banco.rms(),
                static_cast<long long>(m->tiempoMs()), static_cast<long long>(m->duracionMs()));
    CHECK(m->estadoMedio() == "playing", "el medio anda");
    CHECK(cerca(banco.rms(), -15.05, 1.5), "el medio suena por el bus");
    banco.correr(3500);
    CHECK(m->estadoMedio() == "ended", "terminó");
    CHECK(banco.rms() < -60, "terminado: silencio");
    CHECK(m->controlMedio("reiniciar").empty(), "reiniciar");
    banco.correr(900);
    std::printf("medio reiniciado: %s, %.2f dBFS\n", m->estadoMedio().c_str(), banco.rms());
    CHECK(cerca(banco.rms(), -15.05, 1.5), "reiniciado vuelve a sonar (offset al día)");
    CHECK(m->controlMedio("pausa").empty(), "pausa");
    banco.correr(500);
    CHECK(banco.rms() < -60, "en pausa: silencio");
    CHECK(m->controlMedio("play").empty(), "play");
    for (int i = 0; i < 7 && std::getenv("BA_TRAZA"); ++i) {
        banco.correr(100);
        const json st = m->estado();
        std::printf("  tras play: %.1f dB, sin datos %.2f s, %s, %lld ms\n", banco.rms(), st.value("sin_datos_s", -1.0),
                    m->estadoMedio().c_str(), static_cast<long long>(m->tiempoMs()));
    }
    banco.correr(700);
    CHECK(cerca(banco.rms(), -15.05, 1.5), "después de la pausa vuelve a sonar");

    // En bucle: vueltas sin cortes (saltos por segmentos, sin flush). Se mide
    // cada 100 ms durante tres vueltas: nunca silencio.
    programa->quitar("medio");
    banco.entradas.pop_back();
    m.reset();
    const std::string corto = (std::filesystem::temp_directory_path() / "ba_test_motor_corto.wav").string();
    CHECK(hacerWav(corto, 1.0), "se hizo el WAV corto");
    m = std::make_unique<Entrada>(reloj, lienzo, "bucle", json{{"medio", true}, {"local_file", corto}, {"looping", true}});
    banco.entradas.push_back(m.get());
    programa->agregar("bucle");
    banco.correr(800);
    double peor = 0;
    for (int i = 0; i < 30; ++i) {
        banco.correr(100);
        peor = std::min(peor, banco.rms());
    }
    std::printf("en bucle, el peor nivel en 3 s: %.2f dBFS (%s)\n", peor, m->estadoMedio().c_str());
    CHECK(peor > -18, "en bucle no hay silencio entre vueltas");
    programa->quitar("bucle");
    std::filesystem::remove(corto);
    // El limitador del máster: un tono a +6 dBFS sale a <= -1 dBTP (medido por el
    // ebur128level del bus, después del limitador).
    if (hayFabrica("balimitador")) {
        auto f = std::make_unique<Entrada>(reloj, lienzo, "fuerte",
                                           json{{"pipeline_audio", "audiotestsrc is-live=true freq=997 volume=1 ! "
                                                                   "audio/x-raw,format=F32LE,rate=48000,channels=2 ! volume volume=2"}});
        banco.entradas.push_back(f.get());
        programa->agregar("fuerte");
        banco.correr(1500);
        const json pe2 = programa->estado();
        std::printf("limitador: %s; loudness: %s\n", pe2["limitador"].dump(-1, ' ', false, nlohmann::json::error_handler_t::replace).c_str(), pe2["loudness"].dump(-1, ' ', false, nlohmann::json::error_handler_t::replace).c_str());
        CHECK(pe2["loudness"].value("true_peak_db", 0.0) <= -0.95, "+6 dBFS sale a <= -1 dBTP");
        CHECK(pe2["loudness"].value("true_peak_db", -120.0) > -2.0, "y no baja de más");
        CHECK(pe2["limitador"].value("reduccion_db", 0.0) < -5.0, "el limitador está bajando");
        CHECK(pe2["loudness"].value("momentaneo", -120.0) > -10, "el loudness se mide");
        programa->ponerLimitador(false, -1.0);
        banco.correr(800);
        CHECK(programa->estado()["loudness"].value("true_peak_db", 0.0) > 5.0, "apagado no limita");
        programa->ponerLimitador(true, -1.0);
        programa->quitar("fuerte");
        banco.entradas.pop_back();
    } else {
        std::printf("(sin balimitador: no se prueba el limitador)\n");
    }

    // Video: normalizado al lienzo (formato, cuadros, píxel cuadrado; el tamaño queda).
    auto v = std::make_unique<Entrada>(reloj, lienzo, "camara",
                                       json{{"pipeline", "videotestsrc is-live=true do-timestamp=true ! video/x-raw,width=640,height=360,framerate=25/1"}});
    banco.entradas.push_back(v.get());
    for (int i = 0; i < 6 && std::getenv("BA_TRAZA"); ++i) {
        banco.correr(300);
        const json t = v->estado();
        std::printf("  video: p50 %.0f p95 %.0f cuadros %d\n", t.value("edad_p50_ms", 0.0), t.value("edad_p95_ms", 0.0), t.value("cuadros", 0));
    }
    banco.correr(1200);
    const json sv = v->estado();
    std::printf("video: %s\n", sv["sale"].dump(-1, ' ', false, nlohmann::json::error_handler_t::replace).c_str());
    CHECK(sv.value("cuadros", 0) > 20, "da cuadros");
    // En la GPU la entrada publica una textura RGBA (la composición la convierte).
    CHECK(sv["sale"].value("formato", "") == (gpuVideo() ? "RGBA" : "NV12"), "sale en el formato del lienzo");
    CHECK(cerca(sv["sale"].value("fps", 0.0), 30.0, 0.01), "a los cuadros del lienzo");
    CHECK(sv["sale"].value("ancho", 0) == 640, "el tamaño queda (lo escala la composición)");
    CHECK(sv["origen"].value("fps", 0.0) == 25.0, "y se sabe cómo llegaba");
    // Con do-timestamp, sus horas son las de captura (como una cámara de verdad).
    CHECK(sv.value("edad_p50_ms", 9999.0) < 500, "sale con la hora del estudio");
    CHECK(!sv.value("llega_tarde", true), "y no queda «llega tarde»");
    // El video y el audio por separado (la página Tiempos): esta entrada no tiene audio.
    banco.correr(1200);
    const json sv2 = v->estado();
    CHECK(sv2["edad_video"].is_object() && sv2["edad_video"].value("p50_ms", 9999.0) < 500 &&
              sv2["edad_video"].value("p95_ms", -1.0) >= sv2["edad_video"].value("p50_ms", 0.0),
          "la edad del video, sola (p50 ≤ p95)");
    CHECK(sv2["edad_audio"].is_null(), "y sin audio, la del audio vacía");
    banco.entradas.pop_back();
    v.reset();
    // Una imagen (PNG): da cuadros (imagefreeze) con su alfa.
    {
        const std::string png = (std::filesystem::temp_directory_path() / "ba_test_motor.png").string();
        GstElement* p = gst_parse_launch(("videotestsrc num-buffers=1 pattern=solid-color foreground-color=0x800000ff ! "
                                          "video/x-raw,format=BGRA,width=64,height=64 ! videoconvert ! pngenc ! filesink location=" + entreComillas(png)).c_str(), nullptr);
        gst_element_set_state(p, GST_STATE_PLAYING);
        GstBus* b = gst_element_get_bus(p);
        if (GstMessage* m = gst_bus_timed_pop_filtered(b, 5 * GST_SECOND, static_cast<GstMessageType>(GST_MESSAGE_EOS | GST_MESSAGE_ERROR)))
            gst_message_unref(m);
        gst_object_unref(b);
        gst_element_set_state(p, GST_STATE_NULL);
        gst_object_unref(p);
        auto img = std::make_unique<Entrada>(reloj, lienzo, "imagen", json{{"tipo", "imagen"}, {"archivo", png}});
        banco.entradas.push_back(img.get());
        banco.correr(1200);
        const json si = img->estado();
        std::printf("imagen: %d cuadros, sale %s\n", si.value("cuadros", 0), si["sale"].dump(-1, ' ', false, nlohmann::json::error_handler_t::replace).c_str());
        CHECK(si.value("cuadros", 0) > 15, "la imagen da cuadros");
        CHECK(si["sale"].value("formato", "") == (gpuVideo() ? "RGBA" : "BGRA"), "con su alfa (BGRA, o RGBA en la GPU)");
        banco.entradas.pop_back();
        img.reset();
        std::filesystem::remove(png);
    }
    programa->quitar("tono1");
    banco.entradas.clear();
    m.reset();
    e1.reset();
    e2.reset();
    programa.reset();
    std::filesystem::remove(wav);
    std::printf("== test_motor: %s ==\n", fallos == 0 ? "OK" : "FALLOS");
    return fallos == 0 ? 0 : 1;
}
