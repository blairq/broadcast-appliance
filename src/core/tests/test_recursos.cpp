// test_recursos.cpp — diag/Recursos: lo que se mide de la máquina, el historial y los umbrales
//
//   - una medición de verdad da valores en rango (CPU 0–100, RAM usada ≤ total,
//     red ≥ 0) y lo que el sistema no dice queda DESCONOCIDO (-1 / null), no 0:
//     en una máquina sin datos de GPU (Linux con Intel o NVIDIA, una virtual) no falla;
//   - el propio proceso aparece como «app» con su memoria;
//   - el historial es un anillo: no pasa de su capacidad y la serie va del más viejo
//     al más nuevo; el pico es el máximo;
//   - los estados necesitan SOSTENERSE (aviso ≥80, crítico ≥92 por 5 s) para subir
//     y otros tantos por debajo para bajar; los cuadros perdidos tienen los suyos;
//   - el margen es lo que le queda al recurso más cargado;
//   - el hilo mide solo y avisa en cada vuelta.
#include "diag/Recursos.hpp"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <string>
#include <thread>

namespace {
int fallos = 0;
void check(bool ok, const char* que) {
    std::printf("%s: %s\n", ok ? "ok" : "FALLA", que);
    if (!ok) ++fallos;
}
bool desconocidoOEnRango(double v, double lo, double hi) { return v == ba::diag::Recursos::kDesconocido || (v >= lo && v <= hi); }

using ba::diag::Recursos;

Recursos::Muestra conCpu(double cpu, std::int64_t t) {
    Recursos::Muestra m;
    m.t = t;
    m.cpu = cpu;
    m.ramUsadaMb = 4000;
    m.ramTotalMb = 16000; // 25 %
    return m;
}
} // namespace

