// ============================================================================
// src/app/audio/Escucha.cpp — ver Escucha.hpp
// ============================================================================
#include "audio/Escucha.hpp"

#include <algorithm>
#include <chrono>
#include <optional>
#include <utility>

#include "sherpa-onnx/c-api/cxx-api.h"

namespace ba::app::audio {

namespace so = sherpa_onnx::cxx;

namespace {
constexpr std::int32_t kFrecuenciaModelo = 16000; // Parakeet y Silero esperan 16 kHz
}

struct Escucha::Motor {
    // Sin constructor por defecto en sherpa: se crean al cargar.
    std::optional<so::OfflineRecognizer> reconocedor;
    std::optional<so::VoiceActivityDetector> vad;
    bool conVad = false;
    std::int32_t ventanaVad = 512;
};

struct Escucha::Corriente {
    std::optional<so::VoiceActivityDetector> vad;
    std::optional<so::LinearResampler> rs;
    std::uint32_t frecuencia = 0;
    std::vector<float> resto; // muestras a 16 kHz que no completan una ventana del VAD
    int version = -1;         // de los cortes con que se armó el VAD
    std::size_t hablado = 0;  // muestras de la frase en curso (para el indicador)
};

Escucha::Escucha() : hilo_([this]() { bucle(); }) {}

Escucha::~Escucha() {
    {
        std::lock_guard<std::mutex> l(colaLock_);
        salir_ = true;
    }
    colaCv_.notify_all();
    if (hilo_.joinable()) hilo_.join();
}

void Escucha::encolar(std::function<void()> trabajo) {
    {
        std::lock_guard<std::mutex> l(colaLock_);
        cola_.push_back(std::move(trabajo));
    }
    colaCv_.notify_one();
}

void Escucha::bucle() {
    for (;;) {
        std::function<void()> trabajo;
        {
            std::unique_lock<std::mutex> l(colaLock_);
            // Con corrientes abiertas se despierta solo cada 200 ms a atenderlas.
            colaCv_.wait_for(l, std::chrono::milliseconds(200),
                             [this]() { return salir_ || !cola_.empty(); });
            if (salir_ && cola_.empty()) break;
            if (!cola_.empty()) {
                trabajo = std::move(cola_.front());
                cola_.pop_front();
            }
        }
        if (trabajo) trabajo();
        atenderCorrientes();
    }
    corrientes_.clear();
    motor_.reset(); // los modelos se liberan en el hilo que los creó
}

void Escucha::cargar(const Modelos& m, std::function<void(const std::string&)> listo) {
    encolar([this, m, listo = std::move(listo)]() {
        cargado_ = false;
        motor_.reset();
        so::OfflineRecognizerConfig c;
        c.model_config.transducer.encoder = m.encoder;
        c.model_config.transducer.decoder = m.decoder;
        c.model_config.transducer.joiner = m.joiner;
        c.model_config.tokens = m.tokens;
        c.model_config.model_type = m.modelType;
        c.model_config.num_threads = m.hilos;
        c.model_config.provider = "cpu";
        c.decoding_method = "greedy_search";
        auto motor = std::make_unique<Motor>();
        motor->reconocedor.emplace(so::OfflineRecognizer::Create(c));
        if (!motor->reconocedor->Get()) {
            if (listo) listo("no se pudo cargar el reconocedor (¿faltan archivos del modelo?)");
            return;
        }
        if (!m.vad.empty()) {
            so::VadModelConfig v;
            v.silero_vad.model = m.vad;
            v.silero_vad.min_silence_duration = 0.4f;
            v.silero_vad.max_speech_duration = static_cast<float>(kMaxSegundos);
            v.sample_rate = kFrecuenciaModelo;
            v.num_threads = 1;
            motor->vad.emplace(so::VoiceActivityDetector::Create(v, static_cast<float>(kMaxSegundos) + 5.0f));
            motor->conVad = motor->vad->Get() != nullptr;
            motor->ventanaVad = v.silero_vad.window_size;
        }
        motor_ = std::move(motor);
        corrientes_.clear(); // se rearman con el VAD nuevo
        {
            std::lock_guard<std::mutex> l(contLock_);
            vadModelo_ = m.vad;
        }
        cargado_ = true;
        if (listo)
            listo(m.vad.empty() || motor_->conVad ? std::string()
                                                  : std::string("el VAD no cargó: sin recortar silencios"));
    });
}

void Escucha::empezar() {
    std::lock_guard<std::mutex> l(tomaLock_);
    toma_.clear();
    grabando_ = true;
}

void Escucha::alimentar(const float* muestras, std::size_t n, std::uint32_t frecuencia) {
    if (!grabando_ || muestras == nullptr || n == 0) return;
    std::lock_guard<std::mutex> l(tomaLock_);
    frecuencia_ = frecuencia;
    const std::size_t tope = static_cast<std::size_t>(kMaxSegundos * frecuencia);
    if (toma_.size() >= tope) return;
    toma_.insert(toma_.end(), muestras, muestras + std::min(n, tope - toma_.size()));
}

void Escucha::cancelar() {
    grabando_ = false;
    std::lock_guard<std::mutex> l(tomaLock_);
    toma_.clear();
}

void Escucha::terminar(Listo listo) {
    grabando_ = false;
    std::vector<float> audio;
    std::uint32_t frecuencia;
    {
        std::lock_guard<std::mutex> l(tomaLock_);
        audio.swap(toma_);
        frecuencia = frecuencia_;
    }
    encolar([this, audio = std::move(audio), frecuencia, listo = std::move(listo)]() mutable {
        const Resultado r = transcribir(std::move(audio), frecuencia);
        if (listo) listo(r);
    });
}

void Escucha::transcribir(std::vector<float> audio, std::uint32_t frecuencia, Listo listo) {
    encolar([this, audio = std::move(audio), frecuencia, listo = std::move(listo)]() mutable {
        const Resultado r = transcribir(std::move(audio), frecuencia);
        if (listo) listo(r);
    });
}

void Escucha::setFrase(Frase f) {
    std::lock_guard<std::mutex> l(contLock_);
    frase_ = std::move(f);
}

void Escucha::setCortes(const Cortes& c) {
    std::lock_guard<std::mutex> l(contLock_);
    cortes_.pausa = std::clamp(c.pausa, 0.1, 3.0);
    cortes_.maximo = std::clamp(c.maximo, 2.0, 30.0);
    cortes_.umbral = std::clamp(c.umbral, 0.1, 0.95);
    ++versionCortes_;
}

Escucha::Cortes Escucha::cortes() const {
    std::lock_guard<std::mutex> l(contLock_);
    return cortes_;
}

std::map<std::string, Escucha::Oido> Escucha::oido() const {
    std::lock_guard<std::mutex> l(contLock_);
    return oido_;
}

void Escucha::abrirContinuo(const std::string& clave) {
    std::lock_guard<std::mutex> l(contLock_);
    pendientes_[clave].abierta = true;
}

void Escucha::cerrarContinuo(const std::string& clave) {
    std::lock_guard<std::mutex> l(contLock_);
    if (auto it = pendientes_.find(clave); it != pendientes_.end()) it->second.abierta = false;
}

void Escucha::alimentarContinuo(const std::string& clave, const float* muestras, std::size_t n,
                                std::uint32_t frecuencia) {
    if (muestras == nullptr || n == 0) return;
    std::lock_guard<std::mutex> l(contLock_);
    const auto it = pendientes_.find(clave);
    if (it == pendientes_.end() || !it->second.abierta) return;
    Pendiente& p = it->second;
    // Tope: si el hilo se atrasa (una transcripción larga), no crece sin fin.
    if (p.audio.size() > static_cast<std::size_t>(frecuencia) * 10) return;
    p.frecuencia = frecuencia;
    p.audio.insert(p.audio.end(), muestras, muestras + n);
}

void Escucha::atenderCorrientes() {
    std::map<std::string, Pendiente> lote;
    Frase frase;
    Cortes cortes;
    int version = 0;
    std::string vadModelo;
    {
        std::lock_guard<std::mutex> l(contLock_);
        for (auto it = pendientes_.begin(); it != pendientes_.end();) {
            if (!it->second.abierta) {
                corrientes_.erase(it->first);
                it = pendientes_.erase(it);
                continue;
            }
            if (!it->second.audio.empty()) {
                Pendiente& p = lote[it->first];
                p.frecuencia = it->second.frecuencia;
                p.audio.swap(it->second.audio);
            }
            ++it;
        }
        frase = frase_;
        vadModelo = vadModelo_;
        cortes = cortes_;
        version = versionCortes_;
        for (auto it = oido_.begin(); it != oido_.end();)
            it = pendientes_.count(it->first) ? std::next(it) : oido_.erase(it);
    }
    if (lote.empty() || !cargado_ || !motor_ || vadModelo.empty()) return;
    for (auto& [clave, p] : lote) {
        auto& c = corrientes_[clave];
        if (!c) c = std::make_unique<Corriente>();
        if (c->version != version) {
            // Cortes nuevos (o la primera vez): el VAD se arma con ellos. Al
            // pasar `max_speech_duration`, sherpa-onnx sube el umbral y corta en
            // la próxima caída de la voz: una pausa corta, no a mitad de palabra.
            so::VadModelConfig v;
            v.silero_vad.model = vadModelo;
            v.silero_vad.min_silence_duration = static_cast<float>(cortes.pausa);
            v.silero_vad.max_speech_duration = static_cast<float>(cortes.maximo);
            v.silero_vad.threshold = static_cast<float>(cortes.umbral);
            v.sample_rate = kFrecuenciaModelo;
            c->vad.reset();
            c->vad.emplace(so::VoiceActivityDetector::Create(v, 30.0f));
            c->version = version;
            c->hablado = 0;
            if (!c->vad->Get()) continue;
        }
        if (c->frecuencia != p.frecuencia) {
            c->frecuencia = p.frecuencia;
            c->rs.reset();
            if (p.frecuencia != static_cast<std::uint32_t>(kFrecuenciaModelo))
                c->rs.emplace(so::LinearResampler::Create(static_cast<std::int32_t>(p.frecuencia),
                                                          kFrecuenciaModelo, 0.99f * 0.5f * kFrecuenciaModelo,
                                                          6));
        }
        std::vector<float> a16 =
            c->rs ? c->rs->Resample(p.audio.data(), static_cast<std::int32_t>(p.audio.size()), false)
                  : p.audio;
        c->resto.insert(c->resto.end(), a16.begin(), a16.end());
        const std::size_t w = 512;
        std::size_t usado = 0;
        for (; usado + w <= c->resto.size(); usado += w) {
            c->vad->AcceptWaveform(c->resto.data() + usado, w);
            c->hablado = c->vad->IsDetected() ? c->hablado + w : 0;
        }
        {
            std::lock_guard<std::mutex> l(contLock_);
            oido_[clave] = Oido{c->vad->IsDetected(), static_cast<double>(c->hablado) / kFrecuenciaModelo};
        }
        c->resto.erase(c->resto.begin(), c->resto.begin() + static_cast<std::ptrdiff_t>(usado));
        while (!c->vad->IsEmpty()) {
            const so::SpeechSegment seg = c->vad->Front();
            c->vad->Pop();
            const auto t0 = std::chrono::steady_clock::now();
            Resultado r;
            r.segundosDeAudio = static_cast<double>(seg.samples.size()) / kFrecuenciaModelo;
            so::OfflineStream s = motor_->reconocedor->CreateStream();
            s.AcceptWaveform(kFrecuenciaModelo, seg.samples.data(),
                             static_cast<std::int32_t>(seg.samples.size()));
            motor_->reconocedor->Decode(&s);
            r.texto = motor_->reconocedor->GetResult(&s).text;
            const auto ini = r.texto.find_first_not_of(' ');
            r.texto = ini == std::string::npos ? std::string() : r.texto.substr(ini);
            r.ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
            if (!r.texto.empty() && frase) frase(clave, r);
        }
    }
}

Escucha::Resultado Escucha::transcribir(std::vector<float> audio, std::uint32_t frecuencia) {
    Resultado r;
    if (!cargado_ || !motor_) {
        r.error = "el reconocedor de voz no está cargado";
        return r;
    }
    if (audio.empty()) {
        r.error = "no llegó audio del micrófono";
        return r;
    }
    const auto t0 = std::chrono::steady_clock::now();
    // A 16 kHz, que es lo que esperan los modelos.
    if (frecuencia != static_cast<std::uint32_t>(kFrecuenciaModelo)) {
        // Corte apenas debajo de Nyquist de la salida (8 kHz), 6 ceros.
        so::LinearResampler rs = so::LinearResampler::Create(
            static_cast<std::int32_t>(frecuencia), kFrecuenciaModelo, 0.99f * 0.5f * kFrecuenciaModelo, 6);
        audio = rs.Resample(audio.data(), static_cast<std::int32_t>(audio.size()), true);
    }
    // Solo lo hablado: el VAD saca el silencio de antes, de después y del medio.
    std::vector<float> habla;
    if (motor_->conVad) {
        so::VoiceActivityDetector& vad = *motor_->vad;
        vad.Reset();
        const std::int32_t w = motor_->ventanaVad;
        for (std::size_t i = 0; i + static_cast<std::size_t>(w) <= audio.size();
             i += static_cast<std::size_t>(w))
            vad.AcceptWaveform(audio.data() + i, w);
        vad.Flush();
        while (!vad.IsEmpty()) {
            const so::SpeechSegment s = vad.Front();
            habla.insert(habla.end(), s.samples.begin(), s.samples.end());
            habla.insert(habla.end(), kFrecuenciaModelo / 10, 0.0f); // 100 ms entre tramos
            vad.Pop();
        }
        if (habla.empty()) {
            r.error = "no se escuchó a nadie hablar";
            return r;
        }
    } else {
        habla = std::move(audio);
    }
    r.segundosDeAudio = static_cast<double>(habla.size()) / kFrecuenciaModelo;
    so::OfflineStream s = motor_->reconocedor->CreateStream();
    s.AcceptWaveform(kFrecuenciaModelo, habla.data(), static_cast<std::int32_t>(habla.size()));
    motor_->reconocedor->Decode(&s);
    r.texto = motor_->reconocedor->GetResult(&s).text;
    // El texto empieza con un espacio (es como tokeniza el modelo).
    const auto ini = r.texto.find_first_not_of(' ');
    r.texto = ini == std::string::npos ? std::string() : r.texto.substr(ini);
    r.ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    if (r.texto.empty()) r.error = "no se entendió nada";
    return r;
}

} // namespace ba::app::audio
