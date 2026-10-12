// src/app/audio/ObjetivoLoudness.cpp — ver el .hpp

#include "audio/ObjetivoLoudness.hpp"

#include "arranque/ArchivoConfig.hpp"
#include "audio/LoudnessPrograma.hpp"

namespace ba::app::audio {

ObjetivoLoudness::ObjetivoLoudness(std::filesystem::path dataRoot, LoudnessPrograma& loudness)
    : archivo_(std::move(dataRoot) / "Config" / "loudness.json"), loudness_(loudness) {}

std::string ObjetivoLoudness::poner(const nlohmann::json& objetivo) {
    if (std::string e = loudness_.ponerObjetivo(objetivo); !e.empty()) return e;
    // Se guarda lo que quedó (con los lufs/tp del perfil), no lo que vino.
    const auto ob = loudness_.objetivo();
    if (!config::guardar(archivo_, {{"perfil", ob.perfil}, {"lufs", ob.lufs}, {"tp", ob.tp}}))
        return "el objetivo rige, pero no se pudo guardar " + archivo_.filename().string();
    return {};
}

void ObjetivoLoudness::cargar() {
    const nlohmann::json j = config::leerObjeto(archivo_);
    if (!j.empty()) loudness_.ponerObjetivo(j);
}

} // namespace ba::app::audio
