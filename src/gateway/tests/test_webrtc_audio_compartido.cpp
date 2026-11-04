// ============================================================================
// src/gateway/tests/test_webrtc_audio_compartido.cpp — el Opus del retorno compartido, con el
// Opus de libwebrtc de verdad (AudioCompartidoRtc.hpp), sin PeerConnection.
//
// Se hace lo que hacen el hilo del retorno y libwebrtc por cada conexión: el grupo codifica
// el bloque, la fuente de la sesión lo convierte a int16 y lo marca, libwebrtc lo mezcla a
// mono ((L+R)/2) y se lo da al codificador de la conexión (el de paso) con su sello RTP.
//
//   1. Dos conexiones del grupo mandan los MISMOS bytes (los del grupo), con su sello cada
//      una, y el grupo cuenta los paquetes repartidos.
//   2. Entrar: a lo sumo un hueco de 10 ms (alinearse con el paquete del grupo); nunca un
//      solapamiento. Salir en medio de un paquete: el propio rearma el paquete desde donde
//      empezó, sin hueco.
//   3. Una conexión sin marca no usa el grupo nunca.
//   4. Lo que sale se decodifica entero (un Opus válido de punta a punta) y suena.
// ============================================================================

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <vector>

#include "api/audio_codecs/builtin_audio_decoder_factory.h"
#include "api/audio_codecs/builtin_audio_encoder_factory.h"

#include "AudioCompartido.hpp"
#include "webrtc/AudioCompartidoRtc.hpp"

using namespace ba::gateway;

