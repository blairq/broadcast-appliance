// ============================================================================
// src/app/emision/MediosApp.cpp — ver MediosApp.hpp
// ============================================================================

#include "emision/MediosApp.hpp"

#include <algorithm>

#include <nlohmann/json.hpp>

#include "Motor.hpp"
#include "emision/EscenasYFuentes.hpp"
#include "emision/InputSpecs.hpp"
#include "i18n/I18n.hpp"

namespace ba::app {

using json = nlohmann::json;

bool MediosApp::playMedia(const std::string& name, const std::string& file, bool loop, float volume) {
    if (file.empty()) {
        m_.setError(ba::i18n::traducir("emision", "no se dijo que archivo reproducir"));
        return false;
    }
    const json s{{"local_file", file}, {"looping", loop}, {"clear_on_media_end", true}};
    if (m_.escenas_->sourceExists(name)) {
        // Ya existe: se le cambia el archivo y arranca de nuevo (otra fuente dejaría la anterior sonando).
        m_.updateSourceSettings(name, s.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace));
        m_.setSourceVolume(name, volume);
        m_.setSourceMuted(name, false);
        return restartMedia(name);
    }
    SourceSpec spec;
    spec.id = inputs::kMedioType;
    spec.name = name;
    spec.settingsJson = s.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
    spec.volume = volume;
    return m_.addGlobalAudio(spec);
}

bool MediosApp::restartMedia(const std::string& name) {
    if (!m_.escenas_->sourceExists(name)) {
        m_.setError(ba::i18n::llenar(ba::i18n::traducir("emision", "no existe la fuente de medios \"%1\""), {name}));
        return false;
    }
    const std::string e = m_.motor_->controlMedio(name, "reiniciar");
    if (!e.empty()) m_.setError(e);
    return e.empty();
}

bool MediosApp::seekMedia(const std::string& name, std::int64_t ms) {
    if (!m_.escenas_->sourceExists(name)) return false;
    return m_.motor_->controlMedio(name, "ir", std::max<std::int64_t>(0, ms)).empty();
}

bool MediosApp::pararMedia(const std::string& name) {
    if (!m_.escenas_->sourceExists(name)) return false;
    return m_.motor_->controlMedio(name, "parar").empty();
}

bool MediosApp::stopMedia(const std::string& name) {
    if (!m_.escenas_->sourceExists(name)) return false;
    m_.motor_->controlMedio(name, "parar");
    return m_.removeGlobalAudio(name);
}

std::vector<BroadcastEngine::MediaInfo> MediosApp::mediaSources() const {
    std::vector<BroadcastEngine::MediaInfo> out;
    if (!m_.motor_) return out;
    for (const auto& n : m_.escenas_->sourceNames()) {
        const json m = m_.motor_->medio(n);
        if (m.is_null()) continue;
        out.push_back({n, m.value("estado", std::string("none")), m.value("duracion_ms", std::int64_t{0}),
                       m.value("tiempo_ms", std::int64_t{0})});
    }
    return out;
}

} // namespace ba::app
