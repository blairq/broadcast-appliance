// test_capacidad_reglas.cpp — ¿cuántos invitados aguanta la máquina? Lo puro (sala/CapacidadReglas)
//
//   los escalones y sus tiempos (nunca más de 2 minutos), el juicio de un escalón con
//   datos sintéticos (CPU sin el generador, memoria, grilla, audio, entrada, interfaz, la
//   máquina que genera), la guardia de cada segundo, el resultado (máximo, con margen, «al
//   menos», la tendencia de la CPU), la frase, la conexión «ida y vuelta» y el nombre del
//   archivo del historial.

#include <algorithm>
#include <cstdio>
#include <string>

#include "sala/CapacidadReglas.hpp"

namespace cap = ba::app::sala::capacidad;
using json = nlohmann::json;

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

/// Un régimen de `s` segundos con todo sano salvo lo que cambie `f`.
template <class F>
std::vector<cap::Muestra> regimen(int n, int s, F f) {
    std::vector<cap::Muestra> v;
    for (int i = 0; i < s; ++i) {
        cap::Muestra m;
        m.t = 1000LL * i;
        m.cpuMaquina = 40;
        m.cpuGenerador = 10;
        m.cpuEquipo = 40;
        m.memLibreMb = 9000;
        m.memTotalMb = 16000;
        m.cps = 30;
        m.audioMs = 14;
        m.uiMs = 5;
        m.entraron = n;
        m.conVideo = n;
        f(m, i);
        v.push_back(m);
    }
    return v;
}
std::vector<cap::Muestra> sano(int n, int s = 30) {
    return regimen(n, s, [](cap::Muestra&, int) {});
}
bool tiene(const std::vector<std::string>& v, const char* k) { return std::find(v.begin(), v.end(), k) != v.end(); }
} // namespace

