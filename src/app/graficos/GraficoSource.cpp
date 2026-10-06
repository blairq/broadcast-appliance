// ============================================================================
// src/app/graficos/GraficoSource.cpp — ver GraficoSource.hpp
// ============================================================================
#include "graficos/GraficoSource.hpp"

#include "FuentesApp.hpp"

#include <atomic>
#include <chrono>
#include <map>
#include <memory>
#include <mutex>
#include <thread>

namespace ba::app::graficos {
namespace {

struct Reproduccion {
    std::uint64_t id = 0;
    // Los cuadros y el hilo que los entrega. El hilo se apaga con `vivo`, que
    // es lo unico que el destructor necesita tocar.
    std::mutex m;
    std::vector<QImage> cuadros;
    int fps = 30;
    bool repetir = false;
    std::atomic<bool> vivo{false};
    std::thread hilo;

    void parar() {
        vivo.store(false);
        if (hilo.joinable()) hilo.join();
    }
    ~Reproduccion() { parar(); }
};

std::mutex g_lock;
std::map<std::uint64_t, std::shared_ptr<Reproduccion>> g_rep; // por id de FuentesApp

void entregar(std::uint64_t id, const QImage& img) {
    if (img.isNull()) return;
    // El motor espera BGRA con alfa recto (QImage ARGB32 en memoria es BGRA).
    const QImage recto = img.format() == QImage::Format_ARGB32 ? img : img.convertToFormat(QImage::Format_ARGB32);
    fuentes::empujar(id, recto.constBits(), recto.width(), recto.height(), static_cast<int>(recto.bytesPerLine()));
}

bool viva(std::uint64_t id, fuentes::Instancia* out = nullptr) {
    for (const auto& f : fuentes::de(kSourceId))
        if (f.id == id) {
            if (out != nullptr) *out = f;
            return true;
        }
    return false;
}

std::shared_ptr<Reproduccion> reproduccion(std::uint64_t id, bool crear) {
    std::lock_guard<std::mutex> lock(g_lock);
    auto it = g_rep.find(id);
    if (it != g_rep.end()) return it->second;
    if (!crear) return nullptr;
    auto r = std::make_shared<Reproduccion>();
    r->id = id;
    g_rep[id] = r;
    return r;
}

/// Las reproducciones de fuentes que ya no existen se paran.
void limpiar() {
    std::vector<std::shared_ptr<Reproduccion>> muertas;
    {
        std::lock_guard<std::mutex> lock(g_lock);
        for (auto it = g_rep.begin(); it != g_rep.end();) {
            if (!viva(it->first)) {
                muertas.push_back(it->second);
                it = g_rep.erase(it);
            } else {
                ++it;
            }
        }
    }
    // FUERA del lock: el hilo podría estar esperándolo.
    for (auto& r : muertas)
        r->parar();
}

} // namespace

void registerSourceType() {
    fuentes::Tipo t;
    t.id = kSourceId;
    t.nombre = "Gráfico animado";
    t.deApp = true;
    t.defectos = {{"grafico", "zocalo"}, {"ancho", 900}, {"alto", 220}};
    t.props = [](const nlohmann::json&) {
        return std::vector<fuentes::Prop>{{"grafico", "Gráfico", "text"},
                                          {"ancho", "Ancho", "int", 64, 3840, 8},
                                          {"alto", "Alto", "int", 64, 2160, 8}};
    };
    fuentes::registrar(std::move(t));
}

std::vector<InstanciaInfo> instancias() {
    limpiar();
    std::vector<InstanciaInfo> out;
    for (const auto& f : fuentes::de(kSourceId)) {
        InstanciaInfo info;
        info.id = f.id;
        info.nombre = f.nombre;
        info.grafico = f.ajustes.value("grafico", std::string("zocalo"));
        info.ancho = f.ajustes.value("ancho", 900);
        info.alto = f.ajustes.value("alto", 220);
        if (auto r = reproduccion(f.id, false)) {
            info.reproduciendo = r->vivo.load();
            std::lock_guard<std::mutex> l2(r->m);
            info.cuadros = static_cast<int>(r->cuadros.size());
        }
        out.push_back(info);
    }
    return out;
}

bool setGrafico(std::uint64_t id, const std::string& grafico) {
    fuentes::Instancia f;
    if (!viva(id, &f)) return false;
    // A los ajustes de la fuente: es lo que se guarda con la escena.
    fuentes::cambiarAjustes(f.nombre, nlohmann::json{{"grafico", grafico}});
    return true;
}

bool reproducir(std::uint64_t id, std::vector<QImage> cuadros, int fps, bool repetir) {
    if (cuadros.empty() || fps <= 0 || !viva(id)) return false;
    limpiar();
    auto inst = reproduccion(id, true);
    inst->parar(); // si habia otra animacion andando, esta la reemplaza
    {
        std::lock_guard<std::mutex> lock(inst->m);
        // Convertir aca y no en cada cuadro evita hacerlo treinta veces por segundo.
        for (QImage& c : cuadros)
            if (c.format() != QImage::Format_ARGB32) c = c.convertToFormat(QImage::Format_ARGB32);
        inst->cuadros = std::move(cuadros);
        inst->fps = fps;
        inst->repetir = repetir;
    }

    inst->vivo.store(true);
    inst->hilo = std::thread([inst]() {
        const auto paso = std::chrono::microseconds(1000000 / std::max(1, inst->fps));
        auto siguiente = std::chrono::steady_clock::now();
        std::size_t i = 0;
        while (inst->vivo.load()) {
            QImage cuadro;
            bool ultimo = false;
            {
                std::lock_guard<std::mutex> lock(inst->m);
                if (inst->cuadros.empty()) break;
                if (i >= inst->cuadros.size()) {
                    if (!inst->repetir) {
                        // Se acabo la animacion: el ULTIMO cuadro queda fijo,
                        // que es lo que hace un zocalo que entra y se sostiene.
                        i = inst->cuadros.size() - 1;
                        ultimo = true;
                    } else {
                        i = 0;
                    }
                }
                cuadro = inst->cuadros[i];
            }
            entregar(inst->id, cuadro);
            // Ya no hay nada nuevo: el motor sigue mostrando el ultimo cuadro.
            if (ultimo) break;
            ++i;
            siguiente += paso;
            std::this_thread::sleep_until(siguiente);
        }
    });
    return true;
}

bool ocultar(std::uint64_t id) {
    fuentes::Instancia f;
    if (!viva(id, &f)) return false;
    if (auto r = reproduccion(id, false)) r->parar();
    // Un cuadro transparente: sin esto seguiria el ultimo.
    QImage vacio(f.ajustes.value("ancho", 900), f.ajustes.value("alto", 220), QImage::Format_ARGB32);
    vacio.fill(Qt::transparent);
    entregar(id, vacio);
    return true;
}

} // namespace ba::app::graficos
