// ============================================================================
// src/app/audio/Habla.cpp — ver Habla.hpp
// ============================================================================
#include "audio/Habla.hpp"

#include <optional>
#include <utility>

#include "sherpa-onnx/c-api/cxx-api.h"

namespace ba::app::audio {

namespace so = sherpa_onnx::cxx;

struct Habla::Motor {
    std::optional<so::OfflineTts> tts; // sin constructor por defecto en sherpa
};

namespace {

/// Lo que viaja por el `void*` del callback de sherpa.
struct EnCurso {
    const Habla::Salida* salida;
    const std::atomic<std::uint64_t>* generacion;
    std::uint64_t mia;
    std::uint32_t frecuencia;
};

std::int32_t alSintetizar(const float* m, std::int32_t n, float, void* arg) {
    auto* e = static_cast<EnCurso*>(arg);
    if (e->generacion->load() != e->mia) return 0; // callar(): cortar acá
    if (*e->salida && n > 0) (*e->salida)(m, static_cast<std::size_t>(n), e->frecuencia);
    return 1;
}

} // namespace

Habla::Habla() : hilo_([this]() { bucle(); }) {}

Habla::~Habla() {
    {
        std::lock_guard<std::mutex> l(colaLock_);
        salir_ = true;
        ++generacion_;
    }
    colaCv_.notify_all();
    if (hilo_.joinable()) hilo_.join();
}

void Habla::encolar(std::function<void()> trabajo) {
    {
        std::lock_guard<std::mutex> l(colaLock_);
        cola_.push_back(std::move(trabajo));
    }
    colaCv_.notify_one();
}

void Habla::bucle() {
    for (;;) {
        std::function<void()> trabajo;
        {
            std::unique_lock<std::mutex> l(colaLock_);
            colaCv_.wait(l, [this]() { return salir_ || !cola_.empty(); });
            if (salir_) break;
            trabajo = std::move(cola_.front());
            cola_.pop_front();
        }
        trabajo();
    }
    motor_.reset();
}

void Habla::cargar(const Voz& v, std::function<void(const std::string&)> listo) {
    encolar([this, v, listo = std::move(listo)]() {
        cargada_ = false;
        motor_.reset();
        so::OfflineTtsConfig c;
        c.model.vits.model = v.modelo;
        c.model.vits.tokens = v.tokens;
        c.model.vits.data_dir = v.datos;
        c.model.num_threads = v.hilos;
        c.model.provider = "cpu";
        c.max_num_sentences = 1; // frase por frase: la primera suena antes
        auto m = std::make_unique<Motor>();
        m->tts.emplace(so::OfflineTts::Create(c));
        if (!m->tts->Get()) {
            if (listo) listo("no se pudo cargar la voz (¿faltan archivos?)");
            return;
        }
        hablantes_ = m->tts->NumSpeakers();
        motor_ = std::move(m);
        cargada_ = true;
        if (listo) listo({});
    });
}

void Habla::decir(const std::string& texto, int sid, float velocidad, Salida salida,
                  std::function<void()> fin) {
    const std::uint64_t mia = generacion_.load();
    encolar([this, texto, sid, velocidad, mia, salida = std::move(salida), fin = std::move(fin)]() {
        if (generacion_.load() == mia && cargada_ && motor_) {
            EnCurso e{&salida, &generacion_, mia, static_cast<std::uint32_t>(motor_->tts->SampleRate())};
            const int hablante = sid >= 0 && sid < hablantes_ ? sid : 0;
            motor_->tts->Generate(texto, hablante, velocidad, &alSintetizar, &e);
        }
        if (fin) fin();
    });
}

void Habla::callar() {
    std::lock_guard<std::mutex> l(colaLock_);
    ++generacion_;
    cola_.clear();
}

} // namespace ba::app::audio
