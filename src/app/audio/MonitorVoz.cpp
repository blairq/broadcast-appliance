// ============================================================================
// src/app/audio/MonitorVoz.cpp — ver MonitorVoz.hpp
// ============================================================================
#include "audio/MonitorVoz.hpp"
#include "audio/GstVoz.hpp"

#include "emision/FuentesApp.hpp"
#include "comun/HoraMonotonica.hpp"

#include <QCoreApplication>
#include <QMetaObject>
#include <QThread>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <vector>

namespace ba::app::audio {

namespace {
constexpr std::uint32_t kBloque = 1024; // frames por paquete
} // namespace

MonitorVoz::~MonitorVoz() = default;

bool MonitorVoz::asegurar() {
    if (creada_ || pedida_) return true;
    QCoreApplication* app = QCoreApplication::instance();
    if (app == nullptr) return false;
    gstvoz::ponerReloj(&ba::app::ahoraNs);
    // La entrada la crea el motor de la app, en el hilo de la interfaz. Lo
    // encolado espera en la cola de la voz hasta que `bavoz` lo entregue.
    const std::string nombre = nombre_;
    const auto crear = [this, nombre]() {
        std::string nom;
        for (char c : nombre) {
            if (c == '\\' || c == '"') nom += '\\';
            nom += c;
        }
        const nlohmann::json aj{{"pipeline", ""},
                                {"pipeline_audio", "bavoz voz=\"" + nom + "\""},
                                {"audio", true},
                                {"normalizar", false},
                                {"estudio", false},
                                {"voz", nombre}};
        // bavoz, antes de que el motor arme la entrada (si no, avisa «no element» y reintenta).
        gstvoz::registrarElemento();
        // De fábrica: en los auriculares del operador y en ninguna pista (no va
        // al aire). La Matriz le restaura el ruteo que el operador le haya dado.
        const bool ok = fuentes::crearGlobal(nombre, "ba_gst_fuente", aj, true, {});
        std::lock_guard<std::mutex> l(lock_);
        creada_ = ok;
        pedida_ = false; // si no se pudo (sin motor todavía), se reintenta con la próxima frase
    };
    if (QThread::currentThread() == app->thread()) {
        lock_.unlock();
        crear();
        lock_.lock();
    } else {
        pedida_ = true;
        QMetaObject::invokeMethod(app, crear, Qt::QueuedConnection);
    }
    return true;
}

void MonitorVoz::preparar() {
    std::lock_guard<std::mutex> l(lock_);
    asegurar();
}

void MonitorVoz::reproducir(const float* muestras, std::size_t n, std::uint32_t frecuencia) {
    if (muestras == nullptr || n == 0 || frecuencia == 0) return;
    std::lock_guard<std::mutex> l(lock_);
    if (!asegurar()) return;
    // Pegado a lo que ya está sonando; si no suena nada, un poco adelante de ahora.
    const std::uint64_t ahora = ba::app::ahoraNs();
    const std::uint64_t ts = std::max<std::uint64_t>(proximo_, ahora + 50'000'000ULL);
    empujar(muestras, n, frecuencia, ts);
}

void MonitorVoz::reproducirEn(const float* muestras, std::size_t n, std::uint32_t frecuencia,
                              std::uint64_t inicioNs) {
    if (muestras == nullptr || n == 0 || frecuencia == 0) return;
    std::lock_guard<std::mutex> l(lock_);
    if (!asegurar()) return;
    const std::uint64_t ahora = ba::app::ahoraNs();
    // Desde dónde sigue el audio: lo encolado, o un poco adelante de ahora.
    std::uint64_t desde = std::max<std::uint64_t>(proximo_, ahora + 50'000'000ULL);
    // Para que empiece en `inicioNs`, el hueco se llena con silencio (la cola es seguida).
    if (inicioNs > desde) {
        const auto huecos = static_cast<std::size_t>((inicioNs - desde) * frecuencia / 1'000'000'000ULL);
        if (huecos > 0) {
            const std::vector<float> silencio(huecos, 0.0f);
            empujar(silencio.data(), silencio.size(), frecuencia, desde);
            desde = proximo_;
        }
    }
    empujar(muestras, n, frecuencia, desde);
}

void MonitorVoz::empujar(const float* muestras, std::size_t n, std::uint32_t frecuencia, std::uint64_t ts) {
    // A la cola de esta voz: bavoz la entrega a su hora.
    for (std::size_t i = 0; i < n; i += kBloque) {
        const auto frames = static_cast<std::uint32_t>(std::min<std::size_t>(kBloque, n - i));
        gstvoz::encolar(nombre_, muestras + i, frames, frecuencia, ts);
        ts += static_cast<std::uint64_t>(frames) * 1'000'000'000ULL / frecuencia;
    }
    proximo_ = ts;
}

void MonitorVoz::callar() {
    std::lock_guard<std::mutex> l(lock_);
    // Lo que quedaba por decir se tira (la entrada no se rearma: no se pierden
    // su rack ni su ruteo).
    gstvoz::vaciar(nombre_);
    proximo_ = 0;
}

} // namespace ba::app::audio
