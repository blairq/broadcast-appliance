// src/gateway/Fundido.cpp — ver Fundido.hpp

#include "Fundido.hpp"

#include <algorithm>
#include <cstring>

namespace ba::gateway {

float alfaFundido(std::int64_t t0, int durMs, std::int64_t ahora) {
    if (durMs <= 0) return 1.0f;
    float t = static_cast<float>(ahora - t0) / static_cast<float>(durMs);
    t = std::clamp(t, 0.0f, 1.0f);
    return t * t * (3 - 2 * t);
}

void mezclarBgra(std::uint8_t* dst, int dstStride, const std::uint8_t* ant, int antStride, int w, int h, float a) {
    if (dst == nullptr || ant == nullptr || w <= 0 || h <= 0) return;
    // En enteros (0..256): 25 cps x 1280x720 sin coma flotante por píxel.
    const unsigned an = static_cast<unsigned>(std::clamp(a, 0.0f, 1.0f) * 256.0f + 0.5f), av = 256 - an;
    for (int y = 0; y < h; ++y) {
        std::uint8_t* d = dst + static_cast<std::size_t>(y) * dstStride;
        const std::uint8_t* s = ant + static_cast<std::size_t>(y) * antStride;
        for (int i = 0; i < w * 4; ++i) d[i] = static_cast<std::uint8_t>((d[i] * an + s[i] * av) >> 8);
    }
}

void escalarCercano(const std::uint8_t* src, int sw, int sh, std::uint8_t* dst, int dw, int dh) {
    if (src == nullptr || dst == nullptr || sw <= 0 || sh <= 0 || dw <= 0 || dh <= 0) return;
    for (int y = 0; y < dh; ++y) {
        const std::uint8_t* fila = src + static_cast<std::size_t>(y * sh / dh) * sw * 4;
        std::uint8_t* out = dst + static_cast<std::size_t>(y) * dw * 4;
        for (int x = 0; x < dw; ++x) std::memcpy(out + x * 4, fila + static_cast<std::size_t>(x * sw / dw) * 4, 4);
    }
}

void Fundido::empezar(const std::uint8_t* px, int w, int h, int durMs, std::int64_t ahora) {
    if (px == nullptr || w <= 0 || h <= 0 || durMs <= 0) {
        cortar();
        return;
    }
    ant_.assign(px, px + static_cast<std::size_t>(w) * h * 4);
    w_ = w;
    h_ = h;
    t0_ = ahora;
    ms_ = durMs;
    activo_ = true;
}

void Fundido::anterior(const std::uint8_t* px, int w, int h) {
    if (!activo_ || px == nullptr || w <= 0 || h <= 0) return;
    ant_.assign(px, px + static_cast<std::size_t>(w) * h * 4);
    w_ = w;
    h_ = h;
}

bool Fundido::activo(std::int64_t ahora) {
    if (activo_ && ahora - t0_ >= ms_) cortar();
    return activo_;
}

float Fundido::alfa(std::int64_t ahora) const { return activo_ ? alfaFundido(t0_, ms_, ahora) : 1.0f; }

bool Fundido::aplicar(std::uint8_t* px, int stride, int w, int h, std::int64_t ahora) {
    if (!activo(ahora) || px == nullptr || w <= 0 || h <= 0) return false;
    const std::uint8_t* a = ant_.data();
    if (w != w_ || h != h_) {
        escalado_.resize(static_cast<std::size_t>(w) * h * 4);
        escalarCercano(ant_.data(), w_, h_, escalado_.data(), w, h);
        a = escalado_.data();
    }
    mezclarBgra(px, stride, a, w * 4, w, h, alfa(ahora));
    return true;
}

void Fundido::cortar() {
    activo_ = false;
    ant_.clear();
    ant_.shrink_to_fit();
    escalado_.clear();
    escalado_.shrink_to_fit();
    w_ = h_ = 0;
}

} // namespace ba::gateway
