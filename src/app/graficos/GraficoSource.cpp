// ============================================================================
// src/app/graficos/GraficoSource.cpp — ver GraficoSource.hpp
// ============================================================================
#include "graficos/GraficoSource.hpp"

#include <atomic>
#include <chrono>
#include <map>
#include <memory>
#include <mutex>
#include <thread>

#include <obs.h>
#include <util/platform.h>

namespace ba::app::graficos {
namespace {

struct Instancia {
    InstanciaInfo info;
    obs_source_t* source = nullptr;

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
    ~Instancia() { parar(); }
};

std::mutex g_lock;
std::map<std::uint64_t, std::shared_ptr<Instancia>> g_vivas;
std::uint64_t g_siguienteId = 1;

void entregar(obs_source_t* s, const QImage& img) {
    if (s == nullptr || img.isNull()) return;
    obs_source_frame f = {};
    f.data[0] = const_cast<std::uint8_t*>(img.constBits());
    f.linesize[0] = static_cast<uint32_t>(img.bytesPerLine());
    f.width = static_cast<uint32_t>(img.width());
    f.height = static_cast<uint32_t>(img.height());
    f.format = VIDEO_FORMAT_BGRA;
    f.full_range = true;
    f.timestamp = os_gettime_ns();
    obs_source_output_video(s, &f);
}

const char* getName(void*) { return "Gráfico animado"; }

void* create(obs_data_t* settings, obs_source_t* source) {
    auto inst = std::make_shared<Instancia>();
    inst->source = source;
    inst->info.grafico = obs_data_get_string(settings, "grafico");
    inst->info.ancho = static_cast<int>(obs_data_get_int(settings, "ancho"));
    inst->info.alto = static_cast<int>(obs_data_get_int(settings, "alto"));
    const char* n = obs_source_get_name(source);
    inst->info.nombre = n != nullptr ? n : "";

    std::lock_guard<std::mutex> lock(g_lock);
    inst->info.id = g_siguienteId++;
    g_vivas[inst->info.id] = inst;
    // libobs guarda el id, no el puntero: asi nadie puede quedarse con una
    // instancia que ya se destruyo.
    return reinterpret_cast<void*>(static_cast<std::uintptr_t>(inst->info.id));
}

void destroy(void* data) {
    const auto id = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(data));
    std::shared_ptr<Instancia> inst;
    {
        std::lock_guard<std::mutex> lock(g_lock);
        const auto it = g_vivas.find(id);
        if (it == g_vivas.end()) return;
        inst = it->second;
        g_vivas.erase(it);
    }
    // El hilo se para FUERA del lock: si no, el propio hilo podria estar
    // esperandolo para entregar un cuadro y se trabarian entre si.
    inst->parar();
}

void update(void* data, obs_data_t* settings) {
    const auto id = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(data));
    std::lock_guard<std::mutex> lock(g_lock);
    const auto it = g_vivas.find(id);
    if (it == g_vivas.end()) return;
    it->second->info.grafico = obs_data_get_string(settings, "grafico");
    it->second->info.ancho = static_cast<int>(obs_data_get_int(settings, "ancho"));
    it->second->info.alto = static_cast<int>(obs_data_get_int(settings, "alto"));
}

void defaults(obs_data_t* s) {
    obs_data_set_default_string(s, "grafico", "zocalo");
    obs_data_set_default_int(s, "ancho", 900);
    obs_data_set_default_int(s, "alto", 220);
}

obs_properties_t* properties(void*) {
    obs_properties_t* p = obs_properties_create();
    obs_properties_add_text(p, "grafico", "Gráfico", OBS_TEXT_DEFAULT);
    obs_properties_add_int(p, "ancho", "Ancho", 64, 3840, 8);
    obs_properties_add_int(p, "alto", "Alto", 64, 2160, 8);
    return p;
}

struct obs_source_info g_info = {};

} // namespace

void registerSourceType() {
    g_info.id = kSourceId;
    g_info.type = OBS_SOURCE_TYPE_INPUT;
    g_info.output_flags = OBS_SOURCE_ASYNC_VIDEO | OBS_SOURCE_DO_NOT_DUPLICATE;
    g_info.get_name = getName;
    g_info.create = create;
    g_info.destroy = destroy;
    g_info.update = update;
    g_info.get_defaults = defaults;
    g_info.get_properties = properties;
    g_info.icon_type = OBS_ICON_TYPE_TEXT;
    obs_register_source(&g_info);
}

std::vector<InstanciaInfo> instancias() {
    std::lock_guard<std::mutex> lock(g_lock);
    std::vector<InstanciaInfo> out;
    out.reserve(g_vivas.size());
    for (const auto& [id, i] : g_vivas) {
        InstanciaInfo info = i->info;
        info.reproduciendo = i->vivo.load();
        {
            std::lock_guard<std::mutex> l2(i->m);
            info.cuadros = static_cast<int>(i->cuadros.size());
        }
        out.push_back(info);
    }
    return out;
}

bool reproducir(std::uint64_t id, std::vector<QImage> cuadros, int fps, bool repetir) {
    if (cuadros.empty() || fps <= 0) return false;
    std::shared_ptr<Instancia> inst;
    {
        std::lock_guard<std::mutex> lock(g_lock);
        const auto it = g_vivas.find(id);
        if (it == g_vivas.end()) return false;
        inst = it->second;
    }

    inst->parar(); // si habia otra animacion andando, esta la reemplaza
    {
        std::lock_guard<std::mutex> lock(inst->m);
        // BGRA premultiplicado es lo que libobs espera; convertir aca y no en
        // cada cuadro evita hacerlo treinta veces por segundo.
        for (QImage& c : cuadros)
            if (c.format() != QImage::Format_ARGB32_Premultiplied)
                c = c.convertToFormat(QImage::Format_ARGB32_Premultiplied);
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
            entregar(inst->source, cuadro);
            if (ultimo) {
                // Ya no hay nada nuevo que mostrar: se deja de consumir CPU y
                // libobs sigue mostrando el ultimo cuadro entregado.
                break;
            }
            ++i;
            siguiente += paso;
            std::this_thread::sleep_until(siguiente);
        }
    });
    return true;
}

bool ocultar(std::uint64_t id) {
    std::shared_ptr<Instancia> inst;
    {
        std::lock_guard<std::mutex> lock(g_lock);
        const auto it = g_vivas.find(id);
        if (it == g_vivas.end()) return false;
        inst = it->second;
    }
    inst->parar();
    // Un cuadro transparente: sin esto libobs seguiria mostrando el ultimo.
    QImage vacio(inst->info.ancho, inst->info.alto, QImage::Format_ARGB32_Premultiplied);
    vacio.fill(Qt::transparent);
    entregar(inst->source, vacio);
    return true;
}

} // namespace ba::app::graficos