int main() {
    const cap::Criterios c;
    // --- Escalones -----------------------------------------------------------------------------
    {
        const auto e40 = cap::escalones(40);
        CHECK((e40 == std::vector<int>{10, 20, 40}), "hasta 40: 10, 20, 40");
        const auto e50 = cap::escalones(50);
        CHECK((e50 == std::vector<int>{10, 20, 40, 50}), "hasta 50: el último es el tope");
        CHECK((cap::escalones(5) == std::vector<int>{5}), "con un tope chico, un solo escalón");
        CHECK(cap::escalones(0).empty(), "tope 0: nada");
        CHECK(cap::escalones(10000).back() == ba::ipc::kMaxInvitados, "nunca más que el cupo más grande de la sala");
        const auto e100 = cap::escalones(100);
        CHECK((e100 == std::vector<int>{10, 20, 40, 60, 80, 100}), "hasta 100: 10, 20, 40, 60, 80, 100");
    }
    {
        const cap::Tiempos t;
        CHECK(cap::entradaCadaMs(10, t) == 300, "pocos: de a 300 ms");
        // Los 75 s de entrada (120 − 15 − 30) repartidos entre los nuevos.
        CHECK(cap::entradaCadaMs(1000, t) == 75, "1000 nuevos: de a 75 ms (entran en los 75 s)");
        CHECK(cap::entradaCadaMs(5000, t) == 50, "muchísimos: nunca menos de 50 ms");
        for (int n : {1, 10, 20, 40, 100, 200, 512})
            CHECK(cap::duracionEscalonS(n, t) <= 120, "ningún escalón pasa de 2 minutos");
        CHECK(cap::duracionEscalonS(10, t) == 3 + 15 + 30, "10 nuevos: entrar 3 s + asentarse + régimen");
    }
    // --- El juicio ----------------------------------------------------------------------------
    {
        const cap::Paso p = cap::juzgar(20, sano(20), true, c);
        CHECK(!p.degradado, "todo sano: no se degrada");
        CHECK(p.cpu == 30, "la CPU del juicio es sin el generador (40 - 10)");
        CHECK(p.cpuTotal == 40, "la de la máquina entera queda aparte");
        CHECK(p.cpuGenerador == 10, "la parte del generador, informada");
    }
    {
        // 90 % de máquina con el generador al 10: sin él son 80 → sano. Sin restar (remoto) → degradado.
        auto r = regimen(40, 30, [](cap::Muestra& m, int) { m.cpuMaquina = 90; });
        CHECK(!cap::juzgar(40, r, true, c).degradado, "local: descontado el generador, la CPU entra (80 %)");
        const cap::Paso p = cap::juzgar(40, r, false, c);
        CHECK(p.degradado && tiene(p.limites, "cpu"), "remoto (sin generador acá): 90 % es demasiado");
    }
    {
        // Un pico de CPU de pocos segundos no cuenta: es la mediana.
        auto r = regimen(20, 30, [](cap::Muestra& m, int i) {
            if (i < 5) m.cpuMaquina = 100;
        });
        CHECK(!cap::juzgar(20, r, true, c).degradado, "un pico corto de CPU no degrada (mediana)");
    }
    {
        auto r = regimen(20, 30, [](cap::Muestra& m, int i) {
            if (i == 20) m.memLibreMb = 4000;
        });
        const cap::Paso p = cap::juzgar(20, r, true, c);
        CHECK(p.degradado && tiene(p.limites, "memoria"), "la memoria disponible baja (mínimo del régimen) degrada");
        CHECK(p.memMinMb == 4000, "el mínimo de memoria");
    }
    {
        auto r = regimen(40, 30, [](cap::Muestra& m, int i) {
            if (i > 25) m.cps = 22;
        });
        CHECK(tiene(cap::juzgar(40, r, true, c).limites, "grilla"), "la grilla por debajo de 25 cps degrada");
    }
    {
        auto r = regimen(40, 30, [](cap::Muestra& m, int) { m.audioMs = 80; });
        CHECK(tiene(cap::juzgar(40, r, true, c).limites, "audio"), "el audio con más de 50 ms degrada");
    }
    {
        auto r = regimen(40, 30, [](cap::Muestra& m, int) { m.conVideo = 30; });
        CHECK(tiene(cap::juzgar(40, r, true, c).limites, "entrada"), "si no entran con video (30 de 40), degrada");
        auto r2 = regimen(40, 30, [](cap::Muestra& m, int) { m.conVideo = 37; });
        CHECK(!cap::juzgar(40, r2, true, c).degradado, "37 de 40 (más del 90 %) está bien");
    }
    {
        auto r = regimen(20, 30, [](cap::Muestra& m, int i) {
            if (i == 10) m.uiMs = 2500;
        });
        CHECK(tiene(cap::juzgar(20, r, true, c).limites, "interfaz"), "una traba de 2,5 s de la interfaz degrada");
    }
    {
        auto r = regimen(40, 30, [](cap::Muestra& m, int) { m.cpuEquipo = 99; });
        CHECK(tiene(cap::juzgar(40, r, false, c).limites, "generador"), "la máquina que genera al tope: el resultado es un mínimo");
    }
    {
        // Lo que no se sabe no juzga.
        auto r = regimen(20, 30, [](cap::Muestra& m, int) {
            m.cps = -1;
            m.audioMs = -1;
            m.conVideo = -1;
            m.memLibreMb = -1;
        });
        CHECK(!cap::juzgar(20, r, true, c).degradado, "sin grilla, audio, foto ni memoria: no se degrada por eso");
    }
    {
        const cap::Paso p = cap::juzgar(60, {}, true, c, "memoria", "se acabó la memoria");
        CHECK(p.degradado && p.guardia && p.limites.front() == "memoria", "lo que cortó la guardia cuenta como degradado");
    }
    // --- La guardia ---------------------------------------------------------------------------
    {
        CHECK(cap::guardia(sano(20, 5), c).empty(), "sano: la guardia no corta");
        auto r = sano(20, 5);
        r.back().memLibreMb = 2500;
        CHECK(cap::claveGuardia(r, c) == "memoria" && !cap::guardia(r, c).empty(), "memoria debajo de 3 GB: corta ya");
        auto u = sano(20, 5);
        u.back().uiMs = 6000;
        CHECK(cap::claveGuardia(u, c) == "interfaz", "la interfaz parada 6 s: corta");
        auto cpu = regimen(20, 12, [](cap::Muestra& m, int i) {
            if (i >= 2) m.cpuMaquina = 99;
        });
        CHECK(cap::claveGuardia(cpu, c) == "cpu", "CPU al tope 10 s seguidos: corta");
        auto cpu2 = regimen(20, 12, [](cap::Muestra& m, int i) {
            if (i >= 2 && i != 7) m.cpuMaquina = 99;
        });
        CHECK(cap::claveGuardia(cpu2, c).empty(), "con un respiro en el medio, no");
        auto gen = regimen(20, 12, [](cap::Muestra& m, int) { m.cpuEquipo = 99; });
        CHECK(cap::claveGuardia(gen, c) == "generador", "la máquina que genera al tope sostenido: corta");
    }
    // --- El resultado ------------------------------------------------------------------------
    {
        // 10, 20, 40 sanos (CPU 20, 35, 60): llegó al tope sin degradarse.
        std::vector<cap::Paso> pasos;
        int i = 0;
        for (int n : {10, 20, 40}) {
            const double cpu = std::vector<double>{30, 45, 70}[i++];
            pasos.push_back(cap::juzgar(n, regimen(n, 30, [cpu](cap::Muestra& m, int) { m.cpuMaquina = cpu; }), true, c));
        }
        const cap::Resultado r = cap::resumir(pasos, 40, false, c);
        CHECK(r.maximo == 40 && r.alMenos && r.degradoEn == 0, "llegó a 40 sin degradarse: «al menos 40»");
        CHECK(r.conMargen == 40, "con la CPU en 60 % (sin el generador), 40 ya es con margen");
        CHECK(r.estimadoCpu > 40, "la tendencia de la CPU dice que llegaría más lejos");
        CHECK(r.estimadoCpu >= 55 && r.estimadoCpu <= 65, "…unos 60 (recta 20→20 %, 40→60 %: 85 % en ~58)");
        CHECK(r.cpuGenerador == 10, "la parte del generador, para decirla");
        const std::string f = cap::frase(r);
        CHECK(f.find("40") != std::string::npos, "la frase dice el número");
    }
    {
        // 10 y 20 sanos, 40 degradado por CPU.
        std::vector<cap::Paso> pasos{cap::juzgar(10, sano(10), true, c), cap::juzgar(20, sano(20), true, c),
                                     cap::juzgar(40, regimen(40, 30, [](cap::Muestra& m, int) { m.cpuMaquina = 99; }), true, c)};
        const cap::Resultado r = cap::resumir(pasos, 80, false, c);
        CHECK(r.maximo == 20 && r.degradoEn == 40 && !r.alMenos, "se degradó en 40: el máximo es 20");
        CHECK(r.limita == "cpu", "lo que limita es la CPU");
        CHECK(r.conMargen == 15, "con margen: el 80 % de 20, redondeado a 5");
        CHECK(r.estimadoCpu == -1, "degradado: no se estima por la tendencia");
        const json j = cap::aJson(r);
        CHECK(j.value("limita", std::string()) == "cpu" && !j.value("frase", std::string()).empty(), "el JSON trae la frase");
    }
    {
        const std::vector<cap::Paso> pasos{cap::juzgar(10, regimen(10, 30, [](cap::Muestra& m, int) { m.audioMs = 90; }), true, c)};
        const cap::Resultado r = cap::resumir(pasos, 40, false, c);
        CHECK(r.maximo == 0 && r.conMargen == 0 && r.degradoEn == 10 && r.limita == "audio", "ya con 10 se degrada (el audio)");
    }
    {
        const std::vector<cap::Paso> pasos{cap::juzgar(10, sano(10), true, c)};
        const cap::Resultado r = cap::resumir(pasos, 40, true, c);
        CHECK(r.cortada && !r.alMenos && r.maximo == 10, "cancelada después del primero: 10 probados, no «al menos el tope»");
    }
    // --- La muestra va y vuelve por JSON (el probador remoto la lee así) -----------------------
    {
        cap::Muestra m = sano(20, 1).front();
        m.t = 1234;
        const cap::Muestra b = cap::muestraDe(cap::aJson(m));
        CHECK(b.t == 1234 && b.cpuMaquina == 40 && b.conVideo == 20 && b.cps == 30, "la muestra por JSON");
        const cap::Muestra vacia = cap::muestraDe(json::object());
        CHECK(vacia.cpuMaquina < 0 && vacia.entraron < 0, "sin datos: todo «no se sabe»");
    }
    // --- Internet ------------------------------------------------------------------------------
    {
        const auto m = cap::idaYVuelta(20000, 40, 120, 0);
        CHECK(m.subidaKbps == 20000 && m.bajadaKbps == 20000 && m.porTunel, "ida y vuelta: el mínimo como subida y bajada");
        namespace en = ba::app::sala::enlace;
        en::Perfil pf;
        pf.invitados = 10;
        const en::Juicio j = en::juzgar(pf, m);
        CHECK(j.entranConMargen > 0, "con 20 Mbps entran algunos invitados");
    }
    // --- Lo que se lee de la app ---------------------------------------------------------------
    {
        const json lat = json::parse(R"({"entradas": [
            {"entrada": "Invitado 1", "audio": true, "video": false, "edad_p50_ms": 12},
            {"entrada": "Invitado 2", "audio": true, "video": false, "edad_p50_ms": 30},
            {"entrada": "Invitado 3", "audio": true, "video": false, "edad_p50_ms": 14},
            {"entrada": "Invitado 1", "audio": false, "video": true, "edad_p50_ms": 900},
            {"entrada": "Cámara 1", "audio": true, "video": false, "edad_p50_ms": 500}]})");
        CHECK(cap::edadAudioInvitados(lat) == 14, "la edad del audio: la mediana de los invitados (sin video ni cámaras)");
        CHECK(cap::edadAudioInvitados(json::object()) == -1, "sin invitados: no se sabe");
        const json med = json::parse(R"({"grillas": [
            {"entrada": {"cuadros": 300, "cps": 29.8}, "programa": {"cuadros": 280, "cps": 27.5}},
            {"entrada": {"cuadros": 0, "cps": 0}}]})");
        CHECK(cap::cpsDeMedicion(med) == 27.5, "la grilla: el peor mosaico medido (sin los que no midieron)");
        CHECK(cap::cpsDeMedicion(json::object()) == -1, "sin grilla: no se sabe");
        const json ps = json::parse(R"([{"prueba": true, "connected": true, "mediaUp": true},
            {"prueba": true, "connected": true, "mediaUp": false}, {"connected": true}, {"connected": false}])");
        const cap::GenteSala g = cap::genteDe(ps);
        CHECK(g.prueba == 2 && g.pruebaConVideo == 1 && g.reales == 1, "de prueba, con video y reales conectados");
    }
    // --- El código del que recibe ----------------------------------------------------------------
    {
        cap::Codigo a;
        a.url = "https://192.168.1.20:8443";
        a.credencial = "0123456789abcdef0123456789abcdef";
        a.huella = "AB:CD:EF";
        a.maquina = "PC de la sala";
        const std::string s = cap::armarCodigo(a);
        CHECK(s.rfind("BACAP1.", 0) == 0, "el código empieza con BACAP1.");
        CHECK(s.find(a.credencial) == std::string::npos, "la credencial no se lee a simple vista");
        cap::Codigo b;
        CHECK(cap::leerCodigo("  " + s.substr(0, 20) + "\n" + s.substr(20) + " ", b), "se lee aunque venga cortado o con espacios");
        CHECK(b.url == a.url && b.credencial == a.credencial && b.huella == a.huella && b.maquina == a.maquina, "ida y vuelta");
        for (std::size_t largo = 0; largo < 5; ++largo) {
            a.maquina = std::string(largo, 'x'); // todos los restos de base64
            CHECK(cap::leerCodigo(cap::armarCodigo(a), b) && b.maquina == a.maquina, "base64url con cualquier largo");
        }
        CHECK(!cap::leerCodigo("hola", b), "texto cualquiera: no");
        CHECK(!cap::leerCodigo("BACAP1.!!!", b), "basura: no");
        cap::Codigo corta = a;
        corta.credencial = "corta";
        CHECK(!cap::leerCodigo(cap::armarCodigo(corta), b), "sin una credencial de verdad: no");
        cap::Codigo rara = a;
        rara.url = "file:///etc/passwd";
        CHECK(!cap::leerCodigo(cap::armarCodigo(rara), b), "solo http(s)");
    }
    // --- El historial ----------------------------------------------------------------------------
    {
        CHECK(cap::limpiarNombre("Mac de Jorge / sala 2") == "Mac-de-Jorge-sala-2", "el nombre de la máquina, apto para archivo");
        CHECK(cap::limpiarNombre("") == "maquina", "sin nombre: «maquina»");
        const std::string a = cap::nombreArchivo(1700000000000LL, "pc", "este");
        CHECK(a.size() > 20 && a.find("-pc-este.json") != std::string::npos, "fecha-máquina-tipo.json");
    }
    std::printf(g_fallas == 0 ? "\nTODO OK\n" : "\n%d FALLAS\n", g_fallas);
    return g_fallas == 0 ? 0 : 1;
}
