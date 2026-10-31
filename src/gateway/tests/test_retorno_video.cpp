// test_retorno_video.cpp — el video del programa hacia las sesiones: copias y labios
//
// docs/arquitectura/04-plan-unificado.md, F2-13 y F2-11. Con canales de verdad (memoria
// compartida), el MediaPump entero y el backend NULL de WebRTC (cuenta lo que se empuja):
//   1. Copias por cuadro (F2-13): el mismo programa visto por 8 sesiones, con A publicando
//      como antes (1920×1080, que B achicaba en la CPU) y como ahora (1280×720, el tamaño de
//      las sesiones, escalado en la GPU de A). Se miden en B los bytes leídos del canal,
//      los escalados y los convertidos a I420, y las conversiones por cuadro: antes una por
//      sesión (cada una convertía su copia: se reproduce con pushLocalVideo de a una), ahora
//      una para todas (CuadroSaliente). Con los de A (la vista: lo que baja de la GPU, la
//      pasada de formato en la CPU y las copias de Vistas), la cuenta entera por cuadro.
//   2. Invitados entrantes a su tamaño (V3): 640×360 en un canal de 1280×720 viaja a 640×360
//      (no se agranda); uno parado (360×640) en una caja con la forma del canal sin agrandar
//      (1138×640); uno más grande que el canal, al tamaño del canal.
//   3. Labios del retorno para quien MIRA (F2-11), con destello y pulso sintéticos: A
//      escribe el audio 150 ms después de su hora (la latencia de la mezcla) y publica el
//      video 20 ms después. Un destello (un cuadro) y un pulso (el bloque de audio de la
//      misma hora): a la pantalla (la fila, sin conversar) el destello le tiene que salir
//      con el pulso (< 20 ms); al invitado que conversa, ya (T4: ~130 ms antes que el
//      audio, como le salía a todos antes).

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "Labios.hpp"
#include "Log.hpp"
#include "MediaPump.hpp"
#include "Webrtc.hpp"
#include "ipc/HealthBlock.hpp"
#include "ipc/SharedAudioRing.hpp"
#include "ipc/SpoutTransport.hpp"
#include "platform/Ritmo.hpp"

#if defined(_WIN32)
#include <process.h>
#define ba_getpid _getpid
#else
#include <unistd.h>
#define ba_getpid getpid
#endif

using namespace ba::gateway;
using ba::ipc::FrameMeta;
using ba::ipc::HealthBlock;
using Reloj = std::chrono::steady_clock;
using namespace std::chrono_literals;

