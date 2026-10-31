// src/app/audio/ObjetivoLoudness.cpp — ver el .hpp

#include "audio/ObjetivoLoudness.hpp"

#include "arranque/ArchivoConfig.hpp"
#include "audio/LoudnessPrograma.hpp"
#include "i18n/I18n.hpp"

namespace ba::app::audio {

ObjetivoLoudness::ObjetivoLoudness(std::filesystem::path dataRoot, LoudnessDe loudnessDe, std::function<void()> alAplicar)
    : archivo_(std::move(dataRoot) / "Config" / "loudness.json"), loudnessDe_(std::move(loudnessDe)),
      alAplicar_(std::move(alAplicar)) {}

ObjetivoLoudness::ObjetivoLoudness(std::filesystem::path dataRoot, LoudnessPrograma& loudness, std::function<void()> alAplicar)
    : ObjetivoLoudness(std::move(dataRoot), [&loudness](const std::string& s) { return s.empty() ? &loudness : nullptr; },
                       std::move(alAplicar)) {}

std::string ObjetivoLoudness::poner(const nlohmann::json& objetivo, const std::string& salida) {
    LoudnessPrograma* l = loudnessDe_(salida);
    if (l == nullptr) return ba::i18n::llenar(ba::i18n::traducir("loudness", "no hay una salida «%1»"), {salida});
    if (std::string e = l->ponerObjetivo(objetivo); !e.empty()) return e;
    // Se guarda lo que quedó (con los lufs/tp del perfil), no lo que vino. La por omisión
    // (pedida por su id o con "") arriba; las demás en su lugar, sin tocar lo de las otras.
    const auto ob = l->objetivo();
    if (alAplicar_) alAplicar_();
    const nlohmann::json guardado{{"perfil", ob.perfil}, {"lufs", ob.lufs}, {"tp", ob.tp}, {"agc", ob.agc}};
    nlohmann::json j = config::leerObjeto(archivo_);
    if (l == loudnessDe_("")) {
        const nlohmann::json otras = j.value("salidas", nlohmann::json::object());
        j = guardado;
        if (!otras.empty()) j["salidas"] = otras;
    } else {
        if (!j.contains("salidas") || !j["salidas"].is_object()) j["salidas"] = nlohmann::json::object();
        j["salidas"][salida] = guardado;
    }
    if (!config::guardar(archivo_, j)) return "el objetivo rige, pero no se pudo guardar " + archivo_.filename().string();
    return {};
}

void ObjetivoLoudness::cargar() {
    const nlohmann::json j = config::leerObjeto(archivo_);
    LoudnessPrograma* omision = loudnessDe_("");
    if (omision != nullptr && !j.empty()) {
        nlohmann::json propio = j;
        propio.erase("salidas");
        if (!propio.empty()) omision->ponerObjetivo(propio);
    }
    // Las demás: la suya si la guardó (si no, siguen con la que tomaron al crearse).
    if (const auto it = j.find("salidas"); it != j.end() && it->is_object())
        for (const auto& [id, o] : it->items())
            if (LoudnessPrograma* l = loudnessDe_(id); l != nullptr && l != omision) l->ponerObjetivo(o);
    if (alAplicar_) alAplicar_(); // también el de fábrica: el techo depende del códec
}

void ObjetivoLoudness::olvidar(const std::string& salida) {
    nlohmann::json j = config::leerObjeto(archivo_);
    const auto it = j.find("salidas");
    if (salida.empty() || it == j.end() || !it->is_object() || !it->contains(salida)) return;
    it->erase(salida);
    if (it->empty()) j.erase("salidas");
    config::guardar(archivo_, j);
}

} // namespace ba::app::audio
