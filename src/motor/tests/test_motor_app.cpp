// test_motor_app.cpp — lo que el motor le da a la app (M6.1)
//
//   - una entrada tipo=app: la app empuja un cuadro y se ve (y sigue viéndose);
//   - la vista de una escena que no está al aire (el editor) y que se actualiza al cambiarla;
//   - el DSK con una escena entera;
//   - la T-bar: a mitad, mezcla; soltada antes de la mitad vuelve; pasada, queda;
//   - el volumen del máster con fundido (el FTB baja el audio);
//   - la toma del audio del programa (lo que reciben el loudness y los picos);
//   - avisos por cuadro de una vista (lo que va al gateway);
//   - remux de una grabación MKV a MP4 sin recodificar.

#include "Motor.hpp"
#include "Salidas.hpp"

#include <gst/gst.h>
#include <gst/pbutils/pbutils.h>

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <thread>
#if defined(_WIN32)
#include <process.h>
#define getpid _getpid
#else
#include <unistd.h>
#endif
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

void esperar(int ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }

struct Px {
    int r = -1, g = -1, b = -1;
};
Px px(const Cuadro& c, int x, int y) {
    if (c.bgra.empty()) return {};
    const std::uint8_t* p = c.bgra.data() + static_cast<std::size_t>(y) * c.stride + static_cast<std::size_t>(x) * 4;
    return {p[2], p[1], p[0]};
}
bool igual(const Px& p, int r, int g, int b, int tol = 40) {
    return std::abs(p.r - r) <= tol && std::abs(p.g - g) <= tol && std::abs(p.b - b) <= tol;
}
std::string txt(const Px& p) { return "(" + std::to_string(p.r) + "," + std::to_string(p.g) + "," + std::to_string(p.b) + ")"; }

Px mirar(Motor& m, const std::string& clave, int x, int y) {
    Cuadro c;
    std::uint64_t s = 0;
    for (int i = 0; i < 30 && !m.verUltimo(clave, c, s); ++i) esperar(50);
    return px(c, x, y);
}

/// Espera hasta que lo que se ve en (x, y) cumpla `cumple` (o hasta `topeMs`) y devuelve lo
/// último que vio. Con la máquina cargada (la suite en paralelo) una composición nueva tarda
/// más en arrancar que la espera fija de antes: se espera a que pase, no un tiempo.
template <typename F>
Px mirarHasta(Motor& m, const std::string& clave, int x, int y, F cumple, int topeMs = 6000) {
    Px p = mirar(m, clave, x, y);
    for (int t = 0; t < topeMs && !cumple(p); t += 100) {
        esperar(100);
        p = mirar(m, clave, x, y);
    }
    return p;
}

json color(const char* c) {
    return {{"tipo", "color"}, {"color", c}, {"ancho", 320}, {"alto", 180}};
}
} // namespace