int main() {
    std::printf("== test_recursos ==\n");

    // --- Una medición de verdad ----------------------------------------------------
    {
        Recursos r;
        r.medir(); // la primera solo anota los contadores
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        const Recursos::Muestra m = r.medir();
        std::printf("   cpu %.1f %% | ram %.0f/%.0f MB | gpu %.1f %% (%s) | vram %.0f/%.0f MB | red %.3f/%.3f Mbps | "
                    "disco %.2f MB/s\n",
                    m.cpu, m.ramUsadaMb, m.ramTotalMb, m.gpu, m.gpuNombre.c_str(), m.vramUsadaMb, m.vramTotalMb,
                    m.redEntradaMbps, m.redSalidaMbps, m.discoEscrituraMBs);
        check(m.t > 0, "la muestra tiene hora");
        check(m.nucleos >= 1, "sabe cuántos núcleos hay");
        check(desconocidoOEnRango(m.cpu, 0, 100), "CPU de la máquina: 0–100 o desconocida");
#if defined(__APPLE__) || defined(__linux__) || defined(_WIN32)
        check(m.cpu >= 0, "en los tres sistemas la CPU se sabe");
        check(m.ramTotalMb > 0 && m.ramUsadaMb >= 0 && m.ramUsadaMb <= m.ramTotalMb, "RAM: usada entre 0 y el total");
        check(m.redEntradaMbps >= 0 && m.redSalidaMbps >= 0, "red: medida (aunque sea 0)");
#endif
        check(desconocidoOEnRango(m.gpu, 0, 100), "GPU: 0–100 o desconocida (nunca un número inventado)");
        check(m.vramUsadaMb == Recursos::kDesconocido || m.vramUsadaMb >= 0, "VRAM usada: ≥0 o desconocida");
        check(m.vramTotalMb == Recursos::kDesconocido || m.vramTotalMb > 0, "VRAM total: >0 o desconocida");
        check(desconocidoOEnRango(m.redPct, 0, 100), "red en % del enlace: 0–100 o desconocida");
        check(m.fps == Recursos::kDesconocido && m.perdidosPct == Recursos::kDesconocido,
              "sin motor, fps y perdidos quedan desconocidos");
        bool app = false;
        for (const auto& p : m.procesos)
            if (p.nombre == "app" && p.pid == Recursos::pidPropio()) {
                app = true;
                check(p.rssMb > 0, "la memoria del propio proceso");
                check(desconocidoOEnRango(p.cpu, 0, 100), "la CPU del propio proceso en rango");
            }
        check(app, "el propio proceso aparece como «app»");
        // Un pid que no existe: está pedido pero no anda, sin romper nada.
        r.procesos({{"gateway", 0x7ffffff0u}});
        const Recursos::Muestra m2 = r.medir();
        bool gw = false;
        for (const auto& p : m2.procesos)
            if (p.nombre == "gateway") gw = p.cpu == Recursos::kDesconocido && p.rssMb == Recursos::kDesconocido;
        check(gw, "un proceso que no está: desconocido");
        r.agregar(m2);
        const nlohmann::json j = r.informe(60);
        check(j["gpu"]["pct"].is_null() == (m2.gpu < 0), "en el informe, lo desconocido es null");
        check(j["gpu"]["estado"] == (m2.gpu < 0 ? "desconocido" : "normal"), "el estado de la GPU");
        check(j.contains("historial") && j["historial"]["filas"].size() == 1, "el historial pedido (una fila)");
    }

    // --- El anillo, la serie y el pico ---------------------------------------------
    {
        Recursos r(30);
        check(r.capacidad() == 30, "capacidad del historial");
        for (int i = 0; i < 45; ++i) r.agregar(conCpu(i, 1'000'000 + i * 1000));
        check(r.muestras() == 30, "el anillo no pasa de su capacidad");
        const auto s = r.serie("cpu", 10);
        check(s.size() == 10 && s.front() == 35 && s.back() == 44, "la serie: del más viejo al más nuevo");
        check(r.serie("cpu", 1000).size() == 30, "no da más de lo que hay");
        check(r.pico("cpu") == 44, "el pico es el máximo del historial (lo viejo se olvida)");
        check(r.pico("gpu") == Recursos::kDesconocido, "GPU nunca medida: sin pico");
        check(r.serie("nada", 10).empty(), "una serie que no existe");
        const nlohmann::json j = r.informe(30, 10);
        check(j["historial"]["filas"].size() == 3, "agrupado de a 10 s: 3 filas");
        check(j["historial"]["filas"].back()[1] == 44.0, "cada grupo da su máximo, el último al final");
        check(j["historial"]["filas"].front()[0] == 20, "«hace_s» del grupo más viejo (su punto más nuevo)");
    }

    // --- Umbrales sostenidos -------------------------------------------------------
    {
        Recursos r;
        std::int64_t t = 2'000'000;
        const auto paso = [&](double cpu) { r.agregar(conCpu(cpu, t += 1000)); };
        paso(10);
        check(r.estado("cpu") == Recursos::Estado::Normal, "normal");
        check(r.estado("gpu") == Recursos::Estado::Desconocido, "sin dato: desconocido");
        for (int i = 0; i < 4; ++i) paso(85);
        check(r.estado("cpu") == Recursos::Estado::Normal, "4 s arriba de 80 todavía no es aviso");
        paso(85);
        check(r.estado("cpu") == Recursos::Estado::Aviso, "5 s arriba de 80: aviso");
        paso(99);
        paso(30); // un bajón de un segundo no cuenta como sostenido
        for (int i = 0; i < 4; ++i) paso(95);
        check(r.estado("cpu") == Recursos::Estado::Aviso, "el crítico también tiene que sostenerse");
        paso(95);
        check(r.estado("cpu") == Recursos::Estado::Critico, "5 s arriba de 92: crítico");
        for (int i = 0; i < 4; ++i) paso(50);
        check(r.estado("cpu") == Recursos::Estado::Critico, "para bajar también hay que sostenerse");
        paso(50);
        check(r.estado("cpu") == Recursos::Estado::Normal, "5 s abajo de todo: vuelve a normal");
        const nlohmann::json n = r.niveles();
        check(n["cpu"]["estado"] == "normal" && n["gpu"]["estado"] == "desconocido", "niveles para las alarmas");

        // Los cuadros perdidos tienen sus propios umbrales (1 % y 5 %).
        Recursos::Umbrales u = r.umbrales();
        u.sostenidoS = 2;
        r.umbrales(u);
        for (int i = 0; i < 2; ++i) {
            Recursos::Muestra m = conCpu(10, t += 1000);
            m.perdidosPct = 6;
            r.agregar(m);
        }
        check(r.estado("motor") == Recursos::Estado::Critico, "perdiendo 6 % de los cuadros: crítico");
        check(r.estado("cpu") == Recursos::Estado::Normal, "un umbral sostenido configurable");
    }

    // --- El margen -----------------------------------------------------------------
    {
        Recursos r;
        for (int i = 0; i < 10; ++i) {
            Recursos::Muestra m = conCpu(60, 3'000'000 + i * 1000);
            m.gpu = 70;
            r.agregar(m);
        }
        const nlohmann::json j = r.informe();
        check(j["margen"]["limita"] == "gpu" && j["margen"]["pct"] == 30.0, "el margen lo pone el más cargado (GPU al 70 %)");
        check(j["cpu"]["margen"] == 40.0 && j["ram"]["margen"] == 75.0, "el margen de cada uno");
        check(j["estado"] == "normal", "todo normal");
        check(j["umbrales"]["aviso_pct"] == 80.0 && j["umbrales"]["critico_pct"] == 92.0, "los umbrales de fábrica");
    }

    // --- El hilo -------------------------------------------------------------------
    {
        Recursos r;
        std::atomic<int> vueltas{0};
        r.arrancar([&]() { ++vueltas; }, 100);
        for (int i = 0; i < 50 && vueltas < 2; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(50));
        r.parar();
        check(vueltas >= 2, "el hilo mide solo y avisa");
        check(r.muestras() >= 2, "y lo suma al historial");
        r.parar(); // dos veces no rompe
    }

    std::printf("%s (%d fallos)\n", fallos == 0 ? "OK" : "FALLA", fallos);
    return fallos == 0 ? 0 : 1;
}
