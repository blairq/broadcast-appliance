// ============================================================================
// src/app/emision/VozAlLobby.cpp — ver VozAlLobby.hpp
// ============================================================================
#include "emision/VozAlLobby.hpp"

#include <algorithm>
#include <chrono>
#include <vector>

#include "dsp/Resampler.hpp"
#include "ipc/HealthBlock.hpp"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace ba::app {

namespace {

constexpr int kFrecuencia = 48000;          // la del anillo (y la del gateway)
constexpr std::size_t kBloque = 480;        // 10 ms a 48 kHz (formato del anillo)
constexpr std::size_t kTopeCola = 48000 * 60; // un minuto: más es un error de quien encola

std::uint32_t pidPropio() {
#ifdef _WIN32
    return static_cast<std::uint32_t>(GetCurrentProcessId());
#else
    return static_cast<std::uint32_t>(getpid());
#endif
}

} // namespace

VozAlLobby::~VozAlLobby() { cerrar(); }

void VozAlLobby::abrir() {
    if (hilo_.joinable()) return;
    // El canal se crea una vez y queda: rearmarlo con el mismo nombre y la
    // misma identidad confundiría al lector del gateway (no vería que cambió).
    if (ring_.header() == nullptr &&
        !ba::ipc::SharedAudioRing::init(L"BA.Audio.Atendedor", 2, ba::ipc::audio_ring::kDefaultCapacityBlocks,
                                        pidPropio(), 1, ring_)
             .ok)
        return;
    salir_ = false;
    hilo_ = std::thread([this]() { bucle(); });
}

void VozAlLobby::cerrar() {
    salir_ = true;
    if (hilo_.joinable()) hilo_.join();
    callar();
}

void VozAlLobby::encolar(const float* muestras, std::size_t n, std::uint32_t frecuencia) {
    if (muestras == nullptr || n == 0 || frecuencia == 0) return;
    std::vector<float> a48;
    if (frecuencia == static_cast<std::uint32_t>(kFrecuencia)) {
        a48.assign(muestras, muestras + n);
    } else {
        a48.resize(static_cast<std::size_t>(static_cast<double>(n) * kFrecuencia / frecuencia) + 1024);
        std::size_t hechos = 0;
        ba::dsp::Resampler rs;
        rs.configure(static_cast<int>(frecuencia), kFrecuencia, 1, ba::dsp::Quality::SincMedium);
        if (!rs.resample(muestras, n, a48.data(), a48.size(), &hechos) || hechos == 0) return;
        a48.resize(hechos);
    }
    std::lock_guard<std::mutex> l(lock_);
    if (cola_.size() + a48.size() > kTopeCola) return;
    cola_.insert(cola_.end(), a48.begin(), a48.end());
}

void VozAlLobby::callar() {
    std::lock_guard<std::mutex> l(lock_);
    cola_.clear();
}

double VozAlLobby::pendiente() const {
    std::lock_guard<std::mutex> l(lock_);
    return static_cast<double>(cola_.size()) / kFrecuencia;
}

void VozAlLobby::bucle() {
    using reloj = std::chrono::steady_clock;
    std::vector<float> bloque(kBloque * 2);
    auto proximo = reloj::now();
    while (!salir_.load(std::memory_order_relaxed)) {
        {
            std::lock_guard<std::mutex> l(lock_);
            for (std::size_t i = 0; i < kBloque; ++i) {
                float v = 0.0f;
                if (!cola_.empty()) {
                    v = cola_.front();
                    cola_.pop_front();
                }
                bloque[2 * i] = v; // mono -> los dos canales
                bloque[2 * i + 1] = v;
            }
        }
        ring_.push(bloque.data(), ba::ipc::HealthBlock::nowQpc());
        proximo += std::chrono::milliseconds(10);
        const auto ahora = reloj::now();
        // Si el hilo se atrasó mucho (la máquina se trabó), no se recupera
        // mandando de golpe lo atrasado: se sigue desde ahora.
        if (ahora - proximo > std::chrono::milliseconds(100)) proximo = ahora;
        std::this_thread::sleep_until(proximo);
    }
}

} // namespace ba::app