int main() {
#if defined(BA_TEST_GST_RAIZ)
    g_setenv("BA_GST_RAIZ", BA_TEST_GST_RAIZ, FALSE);
#endif
#if defined(BA_TEST_GST_PLUGINS)
    g_setenv("BA_GST_PLUGINS_EXTRA", BA_TEST_GST_PLUGINS, FALSE);
#endif
    std::string err;
    if (!iniciarGStreamer(&err)) {
        std::printf("SKIP: sin GStreamer (%s)\n", err.c_str());
        return 77;
    }
    for (const char* e : {"intersink", "compositor", "imagefreeze", "balimitador"})
        if (!hayFabrica(e)) {
            std::printf("SKIP: falta %s\n", e);
            return 77;
        }
    Motor::Config cfg;
    cfg.lienzo.ancho = 320;
    cfg.lienzo.alto = 180;
    cfg.prefijoAnillos = "";
    cfg.sinkEscucha = "fakesink";
    Motor m(cfg);
    CHECK(m.iniciar().empty(), "el motor arranca");
    CHECK(m.agregarEntrada("rojo", color("#FF0000")).empty(), "rojo");
    CHECK(m.agregarEntrada("verde", color("#00FF00")).empty(), "verde");
    CHECK(m.agregarEntrada("lienzo", json{{"tipo", "app"}}).empty(), "una entrada que pinta la app");
    CHECK(m.agregarEntrada("tono", json{{"pipeline_audio", "audiotestsrc is-live=true wave=sine volume=0.25 freq=440"}},
                           json{{"global", true}})
              .empty(),
          "un tono global");
    m.definirEscena({{"nombre", "Roja"}, {"capas", {{{"entrada", "rojo"}}}}});
    m.definirEscena({{"nombre", "Verde"}, {"capas", {{{"entrada", "verde"}}}}});
    m.definirEscena({{"nombre", "App"}, {"capas", {{{"entrada", "lienzo"}}}}});
    CHECK(m.alAire("Roja").empty(), "al aire la roja");
    CHECK(m.verPedir("programa", 320, 180).empty(), "ver el programa");
    esperar(1500);
    CHECK(igual(mirar(m, "programa", 160, 90), 255, 0, 0), "el programa, rojo");

    // La app empuja un cuadro azul (una sola vez) y la escena App lo muestra.
    {
        std::vector<std::uint8_t> azul(160 * 90 * 4);
        for (std::size_t i = 0; i < azul.size(); i += 4) {
            azul[i] = 255;     // B
            azul[i + 3] = 255; // A
        }
        esperar(300);
        CHECK(m.empujarCuadro("lienzo", azul.data(), 160, 90, 160 * 4), "empujar un cuadro");
        CHECK(!m.empujarCuadro("rojo", azul.data(), 160, 90, 160 * 4), "a una entrada que no es app, no");
        CHECK(m.verPedir("escena:App", 320, 180).empty(), "ver una escena que no está al aire (el editor)");
        esperar(1500);
        const Px p = mirar(m, "escena:App", 160, 90);
        std::printf("escena App (no al aire): %s\nlienzo: %s\n", txt(p).c_str(), m.estado()["entradas"]["lienzo"].dump(-1, ' ', false, nlohmann::json::error_handler_t::replace).c_str());
        CHECK(igual(p, 0, 0, 255), "lo que pintó la app se ve");
        esperar(1500);
        CHECK(igual(mirar(m, "escena:App", 160, 90), 0, 0, 255), "y sigue viéndose (sin mandar más cuadros)");
        // Cambiar la escena la actualiza en la vista.
        m.definirEscena({{"nombre", "App"}, {"capas", {{{"entrada", "verde"}}}}});
        esperar(800);
        CHECK(igual(mirar(m, "escena:App", 160, 90), 0, 255, 0), "la vista de la escena sigue sus cambios");
        m.verSoltar("escena:App");
        m.esperarCola();
        CHECK(m.estado()["vistas"].count("escena:App") == 0, "soltada");
    }

    // DSK con una escena entera (la App, ahora verde) en un recuadro.
    CHECK(m.dsk({{{"escena", "App"}, {"x", 0}, {"y", 0}, {"ancho", 80}, {"alto", 45}}}, 0).empty(), "DSK con una escena");
    const Px dskPx = mirarHasta(m, "programa", 20, 10, [](const Px& p) { return igual(p, 0, 255, 0); });
    const Px resto = mirarHasta(m, "programa", 200, 120, [](const Px& p) { return igual(p, 255, 0, 0); });
    std::printf("DSK con escena: %s, resto %s\n", txt(dskPx).c_str(), txt(resto).c_str());
    CHECK(igual(dskPx, 0, 255, 0) && igual(resto, 255, 0, 0), "la escena del DSK arriba del programa");
    CHECK(m.dsk(json::array(), 0).empty(), "sacar el DSK");
    esperar(500);

    // T-bar hacia la verde.
    m.tbar("Verde", 0.5, false);
    esperar(500);
    const Px mitad = mirar(m, "programa", 160, 90);
    std::printf("T-bar a mitad: %s (hacia %s)\n", txt(mitad).c_str(), m.tbarHacia().c_str());
    CHECK(mitad.r > 70 && mitad.r < 190 && mitad.g > 70 && mitad.g < 190, "a mitad, mezcla");
    CHECK(m.tbarHacia() == "Verde", "va hacia la verde");
    CHECK(!m.tbar("Verde", 0.3, true), "soltada antes de la mitad, vuelve");
    esperar(500);
    CHECK(igual(mirar(m, "programa", 160, 90), 255, 0, 0), "vuelve la roja");
    m.tbar("Verde", 0.4, false);
    CHECK(m.tbar("Verde", 0.8, true), "pasada la mitad, queda");
    esperar(500);
    CHECK(igual(mirar(m, "programa", 160, 90), 0, 255, 0), "queda la verde");
    CHECK(m.estado()["video"].value("al_aire", "") == "Verde", "al aire la verde");

    // La toma del audio del programa y el volumen del máster.
    std::atomic<double> pico{0};
    std::atomic<std::size_t> cuadros{0};
    const int oyente = m.escucharPrograma([&](const float* d, std::size_t n) {
        double p = 0;
        for (std::size_t i = 0; i < n * 2; ++i) p = std::max(p, static_cast<double>(std::fabs(d[i])));
        pico = p;
        cuadros += n;
    });
    esperar(1500);
    std::printf("toma del programa: %zu cuadros, pico %.3f\n", cuadros.load(), pico.load());
    CHECK(cuadros > 48000, "llega el audio del programa (más de 1 s)");
    CHECK(std::fabs(pico - 0.25) < 0.03, "con su nivel (seno de 0,25)");
    m.masterVolumen(0.0, 300);
    esperar(900);
    std::printf("con el máster a 0: pico %.4f\n", pico.load());
    CHECK(pico < 0.01, "el máster baja todo (FTB)");
    m.masterVolumen(1.0, 0);
    esperar(600);
    CHECK(pico > 0.2, "y vuelve");
    m.dejarDeEscuchar(oyente);

    // Avisos por cuadro (lo que va al gateway).
    {
        CHECK(m.verPedir("programa@gateway", 320, 180).empty(), "otra vista del programa");
        std::atomic<int> n{0};
        m.verAvisar("programa@gateway", [&](const Cuadro& c) {
            if (c.ancho == 320) ++n;
        });
        // Desde que llega el primero (armar la vista tarda más con la máquina cargada).
        for (int i = 0; i < 40 && n.load() == 0; ++i) esperar(50);
        n = 0;
        esperar(1000);
        std::printf("avisos en 1 s: %d\n", n.load());
        CHECK(n > 12, "cada cuadro avisa"); // 30 por segundo; con la máquina cargada se pierden algunos
        m.verAvisar("programa@gateway", nullptr);
        m.verSoltar("programa@gateway");
    }

    // Remux de una grabación corta.
    {
        const auto dir = std::filesystem::temp_directory_path() / ("ba_test_motor_app_" + std::to_string(getpid()));
        Grabacion::Config gc;
        gc.carpeta = dir.string();
        gc.nombre = "r";
        gc.tramoSeg = 0;
        gc.kbps = 1000;
        Grabacion g(m.reloj(), gc);
        Fuentes f;
        f.video = m.productorDeVideo("programa", &f.latVideoNs);
        f.audios.push_back(m.productorDeBus("programa", &f.latAudioNs));
        CHECK(g.iniciar(f).empty(), "grabar");
        esperar(2000);
        g.parar();
        const auto ar = g.archivos();
        CHECK(!ar.empty(), "un archivo");
        if (!ar.empty()) {
            const std::string mp4 = (dir / "r.mp4").string();
            double ultimo = 0;
            std::error_code ec0;
            std::printf("mkv: %s, %lld bytes\n", ar[0].c_str(), static_cast<long long>(std::filesystem::file_size(ar[0], ec0)));
            if (std::getenv("BA_GUARDAR")) std::filesystem::copy_file(ar[0], "/tmp/ba_remux.mkv", std::filesystem::copy_options::overwrite_existing, ec0);
            const std::string e = remuxar(ar[0], mp4, [&](double p) { ultimo = p; });
            std::printf("remux: «%s», %.2f\n", e.c_str(), ultimo);
            CHECK(e.empty(), "remux sin error");
            gchar* uri = gst_filename_to_uri(mp4.c_str(), nullptr);
            GstDiscoverer* d = gst_discoverer_new(10 * GST_SECOND, nullptr);
            GstDiscovererInfo* i = gst_discoverer_discover_uri(d, uri, nullptr);
            const bool ok = i != nullptr && gst_discoverer_info_get_result(i) == GST_DISCOVERER_OK;
            int v = 0, a = 0;
            if (ok) {
                GList* lv = gst_discoverer_info_get_video_streams(i);
                GList* la = gst_discoverer_info_get_audio_streams(i);
                v = static_cast<int>(g_list_length(lv));
                a = static_cast<int>(g_list_length(la));
                gst_discoverer_stream_info_list_free(lv);
                gst_discoverer_stream_info_list_free(la);
            }
            CHECK(ok && v == 1 && a == 1, "el MP4 se abre, con video y audio");
            if (i != nullptr) gst_discoverer_info_unref(i);
            g_object_unref(d);
            g_free(uri);
        }
        std::error_code ec;
        std::filesystem::remove_all(dir, ec);
    }
    // El video de una entrada atrasado (la tarjeta retardo): sigue viéndose, no se descarta.
    {
        json v = color("#00FF00");
        v["desfase_video"] = 300;
        CHECK(m.actualizarEntrada("verde", v).empty(), "atrasar el video de la verde");
        esperar(1500);
        CHECK(m.estado()["entradas"]["verde"].value("desfase_video_ms", 0) == 300, "el desfase de video, en el estado");
        CHECK(igual(mirar(m, "programa", 160, 90), 0, 255, 0), "con el video atrasado, el programa sigue verde");
    }
    // Una fuente de POCOS cuadros (como el canal de un invitado vacío: la placa a 5 cps) con el
    // PTZ digital: el tee de la toma entera trababa la rama principal después del primer cuadro
    // (pasaba uno por rearme y la entrada aparecía y desaparecía cada 10 s).
    if (hayFabrica("baptz")) {
        const json lenta{{"pipeline", "videotestsrc is-live=true pattern=ball ! video/x-raw,width=1280,height=720,framerate=5/1"},
                         {"normalizar", false},
                         {"filtros", json{{"video", json::array({{{"id", "ptz"}, {"elemento", "baptz"}}})}}}};
        CHECK(m.agregarEntrada("lenta", lenta).empty(), "una entrada de 5 cps con PTZ");
        esperar(4000);
        const json el = m.estado()["entradas"]["lenta"];
        std::printf("5 cps con PTZ: cuadros %d, sin datos %.1f s\n", el.value("cuadros", 0), el.value("sin_datos_s", -1.0));
        CHECK(el.value("cuadros", 0) >= 5, "con el PTZ sigue pasando cuadros (trabado pasaba uno solo)");
        m.quitarEntrada("lenta");
    }
    // Una fuente de prueba en vivo (la cámara virtual «pelota») con desfase: sigue dando cuadros
    // (en la prueba de Sincronía y red se quedaba sin datos hasta el rearme, 30 s después).
    {
        const json pelota{{"pipeline", "videotestsrc pattern=ball is-live=true"}};
        CHECK(m.agregarEntrada("pelota", pelota).empty(), "la cámara virtual pelota");
        esperar(1500);
        const auto cuadros = [&m]() { return m.estado()["entradas"]["pelota"].value("cuadros", 0); };
        const int antes = cuadros();
        json con = pelota;
        con["desfase"] = 200; // atrasarla: su hora queda en el futuro y la edad, negativa (el caso de la app)
        CHECK(m.actualizarEntrada("pelota", con).empty(), "correrla 200 ms");
        esperar(2500);
        const json ep = m.estado()["entradas"]["pelota"];
        std::printf("pelota con desfase: cuadros %d → %d, sin datos hace %.1f s, edad %s\n", antes, ep.value("cuadros", 0),
                    ep.value("sin_datos_s", -1.0), ep["edad_video"].dump().c_str());
        const int despues = cuadros();
        esperar(1000);
        CHECK(cuadros() > despues + 10, "con el desfase sigue dando cuadros");
        CHECK(ep.value("sin_datos_s", 99.0) >= 0 && ep.value("sin_datos_s", 99.0) < 1.0, "y no queda «sin datos»");
        CHECK(ep["edad_video"].is_object(), "y sigue publicando su edad (aunque sea negativa: sale con hora futura)");
        m.quitarEntrada("pelota");
    }
    // Nombres que, limpios para GStreamer, quedan iguales («Cam 1» y «Cam-1»: Cam_1), y uno
    // con «@» (las vistas se piden «<qué>@<canal>»). Antes el segundo no entraba al pipeline.
    {
        CHECK(m.agregarEntrada("Cam 1", color("#FF0000")).empty(), "la entrada «Cam 1»");
        CHECK(m.agregarEntrada("Cam-1", color("#0000FF")).empty(), "la entrada «Cam-1»");
        CHECK(m.agregarEntrada("Cam@Sala", color("#00FF00")).empty(), "la entrada «Cam@Sala»");
        m.definirEscena({{"nombre", "Parecidas"},
                         {"capas", {{{"entrada", "Cam 1"}, {"x", 0}, {"y", 0}, {"ancho", 160}, {"alto", 180}},
                                    {{"entrada", "Cam-1"}, {"x", 160}, {"y", 0}, {"ancho", 160}, {"alto", 180}, {"orden", 1}}}}});
        CHECK(m.verPedir("escena:Parecidas@prueba", 320, 180).empty(), "ver la escena con «Cam 1» y «Cam-1»");
        CHECK(m.verPedir("entrada:Cam@Sala@prueba", 160, 90).empty(), "ver una entrada con «@» en el nombre");
        esperar(1500);
        const Px a = mirar(m, "escena:Parecidas@prueba", 80, 90), b = mirar(m, "escena:Parecidas@prueba", 240, 90);
        std::printf("Cam 1 | Cam-1: %s | %s\n", txt(a).c_str(), txt(b).c_str());
        CHECK(a.r > 200 && a.b < 60 && b.b > 200 && b.r < 60, "las dos se ven, cada una en su lugar");
        CHECK(igual(mirar(m, "entrada:Cam@Sala@prueba", 80, 45), 0, 255, 0), "la de «@» se ve (y no otra)");
        m.verSoltar("escena:Parecidas@prueba");
        m.verSoltar("entrada:Cam@Sala@prueba");
        for (const char* e : {"Cam 1", "Cam-1", "Cam@Sala"}) m.quitarEntrada(e);
    }
    // El PTZ digital conserva el encuadre cuando la cadena de la entrada se rearma (M6.8).
    if (hayFabrica("baptz") && hayFabrica("bacolor")) {
        const json ptz = json{{"video", json::array({{{"id", "ptz"}, {"elemento", "baptz"}}})}};
        CHECK(m.agregarEntrada("cam", json{{"pipeline", "videotestsrc is-live=true pattern=smpte"}, {"filtros", ptz}}).empty(),
              "una entrada con PTZ digital");
        esperar(1500);
        // El encuadre se ve (en la GPU lo hace baptzgl): x2 arriba a la izquierda de las
        // barras, el borde derecho cae en la verde (sin zoom, sería la azul).
        m.definirEscena({{"nombre", "Cam"}, {"capas", {{{"entrada", "cam"}}}}});
        CHECK(m.verPedir("escena:Cam", 320, 180).empty(), "ver la escena de la cámara");
        CHECK(m.propiedadEntrada("cam", "ba_f_ptz", "ir", "0.25,0.25,2,0").empty(), "zoom x2 arriba a la izquierda");
        esperar(1500);
        const Px der = mirar(m, "escena:Cam", 310, 40);
        std::printf("PTZ x2, borde derecho: %s\n", txt(der).c_str());
        CHECK(der.g > 120 && der.r < 80 && der.b < 80, "con el zoom, a la derecha la barra verde");
        m.verSoltar("escena:Cam");
        // La toma ENTERA (antes del recorte: el panel PTZ la muestra para manejar el
        // recorte encima): con el mismo zoom, el borde derecho es la última barra, la azul.
        CHECK(m.verPedir("entrada-entera:cam", 320, 180).empty(), "ver la toma entera");
        esperar(1500);
        const Px entera = mirar(m, "entrada-entera:cam", 310, 40);
        std::printf("PTZ x2, toma entera, borde derecho: %s\n", txt(entera).c_str());
        CHECK(entera.b > 120 && entera.r < 80 && entera.g < 80, "la toma entera no tiene el recorte (a la derecha, la azul)");
        m.verSoltar("entrada-entera:cam");
        // La MISMA cámara dos veces en una escena (dos bancas recortadas de una toma): cada
        // capa con su recorte. Antes la segunda chocaba de nombre con la primera y la app se caía.
        {
            // El recorte se calcula con el tamaño de verdad de la toma: recién rearmada (el PTZ de
            // arriba), con la máquina cargada, todavía no lo informó y el 320x240 de fábrica corría
            // el recorte una barra (salía la verde en vez de la azul).
            json sale = json::object();
            for (int i = 0; i < 100 && !sale.contains("ancho"); ++i) {
                sale = m.estado()["entradas"]["cam"].value("sale", json::object());
                if (!sale.contains("ancho")) esperar(50);
            }
            CHECK(sale.contains("ancho"), "la toma informa su tamaño");
            const int w = sale.value("ancho", 320), h = sale.value("alto", 240);
            std::printf("misma cámara dos veces: la toma mide %dx%d\n", w, h);
            const json izq{{"entrada", "cam"}, {"productor", "cam:entera:video"}, {"x", 0}, {"y", 0}, {"ancho", 160}, {"alto", 180},
                           {"orden", 0}, {"recorte", {{"izq", 0}, {"arr", 0}, {"der", w * 6 / 7}, {"aba", h / 2}}}};
            const json der{{"entrada", "cam"}, {"productor", "cam:entera:video"}, {"x", 160}, {"y", 0}, {"ancho", 160}, {"alto", 180},
                           {"orden", 1}, {"recorte", {{"izq", w * 6 / 7}, {"arr", 0}, {"der", 0}, {"aba", h / 2}}}};
            m.definirEscena({{"nombre", "Dos"}, {"capas", {izq, der}}});
            CHECK(m.verPedir("escena:Dos", 320, 180).empty(), "ver una escena con la misma cámara dos veces");
            const Px a = mirarHasta(m, "escena:Dos", 80, 90, [](const Px& p) { return p.r > 150 && p.g > 150 && p.b > 150; });
            const Px b = mirarHasta(m, "escena:Dos", 240, 90, [](const Px& p) { return p.b > 120 && p.r < 80 && p.g < 80; });
            std::printf("misma cámara dos veces: izquierda %s, derecha %s\n", txt(a).c_str(), txt(b).c_str());
            CHECK(a.r > 150 && a.g > 150 && a.b > 150, "la misma cámara dos veces: a la izquierda, la primera barra (gris claro)");
            CHECK(b.b > 120 && b.r < 80 && b.g < 80, "y a la derecha, la última (azul)");
            m.verSoltar("escena:Dos");
        }
        CHECK(m.propiedadEntrada("cam", "ba_f_ptz", "ir", "0.3,0.3,3,0").empty(), "ir a un encuadre");
        esperar(300);
        json p = m.propiedadesEntrada("cam", "ba_f_ptz");
        CHECK(std::fabs(p.value("zoom", 1.0) - 3.0) < 0.01, "el zoom quedó en 3");
        // Otra tarjeta antes: la cadena se rearma.
        json dos = ptz;
        dos["video"].insert(dos["video"].begin(), {{"id", "color"}, {"elemento", "bacolor"}});
        CHECK(m.actualizarEntrada("cam", json{{"pipeline", "videotestsrc is-live=true pattern=smpte"}, {"filtros", dos}}).empty(),
              "rearmar con otra tarjeta");
        esperar(1500);
        p = m.propiedadesEntrada("cam", "ba_f_ptz");
        CHECK(std::fabs(p.value("zoom", 1.0) - 3.0) < 0.01 && std::fabs(p.value("x", 0.5) - 0.3) < 0.01,
              "tras rearmar, el mismo encuadre");
        m.quitarEntrada("cam");
    }
    // Tarjetas de imagen (en la GPU, sus pares bacolorgl/barecortegl): un rojo sin
    // saturación es gris (su luma) y la mitad izquierda recortada queda transparente
    // (se ve la capa de abajo: la primera de la lista es la de arriba).
    if (hayFabrica("bacolor") && hayFabrica("barecorte")) {
        json f = json{{"video", json::array({{{"id", "color"}, {"elemento", "bacolor"}, {"propiedades", {{"saturacion", 0}}}},
                                             {{"id", "rec"}, {"elemento", "barecorte"}, {"propiedades", {{"izquierda", 0.5}}}}})}};
        json e = color("#FF0000");
        e["filtros"] = f;
        CHECK(m.agregarEntrada("gris", e).empty(), "una entrada con tarjetas de color y recorte");
        m.definirEscena({{"nombre", "Gris"}, {"capas", {{{"entrada", "gris"}}, {{"entrada", "verde"}}}}});
        CHECK(m.verPedir("escena:Gris", 320, 180).empty(), "ver la escena gris");
        esperar(2000);
        const Px der = mirar(m, "escena:Gris", 280, 90), izq = mirar(m, "escena:Gris", 40, 90);
        std::printf("tarjetas: derecha %s, izquierda %s\n", txt(der).c_str(), txt(izq).c_str());
        CHECK(igual(der, 54, 54, 54, 25), "el rojo sin saturación, gris");
        CHECK(igual(izq, 0, 255, 0), "lo recortado deja ver la capa de abajo");
        m.verSoltar("escena:Gris");
        m.quitarEntrada("gris");
    }
    m.verSoltar("programa");
    m.detener();
    std::printf("== test_motor_app: %s ==\n", fallos == 0 ? "OK" : "FALLOS");
    return fallos == 0 ? 0 : 1;
}
