// test_emisor.cpp — el emisor aparte (F3-01) sobrevive a la caída de «A»
//
// Un «A» de mentira (este mismo ejecutable con --publicar) publica un canal de video
// en NV12 (SpoutSender, como BA.Emisor.<id>), un anillo de audio (como
// BA.Audio.Emisor.<id>) y el de la pista 2. El emisor graba lo que llega (con la pista
// 2) y tiene la repetición prendida. A los 2 s se mata a «A» con SIGKILL; el emisor pasa
// a la placa; 2 s después vuelve otro «A» y el emisor vuelve a la señal; se guarda un
// clip de la repetición. Al final:
//   - UN solo archivo (sin «-parte2»): la grabación no se cortó;
//   - dura lo que duró todo (con la placa en el hueco), con video y DOS audios;
//   - el estado cuenta la placa; el clip está;
//   - el archivo pasa a MP4 (lo que hace el emisor al cerrar en orden).
// Y lo que no necesita procesos: el pedido ida y vuelta por JSON (con destinos extra,
// señal limpia, repetición y pistas), las claves fuera del pedido (en el
// CredentialStore) y nombreLibre.

#include "Emisor.hpp"
#include "Pedido.hpp"
#include "Piezas.hpp"
#include "Reloj.hpp"
#include "Salidas.hpp"
#include "ipc/HealthBlock.hpp"
#include "ipc/SharedAudioRing.hpp"
#include "ipc/SpoutTransport.hpp"

#include <gst/gst.h>
#include <gst/pbutils/pbutils.h>

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

#if defined(_WIN32)
int main() {
    std::printf("SKIP: la prueba mata procesos con SIGKILL (POSIX)\n");
    return 77;
}
#else
#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

extern char** environ;

using json = nlohmann::json;

