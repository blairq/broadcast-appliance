// ============================================================================
// src/app/BroadcastEngine.cpp — Implementacion del motor de emision
//
// Ver BroadcastEngine.hpp para el proposito y el contrato. Aca vive todo lo que
// habla con libobs; es el unico archivo del Proceso A que incluye <obs.h>.
//
// Orden de arranque (importa, y equivocarlo es la causa tipica de un pipeline
// que "no hace nada"):
//   1. obs_startup            -> contexto global
//   2. obs_reset_video        -> modulo grafico + lienzo + formato de salida
//   3. obs_reset_audio        -> mezclador a 48 kHz
//   4. obs_add_module_path    -> donde estan los .so/.dll de los plugins
//   5. obs_load_all_modules   -> SIN esto no existen encoders ni salidas
//   6. obs_post_load_modules  -> los plugins terminan de inicializarse
// ============================================================================

#include "BroadcastEngine.hpp"

#include <obs.h>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <string>
#include <vector>

#include "ipc/HealthBlock.hpp" // nowQpc() para los timestamps del IPC

namespace ba::app {

namespace {

/// Nombres de los canales IPC hacia el Proceso B (spec §4.3/§8.5).
constexpr const wchar_t* kReturnAudioName = L"BA.Audio.ContentForReturn";
constexpr const wchar_t* kProgramVideoName = L"BA.Program";

/// Canal de salida de libobs donde vive la escena de programa.
constexpr std::uint32_t kProgramChannel = 0;

/// Puntero al motor que tiene instalados los callbacks crudos. libobs los
/// invoca como funciones C sin contexto de usuario para el video, asi que hace
/// falta un puntero de archivo. Solo hay un motor por proceso (A es uno solo).
BroadcastEngine* g_returnFeedOwner = nullptr;

/// Convierte UTF-8 a wide para los nombres de los canales IPC.
std::wstring toWide(const std::string& s) {
    std::wstring out;
    out.reserve(s.size());
    for (char c : s) {
        out.push_back(static_cast<wchar_t>(static_cast<unsigned char>(c)));
    }
    return out;
}

} // namespace

// ============================================================================
// Ciclo de vida
// ============================================================================

BroadcastEngine::BroadcastEngine() = default;

BroadcastEngine::~BroadcastEngine() { shutdown(); }

const char* BroadcastEngine::defaultGraphicsModule() {
#if defined(_WIN32)
    return "libobs-d3d11";
#else
    return "libobs-opengl";
#endif
}

void BroadcastEngine::setError(std::string msg) { lastError_ = std::move(msg); }

bool BroadcastEngine::startup(const EnginePaths& paths, const VideoConfig& video, const AudioConfig& audio) {
    if (started_) {
        return true; // idempotente
    }
    paths_ = paths;
    video_ = video;
    audio_ = audio;

    // 1) Contexto global. El segundo argumento es el directorio de config de
    //    los modulos (cada plugin guarda ahi lo suyo).
    if (!obs_startup("en-US", paths_.configDir.empty() ? nullptr : paths_.configDir.c_str(), nullptr)) {
        setError("obs_startup fallo");
        return false;
    }

    // 1b) Datos de libobs (shaders .effect). En el bundle portable viven en
    //     <root>/data/libobs; sin registrarlos, libobs los busca en la ruta de
    //     instalacion COMPILADA, que en una maquina de destino no existe. Es
    //     uno de los puntos donde un bundle "portable" deja de serlo.
    if (!paths_.obsDataDir.empty()) {
        obs_add_data_path(paths_.obsDataDir.c_str());
    }

    // 2) Video. `gpu_conversion` deja la conversion a NV12 en la GPU, que es lo
    //    que permite codificar 1080p sin quemar CPU en el camino.
    struct obs_video_info ovi = {};
    ovi.graphics_module = defaultGraphicsModule();
    ovi.fps_num = static_cast<uint32_t>(video_.fpsNum);
    ovi.fps_den = static_cast<uint32_t>(video_.fpsDen);
    ovi.base_width = static_cast<uint32_t>(video_.baseWidth);
    ovi.base_height = static_cast<uint32_t>(video_.baseHeight);
    ovi.output_width = static_cast<uint32_t>(video_.outputWidth);
    ovi.output_height = static_cast<uint32_t>(video_.outputHeight);
    ovi.output_format = VIDEO_FORMAT_NV12; // lo que esperan los encoders
    ovi.colorspace = VIDEO_CS_709;         // Rec.709: el estandar de HD
    ovi.range = VIDEO_RANGE_PARTIAL;       // limited range, como todo broadcast
    ovi.adapter = 0;
    ovi.gpu_conversion = true;
    ovi.scale_type = OBS_SCALE_BICUBIC;

    const int rc = obs_reset_video(&ovi);
    if (rc != OBS_VIDEO_SUCCESS) {
        // Motivos tipicos: falta el modulo grafico junto al binario
        // (MODULE_NOT_FOUND) o no hay contexto grafico disponible, por ejemplo
        // en una sesion sin display.
        std::string why = "obs_reset_video fallo (" + std::to_string(rc) + ")";
        if (rc == OBS_VIDEO_MODULE_NOT_FOUND) {
            why += ": no se encontro " + std::string(defaultGraphicsModule());
        } else if (rc == OBS_VIDEO_NOT_SUPPORTED) {
            why += ": el sistema no soporta el pipeline solicitado";
        }
        setError(why);
        obs_shutdown();
        return false;
    }

    // 3) Audio a 48 kHz (spec §7.4: el reloj del proyecto).
    struct obs_audio_info oai = {};
    oai.samples_per_sec = static_cast<uint32_t>(audio_.sampleRate);
    oai.speakers = (audio_.channels == 1) ? SPEAKERS_MONO : SPEAKERS_STEREO;
    if (!obs_reset_audio(&oai)) {
        setError("obs_reset_audio fallo");
        obs_shutdown();
        return false;
    }

    // 4-6) Modulos. Sin esto no hay codificadores ni salidas: es el paso que
    //      faltaba en el arranque anterior del Proceso A.
    if (!paths_.moduleBinDir.empty()) {
        // OJO con la forma de los argumentos: el primero es el DIRECTORIO donde
        // viven los modulos, NO un patron con `%module%` y extension. libobs
        // arma el nombre del archivo por su cuenta; si se le pasa
        // ".../%module%.so" no encuentra nada y no carga ningun plugin, en
        // silencio. El segundo si lleva `%module%`: es donde cada modulo busca
        // sus datos (locale, efectos).
        const std::string dataPattern = paths_.moduleDataDir.empty() ? (paths_.moduleBinDir + "/%module%")
                                                                     : (paths_.moduleDataDir + "/%module%");
        obs_add_module_path(paths_.moduleBinDir.c_str(), dataPattern.c_str());
    }
    obs_load_all_modules();
    obs_post_load_modules();

    started_ = true;
    lastError_.clear();
    return true;
}

void BroadcastEngine::shutdown() {
    if (!started_) {
        return;
    }
    stopStreaming();
    stopRecording();
    stopReturnFeeds();

    if (videoEncoder_ != nullptr) {
        obs_encoder_release(reinterpret_cast<obs_encoder_t*>(videoEncoder_));
        videoEncoder_ = nullptr;
    }
    if (audioEncoder_ != nullptr) {
        obs_encoder_release(reinterpret_cast<obs_encoder_t*>(audioEncoder_));
        audioEncoder_ = nullptr;
    }
    if (service_ != nullptr) {
        obs_service_release(reinterpret_cast<obs_service_t*>(service_));
        service_ = nullptr;
    }
    if (sceneSource_ != nullptr) {
        obs_set_output_source(kProgramChannel, nullptr);
        obs_source_release(reinterpret_cast<obs_source_t*>(sceneSource_));
        sceneSource_ = nullptr;
        scene_ = nullptr; // la escena muere con su source
    }

    obs_shutdown();
    started_ = false;
}

// ============================================================================
// Escena
// ============================================================================

bool BroadcastEngine::buildScene() {
    if (!started_) {
        setError("buildScene sin startup");
        return false;
    }
    if (scene_ != nullptr) {
        return true;
    }
    obs_scene_t* scene = obs_scene_create("programa");
    if (scene == nullptr) {
        setError("obs_scene_create fallo");
        return false;
    }
    scene_ = reinterpret_cast<obs_scene*>(scene);
    obs_source_t* src = obs_scene_get_source(scene);
    // El canal 0 es lo que se compone y sale al aire.
    obs_set_output_source(kProgramChannel, src);
    // `obs_scene_get_source` devuelve una referencia PRESTADA; se toma una
    // propia para que la escena sobreviva mientras el motor la use.
    sceneSource_ = reinterpret_cast<obs_source*>(obs_source_get_ref(src));
    return true;
}

bool BroadcastEngine::addGuestAudio(int slot) {
    if (scene_ == nullptr) {
        setError("addGuestAudio sin escena");
        return false;
    }
    obs_data_t* settings = obs_data_create();
    obs_data_set_int(settings, "slot", slot);
    const std::string name = "invitado-audio-" + std::to_string(slot);
    // `ba_guest_audio` lo registra el modulo propio `ba-sources`: lee el ring
    // BA.Audio.Guest.<slot> que publica el Proceso B.
    obs_source_t* src = obs_source_create("ba_guest_audio", name.c_str(), settings, nullptr);
    obs_data_release(settings);
    if (src == nullptr) {
        setError("no se pudo crear ba_guest_audio (¿esta cargado ba-sources?)");
        return false;
    }
    obs_scene_add(reinterpret_cast<obs_scene_t*>(scene_), src);
    obs_source_release(src);
    return true;
}

bool BroadcastEngine::addGuestVideo(int slot) {
    if (scene_ == nullptr) {
        setError("addGuestVideo sin escena");
        return false;
    }
    obs_data_t* settings = obs_data_create();
    obs_data_set_int(settings, "slot", slot);
    const std::string name = "invitado-video-" + std::to_string(slot);
    obs_source_t* src = obs_source_create("ba_guest_video", name.c_str(), settings, nullptr);
    obs_data_release(settings);
    if (src == nullptr) {
        setError("no se pudo crear ba_guest_video (¿esta cargado ba-sources?)");
        return false;
    }
    obs_sceneitem_t* item = obs_scene_add(reinterpret_cast<obs_scene_t*>(scene_), src);
    if (item != nullptr) {
        // Los invitados entran escalados al lienzo; el operador los reubica.
        obs_sceneitem_set_bounds_type(item, OBS_BOUNDS_SCALE_INNER);
        struct vec2 bounds;
        vec2_set(&bounds, static_cast<float>(video_.baseWidth) / 2.0f,
                 static_cast<float>(video_.baseHeight) / 2.0f);
        obs_sceneitem_set_bounds(item, &bounds);
    }
    obs_source_release(src);
    return true;
}

// ============================================================================
// Publicacion del retorno hacia el Proceso B (spec §8.5/§8.6)
// ============================================================================

namespace {

/// Callback de VIDEO crudo de libobs: recibe cada frame del programa ya
/// compuesto. Corre en el hilo de render de libobs.
void rawVideoCallback(void* param, struct video_data* frame);

/// Callback de AUDIO crudo: recibe la mezcla final. Corre en el hilo de audio.
void rawAudioCallback(void* param, size_t mix_idx, struct audio_data* data);

} // namespace

bool BroadcastEngine::publishReturnFeeds() {
    if (!started_) {
        setError("publishReturnFeeds sin startup");
        return false;
    }
    if (returnFeedsActive_) {
        return true;
    }

    // --- Ring de audio de retorno (A -> B) ---
    const std::uint32_t pid =
#if defined(_WIN32)
        0u;
#else
        0u;
#endif
    ba::ipc::Status st = ba::ipc::SharedAudioRing::init(
        kReturnAudioName, static_cast<std::uint32_t>(audio_.channels),
        ba::ipc::audio_ring::kDefaultCapacityBlocks, pid, /*generation=*/1u, returnAudio_);
    if (!st.ok) {
        setError("no se pudo crear el ring de retorno de audio");
        return false;
    }

    // --- Canal de video de programa (A -> B) ---
    ba::ipc::SlotConfig vcfg{video_.outputWidth, video_.outputHeight};
    if (!programVideo_.publish(kProgramVideoName, vcfg)) {
        setError("no se pudo publicar el canal de video de programa");
        return false;
    }

    g_returnFeedOwner = this;

    // El retorno al invitado va en BGRA: es lo que el MediaPump del gateway
    // escala y entrega a WebRTC.
    struct video_scale_info vsi = {};
    vsi.format = VIDEO_FORMAT_BGRA;
    vsi.width = static_cast<uint32_t>(video_.outputWidth);
    vsi.height = static_cast<uint32_t>(video_.outputHeight);
    vsi.colorspace = VIDEO_CS_709;
    vsi.range = VIDEO_RANGE_FULL;
    obs_add_raw_video_callback(&vsi, rawVideoCallback, this);

    struct audio_convert_info aci = {};
    aci.format = AUDIO_FORMAT_FLOAT; // planar float, como el resto del proyecto
    aci.samples_per_sec = static_cast<uint32_t>(audio_.sampleRate);
    aci.speakers = (audio_.channels == 1) ? SPEAKERS_MONO : SPEAKERS_STEREO;
    obs_add_raw_audio_callback(0, &aci, rawAudioCallback, this);

    returnFeedsActive_ = true;
    return true;
}

void BroadcastEngine::stopReturnFeeds() {
    if (!returnFeedsActive_) {
        return;
    }
    obs_remove_raw_video_callback(rawVideoCallback, this);
    obs_remove_raw_audio_callback(0, rawAudioCallback, this);
    g_returnFeedOwner = nullptr;
    returnFeedsActive_ = false;
}

namespace {

void rawVideoCallback(void* param, struct video_data* frame) {
    auto* eng = static_cast<BroadcastEngine*>(param);
    if (eng == nullptr || frame == nullptr || frame->data[0] == nullptr) {
        return;
    }
    eng->onRawVideo(frame);
}

void rawAudioCallback(void* param, size_t mix_idx, struct audio_data* data) {
    (void)mix_idx;
    auto* eng = static_cast<BroadcastEngine*>(param);
    if (eng == nullptr || data == nullptr) {
        return;
    }
    eng->onRawAudio(data);
}

} // namespace

void BroadcastEngine::onRawVideo(struct video_data* frame) {
    if (!programVideo_.published() || frame->data[0] == nullptr) {
        return;
    }
    // libobs entrega BGRA empaquetado en el plano 0. `linesize` puede ser mayor
    // que ancho*4 por alineacion, asi que el tamaño util se calcula con el
    // ancho real, no con el linesize.
    const std::size_t bytes = static_cast<std::size_t>(video_.outputWidth) * video_.outputHeight * 4u;
    ba::ipc::FrameMeta meta;
    meta.frameIndex = ++programFrameIndex_;
    meta.qpc = ba::ipc::HealthBlock::nowQpc();
    programVideo_.send(frame->data[0], bytes, meta);
}

void BroadcastEngine::onRawAudio(struct audio_data* data) {
    if (returnAudio_.header() == nullptr || data->frames == 0) {
        return;
    }
    const int ch = audio_.channels;
    const auto frames = static_cast<std::size_t>(data->frames);

    // libobs entrega PLANAR (un buffer por canal) y el ring transporta
    // INTERLEAVED: hay que entrelazar. Si algun plano falta se rellena con
    // silencio en vez de descartar el bloque, para no cortar el retorno.
    const std::size_t needed = frames * static_cast<std::size_t>(ch);
    const std::size_t base = audioAccum_.size();
    audioAccum_.resize(base + needed);
    for (std::size_t f = 0; f < frames; ++f) {
        for (int c = 0; c < ch; ++c) {
            const auto* plane = reinterpret_cast<const float*>(data->data[c]);
            audioAccum_[base + f * static_cast<std::size_t>(ch) + static_cast<std::size_t>(c)] =
                (plane != nullptr) ? plane[f] : 0.0f;
        }
    }

    // El ring solo acepta bloques exactos de 480 frames (10 ms a 48 kHz).
    const std::size_t blockFloats = returnAudio_.blockFloats();
    std::size_t pos = 0;
    while (audioAccum_.size() - pos >= blockFloats) {
        returnAudio_.push(audioAccum_.data() + pos, ba::ipc::HealthBlock::nowQpc());
        pos += blockFloats;
    }
    if (pos > 0) {
        audioAccum_.erase(audioAccum_.begin(), audioAccum_.begin() + static_cast<std::ptrdiff_t>(pos));
    }
}

// ============================================================================
// Codificadores y salidas
// ============================================================================

std::string BroadcastEngine::pickVideoEncoder(bool preferHardware) const {
    if (preferHardware) {
        // Orden de preferencia: NVENC (NVIDIA), QSV (Intel), AMF (AMD). Se
        // consulta que exista de verdad antes de elegirlo: pedir un encoder
        // ausente hace fallar la salida entera en el peor momento.
        static const char* kHw[] = {"obs_nvenc_h264_tex", "ffmpeg_nvenc", "obs_qsv11_v2", "h264_texture_amf"};
        for (const char* id : kHw) {
            const char* name = obs_encoder_get_display_name(id);
            if (name != nullptr) {
                return id;
            }
        }
    }
    return "obs_x264"; // siempre disponible: el plugin obs-x264 viaja en el bundle
}

bool BroadcastEngine::ensureEncoders(const EncoderConfig& enc) {
    if (videoEncoder_ != nullptr && audioEncoder_ != nullptr) {
        return true;
    }

    if (videoEncoder_ == nullptr) {
        const std::string encId = pickVideoEncoder(enc.preferHardware);
        obs_data_t* vs = obs_data_create();
        obs_data_set_int(vs, "bitrate", enc.videoBitrateKbps);
        obs_data_set_string(vs, "rate_control", enc.rateControl.c_str());
        obs_data_set_string(vs, "preset", enc.preset.c_str());
        // keyint en SEGUNDOS: los CDN piden 2 s y x264 lo toma asi.
        obs_data_set_int(vs, "keyint_sec", enc.keyintSec);
        obs_encoder_t* ve = obs_video_encoder_create(encId.c_str(), "video", vs, nullptr);
        obs_data_release(vs);
        if (ve == nullptr) {
            setError("no se pudo crear el codificador de video: " + encId);
            return false;
        }
        obs_encoder_set_video(ve, obs_get_video());
        videoEncoder_ = reinterpret_cast<obs_encoder*>(ve);
    }

    if (audioEncoder_ == nullptr) {
        obs_data_t* as = obs_data_create();
        obs_data_set_int(as, "bitrate", enc.audioBitrateKbps);
        obs_encoder_t* ae = obs_audio_encoder_create("ffmpeg_aac", "audio", as, 0, nullptr);
        obs_data_release(as);
        if (ae == nullptr) {
            setError("no se pudo crear el codificador de audio (ffmpeg_aac)");
            return false;
        }
        obs_encoder_set_audio(ae, obs_get_audio());
        audioEncoder_ = reinterpret_cast<obs_encoder*>(ae);
    }
    return true;
}

bool BroadcastEngine::startStreaming(const StreamConfig& stream, const EncoderConfig& enc) {
    if (!started_) {
        setError("startStreaming sin startup");
        return false;
    }
    if (streamOutput_ != nullptr && obs_output_active(reinterpret_cast<obs_output_t*>(streamOutput_))) {
        return true;
    }
    if (stream.server.empty()) {
        setError("startStreaming sin servidor");
        return false;
    }
    if (!ensureEncoders(enc)) {
        return false;
    }

    // Servicio RTMP personalizado: servidor + clave los pone el operador (la
    // clave deberia salir de la boveda de secretos, nunca del codigo).
    obs_data_t* ss = obs_data_create();
    obs_data_set_string(ss, "server", stream.server.c_str());
    obs_data_set_string(ss, "key", stream.key.c_str());
    obs_service_t* svc = obs_service_create("rtmp_custom", "servicio", ss, nullptr);
    obs_data_release(ss);
    if (svc == nullptr) {
        setError("obs_service_create fallo");
        return false;
    }
    if (service_ != nullptr) {
        obs_service_release(reinterpret_cast<obs_service_t*>(service_));
    }
    service_ = reinterpret_cast<obs_service*>(svc);

    obs_output_t* out = obs_output_create("rtmp_output", "transmision", nullptr, nullptr);
    if (out == nullptr) {
        setError("obs_output_create(rtmp_output) fallo");
        return false;
    }
    obs_output_set_service(out, svc);
    obs_output_set_video_encoder(out, reinterpret_cast<obs_encoder_t*>(videoEncoder_));
    obs_output_set_audio_encoder(out, reinterpret_cast<obs_encoder_t*>(audioEncoder_), 0);

    if (!obs_output_start(out)) {
        const char* err = obs_output_get_last_error(out);
        setError(std::string("no se pudo iniciar la transmision") +
                 (err != nullptr && *err != '\0' ? std::string(": ") + err : std::string()));
        obs_output_release(out);
        return false;
    }
    if (streamOutput_ != nullptr) {
        obs_output_release(reinterpret_cast<obs_output_t*>(streamOutput_));
    }
    streamOutput_ = reinterpret_cast<obs_output*>(out);
    return true;
}

void BroadcastEngine::stopStreaming() {
    if (streamOutput_ == nullptr) {
        return;
    }
    auto* out = reinterpret_cast<obs_output_t*>(streamOutput_);
    if (obs_output_active(out)) {
        obs_output_stop(out);
    }
    obs_output_release(out);
    streamOutput_ = nullptr;
}

bool BroadcastEngine::startRecording(const RecordConfig& rec, const EncoderConfig& enc) {
    if (!started_) {
        setError("startRecording sin startup");
        return false;
    }
    if (recordOutput_ != nullptr && obs_output_active(reinterpret_cast<obs_output_t*>(recordOutput_))) {
        return true;
    }
    if (!ensureEncoders(enc)) {
        return false;
    }

    // Nombre por marca de tiempo, dentro de Recordings/ (layout §5.1).
    const auto now = std::chrono::system_clock::now();
    const auto secs = std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();
    const std::string path = rec.directory + "/ba-" + std::to_string(secs) + "." +
                             (rec.container.empty() ? std::string("mkv") : rec.container);

    obs_data_t* os = obs_data_create();
    obs_data_set_string(os, "path", path.c_str());
    // `ffmpeg_muxer` remuxa sin recodificar: comparte los mismos encoders que
    // la transmision, de modo que grabar no cuesta un segundo encode.
    obs_output_t* out = obs_output_create("ffmpeg_muxer", "grabacion", os, nullptr);
    obs_data_release(os);
    if (out == nullptr) {
        setError("obs_output_create(ffmpeg_muxer) fallo");
        return false;
    }
    obs_output_set_video_encoder(out, reinterpret_cast<obs_encoder_t*>(videoEncoder_));
    obs_output_set_audio_encoder(out, reinterpret_cast<obs_encoder_t*>(audioEncoder_), 0);

    if (!obs_output_start(out)) {
        const char* err = obs_output_get_last_error(out);
        setError(std::string("no se pudo iniciar la grabacion") +
                 (err != nullptr && *err != '\0' ? std::string(": ") + err : std::string()));
        obs_output_release(out);
        return false;
    }
    if (recordOutput_ != nullptr) {
        obs_output_release(reinterpret_cast<obs_output_t*>(recordOutput_));
    }
    recordOutput_ = reinterpret_cast<obs_output*>(out);
    return true;
}

void BroadcastEngine::stopRecording() {
    if (recordOutput_ == nullptr) {
        return;
    }
    auto* out = reinterpret_cast<obs_output_t*>(recordOutput_);
    if (obs_output_active(out)) {
        obs_output_stop(out);
    }
    obs_output_release(out);
    recordOutput_ = nullptr;
}

// ============================================================================
// Estado
// ============================================================================

EngineStatus BroadcastEngine::status() const {
    EngineStatus st;
    st.lastError = lastError_;
    if (!started_) {
        return st;
    }
    st.videoActive = obs_get_video() != nullptr;
    st.audioActive = obs_get_audio() != nullptr;
    st.fps = obs_get_active_fps();

    auto* stream = reinterpret_cast<obs_output_t*>(streamOutput_);
    if (stream != nullptr) {
        st.streaming = obs_output_active(stream);
        st.droppedFrames = obs_output_get_frames_dropped(stream);
        st.totalFrames = obs_output_get_total_frames(stream);
        st.bytesSent = static_cast<std::uint64_t>(obs_output_get_total_bytes(stream));
    }
    auto* record = reinterpret_cast<obs_output_t*>(recordOutput_);
    if (record != nullptr) {
        st.recording = obs_output_active(record);
    }
    return st;
}

} // namespace ba::app
