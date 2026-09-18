// ============================================================================
// src/obs-module/ba_ipc_sources.cpp — Fuentes IPC de invitado para libobs
//
// Proposito:
//   Registra las dos fuentes que traen a un invitado remoto DENTRO de la
//   escena de OBS, cerrando el camino de datos B -> A (spec §4.3/§8.5):
//
//     - `ba_guest_audio`: lee el ring `BA.Audio.Guest.<slot>` que publica el
//       Proceso B y entrega las muestras a libobs.
//     - `ba_guest_video`: lee el canal compartido `BA.Guest.<slot>` (Spout en
//       Windows, memoria compartida en el resto) y entrega los frames BGRA.
//
//   Sin estas fuentes el gateway escribia en dos canales que no leia nadie: el
//   invitado hablaba y nunca llegaba a la emision. Son el consumidor que
//   faltaba.
//
//   Tambien arregla el ABI del modulo. El archivo anterior exportaba
//   `obs_module_register`, que NO es parte de libobs: el host busca
//   `obs_module_load` (via OBS_DECLARE_MODULE). Con el simbolo equivocado el
//   modulo no se cargaba en ninguna plataforma, por correcto que fuera su
//   contenido.
//
// Modelo de threading:
//   Cada fuente corre UN hilo propio que hace polling del canal IPC y llama a
//   `obs_source_output_audio` / `obs_source_output_video`. libobs espera
//   exactamente eso de una fuente asincrona. El hilo se crea en `create` y se
//   une en `destroy`; `update` solo cambia el slot bajo un mutex, porque puede
//   llegar desde el hilo de la UI mientras el de datos corre.
//
// Estrategia de errores:
//   Ninguna excepcion cruza el borde C de libobs (cada callback tiene su
//   catch). Si el canal IPC todavia no existe -- el Proceso B puede arrancar
//   despues -- la fuente reintenta abrirlo cada 500 ms y mientras tanto no
//   entrega nada, en vez de fallar la creacion.
// ============================================================================

#include <obs-module.h>
#include <obs.h>
#include <util/platform.h> // os_gettime_ns

#include <atomic>
#include <chrono>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "ipc/HealthBlock.hpp"
#include "ipc/SharedAudioRing.hpp"
#include "ipc/SpoutTransport.hpp"