namespace {
int fallos = 0;
#define CHECK(cond, msg)                                                     \
    do {                                                                     \
        if (!(cond)) {                                                       \
            std::printf("FALLO: %s (%s, linea %d)\n", msg, #cond, __LINE__); \
            ++fallos;                                                        \
        }                                                                    \
    } while (0)

constexpr int kAncho = 320, kAlto = 180;

std::wstring ancho(const std::string& s) { return std::wstring(s.begin(), s.end()); }

/// «A»: publica video (NV12, 30 cps, un gris que cambia) y audio (un seno, y otro en la
/// pista 2) por `seg` segundos.
int publicar(const std::string& canalV, const std::string& canalA, int seg, int generacion) {
    ba::ipc::SpoutSender tx;
    ba::ipc::SlotConfig cfg{kAncho, kAlto};
    cfg.formato = ba::ipc::FormatoCanal::NV12;
    if (!tx.publish(ancho(canalV).c_str(), cfg)) return 1;
    ba::ipc::SharedAudioRing ring, ring2;
    if (!ba::ipc::SharedAudioRing::init(ancho(canalA).c_str(), 2, 16, static_cast<std::uint32_t>(getpid()),
                                        static_cast<std::uint32_t>(generacion), ring)
             .ok ||
        !ba::ipc::SharedAudioRing::init(ancho(canalA + ".pista2").c_str(), 2, 16, static_cast<std::uint32_t>(getpid()),
                                        static_cast<std::uint32_t>(generacion), ring2)
             .ok)
        return 1;
    std::vector<std::uint8_t> px(ba::ipc::bytesDeCuadro(kAncho, kAlto, ba::ipc::FormatoCanal::NV12));
    std::vector<float> bloque(ba::ipc::audio_ring::kBlockFrames * 2);
    const auto fin = std::chrono::steady_clock::now() + std::chrono::seconds(seg);
    std::uint64_t cuadro = 0, muestra = 0;
    auto proxVideo = std::chrono::steady_clock::now(), proxAudio = proxVideo;
    while (std::chrono::steady_clock::now() < fin) {
        const auto ahora = std::chrono::steady_clock::now();
        if (ahora >= proxAudio) {
            for (std::size_t i = 0; i < ba::ipc::audio_ring::kBlockFrames; ++i, ++muestra)
                bloque[2 * i] = bloque[2 * i + 1] = 0.2f * std::sin(2.0 * M_PI * 440.0 * static_cast<double>(muestra) / 48000.0);
            ring.push(bloque.data(), ba::ipc::HealthBlock::nowQpc());
            ring2.push(bloque.data(), ba::ipc::HealthBlock::nowQpc());
            proxAudio += std::chrono::milliseconds(10);
        }
        if (ahora >= proxVideo) {
            std::fill(px.begin(), px.begin() + kAncho * kAlto, static_cast<std::uint8_t>(cuadro * 3)); // Y
            std::fill(px.begin() + kAncho * kAlto, px.end(), static_cast<std::uint8_t>(128));        // UV: gris
            ba::ipc::FrameMeta m;
            m.frameIndex = ++cuadro;
            m.qpc = ba::ipc::HealthBlock::nowQpc();
            tx.send(px.data(), px.size(), m);
            proxVideo += std::chrono::microseconds(33333);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    return 0;
}

pid_t lanzarA(const char* yo, const std::string& canalV, const std::string& canalA, int seg, int gen) {
    const std::string s = std::to_string(seg), g = std::to_string(gen);
    std::vector<char*> args{const_cast<char*>(yo),          const_cast<char*>("--publicar"), const_cast<char*>(canalV.c_str()),
                            const_cast<char*>(canalA.c_str()), const_cast<char*>(s.c_str()), const_cast<char*>(g.c_str()), nullptr};
    pid_t pid = 0;
    if (posix_spawn(&pid, yo, nullptr, nullptr, args.data(), environ) != 0) return 0;
    return pid;
}

struct Info {
    bool ok = false;
    double seg = 0;
    int videos = 0, audios = 0;
};

Info mirar(const std::string& ruta) {
    Info r;
    gchar* uri = gst_filename_to_uri(ruta.c_str(), nullptr);
    GstDiscoverer* d = gst_discoverer_new(10 * GST_SECOND, nullptr);
    GstDiscovererInfo* i = gst_discoverer_discover_uri(d, uri, nullptr);
    if (i != nullptr && gst_discoverer_info_get_result(i) == GST_DISCOVERER_OK) {
        r.ok = true;
        r.seg = static_cast<double>(gst_discoverer_info_get_duration(i)) / 1e9;
        GList* v = gst_discoverer_info_get_video_streams(i);
        GList* a = gst_discoverer_info_get_audio_streams(i);
        r.videos = static_cast<int>(g_list_length(v));
        r.audios = static_cast<int>(g_list_length(a));
        gst_discoverer_stream_info_list_free(v);
        gst_discoverer_stream_info_list_free(a);
    }
    if (i != nullptr) gst_discoverer_info_unref(i);
    g_object_unref(d);
    g_free(uri);
    return r;
}

/// Atiende al emisor `ms` milisegundos (como el bucle de main).
void correr(ba::emisor::Emisor& em, int ms) {
    for (int t = 0; t < ms; t += 100) {
        em.tic();
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

/// Atiende hasta que `cond` (o vence `ms`); true si se cumplió.
template <class F>
bool hasta(ba::emisor::Emisor& em, int ms, F cond) {
    for (int t = 0; t < ms; t += 100) {
        em.tic();
        if (cond()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    return cond();
}

void sinProcesos() {
    // El pedido, ida y vuelta.
    ba::emisor::Pedido p;
    p.seq = 7;
    p.ancho = 1280;
    p.alto = 720;
    p.fpsN = 30000;
    p.fpsD = 1001;
    p.placaTexto = "Volvemos enseguida";
    ba::motor::Transmision::Config t;
    t.url = "rtmp://127.0.0.1/live/clave";
    t.urlRespaldo = "rtmp://otro/live/clave";
    t.kbps = 4500;
    t.control = ba::motor::ControlTasa::VBR;
    p.transmision = t;
    ba::motor::Grabacion::Config g;
    g.carpeta = "/tmp/x";
    g.nombre = "ba-1";
    g.control = ba::motor::ControlTasa::Calidad;
    g.crf = 20;
    g.carpetaRespaldo = "/tmp/y";
    p.grabacion = g;
    p.pistas = {2, 4};
    p.remux = "mp4";
    ba::motor::Transmision::Config x;
    x.url = "srt://otro:9000?passphrase=secreta";
    p.extras["yt"] = x;
    ba::emisor::PedidoRepeticion rep;
    rep.segundos = 20;
    rep.carpeta = "/tmp/r";
    rep.guardarSeq = 3;
    rep.guardarEn = "/tmp/r/clip.mkv";
    p.repeticion = rep;
    ba::emisor::PedidoLimpio lim;
    ba::motor::Transmision::Config lt;
    lt.url = "rtmp://limpio/live/otraclave";
    lim.transmision = lt;
    p.limpio = lim;
    const ba::emisor::Pedido q = ba::emisor::pedidoDe(ba::emisor::aJson(p));
    CHECK(ba::emisor::aJson(q) == ba::emisor::aJson(p), "el pedido ida y vuelta");
    CHECK(q.transmision && q.transmision->control == ba::motor::ControlTasa::VBR, "el control de tasa");
    CHECK(!q.copia && q.hayAire(), "sin copia, con aire");
    CHECK(q.pistas == std::vector<int>({2, 4}) && q.remux == "mp4", "las pistas y el remux");
    CHECK(q.extras.size() == 1 && q.limpio && q.limpio->transmision && q.repeticion && q.repeticion->guardarSeq == 3,
          "destinos, señal limpia y repetición");
    // Pistas fuera de 2-6 y un remux que no es mp4/mov no pasan.
    const ba::emisor::Pedido mal = ba::emisor::pedidoDe(json{{"pistas", {1, 2, 7, 2}}, {"remux", "avi"}});
    CHECK(mal.pistas == std::vector<int>({2}) && mal.remux.empty(), "pistas y remux saneados");

    // Las claves: el pedido en disco no las tiene; están cifradas en Credenciales/ y vuelven al leerlo.
    {
        const auto raiz = std::filesystem::temp_directory_path() / ("ba-test-pedido-" + std::to_string(getpid()));
        const auto rt = raiz / "Runtime";
        std::string err;
        CHECK(ba::emisor::escribir(rt, "Sala_2", p, &err), "se escribe el pedido");
        std::ifstream in(ba::emisor::rutaPedido(rt, "Sala_2"));
        const std::string txt((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        CHECK(!txt.empty() && txt.find("clave") == std::string::npos && txt.find("secreta") == std::string::npos &&
                  txt.find("otraclave") == std::string::npos,
              "el pedido en disco no tiene las claves");
        const std::string cred = ba::emisor::nombreCredencial("Sala_2");
        CHECK(cred == "emisor-.sala_2", "el nombre de la credencial (mayúsculas con punto)");
        CHECK(std::filesystem::exists(raiz / "Credenciales" / (cred + ".cred")), "las claves en el CredentialStore");
        ba::emisor::Pedido l;
        CHECK(ba::emisor::leer(rt, "Sala_2", l), "se lee");
        CHECK(ba::emisor::aJson(l) == ba::emisor::aJson(p), "con las claves de vuelta");
        // Sin nada que transmitir, la credencial se borra.
        ba::emisor::Pedido solo;
        solo.grabacion = g;
        CHECK(ba::emisor::escribir(rt, "Sala_2", solo, &err), "se reescribe sin transmisión");
        CHECK(!std::filesystem::exists(raiz / "Credenciales" / (cred + ".cred")), "sin claves, sin credencial");
        // Un pedido de antes (con la URL adentro) también se entiende.
        std::ofstream(ba::emisor::rutaPedido(rt, "viejo")) << ba::emisor::aJson(p).dump();
        ba::emisor::Pedido v;
        CHECK(ba::emisor::leer(rt, "viejo", v) && v.transmision && v.transmision->url == t.url, "un pedido de antes");
        std::filesystem::remove_all(raiz);
    }
    std::string largo(32, 'A');
    CHECK(ba::emisor::nombreCredencial(largo).rfind("emisor-h", 0) == 0, "un id largo con mayúsculas: su huella");
    // Un archivo a medias (tipos malos) no rompe: lo que falta queda de fábrica.
    const ba::emisor::Pedido r = ba::emisor::pedidoDe(json{{"seq", "x"}, {"video", {{"ancho", "mucho"}}}, {"grabacion", 3}});
    CHECK(r.seq == 0 && r.ancho == 1920 && !r.grabacion && !r.hayAire(), "un pedido roto");
    // nombreLibre: no pisa lo que ya está.
    const auto dir = std::filesystem::temp_directory_path() / ("ba-test-nombre-" + std::to_string(getpid()));
    std::filesystem::create_directories(dir);
    CHECK(ba::emisor::nombreLibre(dir, "ba-1") == "ba-1", "carpeta vacía: el mismo nombre");
    std::ofstream(dir / "ba-1-00000.mkv") << "x";
    std::ofstream(dir / "ba-10-00000.mkv") << "x";
    CHECK(ba::emisor::nombreLibre(dir, "ba-1") == "ba-1-sigue2", "con el tramo: -sigue2");
    std::ofstream(dir / "ba-1-sigue2-parte2-00000.mkv") << "x";
    CHECK(ba::emisor::nombreLibre(dir, "ba-1") == "ba-1-sigue3", "con una parte de -sigue2: -sigue3");
    CHECK(ba::emisor::nombreLibre(dir, "ba-2") == "ba-2", "otro nombre no choca");
    std::filesystem::remove_all(dir);
}

} // namespace

int main(int argc, char** argv) {
    if (argc == 6 && std::string(argv[1]) == "--publicar") return publicar(argv[2], argv[3], std::atoi(argv[4]), std::atoi(argv[5]));
    sinProcesos();
#if defined(BA_TEST_GST_RAIZ)
    g_setenv("BA_GST_RAIZ", BA_TEST_GST_RAIZ, FALSE);
#endif
#if defined(BA_TEST_GST_PLUGINS)
    g_setenv("BA_GST_PLUGINS_EXTRA", BA_TEST_GST_PLUGINS, FALSE);
#endif
    std::string err;
    if (!ba::motor::iniciarGStreamer(&err)) {
        std::printf("SKIP: sin GStreamer (%s)\n", err.c_str());
        return fallos == 0 ? 77 : 1;
    }
    for (const char* e : {"bavideoanillo", "baanillo", "intersink", "input-selector", "imagefreeze", "audiomixer", "splitmuxsink",
                          "matroskamux", "avenc_aac", "textoverlay"})
        if (!ba::motor::hayFabrica(e)) {
            std::printf("SKIP: falta %s\n", e);
            return fallos == 0 ? 77 : 1;
        }
    if (ba::motor::elegirCodificador().empty()) {
        std::printf("SKIP: sin codificador H.264\n");
        return fallos == 0 ? 77 : 1;
    }
    ba::motor::ponerGpuVideo(false);
    ba::motor::Reloj reloj;
    const std::string sufijo = std::to_string(getpid());
    const std::string canalV = "BA.Prueba.Emisor." + sufijo, canalA = "BA.Audio.Prueba.Emisor." + sufijo;
    const auto dir = std::filesystem::temp_directory_path() / ("ba-test-emisor-" + sufijo);
    std::filesystem::create_directories(dir);

    ba::emisor::Emisor em(reloj, {"prueba", canalV, canalA, ""});
    ba::emisor::Pedido p;
    p.seq = 1;
    p.ancho = kAncho;
    p.alto = kAlto;
    ba::motor::Grabacion::Config g;
    g.carpeta = dir.string();
    g.nombre = "prueba";
    g.tramoSeg = 0; // un solo archivo: se ve si se partió
    g.kbps = 600;
    g.kbpsAudio = 96;
    g.gopSeg = 1;
    g.fps = 30;
    g.colchonSeg = 5;
    p.grabacion = g;
    p.pistas = {2}; // la pista 2, por su anillo
    p.remux = "mp4";
    ba::emisor::PedidoRepeticion rep;
    rep.segundos = 6;
    rep.carpeta = (dir / "repeticion").string();
    rep.kbps = 600;
    p.repeticion = rep;

    const auto t0 = std::chrono::steady_clock::now();
    pid_t a1 = lanzarA(argv[0], canalV, canalA, 30, 1);
    CHECK(a1 > 0, "se lanza el primer A");
    std::this_thread::sleep_for(std::chrono::milliseconds(300)); // que publique
    em.aplicar(p);
    CHECK(hasta(em, 4000, [&] { return !em.enPlaca(); }), "con A publicando sale la señal");
    correr(em, 1500);
    const json antes = em.estado();
    CHECK(antes["grabacion"].is_object() && antes["grabacion"].value("grabando", false), "graba");

    // A se cae.
    kill(a1, SIGKILL);
    waitpid(a1, nullptr, 0);
    CHECK(hasta(em, 2000, [&] { return em.enPlaca(); }), "sin A, a la placa (antes de 2 s)");
    correr(em, 2000);
    CHECK(em.estado()["grabacion"].value("grabando", false), "la grabación sigue en la placa");

    // Vuelve otro A (otra instancia del canal y otra generación del anillo).
    pid_t a2 = lanzarA(argv[0], canalV, canalA, 30, 2);
    CHECK(a2 > 0, "se lanza el segundo A");
    CHECK(hasta(em, 5000, [&] { return !em.enPlaca(); }), "con A de vuelta, la señal");
    correr(em, 2000);
    const json despues = em.estado();
    std::printf("estado: %s\n", despues.dump().c_str());
    CHECK(despues.value("placas", 0) == 1, "una sola vez a la placa");
    CHECK(despues["audio_hace_ms"].is_number() && despues["audio_hace_ms"].get<int>() < 1000, "llega el audio del segundo A");
    CHECK(despues["pistas"] == json::array({2}), "graba la pista 2");
    CHECK(despues["replay"].is_object() && despues["replay"].value("activa", false), "la repetición anda");
    // Guardar un clip: el pedido sube guardarSeq.
    const std::string clip = (dir / "repeticion" / "clip.mkv").string();
    p.seq = 2;
    p.repeticion->guardarSeq = 1;
    p.repeticion->guardarEn = clip;
    em.aplicar(p);
    CHECK(hasta(em, 8000, [&] { return em.estado()["replay"].value("ultimo", std::string()) == clip; }), "se guardó el clip");
    CHECK(std::filesystem::exists(clip), "el clip está");
    // Lo que hace main al cerrar en orden: parar y pasar lo grabado a MP4.
    const std::vector<std::string> grabados = em.parar();
    CHECK(grabados.size() == 1, "parar dice el archivo de la grabación");
    const double total = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    kill(a2, SIGKILL);
    waitpid(a2, nullptr, 0);

    std::vector<std::filesystem::path> mkv;
    for (const auto& e : std::filesystem::directory_iterator(dir))
        if (e.path().extension() == ".mkv") mkv.push_back(e.path());
    CHECK(mkv.size() == 1, "un solo archivo (no se partió)");
    if (mkv.size() == 1) {
        CHECK(mkv[0].filename() == "prueba-00000.mkv", "el de siempre, sin «-parte2»");
        const Info i = mirar(mkv[0].string());
        std::printf("archivo: %s, %.2f s (todo: %.2f s), %d video, %d audio\n", mkv[0].filename().string().c_str(), i.seg, total,
                    i.videos, i.audios);
        CHECK(i.ok && i.videos == 1 && i.audios == 2, "abre, con video y dos audios (programa y pista 2)");
        CHECK(i.seg > total - 2.5, "dura lo que duró todo (la placa en el hueco)");
        const std::string e = ba::motor::remuxarYVerificar(mkv[0].string(), "mp4", true);
        CHECK(e.empty(), "pasa a MP4");
        auto mp4 = mkv[0];
        mp4.replace_extension(".mp4");
        CHECK(std::filesystem::exists(mp4) && !std::filesystem::exists(mkv[0]), "el MP4 queda, el MKV se va");
        const Info m = mirar(mp4.string());
        CHECK(m.ok && m.videos == 1 && m.audios == 2, "el MP4 abre, con lo mismo");
    }
    std::filesystem::remove_all(dir);
    if (fallos == 0) std::printf("OK: test_emisor\n");
    return fallos == 0 ? 0 : 1;
}
#endif
