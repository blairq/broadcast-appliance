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
//   4. carga de modulos       -> SIN esto no existen encoders ni salidas
//                                (solo los del bundle: ver loadBundleModules)
//   5. obs_post_load_modules  -> los plugins terminan de inicializarse
// ============================================================================

#include "BroadcastEngine.hpp"
#include "ChatOverlaySource.hpp"
#include "graficos/GraficoSource.hpp"
#if defined(BA_HAVE_NDI)
#include "NdiSource.hpp"
#endif

#include <obs.h>

#include "WindowingPlatform.hpp"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <unistd.h>
#endif

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#include "ipc/HealthBlock.hpp" // nowQpc() para los timestamps del IPC

namespace ba::app {

namespace {

std::uint32_t currentPid() {
#if defined(_WIN32)
    return static_cast<std::uint32_t>(GetCurrentProcessId());
#else
    return static_cast<std::uint32_t>(::getpid());
#endif
}

/// Nombres de los canales IPC hacia el Proceso B (spec §4.3/§8.5).
constexpr const wchar_t* kReturnAudioName = L"BA.Audio.ContentForReturn";
constexpr const wchar_t* kProgramVideoName = L"BA.Program";

/// Carga los modulos de `binDir` y SOLO esos.
///
/// No se usa `obs_load_all_modules()` porque recorre tambien las rutas por
/// defecto que libobs agrega en `obs_startup` (en Linux, entre otras, el
/// OBS_INSTALL_PREFIX con el que se compilo libobs: `extern/obs-linux` en la
/// maquina de build). En la maquina de build eso cargaba cada plugin dos veces
/// ("Duplicate library?") y en cualquier otra podria levantar plugins ajenos
/// al bundle. Aca el conjunto de modulos es exactamente el del bundle.
///
/// :param binDir: directorio con los `.so`/`.dll` de los plugins.
/// :param dataDir: raiz de los datos de los plugins; cada modulo recibe
///                 `<dataDir>/<nombre>` (locale, efectos).
/// :returns: cantidad de modulos cargados.
int loadBundleModules(const std::string& binDir, const std::string& dataDir) {
#if defined(_WIN32)
    const char* ext = ".dll";
#else
    const char* ext = ".so";
#endif
    std::error_code ec;
    std::vector<std::filesystem::path> files;
    for (const auto& entry : std::filesystem::directory_iterator(binDir, ec)) {
        if (entry.is_regular_file(ec) && entry.path().extension() == ext) {
            files.push_back(entry.path());
        }
    }
    std::sort(files.begin(), files.end()); // orden estable entre corridas

    int loaded = 0;
    for (const auto& file : files) {
        const std::string name = file.stem().string();
        const std::string data = dataDir + "/" + name;
        obs_module_t* module = nullptr;
        const int rc = obs_open_module(&module, file.string().c_str(), data.c_str());
        if (rc != MODULE_SUCCESS) {
            // MODULE_MISSING_EXPORTS: una DLL de soporte que no es plugin.
            if (rc != MODULE_MISSING_EXPORTS) {
                blog(LOG_WARNING, "ba-engine: no se pudo abrir el modulo '%s' (%d)", file.string().c_str(),
                     rc);
            }
            continue;
        }
        if (obs_init_module(module)) {
            ++loaded;
        } else {
            // Los codificadores por hardware no inician sin su GPU (NVENC sin
            // NVIDIA, QSV sin Intel, AMF sin AMD): es lo esperado, no una falla.
            const bool hw = name.find("nvenc") != std::string::npos ||
                            name.find("qsv") != std::string::npos || name.find("amf") != std::string::npos;
            if (hw) {
                blog(LOG_INFO, "ba-engine: '%s' no disponible (no hay GPU compatible)", name.c_str());
            } else {
                blog(LOG_WARNING, "ba-engine: el modulo '%s' fallo al inicializarse", name.c_str());
            }
        }
    }
    return loaded;
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

    // 0) Plataforma de ventanas (Linux): libobs arranca por defecto en modo
    //    X11 y en obs_startup abre el display X (atajos de teclado). En una
    //    sesion Wayland eso funcionaba de rebote via XWayland; sin display,
    //    obs_startup fallaba sin explicacion.
    if (!selectWindowingPlatform()) {
        return false;
    }

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
    //     libobs concatena la ruta y el archivo tal cual: tiene que terminar en
    //     '/' (sin ella buscaba ".../libobsdefault.effect" y solo andaba en la
    //     maquina que compilo libobs, por la ruta compilada).
    if (!paths_.obsDataDir.empty()) {
        std::string dataDir = paths_.obsDataDir;
        if (dataDir.back() != '/' && dataDir.back() != '\\') dataDir += '/';
        obs_add_data_path(dataDir.c_str());
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

    // 4-5) Modulos. Sin esto no hay codificadores ni salidas: es el paso que
    //      faltaba en el arranque anterior del Proceso A. Con un directorio
    //      de modulos explicito se cargan solo esos; sin el, se delega en las
    //      rutas por defecto de libobs (una instalacion de OBS del sistema).
    if (!paths_.moduleBinDir.empty()) {
        const std::string dataDir = paths_.moduleDataDir.empty() ? paths_.moduleBinDir : paths_.moduleDataDir;
        if (loadBundleModules(paths_.moduleBinDir, dataDir) == 0) {
            // Sin modulos no hay encoders ni salidas: el motor no sirve y hay
            // que decirlo aca, no dejar que falle mas tarde sin motivo claro.
            setError("no se cargo ningun modulo desde " + paths_.moduleBinDir);
            obs_shutdown();
            return false;
        }
    } else {
        obs_load_all_modules();
    }
    obs_post_load_modules();
    // Fuentes propias de la app (no son plugins): el chat en vivo.
    chatoverlay::registerSourceType();
    graficos::registerSourceType();
#if defined(BA_HAVE_NDI)
    ndi::registerSourceType(); // entrada NDI(R) abierta (libNoIDea)
#endif

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
    disablePreview();

    releaseVideoEncoder();
    encoderCandidates_.clear();
    encoderIndex_ = 0;
    if (audioEncoder_ != nullptr) {
        obs_encoder_release(reinterpret_cast<obs_encoder_t*>(audioEncoder_));
        audioEncoder_ = nullptr;
    }
    if (service_ != nullptr) {
        obs_service_release(reinterpret_cast<obs_service_t*>(service_));
        service_ = nullptr;
    }
    releaseScenes(); // transicion, escenas, fuentes y vumetros

    obs_shutdown();
    started_ = false;
    releaseWindowingPlatform();
}

bool BroadcastEngine::selectWindowingPlatform() {
    std::string err;
    if (!ba::app::selectWindowingPlatform(nixDisplay_, nixWayland_, err)) {
        setError(err);
        return false;
    }
    return true;
}

void BroadcastEngine::releaseWindowingPlatform() {
    ba::app::releaseWindowingPlatform(nixDisplay_, nixWayland_);
    nixDisplay_ = nullptr;
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
    // El PID real identifica a ESTE escritor: si A se reinicia, B lo nota
    // (SharedAudioRing::stale) y reabre el ring en vez de quedar mudo.
    const std::uint32_t pid = currentPid();
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
    const std::size_t rowBytes = static_cast<std::size_t>(video_.outputWidth) * 4u;
    const std::size_t bytes = rowBytes * static_cast<std::size_t>(video_.outputHeight);
    const std::uint8_t* pixels = frame->data[0];
    // Con relleno por fila (linesize > ancho*4) hay que empaquetar: mandar el
    // buffer tal cual corria cada fila y B recibia la imagen inclinada.
    if (static_cast<std::size_t>(frame->linesize[0]) != rowBytes) {
        programPacked_.resize(bytes);
        for (int y = 0; y < video_.outputHeight; ++y) {
            std::memcpy(programPacked_.data() + static_cast<std::size_t>(y) * rowBytes,
                        frame->data[0] + static_cast<std::size_t>(y) * frame->linesize[0], rowBytes);
        }
        pixels = programPacked_.data();
    }
    ba::ipc::FrameMeta meta;
    meta.frameIndex = ++programFrameIndex_;
    meta.qpc = ba::ipc::HealthBlock::nowQpc();
    programVideo_.send(pixels, bytes, meta);
}

// ============================================================================
// Vista previa y medidor (UI)
// ============================================================================

namespace {

void previewVideoCallback(void* param, struct video_data* frame) {
    auto* eng = static_cast<BroadcastEngine*>(param);
    if (eng != nullptr && frame != nullptr && frame->data[0] != nullptr) {
        eng->onPreviewVideo(frame);
    }
}

void meterAudioCallback(void* param, size_t, struct audio_data* data) {
    auto* eng = static_cast<BroadcastEngine*>(param);
    if (eng != nullptr && data != nullptr) {
        eng->onMeterAudio(data);
    }
}

/// Max atomico sin lock (el hilo de audio publica, la UI lee y resetea).
void atomicMax(std::atomic<float>& a, float v) {
    float cur = a.load(std::memory_order_relaxed);
    while (v > cur && !a.compare_exchange_weak(cur, v, std::memory_order_relaxed)) {
    }
}

float toDb(float lin) { return lin <= 0.00001f ? -100.0f : 20.0f * std::log10(lin); }

} // namespace

bool BroadcastEngine::enablePreview(int width, int height) {
    if (!started_ || width <= 0 || height <= 0) {
        setError("enablePreview sin startup o con tamaño invalido");
        return false;
    }
    if (previewActive_) {
        return true;
    }
    {
        std::lock_guard<std::mutex> lock(previewLock_);
        previewW_ = width;
        previewH_ = height;
        previewBuf_.assign(static_cast<std::size_t>(width) * height * 4u, 0);
    }
    struct video_scale_info vsi = {};
    vsi.format = VIDEO_FORMAT_BGRA;
    vsi.width = static_cast<uint32_t>(width);
    vsi.height = static_cast<uint32_t>(height);
    vsi.colorspace = VIDEO_CS_709;
    vsi.range = VIDEO_RANGE_FULL;
    obs_add_raw_video_callback(&vsi, previewVideoCallback, this);

    struct audio_convert_info aci = {};
    aci.format = AUDIO_FORMAT_FLOAT_PLANAR;
    aci.samples_per_sec = static_cast<uint32_t>(audio_.sampleRate);
    aci.speakers = SPEAKERS_STEREO;
    obs_add_raw_audio_callback(0, &aci, meterAudioCallback, this);
    previewActive_ = true;
    return true;
}

void BroadcastEngine::disablePreview() {
    if (!previewActive_) {
        return;
    }
    obs_remove_raw_video_callback(previewVideoCallback, this);
    obs_remove_raw_audio_callback(0, meterAudioCallback, this);
    previewActive_ = false;
}

void BroadcastEngine::onPreviewVideo(struct video_data* frame) {
    std::lock_guard<std::mutex> lock(previewLock_);
    const std::size_t rowBytes = static_cast<std::size_t>(previewW_) * 4u;
    if (previewBuf_.size() < rowBytes * static_cast<std::size_t>(previewH_)) {
        return;
    }
    for (int y = 0; y < previewH_; ++y) {
        std::memcpy(previewBuf_.data() + static_cast<std::size_t>(y) * rowBytes,
                    frame->data[0] + static_cast<std::size_t>(y) * frame->linesize[0], rowBytes);
    }
    ++previewSerial_;
}

bool BroadcastEngine::latestPreview(std::vector<std::uint8_t>& dst, int& width, int& height,
                                    std::uint64_t& serial) const {
    std::lock_guard<std::mutex> lock(previewLock_);
    if (previewSerial_ == serial || previewBuf_.empty()) {
        return false;
    }
    dst = previewBuf_;
    width = previewW_;
    height = previewH_;
    serial = previewSerial_;
    return true;
}

void BroadcastEngine::onMeterAudio(struct audio_data* data) {
    float pl = 0.0f;
    float pr = 0.0f;
    const auto* l = reinterpret_cast<const float*>(data->data[0]);
    const auto* r = reinterpret_cast<const float*>(data->data[1]);
    for (uint32_t i = 0; i < data->frames; ++i) {
        if (l != nullptr) {
            pl = std::max(pl, std::fabs(l[i]));
        }
        if (r != nullptr) {
            pr = std::max(pr, std::fabs(r[i]));
        }
    }
    atomicMax(peakL_, pl);
    atomicMax(peakR_, pr);
}

void BroadcastEngine::programPeaksDb(float& left, float& right) {
    left = toDb(peakL_.exchange(0.0f, std::memory_order_relaxed));
    right = toDb(peakR_.exchange(0.0f, std::memory_order_relaxed));
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

namespace {

/// Ids de libobs por familia de codificador H.264 (el primero es el preferido
/// dentro de la familia: las variantes "_tex" codifican desde la textura de la
/// GPU sin bajar el frame a RAM).
const std::vector<std::string>& encoderFamily(const std::string& alias) {
    static const std::vector<std::string> kNvenc = {"obs_nvenc_h264_tex", "ffmpeg_nvenc", "jim_nvenc"};
    static const std::vector<std::string> kQsv = {"obs_qsv11_v2", "obs_qsv11"};
    static const std::vector<std::string> kAmf = {"h264_texture_amf", "amd_amf_h264"};
    static const std::vector<std::string> kVaapi = {"ffmpeg_vaapi_tex", "ffmpeg_vaapi"};
    static const std::vector<std::string> kVt = {"com.apple.videotoolbox.videoencoder.ave.avc",
                                                 "com.apple.videotoolbox.videoencoder.h264.gva",
                                                 "com.apple.videotoolbox.videoencoder.h264"};
    static const std::vector<std::string> kX264 = {"obs_x264"};
    static const std::vector<std::string> kNone = {};
    if (alias == "nvenc") return kNvenc;
    if (alias == "qsv") return kQsv;
    if (alias == "amf") return kAmf;
    if (alias == "vaapi") return kVaapi;
    if (alias == "videotoolbox") return kVt;
    if (alias == "x264" || alias == "software") return kX264;
    return kNone;
}

/// Orden de hardware de "auto" por plataforma (plan multiplataforma §3.2).
const std::vector<std::string>& autoHardwareOrder() {
#if defined(_WIN32)
    static const std::vector<std::string> kOrder = {"nvenc", "qsv", "amf"};
#elif defined(__APPLE__)
    static const std::vector<std::string> kOrder = {"videotoolbox"};
#else
    static const std::vector<std::string> kOrder = {"nvenc", "vaapi"};
#endif
    return kOrder;
}

bool encoderRegistered(const std::string& id) { return obs_encoder_get_display_name(id.c_str()) != nullptr; }

} // namespace

std::vector<std::string> BroadcastEngine::videoEncoderCandidates(const EncoderConfig& enc) const {
    std::vector<std::string> wanted;
    const std::string& choice = enc.videoEncoder.empty() ? std::string("auto") : enc.videoEncoder;
    if (choice == "auto") {
        if (enc.preferHardware) {
            for (const auto& alias : autoHardwareOrder()) {
                const auto& fam = encoderFamily(alias);
                wanted.insert(wanted.end(), fam.begin(), fam.end());
            }
        }
    } else if (!encoderFamily(choice).empty()) {
        const auto& fam = encoderFamily(choice);
        wanted.insert(wanted.end(), fam.begin(), fam.end());
    } else {
        wanted.push_back(choice); // id literal de libobs
    }
    wanted.push_back("obs_x264"); // red de seguridad: viaja siempre en el bundle

    // Solo los registrados (un modulo de hardware sin driver no se registra),
    // sin repetidos y en orden.
    std::vector<std::string> out;
    for (const auto& id : wanted) {
        if (encoderRegistered(id) && std::find(out.begin(), out.end(), id) == out.end()) {
            out.push_back(id);
        }
    }
    return out;
}

void BroadcastEngine::releaseVideoEncoder() {
    if (videoEncoder_ != nullptr) {
        obs_encoder_release(reinterpret_cast<obs_encoder_t*>(videoEncoder_));
        videoEncoder_ = nullptr;
    }
    videoEncoderId_.clear();
}

bool BroadcastEngine::ensureEncoders(const EncoderConfig& enc) {
    if (videoEncoder_ != nullptr && audioEncoder_ != nullptr) {
        return true;
    }

    if (videoEncoder_ == nullptr) {
        if (encoderCandidates_.empty()) {
            encoderCandidates_ = videoEncoderCandidates(enc);
            encoderIndex_ = 0;
        }
        // Se prueban en orden los que ni siquiera se pueden CREAR (p. ej. un
        // id literal mal escrito); los que fallan al ARRANCAR los cubre
        // startOutputWithFallback.
        while (encoderIndex_ < encoderCandidates_.size() && videoEncoder_ == nullptr) {
            const std::string& encId = encoderCandidates_[encoderIndex_];
            obs_data_t* vs = obs_data_create();
            obs_data_set_int(vs, "bitrate", enc.videoBitrateKbps);
            obs_data_set_string(vs, "rate_control", enc.rateControl.c_str());
            // keyint en SEGUNDOS: los CDN piden 2 s.
            obs_data_set_int(vs, "keyint_sec", enc.keyintSec);
            if (encId == "obs_x264") {
                obs_data_set_string(vs, "preset", enc.preset.c_str());
            }
            obs_encoder_t* ve = obs_video_encoder_create(encId.c_str(), "video", vs, nullptr);
            obs_data_release(vs);
            if (ve == nullptr) {
                blog(LOG_WARNING, "ba-engine: no se pudo crear el codificador '%s'; se prueba el siguiente",
                     encId.c_str());
                ++encoderIndex_;
                continue;
            }
            obs_encoder_set_video(ve, obs_get_video());
            videoEncoder_ = reinterpret_cast<obs_encoder*>(ve);
            videoEncoderId_ = encId;
            blog(LOG_INFO, "ba-engine: codificador de video: %s", encId.c_str());
        }
        if (videoEncoder_ == nullptr) {
            setError("no hay codificador de video utilizable");
            return false;
        }
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

bool BroadcastEngine::startOutputWithFallback(obs_output* outRaw, const EncoderConfig& enc) {
    auto* out = reinterpret_cast<obs_output_t*>(outRaw);
    for (;;) {
        obs_output_set_video_encoder(out, reinterpret_cast<obs_encoder_t*>(videoEncoder_));
        obs_output_set_audio_encoder(out, reinterpret_cast<obs_encoder_t*>(audioEncoder_), 0);
        if (obs_output_start(out)) {
            return true;
        }
        const char* err = obs_output_get_last_error(out);
        const std::string why = (err != nullptr && *err != '\0') ? err : "sin detalle";
        // Solo se cambia de codificador si ninguna OTRA salida lo usa: cambiarlo
        // en caliente cortaria la emision o la grabacion que ya corre.
        const bool otherActive = (streamOutput_ != nullptr && streamOutput_ != outRaw &&
                                  obs_output_active(reinterpret_cast<obs_output_t*>(streamOutput_))) ||
                                 (recordOutput_ != nullptr && recordOutput_ != outRaw &&
                                  obs_output_active(reinterpret_cast<obs_output_t*>(recordOutput_)));
        if (otherActive || encoderIndex_ + 1 >= encoderCandidates_.size()) {
            setError(why);
            return false;
        }
        blog(LOG_WARNING, "ba-engine: la salida no arranco con '%s' (%s); se prueba el siguiente codificador",
             videoEncoderId_.c_str(), why.c_str());
        releaseVideoEncoder();
        ++encoderIndex_;
        if (!ensureEncoders(enc)) {
            return false;
        }
    }
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
    if (!startOutputWithFallback(reinterpret_cast<obs_output*>(out), enc)) {
        setError("no se pudo iniciar la transmision: " + lastError_);
        obs_output_release(out);
        return false;
    }
    if (streamOutput_ != nullptr) {
        obs_output_release(reinterpret_cast<obs_output_t*>(streamOutput_));
    }
    streamOutput_ = reinterpret_cast<obs_output*>(out);
    watchOutput(streamOutput_);
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
    if (!startOutputWithFallback(reinterpret_cast<obs_output*>(out), enc)) {
        setError("no se pudo iniciar la grabacion: " + lastError_);
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
// Grabacion POR ENTRADA
// ============================================================================
// Ver el comentario del header. Lo unico que conviene repetir aca: cada entrada
// que se graba es un ENCODE MAS, con su propia vista, su codificador y su
// salida. Nada de esto se comparte con el programa.

struct BroadcastEngine::SourceRec {
    std::string source;
    std::string path;
    obs_view_t* view = nullptr;
    video_t* video = nullptr;
    obs_encoder_t* venc = nullptr;
    obs_encoder_t* aenc = nullptr;
    obs_output_t* out = nullptr;
    int pista = -1;           ///< pista de audio tomada (1..5), -1 = sin audio
    std::uint32_t mixersAntes = 0; ///< para devolverle a la fuente lo que tenia

    ~SourceRec() {
        // El orden importa: primero se para la salida, despues se sueltan los
        // codificadores y por ultimo la vista. Al reves, libobs se queda con la
        // vista viva y el proximo arranque no consigue GPU.
        if (out != nullptr) {
            if (obs_output_active(out)) obs_output_stop(out);
            obs_output_release(out);
        }
        if (venc != nullptr) obs_encoder_release(venc);
        if (aenc != nullptr) obs_encoder_release(aenc);
        if (view != nullptr) {
            obs_view_remove(view);
            obs_view_set_source(view, 0, nullptr);
            obs_view_destroy(view);
        }
    }
};

bool BroadcastEngine::startSourceRecording(const std::string& source, const std::string& dir,
                                           const EncoderConfig& enc) {
    if (!started_) {
        setError("startSourceRecording sin startup");
        return false;
    }
    if (sourceRecs_.count(source) > 0) {
        setError("ya se esta grabando \"" + source + "\" por separado");
        return false;
    }
    if (static_cast<int>(sourceRecs_.size()) >= kMaxIndividuales) {
        setError("ya hay " + std::to_string(kMaxIndividuales) +
                 " entradas grabandose por separado, que es el tope");
        return false;
    }
    obs_source_t* src = reinterpret_cast<obs_source_t*>(ownedSource(source));
    if (src == nullptr) {
        setError("no existe la fuente \"" + source + "\"");
        return false;
    }

    auto rec = std::make_unique<SourceRec>();
    rec->source = source;

    // 1) La vista: la fuente sola, sin escena ni nada mas. De aca sale un
    //    `video_t` propio, que es lo que hace que esto sea la ENTRADA y no el
    //    programa.
    rec->view = obs_view_create();
    if (rec->view == nullptr) {
        setError("no se pudo crear la vista de la entrada");
        return false;
    }
    obs_view_set_source(rec->view, 0, src);

    struct obs_video_info ovi;
    if (!obs_get_video_info(&ovi)) {
        setError("no se pudo leer la configuracion de video");
        return false;
    }
    rec->video = obs_view_add2(rec->view, &ovi);
    if (rec->video == nullptr) {
        setError("no se pudo abrir el render de la entrada (GPU sin lugar)");
        return false;
    }

    // 2) El codificador de video, propio. Se usa x264 y no el de la GPU: los
    //    codificadores por hardware tienen dos o tres sesiones y hay que
    //    dejarselas al programa, que es el que sale al aire.
    obs_data_t* vs = obs_data_create();
    obs_data_set_int(vs, "bitrate", enc.videoBitrateKbps);
    obs_data_set_string(vs, "rate_control", enc.rateControl.c_str());
    obs_data_set_int(vs, "keyint_sec", enc.keyintSec);
    obs_data_set_string(vs, "preset", enc.preset.c_str());
    rec->venc = obs_video_encoder_create("obs_x264", ("venc-" + source).c_str(), vs, nullptr);
    obs_data_release(vs);
    if (rec->venc == nullptr) {
        setError("no se pudo crear el codificador de \"" + source + "\"");
        return false;
    }
    obs_encoder_set_video(rec->venc, rec->video);

    // 3) El audio. SIEMPRE hay que ponerle uno: `ffmpeg_muxer` es una salida
    //    audio+video y sin codificador de audio no arranca —y no dice por que,
    //    devuelve false con el mensaje vacio, que costo un rato encontrar—.
    //
    //    Si la fuente tiene audio propio y queda pista libre, graba EL SUYO:
    //    libobs mezcla en 6 pistas, la 0 es la del programa, quedan 5. Si no
    //    (la fuente no tiene audio, o ya se usaron las cinco), graba la del
    //    PROGRAMA, que igual sirve de referencia para sincronizar al editar.
    //    Cual de las dos quedo se informa; adivinarlo despues, mirando el
    //    archivo, es imposible.
    if ((obs_source_get_output_flags(src) & OBS_SOURCE_AUDIO) != 0) {
        bool usada[6] = {true, false, false, false, false, false}; // 0 = programa
        for (const auto& [n, r] : sourceRecs_)
            if (r->pista > 0 && r->pista < 6) usada[r->pista] = true;
        for (int p = 1; p < 6 && rec->pista < 0; ++p)
            if (!usada[p]) rec->pista = p;

        if (rec->pista > 0) {
            // La fuente pasa a sonar TAMBIEN en su pista: se le suma, no se le
            // saca la del programa, o dejaria de escucharse al aire.
            rec->mixersAntes = obs_source_get_audio_mixers(src);
            obs_source_set_audio_mixers(src, rec->mixersAntes | (1u << rec->pista));
        }
    }
    {
        const size_t mezcla = rec->pista > 0 ? static_cast<size_t>(rec->pista) : 0;
        obs_data_t* as = obs_data_create();
        obs_data_set_int(as, "bitrate", enc.audioBitrateKbps);
        rec->aenc = obs_audio_encoder_create("ffmpeg_aac", ("aenc-" + source).c_str(), as, mezcla,
                                             nullptr);
        obs_data_release(as);
        if (rec->aenc == nullptr) {
            setError("no se pudo crear el codificador de audio de \"" + source + "\"");
            return false;
        }
        obs_encoder_set_audio(rec->aenc, obs_get_audio());
    }

    // 4) La salida a archivo. Nombre con la fuente adentro: despues hay que
    //    encontrarlo entre veinte archivos y "ba-1758546202.mkv" no ayuda.
    std::string limpio;
    for (const char c : source)
        limpio += (std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '-' || c == '_') ? c : '-';
    const auto secs = std::chrono::duration_cast<std::chrono::seconds>(
                          std::chrono::system_clock::now().time_since_epoch())
                          .count();
    rec->path = dir + "/entrada-" + limpio + "-" + std::to_string(secs) + ".mkv";

    obs_data_t* os = obs_data_create();
    obs_data_set_string(os, "path", rec->path.c_str());
    rec->out = obs_output_create("ffmpeg_muxer", ("rec-" + source).c_str(), os, nullptr);
    obs_data_release(os);
    if (rec->out == nullptr) {
        setError("no se pudo crear la salida de \"" + source + "\"");
        return false;
    }
    obs_output_set_video_encoder(rec->out, rec->venc);
    obs_output_set_audio_encoder(rec->out, rec->aenc, 0);
    if (!obs_output_start(rec->out)) {
        const char* e = obs_output_get_last_error(rec->out);
        setError("no arranco la grabacion de \"" + source + "\"" +
                 (e != nullptr && *e != '\0' ? std::string(": ") + e : std::string()));
        return false;
    }

    sourceRecs_[source] = std::move(rec);
    return true;
}

bool BroadcastEngine::stopSourceRecording(const std::string& source) {
    const auto it = sourceRecs_.find(source);
    if (it == sourceRecs_.end()) {
        setError("no se estaba grabando \"" + source + "\" por separado");
        return false;
    }
    // Se le devuelve a la fuente las pistas que tenia: si no, seguiria sonando
    // en una pista que ya no graba nadie.
    if (it->second->pista > 0) {
        obs_source_t* src = reinterpret_cast<obs_source_t*>(ownedSource(source));
        if (src != nullptr) obs_source_set_audio_mixers(src, it->second->mixersAntes);
    }
    sourceRecs_.erase(it); // el destructor para y libera, en orden
    return true;
}

void BroadcastEngine::stopAllSourceRecordings() {
    while (!sourceRecs_.empty()) stopSourceRecording(sourceRecs_.begin()->first);
}

std::vector<BroadcastEngine::GrabacionIndividual> BroadcastEngine::sourceRecordings() const {
    std::vector<GrabacionIndividual> out;
    out.reserve(sourceRecs_.size());
    for (const auto& [n, r] : sourceRecs_)
        out.push_back(GrabacionIndividual{n, r->path, r->pista > 0,
                                          r->out != nullptr && obs_output_active(r->out)});
    return out;
}

// ============================================================================
// Estado
// ============================================================================

// --- Estado de la salida: por que se corto ----------------------------------
// libobs avisa por señales, en el hilo que se le ocurra. Aca solo se anota que
// paso, en castellano y con la causa: el operador necesita saber si esperar
// (se cayo la red y esta reintentando) o hacer algo (la clave es incorrecta).

namespace {

/// Codigos de obs_output (OBS_OUTPUT_*) en palabras que sirvan.
const char* motivoDeCorte(int code) {
    switch (code) {
        case 0: return ""; // OBS_OUTPUT_SUCCESS: cierre normal
        case -1: return "No se pudo escribir en el destino (ruta o permisos)";
        case -2: return "No se pudo conectar con el servidor";
        case -3: return "El servidor rechazó la transmisión (¿la clave es correcta?)";
        case -4: return "Error en la salida";
        case -5: return "Se cortó la conexión con el servidor";
        case -6: return "El servidor no soporta este formato";
        case -7: return "No queda espacio en el disco";
        case -8: return "El codificador falló";
        default: return "La transmisión se cortó";
    }
}

} // namespace

void BroadcastEngine::onOutputReconnect(void* data, ::calldata* /*cd*/) {
    auto* self = static_cast<BroadcastEngine*>(data);
    std::lock_guard<std::mutex> lock(self->outputStateLock_);
    self->reconnecting_ = true;
    if (self->outputProblem_.empty()) self->outputProblem_ = "Se cortó la conexión con el servidor";
}

void BroadcastEngine::onOutputReconnected(void* data, ::calldata* /*cd*/) {
    auto* self = static_cast<BroadcastEngine*>(data);
    std::lock_guard<std::mutex> lock(self->outputStateLock_);
    self->reconnecting_ = false;
    self->outputProblem_.clear();
}

void BroadcastEngine::onOutputStopped(void* data, ::calldata* cd) {
    auto* self = static_cast<BroadcastEngine*>(data);
    const long long code = calldata_int(cd, "code");
    std::lock_guard<std::mutex> lock(self->outputStateLock_);
    self->reconnecting_ = false;
    self->outputProblem_ = motivoDeCorte(static_cast<int>(code));
}

void BroadcastEngine::watchOutput(obs_output* outRaw) {
    auto* out = reinterpret_cast<obs_output_t*>(outRaw);
    if (out == nullptr) return;
    signal_handler_t* sh = obs_output_get_signal_handler(out);
    if (sh == nullptr) return;
    signal_handler_connect(sh, "reconnect", &BroadcastEngine::onOutputReconnect, this);
    signal_handler_connect(sh, "reconnect_success", &BroadcastEngine::onOutputReconnected, this);
    signal_handler_connect(sh, "stop", &BroadcastEngine::onOutputStopped, this);
    // Arrancar limpia lo que hubiera quedado del intento anterior.
    std::lock_guard<std::mutex> lock(outputStateLock_);
    reconnecting_ = false;
    outputProblem_.clear();
}

EngineStatus BroadcastEngine::status() const {
    EngineStatus st;
    st.lastError = lastError_;
    if (!started_) {
        return st;
    }
    st.videoActive = obs_get_video() != nullptr;
    st.audioActive = obs_get_audio() != nullptr;
    st.fps = obs_get_active_fps();
    st.videoEncoder = videoEncoderId_;

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
    {
        std::lock_guard<std::mutex> lock(outputStateLock_);
        st.reconnecting = reconnecting_;
        st.outputProblem = outputProblem_;
    }
    return st;
}

} // namespace ba::app
