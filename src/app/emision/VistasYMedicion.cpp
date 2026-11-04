// ============================================================================
// src/app/emision/VistasYMedicion.cpp — ver VistasYMedicion.hpp
// ============================================================================

#include "emision/VistasYMedicion.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <thread>

#include "Motor.hpp"
#include "ajustes/Ajustes.hpp"
#include "camaras/SincroLabios.hpp"
#include "diag/Registro.hpp"
#include "emision/EmisionSalidas.hpp"
#include "emision/EscenasYFuentes.hpp"
#include "emision/SalidaMotor.hpp"
#include "i18n/I18nTexto.hpp"

namespace ba::app {

using json = nlohmann::json;

namespace {

std::int64_t ahoraMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

/// BGRA (con su stride) → RGBA empaquetado.
void aRgba(const motor::Cuadro& c, std::vector<std::uint8_t>& dst) {
    dst.resize(static_cast<std::size_t>(c.ancho) * c.alto * 4);
    for (int y = 0; y < c.alto; ++y) {
        const std::uint8_t* s = c.bgra.data() + static_cast<std::size_t>(y) * c.stride;
        std::uint8_t* d = dst.data() + static_cast<std::size_t>(y) * c.ancho * 4;
        for (int x = 0; x < c.ancho; ++x) {
            d[4 * x] = s[4 * x + 2];
            d[4 * x + 1] = s[4 * x + 1];
            d[4 * x + 2] = s[4 * x];
            d[4 * x + 3] = s[4 * x + 3];
        }
    }
}

} // namespace

struct VistasYMedicion::MedicionSincro {
    std::string salida; ///< la que se mide (su id: una cámara contra su micrófono en SU programa)
    std::mutex mu;
    std::vector<camaras::Muestra> audio, video;
    std::vector<std::uint8_t> anterior;
};

bool VistasYMedicion::midiendoSincro() const { return std::atomic_load(&medSincro_) != nullptr; }

void VistasYMedicion::soltarCapturas() {
    std::lock_guard<std::mutex> lc(capturaMu_);
    if (m_.motor_)
        for (const auto& [clave, v] : capturas_) m_.motor_->verSoltar(clave);
    capturas_.clear();
}

void VistasYMedicion::esperarCapturas() {
    // Las capturas que están esperando un cuadro (capturarAparte) usan el motor.
    for (int i = 0; i < 80 && capturasEnCurso_.load() > 0; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(50));
}

void VistasYMedicion::soltarVistasDeEscena() {
    for (const auto& [canal, v] : scenePreviews_) m_.motor_->verSoltar(v.clave);
    const std::lock_guard<std::mutex> l(vistasMu_);
    scenePreviews_.clear();
}

bool VistasYMedicion::enablePreview(int width, int height) {
    if (!m_.started_ || width <= 0 || height <= 0) {
        m_.setError(ba::i18n::traducir("emision", "enablePreview sin startup o con tamaño inválido"));
        return false;
    }
    if (previewActive_) return true;
    const std::string e = m_.motor_->verPedir("programa@ui", width, height);
    if (!e.empty()) {
        m_.setError(e);
        return false;
    }
    const std::lock_guard<std::mutex> l(vistasMu_);
    previewActive_ = true;
    return true;
}

void VistasYMedicion::disablePreview() {
    if (!previewActive_ || !m_.motor_) return;
    m_.motor_->verSoltar("programa@ui");
    const std::lock_guard<std::mutex> l(vistasMu_);
    previewActive_ = false;
}

bool VistasYMedicion::latestPreview(motor::Cuadro& out, std::uint64_t& serial) const {
    // DESDE CUALQUIER HILO (la interfaz, F3-03): con el candado de las vistas, sin el turno.
    const std::lock_guard<std::mutex> l(vistasMu_);
    return previewActive_ && m_.motor_ && m_.motor_->verUltimo("programa@ui", out, serial);
}

bool VistasYMedicion::enableScenePreview(const std::string& canal, const std::string& name, int maxWidth) {
    if (canal == "armada" && m_.started_ && m_.motor_) {
        // La vista del bus de preview de la por omisión (lo armado lo pone el
        // conmutador con armar: acá solo se mira).
        if (name.empty()) {
            disableScenePreview(canal);
            return true;
        }
        auto ya = scenePreviews_.find(canal);
        if (ya != scenePreviews_.end()) {
            const std::lock_guard<std::mutex> l(vistasMu_);
            ya->second.name = name;
            return true;
        }
        const int w = maxWidth > 0 ? std::min(maxWidth, m_.video_.baseWidth) : m_.video_.baseWidth;
        const int h = std::max(2, w * m_.video_.baseHeight / std::max(1, m_.video_.baseWidth));
        VistaPrevia v;
        v.clave = "preview@armada";
        v.name = name;
        const std::string ev = m_.motor_->verPedir(v.clave, w, h);
        if (!ev.empty()) {
            m_.setError(ba::i18n::llenar(ba::i18n::traducir("emision", "vista previa: %1"), {ev}));
            return false;
        }
        {
            const std::lock_guard<std::mutex> l(vistasMu_);
            scenePreviews_[canal] = v;
        }
        medirVista(v.clave);
        return true;
    }
    auto it = scenePreviews_.find(canal);
    if (it != scenePreviews_.end() && it->second.name == name) return true;
    disableScenePreview(canal);
    if (name.empty()) return true;
    if (!m_.started_) {
        m_.setError(ba::i18n::traducir("emision", "vista previa de escena: el motor no arranco"));
        return false;
    }
    // «entera:X»: la entrada X antes del recorte del PTZ digital (la toma entera).
    // «mini:X»: la escena X compuesta aparte a pocos cuadros por segundo (las miniaturas
    // de la línea de tiempo: motor::Motor::kMiniFps).
    const bool entera = name.rfind("entera:", 0) == 0 && m_.escenas_->sourceExists(name.substr(7));
    // Las escenas, por su id (así las conoce el motor); `name` queda como lo pidieron.
    const bool mini = name.rfind("mini:", 0) == 0 && m_.escenas_->escena(name.substr(5)) != nullptr;
    const std::string base = entera                     ? "entrada-entera:" + name.substr(7)
                             : mini                     ? "mini:" + m_.escenas_->idDe(name.substr(5))
                             : m_.escenas_->escena(name) != nullptr  ? "escena:" + m_.escenas_->idDe(name)
                             : m_.escenas_->sourceExists(name) ? "entrada:" + name
                                                        : "";
    if (base.empty()) {
        m_.setError(ba::i18n::llenar(ba::i18n::traducir("emision", "vista previa de escena: no existe '%1'"), {name}));
        return false;
    }
    const int w = maxWidth > 0 ? std::min(maxWidth, m_.video_.baseWidth) : m_.video_.baseWidth;
    const int h = std::max(2, w * m_.video_.baseHeight / std::max(1, m_.video_.baseWidth));
    VistaPrevia v;
    v.clave = base + "@" + canal;
    v.name = name;
    const std::string e = m_.motor_->verPedir(v.clave, w, h);
    if (!e.empty()) {
        m_.setError(ba::i18n::llenar(ba::i18n::traducir("emision", "vista previa: %1"), {e}));
        return false;
    }
    {
        const std::lock_guard<std::mutex> l(vistasMu_);
        scenePreviews_[canal] = v;
    }
    medirVista(v.clave);
    return true;
}

bool VistasYMedicion::enableSalidaPreview(const std::string& canal, const std::string& bus, const std::string& salida,
                                          int maxWidth) {
    const SalidaAire* s = m_.laSalida(salida);
    if (s == nullptr || (bus != "programa" && bus != "preview")) {
        m_.setError(ba::i18n::llenar(ba::i18n::traducir("emision", "vista previa: no hay una salida '%1'"), {salida}));
        return false;
    }
    // El nombre anotado es la salida: si se la borra y vuelve, la vista se vuelve a pedir
    // (FrameService mira scenePreviewName) igual que una entrada.
    const std::string nombre = bus + ":" + s->id;
    auto it = scenePreviews_.find(canal);
    if (it != scenePreviews_.end() && it->second.name == nombre) return true;
    disableScenePreview(canal);
    if (!m_.started_ || !m_.motor_) return false;
    const int w = maxWidth > 0 ? std::min(maxWidth, m_.video_.baseWidth) : m_.video_.baseWidth;
    const int h = std::max(2, w * m_.video_.baseHeight / std::max(1, m_.video_.baseWidth));
    VistaPrevia v;
    // «programa:<id>» y «preview:<id>» son las composiciones de esa salida (motor/Vistas.hpp).
    v.clave = nombre + "@" + canal;
    v.name = nombre;
    if (const std::string e = m_.motor_->verPedir(v.clave, w, h); !e.empty()) {
        m_.setError(ba::i18n::llenar(ba::i18n::traducir("emision", "vista previa: %1"), {e}));
        return false;
    }
    const std::lock_guard<std::mutex> l(vistasMu_);
    scenePreviews_[canal] = v;
    return true;
}

void VistasYMedicion::medirVista(const std::string& clave) {
    // BA_MEDIR_PREVIEW=1: cada cuadro de la vista, con su brillo medio y cuánto
    // tardó desde el anterior, al registro (para encontrar parpadeos: un cuadro
    // negro, a medio armar o que falta alrededor de un TAKE).
    const char* medir = std::getenv("BA_MEDIR_PREVIEW");
    if (medir == nullptr || std::string(medir) != "1" || !m_.motor_) return;
    auto antes = std::make_shared<std::int64_t>(0);
    m_.motor_->verAvisar(clave, [clave, antes](const motor::Cuadro& c) {
        std::uint64_t suma = 0, n = 0;
        for (int y = 0; y < c.alto; y += 8)
            for (int x = 0; x < c.ancho; x += 8) {
                const std::uint8_t* p = c.bgra.data() + static_cast<std::size_t>(y) * c.stride + static_cast<std::size_t>(x) * 4;
                suma += (p[0] + 2 * p[1] + p[2]) / 4;
                ++n;
            }
        const std::int64_t ahora = g_get_monotonic_time();
        const std::int64_t dt = *antes ? (ahora - *antes) / 1000 : 0;
        *antes = ahora;
        ba::diag::registro().info(ba::i18n::llenar(ba::i18n::traducir("emision", "medir %1: brillo %2 dt %3 ms serie %4"), {clave, std::to_string(n ? suma / n : 0), std::to_string(dt), std::to_string(c.serie)}));
    });
}

void VistasYMedicion::disableScenePreview(const std::string& canal) {
    auto it = scenePreviews_.find(canal);
    if (it == scenePreviews_.end()) return;
    if (m_.motor_) m_.motor_->verSoltar(it->second.clave);
    const std::lock_guard<std::mutex> l(vistasMu_);
    scenePreviews_.erase(it);
}

std::string VistasYMedicion::scenePreviewName(const std::string& canal) const {
    const std::lock_guard<std::mutex> l(vistasMu_); // DESDE CUALQUIER HILO
    auto it = scenePreviews_.find(canal);
    return it == scenePreviews_.end() ? std::string() : it->second.name;
}

std::vector<std::string> VistasYMedicion::nombresEnVistas() const {
    const std::lock_guard<std::mutex> l(vistasMu_); // DESDE CUALQUIER HILO
    std::vector<std::string> out;
    out.reserve(scenePreviews_.size());
    for (const auto& [canal, v] : scenePreviews_) out.push_back(v.name);
    return out;
}

bool VistasYMedicion::renderScenePreview(const std::string& canal, motor::Cuadro& out, std::uint64_t& serial) {
    const std::lock_guard<std::mutex> l(vistasMu_); // DESDE CUALQUIER HILO
    auto it = scenePreviews_.find(canal);
    if (it == scenePreviews_.end() || !m_.motor_) return false;
    // Solo si hay uno nuevo: antes daba siempre el último y la interfaz lo
    // copiaba y le daba vuelta los colores 30 veces por segundo aunque fuera el
    // mismo (la serie de las vistas es una sola y solo crece). Y sin copiarlo: la
    // interfaz comparte los bytes con la vista (F3-11).
    return m_.motor_->verUltimo(it->second.clave, out, serial);
}

void VistasYMedicion::capturarAparte(const std::string& name, int maxWidth, BroadcastEngine::Captura listo) {
    std::string clave;
    int w = 0, h = 0;
    bool esEscena = false;
    const std::string e = prepararCaptura(name, maxWidth, clave, w, h, esEscena);
    if (!e.empty()) {
        listo("captura: " + e, {}, 0, 0);
        return;
    }
    ++capturasEnCurso_;
    std::thread([this, clave, w, h, esEscena, listo = std::move(listo)]() {
        std::vector<std::uint8_t> rgba;
        int ancho = 0, alto = 0;
        const std::string err = capturarVista(clave, w, h, esEscena, rgba, ancho, alto);
        listo(err.empty() ? std::string() : "captura: " + err, std::move(rgba), ancho, alto);
        --capturasEnCurso_;
    }).detach();
}

std::string VistasYMedicion::prepararCaptura(const std::string& name, int maxWidth, std::string& clave, int& w, int& h,
                                             bool& esEscena) const {
    if (!m_.started_) return ba::i18n::traducir("emision", "el motor no arranco");
    const std::string base = name.empty()                 ? std::string("programa")
                             : m_.escenas_->escena(name) != nullptr    ? "escena:" + name
                             : m_.escenas_->sourceExists(name)   ? "entrada:" + name
                                                          : std::string();
    if (base.empty()) return ba::i18n::llenar(ba::i18n::traducir("emision", "no existe la fuente o escena '%1'"), {name});
    w = m_.video_.baseWidth;
    h = m_.video_.baseHeight;
    if (!name.empty() && base.rfind("entrada:", 0) == 0) {
        int sw = 0, sh = 0;
        if (m_.escenas_->sourceSize(name, sw, sh) && sw > 0 && sh > 0) {
            w = sw;
            h = sh;
        }
    }
    if (maxWidth > 0 && w > maxWidth) {
        h = std::max(2, h * maxWidth / w);
        w = maxWidth;
    }
    w &= ~1;
    h &= ~1;
    esEscena = base.rfind("escena:", 0) == 0;
    clave = base + "@captura";
    return {};
}

std::string VistasYMedicion::capturarVista(const std::string& clave, int w, int h, bool esEscena,
                                           std::vector<std::uint8_t>& rgba, int& width, int& height) {
    std::lock_guard<std::mutex> lc(capturaMu_);
    const auto ahora = std::chrono::steady_clock::now();
    // Las que nadie pidió en 10 s, afuera.
    for (auto it = capturas_.begin(); it != capturas_.end();)
        if (it->first != clave && ahora - it->second.uso > std::chrono::seconds(ajustes::entero("video.vistaReposoS", 10))) {
            m_.motor_->verSoltar(it->first);
            it = capturas_.erase(it);
        } else {
            ++it;
        }
    // La vista de esta fuente: si ya está con este tamaño, se reusa (sin armar nada).
    auto viva = capturas_.find(clave);
    const bool nueva = viva == capturas_.end() || viva->second.ancho != w || viva->second.alto != h;
    if (nueva) {
        if (viva != capturas_.end()) m_.motor_->verSoltar(clave);
        if (!m_.motor_->verPedir(clave, w, h).empty()) {
            capturas_.erase(clave);
            return ba::i18n::llenar(ba::i18n::traducir("emision", "'%1' no tiene imagen"), {clave.substr(0, clave.rfind('@'))});
        }
        capturas_[clave] = {w, h, ahora};
    }
    capturas_[clave].uso = ahora;
    motor::Cuadro c;
    std::uint64_t s = 0;
    bool ok = false;
    // Una escena que no está al aire se compone recién ahora: sus primeros cuadros
    // pueden salir sin las capas que todavía arrancan (en la GPU, compilar los
    // shaders tarda): se toma un cuadro de después. Una vista ya viva, al toque.
    const auto desde = std::chrono::steady_clock::now();
    const auto espera = nueva && esEscena ? std::chrono::milliseconds(ajustes::entero("video.esperaCapturaMs", 700)) : std::chrono::milliseconds(0);
    for (int i = 0; i < 60; ++i) {
        const bool hay = m_.motor_->verUltimo(clave, c, s);
        ok = ok || hay;
        if (ok && std::chrono::steady_clock::now() - desde >= espera) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    if (!ok) {
        m_.motor_->verSoltar(clave);
        capturas_.erase(clave);
        return ba::i18n::llenar(ba::i18n::traducir("emision", "'%1' no dio imagen (sin señal o apagada)"), {clave.substr(0, clave.rfind('@'))});
    }
    aRgba(c, rgba);
    width = c.ancho;
    height = c.alto;
    return {};
}

void VistasYMedicion::programPeaksDb(float& left, float& right, const std::string& salida) {
    const auto aDb = [](float lin) { return lin <= 0.00001f ? -100.0f : 20.0f * std::log10(lin); };
    const SalidaAire* s = m_.laSalida(salida);
    MedidasSalida& m = *(s != nullptr ? *s : m_.porOmision()).medidas;
    left = aDb(m.picoL.exchange(0.0f));
    right = aDb(m.picoR.exchange(0.0f));
}

float VistasYMedicion::picoProgramaVigiaDb() {
    const float lin = m_.medidasPrograma_->picoVigia.exchange(0.0f);
    return lin <= 0.00001f ? -100.0f : 20.0f * std::log10(lin);
}

BroadcastEngine::ProgramaVideo VistasYMedicion::programaVideo(const std::string& salida) const {
    BroadcastEngine::ProgramaVideo v;
    std::shared_ptr<MedidasSalida> m;
    {
        std::lock_guard<std::mutex> l(m_.salidasM_); // desde cualquier hilo (el vigía de las alarmas)
        const auto it = m_.salidas_.find(salida.empty() ? std::string(BroadcastEngine::kSalidaPorOmision) : salida);
        if (it == m_.salidas_.end()) return v;
        m = it->second->medidas;
    }
    const std::int64_t c = m->cambioMs.load();
    if (c == 0) return v;
    v.valido = true;
    v.luma = m->luma.load();
    v.quietoSeg = static_cast<double>(ahoraMs() - c) / 1000.0;
    return v;
}

std::string VistasYMedicion::claveAlarmas(const SalidaAire& s) {
    return s.porOmision() ? std::string("programa@alarmas") : "programa:" + s.id + "@alarmas";
}

void VistasYMedicion::medirSalida(SalidaAire& s) {
    if (!m_.motor_ || s.oyenteAudio != 0) return;
    // La sincronía labios/voz se mide en el programa de la salida que se eligió al empezar
    // (es una cámara contra su micrófono).
    s.oyenteAudio = m_.motor_->escucharPrograma(
        [this, m = s.medidas, id = s.id](const float* d, std::size_t n) {
            static thread_local std::vector<float> l, r;
            l.resize(n);
            r.resize(n);
            float pl = 0, pr = 0;
            for (std::size_t i = 0; i < n; ++i) {
                l[i] = d[2 * i];
                r[i] = d[2 * i + 1];
                pl = std::max(pl, std::fabs(l[i]));
                pr = std::max(pr, std::fabs(r[i]));
            }
            const float* planos[2] = {l.data(), r.data()};
            m->loudness.alimentar(planos, 2, static_cast<std::uint32_t>(n));
            for (auto [a, v] : {std::pair{&m->picoL, pl}, std::pair{&m->picoR, pr}, std::pair{&m->picoVigia, std::max(pl, pr)}}) {
                float cur = a->load(std::memory_order_relaxed);
                while (v > cur && !a->compare_exchange_weak(cur, v, std::memory_order_relaxed)) {
                }
            }
            if (auto ms = std::atomic_load(&medSincro_); ms && ms->salida == id) {
                // Picos cada 5 ms con la hora en que sonó (ahora es cuando sale).
                const std::int64_t fin = ahoraMs();
                for (std::size_t i = 0; i < n; i += 240) {
                    float p = 0;
                    for (std::size_t k = i; k < std::min(n, i + 240); ++k) p = std::max(p, std::max(std::fabs(l[k]), std::fabs(r[k])));
                    const double db = p > 1e-6f ? 20.0 * std::log10(p) : -120.0;
                    const std::int64_t t = fin - static_cast<std::int64_t>((n - i) * 1000 / 48000);
                    std::lock_guard<std::mutex> lk(ms->mu);
                    ms->audio.push_back({t, db});
                }
            }
        },
        s.id);
    // El video de su programa, chiquito: brillo y quietud (las alarmas de negro y congelado)
    // y, si se mide la sincronía en ella, dónde aplauden.
    const std::string clave = claveAlarmas(s);
    m_.motor_->verPedir(clave, 160, 90);
    m_.motor_->verAvisar(clave, [this, m = s.medidas, id = s.id](const motor::Cuadro& c) {
        ++m->cuadros;
        std::uint64_t suma = 0, firma = 1469598103934665603ULL;
        int n = 0;
        std::vector<std::uint8_t> luma;
        const auto med = std::atomic_load(&medSincro_);
        const bool sincro = med && med->salida == id;
        if (sincro) luma.resize(static_cast<std::size_t>(c.ancho) * c.alto);
        for (int y = 0; y < c.alto; ++y)
            for (int x = 0; x < c.ancho; ++x) {
                const std::uint8_t* px = c.bgra.data() + static_cast<std::size_t>(y) * c.stride + static_cast<std::size_t>(x) * 4;
                const std::uint32_t lu = (px[2] * 54u + px[1] * 183u + px[0] * 19u) >> 8;
                if (sincro) luma[static_cast<std::size_t>(y) * c.ancho + x] = static_cast<std::uint8_t>(lu);
                if (y % 3 == 0 && x % 4 == 0) {
                    suma += lu;
                    ++n;
                    firma = (firma ^ (lu >> 4)) * 1099511628211ULL;
                }
            }
        m->luma = n > 0 ? static_cast<double>(suma) / n : 0;
        if (firma != m->firma.load() || m->cambioMs.load() == 0) {
            m->firma = firma;
            m->cambioMs = ahoraMs();
        }
        if (!sincro) return;
        std::lock_guard<std::mutex> lk(med->mu);
        if (!med->anterior.empty() && med->anterior.size() == luma.size()) {
            // El bloque (16×9) que más cambió: unas manos que aplauden son chicas en el cuadro.
            double maximo = 0;
            for (int by = 0; by + 9 <= c.alto; by += 9)
                for (int bx = 0; bx + 16 <= c.ancho; bx += 16) {
                    int suma16 = 0;
                    for (int y = by; y < by + 9; ++y)
                        for (int x = bx; x < bx + 16; ++x) {
                            const std::size_t i = static_cast<std::size_t>(y) * c.ancho + x;
                            suma16 += std::abs(static_cast<int>(luma[i]) - static_cast<int>(med->anterior[i]));
                        }
                    maximo = std::max(maximo, suma16 / (16.0 * 9.0));
                }
            med->video.push_back({ahoraMs(), maximo});
        }
        med->anterior.swap(luma);
    });
}

void VistasYMedicion::dejarDeMedir(SalidaAire& s) {
    if (s.oyenteAudio != 0 && m_.motor_) {
        m_.motor_->dejarDeEscuchar(s.oyenteAudio);
        m_.motor_->verAvisar(claveAlarmas(s), nullptr);
        m_.motor_->verSoltar(claveAlarmas(s));
    }
    s.oyenteAudio = 0;
}

bool VistasYMedicion::empezarMedicionSincro(const std::string& salida) {
    if (!m_.started_) {
        m_.setError(ba::i18n::traducir("emision", "el motor no está andando"));
        return false;
    }
    const SalidaAire* sal = m_.laSalida(salida);
    if (sal == nullptr) return false;
    if (!std::atomic_load(&medSincro_)) {
        auto m = std::make_shared<MedicionSincro>();
        m->salida = sal->id;
        std::atomic_store(&medSincro_, std::move(m));
    }
    return true;
}

nlohmann::json VistasYMedicion::terminarMedicionSincro() {
    auto m = std::atomic_load(&medSincro_);
    if (!m) return {{"ok", false}, {"problema", ba::i18n::traducir("emision", "no se estaba midiendo")}};
    std::atomic_store(&medSincro_, std::shared_ptr<MedicionSincro>());
    std::vector<camaras::Muestra> a, v;
    {
        std::lock_guard<std::mutex> l(m->mu);
        a.swap(m->audio);
        v.swap(m->video);
    }
    const auto ga = camaras::buscarGolpes(a, 20); // 20 dB sobre el piso del audio
    const auto gv = camaras::buscarGolpes(v, 12); // 12 niveles de luma sobre el piso del bloque
    const camaras::Medicion r = camaras::medirRetardo(ga, gv);
    return {{"ok", r.ok},           {"retardo_ms", r.retardoMs}, {"dispersion_ms", r.dispersionMs},
            {"aplausos", r.aplausos}, {"confiable", r.confiable}, {"problema", r.problema},
            {"golpes_audio", ga.size()}, {"golpes_video", gv.size()}, {"muestras_audio", a.size()},
            {"cuadros", v.size()}};
}

json VistasYMedicion::latenciaEstudio() const {
    json out{{"latencia_ms", 0}, {"tope_ms", 0}, {"automatico", true}, {"tarde", json::array()}, {"entradas", json::array()}};
    if (!m_.motor_) return out;
    motor::Reloj& r = m_.motor_->reloj();
    // La fijada (la que usan todos), lo que piden ahora y quién lo pide.
    out["latencia_ms"] = r.latencia() / 1000000;
    out["objetivo_ms"] = r.objetivo() / 1000000;
    out["la_marca"] = r.laMarca();
    out["puede_bajarse"] = r.puedeBajarse();
    out["baja_sola"] = !m_.emision_->alAire();
    out["tope_ms"] = r.topeMs();
    out["automatico"] = r.topeAutomatico();
    out["margen_ms"] = ajustes::entero("video.margenComposicionMs", 5); // el del video (el audio usa audio.margenMezclaMs)
    // El modo baja latencia (F2-05): el tope que se pone con un botón; lo que pasa de ahí va
    // con su propia demora. Se guarda con el evento, como cualquier tope.
    out["baja_latencia_ms"] = ajustes::entero("estudio.bajaLatenciaMs", 110);
    out["baja_latencia"] = !r.topeAutomatico() && r.topeMs() == out["baja_latencia_ms"].get<int>();
    for (const auto& [quien, ns] : r.anotadas())
        if (r.llegaTarde(ns)) out["tarde"].push_back({{"entrada", quien}, {"latencia_ms", ns / 1000000}});
    // Cada entrada: su clase, lo que pide, lo que tarda y si va con su propia demora.
    const auto foto = m_.motor_->instantanea();
    // Los buses de oído (retornos, escucha en vivo: F2-01) y las entradas que no llegan a tiempo.
    out["oido"] = foto ? foto->value("oido", json::object()) : json::object();
    // Con nombre: items() sobre un json temporal recorre memoria liberada.
    const json ents = foto ? foto->value("entradas", json::object()) : json::object();
    for (const auto& [id, e] : ents.items()) {
            if (!e.is_object() || (!e.value("video", false) && !e.value("audio", false))) continue;
            const json ev = e.value("edad_video", json());
            const json ea = e.value("edad_audio", json());
            const json& edad = ev.is_object() ? ev : ea;
            // La edad real: la medida viene achicada por el retraso propio (se mide después del corrimiento).
            const double propio = e.value("retraso_propio_ms", 0.0);
            out["entradas"].push_back({{"entrada", id},
                                       {"clase", e.value("clase", std::string("sala"))},
                                       {"clase_de_fabrica", e.value("clase_de_fabrica", true)},
                                       {"pide_ms", e.value("pide_ms", 0.0)},
                                       {"edad_p50_ms", edad.is_object() ? edad.value("p50_ms", 0.0) + propio : 0.0},
                                       {"edad_p95_ms", edad.is_object() ? edad.value("p95_ms", 0.0) + propio : 0.0},
                                       {"retraso_propio_ms", e.value("retraso_propio_ms", 0.0)},
                                       {"tiembla_ms", e.value("tiembla_ms", 0.0)},
                                       {"llega_tarde", e.value("llega_tarde", false)},
                                       {"en_escena", e.value("en_escena", false)},
                                       {"audio", e.value("audio", false)},
                                       {"entra_al_oido", e.value("entra_al_oido", true)},
                                       {"video", e.value("video", false)},
                                       {"marca", id == out["la_marca"].get<std::string>()}});
        }
    // Los cuadros que llegaron tarde al compositor del programa en el último minuto (por entrada):
    // si aparecen, el colchón no alcanza.
    int tarde = 0;
    json porEntrada = json::object();
    if (foto)
        for (const auto& c : foto->value("video", json::object()).value("programa", json::object()).value("capas", json::array())) {
            const json el = c.value("edad_llega", json());
            const int t = el.is_object() ? el.value("tarde_60s", 0) : 0;
            tarde += t;
            if (t > 0) porEntrada[c.value("entrada", std::string())] = t;
        }
    out["cuadros_tarde_60s"] = tarde;
    out["cuadros_tarde_por_entrada"] = porEntrada;
    for (auto& e : out["entradas"]) e["cuadros_tarde_60s"] = porEntrada.value(e.value("entrada", std::string()), 0);
    return out;
}

bool VistasYMedicion::bajarLatenciaEstudio() { return m_.motor_ && m_.motor_->bajarLatenciaEstudio(); }

void VistasYMedicion::ponerTopeEstudio(long long ms) {
    if (m_.motor_) m_.motor_->ponerTopeEstudio(ms);
}

void VistasYMedicion::soltarVistasSinMirar(bool on) {
    vistasSinMirar_ = on;
    if (m_.motor_) m_.motor_->vistasSinMirar(on);
}

} // namespace ba::app
