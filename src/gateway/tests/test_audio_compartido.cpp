// ============================================================================
// src/gateway/tests/test_audio_compartido.cpp — el audio de retorno una vez por lo que se oye
// (AudioCompartido.hpp): la parte que no necesita libwebrtc.
//
//   1. parecidos: hasta el tope son lo mismo; un pelo más, no.
//   2. La marca: va y vuelve (estéreo, y después de la mezcla a mono de libwebrtc); un audio
//      cualquiera no la imita; un bloque corto no la lleva; el número entero del bloque se
//      recupera de sus 24 bits aunque haya dado la vuelta.
//   3. AgrupadorRetorno: los que oyen lo mismo entran juntos después de kUnirseBloques; el
//      que empieza a hablar sale en el acto; el grupo conserva su etiqueta aunque se vaya el
//      que lo representaba; dos audios distintos con varios cada uno son dos grupos; el que
//      oye algo propio (IFB) va solo; uno solo no es grupo.
//
// El codificador de paso y el grupo Opus (libwebrtc) los prueba test_webrtc_loopback.
// ============================================================================

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <random>
#include <vector>

#include "AudioCompartido.hpp"

using namespace ba::gateway;

namespace {

int g_fallos = 0;
#define CHECK(cond, msg)                                                                                     \
    do {                                                                                                     \
        if (!(cond)) {                                                                                       \
            std::printf("FALLO: %s (%s, linea %d)\n", msg, #cond, __LINE__);                                 \
            ++g_fallos;                                                                                      \
        }                                                                                                    \
    } while (0)

constexpr std::size_t kN = 960; // 10 ms estéreo a 48 kHz

std::vector<float> tono(double hz, double fase, float amp = 0.3f) {
    std::vector<float> v(kN);
    for (std::size_t i = 0; i < kN / 2; ++i) {
        const float x = amp * static_cast<float>(std::sin(fase + 2 * 3.14159265358979 * hz * static_cast<double>(i) / 48000));
        v[2 * i] = v[2 * i + 1] = x;
    }
    return v;
}

void parecidosTope() {
    std::vector<float> a = tono(440, 0), b = a;
    CHECK(parecidos(a.data(), b.data(), kN, AgrupadorRetorno::kTope), "iguales");
    b[100] += 0.0009f;
    CHECK(parecidos(a.data(), b.data(), kN, AgrupadorRetorno::kTope), "dentro del tope");
    b[100] += 0.0002f;
    CHECK(!parecidos(a.data(), b.data(), kN, AgrupadorRetorno::kTope), "pasado el tope");
    CHECK(!parecidos(a.data(), nullptr, kN, AgrupadorRetorno::kTope), "contra nada");
}

void marca() {
    std::mt19937 rng(7);
    std::uniform_int_distribution<int> d(-20000, 20000);
    std::vector<std::int16_t> pcm(kN);
    for (auto& x : pcm) x = static_cast<std::int16_t>(d(rng));
    marcarBloque(pcm.data(), kN / 2, 2, 513, 0x1234567ull);
    std::uint16_t g = 0;
    std::uint32_t k = 0;
    CHECK(leerMarca(pcm.data(), kN / 2, 2, g, k), "la marca se lee en estéreo");
    CHECK(g == 513 && k == 0x234567u, "grupo y 24 bits del bloque");
    // La mezcla a mono de libwebrtc: (L+R)/2 en enteros.
    std::vector<std::int16_t> mono(kN / 2);
    for (std::size_t i = 0; i < kN / 2; ++i) mono[i] = static_cast<std::int16_t>((pcm[2 * i] + pcm[2 * i + 1]) / 2);
    g = 0;
    k = 0;
    CHECK(leerMarca(mono.data(), kN / 2, 1, g, k) && g == 513 && k == 0x234567u, "sobrevive a la mezcla a mono");
    // Lo que no es marca: solo los primeros kCuadrosMarca cuadros cambian, y poco.
    std::vector<std::int16_t> orig(kN);
    for (auto& x : orig) x = static_cast<std::int16_t>(d(rng));
    std::vector<std::int16_t> m = orig;
    marcarBloque(m.data(), kN / 2, 2, 1, 1);
    int distintos = 0;
    for (std::size_t i = 0; i < kN; ++i) {
        if (m[i] != orig[i]) ++distintos;
    }
    for (std::size_t i = kCuadrosMarca * 2; i < kN; ++i) CHECK(m[i] == orig[i], "después de la marca no se toca");
    CHECK(distintos <= static_cast<int>(kCuadrosMarca * 2), "solo los cuadros de la marca");
    // Un audio cualquiera no la imita.
    int falsos = 0;
    for (int t = 0; t < 20000; ++t) {
        for (auto& x : orig) x = static_cast<std::int16_t>(d(rng));
        if (leerMarca(orig.data(), kN / 2, 2, g, k)) ++falsos;
    }
    CHECK(falsos == 0, "ruido sin marca");
    std::vector<std::int16_t> silencio(kN, 0);
    CHECK(!leerMarca(silencio.data(), kN / 2, 2, g, k), "silencio sin marca");
    CHECK(!leerMarca(pcm.data(), kCuadrosMarca - 1, 2, g, k), "bloque corto");
    // El número entero del bloque.
    CHECK(bloqueCompleto(0x000005u, 0x1000003ull) == 0x1000005ull, "cerca, misma vuelta");
    CHECK(bloqueCompleto(0xFFFFFEu, 0x1000001ull) == 0x0FFFFFEull, "la vuelta anterior");
    CHECK(bloqueCompleto(0x000001u, 0x1FFFFFFull) == 0x2000001ull, "la vuelta siguiente");
    CHECK(bloqueCompleto(42u, 40ull) == 42ull, "al principio");
}

std::vector<AgrupadorRetorno::Entrada> entradas(const std::vector<int>& sesiones, const std::vector<const float*>& pcm,
                                                const std::vector<bool>& libre = {}) {
    std::vector<AgrupadorRetorno::Entrada> e;
    for (std::size_t i = 0; i < sesiones.size(); ++i)
        e.push_back({sesiones[i], pcm[i], libre.empty() ? true : static_cast<bool>(libre[i])});
    return e;
}

void agrupador() {
    AgrupadorRetorno a;
    const std::vector<float> todos = tono(300, 0);
    std::vector<float> casi = todos;
    for (auto& x : casi) x += 0.00002f; // otro orden de suma: lo mismo
    const std::vector<float> sinVoz5 = tono(500, 1); // el mix-minus del que habla (no se oye)
    const std::vector<float> programa = tono(800, 2);
    const std::vector<int> s{1, 2, 3, 4, 5, 6, 7};
    const std::vector<const float*> oyen{todos.data(), casi.data(), todos.data(), casi.data(), sinVoz5.data(),
                                         programa.data(), programa.data()};
    for (int b = 1; b < AgrupadorRetorno::kUnirseBloques; ++b) {
        a.agrupar(entradas(s, oyen), kN);
        for (std::size_t i = 0; i < s.size(); ++i) CHECK(a.grupo(i) == 0, "antes de kUnirseBloques nadie entra");
    }
    a.agrupar(entradas(s, oyen), kN);
    const int g1 = a.grupo(0);
    CHECK(g1 != 0 && a.grupo(1) == g1 && a.grupo(2) == g1 && a.grupo(3) == g1, "los cuatro que oyen lo mismo, juntos");
    CHECK(a.grupo(4) == 0, "el que habla, solo");
    const int g2 = a.grupo(5);
    CHECK(g2 != 0 && g2 != g1 && a.grupo(6) == g2, "los dos del programa: otro grupo");
    CHECK(parecidos(a.pcmDe(g1), todos.data(), kN, AgrupadorRetorno::kTope), "el audio del grupo es el de ellos");
    CHECK(a.pcmDe(g2) == programa.data(), "el del otro grupo");

    // La sesión 1 (la que representaba al grupo) empieza a hablar: sale en el acto, y los
    // otros tres siguen en el MISMO grupo (sin volver a esperar).
    const std::vector<float> sinVoz1 = tono(650, 3);
    std::vector<const float*> oyen2 = oyen;
    oyen2[0] = sinVoz1.data();
    a.agrupar(entradas(s, oyen2), kN);
    CHECK(a.grupo(0) == 0, "el que empieza a hablar sale ya");
    CHECK(a.grupo(1) == g1 && a.grupo(2) == g1 && a.grupo(3) == g1, "los demás siguen en el mismo grupo");
    // Se calla: vuelve recién después de kUnirseBloques.
    for (int b = 1; b < AgrupadorRetorno::kUnirseBloques; ++b) {
        a.agrupar(entradas(s, oyen), kN);
        CHECK(a.grupo(0) == 0, "volver espera");
    }
    a.agrupar(entradas(s, oyen), kN);
    CHECK(a.grupo(0) == g1, "y vuelve al mismo grupo");

    // El que oye algo propio (el operador le habla) va solo aunque suene parecido.
    std::vector<bool> libre(s.size(), true);
    libre[2] = false;
    a.agrupar(entradas(s, oyen, libre), kN);
    CHECK(a.grupo(2) == 0, "con IFB, solo");
    CHECK(a.grupo(1) == g1, "los otros siguen");

    // Uno solo no es grupo.
    AgrupadorRetorno b;
    for (int i = 0; i < AgrupadorRetorno::kUnirseBloques + 5; ++i) b.agrupar(entradas({9}, {todos.data()}), kN);
    CHECK(b.grupo(0) == 0 && b.pcmDe(1) == nullptr && b.pcmDe(2) == nullptr, "uno solo codifica solo");

    // Una sesión que se va se olvida: si vuelve, espera de nuevo.
    AgrupadorRetorno c;
    for (int i = 0; i < AgrupadorRetorno::kUnirseBloques; ++i)
        c.agrupar(entradas({1, 2}, {todos.data(), todos.data()}), kN);
    CHECK(c.grupo(0) != 0 && c.grupo(1) != 0, "dos que oyen lo mismo");
    c.agrupar(entradas({1}, {todos.data()}), kN);
    c.agrupar(entradas({1, 2}, {todos.data(), todos.data()}), kN);
    CHECK(c.grupo(1) == 0, "el que volvió espera");
}

} // namespace

int main() {
    parecidosTope();
    marca();
    agrupador();
    if (g_fallos == 0) std::printf("test_audio_compartido: OK\n");
    return g_fallos == 0 ? 0 : 1;
}