namespace {

int g_fallas = 0;
#define CHECK(c, m)                                             \
    do {                                                        \
        if (!(c)) {                                             \
            std::printf("FALLO: %s (linea %d)\n", m, __LINE__); \
            ++g_fallas;                                         \
        } else {                                                \
            std::printf("ok: %s\n", m);                         \
        }                                                       \
    } while (0)

constexpr std::uint64_t kMs = 1'000'000ULL;

std::wstring w(const std::string& s) { return std::wstring(s.begin(), s.end()); }

std::string prefijo(const char* que) {
    return std::string("BA.TestRetVid.") + que + "." + std::to_string(ba_getpid()) + ".";
}

MediaPump::Cfg cfgDe(const std::string& pre) {
    MediaPump::Cfg cfg;
    for (auto* v : {&cfg.guestAudioNames, &cfg.guestVideoNames, &cfg.shareVideoNames}) v->emplace_back();
    cfg.guestVideoNames.push_back(w(pre + "Guest.1"));
    cfg.shareVideoNames.push_back(w(pre + "GuestShare.1"));
    cfg.guestAudioNames.push_back(w(pre + "Audio.Guest.1"));
    cfg.lugaresIniciales = 1;
    cfg.programVideoName = w(pre + "Program");
    cfg.returnAudioName = w(pre + "Audio.Content");
    return cfg;
}

void conectar(WebrtcHub& hub, int sesiones) {
    for (int s = 1; s <= sesiones; ++s) {
        WebrtcSession* x = hub.session(s);
        x->configure(SessionCallbacks{}, nullptr);
        x->setRemoteOffer("v=0");
    }
}

// --- 1. Copias por cuadro --------------------------------------------------------------

struct EnB {
    double leidos = 0, escalados = 0, convertidos = 0, conversiones = 0, cuadros = 0;
};

/// A publica el programa a `ancho`×`alto` durante ~1 s; 8 sesiones lo miran. Lo que hizo B por cuadro.
EnB medirB(const char* que, int ancho, int alto, bool porSesion) {
    const std::string pre = prefijo(que);
    WebrtcHub hub(8, 320, 180, 48000, 2);
    conectar(hub, 8);
    Log log;
    MediaPump pump(hub, cfgDe(pre), log);
    ba::ipc::SpoutSender a;
    a.publish(w(pre + "Program").c_str(), ba::ipc::SlotConfig{ancho, alto, 33'333'333});
    pump.start();
    for (int s = 1; s <= 8; ++s) pump.setRoute(s, s <= 4 ? MediaRoute::program(1) : MediaRoute::deSalida(""));
    std::vector<std::uint8_t> px(static_cast<std::size_t>(ancho) * alto * 4, 0x55);
    std::this_thread::sleep_for(600ms); // que B se suscriba
    const nlohmann::json c0 = pump.copiasVideo();
    std::vector<std::uint8_t> escalado(1280u * 720u * 4u, 0x55);
    auto proximo = Reloj::now();
    for (std::uint64_t i = 1; i <= 30; ++i) {
        proximo += 33ms;
        ba::platform::dormirHasta(proximo);
        const std::uint64_t q = HealthBlock::nowQpc();
        a.send(px.data(), px.size(), FrameMeta{i, q, q});
        // Antes: cada sesión convertía su copia (el BGRA ya achicado al tamaño de la sesión).
        if (porSesion)
            for (int s = 1; s <= 8; ++s) hub.session(s)->pushLocalVideo(escalado.data(), escalado.size(), 1280, 720);
    }
    std::this_thread::sleep_for(500ms);
    const nlohmann::json c1 = pump.copiasVideo();
    pump.stop();
    EnB r;
    const auto d = [&](const char* k) { return static_cast<double>(c1.value(k, 0ULL) - c0.value(k, 0ULL)); };
    r.cuadros = d("cuadros");
    const double n = std::max(1.0, r.cuadros);
    r.leidos = d("leidos_bytes") / n;
    r.escalados = d("escalados_bytes") / n;
    r.convertidos = d("convertidos_bytes") / n;
    r.conversiones = d("conversiones") / n;
    return r;
}

void testCopias() {
    std::printf("-- copias por cuadro del programa con 8 sesiones mirando (F2-13) --\n");
    // Antes: A publicaba la salida entera; B leía, achicaba y cada sesión convertía su copia.
    const EnB antes = medirB("antes", 1920, 1080, true);
    // Ahora: A publica al tamaño de la sesión; B lee y convierte una vez.
    const EnB ahora = medirB("ahora", 1280, 720, false);
    CHECK(antes.cuadros >= 25 && ahora.cuadros >= 25, "B leyó los cuadros");
    CHECK(ahora.escalados == 0, "ahora B no escala nada (A lo manda al tamaño de la sesión)");
    CHECK(std::fabs(ahora.conversiones - 1.0) < 0.05, "ahora: una conversión a I420 por cuadro, para las 8 sesiones");
    CHECK(antes.conversiones >= 8.5, "antes: una por sesión (más la de B)");
    // A: lo que pasaba por la CPU en la vista del retorno, por cuadro (Vistas.cpp, Piezas.cpp).
    //   antes: gldownload de RGBA a 1920×1080 + videoconvert RGBA→BGRA (lee y escribe) +
    //          Vistas: assign al Cuadro + copia para el aviso + el envío al canal;
    //   ahora: gldownload de BGRA a 1280×720 (ya convertido y chico en la GPU) + el envío al
    //          canal (Vistas entrega el buffer mismo: test_salidas mira que sea el mismo).
    const double grande = 1920.0 * 1080 * 4, chico = 1280.0 * 720 * 4;
    const double aAntes = grande /*bajada*/ + 2 * grande /*videoconvert*/ + grande /*assign*/ + grande /*copia*/ + grande /*canal*/;
    const double aAhora = chico /*bajada*/ + chico /*canal*/;
    // B: lo leído del canal, lo escalado (lee el grande y escribe el chico) y cada conversión (lee
    // el BGRA y escribe el I420).
    const double i420 = 1280.0 * 720 * 1.5;
    const double bAntes = antes.leidos + (antes.escalados > 0 ? grande + antes.escalados : 0) + antes.conversiones * (chico + i420);
    const double bAhora = ahora.leidos + (ahora.escalados > 0 ? grande + ahora.escalados : 0) + ahora.conversiones * (chico + i420);
    std::printf("  %-8s A %6.1f MB  B %6.1f MB (leído %.1f, escalado %.1f, %.1f conversiones)  total %6.1f MB por cuadro, %6.0f MB/s a 30 cps\n",
                "antes", aAntes / 1e6, bAntes / 1e6, antes.leidos / 1e6, antes.escalados / 1e6, antes.conversiones,
                (aAntes + bAntes) / 1e6, (aAntes + bAntes) * 30 / 1e6);
    std::printf("  %-8s A %6.1f MB  B %6.1f MB (leído %.1f, escalado %.1f, %.1f conversiones)  total %6.1f MB por cuadro, %6.0f MB/s a 30 cps\n",
                "ahora", aAhora / 1e6, bAhora / 1e6, ahora.leidos / 1e6, ahora.escalados / 1e6, ahora.conversiones,
                (aAhora + bAhora) / 1e6, (aAhora + bAhora) * 30 / 1e6);
    CHECK((aAhora + bAhora) < 0.25 * (aAntes + bAntes), "menos de un cuarto de los bytes por cuadro");
}

// --- 2. Invitados entrantes a su tamaño -------------------------------------------------

void testEntrantes() {
    std::printf("-- invitados entrantes a su tamaño (F2-13, V3) --\n");
    const std::string pre = prefijo("entra");
    WebrtcHub hub(2, 320, 180, 48000, 2);
    conectar(hub, 1);
    Log log;
    MediaPump::Cfg cfg = cfgDe(pre);
    cfg.programW = 1280;
    cfg.programH = 720;
    MediaPump pump(hub, cfg, log);
    pump.start();
    pump.setRoute(1, MediaRoute::program(1));
    ba::ipc::SpoutReceiver rx;
    CHECK(rx.subscribe(w(pre + "Guest.1").c_str()), "la app se suscribe a la cámara del lugar 1");
    std::vector<std::uint8_t> dst(1280u * 720u * 4u);
    // Manda `ancho`×`alto` de un color hasta leer uno de los suyos (entre medio puede pasar la
    // placa del lugar vacío). El de color `valor` en el medio es el suyo.
    const auto mandar = [&](int ancho, int alto, std::uint8_t valor, FrameMeta& m) {
        std::vector<std::uint8_t> px(static_cast<std::size_t>(ancho) * alto * 4, valor);
        for (int i = 0; i < 50; ++i) {
            pump.forwardVideo(1, px.data(), px.size(), ancho, alto);
            std::fill(dst.begin(), dst.end(), 0);
            if (rx.receive(dst.data(), dst.size(), m)) {
                const std::size_t wC = m.ancho != 0 ? m.ancho : 1280u, hC = m.alto != 0 ? m.alto : 720u;
                if (dst[((hC / 2) * wC + wC / 2) * 4u] == valor) return true;
            }
            std::this_thread::sleep_for(10ms);
        }
        return false;
    };
    FrameMeta m{};
    CHECK(mandar(640, 360, 0x31, m) && m.ancho == 640 && m.alto == 360, "640×360 viaja a 640×360 (no se agranda)");
    CHECK(dst[640u * 360u * 4u - 1] == 0x31 && dst[640u * 360u * 4u] == 0, "solo sus bytes, juntos");
    // Parado: una caja con la forma del canal, con barras, a su escala.
    const bool ok = mandar(360, 640, 0x32, m);
    CHECK(ok && m.ancho == 1138 && m.alto == 640, "parado (360×640): en 1138×640, sin agrandar");
    CHECK(ok && dst[(320u * 1138u + 569u) * 4u] == 0x32 && dst[(320u * 1138u + 10u) * 4u] == 0, "con barras a los costados");
    CHECK(mandar(1920, 1080, 0x33, m) && m.ancho == 0 && m.alto == 0, "más grande que el canal: al tamaño del canal");
    pump.stop();
}

// --- 3. Labios para quien mira ----------------------------------------------------------

void testLabios() {
    std::printf("-- labios del retorno para quien mira (F2-11), destello + pulso --\n");
    const std::string pre = prefijo("labios");
    WebrtcHub hub(2, 320, 180, 48000, 2);
    conectar(hub, 2);
    Log log;
    MediaPump pump(hub, cfgDe(pre), log);
    // A: el audio del programa (sellado con su hora, escrito 150 ms después) y su video
    // (con la hora de su contenido, publicado 20 ms después).
    constexpr std::uint64_t kAudioTarde = 150 * kMs, kVideoTarde = 20 * kMs;
    ba::ipc::SharedAudioRing audio;
    CHECK(ba::ipc::SharedAudioRing::init(w(pre + "Audio.Content").c_str(), 2, 16, static_cast<std::uint32_t>(ba_getpid()), 1, audio).ok,
          "el anillo del audio del programa");
    ba::ipc::SpoutSender video;
    CHECK(video.publish(w(pre + "Program").c_str(), ba::ipc::SlotConfig{320, 180, 33'333'333}), "el canal del programa");
    pump.start();
    pump.setRoute(1, MediaRoute::program(1)); // el invitado que conversa
    pump.setRoute(2, MediaRoute::deSalida("")); // una pantalla: mira
    std::atomic<bool> fin{false}, pausa{false};
    // El pulso: el bloque cuya hora alcanza la del destello; cuándo lo escribió A.
    std::atomic<std::uint64_t> destelloQpc{0}, pulsoEscritoQpc{0};
    std::thread escritorAudio([&] {
        std::vector<float> bloque(480 * 2, 0.0f);
        auto proximo = Reloj::now();
        while (!fin.load()) {
            proximo += 10ms;
            ba::platform::dormirHasta(proximo);
            const std::uint64_t ahora = HealthBlock::nowQpc();
            const std::uint64_t sello = ahora - kAudioTarde;
            const std::uint64_t d = destelloQpc.load();
            const bool pulso = d != 0 && sello >= d && pulsoEscritoQpc.load() == 0;
            std::fill(bloque.begin(), bloque.end(), pulso ? 0.9f : 0.0f);
            audio.push(bloque.data(), sello);
            if (pulso) pulsoEscritoQpc.store(HealthBlock::nowQpc());
        }
    });
    std::atomic<std::uint64_t> cuadro{0};
    std::thread escritorVideo([&] {
        std::vector<std::uint8_t> px(320u * 180u * 4u, 0x10);
        auto proximo = Reloj::now();
        while (!fin.load()) {
            proximo += 33ms;
            ba::platform::dormirHasta(proximo);
            if (pausa.load()) continue;
            const std::uint64_t ahora = HealthBlock::nowQpc();
            video.send(px.data(), px.size(), FrameMeta{cuadro.fetch_add(1) + 1, ahora, ahora - kVideoTarde});
        }
    });
    // Lo que se empuja a cada sesión, con su hora (el backend NULL cuenta: se mira cada 0,25 ms).
    struct Marca {
        std::uint64_t qpc;
        std::uint64_t v1, v2, a2;
    };
    std::mutex marcasMu;
    std::vector<Marca> marcas;
    std::thread mirar([&] {
        Marca ant{0, 0, 0, 0};
        while (!fin.load()) {
            Marca m{HealthBlock::nowQpc(), hub.session(1)->localVideoFrames(), hub.session(2)->localVideoFrames(),
                    hub.session(2)->localAudioBlocks()};
            if (m.v1 != ant.v1 || m.v2 != ant.v2 || m.a2 != ant.a2) {
                std::lock_guard<std::mutex> l(marcasMu);
                marcas.push_back(m);
            }
            ant = m;
            std::this_thread::sleep_for(250us);
        }
    });
    std::this_thread::sleep_for(1500ms); // que B abra todo y mida lo que tarda el audio
    std::vector<double> desfaseMira, desfaseConversa;
    for (int vez = 0; vez < 4; ++vez) {
        // Una pausa del video (la cola de la pantalla se vacía) y el destello: el primer cuadro después.
        pausa.store(true);
        std::this_thread::sleep_for(350ms);
        const std::uint64_t antes = HealthBlock::nowQpc();
        destelloQpc.store(0);
        pulsoEscritoQpc.store(0);
        const std::uint64_t n0 = cuadro.load();
        pausa.store(false);
        while (cuadro.load() == n0) std::this_thread::sleep_for(1ms);
        destelloQpc.store(HealthBlock::nowQpc() - kVideoTarde); // la hora del destello (la del cuadro, ±1 ms)
        std::this_thread::sleep_for(400ms);
        // Del registro: el primer cuadro a cada sesión después de la pausa (el destello) y el
        // primer bloque a la pantalla después de que A escribió el pulso.
        std::uint64_t tV1 = 0, tV2 = 0, tA = 0;
        std::uint64_t v1 = 0, v2 = 0, a2 = 0;
        const std::uint64_t pulso = pulsoEscritoQpc.load();
        std::vector<Marca> vistas;
        {
            std::lock_guard<std::mutex> l(marcasMu);
            vistas = marcas;
        }
        for (const Marca& m : vistas) {
            if (m.qpc < antes) {
                v1 = m.v1;
                v2 = m.v2;
                a2 = m.a2;
                continue;
            }
            if (tV1 == 0 && m.v1 > v1) tV1 = m.qpc;
            if (tV2 == 0 && m.v2 > v2) tV2 = m.qpc;
            if (pulso != 0 && tA == 0 && m.qpc >= pulso && m.a2 > a2) tA = m.qpc;
            if (m.qpc < pulso) a2 = m.a2;
        }
        if (tV1 == 0 || tV2 == 0 || tA == 0) {
            std::printf("  vez %d: no se vio el destello o el pulso\n", vez);
            continue;
        }
        const double mira = (static_cast<double>(tV2) - static_cast<double>(tA)) / 1e6;
        const double conversa = (static_cast<double>(tV1) - static_cast<double>(tA)) / 1e6;
        desfaseMira.push_back(mira);
        desfaseConversa.push_back(conversa);
        std::printf("  vez %d: video − audio: pantalla %+6.1f ms, invitado que conversa %+6.1f ms\n", vez, mira, conversa);
    }
    fin.store(true);
    escritorAudio.join();
    escritorVideo.join();
    mirar.join();
    pump.stop();
    CHECK(desfaseMira.size() >= 3, "se midió el destello y el pulso");
    double peorMira = 0, mediaConversa = 0;
    for (double d : desfaseMira) peorMira = std::max(peorMira, std::fabs(d));
    for (double d : desfaseConversa) mediaConversa += d / static_cast<double>(std::max<std::size_t>(1, desfaseConversa.size()));
    std::printf("  antes (sin alinear, como le llega al que conversa): %+.1f ms; ahora la pantalla: peor %.1f ms\n",
                mediaConversa, peorMira);
    CHECK(peorMira < 20.0, "a la pantalla, el destello sale con el pulso (< 20 ms)");
    CHECK(mediaConversa < -100.0, "al que conversa no se le demora (T4): sale antes que el audio, como siempre");
    const nlohmann::json c = pump.copiasVideo();
    CHECK(c.value("demorados", 0ULL) > 0, "los cuadros de la pantalla esperaron a su audio");
}

// --- La cuenta de los labios, suelta -----------------------------------------------------

void testCuentaLabios() {
    const std::uint64_t ahora = 10'000 * kMs;
    CHECK(labios::salida(ahora - 20 * kMs, ahora, 150 * kMs) == ahora + 130 * kMs, "sale a la hora del audio");
    CHECK(labios::salida(0, ahora, 150 * kMs) == ahora, "sin hora del contenido: ya");
    CHECK(labios::salida(ahora - 20 * kMs, ahora, -1) == ahora, "sin saber lo del audio: ya");
    CHECK(labios::salida(ahora - 200 * kMs, ahora, 150 * kMs) == ahora, "si el video ya va atrás: ya (no se adelanta)");
    CHECK(labios::salida(ahora - 1 * kMs, ahora, 900 * kMs) == ahora + labios::kTopeNs, "nunca más que el tope");
    labios::Espera<int> e;
    std::vector<int> fuera;
    e.guardar(ahora + 10, 1, [&](int x) { fuera.push_back(x); });
    e.guardar(ahora + 5, 2, [&](int x) { fuera.push_back(x); }); // no se adelanta al anterior
    std::vector<int> listos;
    e.sacarListos(ahora + 9, [&](int x) { listos.push_back(x); });
    CHECK(listos.empty() && e.proximo() == ahora + 10, "en orden y a su hora");
    e.sacarListos(ahora + 10, [&](int x) { listos.push_back(x); });
    CHECK(listos.size() == 2 && listos[0] == 1 && listos[1] == 2, "salen los dos, en orden");
    for (int i = 0; i < static_cast<int>(labios::kTopeCuadros) + 3; ++i) e.guardar(ahora + 1000, i, [&](int x) { fuera.push_back(x); });
    CHECK(fuera.size() == 3 && e.size() == labios::kTopeCuadros, "lleno: el más viejo sale ya (no se tira)");
    labios::EdadAudio edad;
    edad.medir(150 * kMs);
    for (int i = 0; i < 4; ++i) edad.medir(400 * kMs); // un salto raro cuenta poco
    CHECK(edad.ns() < static_cast<std::int64_t>(180 * kMs), "un salto de la medida cuenta poco hasta que se repite");
}

} // namespace

int main() {
    ba::platform::dormirHasta(Reloj::now()); // (y el temporizador fino en Windows)
    testCuentaLabios();
    testEntrantes();
    testCopias();
    testLabios();
    std::printf(g_fallas == 0 ? "== test_retorno_video: OK ==\n" : "== test_retorno_video: %d fallas ==\n", g_fallas);
    return g_fallas == 0 ? 0 : 1;
}