namespace {

/// Rango de invitados (spec §3.2, §8.2).
constexpr int kMinSlot = 1;
constexpr int kMaxSlot = 8; // invitados 1..4; previews del lobby 1..8
constexpr int kDefaultSlot = 1;

/// Periodo de reintento cuando el canal IPC todavia no existe.
constexpr int kReopenMs = 500;

/// Canal de origen: "guest" (camara/voz del invitado en el programa),
/// "share" (su pantalla o ventana compartida, solo video) o "lobby" (preview y
/// voz de alguien que espera; lo usa la vista del operador, nunca el programa).
std::string channelFromSettings(obs_data_t* settings) {
    const char* ch = obs_data_get_string(settings, "channel");
    const std::string c = ch != nullptr ? ch : "";
    return (c == "share" || c == "lobby") ? c : std::string("guest");
}

/// Nombre wide del canal de audio (BA.Audio.Guest.<n> / BA.Audio.Lobby.<n>).
std::wstring audioName(const std::string& channel, int slot) {
    const std::string s = (channel == "lobby" ? "BA.Audio.Lobby." : "BA.Audio.Guest.") + std::to_string(slot);
    return std::wstring(s.begin(), s.end());
}

/// Nombre wide del canal de video (BA.Guest / BA.GuestShare / BA.Lobby).
std::wstring videoName(const std::string& channel, int slot) {
    const char* base = channel == "share" ? "BA.GuestShare." : channel == "lobby" ? "BA.Lobby." : "BA.Guest.";
    const std::string s = base + std::to_string(slot);
    return std::wstring(s.begin(), s.end());
}

/// Lee el slot de los ajustes, acotado al rango valido.
int slotFromSettings(obs_data_t* settings) {
    int slot = static_cast<int>(obs_data_get_int(settings, "slot"));
    if (slot < kMinSlot) {
        slot = kMinSlot;
    }
    if (slot > kMaxSlot) {
        slot = kMaxSlot;
    }
    return slot;
}

/// Agrega la propiedad de seleccion de invitado.
void addSlotProperty(obs_properties_t* props) {
    obs_property_t* list = obs_properties_add_list(props, "slot", obs_module_text("Invitado"),
                                                   OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
    for (int i = kMinSlot; i <= kMaxSlot; ++i) {
        const std::string label = "Invitado " + std::to_string(i);
        obs_property_list_add_int(list, label.c_str(), i);
    }
}

// ============================================================================
// ba_guest_audio
// ============================================================================

struct GuestAudioSource {
    obs_source_t* source = nullptr;
    std::thread worker;
    std::atomic<bool> stop{false};
    std::mutex slotLock;
    int slot = kDefaultSlot;
    std::string channel = "guest";

    /// Bucle de datos: abre el ring y bombea bloques hacia libobs.
    void run() {
        ba::ipc::SharedAudioRing ring;
        bool open = false;
        int openedSlot = 0;
        std::vector<float> block;

        // Igual que en la fuente de preset: el sello de tiempo sale de un
        // CONTADOR DE MUESTRAS, no del reloj de pared. Con un sello por reloj
        // los bloques avanzan mas rapido que el audio y libobs los descarta
        // por "TS_SMOOTHING_THRESHOLD": el invitado entra en la escena y no se
        // lo oye. El contador se reinicia al reabrir el ring (otro invitado).
        std::uint64_t startNs = 0;
        std::uint64_t samplesSent = 0;

        std::string openedChannel;
        while (!stop.load(std::memory_order_relaxed)) {
            int want;
            std::string wantChannel;
            {
                std::lock_guard<std::mutex> lock(slotLock);
                want = slot;
                wantChannel = channel;
            }
            // Cambio de invitado/canal en caliente: se reabre el ring.
            if (open && (want != openedSlot || wantChannel != openedChannel)) {
                ring = ba::ipc::SharedAudioRing{};
                open = false;
            }
            if (!open) {
                const std::wstring name = audioName(wantChannel, want);
                if (ba::ipc::SharedAudioRing::openReader(name.c_str(), ring).ok) {
                    open = true;
                    openedSlot = want;
                    openedChannel = wantChannel;
                    block.assign(ring.blockFloats(), 0.0f);
                    startNs = os_gettime_ns();
                    samplesSent = 0;
                } else {
                    // El Proceso B puede no haber arrancado todavia.
                    std::this_thread::sleep_for(std::chrono::milliseconds(kReopenMs));
                    continue;
                }
            }

            // B se reinicio (ring nuevo con el mismo nombre): reabrir, o el
            // invitado queda mudo leyendo un ring que ya nadie escribe.
            if (ring.stale()) {
                ring = ba::ipc::SharedAudioRing{};
                open = false;
                continue;
            }
            ba::ipc::PoppedBlock meta;
            if (!ring.pop(block.data(), meta)) {
                std::this_thread::sleep_for(std::chrono::milliseconds(kReopenMs));
                continue;
            }
            if (!meta.isSilence) {
                struct obs_source_audio out = {};
                out.data[0] = reinterpret_cast<const uint8_t*>(block.data());
                out.frames = static_cast<uint32_t>(ba::ipc::audio_ring::kBlockFrames);
                out.speakers = SPEAKERS_STEREO;
                out.format = AUDIO_FORMAT_FLOAT; // interleaved, como el ring
                out.samples_per_sec = ba::ipc::audio_ring::kSampleRate;
                out.timestamp = startNs + static_cast<std::uint64_t>(samplesSent * 1000000000ull /
                                                                     ba::ipc::audio_ring::kSampleRate);
                obs_source_output_audio(source, &out);
                samplesSent += ba::ipc::audio_ring::kBlockFrames;
            }
            // Un bloque son 10 ms; el pacing lo marca el propio ring.
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    }
};

const char* guestAudioGetName(void*) { return obs_module_text("Audio de invitado (BA)"); }

void* guestAudioCreate(obs_data_t* settings, obs_source_t* source) {
    try {
        auto* ctx = new GuestAudioSource();
        ctx->source = source;
        ctx->slot = slotFromSettings(settings);
        ctx->channel = channelFromSettings(settings);
        ctx->worker = std::thread([ctx] { ctx->run(); });
        return ctx;
    } catch (...) {
        return nullptr;
    }
}

void guestAudioDestroy(void* data) {
    auto* ctx = static_cast<GuestAudioSource*>(data);
    if (ctx == nullptr) {
        return;
    }
    ctx->stop.store(true);
    if (ctx->worker.joinable()) {
        ctx->worker.join();
    }
    delete ctx;
}

void guestAudioUpdate(void* data, obs_data_t* settings) {
    auto* ctx = static_cast<GuestAudioSource*>(data);
    if (ctx == nullptr) {
        return;
    }
    std::lock_guard<std::mutex> lock(ctx->slotLock);
    ctx->slot = slotFromSettings(settings);
    ctx->channel = channelFromSettings(settings);
}

obs_properties_t* guestAudioProperties(void*) {
    obs_properties_t* props = obs_properties_create();
    addSlotProperty(props);
    return props;
}

void guestAudioDefaults(obs_data_t* settings) { obs_data_set_default_int(settings, "slot", kDefaultSlot); }

// ============================================================================
// ba_guest_video
// ============================================================================

struct GuestVideoSource {
    obs_source_t* source = nullptr;
    std::thread worker;
    std::atomic<bool> stop{false};
    std::mutex slotLock;
    int slot = kDefaultSlot;
    std::string channel = "guest";

    void run() {
        ba::ipc::SpoutReceiver receiver;
        bool sub = false;
        int subSlot = 0;
        std::vector<uint8_t> pixels;
        uint32_t width = 0;
        uint32_t height = 0;

        std::string subChannel;
        while (!stop.load(std::memory_order_relaxed)) {
            int want;
            std::string wantChannel;
            {
                std::lock_guard<std::mutex> lock(slotLock);
                want = slot;
                wantChannel = channel;
            }
            if (sub && (want != subSlot || wantChannel != subChannel)) {
                receiver = ba::ipc::SpoutReceiver{};
                sub = false;
            }
            if (!sub) {
                const std::wstring name = videoName(wantChannel, want);
                if (receiver.subscribe(name.c_str())) {
                    sub = true;
                    subSlot = want;
                    subChannel = wantChannel;
                    width = static_cast<uint32_t>(receiver.width());
                    height = static_cast<uint32_t>(receiver.height());
                    pixels.assign(static_cast<size_t>(width) * height * 4u, 0u);
                } else {
                    std::this_thread::sleep_for(std::chrono::milliseconds(kReopenMs));
                    continue;
                }
            }

            // B se reinicio (slot nuevo con el mismo nombre): resuscribirse, o
            // el video del invitado queda congelado en el ultimo frame.
            if (receiver.stale()) {
                receiver = ba::ipc::SpoutReceiver{};
                sub = false;
                continue;
            }
            if (receiver.hasData()) {
                ba::ipc::FrameMeta meta{};
                if (receiver.receive(pixels.data(), pixels.size(), meta)) {
                    struct obs_source_frame frame = {};
                    frame.data[0] = pixels.data();
                    frame.linesize[0] = width * 4u;
                    frame.width = width;
                    frame.height = height;
                    frame.format = VIDEO_FORMAT_BGRA;
                    frame.timestamp = os_gettime_ns();
                    obs_source_output_video(source, &frame);
                }
            }
            // ~60 Hz de sondeo: por encima de cualquier cadencia de emision.
            std::this_thread::sleep_for(std::chrono::milliseconds(16));
        }
    }
};

const char* guestVideoGetName(void*) { return obs_module_text("Video de invitado (BA)"); }

void* guestVideoCreate(obs_data_t* settings, obs_source_t* source) {
    try {
        auto* ctx = new GuestVideoSource();
        ctx->source = source;
        ctx->slot = slotFromSettings(settings);
        ctx->channel = channelFromSettings(settings);
        ctx->worker = std::thread([ctx] { ctx->run(); });
        return ctx;
    } catch (...) {
        return nullptr;
    }
}

void guestVideoDestroy(void* data) {
    auto* ctx = static_cast<GuestVideoSource*>(data);
    if (ctx == nullptr) {
        return;
    }
    ctx->stop.store(true);
    if (ctx->worker.joinable()) {
        ctx->worker.join();
    }
    delete ctx;
}

void guestVideoUpdate(void* data, obs_data_t* settings) {
    auto* ctx = static_cast<GuestVideoSource*>(data);
    if (ctx == nullptr) {
        return;
    }
    std::lock_guard<std::mutex> lock(ctx->slotLock);
    ctx->slot = slotFromSettings(settings);
    ctx->channel = channelFromSettings(settings);
}

obs_properties_t* guestVideoProperties(void*) {
    obs_properties_t* props = obs_properties_create();
    addSlotProperty(props);
    return props;
}

void guestVideoDefaults(obs_data_t* settings) { obs_data_set_default_int(settings, "slot", kDefaultSlot); }

} // namespace

// ============================================================================
// Descriptores y registro
// ============================================================================

extern "C" {

static struct obs_source_info ba_guest_audio_info = {};
static struct obs_source_info ba_guest_video_info = {};

/// Registra las dos fuentes IPC. La llama `obs_module_load` (ba_module.cpp).
void ba_register_ipc_sources(void) {
    ba_guest_audio_info.id = "ba_guest_audio";
    ba_guest_audio_info.type = OBS_SOURCE_TYPE_INPUT;
    ba_guest_audio_info.output_flags = OBS_SOURCE_AUDIO;
    ba_guest_audio_info.get_name = guestAudioGetName;
    ba_guest_audio_info.create = guestAudioCreate;
    ba_guest_audio_info.destroy = guestAudioDestroy;
    ba_guest_audio_info.update = guestAudioUpdate;
    ba_guest_audio_info.get_properties = guestAudioProperties;
    ba_guest_audio_info.get_defaults = guestAudioDefaults;
    ba_guest_audio_info.icon_type = OBS_ICON_TYPE_AUDIO_INPUT;
    obs_register_source(&ba_guest_audio_info);

    ba_guest_video_info.id = "ba_guest_video";
    ba_guest_video_info.type = OBS_SOURCE_TYPE_INPUT;
    ba_guest_video_info.output_flags = OBS_SOURCE_ASYNC_VIDEO;
    ba_guest_video_info.get_name = guestVideoGetName;
    ba_guest_video_info.create = guestVideoCreate;
    ba_guest_video_info.destroy = guestVideoDestroy;
    ba_guest_video_info.update = guestVideoUpdate;
    ba_guest_video_info.get_properties = guestVideoProperties;
    ba_guest_video_info.get_defaults = guestVideoDefaults;
    ba_guest_video_info.icon_type = OBS_ICON_TYPE_CAMERA;
    obs_register_source(&ba_guest_video_info);
}

} // extern "C"
