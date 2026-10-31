// ============================================================================
// src/app/tests/clap_retardo256.cpp — un plugin CLAP sintético para las pruebas
//
// Un módulo que solo atrasa el audio 256 muestras y lo DECLARA
// (CLAP_EXT_LATENCY), como un limitador con lookahead pero sin nada más: el
// impulso que entra en la muestra 0 sale en la 256. Lo usa test_rack_latencia
// para ver que el rack informa esa latencia y que anularlo no la cambia.
// Sin parámetros, sin estado y sin editor: lo mínimo que el anfitrión acepta.
// ============================================================================

#include <clap/clap.h>

#include <algorithm>
#include <cstring>
#include <vector>

namespace {

constexpr uint32_t kRetardo = 256;

const char* const kRasgos[] = {CLAP_PLUGIN_FEATURE_AUDIO_EFFECT, CLAP_PLUGIN_FEATURE_STEREO, nullptr};

const clap_plugin_descriptor_t kDescriptor = {
    CLAP_VERSION_INIT, "ba.prueba.retardo256", "Retardo 256", "Broadcast Appliance", "", "", "", "1.0",
    "Atrasa 256 muestras y lo declara (pruebas)", kRasgos};

struct Retardo {
    clap_plugin_t plugin{};
    std::vector<float> linea[2];
    uint32_t pos = 0;
};

Retardo* de(const clap_plugin_t* p) { return static_cast<Retardo*>(p->plugin_data); }

bool puertoInfo(const clap_plugin_t*, uint32_t indice, bool, clap_audio_port_info_t* info) {
    if (indice != 0) return false;
    info->id = 0;
    std::strncpy(info->name, "principal", sizeof(info->name));
    info->flags = CLAP_AUDIO_PORT_IS_MAIN;
    info->channel_count = 2;
    info->port_type = CLAP_PORT_STEREO;
    info->in_place_pair = CLAP_INVALID_ID;
    return true;
}
uint32_t puertos(const clap_plugin_t*, bool) { return 1; }
const clap_plugin_audio_ports_t kPuertos = {puertos, puertoInfo};

uint32_t latencia(const clap_plugin_t*) { return kRetardo; }
const clap_plugin_latency_t kLatencia = {latencia};

clap_process_status procesar(const clap_plugin_t* p, const clap_process_t* proc) {
    Retardo* r = de(p);
    if (proc->audio_inputs_count < 1 || proc->audio_outputs_count < 1) return CLAP_PROCESS_ERROR;
    uint32_t pos = r->pos;
    for (int c = 0; c < 2; ++c) {
        pos = r->pos;
        const float* entra = proc->audio_inputs[0].data32[c];
        float* sale = proc->audio_outputs[0].data32[c];
        float* linea = r->linea[c].data();
        for (uint32_t i = 0; i < proc->frames_count; ++i) {
            const float v = entra[i]; // entrada y salida pueden ser el mismo buffer
            sale[i] = linea[pos];
            linea[pos] = v;
            if (++pos == kRetardo) pos = 0;
        }
    }
    r->pos = pos;
    return CLAP_PROCESS_CONTINUE;
}

bool iniciar(const clap_plugin_t*) { return true; }
void destruir(const clap_plugin_t* p) { delete de(p); }
bool activar(const clap_plugin_t* p, double, uint32_t, uint32_t) {
    Retardo* r = de(p);
    for (auto& l : r->linea) l.assign(kRetardo, 0.0f);
    r->pos = 0;
    return true;
}
void desactivar(const clap_plugin_t*) {}
bool empezar(const clap_plugin_t*) { return true; }
void parar(const clap_plugin_t*) {}
void reiniciar(const clap_plugin_t* p) {
    for (auto& l : de(p)->linea) std::fill(l.begin(), l.end(), 0.0f);
}
const void* extension(const clap_plugin_t*, const char* id) {
    if (std::strcmp(id, CLAP_EXT_AUDIO_PORTS) == 0) return &kPuertos;
    if (std::strcmp(id, CLAP_EXT_LATENCY) == 0) return &kLatencia;
    return nullptr;
}
void enPrincipal(const clap_plugin_t*) {}

uint32_t cuantos(const clap_plugin_factory_t*) { return 1; }
const clap_plugin_descriptor_t* descriptor(const clap_plugin_factory_t*, uint32_t i) {
    return i == 0 ? &kDescriptor : nullptr;
}
const clap_plugin_t* crear(const clap_plugin_factory_t*, const clap_host_t*, const char* id) {
    if (std::strcmp(id, kDescriptor.id) != 0) return nullptr;
    auto* r = new Retardo();
    r->plugin = {&kDescriptor, r,       iniciar, destruir,  activar,   desactivar, empezar,
                 parar,        reiniciar, procesar, extension, enPrincipal};
    return &r->plugin;
}
const clap_plugin_factory_t kFabrica = {cuantos, descriptor, crear};

bool entrar(const char*) { return true; }
void salir() {}
const void* fabrica(const char* id) { return std::strcmp(id, CLAP_PLUGIN_FACTORY_ID) == 0 ? &kFabrica : nullptr; }

} // namespace

extern "C" CLAP_EXPORT const clap_plugin_entry_t clap_entry = {CLAP_VERSION_INIT, entrar, salir, fabrica};
