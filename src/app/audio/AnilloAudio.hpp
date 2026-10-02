// src/app/audio/AnilloAudio.hpp — el colchón de audio estéreo de la matriz
//
// Un anillo estéreo planar (la toma de una fuente, la salida de un bus) y el
// QIODevice que lo lleva a un dispositivo del equipo. Compartido por la
// Matriz y por EquipoAudio (las salidas físicas de varios canales).

#pragma once

#include "audio/RingEstereo.hpp"

#include <QIODevice>

#include <algorithm>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <vector>

namespace ba::app::audio::anillo {

/// Lo que lee el dispositivo (Qt): float estéreo intercalado desde un Ring.
/// Un anillo a un dispositivo estéreo.
class AlDispositivo : public QIODevice {
   public:
    explicit AlDispositivo(std::shared_ptr<Ring> ring) : ring_(std::move(ring)) { open(QIODevice::ReadOnly); }
    bool isSequential() const override { return true; }

   protected:
    qint64 readData(char* data, qint64 max) override {
        const auto n = static_cast<std::uint32_t>(max / static_cast<qint64>(2 * sizeof(float)));
        if (n == 0) return 0;
        l_.resize(n);
        r_.resize(n);
        ring_->pop(l_.data(), r_.data(), n); // si no hay, silencio
        auto* out = reinterpret_cast<float*>(data);
        for (std::uint32_t i = 0; i < n; ++i) {
            out[2 * i] = l_[i];
            out[2 * i + 1] = r_[i];
        }
        return static_cast<qint64>(n) * 2 * static_cast<qint64>(sizeof(float));
    }
    qint64 writeData(const char*, qint64) override { return -1; }

   private:
    std::shared_ptr<Ring> ring_;
    std::vector<float> l_, r_;
};

/// Varios anillos (pares de canales) a un dispositivo de `canales` canales:
/// el par k va a los canales 2k y 2k+1 (si el último es impar, solo el izquierdo).
class AlDispositivoMulti : public QIODevice {
   public:
    AlDispositivoMulti(std::vector<std::shared_ptr<Ring>> pares, int canales)
        : pares_(std::move(pares)), canales_(canales) {
        open(QIODevice::ReadOnly);
    }
    bool isSequential() const override { return true; }

   protected:
    qint64 readData(char* data, qint64 max) override {
        const auto n = static_cast<std::uint32_t>(max / static_cast<qint64>(canales_ * sizeof(float)));
        if (n == 0) return 0;
        auto* out = reinterpret_cast<float*>(data);
        std::fill(out, out + static_cast<std::size_t>(n) * canales_, 0.0f);
        l_.resize(n);
        r_.resize(n);
        for (std::size_t p = 0; p < pares_.size(); ++p) {
            if (!pares_[p]) continue;
            pares_[p]->pop(l_.data(), r_.data(), n);
            const int c0 = static_cast<int>(2 * p), c1 = c0 + 1;
            for (std::uint32_t i = 0; i < n; ++i) {
                if (c0 < canales_) out[static_cast<std::size_t>(i) * canales_ + c0] = l_[i];
                if (c1 < canales_) out[static_cast<std::size_t>(i) * canales_ + c1] = r_[i];
            }
        }
        return static_cast<qint64>(n) * canales_ * static_cast<qint64>(sizeof(float));
    }
    qint64 writeData(const char*, qint64) override { return -1; }

   private:
    std::vector<std::shared_ptr<Ring>> pares_;
    int canales_;
    std::vector<float> l_, r_;
};

} // namespace ba::app::audio::anillo