namespace {

int g_fallos = 0;
#define CHECK(cond, msg)                                                                                     \
    do {                                                                                                     \
        if (!(cond)) {                                                                                       \
            std::printf("FALLO: %s (%s, linea %d)\n", msg, #cond, __LINE__);                                 \
            ++g_fallos;                                                                                      \
        } else {                                                                                             \
            std::printf("ok: %s\n", msg);                                                                    \
        }                                                                                                    \
    } while (0)

/// Una conexión: su codificador (de la fábrica de paso), su sello y lo que mandó.
struct Conexion {
    std::unique_ptr<webrtc::AudioEncoder> enc;
    uint32_t ts = 1000; // cada conexión tiene su propio origen de sellos
    struct Paquete {
        uint32_t ts;
        std::vector<uint8_t> bytes;
    };
    std::vector<Paquete> paquetes;

    /// Un bloque (float estéreo), marcado como `bloque` del grupo `id` si id != 0.
    /// Devuelve los bytes que salieron (vacío si ninguno).
    std::vector<uint8_t> bloque(const std::vector<float>& pcm, uint16_t id, uint64_t k) {
        std::vector<int16_t> st(pcm.size());
        for (size_t i = 0; i < pcm.size(); ++i) st[i] = static_cast<int16_t>(pcm[i] * 32767.0f);
        if (id != 0) marcarBloque(st.data(), st.size() / 2, 2, id, k);
        std::vector<int16_t> mono(st.size() / 2); // la mezcla a mono de libwebrtc
        for (size_t i = 0; i < mono.size(); ++i) mono[i] = static_cast<int16_t>((st[2 * i] + st[2 * i + 1]) / 2);
        rtc::Buffer out;
        const auto info = enc->Encode(ts, mono, &out);
        ts += 480;
        std::vector<uint8_t> b(out.data(), out.data() + out.size());
        if (info.encoded_bytes > 0) paquetes.push_back({info.encoded_timestamp, b});
        return b;
    }
};

std::vector<float> tono(int i) {
    std::vector<float> v(960);
    for (int s = 0; s < 480; ++s) {
        const float x = 0.3f * static_cast<float>(std::sin(2 * 3.14159265358979 * 440.0 * (i * 480 + s) / 48000.0));
        v[2 * s] = v[2 * s + 1] = x;
    }
    return v;
}

/// Revisa los sellos de una conexión: cada paquete empieza donde terminó el anterior (las
/// duraciones salen del decodificador). Cuenta huecos y solapamientos, y si suena.
void revisar(const char* quien, const Conexion& c, int huecosMax, bool& suena) {
    auto dec = webrtc::CreateBuiltinAudioDecoderFactory()->MakeAudioDecoder(
        webrtc::SdpAudioFormat("opus", 48000, 2, {{"minptime", "10"}, {"useinbandfec", "1"}}), absl::nullopt);
    std::vector<int16_t> out(48000);
    int huecos = 0, solapados = 0, malos = 0;
    double energia = 0;
    size_t muestras = 0;
    uint32_t esperado = c.paquetes.empty() ? 0 : c.paquetes.front().ts;
    for (const auto& p : c.paquetes) {
        if (p.ts > esperado) ++huecos;
        if (p.ts < esperado) ++solapados;
        webrtc::AudioDecoder::SpeechType tipo;
        const int n = dec->Decode(p.bytes.data(), p.bytes.size(), 48000, out.size() * sizeof(int16_t), out.data(), &tipo);
        if (n <= 0) {
            ++malos;
            continue;
        }
        const size_t canales = dec->Channels();
        for (int i = 0; i < n; ++i) energia += static_cast<double>(out[static_cast<size_t>(i)]) * out[static_cast<size_t>(i)];
        muestras += static_cast<size_t>(n);
        esperado = p.ts + static_cast<uint32_t>(static_cast<size_t>(n) / canales);
    }
    std::printf("%s: %zu paquetes, %d huecos, %d solapados, %d sin decodificar\n", quien, c.paquetes.size(), huecos,
                solapados, malos);
    CHECK(huecos <= huecosMax, "sin huecos de más");
    CHECK(solapados == 0, "sin solapamientos");
    CHECK(malos == 0, "todo se decodifica");
    suena = muestras > 0 && std::sqrt(energia / static_cast<double>(muestras)) > 1000;
}

} // namespace

int main() {
    std::printf("== test_webrtc_audio_compartido (Opus de libwebrtc) ==\n");
    const auto base = webrtc::CreateBuiltinAudioEncoderFactory();
    const auto registro = std::make_shared<audioret::Registro>(base);
    const auto fabrica = audioret::fabricaDePaso(base, registro);
    const webrtc::SdpAudioFormat formato("opus", 48000, 2, {{"minptime", "10"}, {"useinbandfec", "1"}});
    const auto grupo = registro->grupo("prueba");
    CHECK(registro->grupo("prueba") == grupo && registro->buscar(grupo->id()) == grupo, "el registro da el mismo grupo");

    Conexion a, b, c, e;
    a.enc = fabrica->MakeAudioEncoder(111, formato, absl::nullopt);
    e.enc = fabrica->MakeAudioEncoder(111, formato, absl::nullopt); // como A, sale un bloque después
    b.enc = fabrica->MakeAudioEncoder(111, formato, absl::nullopt);
    c.enc = fabrica->MakeAudioEncoder(111, formato, absl::nullopt);
    CHECK(a.enc && b.enc && c.enc, "la fábrica de paso da codificadores");
    b.ts = 777777; // otro origen: el sello es de cada conexión
    b.bloque(tono(-1), 0, 0); // B queda desfasado medio paquete respecto de A (tiene que alinearse)

    int iguales = 0;
    for (int i = 0; i < 300; ++i) {
        const auto pcm = tono(i);
        const uint64_t k = grupo->codificar(pcm.data(), pcm.size(), 48000);
        const bool aEnGrupo = i < 200 || i >= 260; // A sale del grupo en el bloque 200 (a mitad de paquete o no)
        const auto pa = a.bloque(pcm, aEnGrupo ? grupo->id() : 0, k);
        const auto pb = b.bloque(pcm, grupo->id(), k);
        c.bloque(pcm, 0, 0);
        e.bloque(pcm, i < 201 ? grupo->id() : 0, k); // la otra paridad: sale en medio de un paquete
        if (i > 20 && i < 200 && !pa.empty() && pa == pb) ++iguales;
    }
    std::printf("paquetes iguales A/B en el grupo: %d; repartidos %llu\n", iguales,
                static_cast<unsigned long long>(grupo->repartidos.load()));
    CHECK(iguales >= 80, "A y B mandan los mismos bytes (los del grupo)");
    CHECK(grupo->repartidos.load() >= 2 * 80, "el grupo cuenta lo repartido");
    bool suenaA = false, suenaB = false, suenaC = false;
    revisar("A (entra, sale y vuelve)", a, 2, suenaA);
    revisar("B (entra desfasada)", b, 1, suenaB);
    revisar("C (sin marca)", c, 0, suenaC);
    bool suenaE = false;
    revisar("E (sale en la otra paridad)", e, 1, suenaE);
    CHECK(suenaA && suenaB && suenaC && suenaE, "lo decodificado suena");

    // C nunca usó el grupo: sus bytes son los de su propio codificador. Uno nuevo sin marca
    // con la misma entrada da lo mismo que C.
    Conexion d;
    d.enc = base->MakeAudioEncoder(111, formato, absl::nullopt);
    for (int i = 0; i < 300; ++i) d.bloque(tono(i), 0, 0);
    bool igualC = d.paquetes.size() == c.paquetes.size();
    for (size_t i = 0; igualC && i < d.paquetes.size(); ++i) igualC = d.paquetes[i].bytes == c.paquetes[i].bytes;
    CHECK(igualC, "sin marca, el de paso es el Opus de siempre");

    if (g_fallos == 0) std::printf("test_webrtc_audio_compartido: OK\n");
    return g_fallos == 0 ? 0 : 1;
}
