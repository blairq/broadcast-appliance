// src/app/tests/test_reconocedores.cpp — los dos reconocedores de voz, sin modelos
//
// 1. agent/Reconocedores: qué motor elige cada consumidor (de fábrica Parakeet,
//    se guarda), la carga y descarga por usos (una copia de cada modelo, con
//    margen), la alerta cuando están los dos a la vez y la opción de interrumpir.
// 2. audio/EscuchaStreaming con un modelo y un VAD falsos: varias corrientes
//    decodificadas en LOTE, el VAD que decide qué le llega al modelo (con sus
//    márgenes), el fin de turno del modelo y el cierre por silencio, el parcial
//    y la toma suelta (el dictado).

#include "persist/ColaEscritura.hpp"
#include "agent/Reconocedores.hpp"
#include "audio/EscuchaStreaming.hpp"

#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <filesystem>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

using ba::app::agent::MotorOido;
using ba::app::agent::Reconocedores;
using ba::app::audio::EscuchaStreaming;
using ba::app::audio::Reconocedor;

namespace {

int g_failures = 0;

#define CHECK(cond, msg)                                                                                     \
    do {                                                                                                     \
        if (!(cond)) {                                                                                       \
            std::printf("FALLO: %s (%s, linea %d)\n", msg, #cond, __LINE__);                                 \
            ++g_failures;                                                                                    \
        } else {                                                                                             \
            std::printf("ok: %s\n", msg);                                                                    \
        }                                                                                                    \
    } while (0)

// --- 1. Reconocedores -------------------------------------------------------------------

void probarReconocedores() {
    const auto dir = std::filesystem::temp_directory_path() / "ba-test-reconocedores";
    std::filesystem::remove_all(dir);
    const auto archivo = dir / "Config" / "reconocedores.json";
    std::map<MotorOido, int> cargas, descargas;
    Reconocedores r(archivo);
    r.setAcciones({[&](MotorOido m) { ++cargas[m]; }, [&](MotorOido m) { ++descargas[m]; }});
    r.setMargenMs(1000);

    for (const auto& c : Reconocedores::consumidores())
        CHECK(r.motorDe(c.id) == MotorOido::Parakeet, ("de fábrica, " + c.id + " usa Parakeet").c_str());
    CHECK(Reconocedores::consumidorDeClave("registro:c1") == "registro", "la clave del tótem");
    CHECK(Reconocedores::consumidorDeClave("atencion:x") == "recepcion", "la clave de la recepción");
    CHECK(Reconocedores::consumidorDeClave("Orador") == "transcripcion", "una salida de texto es transcripción");
    CHECK(Reconocedores::consumidorDeClave("dictado") == "dictado", "el dictado");
    CHECK(!r.elegir("nadie", "nemotron").empty(), "un consumidor que no existe da error");
    CHECK(!r.elegir("registro", "whisper").empty(), "un motor que no existe da error");

    // El dictado usa Parakeet: se carga una vez, aunque se use dos veces.
    r.usar("dictado", "dictado", 0);
    r.usar("Orador", "transcripcion", 0);
    CHECK(cargas[MotorOido::Parakeet] == 1, "Parakeet se carga una sola vez");
    CHECK(r.cargando(MotorOido::Parakeet), "mientras carga, «cargando»");
    r.informarCarga(MotorOido::Parakeet, true);
    CHECK(r.cargado(MotorOido::Parakeet), "cargado al avisar el cargador");
    CHECK(r.alerta().empty(), "un solo motor: sin alerta");

    // El tótem pasa a Nemotron: se guarda, y su uso carga Nemotron.
    CHECK(r.elegir("registro", "nemotron").empty(), "elegir Nemotron para el tótem");
    CHECK(r.motorDe("registro") == MotorOido::Nemotron, "el tótem usa Nemotron");
    CHECK(r.usar("registro:c1", "registro", 10) == MotorOido::Nemotron, "el uso del tótem va a Nemotron");
    r.usar("registro:c2", "registro", 10);
    CHECK(cargas[MotorOido::Nemotron] == 1, "Nemotron se carga una sola vez para dos tótems");
    r.informarCarga(MotorOido::Nemotron, true);
    const std::string a = r.alerta();
    CHECK(!a.empty(), "los dos en uso: alerta");
    CHECK(a.find("Tótem de registro") != std::string::npos && a.find("Dictado del operador") != std::string::npos,
          "la alerta dice quién usa cada uno");
    CHECK(a.find("MB") != std::string::npos, "la alerta dice cuánta memoria");
    const auto e = r.estado();
    CHECK(e["alerta"].get<std::string>() == a && e["mezcla"].get<bool>(), "el estado trae la alerta y la mezcla");
    CHECK(e["memoria_mb"].get<int>() ==
              Reconocedores::memoriaMB(MotorOido::Parakeet) + Reconocedores::memoriaMB(MotorOido::Nemotron),
          "la memoria de los dos cargados");

    // Interrumpir: solo el tótem, y solo con Nemotron.
    CHECK(r.interrumpir("registro"), "el tótem con Nemotron se puede interrumpir (de fábrica)");
    CHECK(!r.interrumpir("recepcion"), "la recepción no (no tiene cancelación de eco)");
    CHECK(!r.setInterrumpir("dictado", true).empty(), "el dictado no se puede interrumpir");
    CHECK(r.setInterrumpir("registro", false).empty() && !r.interrumpir("registro"), "apagar la interrupción");

    // Se van los dos tótems: Nemotron queda el margen y después se descarga.
    r.soltar("registro:c1", 100);
    r.soltar("registro:c2", 200);
    CHECK(r.alerta().empty(), "sin usos de Nemotron: sin alerta");
    r.tic(900);
    CHECK(descargas[MotorOido::Nemotron] == 0 && r.cargado(MotorOido::Nemotron), "dentro del margen sigue cargado");
    // Vuelve un tótem dentro del margen: no se recarga.
    r.usar("registro:c3", "registro", 1000);
    CHECK(cargas[MotorOido::Nemotron] == 1, "un tótem que vuelve dentro del margen no recarga");
    r.soltar("registro:c3", 1100);
    r.tic(2000);
    CHECK(descargas[MotorOido::Nemotron] == 0, "el margen cuenta desde el último uso");
    r.tic(2200);
    CHECK(descargas[MotorOido::Nemotron] == 1 && !r.cargado(MotorOido::Nemotron), "pasado el margen se descarga");
    CHECK(descargas[MotorOido::Parakeet] == 0, "Parakeet, en uso, no se toca");

    // Con la máquina caliente (F4-05): lo que nadie usa se descarga en el tic, sin margen.
    r.usar("registro:c9", "registro", 2300);
    r.informarCarga(MotorOido::Nemotron, true);
    r.soltar("registro:c9", 2400);
    r.setPorCalor(true);
    r.tic(2401);
    CHECK(descargas[MotorOido::Nemotron] == 2 && !r.cargado(MotorOido::Nemotron), "por calor: sin uso, se descarga ya");
    CHECK(descargas[MotorOido::Parakeet] == 0 && r.cargado(MotorOido::Parakeet), "por calor: el que está en uso sigue");
    r.setPorCalor(false);

    // Una carga que llega tarde (se descargó mientras cargaba) se suelta.
    r.usar("registro:c4", "registro", 3000);
    r.soltar("registro:c4", 3000);
    r.tic(5000);
    const int antes = descargas[MotorOido::Nemotron];
    r.informarCarga(MotorOido::Nemotron, true);
    CHECK(descargas[MotorOido::Nemotron] == antes + 1 && !r.cargado(MotorOido::Nemotron),
          "la carga que llega tarde se suelta");

    // Cambiar el motor de un uso abierto lo mueve.
    r.usar("registro:c5", "registro", 6000);
    CHECK(r.elegir("registro", "parakeet").empty(), "volver a Parakeet");
    CHECK(r.usar("registro:c5", "registro", 6000) == MotorOido::Parakeet, "el uso pasa a Parakeet");
    CHECK(!r.enUso(MotorOido::Nemotron), "Nemotron sin usos");

    // Los archivos cambiaron: en uso se recarga.
    const int cp = cargas[MotorOido::Parakeet];
    r.recargar(MotorOido::Parakeet);
    CHECK(cargas[MotorOido::Parakeet] == cp + 1, "recargar un motor en uso lo vuelve a cargar");

    // Lo elegido se guarda.
    CHECK(r.elegir("transcripcion", "nemotron").empty(), "la traducción en tiempo real con Nemotron");
    Reconocedores r2(archivo);
    CHECK(r2.motorDe("transcripcion") == MotorOido::Nemotron, "se lee lo elegido");
    CHECK(r2.motorDe("registro") == MotorOido::Parakeet, "lo de fábrica queda de fábrica");
    CHECK(!r2.interrumpirElegido("registro"), "se lee la opción de interrumpir");
    ba::persist::cola().vaciar(); // lo encolado, al disco antes de borrar (si no, el escritor la recrea)
    std::filesystem::remove_all(dir);
}

// --- 2. EscuchaStreaming con un modelo falso ---------------------------------------------

constexpr std::size_t kBloque = 8960; // 560 ms a 16 kHz, como Nemotron

class ModeloFalso : public EscuchaStreaming::Modelo {
   public:
    struct Stream {
        std::size_t recibido = 0, decodificado = 0, palabras = 0; ///< una «palabra» por bloque decodificado
        bool fin = false;
    };
    std::map<int, Stream> streams;
    std::vector<int> lotes; ///< tamaño de cada llamada a decodificar
    std::size_t aceptadas = 0;
    std::size_t turnoMax = 0; ///< > 0: fin de turno cada tanto decodificado (sigue hasta otra palabra)
    int ultimo = 0;
    int soltados = 0;
    int reinicios = 0;

    int nuevoStream() override {
        streams[++ultimo] = {};
        return ultimo;
    }
    void soltarStream(int s) override {
        streams.erase(s);
        ++soltados;
    }
    void aceptar(int s, const float*, std::size_t n) override {
        streams[s].recibido += n;
        aceptadas += n;
    }
    void terminarEntrada(int s) override { streams[s].fin = true; }
    bool listo(int s) override {
        const Stream& x = streams[s];
        const std::size_t falta = x.recibido - x.decodificado;
        return falta >= kBloque || (x.fin && falta > 0);
    }
    void decodificar(const std::vector<int>& ids) override {
        lotes.push_back(static_cast<int>(ids.size()));
        for (int s : ids) {
            Stream& x = streams[s];
            const std::size_t n = std::min(kBloque, x.recibido - x.decodificado);
            x.decodificado += n;
            ++x.palabras;
        }
    }
    /// Como el transducer: el texto del stream solo crece («p1 p2 p3…»), hasta un reinicio.
    std::string texto(int s) override {
        std::string t;
        for (std::size_t i = 1; i <= streams[s].palabras; ++i) t += " p" + std::to_string(i);
        return t;
    }
    /// Como sherpa-onnx: una vez en fin de turno, sigue ahí hasta la palabra siguiente.
    bool finDeTurno(int s) override {
        const std::size_t cada = turnoMax / kBloque;
        return cada > 0 && streams[s].palabras > 0 && streams[s].palabras % cada == 0;
    }
    void reiniciar(int s) override {
        streams[s].palabras = 0;
        ++reinicios;
    }
};

/// Voz = una ventana con energía.
class DetectorFalso : public EscuchaStreaming::Detector {
   public:
    std::size_t ventana() const override { return 512; }
    bool voz(const float* m) override {
        double e = 0;
        for (std::size_t i = 0; i < 512; ++i) e += static_cast<double>(m[i]) * m[i];
        return e / 512 > 1e-4;
    }
};

/// Silencio y tono (una «voz» sintética: no sale por ningún parlante).
std::vector<float> senal(double silencioAntes, double voz, double silencioDespues) {
    std::vector<float> s(static_cast<std::size_t>(silencioAntes * 16000), 0.0f);
    for (std::size_t i = 0; i < static_cast<std::size_t>(voz * 16000); ++i)
        s.push_back(0.3f * static_cast<float>(std::sin(2 * 3.14159265 * 220.0 * static_cast<double>(i) / 16000)));
    s.insert(s.end(), static_cast<std::size_t>(silencioDespues * 16000), 0.0f);
    return s;
}

struct Frases {
    std::mutex m;
    std::condition_variable cv;
    std::map<std::string, std::vector<std::string>> por;
    std::vector<std::string> empezaron;
    std::size_t total() {
        std::size_t n = 0;
        for (auto& [k, v] : por) n += v.size();
        return n;
    }
};

void probarStreaming() {
    EscuchaStreaming e;
    ModeloFalso* modelo = nullptr;
    Frases frases;
    e.setFrase([&](const std::string& clave, const Reconocedor::Resultado& r) {
        std::lock_guard<std::mutex> l(frases.m);
        frases.por[clave].push_back(r.texto);
        frases.cv.notify_all();
    });
    e.setEmpiezaVoz([&](const std::string& clave) {
        std::lock_guard<std::mutex> l(frases.m);
        frases.empezaron.push_back(clave);
    });
    EscuchaStreaming::Margenes mg;
    mg.antes = 0.5;
    mg.despues = 0.8;
    e.setMargenes(mg);
    std::mutex cargaM;
    std::condition_variable cargaCv;
    bool cargo = false;
    std::string error = "?";
    e.cargar(
        [&](std::string&) {
            auto m = std::make_unique<ModeloFalso>();
            modelo = m.get();
            return std::unique_ptr<EscuchaStreaming::Modelo>(std::move(m));
        },
        [](const Reconocedor::Cortes&) { return std::unique_ptr<EscuchaStreaming::Detector>(new DetectorFalso); },
        [&](const std::string& err) {
            std::lock_guard<std::mutex> l(cargaM);
            error = err;
            cargo = true;
            cargaCv.notify_all();
        });
    {
        std::unique_lock<std::mutex> l(cargaM);
        cargaCv.wait_for(l, std::chrono::seconds(5), [&] { return cargo; });
    }
    CHECK(cargo && error.empty() && e.cargado(), "carga con el modelo falso");

    // Tres voces a la vez: 1 s de silencio, 2 s de voz, 1,5 s de silencio cada una.
    const std::vector<std::string> claves{"registro:a", "registro:b", "atencion:c"};
    for (const auto& k : claves) e.abrirContinuo(k);
    const std::vector<float> s = senal(1.0, 2.0, 1.5);
    const std::size_t bloque = 1600; // 100 ms
    bool vioParcial = false;
    for (std::size_t i = 0; i < s.size(); i += bloque) {
        const std::size_t n = std::min(bloque, s.size() - i);
        for (const auto& k : claves) e.alimentarContinuo(k, s.data() + i, n, 16000);
        e.esperarVuelta();
        for (const auto& [k, o] : e.oido())
            if (o.hablando && !o.parcial.empty()) vioParcial = true;
    }
    {
        std::unique_lock<std::mutex> l(frases.m);
        frases.cv.wait_for(l, std::chrono::seconds(5), [&] { return frases.total() >= 3; });
        CHECK(frases.por.size() == 3, "una frase por cada corriente");
        for (const auto& k : claves)
            CHECK(frases.por[k].size() == 1 && frases.por[k][0].rfind("p1", 0) == 0,
                  ("la frase de " + k + " (sin el espacio del principio)").c_str());
        CHECK(frases.empezaron.size() == 3, "avisa cuando cada una empieza a hablar");
    }
    CHECK(vioParcial, "mientras se habla hay texto parcial");
    const auto lote = e.lote();
    CHECK(lote.maximo == 3, "las tres corrientes se decodifican juntas, en un lote");
    // Al modelo le llega la voz más los márgenes (0,5 + 2 + 0,8 s), no los 4,5 s de cada una.
    const double porCorriente = static_cast<double>(modelo->aceptadas) / 3 / 16000;
    std::printf("   al modelo: %.2f s por corriente (de %.2f s oídos)\n", porCorriente,
                static_cast<double>(s.size()) / 16000);
    CHECK(porCorriente > 2.9 && porCorriente < 3.6, "al modelo, solo la voz y sus márgenes");
    CHECK(lote.segundosAlModelo < lote.segundosOidos * 0.85, "el VAD ahorra el silencio");
    CHECK(modelo->streams.empty() && modelo->soltados == 3, "cerrada la frase, el stream se suelta");

    // El fin de turno del modelo: una voz larga sale en dos frases sin cortar el stream.
    modelo->turnoMax = kBloque * 3; // ~1,7 s por turno
    {
        std::lock_guard<std::mutex> l(frases.m);
        frases.por.clear();
    }
    const std::vector<float> larga = senal(0.2, 4.0, 1.5);
    for (std::size_t i = 0; i < larga.size(); i += bloque) {
        e.alimentarContinuo("registro:a", larga.data() + i, std::min(bloque, larga.size() - i), 16000);
        e.esperarVuelta();
    }
    {
        std::unique_lock<std::mutex> l(frases.m);
        frases.cv.wait_for(l, std::chrono::seconds(5), [&] { return frases.por["registro:a"].size() >= 2; });
        CHECK(frases.por["registro:a"].size() >= 2, "el fin de turno del modelo corta una voz larga");
    }
    e.esperarVuelta();
    {
        // Sin reiniciar el stream (reiniciado, el de verdad se come las palabras siguientes):
        // cada frase trae lo nuevo, y juntas son todo lo dicho, sin huecos ni repetidos.
        std::lock_guard<std::mutex> l(frases.m);
        std::string juntas;
        for (const std::string& f : frases.por["registro:a"]) juntas += (juntas.empty() ? "" : " ") + f;
        std::string esperado;
        for (std::size_t i = 1; i <= std::count(juntas.begin(), juntas.end(), 'p'); ++i)
            esperado += (esperado.empty() ? "p" : " p") + std::to_string(i);
        std::printf("   frases: «%s»\n", juntas.c_str());
        CHECK(modelo->reinicios == 0, "el fin de turno no reinicia el stream");
        CHECK(juntas == esperado && frases.por["registro:a"].front() == "p1 p2 p3", "las frases son lo nuevo de cada turno");
    }
    modelo->turnoMax = 0;

    // Cerrar una corriente suelta su stream y deja de oírla.
    for (const auto& k : claves) e.cerrarContinuo(k);
    e.esperarVuelta();
    CHECK(e.oido().empty(), "cerradas, ya no aparecen en lo que se oye");

    // La toma suelta (el dictado), a 48 kHz: se remuestrea y se recorta el silencio.
    std::vector<float> toma48;
    for (float v : senal(1.0, 1.5, 1.0)) toma48.insert(toma48.end(), {v, v, v});
    std::mutex tm;
    std::condition_variable tcv;
    bool hecho = false;
    Reconocedor::Resultado res;
    e.transcribir(toma48, 48000, [&](const Reconocedor::Resultado& r) {
        std::lock_guard<std::mutex> l(tm);
        res = r;
        hecho = true;
        tcv.notify_all();
    });
    {
        std::unique_lock<std::mutex> l(tm);
        tcv.wait_for(l, std::chrono::seconds(5), [&] { return hecho; });
    }
    CHECK(hecho && res.error.empty() && !res.texto.empty(), "la toma suelta da texto");
    CHECK(res.segundosDeAudio > 1.4 && res.segundosDeAudio < 3.2, "la toma, recortada a la voz y sus márgenes");
    hecho = false;
    e.transcribir(std::vector<float>(48000, 0.0f), 48000, [&](const Reconocedor::Resultado& r) {
        std::lock_guard<std::mutex> l(tm);
        res = r;
        hecho = true;
        tcv.notify_all();
    });
    {
        std::unique_lock<std::mutex> l(tm);
        tcv.wait_for(l, std::chrono::seconds(5), [&] { return hecho; });
    }
    CHECK(!res.error.empty(), "una toma muda da error («no se escuchó a nadie»)");

    e.descargar();
    e.esperarVuelta();
    CHECK(!e.cargado(), "descargado");
}

void probarRemuestreo() {
    struct M : EscuchaStreaming::Modelo {
        int nuevoStream() override { return 0; }
        void soltarStream(int) override {}
        void aceptar(int, const float*, std::size_t) override {}
        void terminarEntrada(int) override {}
        bool listo(int) override { return false; }
        void decodificar(const std::vector<int>&) override {}
        std::string texto(int) override { return {}; }
        bool finDeTurno(int) override { return false; }
        void reiniciar(int) override {}
    } m;
    auto rs = m.remuestreador(48000);
    std::size_t total = 0;
    std::vector<float> b(1000, 0.5f); // bloques que no son múltiplo de 3
    for (int i = 0; i < 48; ++i) total += rs(b.data(), b.size()).size();
    CHECK(total == 16000, "48 kHz a 16 kHz: un tercio exacto, de a bloques");
    auto rs2 = m.remuestreador(44100);
    total = 0;
    for (int i = 0; i < 44; ++i) total += rs2(b.data(), b.size()).size();
    CHECK(total > 15900 && total < 16100, "44,1 kHz a 16 kHz");
}

} // namespace

int main() {
    probarReconocedores();
    probarRemuestreo();
    probarStreaming();
    if (g_failures > 0) {
        std::printf("test_reconocedores: %d fallas\n", g_failures);
        return 1;
    }
    std::printf("test_reconocedores: todo bien\n");
    return 0;
}
