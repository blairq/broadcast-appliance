// src/app/audio/GrabadorWav.cpp — ver GrabadorWav.hpp

#include "audio/GrabadorWav.hpp"
#include "platform/Hilo.hpp"

#include <algorithm>
#include <chrono>
#include <cstring>

namespace ba::app::audio {

namespace {

void u32(std::ofstream& f, std::uint32_t v) {
    const char b[4] = {static_cast<char>(v & 0xFF), static_cast<char>((v >> 8) & 0xFF),
                       static_cast<char>((v >> 16) & 0xFF), static_cast<char>((v >> 24) & 0xFF)};
    f.write(b, 4);
}
void u16(std::ofstream& f, std::uint16_t v) {
    const char b[2] = {static_cast<char>(v & 0xFF), static_cast<char>((v >> 8) & 0xFF)};
    f.write(b, 2);
}

/// Encabezado WAV de 32 bits flotante estéreo (formato 3, IEEE float) con
/// `datos` bytes de audio. 44 bytes + chunk «fact».
void encabezado(std::ofstream& f, std::uint32_t frecuencia, std::uint64_t datos) {
    const std::uint32_t d = static_cast<std::uint32_t>(datos);
    f.write("RIFF", 4);
    u32(f, 4 + (8 + 16) + (8 + 4) + (8 + d));
    f.write("WAVE", 4);
    f.write("fmt ", 4);
    u32(f, 16);
    u16(f, 3); // IEEE float
    u16(f, 2);
    u32(f, frecuencia);
    u32(f, frecuencia * 2 * 4);
    u16(f, 2 * 4);
    u16(f, 32);
    f.write("fact", 4);
    u32(f, 4);
    u32(f, d / 8);
    f.write("data", 4);
    u32(f, d);
}

} // namespace

GrabadorWav::GrabadorWav(std::filesystem::path base, std::uint32_t frecuencia)
    : base_(std::move(base)), frecuencia_(frecuencia == 0 ? 48000 : frecuencia) {}

GrabadorWav::~GrabadorWav() { terminar(); }

std::filesystem::path GrabadorWav::archivo() const {
    std::lock_guard<std::mutex> l(m_);
    return actual_;
}

bool GrabadorWav::abrir(int parte) {
    std::filesystem::path p = base_;
    p += parte == 1 ? ".wav" : "-parte" + std::to_string(parte) + ".wav";
    std::error_code ec;
    std::filesystem::create_directories(p.parent_path(), ec);
    f_.open(p, std::ios::binary | std::ios::trunc);
    if (!f_) return false;
    encabezado(f_, frecuencia_, 0); // se completa al cerrar
    {
        std::lock_guard<std::mutex> l(m_);
        actual_ = p;
    }
    datosArchivo_ = 0;
    parte_ = parte;
    return true;
}

void GrabadorWav::cerrarArchivo() {
    if (!f_.is_open()) return;
    f_.seekp(0);
    encabezado(f_, frecuencia_, datosArchivo_);
    f_.close();
}

std::string GrabadorWav::empezar() {
    if (grabando_) return {};
    if (!abrir(1)) return "no se pudo crear " + (base_.string() + ".wav");
    salir_ = false;
    grabando_ = true;
    hilo_ = std::thread([this]() { ba::platform::nombrarHilo("grabador-wav"); bucle(); });
    return {};
}

void GrabadorWav::terminar() {
    {
        std::lock_guard<std::mutex> l(m_);
        if (!grabando_) return;
        salir_ = true;
    }
    cv_.notify_all();
    if (hilo_.joinable()) hilo_.join();
    cerrarArchivo();
    grabando_ = false;
}

void GrabadorWav::agregar(const float* l, const float* r, std::uint32_t n) {
    if (!grabando_.load(std::memory_order_relaxed) || n == 0) return;
    {
        std::lock_guard<std::mutex> g(m_);
        const std::size_t base = cola_.size();
        cola_.resize(base + 2 * static_cast<std::size_t>(n));
        for (std::uint32_t i = 0; i < n; ++i) {
            cola_[base + 2 * i] = l[i];
            cola_[base + 2 * i + 1] = r[i];
        }
    }
    cv_.notify_one();
}

void GrabadorWav::bucle() {
    std::vector<float> lote;
    for (;;) {
        bool fin;
        {
            std::unique_lock<std::mutex> l(m_);
            cv_.wait_for(l, std::chrono::milliseconds(200), [this]() { return salir_ || !cola_.empty(); });
            lote.swap(cola_);
            fin = salir_;
        }
        std::size_t hecho = 0;
        while (hecho < lote.size()) {
            // ¿Entra en este archivo? Si no, se sigue en la parte siguiente.
            const std::uint64_t lugar = (tope_ - datosArchivo_) / 8 * 2; // en floats (frames enteros)
            if (lugar == 0) {
                cerrarArchivo();
                if (!abrir(parte_ + 1)) {
                    grabando_ = false;
                    return;
                }
                continue;
            }
            const std::size_t va =
                std::min<std::size_t>(lote.size() - hecho, static_cast<std::size_t>(lugar));
            f_.write(reinterpret_cast<const char*>(lote.data() + hecho),
                     static_cast<std::streamsize>(va * sizeof(float)));
            datosArchivo_ += va * sizeof(float);
            frames_ += va / 2;
            hecho += va;
        }
        lote.clear();
        if (fin) return;
    }
}

} // namespace ba::app::audio
