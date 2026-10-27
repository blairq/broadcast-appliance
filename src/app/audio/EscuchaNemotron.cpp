// ============================================================================
// src/app/audio/EscuchaNemotron.cpp — ver EscuchaNemotron.hpp
// ============================================================================
#include "audio/EscuchaNemotron.hpp"
#include "i18n/I18n.hpp"

#include <map>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include "sherpa-onnx/c-api/cxx-api.h"

namespace ba::app::audio {

namespace so = sherpa_onnx::cxx;

namespace {

class ModeloSherpa : public EscuchaStreaming::Modelo {
   public:
    ModeloSherpa(so::OnlineRecognizer r, std::string idioma) : rec_(std::move(r)), idioma_(std::move(idioma)) {}
    ~ModeloSherpa() override { streams_.clear(); } // antes que el reconocedor

    int nuevoStream() override {
        auto s = std::make_unique<so::OnlineStream>(rec_.CreateStream());
        // El idioma va por stream (es el «prompt» de idioma del modelo).
        if (!idioma_.empty()) s->SetOption("language", idioma_.c_str());
        const int id = ++ultimo_;
        streams_[id] = std::move(s);
        return id;
    }
    void soltarStream(int s) override { streams_.erase(s); }
    void aceptar(int s, const float* m, std::size_t n) override {
        if (auto* st = stream(s)) st->AcceptWaveform(EscuchaStreaming::kFrecuencia, m, static_cast<std::int32_t>(n));
    }
    void terminarEntrada(int s) override {
        if (auto* st = stream(s)) st->InputFinished();
    }
    bool listo(int s) override {
        auto* st = stream(s);
        return st != nullptr && rec_.IsReady(st);
    }
    void decodificar(const std::vector<int>& ids) override {
        // La API de C++ pide los streams contiguos; la de C, punteros: un
        // paso del encoder para todos juntos.
        std::vector<const SherpaOnnxOnlineStream*> ps;
        ps.reserve(ids.size());
        for (int s : ids)
            if (auto* st = stream(s)) ps.push_back(st->Get());
        if (ps.empty()) return;
        SherpaOnnxDecodeMultipleOnlineStreams(rec_.Get(), ps.data(), static_cast<std::int32_t>(ps.size()));
    }
    std::string texto(int s) override {
        auto* st = stream(s);
        return st != nullptr ? rec_.GetResult(st).text : std::string();
    }
    bool finDeTurno(int s) override {
        auto* st = stream(s);
        return st != nullptr && rec_.IsEndpoint(st);
    }
    void reiniciar(int s) override {
        if (auto* st = stream(s)) rec_.Reset(st);
    }
    std::function<std::vector<float>(const float*, std::size_t)> remuestreador(std::uint32_t frecuencia) override {
        if (frecuencia == EscuchaStreaming::kFrecuencia || frecuencia == 0)
            return EscuchaStreaming::Modelo::remuestreador(frecuencia);
        // El de sherpa-onnx, como en Escucha: corte apenas debajo de Nyquist de la salida, 6 ceros.
        auto rs = std::make_shared<so::LinearResampler>(
            so::LinearResampler::Create(static_cast<std::int32_t>(frecuencia), EscuchaStreaming::kFrecuencia,
                                        0.99f * 0.5f * EscuchaStreaming::kFrecuencia, 6));
        return [rs](const float* m, std::size_t n) {
            return rs->Resample(m, static_cast<std::int32_t>(n), false);
        };
    }

   private:
    so::OnlineStream* stream(int s) {
        const auto it = streams_.find(s);
        return it == streams_.end() ? nullptr : it->second.get();
    }
    so::OnlineRecognizer rec_;
    std::string idioma_;
    std::map<int, std::unique_ptr<so::OnlineStream>> streams_;
    int ultimo_ = 0;
};

class DetectorSilero : public EscuchaStreaming::Detector {
   public:
    explicit DetectorSilero(so::VoiceActivityDetector v, std::size_t ventana)
        : vad_(std::move(v)), ventana_(ventana) {}
    std::size_t ventana() const override { return ventana_; }
    bool voz(const float* m) override {
        vad_.AcceptWaveform(m, static_cast<std::int32_t>(ventana_));
        // Los segmentos que junta sherpa-onnx no se usan (el audio va al
        // modelo por otro lado): se tiran para que no crezcan.
        if (!vad_.IsEmpty()) vad_.Clear();
        return vad_.IsDetected();
    }

   private:
    so::VoiceActivityDetector vad_;
    std::size_t ventana_;
};

} // namespace

void cargarNemotron(EscuchaStreaming& e, const ModelosNemotron& m, std::function<void(const std::string&)> listo) {
    EscuchaStreaming::FabricaModelo modelo = [m](std::string& error) -> std::unique_ptr<EscuchaStreaming::Modelo> {
        so::OnlineRecognizerConfig c;
        c.model_config.transducer.encoder = m.encoder;
        c.model_config.transducer.decoder = m.decoder;
        c.model_config.transducer.joiner = m.joiner;
        c.model_config.tokens = m.tokens;
        c.model_config.num_threads = m.hilos;
        c.model_config.provider = "cpu";
        c.decoding_method = "greedy_search";
        // El fin de turno del modelo: regla 1 (nada dicho y mucho silencio) sin
        // tocar, regla 2 (silencio después de hablar) y regla 3 (largo máximo)
        // con lo de la configuración.
        c.enable_endpoint = true;
        c.rule2_min_trailing_silence = static_cast<float>(m.pausaTurno);
        c.rule3_min_utterance_length = static_cast<float>(m.maximoTurno);
        so::OnlineRecognizer r = so::OnlineRecognizer::Create(c);
        if (!r.Get()) {
            error = ba::i18n::traducir("escucha", "no se pudo cargar Nemotron (¿faltan archivos del modelo?)");
            return nullptr;
        }
        return std::make_unique<ModeloSherpa>(std::move(r), m.idioma);
    };
    const std::string vad = m.vad;
    EscuchaStreaming::FabricaDetector detector =
        [vad](const Reconocedor::Cortes& cortes) -> std::unique_ptr<EscuchaStreaming::Detector> {
        if (vad.empty()) return nullptr;
        so::VadModelConfig v;
        v.silero_vad.model = vad;
        // El VAD solo decide qué le llega al modelo: el silencio corto que
        // termina la voz es la pausa de los cortes, y no corta por largo (eso
        // lo hacen el modelo y EscuchaStreaming).
        v.silero_vad.min_silence_duration = static_cast<float>(cortes.pausa);
        v.silero_vad.max_speech_duration = 20.0f;
        v.silero_vad.threshold = static_cast<float>(cortes.umbral);
        v.sample_rate = static_cast<std::int32_t>(EscuchaStreaming::kFrecuencia);
        v.num_threads = 1;
        // El búfer de sherpa-onnx junta la voz en curso (aunque acá no se use):
        // con más que la frase más larga no se agranda a mitad de camino.
        so::VoiceActivityDetector d = so::VoiceActivityDetector::Create(v, 30.0f);
        if (!d.Get()) return nullptr;
        return std::make_unique<DetectorSilero>(std::move(d), static_cast<std::size_t>(v.silero_vad.window_size));
    };
    e.cargar(std::move(modelo), std::move(detector), std::move(listo));
}

} // namespace ba::app::audio
